#include "wayland/fbwl_cursor.h"

#include <stdint.h>
#include <stdlib.h>

#include <pixman.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"

struct fbwl_pointer_constraint {
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_scene *scene;
    struct wlr_seat *seat;

    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wlr_pointer_constraint_v1 **active_pointer_constraint;

    struct wlr_pointer_constraint_v1 *constraint;
    struct fbwl_cursor_menu_hooks menu_hooks;

    struct wl_listener set_region;
    struct wl_listener destroy;
};

static bool menu_is_open(const struct fbwl_cursor_menu_hooks *hooks) {
    return hooks != NULL && hooks->is_open != NULL && hooks->is_open(hooks->userdata);
}

static void cursor_update_pointer_constraint(struct wlr_pointer_constraints_v1 *pointer_constraints,
        struct wlr_seat *seat, struct wlr_pointer_constraint_v1 **active_pointer_constraint) {
    if (pointer_constraints == NULL || seat == NULL || active_pointer_constraint == NULL) {
        return;
    }

    struct wlr_surface *focused_surface = seat->pointer_state.focused_surface;
    struct wlr_pointer_constraint_v1 *constraint = NULL;
    if (focused_surface != NULL) {
        constraint = wlr_pointer_constraints_v1_constraint_for_surface(
            pointer_constraints, focused_surface, seat);
    }

    if (constraint == *active_pointer_constraint) {
        return;
    }

    if (*active_pointer_constraint != NULL) {
        struct wlr_pointer_constraint_v1 *old = *active_pointer_constraint;
        *active_pointer_constraint = NULL;
        wlr_pointer_constraint_v1_send_deactivated(old);
    }

    if (constraint != NULL) {
        *active_pointer_constraint = constraint;
        wlr_pointer_constraint_v1_send_activated(constraint);
    }
}

static void process_cursor_motion(struct wlr_scene *scene, struct wlr_cursor *cursor,
        struct wlr_xcursor_manager *cursor_mgr, struct wlr_seat *seat,
        struct wlr_pointer_constraints_v1 *pointer_constraints,
        struct wlr_pointer_constraint_v1 **active_pointer_constraint,
        const struct fbwl_cursor_menu_hooks *menu_hooks,
        uint32_t time_msec) {
    if (scene == NULL || cursor == NULL || cursor_mgr == NULL || seat == NULL) {
        return;
    }

    if (menu_is_open(menu_hooks)) {
        if (menu_hooks != NULL && menu_hooks->index_at != NULL && menu_hooks->set_selected != NULL) {
            const ssize_t idx = menu_hooks->index_at(menu_hooks->userdata, (int)cursor->x, (int)cursor->y);
            if (idx >= 0) {
                menu_hooks->set_selected(menu_hooks->userdata, (size_t)idx);
            }
        }

        wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
        struct wlr_surface *prev_surface = seat->pointer_state.focused_surface;
        wlr_seat_pointer_clear_focus(seat);
        if (prev_surface != seat->pointer_state.focused_surface) {
            cursor_update_pointer_constraint(pointer_constraints, seat, active_pointer_constraint);
        }
        return;
    }

    double sx = 0, sy = 0;
    struct wlr_surface *surface = NULL;
    (void)fbwl_view_at(scene, cursor->x, cursor->y, &surface, &sx, &sy);

    if (surface != NULL) {
        struct wlr_surface *prev_surface = seat->pointer_state.focused_surface;
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time_msec, sx, sy);
        if (prev_surface != seat->pointer_state.focused_surface) {
            cursor_update_pointer_constraint(pointer_constraints, seat, active_pointer_constraint);
        }
        return;
    }

    wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
    struct wlr_surface *prev_surface = seat->pointer_state.focused_surface;
    wlr_seat_pointer_clear_focus(seat);
    if (prev_surface != seat->pointer_state.focused_surface) {
        cursor_update_pointer_constraint(pointer_constraints, seat, active_pointer_constraint);
    }
}

