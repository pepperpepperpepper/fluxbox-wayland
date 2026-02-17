#include "wayland/fbwl_view.h"
#include "wayland/fbwl_deco_mask.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include <xcb/xcb_icccm.h>
#include "wmcore/fbwm_output.h"
#include "wayland/fbwl_output.h"
#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_xwayland.h"
struct wlr_surface *fbwl_view_wlr_surface(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }
    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->base->surface : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->surface : NULL;
    default:
        return NULL;
    }
}
struct fbwl_view *fbwl_view_from_surface(struct wlr_surface *surface) {
    if (surface == NULL) {
        return NULL;
    }
    struct wlr_xdg_toplevel *xdg_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(surface);
    if (xdg_toplevel != NULL && xdg_toplevel->base != NULL) {
        struct wlr_scene_tree *tree = xdg_toplevel->base->data;
        if (tree != NULL && tree->node.data != NULL) {
            return tree->node.data;
        }
    }
    struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
    if (xsurface != NULL && xsurface->data != NULL) {
        return xsurface->data;
    }
    return NULL;
}
const char *fbwl_view_title(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }
    if (view->title_override != NULL && view->title_override[0] != '\0') {
        return view->title_override;
    }
    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->title : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->title : NULL;
    default:
        return NULL;
    }
}
const char *fbwl_view_app_id(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }
    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->app_id : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->class : NULL;
    default:
        return NULL;
    }
}
const char *fbwl_view_instance(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }
    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->app_id : NULL;
    case FBWL_VIEW_XWAYLAND:
        if (view->xwayland_surface == NULL) {
            return NULL;
        }
        if (view->xwayland_surface->instance != NULL && view->xwayland_surface->instance[0] != '\0') {
            return view->xwayland_surface->instance;
        }
        return view->xwayland_surface->class;
    default:
        return NULL;
    }
}
const char *fbwl_view_role(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }
    switch (view->type) {
    case FBWL_VIEW_XDG:
        return NULL;
    case FBWL_VIEW_XWAYLAND: {
        const char *role = view->xwayland_surface != NULL ? view->xwayland_surface->role : NULL;
        return (role != NULL && role[0] != '\0') ? role : view->xwayland_role_cache;
    }
    default:
        return NULL;
    }
}
bool fbwl_view_is_transient(const struct fbwl_view *view) {
    if (view == NULL) {
        return false;
    }
    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL && view->xdg_toplevel->parent != NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL && view->xwayland_surface->parent != NULL;
    default:
        return false;
    }
}
bool fbwl_view_is_icon_hidden(const struct fbwl_view *view) {
    if (view == NULL) {
        return false;
    }
    if (view->in_slit) {
        return true;
    }
    if (view->icon_hidden_override_set) {
        return view->icon_hidden_override;
    }
    if (view->type != FBWL_VIEW_XWAYLAND || view->xwayland_surface == NULL) {
        return false;
    }
    if (view->xwayland_surface->skip_taskbar) {
        return true;
    }
    const struct wlr_xwayland_surface *xsurface = view->xwayland_surface;
    return wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK) ||
        wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DESKTOP) ||
        wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH) ||
        wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR) ||
        wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_MENU);
}
bool fbwl_view_is_focus_hidden(const struct fbwl_view *view) {
    if (view == NULL) {
        return false;
    }
    if (view->in_slit) {
        return true;
    }
    if (view->focus_hidden_override_set) {
        return view->focus_hidden_override;
    }
    if (view->type != FBWL_VIEW_XWAYLAND || view->xwayland_surface == NULL) {
        return false;
    }
    const struct wlr_xwayland_surface *xsurface = view->xwayland_surface;
    return wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK) ||
        wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DESKTOP) ||
        wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH);
}
bool fbwl_view_is_urgent(const struct fbwl_view *view) {
    if (view == NULL) {
        return false;
    }
    if (view->attention_active) {
        return true;
    }
    if (view->type != FBWL_VIEW_XWAYLAND || view->xwayland_surface == NULL) {
        return false;
    }
    if (view->xwayland_surface->demands_attention) {
        return true;
    }
    if (view->xwayland_surface->hints != NULL &&
            (view->xwayland_surface->hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY) != 0) {
        return true;
    }
    return false;
}
const char *fbwl_view_display_title(const struct fbwl_view *view) {
    if (view == NULL) {
        return "(no-view)";
    }
    const char *title = fbwl_view_title(view);
    if (title != NULL && *title != '\0') {
        return title;
    }
    const char *app_id = fbwl_view_app_id(view);
    if (app_id != NULL && *app_id != '\0') {
        return app_id;
    }
    return "(no-title)";
}
int fbwl_view_current_width(const struct fbwl_view *view) {
    if (view == NULL) {
        return 0;
    }
    if (view->width > 0) {
        return view->width;
    }
    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    if (surface != NULL) {
        return surface->current.width;
    }
    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        return view->xwayland_surface->width;
    }
    return 0;
}
int fbwl_view_current_height(const struct fbwl_view *view) {
    if (view == NULL) {
        return 0;
    }
    if (view->height > 0) {
        return view->height;
    }
    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    if (surface != NULL) {
        return surface->current.height;
    }
    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        return view->xwayland_surface->height;
    }
    return 0;
}
void fbwl_view_set_activated(struct fbwl_view *view, bool activated) {
    if (view == NULL) {
        return;
    }
    switch (view->type) {
    case FBWL_VIEW_XDG:
        if (view->xdg_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(view->xdg_toplevel, activated);
        }
        break;
    case FBWL_VIEW_XWAYLAND:
        if (view->xwayland_surface != NULL) {
            wlr_xwayland_surface_activate(view->xwayland_surface, activated);
            if (activated) {
                wlr_xwayland_surface_offer_focus(view->xwayland_surface);
            }
        }
        break;
    default:
        break;
    }
}
void fbwl_view_save_geometry(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }
    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    view->saved_x = view->x;
    view->saved_y = view->y;
    view->saved_w = w > 0 ? w : 0;
    view->saved_h = h > 0 ? h : 0;
}
void fbwl_view_get_output_box(const struct fbwl_view *view, struct wlr_output_layout *output_layout,
        struct wlr_output *preferred, struct wlr_box *box) {
    *box = (struct wlr_box){0};
    if (view == NULL || output_layout == NULL) {
        return;
    }
    struct wlr_output *output = preferred;
    if (output == NULL) {
        const int w = fbwl_view_current_width(view);
        const int h = fbwl_view_current_height(view);
        double cx = view->x + (double)w / 2.0;
        double cy = view->y + (double)h / 2.0;
        output = wlr_output_layout_output_at(output_layout, cx, cy);
    }
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }
    wlr_output_layout_get_box(output_layout, output, box);
}
static void apply_toolbar_strut_box(const struct fbwl_server *server, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_output *output, struct wlr_box *box) {
    if (server == NULL || output_layout == NULL || outputs == NULL || output == NULL || box == NULL) {
        return;
    }
    if (box->width < 1 || box->height < 1) {
        return;
    }
    if (!server->toolbar_ui.enabled || server->toolbar_ui.max_over || server->toolbar_ui.auto_hide) { return; }
    const size_t on_head = server->toolbar_ui.on_head >= 0 ? (size_t)server->toolbar_ui.on_head : 0;
    struct wlr_output *toolbar_out = fbwl_screen_map_output_for_screen(output_layout, outputs, on_head);
    if (toolbar_out == NULL || toolbar_out != output) {
        return;
    }
    int thickness = server->toolbar_ui.thickness;
    if (thickness < 1) {
        thickness = server->toolbar_ui.height_override;
        if (thickness <= 0) {
            thickness = server->decor_theme.toolbar_height > 0 ? server->decor_theme.toolbar_height :
                (server->decor_theme.title_height > 0 ? server->decor_theme.title_height : 24);
        }
    }
    if (thickness < 1) thickness = 1;
    int border_w = server->decor_theme.toolbar_border_width;
    if (border_w < 0) border_w = 0;
    if (border_w > 20) border_w = 20;
    int bevel_w = server->decor_theme.toolbar_bevel_width;
    if (bevel_w < 0) bevel_w = 0;
    if (bevel_w > 20) bevel_w = 20;
    int t = thickness + 2 * bevel_w + 2 * border_w;
    if (t < 1) return;
    switch (server->toolbar_ui.placement) {
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        if (box->height <= t) {
            box->height = 0;
            return;
        }
        box->y += t;
        box->height -= t;
        break;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        if (box->height <= t) {
            box->height = 0;
            return;
        }
        box->height -= t;
        break;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
        if (box->width <= t) {
            box->width = 0;
            return;
        }
        box->x += t;
        box->width -= t;
        break;
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        if (box->width <= t) {
            box->width = 0;
            return;
        }
        box->width -= t;
        break;
    }
}

