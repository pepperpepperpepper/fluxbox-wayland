#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

#include "wayland/fbwl_cursor.h"

struct wlr_cursor;
struct wlr_pointer_constraints_v1;
struct wlr_pointer_constraint_v1;
struct wlr_relative_pointer_manager_v1;
struct wlr_scene;
struct wlr_seat;
struct wlr_xcursor_manager;

struct fbwl_pointer_constraints_state {
    struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
    struct wlr_pointer_constraints_v1 *constraints;
    struct wl_listener new_constraint;

    struct wlr_pointer_constraint_v1 *active;
    bool phys_valid;
    double phys_x;
    double phys_y;

    struct wlr_cursor **cursor;
    struct wlr_xcursor_manager **cursor_mgr;
    struct wlr_scene **scene;
    struct wlr_seat **seat;

    struct fbwl_cursor_menu_hooks menu_hooks;
};

bool fbwl_pointer_constraints_init(struct fbwl_pointer_constraints_state *state,
        struct wl_display *display,
        struct wlr_cursor **cursor,
        struct wlr_xcursor_manager **cursor_mgr,
        struct wlr_scene **scene,
        struct wlr_seat **seat,
        const struct fbwl_cursor_menu_hooks *menu_hooks);

void fbwl_pointer_constraints_finish(struct fbwl_pointer_constraints_state *state);

