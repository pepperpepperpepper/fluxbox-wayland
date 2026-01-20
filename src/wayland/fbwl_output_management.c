#include "wayland/fbwl_output_management.h"

#include <stdlib.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_output.h"

void fbwl_output_manager_update(struct wlr_output_manager_v1 *output_manager,
        struct wl_list *outputs,
        struct wlr_output_layout *output_layout) {
    if (output_manager == NULL || outputs == NULL || output_layout == NULL) {
        return;
    }

    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
    if (config == NULL) {
        wlr_log(WLR_ERROR, "OutputMgmt: failed to allocate current configuration");
        return;
    }

    struct fbwl_output *out;
    wl_list_for_each(out, outputs, link) {
        if (out->wlr_output == NULL) {
            continue;
        }
        struct wlr_output_configuration_head_v1 *head =
            wlr_output_configuration_head_v1_create(config, out->wlr_output);
        if (head == NULL) {
            continue;
        }

        struct wlr_output_layout_output *lo =
            wlr_output_layout_get(output_layout, out->wlr_output);
        if (lo != NULL) {
            head->state.x = lo->x;
            head->state.y = lo->y;
        } else {
            head->state.enabled = false;
        }
    }

    wlr_output_manager_v1_set_configuration(output_manager, config);
}

bool fbwl_output_management_apply_config(struct wlr_backend *backend,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        struct wlr_output_configuration_v1 *config,
        bool test_only,
        fbwl_output_mgmt_arrange_layers_fn arrange_layers_on_output,
        void *arrange_layers_userdata) {
    if (backend == NULL || output_layout == NULL || outputs == NULL || config == NULL) {
        return false;
    }

    size_t states_len = 0;
    struct wlr_backend_output_state *states =
        wlr_output_configuration_v1_build_state(config, &states_len);
    if (states == NULL) {
        wlr_log(WLR_ERROR, "OutputMgmt: failed to build output state array");
        return false;
    }

    bool ok = false;
    if (test_only) {
        ok = wlr_backend_test(backend, states, states_len);
    } else {
        ok = wlr_backend_commit(backend, states, states_len);
    }

    for (size_t i = 0; i < states_len; i++) {
        wlr_output_state_finish(&states[i].base);
    }
    free(states);

    if (!ok) {
        return false;
    }
    if (test_only) {
        return true;
    }

    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link) {
        struct wlr_output *wlr_output = head->state.output;
        if (wlr_output == NULL) {
            continue;
        }

        if (head->state.enabled) {
            wlr_output_layout_add(output_layout, wlr_output, head->state.x, head->state.y);

            struct wlr_box box = {0};
            wlr_output_layout_get_box(output_layout, wlr_output, &box);
            struct fbwl_output *out = fbwl_output_find(outputs, wlr_output);
            if (out != NULL) {
                out->usable_area = box;
            }
            wlr_log(WLR_INFO, "OutputLayout: name=%s x=%d y=%d w=%d h=%d",
                wlr_output->name != NULL ? wlr_output->name : "(unnamed)",
                box.x, box.y, box.width, box.height);
            if (arrange_layers_on_output != NULL) {
                arrange_layers_on_output(arrange_layers_userdata, wlr_output);
            }
        } else {
            wlr_output_layout_remove(output_layout, wlr_output);
            wlr_log(WLR_INFO, "OutputLayout: name=%s removed",
                wlr_output->name != NULL ? wlr_output->name : "(unnamed)");
        }
    }

    return true;
}

