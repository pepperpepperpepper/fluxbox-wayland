#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_output_layout;
struct wlr_xdg_output_manager_v1;

struct fbwl_xdg_output_state {
    struct wlr_xdg_output_manager_v1 *manager;
};

bool fbwl_xdg_output_init(struct fbwl_xdg_output_state *state, struct wl_display *display,
        struct wlr_output_layout *output_layout);
void fbwl_xdg_output_finish(struct fbwl_xdg_output_state *state);

