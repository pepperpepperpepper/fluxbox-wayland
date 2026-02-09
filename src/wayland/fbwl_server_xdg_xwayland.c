#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wmcore/fbwm_output.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_xembed_sni_proxy.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_attention.h"
#include "wayland/fbwl_view_foreign_toplevel.h"

static xcb_atom_t intern_atom(xcb_connection_t *conn, const char *name) {
    if (conn == NULL || name == NULL) {
        return XCB_ATOM_NONE;
    }
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return XCB_ATOM_NONE;
    }
    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

static bool xwayland_window_prop_has_any_data(xcb_connection_t *conn, xcb_window_t win, xcb_atom_t prop, xcb_atom_t type) {
    if (conn == NULL || win == XCB_WINDOW_NONE || prop == XCB_ATOM_NONE) {
        return false;
    }
    xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, win, prop, type, 0, 1);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return false;
    }
    const bool ok = xcb_get_property_value_length(reply) > 0;
    free(reply);
    return ok;
}

static bool xwayland_window_prop_u32_nonzero(xcb_connection_t *conn, xcb_window_t win, xcb_atom_t prop, xcb_atom_t type) {
    if (conn == NULL || win == XCB_WINDOW_NONE || prop == XCB_ATOM_NONE) {
        return false;
    }
    xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, win, prop, type, 0, 1);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return false;
    }
    bool ok = false;
    if (reply->format == 32 && xcb_get_property_value_length(reply) >= 4) {
        const uint32_t *vals = (const uint32_t *)xcb_get_property_value(reply);
        ok = vals != NULL && vals[0] != 0;
    }
    free(reply);
    return ok;
}

static bool xwayland_surface_is_kde_dockapp(struct wlr_xwayland *xwayland, const struct wlr_xwayland_surface *xsurface) {
    if (xwayland == NULL || xsurface == NULL) {
        return false;
    }
    xcb_connection_t *conn = wlr_xwayland_get_xwm_connection(xwayland);
    if (conn == NULL) {
        return false;
    }

    const xcb_atom_t atom_kwm_dockwindow = intern_atom(conn, "KWM_DOCKWINDOW");
    const xcb_atom_t atom_kde_systray = intern_atom(conn, "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR");

    if (atom_kde_systray != XCB_ATOM_NONE &&
            xwayland_window_prop_has_any_data(conn, xsurface->window_id, atom_kde_systray, XCB_ATOM_WINDOW)) {
        return true;
    }

    // Fluxbox/X11 checks that the first 32-bit value is non-zero.
    if (atom_kwm_dockwindow != XCB_ATOM_NONE &&
            xwayland_window_prop_u32_nonzero(conn, xsurface->window_id, atom_kwm_dockwindow, atom_kwm_dockwindow)) {
        return true;
    }

    return false;
}

static void fbwl_wm_view_focus(struct fbwm_view *wm_view) {
    struct fbwl_view *view = wm_view->userdata;
    focus_view(view);
}

static bool fbwl_wm_view_is_mapped(const struct fbwm_view *wm_view) {
    const struct fbwl_view *view = wm_view->userdata;
    return view != NULL && view->mapped && !view->minimized;
}

static const char *fbwl_wm_view_title(const struct fbwm_view *wm_view) {
    const struct fbwl_view *view = wm_view->userdata;
    return fbwl_view_title(view);
}

static const char *fbwl_wm_view_app_id(const struct fbwm_view *wm_view) {
    const struct fbwl_view *view = wm_view->userdata;
    return fbwl_view_app_id(view);
}

static int fbwl_wm_view_head(const struct fbwm_view *wm_view) {
    const struct fbwl_view *view = wm_view != NULL ? wm_view->userdata : NULL;
    const struct fbwl_server *server = view != NULL ? view->server : NULL;
    if (server == NULL) {
        return 0;
    }
    const size_t head = fbwl_server_screen_index_for_view(server, view);
    if (head > (size_t)INT_MAX) {
        return 0;
    }
    return (int)head;
}

static bool fbwl_wm_view_get_box(const struct fbwm_view *wm_view, struct fbwm_box *out) {
    const struct fbwl_view *view = wm_view != NULL ? wm_view->userdata : NULL;
    if (view == NULL || out == NULL) {
        return false;
    }

    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    if (w < 1 || h < 1) {
        return false;
    }

    int x = view->x;
    int y = view->y;
    int fw = w;
    int fh = h;

    if (view->decor_enabled && !view->fullscreen) {
        const struct fbwl_decor_theme *theme = &view->server->decor_theme;
        const int border = theme->border_width;
        const int title_h = theme->title_height;

        x -= border;
        y -= title_h + border;
        fw += 2 * border;
        fh += title_h + 2 * border;
    }

    *out = (struct fbwm_box){
        .x = x,
        .y = y,
        .width = fw,
        .height = fh,
    };
    return true;
}