static void apply_slit_strut_box(const struct fbwl_server *server, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_output *output, struct wlr_box *box) {
    if (server == NULL || output_layout == NULL || outputs == NULL || output == NULL || box == NULL) {
        return;
    }
    if (box->width < 1 || box->height < 1) {
        return;
    }
    if (!server->slit_ui.enabled || server->slit_ui.max_over || server->slit_ui.auto_hide) {
        return;
    }

    const size_t on_head = server->slit_ui.on_head >= 0 ? (size_t)server->slit_ui.on_head : 0;
    struct wlr_output *slit_out = fbwl_screen_map_output_for_screen(output_layout, outputs, on_head);
    if (slit_out == NULL || slit_out != output) {
        return;
    }

    const enum fbwl_toolbar_placement placement = server->slit_ui.placement;
    const int t = placement == FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM || placement == FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER ||
            placement == FBWL_TOOLBAR_PLACEMENT_LEFT_TOP || placement == FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM ||
            placement == FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER || placement == FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP ?
        server->slit_ui.width : server->slit_ui.height;
    if (t < 1) {
        return;
    }
    switch (server->slit_ui.placement) {
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        if (box->height <= t) {
            box->height = 0;
            return;
        }
        box->y += t;
        box->height -= t;
        break;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        if (box->height <= t) {
            box->height = 0;
            return;
        }
        box->height -= t;
        break;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
        if (box->width <= t) {
            box->width = 0;
            return;
        }
        box->x += t;
        box->width -= t;
        break;
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        if (box->width <= t) {
            box->width = 0;
            return;
        }
        box->width -= t;
        break;
    }
}

