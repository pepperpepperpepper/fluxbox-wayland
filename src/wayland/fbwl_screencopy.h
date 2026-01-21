#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_screencopy_manager_v1;

struct fbwl_screencopy_state {
    struct wlr_screencopy_manager_v1 *manager;
};

bool fbwl_screencopy_init(struct fbwl_screencopy_state *state, struct wl_display *display);
void fbwl_screencopy_finish(struct fbwl_screencopy_state *state);

