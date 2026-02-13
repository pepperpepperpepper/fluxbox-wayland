#include "wayland/fbwl_server_keybinding_actions.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_view.h"

static struct fbwl_view *find_view_by_create_seq(struct fbwl_server *server, uint64_t create_seq) {
    if (server == NULL || create_seq == 0) {
        return NULL;
    }
    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view != NULL && view->create_seq == create_seq) {
            return view;
        }
    }
    return NULL;
}

static bool output_box_for_head(struct fbwl_server *server, size_t head0, struct wlr_box *box, struct wlr_output **out_output) {
    if (box != NULL) {
        *box = (struct wlr_box){0};
    }
    if (out_output != NULL) {
        *out_output = NULL;
    }
    if (server == NULL || server->output_layout == NULL) {
        return false;
    }
    struct wlr_output *out = fbwl_screen_map_output_for_screen(server->output_layout, &server->outputs, head0);
    if (out == NULL) {
        out = wlr_output_layout_get_center_output(server->output_layout);
    }
    if (out == NULL) {
        return false;
    }
    if (box != NULL) {
        wlr_output_layout_get_box(server->output_layout, out, box);
        if (box->width < 1 || box->height < 1) {
            return false;
        }
    }
    if (out_output != NULL) {
        *out_output = out;
    }
    return true;
}