static void clamp_to_box(double *x, double *y, const struct wlr_box *box) {
    if (x == NULL || y == NULL || box == NULL) {
        return;
    }

    double min_x = box->x;
    double min_y = box->y;
    double max_x = box->x;
    double max_y = box->y;
    if (box->width > 0) {
        max_x = box->x + box->width - 1;
    }
    if (box->height > 0) {
        max_y = box->y + box->height - 1;
    }

    if (*x < min_x) {
        *x = min_x;
    }
    if (*x > max_x) {
        *x = max_x;
    }
    if (*y < min_y) {
        *y = min_y;
    }
    if (*y > max_y) {
        *y = max_y;
    }
}

static bool get_confine_box(struct wlr_scene *scene, struct wlr_cursor *cursor,
        struct wlr_pointer_constraint_v1 *constraint,
        struct wlr_box *box) {
    if (scene == NULL || cursor == NULL || constraint == NULL || box == NULL) {
        return false;
    }
    if (constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED || constraint->surface == NULL) {
        return false;
    }

    struct wlr_surface *surface = NULL;
    double sx = 0, sy = 0;
    (void)fbwl_view_at(scene, cursor->x, cursor->y, &surface, &sx, &sy);
    if (surface != constraint->surface) {
        return false;
    }

    const double origin_x = cursor->x - sx;
    const double origin_y = cursor->y - sy;

    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = constraint->surface->current.width;
    int32_t y2 = constraint->surface->current.height;

    if (pixman_region32_not_empty(&constraint->region)) {
        const pixman_box32_t *ext = pixman_region32_extents(&constraint->region);
        if (ext != NULL) {
            x1 = ext->x1;
            y1 = ext->y1;
            x2 = ext->x2;
            y2 = ext->y2;
        }
    }

    if (x2 <= x1) {
        x2 = x1 + 1;
    }
    if (y2 <= y1) {
        y2 = y1 + 1;
    }

    box->x = (int)(origin_x + x1);
    box->y = (int)(origin_y + y1);
    box->width = x2 - x1;
    box->height = y2 - y1;
    return true;
}

static void pointer_constraint_set_region(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_pointer_constraint *pc = wl_container_of(listener, pc, set_region);
    if (pc == NULL || pc->constraint == NULL || pc->active_pointer_constraint == NULL) {
        return;
    }
    if (pc->constraint != *pc->active_pointer_constraint) {
        return;
    }
    if (pc->constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED) {
        return;
    }

    struct wlr_box box = {0};
    if (!get_confine_box(pc->scene, pc->cursor, pc->constraint, &box)) {
        return;
    }
    double x = pc->cursor->x;
    double y = pc->cursor->y;
    clamp_to_box(&x, &y, &box);
    (void)wlr_cursor_warp(pc->cursor, NULL, x, y);
    process_cursor_motion(pc->scene, pc->cursor, pc->cursor_mgr, pc->seat,
        pc->pointer_constraints, pc->active_pointer_constraint, &pc->menu_hooks, 0);
}

static void pointer_constraint_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_pointer_constraint *pc = wl_container_of(listener, pc, destroy);
    if (pc == NULL) {
        return;
    }
    if (pc->active_pointer_constraint != NULL && *pc->active_pointer_constraint == pc->constraint) {
        *pc->active_pointer_constraint = NULL;
    }
    fbwl_cleanup_listener(&pc->set_region);
    fbwl_cleanup_listener(&pc->destroy);
    free(pc);
}

