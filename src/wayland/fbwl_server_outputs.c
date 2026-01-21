#include "wayland/fbwl_server_internal.h"

#include "wayland/fbwl_output.h"
#include "wayland/fbwl_output_management.h"
#include "wayland/fbwl_output_power.h"
#include "wayland/fbwl_scene_layers.h"
#include "wayland/fbwl_view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

static uint8_t float_to_u8_clamped(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

static uint32_t rgb24_from_rgba(const float rgba[4]) {
    if (rgba == NULL) {
        return 0;
    }
    const uint32_t r = float_to_u8_clamped(rgba[0]);
    const uint32_t g = float_to_u8_clamped(rgba[1]);
    const uint32_t b = float_to_u8_clamped(rgba[2]);
    return (r << 16) | (g << 8) | b;
}

static void server_background_update_output(struct fbwl_server *server, struct fbwl_output *output) {
    if (server == NULL || output == NULL || server->output_layout == NULL || server->layer_background == NULL ||
            output->wlr_output == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);

    if (box.width < 1 || box.height < 1) {
        if (output->background_rect != NULL) {
            wlr_scene_node_destroy(&output->background_rect->node);
            output->background_rect = NULL;
        }
        return;
    }

    if (output->background_rect == NULL) {
        output->background_rect =
            wlr_scene_rect_create(server->layer_background, box.width, box.height, server->background_color);
        if (output->background_rect == NULL) {
            wlr_log(WLR_ERROR, "Background: failed to create rect");
            return;
        }
    } else {
        wlr_scene_rect_set_size(output->background_rect, box.width, box.height);
        wlr_scene_rect_set_color(output->background_rect, server->background_color);
    }

    wlr_scene_node_set_position(&output->background_rect->node, box.x, box.y);
    wlr_scene_node_lower_to_bottom(&output->background_rect->node);

    const uint32_t rgb = rgb24_from_rgba(server->background_color);
    wlr_log(WLR_INFO, "Background: output name=%s x=%d y=%d w=%d h=%d rgb=#%06x",
        output->wlr_output->name != NULL ? output->wlr_output->name : "(unnamed)",
        box.x, box.y, box.width, box.height, rgb);
}

static void server_background_update_all(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    struct fbwl_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        server_background_update_output(server, out);
    }
}

static void server_output_management_arrange_layers_on_output(void *userdata, struct wlr_output *wlr_output) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_scene_layers_arrange_layer_surfaces_on_output(server->output_layout, &server->outputs, &server->layer_surfaces,
        wlr_output);
}

static void server_output_manager_test(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_manager_test);
    struct wlr_output_configuration_v1 *config = data;

    wlr_log(WLR_INFO, "OutputMgmt: test serial=%u", config->serial);
    const bool ok = fbwl_output_management_apply_config(server->backend, server->output_layout, &server->outputs,
        config, true, server_output_management_arrange_layers_on_output, server);
    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

static void server_output_manager_apply(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_manager_apply);
    struct wlr_output_configuration_v1 *config = data;

    wlr_log(WLR_INFO, "OutputMgmt: apply serial=%u", config->serial);
    const bool ok = fbwl_output_management_apply_config(server->backend, server->output_layout, &server->outputs,
        config, false, server_output_management_arrange_layers_on_output, server);
    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
        fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
        server_background_update_all(server);
        server_toolbar_ui_update_position(server);
        server_cmd_dialog_ui_update_position(server);
        server_osd_ui_update_position(server);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

static void server_output_power_set_mode(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_power_set_mode);
    if (server == NULL) {
        return;
    }
    fbwl_output_power_handle_set_mode(data, server->output_manager, &server->outputs, server->output_layout);
}

static void server_output_destroyed(void *userdata, struct wlr_output *wlr_output) {
    struct fbwl_server *server = userdata;
    if (server == NULL || wlr_output == NULL) {
        return;
    }
    fbwl_session_lock_on_output_destroyed(&server->session_lock);

    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view != NULL && view->foreign_output == wlr_output) {
            view->foreign_output = NULL;
        }
    }

    fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
    server_toolbar_ui_update_position(server);
    server_cmd_dialog_ui_update_position(server);
    server_osd_ui_update_position(server);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    struct fbwl_output *output = fbwl_output_create(&server->outputs, wlr_output,
        server->allocator, server->renderer,
        server->output_layout, server->scene, server->scene_layout,
        server_output_destroyed, server);
    if (output == NULL) {
        wlr_log(WLR_ERROR, "Output: failed to create output");
        return;
    }

    server_background_update_output(server, output);
    fbwl_scene_layers_arrange_layer_surfaces_on_output(server->output_layout, &server->outputs, &server->layer_surfaces,
        wlr_output);
    fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
    server_toolbar_ui_update_position(server);
    server_cmd_dialog_ui_update_position(server);
    server_osd_ui_update_position(server);
}

bool fbwl_server_outputs_init(struct fbwl_server *server) {
    if (server == NULL) {
        return false;
    }

    server->output_layout = wlr_output_layout_create(server->wl_display);
    if (server->output_layout == NULL) {
        wlr_log(WLR_ERROR, "failed to create output layout");
        return false;
    }

    server->output_manager = wlr_output_manager_v1_create(server->wl_display);
    if (server->output_manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create output manager");
        return false;
    }
    server->output_manager_apply.notify = server_output_manager_apply;
    wl_signal_add(&server->output_manager->events.apply, &server->output_manager_apply);
    server->output_manager_test.notify = server_output_manager_test;
    wl_signal_add(&server->output_manager->events.test, &server->output_manager_test);

    server->output_power_mgr = wlr_output_power_manager_v1_create(server->wl_display);
    if (server->output_power_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create output power manager");
        return false;
    }
    server->output_power_set_mode.notify = server_output_power_set_mode;
    wl_signal_add(&server->output_power_mgr->events.set_mode, &server->output_power_set_mode);

    if (!fbwl_xdg_output_init(&server->xdg_output, server->wl_display, server->output_layout)) {
        return false;
    }
    wl_list_init(&server->outputs);
    wl_list_init(&server->layer_surfaces);
    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    server->scene = wlr_scene_create();
    server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);
    server->layer_background = wlr_scene_tree_create(&server->scene->tree);
    server->layer_bottom = wlr_scene_tree_create(&server->scene->tree);
    server->layer_normal = wlr_scene_tree_create(&server->scene->tree);
    server->layer_fullscreen = wlr_scene_tree_create(&server->scene->tree);
    server->layer_top = wlr_scene_tree_create(&server->scene->tree);
    server->layer_overlay = wlr_scene_tree_create(&server->scene->tree);
    return true;
}
