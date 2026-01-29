#include <stdbool.h>
#include <stddef.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wmcore/fbwm_output.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_foreign_toplevel.h"

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

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, map);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_map(view, &server->wm, server->output_layout, &server->outputs,
        server->cursor->x, server->cursor->y, server->apps_rules, server->apps_rule_count, &hooks);
    if (!fbwm_core_view_is_visible(&server->wm, &view->wm_view)) {
        return;
    }

    bool focus_new = server->focus.focus_new_windows;
    if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_GAIN) {
        focus_new = true;
    } else if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_REFUSE) {
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
    fbwm_core_focus_view(&server->wm, &view->wm_view);
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

    if (!view->wm_view.sticky &&
            view->wm_view.workspace != fbwm_core_workspace_current(&server->wm)) {
        fbwm_core_workspace_switch(&server->wm, view->wm_view.workspace);
        apply_workspace_visibility(server, "foreign-activate-switch");
    }

    const enum fbwl_focus_reason prev_reason = server->focus_reason;
    server->focus_reason = FBWL_FOCUS_REASON_ACTIVATE;
    fbwm_core_focus_view(&server->wm, &view->wm_view);
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
    fbwl_xwayland_handle_surface_map(view, &server->wm, server->output_layout, &server->outputs,
        server->cursor->x, server->cursor->y, server->apps_rules, server->apps_rule_count, &hooks);
    if (!fbwm_core_view_is_visible(&server->wm, &view->wm_view)) {
        return;
    }

    bool focus_new = server->focus.focus_new_windows;
    if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_GAIN) {
        focus_new = true;
    } else if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_REFUSE) {
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
    fbwm_core_focus_view(&server->wm, &view->wm_view);
    server->focus_reason = prev_reason;
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, unmap);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_unmap(view, &server->wm, &hooks);
}

static void xwayland_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, commit);
    struct fbwl_server *server = view->server;
    fbwl_xwayland_handle_surface_commit(view, server != NULL ? &server->decor_theme : NULL);
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
    fbwl_xwayland_handle_surface_dissociate(view, &server->wm, &hooks);
}

static void xwayland_surface_request_configure(struct wl_listener *listener, void *data) {
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    struct fbwl_server *server = view->server;
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

static void xwayland_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, destroy);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_destroy(view, &server->wm, &hooks);
}

void server_xwayland_ready(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_server *server = wl_container_of(listener, server, xwayland_ready);
    const char *display_name = server->xwayland != NULL ? server->xwayland->display_name : NULL;
    fbwl_xwayland_handle_ready(display_name);
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
        xwayland_surface_set_title,
        xwayland_surface_set_class,
        server != NULL ? server->foreign_toplevel_mgr : NULL,
        foreign_handlers);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, destroy);
    struct fbwl_server *server = view->server;
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
