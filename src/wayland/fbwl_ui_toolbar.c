#include "wayland/fbwl_ui_toolbar.h"

#include "wmcore/fbwm_core.h"
#ifdef HAVE_SYSTEMD
#include "wayland/fbwl_sni_tray.h"
#endif
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_view.h"

#include <linux/input-event-codes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

static void fbwl_ui_toolbar_destroy_scene(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->clock_timer != NULL) {
        wl_event_source_remove(ui->clock_timer);
        ui->clock_timer = NULL;
    }
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->highlight = NULL;
    free(ui->cells);
    ui->cells = NULL;
    free(ui->labels);
    ui->labels = NULL;
    ui->cell_count = 0;

    free(ui->iconbar_views);
    ui->iconbar_views = NULL;
    free(ui->iconbar_item_lx);
    ui->iconbar_item_lx = NULL;
    free(ui->iconbar_item_w);
    ui->iconbar_item_w = NULL;
    free(ui->iconbar_items);
    ui->iconbar_items = NULL;
    free(ui->iconbar_labels);
    ui->iconbar_labels = NULL;
    ui->iconbar_count = 0;

    if (ui->tray_ids != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_ids[i]);
        }
    }
    free(ui->tray_ids);
    ui->tray_ids = NULL;

    if (ui->tray_services != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_services[i]);
        }
    }
    free(ui->tray_services);
    ui->tray_services = NULL;

    if (ui->tray_paths != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_paths[i]);
        }
    }
    free(ui->tray_paths);
    ui->tray_paths = NULL;

    free(ui->tray_item_lx);
    ui->tray_item_lx = NULL;
    free(ui->tray_item_w);
    ui->tray_item_w = NULL;
    free(ui->tray_rects);
    ui->tray_rects = NULL;
    free(ui->tray_icons);
    ui->tray_icons = NULL;
    ui->tray_count = 0;
    ui->tray_x = 0;
    ui->tray_w = 0;
    ui->tray_icon_w = 0;

    ui->clock_label = NULL;
    ui->clock_text[0] = '\0';
}

void fbwl_ui_toolbar_destroy(struct fbwl_toolbar_ui *ui) {
    fbwl_ui_toolbar_destroy_scene(ui);
}

static void fbwl_ui_toolbar_update_current(struct fbwl_toolbar_ui *ui, struct fbwm_core *wm) {
    if (ui == NULL || wm == NULL) {
        return;
    }
    if (!ui->enabled || ui->highlight == NULL || ui->cell_w < 1 || ui->cell_count < 1) {
        return;
    }

    int cur = fbwm_core_workspace_current(wm);
    if (cur < 0) {
        cur = 0;
    }
    if ((size_t)cur >= ui->cell_count) {
        cur = (int)ui->cell_count - 1;
    }
    wlr_scene_node_set_position(&ui->highlight->node, cur * ui->cell_w, 0);
}

static void fbwl_ui_toolbar_clock_render(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (!ui->enabled || ui->tree == NULL || ui->clock_label == NULL || ui->clock_w < 1 || ui->height < 1) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL) {
        return;
    }

    char text[sizeof(ui->clock_text)];
    if (strftime(text, sizeof(text), "%H:%M", &tm) == 0) {
        return;
    }

    if (strcmp(text, ui->clock_text) == 0) {
        return;
    }

    strncpy(ui->clock_text, text, sizeof(ui->clock_text));
    ui->clock_text[sizeof(ui->clock_text) - 1] = '\0';

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct wlr_buffer *buf = fbwl_text_buffer_create(ui->clock_text, ui->clock_w, ui->height, 8, fg);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(ui->clock_label, NULL);
        return;
    }

    wlr_scene_buffer_set_buffer(ui->clock_label, buf);
    wlr_buffer_drop(buf);
    wlr_log(WLR_INFO, "Toolbar: clock text=%s", ui->clock_text);
}

static int fbwl_ui_toolbar_clock_timer(void *data) {
    struct fbwl_toolbar_ui *ui = data;
    if (ui == NULL) {
        return 0;
    }

    fbwl_ui_toolbar_clock_render(ui);

    if (ui->clock_timer != NULL) {
        wl_event_source_timer_update(ui->clock_timer, 1000);
    }
    return 0;
}

