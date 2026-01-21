#include "wayland/fbwl_xdg_activation.h"

#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"

#include "wmcore/fbwm_core.h"

#include <stdbool.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/util/log.h>

static void fbwl_xdg_activation_request_activate(struct wl_listener *listener, void *data) {
    struct fbwl_xdg_activation_state *state = wl_container_of(listener, state, request_activate);
    struct wlr_xdg_activation_v1_request_activate_event *event = data;
    if (event == NULL || event->surface == NULL) {
        return;
    }

    struct fbwl_view *view = fbwl_view_from_surface(event->surface);
    if (view == NULL) {
        wlr_log(WLR_INFO, "XDG activation: request for unknown surface");
        return;
    }

    if (view->minimized) {
        state->view_set_minimized(view, false, "xdg-activation");
    }
    fbwm_core_focus_view(state->wm, &view->wm_view);
}

bool fbwl_xdg_activation_init(struct fbwl_xdg_activation_state *state, struct wl_display *display,
        struct fbwm_core *wm, void (*view_set_minimized)(struct fbwl_view *view, bool minimized, const char *why)) {
    if (state == NULL || display == NULL || wm == NULL || view_set_minimized == NULL) {
        return false;
    }

    state->wm = wm;
    state->view_set_minimized = view_set_minimized;

    state->xdg_activation = wlr_xdg_activation_v1_create(display);
    if (state->xdg_activation == NULL) {
        wlr_log(WLR_ERROR, "failed to create xdg activation manager");
        return false;
    }
    state->request_activate.notify = fbwl_xdg_activation_request_activate;
    wl_signal_add(&state->xdg_activation->events.request_activate, &state->request_activate);
    return true;
}

void fbwl_xdg_activation_finish(struct fbwl_xdg_activation_state *state) {
    if (state == NULL) {
        return;
    }
    fbwl_cleanup_listener(&state->request_activate);
}

