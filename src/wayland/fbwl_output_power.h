#pragma once

#include <wayland-server-core.h>

struct wlr_output_layout;
struct wlr_output_manager_v1;
struct wlr_output_power_v1_set_mode_event;

void fbwl_output_power_handle_set_mode(struct wlr_output_power_v1_set_mode_event *event,
        struct wlr_output_manager_v1 *output_manager,
        struct wl_list *outputs,
        struct wlr_output_layout *output_layout);

