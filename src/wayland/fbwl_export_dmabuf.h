#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_export_dmabuf_manager_v1;

struct fbwl_export_dmabuf_state {
    struct wlr_export_dmabuf_manager_v1 *manager;
};

bool fbwl_export_dmabuf_init(struct fbwl_export_dmabuf_state *state, struct wl_display *display);
void fbwl_export_dmabuf_finish(struct fbwl_export_dmabuf_state *state);