void fbwl_ui_toolbar_update_iconbar_focus(struct fbwl_toolbar_ui *ui, const struct fbwl_decor_theme *decor_theme,
        const struct fbwl_view *focused_view) {
    if (ui == NULL || decor_theme == NULL) {
        return;
    }
    if (!ui->enabled || ui->tree == NULL || ui->iconbar_count < 1 || ui->iconbar_items == NULL ||
            ui->iconbar_views == NULL) {
        return;
    }

    float active[4] = {decor_theme->titlebar_active[0], decor_theme->titlebar_active[1],
        decor_theme->titlebar_active[2], 0.65f};
    float inactive[4] = {0.00f, 0.00f, 0.00f, 0.01f};

    for (size_t i = 0; i < ui->iconbar_count; i++) {
        if (ui->iconbar_items[i] == NULL) {
            continue;
        }
        const bool focused = ui->iconbar_views[i] != NULL && ui->iconbar_views[i] == focused_view;
        wlr_scene_rect_set_color(ui->iconbar_items[i], focused ? active : inactive);
    }
}

void fbwl_ui_toolbar_rebuild(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env) {
    if (ui == NULL || env == NULL || env->scene == NULL || env->decor_theme == NULL || env->wm == NULL) {
        return;
    }

    ui->enabled = true;

    fbwl_ui_toolbar_destroy_scene(ui);

    if (!ui->enabled) {
        return;
    }

    struct wlr_scene_tree *parent =
        env->layer_top != NULL ? env->layer_top : &env->scene->tree;
    ui->tree = wlr_scene_tree_create(parent);
    if (ui->tree == NULL) {
        return;
    }

    ui->x = 0;
    ui->y = 0;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);

    int workspaces = fbwm_core_workspace_count(env->wm);
    if (workspaces < 1) {
        workspaces = 1;
    }

    const int h = env->decor_theme->title_height > 0 ? env->decor_theme->title_height : 24;
    int cell_w = h * 2;
    if (cell_w < 32) {
        cell_w = 32;
    }
    if (cell_w > 256) {
        cell_w = 256;
    }

    ui->height = h;
    ui->cell_w = cell_w;
    ui->cell_count = (size_t)workspaces;
    ui->ws_width = (int)ui->cell_count * ui->cell_w;

    int output_w = 0;
    if (env->output_layout != NULL) {
        struct wlr_output *out = wlr_output_layout_get_center_output(env->output_layout);
        if (out != NULL) {
            struct wlr_box box = {0};
            wlr_output_layout_get_box(env->output_layout, out, &box);
            output_w = box.width;
        }
    }

    ui->width = output_w > ui->ws_width ? output_w : ui->ws_width;
    ui->clock_w = 120;
    const int avail_right = ui->width - ui->ws_width;
    if (avail_right < 1) {
        ui->clock_w = 0;
    } else if (ui->clock_w > avail_right) {
        ui->clock_w = avail_right;
    }
    if (ui->clock_w < 0) {
        ui->clock_w = 0;
    }
    ui->clock_x = ui->width - ui->clock_w;

    size_t tray_count = 0;
#ifdef HAVE_SYSTEMD
    {
        if (env->sni != NULL && env->sni->items.prev != NULL && env->sni->items.next != NULL) {
            struct fbwl_sni_item *item;
            wl_list_for_each(item, &env->sni->items, link) {
                if (item->status != FBWL_SNI_STATUS_PASSIVE) {
                    tray_count++;
                }
            }
        }
    }
