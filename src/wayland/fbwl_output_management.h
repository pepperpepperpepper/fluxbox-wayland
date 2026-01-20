#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_backend;
struct wlr_output;
struct wlr_output_configuration_v1;
struct wlr_output_layout;
struct wlr_output_manager_v1;

typedef void (*fbwl_output_mgmt_arrange_layers_fn)(void *userdata, struct wlr_output *wlr_output);

void fbwl_output_manager_update(struct wlr_output_manager_v1 *output_manager,
        struct wl_list *outputs,
        struct wlr_output_layout *output_layout);

bool fbwl_output_management_apply_config(struct wlr_backend *backend,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        struct wlr_output_configuration_v1 *config,
        bool test_only,
        fbwl_output_mgmt_arrange_layers_fn arrange_layers_on_output,
        void *arrange_layers_userdata);

