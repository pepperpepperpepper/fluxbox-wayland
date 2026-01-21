#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_viewporter;

struct fbwl_viewporter_state {
    struct wlr_viewporter *viewporter;
};

bool fbwl_viewporter_init(struct fbwl_viewporter_state *state, struct wl_display *display);
void fbwl_viewporter_finish(struct fbwl_viewporter_state *state);

