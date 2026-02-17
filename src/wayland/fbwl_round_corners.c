#include "wayland/fbwl_round_corners.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>

#include "wayland/fbwl_ui_text.h"

// These bitmasks match Fluxbox's X11 shaping patterns (see src/FbTk/Shape.cc).
// A set bit indicates the pixel is kept; an unset bit indicates the pixel is cut out.
static const uint8_t tl_keep[8] = {0xc0, 0xf8, 0xfc, 0xfe, 0xfe, 0xfe, 0xff, 0xff};
static const uint8_t tr_keep[8] = {0x03, 0x1f, 0x3f, 0x7f, 0x7f, 0x7f, 0xff, 0xff};
static const uint8_t bl_keep[8] = {0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xfc, 0xf8, 0xc0};
static const uint8_t br_keep[8] = {0xff, 0xff, 0x7f, 0x7f, 0x7f, 0x3f, 0x1f, 0x03};

static bool rects_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) {
        return false;
    }
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static uint32_t parse_token(const char *tok) {
    if (tok == NULL || *tok == '\0') {
        return 0;
    }
    if (strcasecmp(tok, "TopLeft") == 0) {
        return FBWL_ROUND_CORNERS_TOPLEFT;
    }
    if (strcasecmp(tok, "TopRight") == 0) {
        return FBWL_ROUND_CORNERS_TOPRIGHT;
    }
    if (strcasecmp(tok, "BottomLeft") == 0) {
        return FBWL_ROUND_CORNERS_BOTTOMLEFT;
    }
    if (strcasecmp(tok, "BottomRight") == 0) {
        return FBWL_ROUND_CORNERS_BOTTOMRIGHT;
    }
    if (strcasecmp(tok, "None") == 0) {
        return FBWL_ROUND_CORNERS_NONE;
    }
    return 0;
}

uint32_t fbwl_round_corners_parse(const char *val) {
    if (val == NULL) {
        return 0;
    }

    while (*val != '\0' && isspace((unsigned char)*val)) {
        val++;
    }
    if (*val == '\0') {
        return 0;
    }

    uint32_t out = 0;
    const char *p = val;
    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char *start = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        const size_t len = (size_t)(p - start);
        if (len == 0) {
            continue;
        }

        char tok[32];
        if (len >= sizeof(tok)) {
            continue;
        }
        memcpy(tok, start, len);
        tok[len] = '\0';

        const uint32_t bit = parse_token(tok);
        if (bit == FBWL_ROUND_CORNERS_NONE && strcasecmp(tok, "None") == 0) {
            return 0;
        }
        out |= bit;
    }

    return out;
}

bool fbwl_round_corners_point_visible(uint32_t mask, int frame_w, int frame_h, int x, int y) {
    if (mask == 0) {
        return true;
    }
    if (frame_w < 8 || frame_h < 8) {
        return true;
    }
    if (x < 0 || y < 0 || x >= frame_w || y >= frame_h) {
        return true;
    }

    if ((mask & FBWL_ROUND_CORNERS_TOPLEFT) != 0 && x < 8 && y < 8) {
        return (tl_keep[y] & (uint8_t)(1u << x)) != 0;
    }
    if ((mask & FBWL_ROUND_CORNERS_TOPRIGHT) != 0 && x >= frame_w - 8 && y < 8) {
        const int bx = x - (frame_w - 8);
        return (tr_keep[y] & (uint8_t)(1u << bx)) != 0;
    }
    if ((mask & FBWL_ROUND_CORNERS_BOTTOMLEFT) != 0 && x < 8 && y >= frame_h - 8) {
        const int by = y - (frame_h - 8);
        return (bl_keep[by] & (uint8_t)(1u << x)) != 0;
    }
    if ((mask & FBWL_ROUND_CORNERS_BOTTOMRIGHT) != 0 && x >= frame_w - 8 && y >= frame_h - 8) {
        const int bx = x - (frame_w - 8);
        const int by = y - (frame_h - 8);
        return (br_keep[by] & (uint8_t)(1u << bx)) != 0;
    }

    return true;
}

static void apply_corner(cairo_surface_t *surface,
        const uint8_t keep[static 8],
        int block_x,
        int block_y,
        int offset_x,
        int offset_y,
        int surf_w,
        int surf_h) {
    if (surface == NULL) {
        return;
    }
    if (surf_w < 1 || surf_h < 1) {
        return;
    }

    const int rect_x = offset_x;
    const int rect_y = offset_y;
    const int rect_w = surf_w;
    const int rect_h = surf_h;
    if (!rects_intersect(rect_x, rect_y, rect_w, rect_h, block_x, block_y, 8, 8)) {
        return;
    }

    cairo_surface_flush(surface);
    uint8_t *data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    if (data == NULL || stride <= 0) {
        return;
    }

    const int ix1 = rect_x > block_x ? rect_x : block_x;
    const int iy1 = rect_y > block_y ? rect_y : block_y;
    const int ix2 = (rect_x + rect_w) < (block_x + 8) ? (rect_x + rect_w) : (block_x + 8);
    const int iy2 = (rect_y + rect_h) < (block_y + 8) ? (rect_y + rect_h) : (block_y + 8);

    for (int gy = iy1; gy < iy2; gy++) {
        const int by = gy - block_y;
        const int y = gy - offset_y;
        if (y < 0 || y >= surf_h || by < 0 || by >= 8) {
            continue;
        }
        uint32_t *row = (uint32_t *)(data + (size_t)y * (size_t)stride);
        for (int gx = ix1; gx < ix2; gx++) {
            const int bx = gx - block_x;
            const int x = gx - offset_x;
            if (x < 0 || x >= surf_w || bx < 0 || bx >= 8) {
                continue;
            }
            if ((keep[by] & (uint8_t)(1u << bx)) == 0) {
                row[x] = 0x00000000u;
            }
        }
    }

    cairo_surface_mark_dirty(surface);
}

