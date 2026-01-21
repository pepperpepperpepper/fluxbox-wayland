#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_input_method_manager_v2;
struct wlr_input_method_v2;
struct wlr_seat;
struct wlr_surface;
struct wlr_text_input_manager_v3;
struct wlr_text_input_v3;

struct fbwl_text_input_state {
    struct wlr_text_input_manager_v3 *text_input_mgr;
    struct wl_listener new_text_input;
    struct wlr_text_input_v3 *active_text_input;

    struct wlr_input_method_manager_v2 *input_method_mgr;
    struct wl_listener new_input_method;
    struct wlr_input_method_v2 *input_method;

    struct wlr_seat *seat;
};

bool fbwl_text_input_init(struct fbwl_text_input_state *state, struct wl_display *display, struct wlr_seat *seat);
void fbwl_text_input_finish(struct fbwl_text_input_state *state);

void fbwl_text_input_update_focus(struct fbwl_text_input_state *state, struct wlr_surface *surface);