const struct fbwm_view_ops fbwl_wm_view_ops = {
    .focus = fbwl_wm_view_focus,
    .is_mapped = fbwl_wm_view_is_mapped,
    .get_box = fbwl_wm_view_get_box,
    .head = fbwl_wm_view_head,
    .title = fbwl_wm_view_title,
    .app_id = fbwl_wm_view_app_id,
};

static void xdg_shell_apply_workspace_visibility(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    apply_workspace_visibility(server, why);
}

static void xdg_shell_toolbar_rebuild(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    server_toolbar_ui_rebuild(server);
}

static void xdg_shell_clear_keyboard_focus(void *userdata) {
    struct fbwl_server *server = userdata;
    clear_keyboard_focus(server);
}

static void xdg_shell_clear_focused_view_if_matches(void *userdata, struct fbwl_view *view) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->focused_view == view) {
        server->focused_view = NULL;
    }
}

struct fbwl_xdg_shell_hooks xdg_shell_hooks(struct fbwl_server *server) {
    return (struct fbwl_xdg_shell_hooks){
        .userdata = server,
        .apply_workspace_visibility = xdg_shell_apply_workspace_visibility,
        .toolbar_rebuild = xdg_shell_toolbar_rebuild,
        .clear_keyboard_focus = xdg_shell_clear_keyboard_focus,
        .clear_focused_view_if_matches = xdg_shell_clear_focused_view_if_matches,
        .apps_rules_apply_pre_map = server_apps_rules_apply_pre_map,
        .apps_rules_apply_post_map = server_apps_rules_apply_post_map,
        .view_set_minimized = view_set_minimized,
    };
}

struct fbwl_xwayland_hooks xwayland_hooks(struct fbwl_server *server) {
    return (struct fbwl_xwayland_hooks){
        .userdata = server,
        .apply_workspace_visibility = xdg_shell_apply_workspace_visibility,
        .toolbar_rebuild = xdg_shell_toolbar_rebuild,
        .clear_keyboard_focus = xdg_shell_clear_keyboard_focus,
        .clear_focused_view_if_matches = xdg_shell_clear_focused_view_if_matches,
        .apps_rules_apply_pre_map = server_apps_rules_apply_pre_map,
        .apps_rules_apply_post_map = server_apps_rules_apply_post_map,
        .view_set_minimized = view_set_minimized,
    };
}

static bool xwayland_surface_should_be_in_slit(const struct fbwl_server *server, const struct wlr_xwayland_surface *xsurface) {
    if (xsurface == NULL) {
        return false;
    }
    if (xsurface->strut_partial != NULL) {
        // Panels should remain regular XWayland windows for now.
        return false;
    }
    if (wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK)) {
        return true;
    }
    if (server != NULL && server->slit_ui.accept_kde_dockapps &&
            xwayland_surface_is_kde_dockapp(server->xwayland, xsurface)) {
        return true;
    }
    return false;
}

static void server_apps_rule_matchlimit_inc(struct fbwl_server *server, const struct fbwl_view *view) {
    if (server == NULL || view == NULL || server->apps_rules == NULL) {
        return;
    }
    if (!view->apps_rule_index_valid ||
            view->apps_rules_generation != server->apps_rules_generation ||
            view->apps_rule_index >= server->apps_rule_count) {
        return;
    }
    struct fbwl_apps_rule *rule = &server->apps_rules[view->apps_rule_index];
    if (rule->match_limit <= 0) {
        return;
    }
    if (rule->match_count < INT_MAX) {
        rule->match_count++;
    }
}