static void apply_configured_struts_box(const struct fbwl_server *server, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_output *output, struct wlr_box *box) {
    if (server == NULL || output_layout == NULL || outputs == NULL || output == NULL || box == NULL) {
        return;
    }
    if (box->width < 1 || box->height < 1) {
        return;
    }

    bool found = false;
    const size_t screen = fbwl_screen_map_screen_for_output(output_layout, outputs, output, &found);
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config(server, found ? screen : 0);
    if (cfg == NULL) {
        return;
    }

    int l = cfg->struts.left_px;
    int r = cfg->struts.right_px;
    int t = cfg->struts.top_px;
    int b = cfg->struts.bottom_px;
    if (l < 0) { l = 0; }
    if (r < 0) { r = 0; }
    if (t < 0) { t = 0; }
    if (b < 0) { b = 0; }

    const int64_t w = box->width;
    const int64_t h = box->height;
    if ((int64_t)l + (int64_t)r >= w || (int64_t)t + (int64_t)b >= h) {
        box->width = 0;
        box->height = 0;
        return;
    }

    box->x += l;
    box->y += t;
    box->width -= l + r;
    box->height -= t + b;
}
void fbwl_view_get_output_usable_box(const struct fbwl_view *view, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_output *preferred, struct wlr_box *box) {
    *box = (struct wlr_box){0};
    if (view == NULL || output_layout == NULL || outputs == NULL) {
        return;
    }
    struct wlr_output *output = preferred;
    if (output == NULL) {
        const int w = fbwl_view_current_width(view);
        const int h = fbwl_view_current_height(view);
        double cx = view->x + (double)w / 2.0;
        double cy = view->y + (double)h / 2.0;
        output = wlr_output_layout_output_at(output_layout, cx, cy);
    }
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }
    if (output == NULL) {
        return;
    }
    struct fbwl_output *out = fbwl_output_find(outputs, output);
    if (out != NULL && out->usable_area.width > 0 && out->usable_area.height > 0) {
        *box = out->usable_area;
        apply_toolbar_strut_box(view->server, output_layout, outputs, output, box);
        apply_slit_strut_box(view->server, output_layout, outputs, output, box);
        apply_configured_struts_box(view->server, output_layout, outputs, output, box);
        return;
    }
    wlr_output_layout_get_box(output_layout, output, box);
    apply_toolbar_strut_box(view->server, output_layout, outputs, output, box);
    apply_slit_strut_box(view->server, output_layout, outputs, output, box);
    apply_configured_struts_box(view->server, output_layout, outputs, output, box);
}