#endif

    ui->tray_x = ui->clock_x;
    ui->tray_w = 0;
    ui->tray_icon_w = 0;
    ui->tray_count = 0;

    int avail_mid = ui->width - ui->ws_width - ui->clock_w;
    if (avail_mid < 0) {
        avail_mid = 0;
    }

    int tray_icon_w = ui->height;
    if (tray_icon_w < 1) {
        tray_icon_w = 1;
    }

    if (tray_count > 0 && avail_mid > 0) {
        size_t max_fit = (size_t)(avail_mid / tray_icon_w);
        if (tray_count > max_fit) {
            tray_count = max_fit;
        }
        if (tray_count > 0) {
            ui->tray_icon_w = tray_icon_w;
            ui->tray_w = (int)tray_count * ui->tray_icon_w;
            ui->tray_x = ui->clock_x - ui->tray_w;
            ui->tray_count = tray_count;
        }
    }

    ui->iconbar_x = ui->ws_width;
    ui->iconbar_w = ui->tray_x - ui->iconbar_x;
    if (ui->iconbar_w < 0) {
        ui->iconbar_w = 0;
    }

    float bg[4] = {env->decor_theme->titlebar_inactive[0], env->decor_theme->titlebar_inactive[1],
        env->decor_theme->titlebar_inactive[2], 0.85f};
    float hi[4] = {env->decor_theme->titlebar_active[0], env->decor_theme->titlebar_active[1],
        env->decor_theme->titlebar_active[2], 0.85f};
    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    ui->bg = wlr_scene_rect_create(ui->tree, ui->width, ui->height, bg);
    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, 0, 0);
    }

    ui->highlight = wlr_scene_rect_create(ui->tree, ui->cell_w, ui->height, hi);
    if (ui->highlight != NULL) {
        fbwl_ui_toolbar_update_current(ui, env->wm);
    }

    if (ui->cell_count > 0) {
        ui->cells = calloc(ui->cell_count, sizeof(*ui->cells));
        ui->labels = calloc(ui->cell_count, sizeof(*ui->labels));

        if (ui->cells != NULL) {
            float item[4] = {0.00f, 0.00f, 0.00f, 0.01f};
            for (size_t i = 0; i < ui->cell_count; i++) {
                ui->cells[i] = wlr_scene_rect_create(ui->tree, ui->cell_w, ui->height, item);
                if (ui->cells[i] != NULL) {
                    wlr_scene_node_set_position(&ui->cells[i]->node, (int)i * ui->cell_w, 0);
                }
            }
        }

        for (size_t i = 0; i < ui->cell_count; i++) {
            char label[16];
            if (snprintf(label, sizeof(label), "%zu", i + 1) < 0) {
                continue;
            }
            struct wlr_buffer *buf = fbwl_text_buffer_create(label, ui->cell_w, ui->height, 8, fg);
            if (buf != NULL) {
                struct wlr_scene_buffer *sb = wlr_scene_buffer_create(ui->tree, buf);
                if (sb != NULL) {
                    wlr_scene_node_set_position(&sb->node, (int)i * ui->cell_w, 0);
                    if (ui->labels != NULL) {
                        ui->labels[i] = sb;
                    }
                }
                wlr_buffer_drop(buf);
            }
        }
    }

    const int cur_ws = fbwm_core_workspace_current(env->wm);
    size_t icon_count = 0;
    for (struct fbwm_view *wm_view = env->wm->views.next; wm_view != &env->wm->views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped) {
            continue;
        }
        if (!wm_view->sticky && wm_view->workspace != cur_ws) {
            continue;
        }
        icon_count++;
    }

    if (ui->iconbar_w > 0 && icon_count > 0) {
        ui->iconbar_views = calloc(icon_count, sizeof(*ui->iconbar_views));
        ui->iconbar_item_lx = calloc(icon_count, sizeof(*ui->iconbar_item_lx));
        ui->iconbar_item_w = calloc(icon_count, sizeof(*ui->iconbar_item_w));
        ui->iconbar_items = calloc(icon_count, sizeof(*ui->iconbar_items));
        ui->iconbar_labels = calloc(icon_count, sizeof(*ui->iconbar_labels));

        if (ui->iconbar_views == NULL || ui->iconbar_item_lx == NULL || ui->iconbar_item_w == NULL ||
                ui->iconbar_items == NULL || ui->iconbar_labels == NULL) {
            free(ui->iconbar_views);
            ui->iconbar_views = NULL;
            free(ui->iconbar_item_lx);
            ui->iconbar_item_lx = NULL;
            free(ui->iconbar_item_w);
            ui->iconbar_item_w = NULL;
            free(ui->iconbar_items);
            ui->iconbar_items = NULL;
            free(ui->iconbar_labels);
            ui->iconbar_labels = NULL;
            ui->iconbar_count = 0;
        } else {
            ui->iconbar_count = icon_count;
            int xoff = ui->iconbar_x;
            const int base_w = ui->iconbar_w / (int)icon_count;
            const int rem = ui->iconbar_w % (int)icon_count;

            size_t idx = 0;
            for (struct fbwm_view *wm_view = env->wm->views.next;
                    wm_view != &env->wm->views;
                    wm_view = wm_view->next) {
                struct fbwl_view *view = wm_view->userdata;
                if (view == NULL || !view->mapped) {
                    continue;
                }
                if (!wm_view->sticky && wm_view->workspace != cur_ws) {
                    continue;
                }
                if (idx >= icon_count) {
                    break;
                }

                int iw = base_w + ((int)idx < rem ? 1 : 0);
                if (iw < 1) {
                    iw = 1;
                }

                ui->iconbar_views[idx] = view;
                ui->iconbar_item_lx[idx] = xoff;
                ui->iconbar_item_w[idx] = iw;

                float item[4] = {0.00f, 0.00f, 0.00f, 0.01f};
                ui->iconbar_items[idx] = wlr_scene_rect_create(ui->tree, iw, ui->height, item);
                if (ui->iconbar_items[idx] != NULL) {
                    wlr_scene_node_set_position(&ui->iconbar_items[idx]->node, xoff, 0);
                }

                struct wlr_buffer *buf = fbwl_text_buffer_create(fbwl_view_display_title(view), iw, ui->height, 8, fg);
                if (buf != NULL) {
                    struct wlr_scene_buffer *sb = wlr_scene_buffer_create(ui->tree, buf);
                    if (sb != NULL) {
                        wlr_scene_node_set_position(&sb->node, xoff, 0);
                        ui->iconbar_labels[idx] = sb;
                    }
                    wlr_buffer_drop(buf);
                }

                wlr_log(WLR_INFO, "Toolbar: iconbar item idx=%zu lx=%d w=%d title=%s minimized=%d",
                    idx, xoff, iw, fbwl_view_display_title(view), view->minimized ? 1 : 0);

                xoff += iw;
                idx++;
            }
            ui->iconbar_count = idx;
        }
    }

    if (ui->tray_count > 0 && ui->tray_w > 0 && ui->tray_icon_w > 0) {
        ui->tray_ids = calloc(ui->tray_count, sizeof(*ui->tray_ids));
        ui->tray_services = calloc(ui->tray_count, sizeof(*ui->tray_services));
        ui->tray_paths = calloc(ui->tray_count, sizeof(*ui->tray_paths));
        ui->tray_item_lx = calloc(ui->tray_count, sizeof(*ui->tray_item_lx));
        ui->tray_item_w = calloc(ui->tray_count, sizeof(*ui->tray_item_w));
        ui->tray_rects = calloc(ui->tray_count, sizeof(*ui->tray_rects));
        ui->tray_icons = calloc(ui->tray_count, sizeof(*ui->tray_icons));

        if (ui->tray_ids == NULL || ui->tray_services == NULL || ui->tray_paths == NULL ||
                ui->tray_item_lx == NULL || ui->tray_item_w == NULL || ui->tray_rects == NULL ||
                ui->tray_icons == NULL) {
            if (ui->tray_ids != NULL) {
                for (size_t i = 0; i < ui->tray_count; i++) {
                    free(ui->tray_ids[i]);
                }
            }
            free(ui->tray_ids);
            ui->tray_ids = NULL;

            if (ui->tray_services != NULL) {
                for (size_t i = 0; i < ui->tray_count; i++) {
                    free(ui->tray_services[i]);
                }
            }
            free(ui->tray_services);
            ui->tray_services = NULL;

            if (ui->tray_paths != NULL) {
                for (size_t i = 0; i < ui->tray_count; i++) {
                    free(ui->tray_paths[i]);
                }
            }
            free(ui->tray_paths);
            ui->tray_paths = NULL;

            free(ui->tray_item_lx);
            ui->tray_item_lx = NULL;
            free(ui->tray_item_w);
            ui->tray_item_w = NULL;
            free(ui->tray_rects);
            ui->tray_rects = NULL;
            free(ui->tray_icons);
            ui->tray_icons = NULL;
            ui->tray_count = 0;
        } else {
            int xoff = ui->tray_x;
            const int pad = ui->height >= 8 ? 2 : 0;
            int size = ui->height - 2 * pad;
            if (size < 1) {
                size = 1;
            }

            float item[4] = {env->decor_theme->titlebar_active[0], env->decor_theme->titlebar_active[1],
                env->decor_theme->titlebar_active[2], 0.65f};

#ifdef HAVE_SYSTEMD
            size_t idx = 0;
            if (env->sni != NULL && env->sni->items.prev != NULL && env->sni->items.next != NULL) {
                struct fbwl_sni_item *sni;
                wl_list_for_each(sni, &env->sni->items, link) {
                    if (sni->status == FBWL_SNI_STATUS_PASSIVE) {
                        continue;
                    }
                    if (idx >= ui->tray_count) {
                        break;
                    }

                    ui->tray_item_lx[idx] = xoff;
                    ui->tray_item_w[idx] = ui->tray_icon_w;

                    ui->tray_ids[idx] = strdup(sni->id != NULL ? sni->id : "");
                    ui->tray_services[idx] = strdup(sni->service != NULL ? sni->service : "");
                    ui->tray_paths[idx] = strdup(sni->path != NULL ? sni->path : "");

                    ui->tray_rects[idx] = wlr_scene_rect_create(ui->tree, size, size, item);
                    if (ui->tray_rects[idx] != NULL) {
                        wlr_scene_node_set_position(&ui->tray_rects[idx]->node, xoff + pad, pad);
                    }

                    ui->tray_icons[idx] = wlr_scene_buffer_create(ui->tree, sni->icon_buf);
                    if (ui->tray_icons[idx] != NULL) {
                        wlr_scene_node_set_position(&ui->tray_icons[idx]->node, xoff + pad, pad);
                        wlr_scene_buffer_set_dest_size(ui->tray_icons[idx], size, size);
                    }

                    wlr_log(WLR_INFO, "Toolbar: tray item idx=%zu lx=%d w=%d id=%s",
                        idx, xoff, ui->tray_icon_w, sni->id != NULL ? sni->id : "");

                    xoff += ui->tray_icon_w;
                    idx++;
                }
            }
            ui->tray_count = idx;
#endif
        }
    }

    if (ui->clock_w > 0) {
        ui->clock_label = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->clock_label != NULL) {
            wlr_scene_node_set_position(&ui->clock_label->node, ui->clock_x, 0);
            fbwl_ui_toolbar_clock_render(ui);
        }

        struct wl_event_loop *loop = env->wl_display != NULL ? wl_display_get_event_loop(env->wl_display) : NULL;
        if (loop != NULL) {
            ui->clock_timer = wl_event_loop_add_timer(loop, fbwl_ui_toolbar_clock_timer, ui);
            if (ui->clock_timer != NULL) {
                wl_event_source_timer_update(ui->clock_timer, 1000);
            }
        }
    }

    fbwl_ui_toolbar_update_iconbar_focus(ui, env->decor_theme, env->focused_view);

    wlr_scene_node_raise_to_top(&ui->tree->node);
    wlr_log(WLR_INFO, "Toolbar: built x=%d y=%d w=%d h=%d cell_w=%d workspaces=%zu iconbar=%zu tray=%zu clock_w=%d",
        ui->x, ui->y, ui->width, ui->height, ui->cell_w, ui->cell_count, ui->iconbar_count, ui->tray_count,
        ui->clock_w);
    fbwl_ui_toolbar_update_position(ui, env);
}

