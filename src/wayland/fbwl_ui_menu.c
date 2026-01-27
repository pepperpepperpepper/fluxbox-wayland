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

#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

static void fbwl_ui_menu_cancel_submenu_timer(struct fbwl_menu_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->submenu_timer != NULL) {
        wl_event_source_remove(ui->submenu_timer);
        ui->submenu_timer = NULL;
    }
    ui->submenu_pending_idx = 0;
}

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
    fbwl_ui_menu_cancel_submenu_timer(ui);
    ui->open = false;
    ui->current = NULL;
    ui->depth = 0;
    ui->selected = 0;
    ui->target_view = NULL;
    ui->env = (struct fbwl_ui_menu_env){0};
    ui->hovered_idx = -1;

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
        if (it->kind == FBWL_MENU_ITEM_SEPARATOR) {
            float sep[4] = {1.0f, 1.0f, 1.0f, 0.30f};
            struct wlr_scene_rect *line = wlr_scene_rect_create(ui->tree, w, 1, sep);
            if (line != NULL) {
                wlr_scene_node_set_position(&line->node, 0, (int)i * item_h + item_h / 2);
            }
            continue;
        }
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

static void fbwl_ui_menu_enter_submenu(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env, size_t idx,
        const char *reason, int delay_ms) {
    if (ui == NULL || env == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL) {
        return;
    }
    if (idx >= ui->current->item_count) {
        return;
    }

    struct fbwl_menu_item *it = &ui->current->items[idx];
    if (it->kind != FBWL_MENU_ITEM_SUBMENU || it->submenu == NULL) {
        return;
    }

    if (ui->depth + 1 >= (sizeof(ui->stack) / sizeof(ui->stack[0]))) {
        return;
    }

    fbwl_ui_menu_cancel_submenu_timer(ui);

    const char *label = it->label != NULL ? it->label : "(no-label)";

    ui->depth++;
    ui->stack[ui->depth] = it->submenu;
    ui->current = it->submenu;
    ui->selected = 0;
    ui->hovered_idx = -1;
    fbwl_ui_menu_rebuild(ui, env);
    if (delay_ms >= 0) {
        wlr_log(WLR_INFO, "Menu: enter-submenu reason=%s delay_ms=%d label=%s items=%zu",
            reason != NULL ? reason : "(null)", delay_ms, label, ui->current->item_count);
    } else {
        wlr_log(WLR_INFO, "Menu: enter-submenu reason=%s label=%s items=%zu",
            reason != NULL ? reason : "(null)", label, ui->current->item_count);
    }
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
    ui->env = *env;
    ui->hovered_idx = -1;
    fbwl_ui_menu_cancel_submenu_timer(ui);

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
    ui->env = *env;
    ui->hovered_idx = -1;
    fbwl_ui_menu_cancel_submenu_timer(ui);

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

static int fbwl_ui_menu_submenu_timer(void *data) {
    struct fbwl_menu_ui *ui = data;
    if (ui == NULL || !ui->open || ui->current == NULL) {
        return 0;
    }

    const size_t idx = ui->submenu_pending_idx;
    const int delay_ms = ui->menu_delay_ms;

    fbwl_ui_menu_cancel_submenu_timer(ui);

    if (ui->hovered_idx < 0 || (size_t)ui->hovered_idx != idx) {
        return 0;
    }
    if (idx >= ui->current->item_count) {
        return 0;
    }
    if (ui->env.wl_display == NULL) {
        return 0;
    }

    fbwl_ui_menu_enter_submenu(ui, &ui->env, idx, "delay", delay_ms);
    return 0;
}

void fbwl_ui_menu_handle_motion(struct fbwl_menu_ui *ui, int lx, int ly) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL) {
        return;
    }

    const ssize_t idx = fbwl_ui_menu_index_at(ui, lx, ly);
    if (idx < 0) {
        ui->hovered_idx = -1;
        fbwl_ui_menu_cancel_submenu_timer(ui);
        return;
    }

    if (ui->hovered_idx != idx) {
        ui->hovered_idx = idx;
        fbwl_ui_menu_set_selected(ui, (size_t)idx);
    }

    if (ui->env.wl_display == NULL) {
        return;
    }

    const size_t hovered = (size_t)idx;
    if (hovered >= ui->current->item_count) {
        return;
    }

    const struct fbwl_menu_item *it = &ui->current->items[hovered];
    if (it->kind != FBWL_MENU_ITEM_SUBMENU || it->submenu == NULL) {
        fbwl_ui_menu_cancel_submenu_timer(ui);
        return;
    }

    if (ui->submenu_timer != NULL && ui->submenu_pending_idx == hovered) {
        return;
    }

    fbwl_ui_menu_cancel_submenu_timer(ui);

    int delay_ms = ui->menu_delay_ms;
    if (delay_ms < 0) {
        delay_ms = 0;
    }
    if (delay_ms == 0) {
        fbwl_ui_menu_enter_submenu(ui, &ui->env, hovered, "hover", delay_ms);
        return;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(ui->env.wl_display);
    if (loop == NULL) {
        return;
    }

    ui->submenu_pending_idx = hovered;
    ui->submenu_timer = wl_event_loop_add_timer(loop, fbwl_ui_menu_submenu_timer, ui);
    if (ui->submenu_timer == NULL) {
        ui->submenu_pending_idx = 0;
        return;
    }

    wl_event_source_timer_update(ui->submenu_timer, delay_ms);
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

    if (it->kind == FBWL_MENU_ITEM_SEPARATOR || it->kind == FBWL_MENU_ITEM_NOP) {
        return;
    }
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
        fbwl_ui_menu_enter_submenu(ui, env, ui->selected, "activate", -1);
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
