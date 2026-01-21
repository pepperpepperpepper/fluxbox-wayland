#include "wayland/fbwl_xdg_output.h"

#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>

bool fbwl_xdg_output_init(struct fbwl_xdg_output_state *state, struct wl_display *display,
        struct wlr_output_layout *output_layout) {
    if (state == NULL || display == NULL || output_layout == NULL) {
        return false;
    }

    state->manager = wlr_xdg_output_manager_v1_create(display, output_layout);
    if (state->manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create xdg output manager");
        return false;
    }

    return true;
}

void fbwl_xdg_output_finish(struct fbwl_xdg_output_state *state) {
    if (state == NULL) {
        return;
    }

    state->manager = NULL;
}