void fbwl_round_corners_apply_to_cairo_surface(cairo_surface_t *surface,
        int offset_x, int offset_y, int frame_w, int frame_h, uint32_t mask) {
    if (surface == NULL) {
        return;
    }
    if (mask == 0) {
        return;
    }
    if (frame_w < 8 || frame_h < 8) {
        return;
    }
    if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE) {
        return;
    }
    if (cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32) {
        return;
    }

    const int surf_w = cairo_image_surface_get_width(surface);
    const int surf_h = cairo_image_surface_get_height(surface);
    if (surf_w < 1 || surf_h < 1) {
        return;
    }

    if ((mask & FBWL_ROUND_CORNERS_TOPLEFT) != 0) {
        apply_corner(surface, tl_keep, 0, 0, offset_x, offset_y, surf_w, surf_h);
    }
    if ((mask & FBWL_ROUND_CORNERS_TOPRIGHT) != 0) {
        apply_corner(surface, tr_keep, frame_w - 8, 0, offset_x, offset_y, surf_w, surf_h);
    }
    if ((mask & FBWL_ROUND_CORNERS_BOTTOMLEFT) != 0) {
        apply_corner(surface, bl_keep, 0, frame_h - 8, offset_x, offset_y, surf_w, surf_h);
    }
    if ((mask & FBWL_ROUND_CORNERS_BOTTOMRIGHT) != 0) {
        apply_corner(surface, br_keep, frame_w - 8, frame_h - 8, offset_x, offset_y, surf_w, surf_h);
    }
}

static bool mask_needed(int offset_x, int offset_y, int w, int h, int frame_w, int frame_h, uint32_t mask) {
    if (mask == 0) {
        return false;
    }
    if (frame_w < 8 || frame_h < 8) {
        return false;
    }
    if (w < 1 || h < 1) {
        return false;
    }

    if ((mask & FBWL_ROUND_CORNERS_TOPLEFT) != 0 &&
            rects_intersect(offset_x, offset_y, w, h, 0, 0, 8, 8)) {
        return true;
    }
    if ((mask & FBWL_ROUND_CORNERS_TOPRIGHT) != 0 &&
            rects_intersect(offset_x, offset_y, w, h, frame_w - 8, 0, 8, 8)) {
        return true;
    }
    if ((mask & FBWL_ROUND_CORNERS_BOTTOMLEFT) != 0 &&
            rects_intersect(offset_x, offset_y, w, h, 0, frame_h - 8, 8, 8)) {
        return true;
    }
    if ((mask & FBWL_ROUND_CORNERS_BOTTOMRIGHT) != 0 &&
            rects_intersect(offset_x, offset_y, w, h, frame_w - 8, frame_h - 8, 8, 8)) {
        return true;
    }
    return false;
}

struct wlr_buffer *fbwl_round_corners_mask_buffer_owned(struct wlr_buffer *src,
        int offset_x, int offset_y, int frame_w, int frame_h, uint32_t mask) {
    if (src == NULL) {
        return NULL;
    }
    if (mask == 0) {
        return src;
    }
    if (!mask_needed(offset_x, offset_y, src->width, src->height, frame_w, frame_h, mask)) {
        return src;
    }

    void *data = NULL;
    uint32_t format = 0;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(src, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
        return src;
    }
    if (data == NULL || stride == 0 || format != DRM_FORMAT_ARGB8888) {
        wlr_buffer_end_data_ptr_access(src);
        return src;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, src->width, src->height);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        wlr_buffer_end_data_ptr_access(src);
        return src;
    }

    const int dst_stride = cairo_image_surface_get_stride(surface);
    uint8_t *dst = cairo_image_surface_get_data(surface);
    if (dst == NULL || dst_stride <= 0) {
        cairo_surface_destroy(surface);
        wlr_buffer_end_data_ptr_access(src);
        return src;
    }

    const uint8_t *src_bytes = data;
    const size_t row_bytes = (size_t)src->width * 4u;
    for (int y = 0; y < src->height; y++) {
        const uint8_t *srow = src_bytes + (size_t)y * stride;
        uint8_t *drow = dst + (size_t)y * (size_t)dst_stride;
        memcpy(drow, srow, row_bytes);
    }
    cairo_surface_mark_dirty(surface);

    fbwl_round_corners_apply_to_cairo_surface(surface, offset_x, offset_y, frame_w, frame_h, mask);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        wlr_buffer_end_data_ptr_access(src);
        return src;
    }

    wlr_buffer_end_data_ptr_access(src);
    wlr_buffer_drop(src);
    return buf;
}

