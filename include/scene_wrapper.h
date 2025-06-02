#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct wlr_scene;
struct wlr_scene_tree;
struct wlr_scene_output;
struct wlr_scene_output_layout;
struct wlr_xdg_surface;
struct wlr_output;
struct wlr_output_layout;

// C wrapper functions for wlr_scene operations to avoid C++ compilation issues
struct wlr_scene* fluxbox_scene_create(void);
struct wlr_scene_output_layout* fluxbox_scene_attach_output_layout(struct wlr_scene* scene, struct wlr_output_layout* output_layout);
void fluxbox_scene_destroy(struct wlr_scene* scene);
struct wlr_scene_output* fluxbox_scene_output_create(struct wlr_scene* scene, struct wlr_output* output);
struct wlr_scene_tree* fluxbox_scene_tree_create(struct wlr_scene* scene);
struct wlr_scene_tree* fluxbox_scene_xdg_surface_create(struct wlr_scene* scene, struct wlr_xdg_surface* xdg_surface);
void fluxbox_scene_node_set_position(struct wlr_scene_tree* tree, int x, int y);
void fluxbox_scene_node_set_enabled(struct wlr_scene_tree* tree, bool enabled);
void fluxbox_scene_node_destroy(struct wlr_scene_tree* tree);
void fluxbox_scene_node_reparent(struct wlr_scene_tree* tree, struct wlr_scene_tree* new_parent);
struct wlr_scene_tree* fluxbox_scene_tree_create_subsurface(struct wlr_scene_tree* parent);
void fluxbox_scene_node_get_position(struct wlr_scene_tree* tree, int* x, int* y);

// Scene rect wrappers
struct wlr_scene_rect;
struct wlr_scene_rect* fluxbox_scene_rect_create(struct wlr_scene_tree* parent, int width, int height, const float* color);
void fluxbox_scene_rect_set_size(struct wlr_scene_rect* rect, int width, int height);
void fluxbox_scene_rect_set_color(struct wlr_scene_rect* rect, const float* color);
void fluxbox_scene_node_destroy_rect(struct wlr_scene_rect* rect);
void fluxbox_scene_rect_set_position(struct wlr_scene_rect* rect, int x, int y);

#ifdef __cplusplus
}
#endif