#include "wayland/fbwl_grabs.h"

#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"

static void grab_outline_destroy(struct fbwl_grab *grab) {
    if (grab == NULL) {
        return;
    }
    if (grab->outline_tree != NULL) {
        wlr_scene_node_destroy(&grab->outline_tree->node);
    }
    grab->outline_tree = NULL;
    grab->outline_top = NULL;
    grab->outline_bottom = NULL;
    grab->outline_left = NULL;
    grab->outline_right = NULL;
}

static void grab_outline_update(struct fbwl_grab *grab, struct fbwl_view *view,
        int frame_x, int frame_y, int frame_w, int frame_h) {
    if (grab == NULL || view == NULL || view->server == NULL) {
        return;
    }
    if (frame_w < 1 || frame_h < 1) {
        grab_outline_destroy(grab);
        return;
    }
    struct fbwl_server *server = view->server;
    if (server->scene == NULL) {
        return;
    }

    const int thickness = 1;
    float color[4] = {1.0f, 1.0f, 1.0f, 0.85f};

    if (grab->outline_tree == NULL) {
        struct wlr_scene_tree *parent = server->layer_overlay != NULL ? server->layer_overlay : &server->scene->tree;
        grab->outline_tree = wlr_scene_tree_create(parent);
        if (grab->outline_tree == NULL) {
            return;
        }

        grab->outline_top = wlr_scene_rect_create(grab->outline_tree, 1, 1, color);
        grab->outline_bottom = wlr_scene_rect_create(grab->outline_tree, 1, 1, color);
        grab->outline_left = wlr_scene_rect_create(grab->outline_tree, 1, 1, color);
        grab->outline_right = wlr_scene_rect_create(grab->outline_tree, 1, 1, color);
    }

    if (grab->outline_tree == NULL || grab->outline_top == NULL || grab->outline_bottom == NULL ||
            grab->outline_left == NULL || grab->outline_right == NULL) {
        grab_outline_destroy(grab);
        return;
    }

    int t = thickness;
    if (t < 1) {
        t = 1;
    }
    if (t > frame_w) {
        t = frame_w;
    }
    if (t > frame_h) {
        t = frame_h;
    }

    wlr_scene_node_set_position(&grab->outline_tree->node, frame_x, frame_y);
    wlr_scene_rect_set_size(grab->outline_top, frame_w, t);
    wlr_scene_node_set_position(&grab->outline_top->node, 0, 0);
    wlr_scene_rect_set_size(grab->outline_bottom, frame_w, t);
    wlr_scene_node_set_position(&grab->outline_bottom->node, 0, frame_h - t);
    wlr_scene_rect_set_size(grab->outline_left, t, frame_h);
    wlr_scene_node_set_position(&grab->outline_left->node, 0, 0);
    wlr_scene_rect_set_size(grab->outline_right, t, frame_h);
    wlr_scene_node_set_position(&grab->outline_right->node, frame_w - t, 0);

    wlr_scene_node_set_enabled(&grab->outline_tree->node, true);
    wlr_scene_node_raise_to_top(&grab->outline_tree->node);
}

static void grab_apply_pending(struct fbwl_grab *grab, struct wlr_output_layout *output_layout, const char *reason) {
    if (grab == NULL || !grab->pending_valid) {
        return;
    }
    struct fbwl_view *view = grab->view;
    if (view == NULL) {
        return;
    }

    const bool sync_tabs = !grab->tab_attach_enabled;

    view->x = grab->pending_x;
    view->y = grab->pending_y;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }
    fbwl_view_pseudo_bg_update(view, reason);

    const bool include_size = grab->mode == FBWL_CURSOR_RESIZE;
    if (include_size) {
        const int w = grab->pending_w;
        const int h = grab->pending_h;
        if (w > 0 && h > 0) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
                wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
            } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
                wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                    (uint16_t)w, (uint16_t)h);
            }
            if (sync_tabs) {
                fbwl_tabs_sync_geometry_from_view(view, true, w, h, reason);
            }
        }
    } else {
        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            const int w = grab->pending_w;
            const int h = grab->pending_h;
            if (w > 0 && h > 0) {
                wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                    (uint16_t)w, (uint16_t)h);
            }
        }
        if (sync_tabs) {
            fbwl_tabs_sync_geometry_from_view(view, false, 0, 0, reason);
        }
    }

    fbwl_view_foreign_update_output_from_position(view, output_layout);
}

static int grab_resize_timer_cb(void *data) {
    struct fbwl_grab *grab = data;
    if (grab == NULL || grab->mode != FBWL_CURSOR_RESIZE || grab->view == NULL || grab->view->server == NULL) {
        return 0;
    }
    grab_apply_pending(grab, grab->view->server->output_layout, "resize-delay");
    wlr_log(WLR_INFO, "Resize: apply-delay %s w=%d h=%d",
        fbwl_view_display_title(grab->view),
        grab->pending_w, grab->pending_h);
    return 0;
}