static void server_apps_rule_matchlimit_dec(struct fbwl_server *server, const struct fbwl_view *view) {
    if (server == NULL || view == NULL || server->apps_rules == NULL) {
        return;
    }
    if (!view->apps_rule_index_valid ||
            view->apps_rules_generation != server->apps_rules_generation ||
            view->apps_rule_index >= server->apps_rule_count) {
        return;
    }
    struct fbwl_apps_rule *rule = &server->apps_rules[view->apps_rule_index];
    if (rule->match_limit <= 0) {
        return;
    }
    if (rule->match_count > 0) {
        rule->match_count--;
    }
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, map);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    const bool rules_applied_before = view->apps_rules_applied;
    fbwl_xdg_shell_handle_toplevel_map(view, &server->wm, server->output_layout, &server->outputs,
        server->cursor->x, server->cursor->y, server->apps_rules, server->apps_rule_count, &hooks);
    if (!rules_applied_before && view->apps_rules_applied) {
        server_apps_rule_matchlimit_inc(server, view);
    }
    if (!fbwm_core_view_is_visible(&server->wm, &view->wm_view)) {
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    bool focus_new = cfg != NULL ? cfg->focus.focus_new_windows : server->focus.focus_new_windows;
    if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_GAIN) {
        focus_new = true;
    } else if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_REFUSE) {
        focus_new = false;
    }
    if (!fbwl_view_accepts_focus(view)) {
        focus_new = false;
    }

    if (!focus_new) {
        if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_REFUSE) {
            wlr_log(WLR_INFO, "FocusNew: refused title=%s app_id=%s",
                fbwl_view_display_title(view),
                fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)");
        }
        return;
    }

    const enum fbwl_focus_reason prev_reason = server->focus_reason;
    server->focus_reason = FBWL_FOCUS_REASON_MAP;
    if (server_focus_request_allowed(server, view, FBWL_FOCUS_REASON_MAP, "map")) {
        fbwm_core_focus_view(&server->wm, &view->wm_view);
    }
    server->focus_reason = prev_reason;
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, unmap);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_unmap(view, &server->wm, &hooks);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, commit);
    struct fbwl_server *server = view->server;
    fbwl_xdg_shell_handle_toplevel_commit(view, &server->decor_theme);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_maximize);
    struct fbwl_server *server = view->server;
    fbwl_xdg_shell_handle_toplevel_request_maximize(view, server->output_layout, &server->outputs);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_fullscreen);
    struct fbwl_server *server = view->server;
    fbwl_xdg_shell_handle_toplevel_request_fullscreen(view, server->output_layout, &server->outputs,
        server->layer_normal, server->layer_fullscreen);
}

static void xdg_toplevel_request_minimize(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_minimize);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_request_minimize(view, &hooks);
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, set_title);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_set_title(view, &server->decor_theme, &hooks);
}

static void xdg_toplevel_set_app_id(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, set_app_id);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_set_app_id(view, &hooks);
}

static void foreign_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_maximize);
    fbwl_view_set_maximized(view, event->maximized, view->server->output_layout, &view->server->outputs);
}

static void foreign_toplevel_request_minimize(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_minimize);
    view_set_minimized(view, event->minimized, "foreign-request");
}

static void foreign_toplevel_request_activate(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_activate);
    struct fbwl_server *server = view->server;
    if (server == NULL) {
        return;
    }

    if (view->minimized) {
        view_set_minimized(view, false, "foreign-activate");
    }

    if (!view->wm_view.sticky) {
        const size_t head = fbwl_server_screen_index_for_view(server, view);
        const int cur_ws = fbwm_core_workspace_current_for_head(&server->wm, head);
        if (view->wm_view.workspace != cur_ws) {
            server_workspace_switch_on_head(server, head, view->wm_view.workspace, "foreign-activate-switch");
        }
    }

    const enum fbwl_focus_reason prev_reason = server->focus_reason;
    server->focus_reason = FBWL_FOCUS_REASON_ACTIVATE;
    if (server_focus_request_allowed(server, view, FBWL_FOCUS_REASON_ACTIVATE, "activate")) {
        fbwm_core_focus_view(&server->wm, &view->wm_view);
    }
    server->focus_reason = prev_reason;
    (void)event;
}

static void foreign_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_fullscreen);
    fbwl_view_set_fullscreen(view, event->fullscreen, view->server->output_layout, &view->server->outputs,
        view->server->layer_normal, view->server->layer_fullscreen, event->output);
}

static void foreign_toplevel_request_close(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_close);
    if (view->type == FBWL_VIEW_XDG) {
        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
    } else if (view->type == FBWL_VIEW_XWAYLAND) {
        wlr_xwayland_surface_close(view->xwayland_surface);
    }
}