void fbwl_cursor_handle_motion(struct wlr_cursor *cursor, struct wlr_xcursor_manager *cursor_mgr,
        struct wlr_scene *scene, struct wlr_seat *seat,
        struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr,
        struct wlr_pointer_constraints_v1 *pointer_constraints,
        struct wlr_pointer_constraint_v1 **active_pointer_constraint,
        bool *pointer_phys_valid, double *pointer_phys_x, double *pointer_phys_y,
        enum fbwl_cursor_mode grab_mode,
        void (*grab_update)(void *userdata), void *grab_userdata,
        const struct fbwl_cursor_menu_hooks *menu_hooks,
        const struct wlr_pointer_motion_event *event) {
    if (cursor == NULL || seat == NULL || cursor_mgr == NULL || scene == NULL || event == NULL ||
            active_pointer_constraint == NULL ||
            pointer_phys_valid == NULL || pointer_phys_x == NULL || pointer_phys_y == NULL) {
        return;
    }

    const double dx = event->delta_x;
    const double dy = event->delta_y;
    const double unaccel_dx = event->unaccel_dx;
    const double unaccel_dy = event->unaccel_dy;

    if (!*pointer_phys_valid) {
        *pointer_phys_x = cursor->x;
        *pointer_phys_y = cursor->y;
        *pointer_phys_valid = true;
    }
    *pointer_phys_x += dx;
    *pointer_phys_y += dy;

    if (relative_pointer_mgr != NULL) {
        wlr_relative_pointer_manager_v1_send_relative_motion(relative_pointer_mgr,
            seat, (uint64_t)event->time_msec * 1000, dx, dy, unaccel_dx, unaccel_dy);
    }

    if (grab_mode != FBWL_CURSOR_PASSTHROUGH) {
        wlr_cursor_move(cursor, &event->pointer->base, dx, dy);
        if (grab_update != NULL) {
            grab_update(grab_userdata);
        }
        return;
    }

    if (*active_pointer_constraint != NULL &&
            (*active_pointer_constraint)->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        process_cursor_motion(scene, cursor, cursor_mgr, seat, pointer_constraints,
            active_pointer_constraint, menu_hooks, event->time_msec);
        return;
    }

    if (*active_pointer_constraint != NULL &&
            (*active_pointer_constraint)->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
        double x = cursor->x + dx;
        double y = cursor->y + dy;
        struct wlr_box box = {0};
        if (get_confine_box(scene, cursor, *active_pointer_constraint, &box)) {
            clamp_to_box(&x, &y, &box);
            if (!wlr_cursor_warp(cursor, &event->pointer->base, x, y)) {
                wlr_cursor_move(cursor, &event->pointer->base, dx, dy);
            }
        } else {
            wlr_cursor_move(cursor, &event->pointer->base, dx, dy);
        }
    } else {
        wlr_cursor_move(cursor, &event->pointer->base, dx, dy);
    }

    process_cursor_motion(scene, cursor, cursor_mgr, seat, pointer_constraints,
        active_pointer_constraint, menu_hooks, event->time_msec);
}

