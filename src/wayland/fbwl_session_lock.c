#include "wayland/fbwl_session_lock.h"

#include "wayland/fbwl_output.h"
#include "wayland/fbwl_util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/log.h>

struct fbwl_session_lock_surface {
    struct wl_list link;
    struct fbwl_session_lock_state *state;
    struct wlr_session_lock_surface_v1 *lock_surface;
    struct wlr_scene_tree *scene_tree;
    bool has_buffer;
    struct wl_listener surface_commit;
    struct wl_listener destroy;
};

static void fbwl_session_lock_maybe_send_locked(struct fbwl_session_lock_state *state) {
    if (state == NULL || state->session_lock == NULL || state->session_lock_sent_locked) {
        return;
    }

    if (state->session_lock_expected_surfaces < 1) {
        state->session_lock_expected_surfaces = 1;
    }

    size_t surface_count = 0;
    size_t committed_count = 0;
    struct fbwl_session_lock_surface *ls;
    wl_list_for_each(ls, &state->session_lock_surfaces, link) {
        surface_count++;
        if (ls->has_buffer) {
            committed_count++;
        }
    }

    if (surface_count < state->session_lock_expected_surfaces ||
            committed_count < state->session_lock_expected_surfaces) {
        return;
    }

    wlr_session_lock_v1_send_locked(state->session_lock);
    state->session_lock_sent_locked = true;
    wlr_log(WLR_INFO, "SessionLock: locked");
}

static void session_lock_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_session_lock_surface *ls = wl_container_of(listener, ls, surface_commit);
    struct fbwl_session_lock_state *state = ls != NULL ? ls->state : NULL;
    if (state == NULL || ls->lock_surface == NULL || ls->lock_surface->surface == NULL) {
        return;
    }

    if (ls->has_buffer) {
        return;
    }

    if (!wlr_surface_has_buffer(ls->lock_surface->surface)) {
        return;
    }

    ls->has_buffer = true;
    fbwl_session_lock_maybe_send_locked(state);
}

static void session_lock_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_session_lock_surface *ls = wl_container_of(listener, ls, destroy);
    fbwl_cleanup_listener(&ls->surface_commit);
    fbwl_cleanup_listener(&ls->destroy);
    if (ls->scene_tree != NULL) {
        wlr_scene_node_destroy(&ls->scene_tree->node);
        ls->scene_tree = NULL;
    }
    wl_list_remove(&ls->link);
    free(ls);
}

static void fbwl_session_lock_new_surface(struct wl_listener *listener, void *data) {
    struct fbwl_session_lock_state *state = wl_container_of(listener, state, session_lock_new_surface);
    struct wlr_session_lock_surface_v1 *lock_surface = data;
    if (state == NULL || lock_surface == NULL || lock_surface->surface == NULL) {
        return;
    }

    struct fbwl_session_lock_surface *ls = calloc(1, sizeof(*ls));
    if (ls == NULL) {
        return;
    }
    ls->state = state;
    ls->lock_surface = lock_surface;
    wl_list_insert(&state->session_lock_surfaces, &ls->link);

    struct wlr_scene *scene = state->scene != NULL ? *state->scene : NULL;
    struct wlr_scene_tree *overlay = state->layer_overlay != NULL ? *state->layer_overlay : NULL;
    struct wlr_scene_tree *parent = overlay != NULL ? overlay : (scene != NULL ? &scene->tree : NULL);
    if (parent != NULL) {
        ls->scene_tree = wlr_scene_tree_create(parent);
        if (ls->scene_tree != NULL) {
            (void)wlr_scene_surface_create(ls->scene_tree, lock_surface->surface);

            struct wlr_box box = {0};
            struct wlr_output_layout *layout = state->output_layout != NULL ? *state->output_layout : NULL;
            if (layout != NULL && lock_surface->output != NULL) {
                wlr_output_layout_get_box(layout, lock_surface->output, &box);
            }
            wlr_scene_node_set_position(&ls->scene_tree->node, box.x, box.y);
            wlr_scene_node_raise_to_top(&ls->scene_tree->node);
        }
    }

    ls->destroy.notify = session_lock_surface_destroy;
    wl_signal_add(&lock_surface->events.destroy, &ls->destroy);

    ls->has_buffer = wlr_surface_has_buffer(lock_surface->surface);
    ls->surface_commit.notify = session_lock_surface_commit;
    wl_signal_add(&lock_surface->surface->events.commit, &ls->surface_commit);

    uint32_t width = 0;
    uint32_t height = 0;
    if (lock_surface->output != NULL) {
        width = (uint32_t)lock_surface->output->width;
        height = (uint32_t)lock_surface->output->height;
    }
    if (width == 0) {
        width = 1280;
    }
    if (height == 0) {
        height = 720;
    }
    (void)wlr_session_lock_surface_v1_configure(lock_surface, width, height);

    if (state->hooks.clear_keyboard_focus != NULL) {
        state->hooks.clear_keyboard_focus(state->hooks.userdata);
    }

    struct wlr_seat *seat = state->seat != NULL ? *state->seat : NULL;
    if (seat != NULL) {
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard != NULL) {
            wlr_seat_keyboard_notify_enter(seat, lock_surface->surface,
                keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
        }
    }

    if (state->hooks.update_shortcuts_inhibitor != NULL) {
        state->hooks.update_shortcuts_inhibitor(state->hooks.userdata);
    }
    fbwl_session_lock_maybe_send_locked(state);
}

