#include "wayland/fbwl_pointer_constraints.h"

#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_util.h"

static void handle_new_pointer_constraint(struct wl_listener *listener, void *data) {
    struct fbwl_pointer_constraints_state *state = wl_container_of(listener, state, new_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;

    fbwl_cursor_handle_new_pointer_constraint(
        state->cursor != NULL ? *state->cursor : NULL,
        state->cursor_mgr != NULL ? *state->cursor_mgr : NULL,
        state->scene != NULL ? *state->scene : NULL,
        state->seat != NULL ? *state->seat : NULL,
        state->constraints,
        &state->active,
        &state->menu_hooks,
        constraint);
}

bool fbwl_pointer_constraints_init(struct fbwl_pointer_constraints_state *state,
        struct wl_display *display,
        struct wlr_cursor **cursor,
        struct wlr_xcursor_manager **cursor_mgr,
        struct wlr_scene **scene,
        struct wlr_seat **seat,
        const struct fbwl_cursor_menu_hooks *menu_hooks) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->cursor = cursor;
    state->cursor_mgr = cursor_mgr;
    state->scene = scene;
    state->seat = seat;

    if (menu_hooks != NULL) {
        state->menu_hooks = *menu_hooks;
    } else {
        state->menu_hooks = (struct fbwl_cursor_menu_hooks){0};
    }

    state->relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(display);
    state->constraints = wlr_pointer_constraints_v1_create(display);
    if (state->constraints == NULL) {
        wlr_log(WLR_ERROR, "failed to create pointer constraints manager");
        return false;
    }

    state->new_constraint.notify = handle_new_pointer_constraint;
    wl_signal_add(&state->constraints->events.new_constraint, &state->new_constraint);

    state->active = NULL;
    state->phys_valid = false;
    state->phys_x = 0.0;
    state->phys_y = 0.0;
    return true;
}

void fbwl_pointer_constraints_finish(struct fbwl_pointer_constraints_state *state) {
    if (state == NULL) {
        return;
    }

    fbwl_cleanup_listener(&state->new_constraint);
    state->relative_pointer_mgr = NULL;
    state->constraints = NULL;
    state->cursor = NULL;
    state->cursor_mgr = NULL;
    state->scene = NULL;
    state->seat = NULL;
    state->active = NULL;
    state->phys_valid = false;
}

