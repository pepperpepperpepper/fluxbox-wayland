#include "wayland/fbwl_ui_menu.h"

#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

static void fbwl_ui_menu_destroy_scene(struct fbwl_menu_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->highlight = NULL;
    free(ui->item_rects);
    ui->item_rects = NULL;
    ui->item_rect_count = 0;
}

void fbwl_ui_menu_close(struct fbwl_menu_ui *ui, const char *why) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open) {
        return;
    }

    fbwl_ui_menu_destroy_scene(ui);
    ui->open = false;
    ui->current = NULL;
    ui->depth = 0;
    ui->selected = 0;
    ui->target_view = NULL;

    wlr_log(WLR_INFO, "Menu: close reason=%s", why != NULL ? why : "(null)");
}

static void fbwl_ui_menu_rebuild(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env) {
    if (env == NULL || env->scene == NULL || env->decor_theme == NULL) {
        return;
    }
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL) {
        return;
    }

    fbwl_ui_menu_destroy_scene(ui);

    struct wlr_scene_tree *parent =
        env->layer_overlay != NULL ? env->layer_overlay : &env->scene->tree;
    ui->tree = wlr_scene_tree_create(parent);
    if (ui->tree == NULL) {
        ui->open = false;
        ui->current = NULL;
        ui->depth = 0;
        ui->selected = 0;
        return;
    }
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);

    const int count = (int)ui->current->item_count;
    const int item_h = ui->item_h > 0 ? ui->item_h : env->decor_theme->title_height;
    const int w = ui->width > 0 ? ui->width : 200;
    const int h = count > 0 ? count * item_h : item_h;

    float bg[4] = {env->decor_theme->titlebar_inactive[0], env->decor_theme->titlebar_inactive[1],
        env->decor_theme->titlebar_inactive[2], 0.95f};
    float hi[4] = {env->decor_theme->titlebar_active[0], env->decor_theme->titlebar_active[1],
        env->decor_theme->titlebar_active[2], 0.95f};

    ui->bg = wlr_scene_rect_create(ui->tree, w, h, bg);
    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, 0, 0);
    }

    if (ui->selected >= ui->current->item_count) {
        ui->selected = ui->current->item_count > 0 ? ui->current->item_count - 1 : 0;
    }
    ui->highlight = wlr_scene_rect_create(ui->tree, w, item_h, hi);
    if (ui->highlight != NULL) {
        wlr_scene_node_set_position(&ui->highlight->node, 0, (int)ui->selected * item_h);
    }

    ui->item_rect_count = ui->current->item_count;
    if (ui->item_rect_count > 0) {
        ui->item_rects = calloc(ui->item_rect_count, sizeof(*ui->item_rects));
        if (ui->item_rects != NULL) {
            float item[4] = {0.00f, 0.00f, 0.00f, 0.01f};
            for (size_t i = 0; i < ui->item_rect_count; i++) {
                ui->item_rects[i] = wlr_scene_rect_create(ui->tree, w, item_h, item);
                if (ui->item_rects[i] != NULL) {
                    wlr_scene_node_set_position(&ui->item_rects[i]->node, 0, (int)i * item_h);
                }
            }
        }
    }

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (size_t i = 0; i < ui->current->item_count; i++) {
        const struct fbwl_menu_item *it = &ui->current->items[i];
        const char *label = it->label != NULL ? it->label : "(no-label)";
        char render_label[512];
        const char *render = label;
        if (it->kind == FBWL_MENU_ITEM_SUBMENU) {
            if (snprintf(render_label, sizeof(render_label), "%s  >", label) >= 0) {
                render = render_label;
            }
        }

        struct wlr_buffer *text_buf = fbwl_text_buffer_create(render, w, item_h, 8, fg);
        if (text_buf != NULL) {
            struct wlr_scene_buffer *sb = wlr_scene_buffer_create(ui->tree, text_buf);
            if (sb != NULL) {
                wlr_scene_node_set_position(&sb->node, 0, (int)i * item_h);
            }
            wlr_buffer_drop(text_buf);
        }
    }

    wlr_scene_node_raise_to_top(&ui->tree->node);
}

