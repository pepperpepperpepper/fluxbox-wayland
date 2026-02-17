#pragma once

#include <stdbool.h>
#include <stdint.h>

struct fbwl_decor_theme;
struct wlr_buffer;
struct wlr_scene_tree;

enum fbwl_toolbar_placement;

uint32_t fbwl_ui_toolbar_shaped_round_mask(enum fbwl_toolbar_placement placement, const struct fbwl_decor_theme *theme);

bool fbwl_ui_toolbar_shaped_point_visible(enum fbwl_toolbar_placement placement, const struct fbwl_decor_theme *theme,
        int frame_w, int frame_h, int x, int y);

struct wlr_buffer *fbwl_ui_toolbar_shaped_mask_buffer_owned(enum fbwl_toolbar_placement placement,
        const struct fbwl_decor_theme *theme,
        struct wlr_buffer *src,
        int offset_x, int offset_y, int frame_w, int frame_h);

void fbwl_ui_toolbar_build_border(struct wlr_scene_tree *tree, enum fbwl_toolbar_placement placement,
        const struct fbwl_decor_theme *theme,
        int width, int height, int border_w, float alpha);

