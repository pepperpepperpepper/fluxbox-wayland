#pragma once

#include <stdbool.h>

#include "wayland/fbwl_wallpaper.h"

struct wlr_buffer;
struct wlr_output_layout;
struct wlr_scene_tree;

struct wlr_scene_buffer;
struct wlr_scene_rect;

struct fbwl_pseudo_bg {
    struct wlr_scene_buffer *image;
    struct wlr_scene_rect *rect;
};

void fbwl_pseudo_bg_destroy(struct fbwl_pseudo_bg *bg);

void fbwl_pseudo_bg_update(struct fbwl_pseudo_bg *bg,
    struct wlr_scene_tree *parent,
    struct wlr_output_layout *output_layout,
    int global_x,
    int global_y,
    int rel_x,
    int rel_y,
    int width,
    int height,
    enum fbwl_wallpaper_mode wallpaper_mode,
    struct wlr_buffer *wallpaper_buf,
    const float background_color[4]);