static bool view_frame_metrics(const struct fbwl_server *server, const struct fbwl_view *view,
        int *out_frame_x, int *out_frame_y, int *out_frame_w, int *out_frame_h,
        int *out_left, int *out_top, int *out_border) {
    if (out_frame_x != NULL) {
        *out_frame_x = 0;
    }
    if (out_frame_y != NULL) {
        *out_frame_y = 0;
    }
    if (out_frame_w != NULL) {
        *out_frame_w = 0;
    }
    if (out_frame_h != NULL) {
        *out_frame_h = 0;
    }
    if (out_left != NULL) {
        *out_left = 0;
    }
    if (out_top != NULL) {
        *out_top = 0;
    }
    if (out_border != NULL) {
        *out_border = 0;
    }

    if (server == NULL || view == NULL || out_frame_x == NULL || out_frame_y == NULL || out_frame_w == NULL || out_frame_h == NULL) {
        return false;
    }

    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    if (w < 1 || h < 1) {
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    fbwl_view_decor_frame_extents(view, &server->decor_theme, &left, &top, &right, &bottom);

    const int border = left > right ? left : right;
    const int frame_w = w + left + right;
    const int frame_h = h + top + bottom;

    *out_frame_x = view->x - left;
    *out_frame_y = view->y - top;
    *out_frame_w = frame_w;
    *out_frame_h = frame_h;
    if (out_left != NULL) {
        *out_left = left;
    }
    if (out_top != NULL) {
        *out_top = top;
    }
    if (out_border != NULL) {
        *out_border = border;
    }
    return true;
}

static void view_move_to_head(struct fbwl_server *server, struct fbwl_view *view, size_t head0, const char *why) {
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->output_layout == NULL) {
        return;
    }

    struct wlr_box dst = {0};
    struct wlr_output *dst_out = NULL;
    if (!output_box_for_head(server, head0, &dst, &dst_out) || dst_out == NULL) {
        return;
    }

    const int cur_w = fbwl_view_current_width(view);
    const int cur_h = fbwl_view_current_height(view);
    if (cur_w < 1 || cur_h < 1) {
        return;
    }

    if (view->fullscreen) {
        struct wlr_box box = {0};
        fbwl_view_get_output_box(view, server->output_layout, dst_out, &box);
        if (box.width < 1 || box.height < 1) {
            return;
        }
        view->x = box.x;
        view->y = box.y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        }
        fbwl_view_pseudo_bg_update(view, why != NULL ? why : "set-head-fullscreen");
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, box.width, box.height);
        } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)box.width, (uint16_t)box.height);
        }
        view->placed = true;
        fbwl_tabs_sync_geometry_from_view(view, true, box.width, box.height, why != NULL ? why : "set-head-fullscreen");
        fbwl_view_foreign_update_output_from_position(view, server->output_layout);
        wlr_log(WLR_INFO, "Head: move fullscreen title=%s head=%zu x=%d y=%d w=%d h=%d reason=%s",
            fbwl_view_display_title(view), head0 + 1, view->x, view->y, box.width, box.height,
            why != NULL ? why : "(null)");
        return;
    }

    if (view->maximized || view->maximized_h || view->maximized_v) {
        const struct fbwl_screen_config *cfg = fbwl_server_screen_config(server, head0);
        const bool full_max = cfg != NULL ? cfg->full_maximization : server->full_maximization;
        struct wlr_box box = {0};
        full_max ?
            fbwl_view_get_output_box(view, server->output_layout, dst_out, &box) :
            fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, dst_out, &box);
        fbwl_view_apply_tabs_maxover_box(view, &box);
        if (box.width < 1 || box.height < 1) {
            return;
        }

        int x = view->x;
        int y = view->y;
        int w = cur_w;
        int h = cur_h;

        if (view->maximized_h || view->maximized) {
            x = box.x;
            w = box.width;
        } else {
            x = view->saved_x;
            w = view->saved_w > 0 ? view->saved_w : cur_w;
        }

        if (view->maximized_v || view->maximized) {
            y = box.y;
            h = box.height;
        } else {
            y = view->saved_y;
            h = view->saved_h > 0 ? view->saved_h : cur_h;
        }

        int frame_left = 0;
        int frame_top = 0;
        int frame_right = 0;
        int frame_bottom = 0;
        fbwl_view_decor_frame_extents(view, &server->decor_theme, &frame_left, &frame_top, &frame_right, &frame_bottom);
        if (view->maximized_h || view->maximized) {
            x += frame_left;
            w -= frame_left + frame_right;
        }
        if (view->maximized_v || view->maximized) {
            y += frame_top;
            h -= frame_top + frame_bottom;
        }
        if (w < 1 || h < 1) {
            return;
        }

        view->x = x;
        view->y = y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        }
        fbwl_view_pseudo_bg_update(view, why != NULL ? why : "set-head-maximized");
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, view->maximized);
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
        } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            wlr_xwayland_surface_set_maximized(view->xwayland_surface, view->maximized_h || view->maximized, view->maximized_v || view->maximized);
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)w, (uint16_t)h);
        }
        view->placed = true;
        fbwl_tabs_sync_geometry_from_view(view, true, w, h, why != NULL ? why : "set-head-maximized");
        fbwl_view_foreign_update_output_from_position(view, server->output_layout);
        wlr_log(WLR_INFO, "Head: move maximized title=%s head=%zu x=%d y=%d w=%d h=%d reason=%s",
            fbwl_view_display_title(view), head0 + 1, view->x, view->y, w, h,
            why != NULL ? why : "(null)");
        return;
    }

    const size_t src_head0 = fbwl_server_screen_index_for_view(server, view);
    struct wlr_box src = {0};
    if (!output_box_for_head(server, src_head0, &src, NULL)) {
        src = dst;
    }

    int frame_x = 0;
    int frame_y = 0;
    int frame_w = 0;
    int frame_h = 0;
    int left = 0;
    int top = 0;
    int border = 0;
    if (!view_frame_metrics(server, view, &frame_x, &frame_y, &frame_w, &frame_h, &left, &top, &border)) {
        return;
    }

    int new_frame_x = frame_x;
    int new_frame_y = frame_y;

    if (src.width > 0 && dst.width > 0) {
        const int d = (src.x + src.width) - (frame_x + frame_w);
        if (abs(src.x - frame_x) > border && abs(d) <= border) {
            new_frame_x = dst.x + dst.width - (frame_w + d);
        } else {
            new_frame_x = (int)(((long long)dst.width * (frame_x - src.x)) / src.width + dst.x);
        }
    } else {
        new_frame_x = dst.x;
    }

    if (src.height > 0 && dst.height > 0) {
        const int d = (src.y + src.height) - (frame_y + frame_h);
        if (abs(src.y - frame_y) > border && abs(d) <= border) {
            new_frame_y = dst.y + dst.height - (frame_h + d);
        } else {
            new_frame_y = (int)(((long long)dst.height * (frame_y - src.y)) / src.height + dst.y);
        }
    } else {
        new_frame_y = dst.y;
    }

    view->x = new_frame_x + left;
    view->y = new_frame_y + top;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }
    fbwl_view_pseudo_bg_update(view, why != NULL ? why : "set-head");
    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)cur_w, (uint16_t)cur_h);
    }
    view->placed = true;
    fbwl_tabs_sync_geometry_from_view(view, false, 0, 0, why != NULL ? why : "set-head");
    fbwl_view_foreign_update_output_from_position(view, server->output_layout);
    wlr_log(WLR_INFO, "Head: move title=%s head=%zu x=%d y=%d reason=%s",
        fbwl_view_display_title(view), head0 + 1, view->x, view->y,
        why != NULL ? why : "(null)");
}

void server_keybindings_view_set_head(void *userdata, struct fbwl_view *view, int head) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    const size_t heads = fbwm_core_head_count(&server->wm);
    if (heads < 1) {
        return;
    }

    int num = head;
    if (num == 0) {
        num = 1;
    }
    if (num < 0) {
        num += (int)heads + 1;
    }
    if (num < 1) {
        num = 1;
    }
    if ((size_t)num > heads) {
        num = (int)heads;
    }
    view_move_to_head(server, view, (size_t)(num - 1), "sethead");
}

