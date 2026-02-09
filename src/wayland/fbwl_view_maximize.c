#include "wayland/fbwl_view.h"

#include <stdbool.h>
#include <stdint.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"

void fbwl_view_set_maximized_axes(struct fbwl_view *view, bool maximized_h, bool maximized_v,
        struct wlr_output_layout *output_layout, struct wl_list *outputs) {
    if (view == NULL || output_layout == NULL) {
        return;
    }
    if (maximized_h && maximized_v) {
        fbwl_view_set_maximized(view, true, output_layout, outputs);
        return;
    }
    if (!maximized_h && !maximized_v) {
        fbwl_view_set_maximized(view, false, output_layout, outputs);
        return;
    }

    if (view->fullscreen) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    if (view->maximized_h == maximized_h && view->maximized_v == maximized_v) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    struct fbwl_server *server = view->server;
    struct fbwl_view *before = server != NULL ? server_strict_mousefocus_view_under_cursor(server) : NULL;

    const bool had_axes = view->maximized_h || view->maximized_v;
    if (!had_axes || view->saved_w < 1 || view->saved_h < 1) {
        fbwl_view_save_geometry(view);
    }

    const int cur_h = fbwl_view_current_height(view);
    const int cur_w = fbwl_view_current_width(view);
    if (cur_w < 1 || cur_h < 1) {
        return;
    }

    const struct fbwl_screen_config *cfg = server != NULL ? fbwl_server_screen_config_for_view(server, view) : NULL;
    const bool full_max = cfg != NULL ? cfg->full_maximization : (server != NULL && server->full_maximization);

    struct wlr_box box;
    if (full_max) {
        fbwl_view_get_output_box(view, output_layout, NULL, &box);
    } else {
        fbwl_view_get_output_usable_box(view, output_layout, outputs, NULL, &box);
    }
    fbwl_view_apply_tabs_maxover_box(view, &box);

    if (box.width < 1 || box.height < 1) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    int x = view->x;
    int y = view->y;
    int w = cur_w;
    int h = cur_h;

    if (maximized_h) {
        x = box.x;
        w = box.width;
    } else {
        x = view->saved_x;
        w = view->saved_w > 0 ? view->saved_w : cur_w;
    }

    if (maximized_v) {
        y = box.y;
        h = box.height;
    } else {
        y = view->saved_y;
        h = view->saved_h > 0 ? view->saved_h : cur_h;
    }

    if (server != NULL && view->decor_enabled) {
        const int border = server->decor_theme.border_width;
        const int title_h = server->decor_theme.title_height;
        if (maximized_h) {
            x += border;
            w -= 2 * border;
        }
        if (maximized_v) {
            y += title_h + border;
            h -= title_h + 2 * border;
        }
    }
    if (w < 1 || h < 1) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    view->maximized_h = maximized_h;
    view->maximized_v = maximized_v;
    view->maximized = maximized_h && maximized_v;
    view->x = x;
    view->y = y;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }

    if (view->type == FBWL_VIEW_XDG) {
        wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, view->maximized);
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
    } else if (view->type == FBWL_VIEW_XWAYLAND) {
        wlr_xwayland_surface_set_maximized(view->xwayland_surface, view->maximized_h, view->maximized_v);
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)w, (uint16_t)h);
    }
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, view->maximized);
    }

    fbwl_tabs_sync_geometry_from_view(view, true, w, h, maximized_h ? "maximize-h-set" : "maximize-v-set");
    fbwl_view_foreign_update_output_from_position(view, output_layout);
    wlr_log(WLR_INFO, "MaximizeAxes: %s horz=%d vert=%d w=%d h=%d", fbwl_view_display_title(view),
        maximized_h ? 1 : 0, maximized_v ? 1 : 0, w, h);
    if (server != NULL) {
        server_strict_mousefocus_recheck_after_restack(server, before, maximized_h ? "maximize-h-set" : "maximize-v-set");
        server_toolbar_ui_rebuild(server);
    }
}
