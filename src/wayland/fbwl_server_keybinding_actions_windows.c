#include "wayland/fbwl_server_keybinding_actions.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_output.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_ui_toolbar_iconbar_pattern.h"
#include "wayland/fbwl_view.h"
#include "wmcore/fbwm_output.h"

struct view_vec {
    struct fbwl_view **items;
    size_t len;
    size_t cap;
};

static bool view_vec_push(struct view_vec *vec, struct fbwl_view *view) {
    if (vec == NULL || view == NULL) {
        return false;
    }
    if (vec->len >= vec->cap) {
        const size_t new_cap = vec->cap > 0 ? vec->cap * 2 : 16;
        struct fbwl_view **tmp = realloc(vec->items, new_cap * sizeof(*tmp));
        if (tmp == NULL) {
            return false;
        }
        vec->items = tmp;
        vec->cap = new_cap;
    }
    vec->items[vec->len++] = view;
    return true;
}

static void view_vec_remove_index(struct view_vec *vec, size_t idx) {
    if (vec == NULL || idx >= vec->len || vec->len == 0) {
        return;
    }
    vec->items[idx] = vec->items[vec->len - 1];
    vec->len--;
}

static void build_pattern_env(struct fbwl_ui_toolbar_env *env, struct fbwl_server *server, int cursor_x, int cursor_y) {
    if (env == NULL) {
        return;
    }

    *env = (struct fbwl_ui_toolbar_env){0};
    if (server == NULL) {
        return;
    }

    env->wm = &server->wm;
    env->focused_view = server->wm.focused != NULL ? server->wm.focused->userdata : NULL;
    env->cursor_valid = true;
    env->cursor_x = (double)cursor_x;
    env->cursor_y = (double)cursor_y;
    env->output_layout = server->output_layout;
    env->outputs = &server->outputs;
    env->xwayland = server->xwayland;
    env->layer_background = server->layer_background;
    env->layer_bottom = server->layer_bottom;
    env->layer_normal = server->layer_normal;
    env->layer_fullscreen = server->layer_fullscreen;
    env->layer_top = server->layer_top;
    env->layer_overlay = server->layer_overlay;
    env->decor_theme = &server->decor_theme;
}

static int current_workspace_at_cursor(struct fbwl_server *server, int cursor_x, int cursor_y, size_t *out_head) {
    if (server == NULL) {
        if (out_head != NULL) {
            *out_head = 0;
        }
        return 0;
    }
    const size_t head = fbwl_server_screen_index_at(server, cursor_x, cursor_y);
    if (out_head != NULL) {
        *out_head = head;
    }
    return fbwm_core_workspace_current_for_head(&server->wm, head);
}

struct server_place_output_ctx {
    struct fbwm_box full;
    struct fbwm_box usable;
    const char *name;
};

static const char *server_place_output_name(const struct fbwm_output *wm_output) {
    const struct server_place_output_ctx *ctx = wm_output != NULL ? wm_output->userdata : NULL;
    return ctx != NULL && ctx->name != NULL ? ctx->name : "(unknown)";
}

static bool server_place_output_full_box(const struct fbwm_output *wm_output, struct fbwm_box *out) {
    const struct server_place_output_ctx *ctx = wm_output != NULL ? wm_output->userdata : NULL;
    if (ctx == NULL || out == NULL) {
        return false;
    }
    *out = ctx->full;
    return true;
}

static bool server_place_output_usable_box(const struct fbwm_output *wm_output, struct fbwm_box *out) {
    const struct server_place_output_ctx *ctx = wm_output != NULL ? wm_output->userdata : NULL;
    if (ctx == NULL || out == NULL) {
        return false;
    }
    *out = ctx->usable;
    return true;
}

static const struct fbwm_output_ops server_place_output_ops = {
    .name = server_place_output_name,
    .full_box = server_place_output_full_box,
    .usable_box = server_place_output_usable_box,
};