void fbwl_cursor_handle_motion_absolute(struct wlr_cursor *cursor, struct wlr_xcursor_manager *cursor_mgr,
        struct wlr_scene *scene, struct wlr_seat *seat,
        struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr,
        struct wlr_pointer_constraints_v1 *pointer_constraints,
        struct wlr_pointer_constraint_v1 **active_pointer_constraint,
        bool *pointer_phys_valid, double *pointer_phys_x, double *pointer_phys_y,
        enum fbwl_cursor_mode grab_mode,
        void (*grab_update)(void *userdata), void *grab_userdata,
        const struct fbwl_cursor_menu_hooks *menu_hooks,
        const struct wlr_pointer_motion_absolute_event *event) {
    if (cursor == NULL || seat == NULL || cursor_mgr == NULL || scene == NULL || event == NULL ||
            active_pointer_constraint == NULL ||
            pointer_phys_valid == NULL || pointer_phys_x == NULL || pointer_phys_y == NULL) {
        return;
    }

    double lx = 0, ly = 0;
    wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base,
        event->x, event->y, &lx, &ly);

    double dx = 0, dy = 0;
    if (*pointer_phys_valid) {
        dx = lx - *pointer_phys_x;
        dy = ly - *pointer_phys_y;
    }
    *pointer_phys_x = lx;
    *pointer_phys_y = ly;
    *pointer_phys_valid = true;

    if (relative_pointer_mgr != NULL) {
        wlr_relative_pointer_manager_v1_send_relative_motion(relative_pointer_mgr,
            seat, (uint64_t)event->time_msec * 1000, dx, dy, dx, dy);
    }

    if (grab_mode != FBWL_CURSOR_PASSTHROUGH) {
        wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
        if (grab_update != NULL) {
            grab_update(grab_userdata);
        }
        return;
    }

    if (*active_pointer_constraint != NULL &&
            (*active_pointer_constraint)->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        process_cursor_motion(scene, cursor, cursor_mgr, seat, pointer_constraints,
            active_pointer_constraint, menu_hooks, event->time_msec);
        return;
    }

    if (*active_pointer_constraint != NULL &&
            (*active_pointer_constraint)->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
        struct wlr_box box = {0};
        if (get_confine_box(scene, cursor, *active_pointer_constraint, &box)) {
            double x = lx;
            double y = ly;
            clamp_to_box(&x, &y, &box);
            wlr_cursor_warp_closest(cursor, &event->pointer->base, x, y);
        } else {
            wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
        }
    } else {
        wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
    }

    process_cursor_motion(scene, cursor, cursor_mgr, seat, pointer_constraints,
        active_pointer_constraint, menu_hooks, event->time_msec);
}

void fbwl_cursor_handle_shape_request(struct wlr_seat *seat, struct wlr_cursor *cursor,
        struct wlr_xcursor_manager *cursor_mgr,
        const struct wlr_cursor_shape_manager_v1_request_set_shape_event *event) {
    if (seat == NULL || cursor == NULL || cursor_mgr == NULL || event == NULL) {
        return;
    }

    struct wlr_seat_client *focused_client =
        seat->pointer_state.focused_client;
    if (focused_client != event->seat_client) {
        return;
    }
    if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER) {
        return;
    }

    const char *name = wlr_cursor_shape_v1_name(event->shape);
    if (name == NULL) {
        name = "default";
    }

    wlr_log(WLR_INFO, "CursorShape: name=%s", name);
    wlr_cursor_set_xcursor(cursor, cursor_mgr, name);
}

void fbwl_cursor_handle_new_pointer_constraint(struct wlr_cursor *cursor, struct wlr_xcursor_manager *cursor_mgr,
        struct wlr_scene *scene, struct wlr_seat *seat,
        struct wlr_pointer_constraints_v1 *pointer_constraints,
        struct wlr_pointer_constraint_v1 **active_pointer_constraint,
        const struct fbwl_cursor_menu_hooks *menu_hooks,
        struct wlr_pointer_constraint_v1 *constraint) {
    if (cursor == NULL || cursor_mgr == NULL || scene == NULL || seat == NULL || constraint == NULL ||
            active_pointer_constraint == NULL) {
        return;
    }

    struct fbwl_pointer_constraint *pc = calloc(1, sizeof(*pc));
    if (pc == NULL) {
        return;
    }
    pc->cursor = cursor;
    pc->cursor_mgr = cursor_mgr;
    pc->scene = scene;
    pc->seat = seat;
    pc->pointer_constraints = pointer_constraints;
    pc->active_pointer_constraint = active_pointer_constraint;
    pc->constraint = constraint;
    if (menu_hooks != NULL) {
        pc->menu_hooks = *menu_hooks;
    }

    pc->set_region.notify = pointer_constraint_set_region;
    wl_signal_add(&constraint->events.set_region, &pc->set_region);
    pc->destroy.notify = pointer_constraint_destroy;
    wl_signal_add(&constraint->events.destroy, &pc->destroy);

    cursor_update_pointer_constraint(pointer_constraints, seat, active_pointer_constraint);
}

