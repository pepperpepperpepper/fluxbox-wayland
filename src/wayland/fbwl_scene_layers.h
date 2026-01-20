#pragma once

#include <wayland-server-core.h>

struct wlr_layer_surface_v1;
struct wlr_output;
struct wlr_output_layout;
struct wlr_scene_tree;

void fbwl_scene_layers_arrange_layer_surfaces_on_output(struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        struct wl_list *layer_surfaces,
        struct wlr_output *wlr_output);

void fbwl_scene_layers_handle_new_layer_surface(struct wlr_layer_surface_v1 *layer_surface,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        struct wl_list *layer_surfaces,
        struct wlr_scene_tree *layer_background,
        struct wlr_scene_tree *layer_bottom,
        struct wlr_scene_tree *layer_top,
        struct wlr_scene_tree *layer_overlay,
        struct wlr_scene_tree *scene_root);