static bool output_boxes_at_cursor(struct fbwl_server *server, int cursor_x, int cursor_y,
        struct server_place_output_ctx *out_ctx) {
    if (server == NULL || out_ctx == NULL) {
        return false;
    }
    if (server->output_layout == NULL) {
        return false;
    }

    struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, cursor_x, cursor_y);
    if (out == NULL) {
        out = wlr_output_layout_get_center_output(server->output_layout);
    }
    if (out == NULL) {
        return false;
    }

    struct wlr_box full = {0};
    wlr_output_layout_get_box(server->output_layout, out, &full);

    struct wlr_box usable = full;
    struct fbwl_output *out_data = fbwl_output_find(&server->outputs, out);
    if (out_data != NULL && out_data->usable_area.width > 0 && out_data->usable_area.height > 0) {
        usable = out_data->usable_area;
    }

    *out_ctx = (struct server_place_output_ctx){
        .full = {.x = full.x, .y = full.y, .width = full.width, .height = full.height},
        .usable = {.x = usable.x, .y = usable.y, .width = usable.width, .height = usable.height},
        .name = out->name,
    };
    return true;
}

static void view_frame_offsets(const struct fbwl_server *server, const struct fbwl_view *view,
        int *out_left, int *out_top, int *out_right, int *out_bottom) {
    if (out_left != NULL) {
        *out_left = 0;
    }
    if (out_top != NULL) {
        *out_top = 0;
    }
    if (out_right != NULL) {
        *out_right = 0;
    }
    if (out_bottom != NULL) {
        *out_bottom = 0;
    }

    if (server == NULL || view == NULL) {
        return;
    }
    fbwl_view_decor_frame_extents(view, &server->decor_theme, out_left, out_top, out_right, out_bottom);
}

static void view_prepare_for_manual_geometry(struct fbwl_server *server, struct fbwl_view *view) {
    if (server == NULL || view == NULL) {
        return;
    }

    if (view->fullscreen) {
        fbwl_view_set_fullscreen(view, false, server->output_layout, &server->outputs,
            server->layer_normal, server->layer_fullscreen, NULL);
    }

    if (!view->maximized && !view->maximized_h && !view->maximized_v) {
        return;
    }

    view->maximized = false;
    view->maximized_h = false;
    view->maximized_v = false;

    if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
        wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, false);
    } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_set_maximized(view->xwayland_surface, false, false);
    }
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, false);
    }
}

static bool server_view_move_frame(struct fbwl_server *server, struct fbwl_view *view, int frame_x, int frame_y,
        const char *why) {
    if (server == NULL || view == NULL) {
        return false;
    }

    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    if (w < 1 || h < 1) {
        return false;
    }

    int left = 0, top = 0, right = 0, bottom = 0;
    view_frame_offsets(server, view, &left, &top, &right, &bottom);

    view->x = frame_x + left;
    view->y = frame_y + top;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }
    fbwl_view_pseudo_bg_update(view, why);

    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)w, (uint16_t)h);
    }

    view->placed = true;
    fbwl_tabs_sync_geometry_from_view(view, false, 0, 0, why);
    fbwl_view_foreign_update_output_from_position(view, server->output_layout);
    return true;
}

static bool server_view_move_resize_frame(struct fbwl_server *server, struct fbwl_view *view, int frame_x, int frame_y,
        int frame_w, int frame_h, const char *why) {
    if (server == NULL || view == NULL) {
        return false;
    }

    int left = 0, top = 0, right = 0, bottom = 0;
    view_frame_offsets(server, view, &left, &top, &right, &bottom);

    int w = frame_w - left - right;
    int h = frame_h - top - bottom;
    if (w < 1 || h < 1) {
        return false;
    }

    view->x = frame_x + left;
    view->y = frame_y + top;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }
    fbwl_view_pseudo_bg_update(view, why);

    if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
    } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)w, (uint16_t)h);
    }

    view->width = w;
    view->height = h;
    view->placed = true;

    fbwl_tabs_sync_geometry_from_view(view, true, w, h, why);
    fbwl_view_foreign_update_output_from_position(view, server->output_layout);
    return true;
}

static bool view_frame_box(const struct fbwl_view *view, struct fbwm_box *out) {
    if (view == NULL || out == NULL) {
        return false;
    }
    if (view->wm_view.ops == NULL || view->wm_view.ops->get_box == NULL) {
        return false;
    }
    return view->wm_view.ops->get_box(&view->wm_view, out);
}

static bool view_is_desktop_layer(const struct fbwl_server *server, const struct fbwl_view *view) {
    if (server == NULL || view == NULL) {
        return false;
    }
    struct wlr_scene_tree *layer = view->base_layer != NULL ? view->base_layer : server->layer_normal;
    return layer != NULL && layer == server->layer_background;
}