static bool snap_move_axis(int *pos, int size, int min, int max, int threshold) {
    if (pos == NULL || size < 1 || threshold <= 0) {
        return false;
    }
    if (max <= min || size > (max - min)) {
        *pos = min;
        return true;
    }
    const int dist_min = abs(*pos - min);
    const int dist_max = abs((*pos + size) - max);
    if (dist_min <= threshold && dist_min <= dist_max) {
        *pos = min;
        return true;
    }
    if (dist_max <= threshold && dist_max < dist_min) {
        *pos = max - size;
        return true;
    }
    return false;
}

static void view_frame_extents(const struct fbwl_view *view, int *left, int *top, int *right, int *bottom) {
    if (left != NULL) {
        *left = 0;
    }
    if (top != NULL) {
        *top = 0;
    }
    if (right != NULL) {
        *right = 0;
    }
    if (bottom != NULL) {
        *bottom = 0;
    }
    if (view == NULL || view->server == NULL) {
        return;
    }
    fbwl_view_decor_frame_extents(view, &view->server->decor_theme, left, top, right, bottom);
}

void fbwl_grab_begin_move(struct fbwl_grab *grab, struct fbwl_view *view, struct wlr_cursor *cursor,
        uint32_t button) {
    if (grab == NULL || view == NULL || cursor == NULL) {
        return;
    }

    grab->tab_attach_enabled = false;

    struct fbwl_server *server = view->server;
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool disable_move = cfg != NULL ? cfg->max_disable_move : (server != NULL && server->max_disable_move);
    if (disable_move && (view->fullscreen || view->maximized)) {
        fbwl_grab_end(grab);
        return;
    }
    if (server != NULL && server->output_layout != NULL && view->fullscreen) {
        fbwl_view_set_fullscreen(view, false, server->output_layout, &server->outputs,
            server->layer_normal, server->layer_fullscreen, NULL);
    }

    grab->view = view;
    grab->button = button;
    grab->resize_edges = WLR_EDGE_NONE;
    grab->grab_x = cursor->x;
    grab->grab_y = cursor->y;
    grab->view_x = view->x;
    grab->view_y = view->y;
    grab->view_w = fbwl_view_current_width(view);
    grab->view_h = fbwl_view_current_height(view);
    grab->last_w = grab->view_w;
    grab->last_h = grab->view_h;
    grab->last_cursor_valid = true;
    grab->last_cursor_x = (int)cursor->x;
    grab->last_cursor_y = (int)cursor->y;
    grab->pending_valid = true;
    grab->pending_x = grab->view_x;
    grab->pending_y = grab->view_y;
    grab->pending_w = grab->view_w;
    grab->pending_h = grab->view_h;
    grab_outline_destroy(grab);
    if (grab->resize_timer != NULL) {
        wl_event_source_remove(grab->resize_timer);
        grab->resize_timer = NULL;
    }
    grab->mode = FBWL_CURSOR_MOVE;
}

void fbwl_grab_begin_tabbing(struct fbwl_grab *grab, struct fbwl_view *view, struct wlr_cursor *cursor,
        uint32_t button) {
    fbwl_grab_begin_move(grab, view, cursor, button);
    if (grab != NULL && grab->mode == FBWL_CURSOR_MOVE && grab->view == view) {
        grab->tab_attach_enabled = true;
    }
}

void fbwl_grab_begin_resize(struct fbwl_grab *grab, struct fbwl_view *view, struct wlr_cursor *cursor,
        uint32_t button, uint32_t edges) {
    if (grab == NULL || view == NULL || cursor == NULL) {
        return;
    }

    grab->tab_attach_enabled = false;

    struct fbwl_server *server = view->server;
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool disable_resize = cfg != NULL ? cfg->max_disable_resize : (server != NULL && server->max_disable_resize);
    if (disable_resize && (view->fullscreen || view->maximized)) {
        fbwl_grab_end(grab);
        return;
    }
    if (server != NULL && server->output_layout != NULL && view->fullscreen) {
        fbwl_view_set_fullscreen(view, false, server->output_layout, &server->outputs,
            server->layer_normal, server->layer_fullscreen, NULL);
    }
    if (server != NULL && server->output_layout != NULL && view->maximized) {
        fbwl_view_set_maximized(view, false, server->output_layout, &server->outputs);
    }

    grab->view = view;
    grab->button = button;
    grab->resize_edges = edges;
    grab->grab_x = cursor->x;
    grab->grab_y = cursor->y;
    grab->view_x = view->x;
    grab->view_y = view->y;
    grab->view_w = fbwl_view_current_width(view);
    grab->view_h = fbwl_view_current_height(view);
    grab->last_w = grab->view_w;
    grab->last_h = grab->view_h;
    grab->last_cursor_valid = true;
    grab->last_cursor_x = (int)cursor->x;
    grab->last_cursor_y = (int)cursor->y;
    grab->pending_valid = true;
    grab->pending_x = grab->view_x;
    grab->pending_y = grab->view_y;
    grab->pending_w = grab->view_w;
    grab->pending_h = grab->view_h;
    grab_outline_destroy(grab);
    if (grab->resize_timer != NULL) {
        wl_event_source_remove(grab->resize_timer);
        grab->resize_timer = NULL;
    }
    grab->mode = FBWL_CURSOR_RESIZE;

    if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
        wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, true);
    }
}

