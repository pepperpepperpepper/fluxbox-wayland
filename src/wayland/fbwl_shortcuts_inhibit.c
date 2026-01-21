#include "wayland/fbwl_shortcuts_inhibit.h"

#include "wayland/fbwl_util.h"

#include <stdbool.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

struct fbwl_shortcuts_inhibitor {
    struct wl_list link;
    struct fbwl_shortcuts_inhibit_state *state;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
};

static struct wlr_keyboard_shortcuts_inhibitor_v1 *fbwl_find_shortcuts_inhibitor(
        struct fbwl_shortcuts_inhibit_state *state, struct wlr_surface *surface) {
    if (state == NULL || surface == NULL || state->seat == NULL || *state->seat == NULL) {
        return NULL;
    }

    struct wlr_seat *seat = *state->seat;
    struct fbwl_shortcuts_inhibitor *si;
    wl_list_for_each(si, &state->shortcuts_inhibitors, link) {
        struct wlr_keyboard_shortcuts_inhibitor_v1 *inhib = si->inhibitor;
        if (inhib != NULL && inhib->seat == seat && inhib->surface == surface) {
            return inhib;
        }
    }
    return NULL;
}

void fbwl_shortcuts_inhibit_update(struct fbwl_shortcuts_inhibit_state *state) {
    if (state == NULL || state->shortcuts_inhibit_mgr == NULL || state->seat == NULL || *state->seat == NULL) {
        return;
    }

    struct wlr_seat *seat = *state->seat;
    struct wlr_surface *focused_surface = seat->keyboard_state.focused_surface;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *want =
        fbwl_find_shortcuts_inhibitor(state, focused_surface);

    if (want == state->active_shortcuts_inhibitor) {
        if (want != NULL && !want->active) {
            wlr_keyboard_shortcuts_inhibitor_v1_activate(want);
            wlr_log(WLR_INFO, "ShortcutsInhibit: activated");
        }
        return;
    }

    if (state->active_shortcuts_inhibitor != NULL) {
        struct wlr_keyboard_shortcuts_inhibitor_v1 *old = state->active_shortcuts_inhibitor;
        state->active_shortcuts_inhibitor = NULL;
        if (old->active) {
            wlr_keyboard_shortcuts_inhibitor_v1_deactivate(old);
            wlr_log(WLR_INFO, "ShortcutsInhibit: deactivated");
        }
    }

    if (want != NULL) {
        wlr_keyboard_shortcuts_inhibitor_v1_activate(want);
        state->active_shortcuts_inhibitor = want;
        wlr_log(WLR_INFO, "ShortcutsInhibit: activated");
    }
}

bool fbwl_shortcuts_inhibit_is_inhibited(struct fbwl_shortcuts_inhibit_state *state) {
    if (state == NULL || state->seat == NULL || *state->seat == NULL) {
        return false;
    }

    struct wlr_seat *seat = *state->seat;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhib = state->active_shortcuts_inhibitor;
    if (inhib == NULL || !inhib->active) {
        return false;
    }
    return seat->keyboard_state.focused_surface == inhib->surface;
}

static void shortcuts_inhibitor_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_shortcuts_inhibitor *si = wl_container_of(listener, si, destroy);
    if (si == NULL) {
        return;
    }

    struct fbwl_shortcuts_inhibit_state *state = si->state;
    if (state != NULL && state->active_shortcuts_inhibitor == si->inhibitor) {
        state->active_shortcuts_inhibitor = NULL;
    }

    fbwl_cleanup_listener(&si->destroy);
    wl_list_remove(&si->link);
    free(si);
}

static void fbwl_new_shortcuts_inhibitor(struct wl_listener *listener, void *data) {
    struct fbwl_shortcuts_inhibit_state *state = wl_container_of(listener, state, new_shortcuts_inhibitor);
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor = data;
    if (state == NULL || inhibitor == NULL) {
        return;
    }

    wlr_log(WLR_INFO, "ShortcutsInhibit: new inhibitor");

    struct fbwl_shortcuts_inhibitor *si = calloc(1, sizeof(*si));
    if (si == NULL) {
        return;
    }
    si->state = state;
    si->inhibitor = inhibitor;
    wl_list_insert(&state->shortcuts_inhibitors, &si->link);

    si->destroy.notify = shortcuts_inhibitor_destroy;
    wl_signal_add(&inhibitor->events.destroy, &si->destroy);

    fbwl_shortcuts_inhibit_update(state);
}

bool fbwl_shortcuts_inhibit_init(struct fbwl_shortcuts_inhibit_state *state, struct wl_display *display,
        struct wlr_seat **seat) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->seat = seat;

    state->shortcuts_inhibit_mgr = wlr_keyboard_shortcuts_inhibit_v1_create(display);
    if (state->shortcuts_inhibit_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create keyboard-shortcuts-inhibit manager");
        return false;
    }

    state->active_shortcuts_inhibitor = NULL;
    wl_list_init(&state->shortcuts_inhibitors);
    state->new_shortcuts_inhibitor.notify = fbwl_new_shortcuts_inhibitor;
    wl_signal_add(&state->shortcuts_inhibit_mgr->events.new_inhibitor, &state->new_shortcuts_inhibitor);
    return true;
}

void fbwl_shortcuts_inhibit_finish(struct fbwl_shortcuts_inhibit_state *state) {
    if (state == NULL) {
        return;
    }
    fbwl_cleanup_listener(&state->new_shortcuts_inhibitor);
}

