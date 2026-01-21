#include "wayland/fbwl_screencopy.h"

#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/util/log.h>

bool fbwl_screencopy_init(struct fbwl_screencopy_state *state, struct wl_display *display) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->manager = wlr_screencopy_manager_v1_create(display);
    if (state->manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create screencopy manager");
        return false;
    }

    return true;
}

void fbwl_screencopy_finish(struct fbwl_screencopy_state *state) {
    if (state == NULL) {
        return;
    }

    state->manager = NULL;
}