void fbwl_grab_commit(struct fbwl_grab *grab, struct wlr_output_layout *output_layout, const char *why) {
    if (grab == NULL) {
        return;
    }
    if (grab->resize_timer != NULL) {
        wl_event_source_remove(grab->resize_timer);
        grab->resize_timer = NULL;
    }
    grab_outline_destroy(grab);
    grab_apply_pending(grab, output_layout, why);
}

void fbwl_grab_end(struct fbwl_grab *grab) {
    if (grab == NULL) {
        return;
    }

    struct fbwl_view *view = grab->view;
    if (view != NULL && grab->mode == FBWL_CURSOR_RESIZE && view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
        wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, false);
    }

    grab_outline_destroy(grab);
    if (grab->resize_timer != NULL) {
        wl_event_source_remove(grab->resize_timer);
        grab->resize_timer = NULL;
    }

    grab->mode = FBWL_CURSOR_PASSTHROUGH;
    grab->view = NULL;
    grab->button = 0;
    grab->resize_edges = WLR_EDGE_NONE;
    grab->tab_attach_enabled = false;
    grab->pending_valid = false;
    grab->last_cursor_valid = false;
}

void fbwl_grab_update(struct fbwl_grab *grab, struct wlr_cursor *cursor,
        struct wlr_output_layout *output_layout, struct wl_list *outputs,
        int edge_snap_threshold_px, int edge_resize_snap_threshold_px,
        bool opaque_move, bool opaque_resize, int opaque_resize_delay_ms) {
    if (grab == NULL || cursor == NULL) {
        return;
    }

    struct fbwl_view *view = grab->view;
    if (view == NULL) {
        return;
    }

    const double dx = cursor->x - grab->grab_x;
    const double dy = cursor->y - grab->grab_y;

    if (grab->mode == FBWL_CURSOR_MOVE) {
        int x = grab->view_x + (int)dx;
        int y = grab->view_y + (int)dy;
        if (edge_snap_threshold_px > 0 && output_layout != NULL && outputs != NULL) {
            struct wlr_box box = {0};
            struct wlr_output *output = wlr_output_layout_output_at(output_layout, cursor->x, cursor->y);
            fbwl_view_get_output_usable_box(view, output_layout, outputs, output, &box);
            if (box.width > 0 && box.height > 0) {
                int ext_left = 0;
                int ext_top = 0;
                int ext_right = 0;
                int ext_bottom = 0;
                const int w = fbwl_view_current_width(view);
                const int h = fbwl_view_current_height(view);
                view_frame_extents(view, &ext_left, &ext_top, &ext_right, &ext_bottom);
                const int frame_w = w + ext_left + ext_right;
                const int frame_h = h + ext_top + ext_bottom;
                int frame_x = x - ext_left;
                int frame_y = y - ext_top;
                (void)snap_move_axis(&frame_x, frame_w, box.x, box.x + box.width, edge_snap_threshold_px);
                (void)snap_move_axis(&frame_y, frame_h, box.y, box.y + box.height, edge_snap_threshold_px);
                x = frame_x + ext_left;
                y = frame_y + ext_top;
            }
        }
        grab->pending_valid = true;
        grab->pending_x = x;
        grab->pending_y = y;
        grab->pending_w = fbwl_view_current_width(view);
        grab->pending_h = fbwl_view_current_height(view);

        if (opaque_move) {
            grab_apply_pending(grab, output_layout, "move");
        } else {
            int ext_left = 0;
            int ext_top = 0;
            int ext_right = 0;
            int ext_bottom = 0;
            const int w = fbwl_view_current_width(view);
            const int h = fbwl_view_current_height(view);
            view_frame_extents(view, &ext_left, &ext_top, &ext_right, &ext_bottom);
            grab_outline_update(grab, view,
                x - ext_left, y - ext_top,
                w + ext_left + ext_right,
                h + ext_top + ext_bottom);
        }
        return;
    }

    if (grab->mode == FBWL_CURSOR_RESIZE) {
        uint32_t edges = grab->resize_edges;
        if (edges == WLR_EDGE_NONE) {
            edges = WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
        }

        int x = grab->view_x;
        int y = grab->view_y;
        int w = grab->view_w;
        int h = grab->view_h;

        if ((edges & WLR_EDGE_LEFT) != 0) {
            x = grab->view_x + (int)dx;
            w = grab->view_w - (int)dx;
        } else if ((edges & WLR_EDGE_RIGHT) != 0) {
            w = grab->view_w + (int)dx;
        }

        if ((edges & WLR_EDGE_TOP) != 0) {
            y = grab->view_y + (int)dy;
            h = grab->view_h - (int)dy;
        } else if ((edges & WLR_EDGE_BOTTOM) != 0) {
            h = grab->view_h + (int)dy;
        }

        if (w < 1) {
            w = 1;
            if ((edges & WLR_EDGE_LEFT) != 0) {
                x = grab->view_x + (grab->view_w - 1);
            }
        }
        if (h < 1) {
            h = 1;
            if ((edges & WLR_EDGE_TOP) != 0) {
                y = grab->view_y + (grab->view_h - 1);
            }
        }

        if (edge_resize_snap_threshold_px > 0 && output_layout != NULL && outputs != NULL) {
            struct wlr_box box = {0};
            struct wlr_output *output = wlr_output_layout_output_at(output_layout, cursor->x, cursor->y);
            fbwl_view_get_output_usable_box(view, output_layout, outputs, output, &box);
            if (box.width > 0 && box.height > 0) {
                int ext_left = 0;
                int ext_top = 0;
                int ext_right = 0;
                int ext_bottom = 0;
                view_frame_extents(view, &ext_left, &ext_top, &ext_right, &ext_bottom);
                int frame_l = x - ext_left;
                int frame_t = y - ext_top;
                int frame_r = x + w + ext_right;
                int frame_b = y + h + ext_bottom;
                const int box_r = box.x + box.width;
                const int box_b = box.y + box.height;
                if ((edges & WLR_EDGE_LEFT) != 0 && abs(frame_l - box.x) <= edge_resize_snap_threshold_px) {
                    frame_l = box.x;
                }
                if ((edges & WLR_EDGE_RIGHT) != 0 && abs(frame_r - box_r) <= edge_resize_snap_threshold_px) {
                    frame_r = box_r;
                }
                if ((edges & WLR_EDGE_TOP) != 0 && abs(frame_t - box.y) <= edge_resize_snap_threshold_px) {
                    frame_t = box.y;
                }
                if ((edges & WLR_EDGE_BOTTOM) != 0 && abs(frame_b - box_b) <= edge_resize_snap_threshold_px) {
                    frame_b = box_b;
                }
                x = frame_l + ext_left;
                y = frame_t + ext_top;
                w = (frame_r - frame_l) - ext_left - ext_right;
                h = (frame_b - frame_t) - ext_top - ext_bottom;
                if (w < 1) {
                    w = 1;
                    if ((edges & WLR_EDGE_LEFT) != 0) {
                        x = (frame_r - ext_right) - 1;
                    }
                }
                if (h < 1) {
                    h = 1;
                    if ((edges & WLR_EDGE_TOP) != 0) {
                        y = (frame_b - ext_bottom) - 1;
                    }
                }
            }
        }

        grab->last_w = w;
        grab->last_h = h;

        grab->pending_valid = true;
        grab->pending_x = x;
        grab->pending_y = y;
        grab->pending_w = w;
        grab->pending_h = h;

        if (!opaque_resize) {
            int ext_left = 0;
            int ext_top = 0;
            int ext_right = 0;
            int ext_bottom = 0;
            view_frame_extents(view, &ext_left, &ext_top, &ext_right, &ext_bottom);
            grab_outline_update(grab, view,
                x - ext_left, y - ext_top,
                w + ext_left + ext_right,
                h + ext_top + ext_bottom);
            return;
        }

        if (opaque_resize_delay_ms > 0) {
            if (grab->resize_timer == NULL) {
                struct fbwl_server *server = view->server;
                struct wl_event_loop *loop = server != NULL ? wl_display_get_event_loop(server->wl_display) : NULL;
                if (loop != NULL) {
                    grab->resize_timer = wl_event_loop_add_timer(loop, grab_resize_timer_cb, grab);
                }
            }
            if (grab->resize_timer != NULL) {
                wl_event_source_timer_update(grab->resize_timer, opaque_resize_delay_ms);
            }
            return;
        }

        grab_apply_pending(grab, output_layout, "resize");
        return;
    }
}