void server_keybindings_views_attach_pattern(void *userdata, const char *pattern, int cursor_x, int cursor_y) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    char *tmp = strdup(pattern != NULL ? pattern : "");
    if (tmp == NULL) {
        return;
    }

    struct fbwl_iconbar_pattern pat = {0};
    fbwl_iconbar_pattern_parse_inplace(&pat, tmp);

    struct fbwl_ui_toolbar_env env = {0};
    build_pattern_env(&env, server, cursor_x, cursor_y);

    size_t head = 0;
    const int current_ws = current_workspace_at_cursor(server, cursor_x, cursor_y, &head);

    struct fbwl_view *anchor = NULL;
    size_t attached = 0;
    for (struct fbwm_view *wm_view = server->wm.views.next; wm_view != &server->wm.views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped || view->in_slit) {
            continue;
        }
        if (!fbwl_client_pattern_matches(&pat, &env, view, current_ws)) {
            continue;
        }

        if (anchor == NULL) {
            if (view->minimized) {
                continue;
            }
            anchor = view;
            continue;
        }

        if (fbwl_tabs_attach(view, anchor, "attach-cmd")) {
            attached++;
        }
    }

    if (anchor != NULL) {
        wlr_log(WLR_INFO, "Attach: head=%zu ws=%d anchor=%s attached=%zu pattern=%s",
            head, current_ws, fbwl_view_display_title(anchor), attached,
            pattern != NULL ? pattern : "");
    } else {
        wlr_log(WLR_INFO, "Attach: head=%zu ws=%d anchor=(none) attached=%zu pattern=%s",
            head, current_ws, attached, pattern != NULL ? pattern : "");
    }

    if (attached > 0) {
        server_toolbar_ui_rebuild(server);
    }

    fbwl_iconbar_pattern_free(&pat);
    free(tmp);
}

void server_keybindings_show_desktop(void *userdata, int cursor_x, int cursor_y) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    size_t head = 0;
    const int ws = current_workspace_at_cursor(server, cursor_x, cursor_y, &head);

    struct view_vec views = {0};
    for (struct fbwm_view *wm_view = server->wm.views.next; wm_view != &server->wm.views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped || view->in_slit) {
            continue;
        }
        if (!(wm_view->sticky || wm_view->workspace == ws)) {
            continue;
        }
        if (view_is_desktop_layer(server, view)) {
            continue;
        }
        (void)view_vec_push(&views, view);
    }

    bool any_unminimized = false;
    for (size_t i = 0; i < views.len; i++) {
        if (!views.items[i]->minimized) {
            any_unminimized = true;
            break;
        }
    }

    if (any_unminimized) {
        for (size_t i = 0; i < views.len; i++) {
            if (!views.items[i]->minimized) {
                view_set_minimized(views.items[i], true, "showdesktop");
            }
        }
    } else {
        for (size_t i = views.len; i > 0; i--) {
            if (views.items[i - 1]->minimized) {
                view_set_minimized(views.items[i - 1], false, "showdesktop");
            }
        }
    }

    wlr_log(WLR_INFO, "ShowDesktop: head=%zu ws=%d action=%s count=%zu",
        head, ws, any_unminimized ? "minimize" : "restore", views.len);

    free(views.items);
}

static unsigned int floor_sqrt_u(unsigned int n) {
    unsigned int r = 0;
    while ((r + 1) * (r + 1) <= n) {
        r++;
    }
    return r;
}

static bool arrange_is_stacked(int method) {
    return method >= 3;
}

static void arrange_split_boxes(const struct fbwm_box *usable, int method, struct fbwm_box *out_tile, struct fbwm_box *out_main) {
    if (out_tile != NULL) {
        *out_tile = *usable;
    }
    if (out_main != NULL) {
        *out_main = (struct fbwm_box){0};
    }

    if (usable == NULL || out_tile == NULL || out_main == NULL) {
        return;
    }
    if (!arrange_is_stacked(method)) {
        return;
    }

    if (method == 3 || method == 4) {
        const int tile_w = usable->width / 2;
        const int main_w = usable->width - tile_w;
        if (method == 3) {
            *out_tile = (struct fbwm_box){.x = usable->x, .y = usable->y, .width = tile_w, .height = usable->height};
            *out_main = (struct fbwm_box){.x = usable->x + tile_w, .y = usable->y, .width = main_w, .height = usable->height};
        } else {
            *out_main = (struct fbwm_box){.x = usable->x, .y = usable->y, .width = main_w, .height = usable->height};
            *out_tile = (struct fbwm_box){.x = usable->x + main_w, .y = usable->y, .width = tile_w, .height = usable->height};
        }
        return;
    }

    const int tile_h = usable->height / 2;
    const int main_h = usable->height - tile_h;
    if (method == 5) {
        *out_tile = (struct fbwm_box){.x = usable->x, .y = usable->y, .width = usable->width, .height = tile_h};
        *out_main = (struct fbwm_box){.x = usable->x, .y = usable->y + tile_h, .width = usable->width, .height = main_h};
    } else {
        *out_main = (struct fbwm_box){.x = usable->x, .y = usable->y, .width = usable->width, .height = main_h};
        *out_tile = (struct fbwm_box){.x = usable->x, .y = usable->y + main_h, .width = usable->width, .height = tile_h};
    }
}

