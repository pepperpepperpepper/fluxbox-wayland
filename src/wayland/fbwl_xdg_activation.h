#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct fbwl_view;
struct fbwm_core;
struct wlr_xdg_activation_v1;

struct fbwl_xdg_activation_state {
    struct wlr_xdg_activation_v1 *xdg_activation;
    struct wl_listener request_activate;

    struct fbwm_core *wm;
    void (*view_set_minimized)(struct fbwl_view *view, bool minimized, const char *why);
};

bool fbwl_xdg_activation_init(struct fbwl_xdg_activation_state *state, struct wl_display *display,
        struct fbwm_core *wm, void (*view_set_minimized)(struct fbwl_view *view, bool minimized, const char *why));
void fbwl_xdg_activation_finish(struct fbwl_xdg_activation_state *state);

