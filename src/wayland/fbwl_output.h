#pragma once

#include <stddef.h>

#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct wlr_allocator;
struct wlr_renderer;
struct wlr_output;
struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_output_layout;
struct wlr_scene_rect;

typedef void (*fbwl_output_on_destroy_fn)(void *userdata, struct wlr_output *wlr_output);

struct fbwl_output {
    struct wl_list link;
    struct wlr_output *wlr_output;
    struct wlr_box usable_area;
    struct wlr_scene_rect *background_rect;
    struct wlr_scene *scene;

    fbwl_output_on_destroy_fn on_destroy;
    void *on_destroy_userdata;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct fbwl_output *fbwl_output_find(struct wl_list *outputs, struct wlr_output *wlr_output);
size_t fbwl_output_count(const struct wl_list *outputs);

struct fbwl_output *fbwl_output_create(struct wl_list *outputs, struct wlr_output *wlr_output,
        struct wlr_allocator *allocator, struct wlr_renderer *renderer,
        struct wlr_output_layout *output_layout, struct wlr_scene *scene, struct wlr_scene_output_layout *scene_layout,
        fbwl_output_on_destroy_fn on_destroy, void *on_destroy_userdata);