static int64_t dist2_i32(int x0, int y0, int x1, int y1) {
    int64_t dx = (int64_t)x0 - (int64_t)x1;
    int64_t dy = (int64_t)y0 - (int64_t)y1;
    return dx * dx + dy * dy;
}

static void arrange_views_in_box(struct fbwl_server *server, struct view_vec *views, const struct fbwm_box *area,
        int method, const char *why) {
    if (server == NULL || views == NULL || area == NULL) {
        return;
    }
    if (views->len == 0 || area->width < 1 || area->height < 1) {
        return;
    }

    unsigned int win_count = (unsigned int)views->len;
    unsigned int cols = floor_sqrt_u(win_count);
    if (cols < 1) {
        cols = 1;
    }
    unsigned int rows = (win_count + cols - 1) / cols;

    const bool rotate =
        method == 1 || (method == 0 && area->width < area->height);
    if (rotate) {
        unsigned int tmp = cols;
        cols = rows;
        rows = tmp;
    }
    if (cols < 1) {
        cols = 1;
    }
    if (rows < 1) {
        rows = 1;
    }

    const int cell_w = area->width / (int)cols;
    const int cell_h = area->height / (int)rows;

    for (unsigned int i = 0; i < rows && views->len > 0; i++) {
        for (unsigned int j = 0; j < cols && views->len > 0; j++) {
            const int frame_x = area->x + (int)j * cell_w;
            const int frame_y = area->y + (int)i * cell_h;
            const int frame_w = (j + 1 == cols) ? (area->x + area->width - frame_x) : cell_w;
            const int frame_h = (i + 1 == rows) ? (area->y + area->height - frame_y) : cell_h;

            const int cell_cx = frame_x + frame_w / 2;
            const int cell_cy = frame_y + frame_h / 2;

            size_t best_idx = 0;
            int64_t best_dist = INT64_MAX;
            uint64_t best_seq = 0;

            for (size_t k = 0; k < views->len; k++) {
                struct fbwl_view *candidate = views->items[k];
                struct fbwm_box box = {0};
                if (!view_frame_box(candidate, &box)) {
                    continue;
                }
                const int win_cx = box.x + box.width / 2;
                const int win_cy = box.y + box.height / 2;
                const int64_t dist = dist2_i32(win_cx, win_cy, cell_cx, cell_cy);
                const uint64_t seq = candidate->create_seq;
                if (dist < best_dist || (dist == best_dist && seq < best_seq)) {
                    best_idx = k;
                    best_dist = dist;
                    best_seq = seq;
                }
            }

            struct fbwl_view *pick = views->items[best_idx];
            view_prepare_for_manual_geometry(server, pick);
            if (server_view_move_resize_frame(server, pick, frame_x, frame_y, frame_w, frame_h, why)) {
                wlr_log(WLR_INFO, "ArrangeWindows: view=%s x=%d y=%d w=%d h=%d",
                    fbwl_view_display_title(pick), frame_x, frame_y, frame_w, frame_h);
            }
            view_vec_remove_index(views, best_idx);
        }
    }
}