static void fbwl_session_lock_unlock(struct wl_listener *listener, void *data) {
    struct fbwl_session_lock_state *state = wl_container_of(listener, state, session_lock_unlock);
    struct wlr_session_lock_v1 *lock = data;
    if (state == NULL || lock == NULL) {
        return;
    }
    wlr_log(WLR_INFO, "SessionLock: unlock");
    wlr_session_lock_v1_destroy(lock);
}

static void fbwl_session_lock_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_session_lock_state *state = wl_container_of(listener, state, session_lock_destroy);
    if (state == NULL) {
        return;
    }

    wlr_log(WLR_INFO, "SessionLock: destroy");

    state->session_lock = NULL;
    state->session_locked = false;
    state->session_lock_sent_locked = false;
    state->session_lock_expected_surfaces = 0;

    fbwl_cleanup_listener(&state->session_lock_new_surface);
    fbwl_cleanup_listener(&state->session_lock_unlock);
    fbwl_cleanup_listener(&state->session_lock_destroy);

    struct fbwl_session_lock_surface *ls, *tmp;
    wl_list_for_each_safe(ls, tmp, &state->session_lock_surfaces, link) {
        fbwl_cleanup_listener(&ls->surface_commit);
        fbwl_cleanup_listener(&ls->destroy);
        if (ls->scene_tree != NULL) {
            wlr_scene_node_destroy(&ls->scene_tree->node);
            ls->scene_tree = NULL;
        }
        wl_list_remove(&ls->link);
        free(ls);
    }
}

static void fbwl_session_lock_new_lock(struct wl_listener *listener, void *data) {
    struct fbwl_session_lock_state *state = wl_container_of(listener, state, new_session_lock);
    struct wlr_session_lock_v1 *lock = data;
    if (state == NULL || lock == NULL) {
        return;
    }

    if (state->session_lock != NULL) {
        wlr_log(WLR_INFO, "SessionLock: rejecting (already locked)");
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    state->session_lock = lock;
    state->session_locked = true;
    state->session_lock_sent_locked = false;
    state->session_lock_expected_surfaces = state->outputs != NULL ? fbwl_output_count(state->outputs) : 0;
    if (state->session_lock_expected_surfaces < 1) {
        state->session_lock_expected_surfaces = 1;
    }

    wlr_log(WLR_INFO, "SessionLock: new lock");

    if (state->hooks.clear_keyboard_focus != NULL) {
        state->hooks.clear_keyboard_focus(state->hooks.userdata);
    }

    struct wlr_seat *seat = state->seat != NULL ? *state->seat : NULL;
    if (seat != NULL) {
        wlr_seat_pointer_clear_focus(seat);
    }
    if (state->hooks.text_input_update_focus != NULL) {
        state->hooks.text_input_update_focus(state->hooks.userdata, NULL);
    }

    state->session_lock_new_surface.notify = fbwl_session_lock_new_surface;
    wl_signal_add(&lock->events.new_surface, &state->session_lock_new_surface);
    state->session_lock_unlock.notify = fbwl_session_lock_unlock;
    wl_signal_add(&lock->events.unlock, &state->session_lock_unlock);
    state->session_lock_destroy.notify = fbwl_session_lock_destroy;
    wl_signal_add(&lock->events.destroy, &state->session_lock_destroy);
}

bool fbwl_session_lock_init(struct fbwl_session_lock_state *state, struct wl_display *display,
        struct wlr_scene **scene, struct wlr_scene_tree **layer_overlay, struct wlr_output_layout **output_layout,
        struct wlr_seat **seat, struct wl_list *outputs, const struct fbwl_session_lock_hooks *hooks) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->scene = scene;
    state->layer_overlay = layer_overlay;
    state->output_layout = output_layout;
    state->seat = seat;
    state->outputs = outputs;
    state->hooks = hooks != NULL ? *hooks : (struct fbwl_session_lock_hooks){0};

    state->session_lock_mgr = wlr_session_lock_manager_v1_create(display);
    if (state->session_lock_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create session lock manager");
        return false;
    }

    state->new_session_lock.notify = fbwl_session_lock_new_lock;
    wl_signal_add(&state->session_lock_mgr->events.new_lock, &state->new_session_lock);

    state->session_lock = NULL;
    wl_list_init(&state->session_lock_surfaces);
    state->session_locked = false;
    state->session_lock_sent_locked = false;
    state->session_lock_expected_surfaces = 0;
    return true;
}

void fbwl_session_lock_finish(struct fbwl_session_lock_state *state) {
    if (state == NULL) {
        return;
    }
    fbwl_cleanup_listener(&state->new_session_lock);
    fbwl_cleanup_listener(&state->session_lock_new_surface);
    fbwl_cleanup_listener(&state->session_lock_unlock);
    fbwl_cleanup_listener(&state->session_lock_destroy);
}

bool fbwl_session_lock_is_locked(const struct fbwl_session_lock_state *state) {
    return state != NULL && state->session_locked;
}

void fbwl_session_lock_on_output_destroyed(struct fbwl_session_lock_state *state) {
    if (state == NULL || state->session_lock == NULL || state->session_lock_sent_locked ||
            state->session_lock_expected_surfaces <= 1) {
        return;
    }
    state->session_lock_expected_surfaces--;
    fbwl_session_lock_maybe_send_locked(state);
}

