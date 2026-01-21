#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_keyboard_shortcuts_inhibit_manager_v1;
struct wlr_keyboard_shortcuts_inhibitor_v1;
struct wlr_seat;

struct fbwl_shortcuts_inhibit_state {
    struct wlr_keyboard_shortcuts_inhibit_manager_v1 *shortcuts_inhibit_mgr;
    struct wl_listener new_shortcuts_inhibitor;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *active_shortcuts_inhibitor;
    struct wl_list shortcuts_inhibitors;

    struct wlr_seat **seat;
};

bool fbwl_shortcuts_inhibit_init(struct fbwl_shortcuts_inhibit_state *state, struct wl_display *display,
        struct wlr_seat **seat);
void fbwl_shortcuts_inhibit_finish(struct fbwl_shortcuts_inhibit_state *state);

void fbwl_shortcuts_inhibit_update(struct fbwl_shortcuts_inhibit_state *state);
bool fbwl_shortcuts_inhibit_is_inhibited(struct fbwl_shortcuts_inhibit_state *state);

