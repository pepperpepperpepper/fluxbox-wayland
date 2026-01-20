#pragma once

#include <wayland-server-core.h>

struct fbwl_view;
struct wlr_foreign_toplevel_manager_v1;

struct fbwl_view_foreign_toplevel_handlers {
    wl_notify_func_t request_maximize;
    wl_notify_func_t request_minimize;
    wl_notify_func_t request_activate;
    wl_notify_func_t request_fullscreen;
    wl_notify_func_t request_close;
};

void fbwl_view_foreign_toplevel_create(struct fbwl_view *view, struct wlr_foreign_toplevel_manager_v1 *manager,
        const char *title, const char *app_id, const struct fbwl_view_foreign_toplevel_handlers *handlers);

void fbwl_view_foreign_toplevel_destroy(struct fbwl_view *view);

void fbwl_view_foreign_toplevel_set_title(struct fbwl_view *view, const char *title);
void fbwl_view_foreign_toplevel_set_app_id(struct fbwl_view *view, const char *app_id);
