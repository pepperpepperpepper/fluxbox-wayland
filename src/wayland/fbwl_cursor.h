#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct wlr_cursor;
struct wlr_cursor_shape_manager_v1_request_set_shape_event;
struct wlr_pointer_constraints_v1;
struct wlr_pointer_constraint_v1;
struct wlr_relative_pointer_manager_v1;
struct wlr_pointer_motion_event;
struct wlr_pointer_motion_absolute_event;
struct wlr_scene;
struct wlr_seat;
struct wlr_xcursor_manager;

enum fbwl_cursor_mode {
    FBWL_CURSOR_PASSTHROUGH,
    FBWL_CURSOR_MOVE,
    FBWL_CURSOR_RESIZE,
};

struct fbwl_cursor_menu_hooks {
    void *userdata;
    bool (*is_open)(void *userdata);
    ssize_t (*index_at)(void *userdata, int lx, int ly);
    void (*set_selected)(void *userdata, size_t idx);
};

void fbwl_cursor_handle_motion(struct wlr_cursor *cursor, struct wlr_xcursor_manager *cursor_mgr,
        struct wlr_scene *scene, struct wlr_seat *seat,
        struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr,
        struct wlr_pointer_constraints_v1 *pointer_constraints,
        struct wlr_pointer_constraint_v1 **active_pointer_constraint,
        bool *pointer_phys_valid, double *pointer_phys_x, double *pointer_phys_y,
        enum fbwl_cursor_mode grab_mode,
        void (*grab_update)(void *userdata), void *grab_userdata,
        const struct fbwl_cursor_menu_hooks *menu_hooks,
        const struct wlr_pointer_motion_event *event);

void fbwl_cursor_handle_motion_absolute(struct wlr_cursor *cursor, struct wlr_xcursor_manager *cursor_mgr,
        struct wlr_scene *scene, struct wlr_seat *seat,
        struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr,
        struct wlr_pointer_constraints_v1 *pointer_constraints,
        struct wlr_pointer_constraint_v1 **active_pointer_constraint,
        bool *pointer_phys_valid, double *pointer_phys_x, double *pointer_phys_y,
        enum fbwl_cursor_mode grab_mode,
        void (*grab_update)(void *userdata), void *grab_userdata,
        const struct fbwl_cursor_menu_hooks *menu_hooks,
        const struct wlr_pointer_motion_absolute_event *event);

void fbwl_cursor_handle_shape_request(struct wlr_seat *seat, struct wlr_cursor *cursor,
        struct wlr_xcursor_manager *cursor_mgr,
        const struct wlr_cursor_shape_manager_v1_request_set_shape_event *event);

void fbwl_cursor_handle_new_pointer_constraint(struct wlr_cursor *cursor, struct wlr_xcursor_manager *cursor_mgr,
        struct wlr_scene *scene, struct wlr_seat *seat,
        struct wlr_pointer_constraints_v1 *pointer_constraints,
        struct wlr_pointer_constraint_v1 **active_pointer_constraint,
        const struct fbwl_cursor_menu_hooks *menu_hooks,
        struct wlr_pointer_constraint_v1 *constraint);
