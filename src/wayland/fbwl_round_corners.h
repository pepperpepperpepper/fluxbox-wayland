#pragma once

#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>

struct wlr_buffer;

enum fbwl_round_corners_mask {
    FBWL_ROUND_CORNERS_NONE = 0,
    FBWL_ROUND_CORNERS_BOTTOMRIGHT = 0x01,
    FBWL_ROUND_CORNERS_TOPRIGHT = 0x02,
    FBWL_ROUND_CORNERS_BOTTOMLEFT = 0x04,
    FBWL_ROUND_CORNERS_TOPLEFT = 0x08,
};

uint32_t fbwl_round_corners_parse(const char *val);
bool fbwl_round_corners_point_visible(uint32_t mask, int frame_w, int frame_h, int x, int y);

void fbwl_round_corners_apply_to_cairo_surface(cairo_surface_t *surface,
    int offset_x, int offset_y, int frame_w, int frame_h, uint32_t mask);

// Takes ownership of `src`. May return `src` (unmodified) when no masking is needed.
struct wlr_buffer *fbwl_round_corners_mask_buffer_owned(struct wlr_buffer *src,
    int offset_x, int offset_y, int frame_w, int frame_h, uint32_t mask);

