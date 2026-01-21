#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_xdg_decoration_manager_v1;
struct fbwl_decor_theme;

struct fbwl_xdg_decoration_state {
    struct wlr_xdg_decoration_manager_v1 *manager;
    struct wl_listener new_toplevel_decoration;
    const struct fbwl_decor_theme *decor_theme;
};

bool fbwl_xdg_decoration_init(struct fbwl_xdg_decoration_state *state, struct wl_display *display,
        const struct fbwl_decor_theme *decor_theme);
void fbwl_xdg_decoration_finish(struct fbwl_xdg_decoration_state *state);
