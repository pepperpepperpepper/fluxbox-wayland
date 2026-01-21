#include "wayland/fbwl_ext_image_capture.h"

#include <wlr/util/log.h>
#include <wlr/version.h>

#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8) | 0)
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#endif

bool fbwl_ext_image_capture_init(struct fbwl_ext_image_capture_state *state, struct wl_display *display) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->copy_capture_manager = NULL;
    state->output_capture_source_manager = NULL;

#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8) | 0)
    state->copy_capture_manager = wlr_ext_image_copy_capture_manager_v1_create(display, 1);
    if (state->copy_capture_manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create ext-image-copy-capture manager");
        return false;
    }

    state->output_capture_source_manager = wlr_ext_output_image_capture_source_manager_v1_create(display, 1);
    if (state->output_capture_source_manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create ext-output-image-capture-source manager");
        return false;
    }
#endif

    return true;
}

void fbwl_ext_image_capture_finish(struct fbwl_ext_image_capture_state *state) {
    if (state == NULL) {
        return;
    }

    state->copy_capture_manager = NULL;
    state->output_capture_source_manager = NULL;
}

