#include "wayland/fbwl_fractional_scale.h"

#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/util/log.h>

bool fbwl_fractional_scale_init(struct fbwl_fractional_scale_state *state, struct wl_display *display) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->manager = wlr_fractional_scale_manager_v1_create(display, 1);
    if (state->manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create fractional scale manager");
        return false;
    }

    return true;
}

void fbwl_fractional_scale_finish(struct fbwl_fractional_scale_state *state) {
    if (state == NULL) {
        return;
    }

    state->manager = NULL;
}

