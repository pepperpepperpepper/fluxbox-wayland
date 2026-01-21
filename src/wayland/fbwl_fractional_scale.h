#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_fractional_scale_manager_v1;

struct fbwl_fractional_scale_state {
    struct wlr_fractional_scale_manager_v1 *manager;
};

bool fbwl_fractional_scale_init(struct fbwl_fractional_scale_state *state, struct wl_display *display);
void fbwl_fractional_scale_finish(struct fbwl_fractional_scale_state *state);