void server_keybindings_view_send_to_rel_head(void *userdata, struct fbwl_view *view, int delta) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    const size_t heads = fbwm_core_head_count(&server->wm);
    if (heads < 2) {
        return;
    }
    if (delta == 0) {
        delta = 1;
    }
    const int total = (int)heads;
    const int cur0 = (int)fbwl_server_screen_index_for_view(server, view);
    int mod = delta % total;
    int next0 = (cur0 + mod) % total;
    if (next0 < 0) {
        next0 += total;
    }
    view_move_to_head(server, view, (size_t)next0, delta > 0 ? "sendtonexthead" : "sendtoprevhead");
}

static void marked_windows_remove_index(struct fbwl_server *server, size_t idx) {
    if (server == NULL || idx >= server->marked_windows.len || server->marked_windows.len == 0) {
        return;
    }
    server->marked_windows.items[idx] = server->marked_windows.items[server->marked_windows.len - 1];
    server->marked_windows.len--;
}

static bool marked_windows_set(struct fbwl_server *server, uint32_t keycode, uint64_t create_seq) {
    if (server == NULL || keycode == 0 || create_seq == 0) {
        return false;
    }

    for (size_t i = 0; i < server->marked_windows.len; i++) {
        if (server->marked_windows.items[i].keycode == keycode) {
            server->marked_windows.items[i].create_seq = create_seq;
            return true;
        }
    }

    if (server->marked_windows.len >= server->marked_windows.cap) {
        const size_t new_cap = server->marked_windows.cap > 0 ? server->marked_windows.cap * 2 : 16;
        void *tmp = realloc(server->marked_windows.items, new_cap * sizeof(server->marked_windows.items[0]));
        if (tmp == NULL) {
            return false;
        }
        server->marked_windows.items = tmp;
        server->marked_windows.cap = new_cap;
    }

    server->marked_windows.items[server->marked_windows.len++] = (struct fbwl_marked_window){
        .keycode = keycode,
        .create_seq = create_seq,
    };
    return true;
}

void server_keybindings_mark_window(void *userdata, struct fbwl_view *view, uint32_t keycode) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (keycode == 0) {
        wlr_log(WLR_ERROR, "MarkWindow: missing placeholder keycode (use Arg binding)");
        return;
    }

    if (!marked_windows_set(server, keycode, view->create_seq)) {
        wlr_log(WLR_ERROR, "MarkWindow: OOM");
        return;
    }

    wlr_log(WLR_INFO, "MarkWindow: keycode=%u create_seq=%llu title=%s",
        keycode,
        (unsigned long long)view->create_seq,
        fbwl_view_display_title(view));
}

void server_keybindings_goto_marked_window(void *userdata, uint32_t keycode) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    if (keycode == 0) {
        wlr_log(WLR_ERROR, "GotoMarkedWindow: missing placeholder keycode (use Arg binding)");
        return;
    }

    uint64_t seq = 0;
    size_t idx = 0;
    bool found = false;
    for (size_t i = 0; i < server->marked_windows.len; i++) {
        if (server->marked_windows.items[i].keycode == keycode) {
            idx = i;
            seq = server->marked_windows.items[i].create_seq;
            found = true;
            break;
        }
    }
    if (!found || seq == 0) {
        wlr_log(WLR_INFO, "GotoMarkedWindow: no match keycode=%u", keycode);
        return;
    }

    struct fbwl_view *view = find_view_by_create_seq(server, seq);
    if (view == NULL) {
        marked_windows_remove_index(server, idx);
        wlr_log(WLR_INFO, "GotoMarkedWindow: stale keycode=%u create_seq=%llu", keycode, (unsigned long long)seq);
        return;
    }

    if (view->minimized) {
        view_set_minimized(view, false, "goto-marked-window");
    }
    if (view->tab_group != NULL && !fbwl_tabs_view_is_active(view)) {
        fbwl_tabs_activate(view, "goto-marked-window");
    }

    if (fbwm_core_view_is_visible(&server->wm, &view->wm_view)) {
        fbwm_core_focus_view_with_reason(&server->wm, &view->wm_view, "marked-window");
    } else {
        fbwm_core_refocus(&server->wm);
    }
    server_raise_view(view, "goto-marked-window");

    wlr_log(WLR_INFO, "GotoMarkedWindow: keycode=%u create_seq=%llu title=%s",
        keycode,
        (unsigned long long)view->create_seq,
        fbwl_view_display_title(view));
}