void fbwl_ui_toolbar_update_position(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env) {
    if (ui == NULL || env == NULL || env->output_layout == NULL) {
        return;
    }
    if (!ui->enabled || ui->tree == NULL) {
        return;
    }

    struct wlr_output *out = wlr_output_layout_get_center_output(env->output_layout);
    if (out == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(env->output_layout, out, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    const int desired_w = box.width > ui->ws_width ? box.width : ui->ws_width;
    if (desired_w != ui->width) {
        fbwl_ui_toolbar_rebuild(ui, env);
        return;
    }

    int x = box.x;
    int y = box.y + box.height - ui->height;
    if (y < box.y) {
        y = box.y;
    }

    if (x == ui->x && y == ui->y) {
        return;
    }

    ui->x = x;
    ui->y = y;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
    wlr_log(WLR_INFO, "Toolbar: position x=%d y=%d h=%d cell_w=%d workspaces=%zu",
        ui->x, ui->y, ui->height, ui->cell_w, ui->cell_count);
}

bool fbwl_ui_toolbar_handle_click(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
        const struct fbwl_ui_toolbar_hooks *hooks, int lx, int ly, uint32_t button) {
    if (ui == NULL || env == NULL || env->wm == NULL) {
        return false;
    }
    if (!ui->enabled || ui->tree == NULL || ui->cell_w < 1 || ui->height < 1 || ui->cell_count < 1) {
        return false;
    }

    const int x = lx - ui->x;
    const int y = ly - ui->y;
    if (x < 0 || x >= ui->width || y < 0 || y >= ui->height) {
        return false;
    }

    if (x < ui->ws_width) {
        if (button != BTN_LEFT) {
            return false;
        }

        const int idx = x / ui->cell_w;
        if (idx < 0 || (size_t)idx >= ui->cell_count) {
            return true;
        }

        wlr_log(WLR_INFO, "Toolbar: click workspace=%d", idx + 1);
        fbwm_core_workspace_switch(env->wm, idx);
        if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "toolbar");
        }
        return true;
    }

    if (button == BTN_LEFT &&
            ui->iconbar_count > 0 && ui->iconbar_views != NULL && ui->iconbar_item_lx != NULL &&
            ui->iconbar_item_w != NULL) {
        for (size_t i = 0; i < ui->iconbar_count; i++) {
            const int ix = ui->iconbar_item_lx[i];
            const int iw = ui->iconbar_item_w[i];
            if (x < ix || x >= ix + iw) {
                continue;
            }

            struct fbwl_view *view = ui->iconbar_views[i];
            if (view == NULL) {
                return true;
            }

            wlr_log(WLR_INFO, "Toolbar: click iconbar idx=%zu title=%s", i, fbwl_view_display_title(view));

            if (view->minimized) {
                if (hooks != NULL && hooks->view_set_minimized != NULL) {
                    hooks->view_set_minimized(hooks->userdata, view, false, "toolbar-iconbar");
                }
            }

            if (!view->wm_view.sticky &&
                    view->wm_view.workspace != fbwm_core_workspace_current(env->wm)) {
                fbwm_core_workspace_switch(env->wm, view->wm_view.workspace);
                if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
                    hooks->apply_workspace_visibility(hooks->userdata, "toolbar-iconbar-switch");
                }
            }

            fbwm_core_focus_view(env->wm, &view->wm_view);
            return true;
        }
    }

    if (ui->tray_count > 0 && ui->tray_item_lx != NULL && ui->tray_item_w != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            const int ix = ui->tray_item_lx[i];
            const int iw = ui->tray_item_w[i];
            if (x < ix || x >= ix + iw) {
                continue;
            }

            const char *id = (ui->tray_ids != NULL && ui->tray_ids[i] != NULL) ? ui->tray_ids[i] : "";
            const char *method = NULL;
            const char *action = NULL;
            if (button == BTN_LEFT) {
                method = "Activate";
                action = "activate";
            } else if (button == BTN_MIDDLE) {
                method = "SecondaryActivate";
                action = "secondary-activate";
            } else if (button == BTN_RIGHT) {
                method = "ContextMenu";
                action = "context-menu";
            } else {
                return false;
            }

            wlr_log(WLR_INFO, "Toolbar: click tray idx=%zu id=%s action=%s", i, id, action);

#ifdef HAVE_SYSTEMD
            if (env->sni != NULL && ui->tray_services != NULL && ui->tray_paths != NULL) {
                const char *service = ui->tray_services[i];
                const char *path = ui->tray_paths[i];
                fbwl_sni_send_item_action(env->sni, id, service, path, method, action, lx, ly);
            }
#endif

            return true;
        }
    }
    return button == BTN_LEFT;
}

