#include "wayland/fbwl_viewporter.h"

#include <wlr/types/wlr_viewporter.h>
#include <wlr/util/log.h>

bool fbwl_viewporter_init(struct fbwl_viewporter_state *state, struct wl_display *display) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->viewporter = wlr_viewporter_create(display);
    if (state->viewporter == NULL) {
        wlr_log(WLR_ERROR, "failed to create viewporter");
        return false;
    }

    return true;
}

void fbwl_viewporter_finish(struct fbwl_viewporter_state *state) {
    if (state == NULL) {
        return;
    }

    state->viewporter = NULL;
}