void server_keybindings_arrange_windows(void *userdata, int method, const char *pattern, int cursor_x, int cursor_y) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    struct server_place_output_ctx out_ctx = {0};
    if (!output_boxes_at_cursor(server, cursor_x, cursor_y, &out_ctx)) {
        return;
    }

    struct fbwl_ui_toolbar_env env = {0};
    build_pattern_env(&env, server, cursor_x, cursor_y);

    size_t head = 0;
    const int ws = current_workspace_at_cursor(server, cursor_x, cursor_y, &head);

    char *tmp = strdup(pattern != NULL ? pattern : "");
    if (tmp == NULL) {
        return;
    }

    struct fbwl_iconbar_pattern pat = {0};
    fbwl_iconbar_pattern_parse_inplace(&pat, tmp);

    struct view_vec views = {0};
    for (struct fbwm_view *wm_view = server->wm.views.next; wm_view != &server->wm.views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped || view->minimized || view->in_slit) {
            continue;
        }
        if (!(wm_view->sticky || wm_view->workspace == ws)) {
            continue;
        }
        const int vhead = view->wm_view.ops != NULL && view->wm_view.ops->head != NULL ? view->wm_view.ops->head(&view->wm_view) : 0;
        if ((size_t)vhead != head) {
            continue;
        }
        if (!fbwl_client_pattern_matches(&pat, &env, view, ws)) {
            continue;
        }
        (void)view_vec_push(&views, view);
    }

    struct fbwl_view *main = NULL;
    if (arrange_is_stacked(method) && views.len > 0) {
        struct fbwl_view *focused = server->wm.focused != NULL ? server->wm.focused->userdata : NULL;
        if (focused != NULL) {
            for (size_t i = 0; i < views.len; i++) {
                if (views.items[i] == focused) {
                    main = focused;
                    view_vec_remove_index(&views, i);
                    break;
                }
            }
        }
        if (main == NULL && views.len > 0) {
            main = views.items[views.len - 1];
            views.len--;
        }
    }

    const size_t total = views.len + (main != NULL ? 1 : 0);

    struct fbwm_box tile = out_ctx.usable;
    struct fbwm_box main_box = {0};
    arrange_split_boxes(&out_ctx.usable, method, &tile, &main_box);

    arrange_views_in_box(server, &views, &tile, method, "arrange-windows");

    if (main != NULL && main_box.width > 0 && main_box.height > 0) {
        view_prepare_for_manual_geometry(server, main);
        if (server_view_move_resize_frame(server, main, main_box.x, main_box.y, main_box.width, main_box.height, "arrange-windows-main")) {
            wlr_log(WLR_INFO, "ArrangeWindows: main=%s x=%d y=%d w=%d h=%d",
                fbwl_view_display_title(main), main_box.x, main_box.y, main_box.width, main_box.height);
        }
    }

    wlr_log(WLR_INFO, "ArrangeWindows: head=%zu ws=%d method=%d count=%zu pattern=%s",
        head, ws, method, total, pattern != NULL ? pattern : "");

    server_toolbar_ui_rebuild(server);
    server_strict_mousefocus_recheck(server, "arrange-windows");

    free(views.items);
    fbwl_iconbar_pattern_free(&pat);
    free(tmp);
}