void fbwl_ui_menu_open_root(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
        struct fbwl_menu *root_menu, int x, int y) {
    if (ui == NULL || env == NULL || root_menu == NULL) {
        return;
    }

    fbwl_ui_menu_close(ui, "reopen");

    ui->open = true;
    ui->current = root_menu;
    ui->depth = 0;
    ui->stack[0] = root_menu;
    ui->selected = 0;
    ui->target_view = NULL;

    ui->x = x;
    ui->y = y;
    ui->width = 200;
    ui->item_h = env->decor_theme != NULL ? env->decor_theme->title_height : 0;

    fbwl_ui_menu_rebuild(ui, env);
    wlr_log(WLR_INFO, "Menu: open at x=%d y=%d items=%zu", x, y, ui->current->item_count);
}

void fbwl_ui_menu_open_window(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
        struct fbwl_menu *window_menu, struct fbwl_view *view, int x, int y) {
    if (ui == NULL || env == NULL || window_menu == NULL || view == NULL) {
        return;
    }

    fbwl_ui_menu_close(ui, "reopen-window");

    ui->open = true;
    ui->current = window_menu;
    ui->depth = 0;
    ui->stack[0] = window_menu;
    ui->selected = 0;
    ui->target_view = view;

    ui->x = x;
    ui->y = y;
    ui->width = 200;
    ui->item_h = env->decor_theme != NULL ? env->decor_theme->title_height : 0;

    fbwl_ui_menu_rebuild(ui, env);
    wlr_log(WLR_INFO, "Menu: open-window title=%s x=%d y=%d items=%zu",
        fbwl_view_display_title(view), x, y, ui->current->item_count);
}

ssize_t fbwl_ui_menu_index_at(const struct fbwl_menu_ui *ui, int lx, int ly) {
    if (ui == NULL || !ui->open || ui->current == NULL) {
        return -1;
    }
    const int x = lx - ui->x;
    const int y = ly - ui->y;
    const int item_h = ui->item_h > 0 ? ui->item_h : 1;
    const int w = ui->width > 0 ? ui->width : 1;
    const int h = (int)ui->current->item_count * item_h;
    if (x < 0 || x >= w || y < 0 || y >= h) {
        return -1;
    }
    const ssize_t idx = y / item_h;
    if (idx < 0 || (size_t)idx >= ui->current->item_count) {
        return -1;
    }
    return idx;
}

void fbwl_ui_menu_set_selected(struct fbwl_menu_ui *ui, size_t idx) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL) {
        return;
    }
    if (ui->current->item_count == 0) {
        ui->selected = 0;
        return;
    }
    if (idx >= ui->current->item_count) {
        idx = ui->current->item_count - 1;
    }
    ui->selected = idx;
    if (ui->highlight != NULL) {
        const int item_h = ui->item_h > 0 ? ui->item_h : 1;
        wlr_scene_node_set_position(&ui->highlight->node, 0, (int)ui->selected * item_h);
    }
}