void fbwl_view_apply_tabs_maxover_box(const struct fbwl_view *view, struct wlr_box *box) {
    if (view == NULL || box == NULL) {
        return;
    }
    if (box->width < 1 || box->height < 1) {
        return;
    }

    const struct fbwl_server *server = view->server;
    if (server == NULL) {
        return;
    }
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const struct fbwl_tabs_config *tabs = cfg != NULL ? &cfg->tabs : &server->tabs;
    const bool intitlebar = tabs->intitlebar && (view->decor_mask & FBWL_DECORM_TITLEBAR) != 0;
    if (intitlebar || tabs->max_over) {
        return;
    }
    if (!view->decor_enabled || view->tab_group == NULL || (view->decor_mask & FBWL_DECORM_TAB) == 0) {
        return;
    }

    int frame_left = 0;
    int frame_top = 0;
    int frame_right = 0;
    int frame_bottom = 0;
    fbwl_view_decor_frame_extents(view, &server->decor_theme, &frame_left, &frame_top, &frame_right, &frame_bottom);
    int border = frame_left > frame_right ? frame_left : frame_right;

    int thickness = 0;
    switch (tabs->placement) {
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        thickness = server->decor_theme.title_height;
        break;
    default:
        thickness = tabs->width_px;
        break;
    }
    if (thickness < 1) {
        thickness = server->decor_theme.title_height > 0 ? server->decor_theme.title_height : 24;
    }
    if (thickness < 1) {
        thickness = 1;
    }

    const int off = thickness + border;
    if (off < 1) {
        return;
    }

    switch (tabs->placement) {
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        if (box->height <= off) {
            box->height = 0;
            return;
        }
        box->y += off;
        box->height -= off;
        break;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        if (box->height <= off) {
            box->height = 0;
            return;
        }
        box->height -= off;
        break;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
        if (box->width <= off) {
            box->width = 0;
            return;
        }
        box->x += off;
        box->width -= off;
        break;
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        if (box->width <= off) {
            box->width = 0;
            return;
        }
        box->width -= off;
        break;
    }
}
static void view_foreign_set_output(struct fbwl_view *view, struct wlr_output *output) {
    if (view == NULL || view->foreign_toplevel == NULL) {
        return;
    }
    if (output == view->foreign_output) {
        return;
    }
    if (view->foreign_output != NULL) {
        wlr_foreign_toplevel_handle_v1_output_leave(view->foreign_toplevel, view->foreign_output);
    }
    view->foreign_output = output;
    if (output != NULL) {
        wlr_foreign_toplevel_handle_v1_output_enter(view->foreign_toplevel, output);
    }
}
void fbwl_view_foreign_update_output_from_position(struct fbwl_view *view, struct wlr_output_layout *output_layout) {
    if (view == NULL || output_layout == NULL) {
        return;
    }
    struct wlr_output *output = wlr_output_layout_output_at(output_layout, view->x + 1, view->y + 1);
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }
    view_foreign_set_output(view, output);
}
struct fbwl_wm_output {
    struct fbwm_output wm_output;
    struct fbwl_server *server;
    struct wlr_output_layout *output_layout;
    struct wl_list *outputs;
    struct wlr_output *wlr_output;
};
static const char *fbwl_wm_output_name(const struct fbwm_output *wm_output) {
    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    return output != NULL && output->wlr_output != NULL ? output->wlr_output->name : NULL;
}
static bool fbwl_wm_output_full_box(const struct fbwm_output *wm_output, struct fbwm_box *out) {
    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    if (output == NULL || output->output_layout == NULL || output->wlr_output == NULL || out == NULL) {
        return false;
    }
    struct wlr_box box = {0};
    wlr_output_layout_get_box(output->output_layout, output->wlr_output, &box);
    *out = (struct fbwm_box){
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height,
    };
    return true;
}
static bool fbwl_wm_output_usable_box(const struct fbwm_output *wm_output, struct fbwm_box *out) {
    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    if (output == NULL || output->outputs == NULL || output->wlr_output == NULL || out == NULL) {
        return false;
    }
    struct fbwl_output *out_data = fbwl_output_find(output->outputs, output->wlr_output);
    if (out_data == NULL || out_data->usable_area.width < 1 || out_data->usable_area.height < 1) {
        return false;
    }
    struct wlr_box box = out_data->usable_area;
    apply_toolbar_strut_box(output->server, output->output_layout, output->outputs, output->wlr_output, &box);
    apply_slit_strut_box(output->server, output->output_layout, output->outputs, output->wlr_output, &box);
    apply_configured_struts_box(output->server, output->output_layout, output->outputs, output->wlr_output, &box);
    if (box.width < 1 || box.height < 1) {
        return false;
    }
    *out = (struct fbwm_box){
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height,
    };
    return true;
}
static const struct fbwm_output_ops fbwl_wm_output_ops = {
    .name = fbwl_wm_output_name,
    .full_box = fbwl_wm_output_full_box,
    .usable_box = fbwl_wm_output_usable_box,
};
void fbwl_view_place_initial(struct fbwl_view *view, struct fbwm_core *wm, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, double cursor_x, double cursor_y) {
    if (view == NULL || wm == NULL || output_layout == NULL) {
        return;
    }
    struct wlr_output *output = wlr_output_layout_output_at(output_layout, cursor_x, cursor_y);
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }
    struct fbwl_wm_output wm_out = {0};
    const struct fbwm_output *wm_output = NULL;
    if (output != NULL) {
        wm_out.server = view->server;
        wm_out.output_layout = output_layout;
        wm_out.outputs = outputs;
        wm_out.wlr_output = output;
        fbwm_output_init(&wm_out.wm_output, &fbwl_wm_output_ops, &wm_out);
        wm_output = &wm_out.wm_output;
    }
    struct fbwm_box full = {0};
    struct fbwm_box box = {0};
    if (wm_output != NULL) {
        (void)fbwm_output_get_full_box(wm_output, &full);
        if (!fbwm_output_get_usable_box(wm_output, &box) || box.width < 1 || box.height < 1) {
            box = full;
        }
    }
    int frame_left = 0;
    int frame_top = 0;
    int frame_right = 0;
    int frame_bottom = 0;
    const struct fbwl_decor_theme *theme = view->server != NULL ? &view->server->decor_theme : NULL;
    if (theme != NULL) {
        fbwl_view_decor_frame_extents(view, theme, &frame_left, &frame_top, &frame_right, &frame_bottom);
    }
    int frame_w = fbwl_view_current_width(view) + frame_left + frame_right;
    int frame_h = fbwl_view_current_height(view) + frame_top + frame_bottom;
    int x = 0;
    int y = 0;

    const struct fbwl_screen_config *cfg = NULL;
    if (view->server != NULL) {
        cfg = fbwl_server_screen_config_at(view->server, cursor_x, cursor_y);
    }
    const enum fbwm_window_placement_strategy prev_place = fbwm_core_window_placement(wm);
    const enum fbwm_row_placement_direction prev_row = fbwm_core_row_placement_direction(wm);
    const enum fbwm_col_placement_direction prev_col = fbwm_core_col_placement_direction(wm);
    if (cfg != NULL) {
        fbwm_core_set_window_placement(wm, cfg->placement_strategy);
        fbwm_core_set_row_placement_direction(wm, cfg->placement_row_dir);
        fbwm_core_set_col_placement_direction(wm, cfg->placement_col_dir);
    }
    fbwm_core_place_next(wm, wm_output, frame_w, frame_h, (int)cursor_x, (int)cursor_y, &x, &y);
    if (cfg != NULL) {
        fbwm_core_set_window_placement(wm, prev_place);
        fbwm_core_set_row_placement_direction(wm, prev_row);
        fbwm_core_set_col_placement_direction(wm, prev_col);
    }
    view->x = x + frame_left;
    view->y = y + frame_top;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y); fbwl_view_pseudo_bg_update(view, "place");
    }
    wlr_log(WLR_INFO, "Place: %s out=%s x=%d y=%d usable=%d,%d %dx%d full=%d,%d %dx%d",
        fbwl_view_display_title(view),
        fbwm_output_name(wm_output) != NULL ? fbwm_output_name(wm_output) : "(none)",
        view->x, view->y,
        box.x, box.y, box.width, box.height,
        full.x, full.y, full.width, full.height);
    view_foreign_set_output(view, output);
}
void fbwl_view_set_maximized(struct fbwl_view *view, bool maximized, struct wlr_output_layout *output_layout,
        struct wl_list *outputs) {
    if (view == NULL) {
        return;
    }
    if (view->fullscreen) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }
    const bool any_max = view->maximized || view->maximized_h || view->maximized_v;
    if ((maximized && view->maximized) || (!maximized && !any_max)) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    struct fbwl_server *server = view->server;
    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);

    if (maximized) {
        const bool had_axes = view->maximized_h || view->maximized_v;
        if (!had_axes || view->saved_w < 1 || view->saved_h < 1) {
            fbwl_view_save_geometry(view);
        }
        const struct fbwl_screen_config *cfg =
            server != NULL ? fbwl_server_screen_config_for_view(server, view) : NULL;
        const bool full_max = cfg != NULL ? cfg->full_maximization : (server != NULL && server->full_maximization);
        struct wlr_box box;
        full_max ?
            fbwl_view_get_output_box(view, output_layout, NULL, &box) :
            fbwl_view_get_output_usable_box(view, output_layout, outputs, NULL, &box);
        fbwl_view_apply_tabs_maxover_box(view, &box);
        if (box.width < 1 || box.height < 1) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
                wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
            }
            return;
        }
        const struct fbwl_decor_theme *theme = view->server != NULL ? &view->server->decor_theme : NULL;
        int frame_left = 0;
        int frame_top = 0;
        int frame_right = 0;
        int frame_bottom = 0;
        if (theme != NULL) {
            fbwl_view_decor_frame_extents(view, theme, &frame_left, &frame_top, &frame_right, &frame_bottom);
        }
        int w = box.width - frame_left - frame_right;
        int h = box.height - frame_top - frame_bottom;
        if (w < 1 || h < 1) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
                wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
            }
            return;
        }
        const bool ignore_inc = view->ignore_size_hints_override_set ? view->ignore_size_hints_override :
            (cfg != NULL ? cfg->max_ignore_increment : (server == NULL || server->max_ignore_increment));
        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL && !ignore_inc) {
            fbwl_xwayland_apply_size_hints(view->xwayland_surface, &w, &h, true);
        }
        view->maximized = true;
        view->maximized_h = true;
        view->maximized_v = true;
        view->x = box.x + frame_left;
        view->y = box.y + frame_top;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y); fbwl_view_pseudo_bg_update(view, "maximize");
            wlr_scene_node_raise_to_top(&view->scene_tree->node);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, true);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_maximized(view->xwayland_surface, true, true);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, true);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)w, (uint16_t)h);
        }
        fbwl_tabs_sync_geometry_from_view(view, true, w, h, "maximize-on");
        fbwl_view_foreign_update_output_from_position(view, output_layout);
        wlr_log(WLR_INFO, "Maximize: %s on w=%d h=%d", fbwl_view_display_title(view), w, h);
        server_strict_mousefocus_recheck_after_restack(server, before, "maximize-on");
    } else {
        view->maximized = false;
        view->maximized_h = false;
        view->maximized_v = false;
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, false);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_maximized(view->xwayland_surface, false, false);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, false);
        }
        view->x = view->saved_x;
        view->y = view->saved_y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y); fbwl_view_pseudo_bg_update(view, "unmaximize");
            wlr_scene_node_raise_to_top(&view->scene_tree->node);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel,
                view->saved_w > 0 ? view->saved_w : 0,
                view->saved_h > 0 ? view->saved_h : 0);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)(view->saved_w > 0 ? view->saved_w : 1),
                (uint16_t)(view->saved_h > 0 ? view->saved_h : 1));
        }
        fbwl_tabs_sync_geometry_from_view(view, true, view->saved_w > 0 ? view->saved_w : fbwl_view_current_width(view),
            view->saved_h > 0 ? view->saved_h : fbwl_view_current_height(view), "maximize-off");
        fbwl_view_foreign_update_output_from_position(view, output_layout);
        wlr_log(WLR_INFO, "Maximize: %s off", fbwl_view_display_title(view));
        server_strict_mousefocus_recheck_after_restack(server, before, "maximize-off");
    }
    if (server != NULL) {
        server_toolbar_ui_rebuild(server);
    }
}
void fbwl_view_set_fullscreen(struct fbwl_view *view, bool fullscreen, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_scene_tree *layer_normal, struct wlr_scene_tree *layer_fullscreen,
        struct wlr_output *output) {
    if (view == NULL) {
        return;
    }
    if (fullscreen == view->fullscreen) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    struct fbwl_server *server = view->server;
    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);

    if (fullscreen) {
        if (view->maximized || view->maximized_h || view->maximized_v) {
            fbwl_view_set_maximized(view, false, output_layout, outputs);
        }
        fbwl_view_save_geometry(view);
        struct wlr_box box;
        fbwl_view_get_output_box(view, output_layout, output, &box);
        if (box.width < 1 || box.height < 1) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
                wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
            }
            server_strict_mousefocus_recheck_after_restack(server, before, "fullscreen-on");
            return;
        }
        view->fullscreen = true;
        fbwl_view_decor_apply_enabled(view);
        if (layer_fullscreen != NULL && view->scene_tree != NULL) {
            wlr_scene_node_reparent(&view->scene_tree->node, layer_fullscreen);
        }
        view->x = box.x;
        view->y = box.y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y); fbwl_view_pseudo_bg_update(view, "fullscreen-on");
            wlr_scene_node_raise_to_top(&view->scene_tree->node);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, true);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, true);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, true);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, box.width, box.height);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)box.width, (uint16_t)box.height);
        }
        fbwl_view_foreign_update_output_from_position(view, output_layout);
        wlr_log(WLR_INFO, "Fullscreen: %s on w=%d h=%d", fbwl_view_display_title(view), box.width, box.height);
        server_strict_mousefocus_recheck_after_restack(server, before, "fullscreen-on");
    } else {
        view->fullscreen = false;
        fbwl_view_decor_apply_enabled(view);
        struct wlr_scene_tree *restore_layer = view->base_layer != NULL ? view->base_layer : layer_normal;
        if (restore_layer != NULL && view->scene_tree != NULL) {
            wlr_scene_node_reparent(&view->scene_tree->node, restore_layer);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, false);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, false);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, false);
        }
        view->x = view->saved_x;
        view->y = view->saved_y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y); fbwl_view_pseudo_bg_update(view, "fullscreen-off");
            wlr_scene_node_raise_to_top(&view->scene_tree->node);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel,
                view->saved_w > 0 ? view->saved_w : 0,
                view->saved_h > 0 ? view->saved_h : 0);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)(view->saved_w > 0 ? view->saved_w : 1),
                (uint16_t)(view->saved_h > 0 ? view->saved_h : 1));
        }
        fbwl_view_foreign_update_output_from_position(view, output_layout);
        wlr_log(WLR_INFO, "Fullscreen: %s off", fbwl_view_display_title(view));
        server_strict_mousefocus_recheck_after_restack(server, before, "fullscreen-off");
    }
    if (server != NULL) {
        server_toolbar_ui_rebuild(server);
    }
}
struct fbwl_view *fbwl_view_at(struct wlr_scene *scene, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    *surface = NULL;
    *sx = 0;
    *sy = 0;
    if (scene == NULL) {
        return NULL;
    }
    double node_x = 0, node_y = 0;
    struct wlr_scene_node *node = wlr_scene_node_at(&scene->tree.node, lx, ly, &node_x, &node_y);
    if (node == NULL) {
        return NULL;
    }
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *scene_surface =
            wlr_scene_surface_try_from_buffer(scene_buffer);
        if (scene_surface != NULL) {
            *surface = scene_surface->surface;
            *sx = node_x;
            *sy = node_y;
        }
    }
    struct wlr_scene_node *walk = node;
    while (walk != NULL && walk->data == NULL) {
        walk = walk->parent != NULL ? &walk->parent->node : NULL;
    }
    return walk != NULL ? walk->data : NULL;
}
