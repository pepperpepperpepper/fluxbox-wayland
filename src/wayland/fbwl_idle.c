#include "wayland/fbwl_idle.h"

#include "wayland/fbwl_util.h"

#include <stdbool.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

struct fbwl_idle_inhibitor {
    struct fbwl_idle_state *state;
    struct wlr_idle_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
};

static void fbwl_idle_set_inhibited(struct fbwl_idle_state *state, bool inhibited, const char *why) {
    if (state == NULL) {
        return;
    }
    if (state->idle_inhibited == inhibited) {
        return;
    }
    state->idle_inhibited = inhibited;
    if (state->idle_notifier != NULL) {
        wlr_idle_notifier_v1_set_inhibited(state->idle_notifier, inhibited);
    }
    wlr_log(WLR_INFO, "Idle: inhibited=%d reason=%s", inhibited ? 1 : 0, why != NULL ? why : "(null)");
}

static void idle_inhibitor_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_idle_inhibitor *ii = wl_container_of(listener, ii, destroy);
    struct fbwl_idle_state *state = ii->state;
    fbwl_cleanup_listener(&ii->destroy);
    if (state != NULL && state->idle_inhibitor_count > 0) {
        state->idle_inhibitor_count--;
        fbwl_idle_set_inhibited(state, state->idle_inhibitor_count > 0, "destroy-inhibitor");
    }
    free(ii);
}

static void fbwl_new_idle_inhibitor(struct wl_listener *listener, void *data) {
    struct fbwl_idle_state *state = wl_container_of(listener, state, new_idle_inhibitor);
    struct wlr_idle_inhibitor_v1 *inhibitor = data;

    struct fbwl_idle_inhibitor *ii = calloc(1, sizeof(*ii));
    ii->state = state;
    ii->inhibitor = inhibitor;
    ii->destroy.notify = idle_inhibitor_destroy;
    wl_signal_add(&inhibitor->events.destroy, &ii->destroy);

    state->idle_inhibitor_count++;
    fbwl_idle_set_inhibited(state, true, "new-inhibitor");
}

bool fbwl_idle_init(struct fbwl_idle_state *state, struct wl_display *display, struct wlr_seat **seat) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->seat = seat;

    state->idle_notifier = wlr_idle_notifier_v1_create(display);
    if (state->idle_notifier == NULL) {
        wlr_log(WLR_ERROR, "failed to create idle notifier");
        return false;
    }
    state->idle_inhibited = false;
    wlr_idle_notifier_v1_set_inhibited(state->idle_notifier, false);

    state->idle_inhibit_mgr = wlr_idle_inhibit_v1_create(display);
    if (state->idle_inhibit_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create idle inhibit manager");
        return false;
    }
    state->idle_inhibitor_count = 0;
    state->new_idle_inhibitor.notify = fbwl_new_idle_inhibitor;
    wl_signal_add(&state->idle_inhibit_mgr->events.new_inhibitor, &state->new_idle_inhibitor);
    return true;
}

void fbwl_idle_finish(struct fbwl_idle_state *state) {
    if (state == NULL) {
        return;
    }
    fbwl_cleanup_listener(&state->new_idle_inhibitor);
}

void fbwl_idle_notify_activity(struct fbwl_idle_state *state) {
    if (state == NULL || state->idle_notifier == NULL || state->seat == NULL || *state->seat == NULL) {
        return;
    }
    wlr_idle_notifier_v1_notify_activity(state->idle_notifier, *state->seat);
}