static void fbwl_ui_menu_activate_selected(struct fbwl_menu_ui *ui,
        const struct fbwl_ui_menu_env *env, const struct fbwl_ui_menu_hooks *hooks) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL || ui->current->item_count == 0) {
        return;
    }
    if (ui->selected >= ui->current->item_count) {
        ui->selected = ui->current->item_count - 1;
    }

    struct fbwl_menu_item *it = &ui->current->items[ui->selected];
    const char *label = it->label != NULL ? it->label : "(no-label)";

    if (it->kind == FBWL_MENU_ITEM_EXEC) {
        wlr_log(WLR_INFO, "Menu: exec label=%s cmd=%s", label, it->cmd != NULL ? it->cmd : "(null)");
        if (hooks != NULL && hooks->spawn != NULL) {
            hooks->spawn(hooks->userdata, it->cmd);
        }
        fbwl_ui_menu_close(ui, "exec");
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_EXIT) {
        wlr_log(WLR_INFO, "Menu: exit label=%s", label);
        fbwl_ui_menu_close(ui, "exit");
        if (hooks != NULL && hooks->terminate != NULL) {
            hooks->terminate(hooks->userdata);
        }
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_VIEW_ACTION) {
        struct fbwl_view *view = ui->target_view;
        if (view == NULL) {
            fbwl_ui_menu_close(ui, "window-action-no-view");
            return;
        }

        switch (it->view_action) {
        case FBWL_MENU_VIEW_CLOSE:
            wlr_log(WLR_INFO, "Menu: window-close title=%s", fbwl_view_display_title(view));
            if (hooks != NULL && hooks->view_close != NULL) {
                hooks->view_close(hooks->userdata, view);
            }
            fbwl_ui_menu_close(ui, "window-close");
            return;
        case FBWL_MENU_VIEW_TOGGLE_MINIMIZE:
            wlr_log(WLR_INFO, "Menu: window-minimize title=%s", fbwl_view_display_title(view));
            if (hooks != NULL && hooks->view_set_minimized != NULL) {
                hooks->view_set_minimized(hooks->userdata, view, !view->minimized, "window-menu");
            }
            fbwl_ui_menu_close(ui, "window-minimize");
            return;
        case FBWL_MENU_VIEW_TOGGLE_MAXIMIZE:
            wlr_log(WLR_INFO, "Menu: window-maximize title=%s", fbwl_view_display_title(view));
            if (hooks != NULL && hooks->view_set_maximized != NULL) {
                hooks->view_set_maximized(hooks->userdata, view, !view->maximized);
            }
            fbwl_ui_menu_close(ui, "window-maximize");
            return;
        case FBWL_MENU_VIEW_TOGGLE_FULLSCREEN:
            wlr_log(WLR_INFO, "Menu: window-fullscreen title=%s", fbwl_view_display_title(view));
            if (hooks != NULL && hooks->view_set_fullscreen != NULL) {
                hooks->view_set_fullscreen(hooks->userdata, view, !view->fullscreen);
            }
            fbwl_ui_menu_close(ui, "window-fullscreen");
            return;
        default:
            fbwl_ui_menu_close(ui, "window-action-unknown");
            return;
        }
    }
    if (it->kind == FBWL_MENU_ITEM_SUBMENU && it->submenu != NULL) {
        if (ui->depth + 1 < (sizeof(ui->stack) / sizeof(ui->stack[0]))) {
            ui->depth++;
            ui->stack[ui->depth] = it->submenu;
            ui->current = it->submenu;
            ui->selected = 0;
            fbwl_ui_menu_rebuild(ui, env);
            wlr_log(WLR_INFO, "Menu: enter-submenu label=%s items=%zu", label, ui->current->item_count);
        }
        return;
    }
}

bool fbwl_ui_menu_handle_keypress(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
        const struct fbwl_ui_menu_hooks *hooks, xkb_keysym_t sym) {
    if (ui == NULL) {
        return false;
    }
    if (!ui->open) {
        return false;
    }

    if (sym == XKB_KEY_Escape) {
        fbwl_ui_menu_close(ui, "escape");
        return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        fbwl_ui_menu_activate_selected(ui, env, hooks);
        return true;
    }
    if (sym == XKB_KEY_Down) {
        fbwl_ui_menu_set_selected(ui, ui->selected + 1);
        return true;
    }
    if (sym == XKB_KEY_Up) {
        size_t idx = ui->selected;
        if (idx > 0) {
            idx--;
        }
        fbwl_ui_menu_set_selected(ui, idx);
        return true;
    }
    if (sym == XKB_KEY_Left || sym == XKB_KEY_BackSpace) {
        if (ui->depth > 0) {
            ui->depth--;
            ui->current = ui->stack[ui->depth];
            ui->selected = 0;
            fbwl_ui_menu_rebuild(ui, env);
            wlr_log(WLR_INFO, "Menu: back items=%zu", ui->current != NULL ? ui->current->item_count : 0);
        } else {
            fbwl_ui_menu_close(ui, "back");
        }
        return true;
    }

    return false;
}

bool fbwl_ui_menu_handle_click(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
        const struct fbwl_ui_menu_hooks *hooks, int lx, int ly, uint32_t button) {
    if (ui == NULL) {
        return false;
    }
    if (!ui->open || ui->current == NULL) {
        return false;
    }

    const ssize_t idx = fbwl_ui_menu_index_at(ui, lx, ly);
    if (idx < 0) {
        fbwl_ui_menu_close(ui, "click-outside");
        return true;
    }

    fbwl_ui_menu_set_selected(ui, (size_t)idx);
    if (button == BTN_LEFT) {
        fbwl_ui_menu_activate_selected(ui, env, hooks);
    } else if (button == BTN_RIGHT) {
        fbwl_ui_menu_close(ui, "right-click");
    }
    return true;
}

