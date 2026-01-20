#include "wayland/fbwl_output_power.h"

#include <stdbool.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_output_management.h"

void fbwl_output_power_handle_set_mode(struct wlr_output_power_v1_set_mode_event *event,
        struct wlr_output_manager_v1 *output_manager,
        struct wl_list *outputs,
        struct wlr_output_layout *output_layout) {
    if (event == NULL || event->output == NULL) {
        return;
    }

    const bool enable = (event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    wlr_log(WLR_INFO, "OutputPower: set_mode output=%s mode=%s",
        event->output->name != NULL ? event->output->name : "(unnamed)",
        enable ? "on" : "off");

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, enable);
    if (enable) {
        struct wlr_output_mode *mode = wlr_output_preferred_mode(event->output);
        if (mode != NULL) {
            wlr_output_state_set_mode(&state, mode);
        }
    }

    const bool ok = wlr_output_commit_state(event->output, &state);
    wlr_output_state_finish(&state);
    if (!ok) {
        wlr_log(WLR_ERROR, "OutputPower: commit failed output=%s",
            event->output->name != NULL ? event->output->name : "(unnamed)");
    }

    fbwl_output_manager_update(output_manager, outputs, output_layout);
}