void server_keybindings_unclutter(void *userdata, const char *pattern, int cursor_x, int cursor_y) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    struct server_place_output_ctx out_ctx = {0};
    if (!output_boxes_at_cursor(server, cursor_x, cursor_y, &out_ctx)) {
        return;
    }

    struct fbwl_ui_toolbar_env env = {0};
    build_pattern_env(&env, server, cursor_x, cursor_y);

    size_t head = 0;
    const int ws = current_workspace_at_cursor(server, cursor_x, cursor_y, &head);

    char *tmp = strdup(pattern != NULL ? pattern : "");
    if (tmp == NULL) {
        return;
    }

    struct fbwl_iconbar_pattern pat = {0};
    fbwl_iconbar_pattern_parse_inplace(&pat, tmp);

    struct view_vec placed = {0};
    for (struct fbwm_view *wm_view = server->wm.views.next; wm_view != &server->wm.views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped || view->minimized || view->in_slit) {
            continue;
        }
        if (view->fullscreen || view->maximized || view->maximized_h || view->maximized_v) {
            continue;
        }
        if (!(wm_view->sticky || wm_view->workspace == ws)) {
            continue;
        }
        const int vhead = view->wm_view.ops != NULL && view->wm_view.ops->head != NULL ? view->wm_view.ops->head(&view->wm_view) : 0;
        if ((size_t)vhead != head) {
            continue;
        }
        if (!fbwl_client_pattern_matches(&pat, &env, view, ws)) {
            continue;
        }
        (void)view_vec_push(&placed, view);
    }

    if (placed.len == 0) {
        free(placed.items);
        fbwl_iconbar_pattern_free(&pat);
        free(tmp);
        return;
    }

    for (size_t i = 0; i < placed.len; i++) {
        struct fbwm_box box = {0};
        if (!view_frame_box(placed.items[i], &box)) {
            continue;
        }
        (void)server_view_move_frame(server, placed.items[i], -box.width, -box.height, "unclutter-clean");
    }

    struct fbwm_output wm_out = {0};
    struct server_place_output_ctx place_ctx = out_ctx;
    fbwm_output_init(&wm_out, &server_place_output_ops, &place_ctx);

    enum fbwm_window_placement_strategy orig = fbwm_core_window_placement(&server->wm);
    enum fbwm_window_placement_strategy strat =
        out_ctx.usable.width >= out_ctx.usable.height ? FBWM_PLACE_ROW_MIN_OVERLAP : FBWM_PLACE_COL_MIN_OVERLAP;
    fbwm_core_set_window_placement(&server->wm, strat);

    for (size_t i = 0; i < placed.len; i++) {
        struct fbwm_box box = {0};
        if (!view_frame_box(placed.items[i], &box)) {
            continue;
        }
        int x = box.x;
        int y = box.y;
        fbwm_core_place_next(&server->wm, &wm_out, box.width, box.height, cursor_x, cursor_y, &x, &y);
        if (server_view_move_frame(server, placed.items[i], x, y, "unclutter")) {
            wlr_log(WLR_INFO, "Unclutter: view=%s x=%d y=%d", fbwl_view_display_title(placed.items[i]), x, y);
        }
    }

    fbwm_core_set_window_placement(&server->wm, orig);

    wlr_log(WLR_INFO, "Unclutter: head=%zu ws=%d count=%zu pattern=%s",
        head, ws, placed.len, pattern != NULL ? pattern : "");

    server_toolbar_ui_rebuild(server);
    server_strict_mousefocus_recheck(server, "unclutter");

    free(placed.items);
    fbwl_iconbar_pattern_free(&pat);
    free(tmp);
}

enum deiconify_mode {
    DEICONIFY_LAST_WORKSPACE = 0,
    DEICONIFY_LAST,
    DEICONIFY_ALL_WORKSPACE,
    DEICONIFY_ALL,
};

enum deiconify_dest {
    DEICONIFY_DEST_CURRENT = 0,
    DEICONIFY_DEST_ORIGIN,
    DEICONIFY_DEST_ORIGIN_QUIET,
};

static const char *deiconify_mode_str(enum deiconify_mode mode) {
    switch (mode) {
    case DEICONIFY_ALL:
        return "All";
    case DEICONIFY_ALL_WORKSPACE:
        return "AllWorkspace";
    case DEICONIFY_LAST:
        return "Last";
    case DEICONIFY_LAST_WORKSPACE:
    default:
        return "LastWorkspace";
    }
}

static const char *deiconify_dest_str(enum deiconify_dest dest) {
    switch (dest) {
    case DEICONIFY_DEST_ORIGIN:
        return "Origin";
    case DEICONIFY_DEST_ORIGIN_QUIET:
        return "OriginQuiet";
    case DEICONIFY_DEST_CURRENT:
    default:
        return "Current";
    }
}

static bool parse_deiconify_mode(const char *s, enum deiconify_mode *out_mode) {
    if (s == NULL || out_mode == NULL) {
        return false;
    }
    if (strcasecmp(s, "all") == 0) {
        *out_mode = DEICONIFY_ALL;
        return true;
    }
    if (strcasecmp(s, "allworkspace") == 0) {
        *out_mode = DEICONIFY_ALL_WORKSPACE;
        return true;
    }
    if (strcasecmp(s, "last") == 0) {
        *out_mode = DEICONIFY_LAST;
        return true;
    }
    if (strcasecmp(s, "lastworkspace") == 0) {
        *out_mode = DEICONIFY_LAST_WORKSPACE;
        return true;
    }
    return false;
}

static bool parse_deiconify_dest(const char *s, enum deiconify_dest *out_dest) {
    if (s == NULL || out_dest == NULL) {
        return false;
    }
    if (strcasecmp(s, "current") == 0) {
        *out_dest = DEICONIFY_DEST_CURRENT;
        return true;
    }
    if (strcasecmp(s, "origin") == 0) {
        *out_dest = DEICONIFY_DEST_ORIGIN;
        return true;
    }
    if (strcasecmp(s, "originquiet") == 0) {
        *out_dest = DEICONIFY_DEST_ORIGIN_QUIET;
        return true;
    }
    return false;
}

