#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_ext_image_copy_capture_manager_v1;
struct wlr_ext_output_image_capture_source_manager_v1;

struct fbwl_ext_image_capture_state {
    struct wlr_ext_image_copy_capture_manager_v1 *copy_capture_manager;
    struct wlr_ext_output_image_capture_source_manager_v1 *output_capture_source_manager;
};

bool fbwl_ext_image_capture_init(struct fbwl_ext_image_capture_state *state, struct wl_display *display);
void fbwl_ext_image_capture_finish(struct fbwl_ext_image_capture_state *state);