static void xwayland_surface_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, map);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    const bool rules_applied_before = view->apps_rules_applied;

    if (view != NULL && server != NULL && view->xwayland_surface != NULL &&
            xwayland_surface_should_be_in_slit(server, view->xwayland_surface)) {
        view->in_slit = true;
        view->wm_view.sticky = true;
        fbwl_view_decor_set_enabled(view, false);
        view->decor_forced = true;

        view->mapped = true;
        view->minimized = false;
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, false);
        }

        (void)server_slit_ui_attach_view(server, view, "xwayland-map");
        wlr_log(WLR_INFO, "Slit: manage dock view title=%s class=%s",
            fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
            fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-class)");
        return;
    }

    fbwl_xwayland_handle_surface_map(view, &server->wm, server->output_layout, &server->outputs,
        server->cursor->x, server->cursor->y, server->apps_rules, server->apps_rule_count, &hooks);
    if (!rules_applied_before && view->apps_rules_applied) {
        server_apps_rule_matchlimit_inc(server, view);
    }
    if (!fbwm_core_view_is_visible(&server->wm, &view->wm_view)) {
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    bool focus_new = cfg != NULL ? cfg->focus.focus_new_windows : server->focus.focus_new_windows;
    if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_GAIN) {
        focus_new = true;
    } else if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_REFUSE) {
        focus_new = false;
    }
    if (!fbwl_view_accepts_focus(view)) {
        focus_new = false;
    }

    if (!focus_new) {
        if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_REFUSE) {
            wlr_log(WLR_INFO, "FocusNew: refused title=%s app_id=%s",
                fbwl_view_display_title(view),
                fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)");
        }
        return;
    }

    const enum fbwl_focus_reason prev_reason = server->focus_reason;
    server->focus_reason = FBWL_FOCUS_REASON_MAP;
    if (server_focus_request_allowed(server, view, FBWL_FOCUS_REASON_MAP, "map")) {
        fbwm_core_focus_view(&server->wm, &view->wm_view);
    }
    server->focus_reason = prev_reason;
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, unmap);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    if (view != NULL && server != NULL && view->in_slit) {
        server_slit_ui_detach_view(server, view, "xwayland-unmap");
        view->in_slit = false;
    }
    fbwl_xwayland_handle_surface_unmap(view, &server->wm, &hooks);
}

static void xwayland_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, commit);
    struct fbwl_server *server = view->server;
    fbwl_xwayland_handle_surface_commit(view, server != NULL ? &server->decor_theme : NULL);
    if (view != NULL && server != NULL && view->in_slit) {
        server_slit_ui_handle_view_commit(server, view, "xwayland-commit");
    }
}

static void xwayland_surface_associate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_associate);
    struct fbwl_server *server = view->server;
    if (server == NULL) {
        return;
    }

    struct wlr_scene_tree *parent =
        server->layer_normal != NULL ? server->layer_normal : &server->scene->tree;
    fbwl_xwayland_handle_surface_associate(view, parent, &server->decor_theme, server->output_layout,
        xwayland_surface_map, xwayland_surface_unmap, xwayland_surface_commit);
}

static void xwayland_surface_dissociate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_dissociate);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    if (view != NULL && server != NULL && view->in_slit) {
        server_slit_ui_detach_view(server, view, "xwayland-dissociate");
        view->in_slit = false;
    }
    fbwl_xwayland_handle_surface_dissociate(view, &server->wm, &hooks);
}

static void xwayland_surface_request_configure(struct wl_listener *listener, void *data) {
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    struct fbwl_server *server = view->server;
    if (view != NULL && server != NULL && view->in_slit) {
        server_slit_ui_apply_view_geometry(server, view, "xwayland-request-configure");
        return;
    }
    fbwl_xwayland_handle_surface_request_configure(view, event,
        server != NULL ? &server->decor_theme : NULL,
        server != NULL ? server->output_layout : NULL);
}

static void xwayland_surface_request_activate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_activate);
    struct fbwl_server *server = view->server;
    if (server == NULL) {
        return;
    }
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_request_activate(view, &server->wm, &hooks);
}

static void xwayland_surface_request_close(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_close);
    fbwl_xwayland_handle_surface_request_close(view);
}

static void xwayland_surface_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_set_title);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_set_title(view, server != NULL ? &server->decor_theme : NULL, &hooks);
}

static void xwayland_surface_set_class(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_set_class);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_set_class(view, &hooks);
}

static bool xwayland_surface_is_urgent(const struct wlr_xwayland_surface *xsurface) {
    if (xsurface == NULL) {
        return false;
    }
    if (xsurface->demands_attention) {
        return true;
    }
    return xsurface->hints != NULL &&
        (xsurface->hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY) != 0;
}

