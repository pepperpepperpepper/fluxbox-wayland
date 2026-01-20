#include "wayland/fbwl_grabs.h"

#include <stdint.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_view.h"

void fbwl_grab_update(struct fbwl_grab *grab, struct wlr_cursor *cursor) {
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
        view->x = grab->view_x + (int)dx;
        view->y = grab->view_y + (int)dy;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            const int w = fbwl_view_current_width(view);
            const int h = fbwl_view_current_height(view);
            if (w > 0 && h > 0) {
                wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                    (uint16_t)w, (uint16_t)h);
            }
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

        view->x = x;
        view->y = y;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);

        grab->last_w = w;
        grab->last_h = h;
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
        } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)w, (uint16_t)h);
        }
        return;
    }
}

