#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_output.h>

// C wrapper functions for wlr_scene operations to avoid C++ compilation issues

struct wlr_scene* fluxbox_scene_create(void) {
    return wlr_scene_create();
}

struct wlr_scene_output_layout* fluxbox_scene_attach_output_layout(struct wlr_scene* scene, struct wlr_output_layout* output_layout) {
    return wlr_scene_attach_output_layout(scene, output_layout);
}

void fluxbox_scene_destroy(struct wlr_scene* scene) {
    wlr_scene_node_destroy(&scene->tree.node);
}

struct wlr_scene_output* fluxbox_scene_output_create(struct wlr_scene* scene, struct wlr_output* output) {
    return wlr_scene_output_create(scene, output);
}

struct wlr_scene_tree* fluxbox_scene_tree_create(struct wlr_scene* scene) {
    return &scene->tree;
}

struct wlr_scene_tree* fluxbox_scene_xdg_surface_create(struct wlr_scene* scene, struct wlr_xdg_surface* xdg_surface) {
    return wlr_scene_xdg_surface_create(&scene->tree, xdg_surface);
}

void fluxbox_scene_node_set_position(struct wlr_scene_tree* tree, int x, int y) {
    wlr_scene_node_set_position(&tree->node, x, y);
}

void fluxbox_scene_node_set_enabled(struct wlr_scene_tree* tree, bool enabled) {
    wlr_scene_node_set_enabled(&tree->node, enabled);
}

void fluxbox_scene_node_destroy(struct wlr_scene_tree* tree) {
    wlr_scene_node_destroy(&tree->node);
}

void fluxbox_scene_node_reparent(struct wlr_scene_tree* tree, struct wlr_scene_tree* new_parent) {
    wlr_scene_node_reparent(&tree->node, new_parent);
}

struct wlr_scene_tree* fluxbox_scene_tree_create_subsurface(struct wlr_scene_tree* parent) {
    return wlr_scene_tree_create(parent);
}

void fluxbox_scene_node_get_position(struct wlr_scene_tree* tree, int* x, int* y) {
    if (x) *x = tree->node.x;
    if (y) *y = tree->node.y;
}

struct wlr_scene_rect* fluxbox_scene_rect_create(struct wlr_scene_tree* parent, int width, int height, const float* color) {
    if (!parent || !color) return NULL;
    return wlr_scene_rect_create(parent, width, height, color);
}

void fluxbox_scene_rect_set_size(struct wlr_scene_rect* rect, int width, int height) {
    if (!rect) return;
    wlr_scene_rect_set_size(rect, width, height);
}

void fluxbox_scene_rect_set_color(struct wlr_scene_rect* rect, const float* color) {
    if (!rect || !color) return;
    wlr_scene_rect_set_color(rect, color);
}

void fluxbox_scene_node_destroy_rect(struct wlr_scene_rect* rect) {
    if (!rect) return;
    wlr_scene_node_destroy(&rect->node);
}

void fluxbox_scene_rect_set_position(struct wlr_scene_rect* rect, int x, int y) {
    if (!rect) return;
    wlr_scene_node_set_position(&rect->node, x, y);
}