static void xwayland_surface_sync_urgency(struct fbwl_view *view, const char *why) {
    if (view == NULL || view->server == NULL) {
        return;
    }
    struct fbwl_server *server = view->server;

    const bool urgent = xwayland_surface_is_urgent(view->xwayland_surface);
    const bool urgent_prev = view->xwayland_urgent;
    view->xwayland_urgent = urgent;
    if (urgent == urgent_prev) {
        return;
    }

    if (urgent) {
        const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
        const int interval_ms =
            cfg != NULL ? cfg->focus.demands_attention_timeout_ms : server->focus.demands_attention_timeout_ms;

        if (interval_ms > 0 && server->focused_view != view && !view->attention_active) {
            view->attention_from_xwayland_urgency = true;
            fbwl_view_attention_request(view, interval_ms, &server->decor_theme,
                why != NULL ? why : "xwayland-urgent");
            return;
        }

        server_toolbar_ui_rebuild(server);
        return;
    }

    if (view->attention_active && view->attention_from_xwayland_urgency) {
        fbwl_view_attention_clear(view, &server->decor_theme,
            why != NULL ? why : "xwayland-urgent-clear");
        return;
    }
    server_toolbar_ui_rebuild(server);
}

static void xwayland_surface_request_demands_attention(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_demands_attention);
    xwayland_surface_sync_urgency(view, "xwayland-demands-attention");
}

static void xwayland_surface_set_hints(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_set_hints);
    xwayland_surface_sync_urgency(view, "xwayland-set-hints");
}

static void xwayland_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, destroy);
    struct fbwl_server *server = view->server;
    server_apps_rule_matchlimit_dec(server, view);
    server_apps_rules_save_on_close(view);
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    if (view != NULL && server != NULL && view->in_slit) {
        server_slit_ui_detach_view(server, view, "xwayland-destroy");
        view->in_slit = false;
    }
    fbwl_xwayland_handle_surface_destroy(view, &server->wm, &hooks);
}

void server_xwayland_ready(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_server *server = wl_container_of(listener, server, xwayland_ready);
    const char *display_name = server->xwayland != NULL ? server->xwayland->display_name : NULL;
    fbwl_xwayland_handle_ready(display_name);
    fbwl_xembed_sni_proxy_maybe_start(server, display_name);
}

void server_xwayland_new_surface(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, xwayland_new_surface);
    struct wlr_xwayland_surface *xsurface = data;
    const struct fbwl_view_foreign_toplevel_handlers *foreign_handlers = NULL;
    if (server != NULL && server->foreign_toplevel_mgr != NULL) {
        static const struct fbwl_view_foreign_toplevel_handlers handlers = {
            .request_maximize = foreign_toplevel_request_maximize,
            .request_minimize = foreign_toplevel_request_minimize,
            .request_activate = foreign_toplevel_request_activate,
            .request_fullscreen = foreign_toplevel_request_fullscreen,
            .request_close = foreign_toplevel_request_close,
        };
        foreign_handlers = &handlers;
    }

    fbwl_xwayland_handle_new_surface(server, xsurface, &fbwl_wm_view_ops,
        xwayland_surface_destroy,
        xwayland_surface_associate,
        xwayland_surface_dissociate,
        xwayland_surface_request_configure,
        xwayland_surface_request_activate,
        xwayland_surface_request_close,
        xwayland_surface_request_demands_attention,
        xwayland_surface_set_title,
        xwayland_surface_set_class,
        xwayland_surface_set_hints,
        server != NULL ? server->foreign_toplevel_mgr : NULL,
        foreign_handlers);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, destroy);
    struct fbwl_server *server = view->server;
    server_apps_rule_matchlimit_dec(server, view);
    server_apps_rules_save_on_close(view);
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_destroy(view, &server->wm, &hooks);
}

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    const struct fbwl_view_foreign_toplevel_handlers *foreign_handlers = NULL;
    if (server->foreign_toplevel_mgr != NULL) {
        static const struct fbwl_view_foreign_toplevel_handlers handlers = {
            .request_maximize = foreign_toplevel_request_maximize,
            .request_minimize = foreign_toplevel_request_minimize,
            .request_activate = foreign_toplevel_request_activate,
            .request_fullscreen = foreign_toplevel_request_fullscreen,
            .request_close = foreign_toplevel_request_close,
        };
        foreign_handlers = &handlers;
    }

    fbwl_xdg_shell_handle_new_toplevel(server, xdg_toplevel,
        server->layer_normal != NULL ? server->layer_normal : &server->scene->tree,
        &server->decor_theme, &fbwl_wm_view_ops,
        xdg_toplevel_map, xdg_toplevel_unmap, xdg_toplevel_commit, xdg_toplevel_destroy,
        xdg_toplevel_request_maximize, xdg_toplevel_request_fullscreen, xdg_toplevel_request_minimize,
        xdg_toplevel_set_title, xdg_toplevel_set_app_id,
        server->foreign_toplevel_mgr, foreign_handlers);
}
