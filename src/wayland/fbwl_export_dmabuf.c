#include "wayland/fbwl_export_dmabuf.h"

#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/util/log.h>

bool fbwl_export_dmabuf_init(struct fbwl_export_dmabuf_state *state, struct wl_display *display) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->manager = wlr_export_dmabuf_manager_v1_create(display);
    if (state->manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create export dmabuf manager");
        return false;
    }

    return true;
}

void fbwl_export_dmabuf_finish(struct fbwl_export_dmabuf_state *state) {
    if (state == NULL) {
        return;
    }

    state->manager = NULL;
}