static const char *skip_ws(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

void server_keybindings_deiconify(void *userdata, const char *args, int cursor_x, int cursor_y) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    size_t head = 0;
    const int ws = current_workspace_at_cursor(server, cursor_x, cursor_y, &head);

    enum deiconify_mode mode = DEICONIFY_LAST_WORKSPACE;
    enum deiconify_dest dest = DEICONIFY_DEST_CURRENT;

    const char *s = skip_ws(args);
    if (s != NULL && *s != '\0') {
        char *copy = strdup(s);
        if (copy == NULL) {
            return;
        }
        char *saveptr = NULL;
        const char *tok1 = strtok_r(copy, " \t\r\n", &saveptr);
        const char *tok2 = strtok_r(NULL, " \t\r\n", &saveptr);
        const char *tok3 = strtok_r(NULL, " \t\r\n", &saveptr);
        if (tok1 != NULL && !parse_deiconify_mode(tok1, &mode)) {
            wlr_log(WLR_ERROR, "Deiconify: invalid mode=%s", tok1);
            free(copy);
            return;
        }
        if (tok2 != NULL && !parse_deiconify_dest(tok2, &dest)) {
            wlr_log(WLR_ERROR, "Deiconify: invalid destination=%s", tok2);
            free(copy);
            return;
        }
        if (tok3 != NULL) {
            wlr_log(WLR_ERROR, "Deiconify: too many args: %s", s);
            free(copy);
            return;
        }
        free(copy);
    }

    struct view_vec picks = {0};
    const bool workspace_limited = (mode == DEICONIFY_LAST_WORKSPACE || mode == DEICONIFY_ALL_WORKSPACE);
    const bool pick_one = (mode == DEICONIFY_LAST_WORKSPACE || mode == DEICONIFY_LAST);

    for (struct fbwm_view *wm_view = server->wm.views.next; wm_view != &server->wm.views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped || view->in_slit || !view->minimized) {
            continue;
        }
        if (workspace_limited && !(wm_view->sticky || wm_view->workspace == ws)) {
            continue;
        }
        (void)view_vec_push(&picks, view);
        if (pick_one) {
            break;
        }
    }

    if (picks.len == 0) {
        wlr_log(WLR_INFO, "Deiconify: head=%zu ws=%d mode=%s dest=%s count=0",
            head, ws, deiconify_mode_str(mode), deiconify_dest_str(dest));
        return;
    }

    if (dest == DEICONIFY_DEST_CURRENT) {
        for (size_t i = 0; i < picks.len; i++) {
            if (!picks.items[i]->wm_view.sticky) {
                picks.items[i]->wm_view.workspace = ws;
            }
        }
    } else if (dest == DEICONIFY_DEST_ORIGIN) {
        const int dest_ws = picks.items[0]->wm_view.workspace;
        server_workspace_switch_on_head(server, head, dest_ws, "deiconify-origin");
    }

    for (size_t i = 0; i < picks.len; i++) {
        view_set_minimized(picks.items[i], false, "deiconify");
    }

    wlr_log(WLR_INFO, "Deiconify: head=%zu ws=%d mode=%s dest=%s count=%zu",
        head, ws, deiconify_mode_str(mode), deiconify_dest_str(dest), picks.len);

    free(picks.items);
}

void server_keybindings_close_all_windows(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    struct view_vec views = {0};
    for (struct fbwm_view *wm_view = server->wm.views.next; wm_view != &server->wm.views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped || view->in_slit) {
            continue;
        }
        (void)view_vec_push(&views, view);
    }

    wlr_log(WLR_INFO, "CloseAllWindows: count=%zu", views.len);

    for (size_t i = 0; i < views.len; i++) {
        struct fbwl_view *view = views.items[i];
        if (view == NULL) {
            continue;
        }
        wlr_log(WLR_INFO, "CloseAllWindows: close title=%s", fbwl_view_display_title(view));
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
            wlr_xdg_toplevel_send_close(view->xdg_toplevel);
        } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            wlr_xwayland_surface_close(view->xwayland_surface);
        }
    }

    free(views.items);
}
