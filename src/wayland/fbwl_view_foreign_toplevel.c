#include "wayland/fbwl_view_foreign_toplevel.h"

#include <stddef.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>

#include "wayland/fbwl_view.h"

void fbwl_view_foreign_toplevel_create(struct fbwl_view *view, struct wlr_foreign_toplevel_manager_v1 *manager,
        const char *title, const char *app_id, const struct fbwl_view_foreign_toplevel_handlers *handlers) {
    if (view == NULL || manager == NULL) {
        return;
    }
    if (view->foreign_toplevel != NULL) {
        return;
    }

    view->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(manager);
    if (view->foreign_toplevel == NULL) {
        return;
    }

    view->foreign_toplevel->data = view;
    wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, title != NULL ? title : "");
    wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, app_id != NULL ? app_id : "");
    wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, view->maximized);
    wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, view->minimized);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, view->fullscreen);

    if (handlers == NULL) {
        return;
    }

    if (handlers->request_maximize != NULL) {
        view->foreign_request_maximize.notify = handlers->request_maximize;
        wl_signal_add(&view->foreign_toplevel->events.request_maximize, &view->foreign_request_maximize);
    }
    if (handlers->request_minimize != NULL) {
        view->foreign_request_minimize.notify = handlers->request_minimize;
        wl_signal_add(&view->foreign_toplevel->events.request_minimize, &view->foreign_request_minimize);
    }
    if (handlers->request_activate != NULL) {
        view->foreign_request_activate.notify = handlers->request_activate;
        wl_signal_add(&view->foreign_toplevel->events.request_activate, &view->foreign_request_activate);
    }
    if (handlers->request_fullscreen != NULL) {
        view->foreign_request_fullscreen.notify = handlers->request_fullscreen;
        wl_signal_add(&view->foreign_toplevel->events.request_fullscreen, &view->foreign_request_fullscreen);
    }
    if (handlers->request_close != NULL) {
        view->foreign_request_close.notify = handlers->request_close;
        wl_signal_add(&view->foreign_toplevel->events.request_close, &view->foreign_request_close);
    }
}

void fbwl_view_foreign_toplevel_destroy(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }

    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
        view->foreign_toplevel = NULL;
    }
    view->foreign_output = NULL;
}

void fbwl_view_foreign_toplevel_set_title(struct fbwl_view *view, const char *title) {
    if (view == NULL || view->foreign_toplevel == NULL) {
        return;
    }
    wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, title != NULL ? title : "");
}

void fbwl_view_foreign_toplevel_set_app_id(struct fbwl_view *view, const char *app_id) {
    if (view == NULL || view->foreign_toplevel == NULL) {
        return;
    }
    wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, app_id != NULL ? app_id : "");
}
