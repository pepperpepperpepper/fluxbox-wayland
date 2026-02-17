#include "wayland/fbwl_ui_toolbar_shape.h"

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "wayland/fbwl_round_corners.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_ui_toolbar.h"

uint32_t fbwl_ui_toolbar_shaped_round_mask(enum fbwl_toolbar_placement placement, const struct fbwl_decor_theme *theme) {
    if (theme == NULL || !theme->toolbar_shaped) {
        return 0;
    }

    switch (placement) {
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
        return FBWL_ROUND_CORNERS_BOTTOMRIGHT;
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
        return FBWL_ROUND_CORNERS_BOTTOMLEFT | FBWL_ROUND_CORNERS_BOTTOMRIGHT;
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        return FBWL_ROUND_CORNERS_BOTTOMLEFT;

    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
        return FBWL_ROUND_CORNERS_TOPRIGHT;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
        return FBWL_ROUND_CORNERS_TOPLEFT | FBWL_ROUND_CORNERS_TOPRIGHT;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        return FBWL_ROUND_CORNERS_TOPLEFT;

    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
        return FBWL_ROUND_CORNERS_BOTTOMRIGHT;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
        return FBWL_ROUND_CORNERS_TOPRIGHT | FBWL_ROUND_CORNERS_BOTTOMRIGHT;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
        return FBWL_ROUND_CORNERS_TOPRIGHT;

    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        return FBWL_ROUND_CORNERS_BOTTOMLEFT;
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
        return FBWL_ROUND_CORNERS_TOPLEFT | FBWL_ROUND_CORNERS_BOTTOMLEFT;
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
        return FBWL_ROUND_CORNERS_TOPLEFT;
    default:
        return 0;
    }
}

bool fbwl_ui_toolbar_shaped_point_visible(enum fbwl_toolbar_placement placement, const struct fbwl_decor_theme *theme,
        int frame_w, int frame_h, int x, int y) {
    const uint32_t mask = fbwl_ui_toolbar_shaped_round_mask(placement, theme);
    if (mask == 0) {
        return true;
    }
    return fbwl_round_corners_point_visible(mask, frame_w, frame_h, x, y);
}

struct wlr_buffer *fbwl_ui_toolbar_shaped_mask_buffer_owned(enum fbwl_toolbar_placement placement,
        const struct fbwl_decor_theme *theme,
        struct wlr_buffer *src,
        int offset_x, int offset_y, int frame_w, int frame_h) {
    const uint32_t mask = fbwl_ui_toolbar_shaped_round_mask(placement, theme);
    if (mask == 0) {
        return src;
    }
    return fbwl_round_corners_mask_buffer_owned(src, offset_x, offset_y, frame_w, frame_h, mask);
}

static struct wlr_buffer *solid_color_buffer_masked(int width, int height, const float rgba[static 4],
        uint32_t round_mask, int offset_x, int offset_y, int frame_w, int frame_h) {
    if (width < 1 || height < 1) {
        return NULL;
    }
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    cairo_t *cr = cairo_create(surface);
    if (cr == NULL || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        if (cr != NULL) {
            cairo_destroy(cr);
        }
        return NULL;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, rgba[0], rgba[1], rgba[2], rgba[3]);
    cairo_paint(cr);
    cairo_destroy(cr);

    fbwl_round_corners_apply_to_cairo_surface(surface, offset_x, offset_y, frame_w, frame_h, round_mask);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

void fbwl_ui_toolbar_build_border(struct wlr_scene_tree *tree, enum fbwl_toolbar_placement placement,
        const struct fbwl_decor_theme *theme,
        int width, int height, int border_w, float alpha) {
    if (tree == NULL || theme == NULL || width < 1 || height < 1 || border_w < 1) {
        return;
    }

    const uint32_t round_mask = fbwl_ui_toolbar_shaped_round_mask(placement, theme);

    float c[4] = {
        theme->toolbar_border_color[0],
        theme->toolbar_border_color[1],
        theme->toolbar_border_color[2],
        theme->toolbar_border_color[3] * alpha,
    };

    if (round_mask == 0) {
        struct wlr_scene_rect *top = wlr_scene_rect_create(tree, width, border_w, c);
        struct wlr_scene_rect *bottom = wlr_scene_rect_create(tree, width, border_w, c);
        struct wlr_scene_rect *left = wlr_scene_rect_create(tree, border_w, height - 2 * border_w, c);
        struct wlr_scene_rect *right = wlr_scene_rect_create(tree, border_w, height - 2 * border_w, c);
        if (top != NULL) {
            wlr_scene_node_set_position(&top->node, 0, 0);
        }
        if (bottom != NULL) {
            wlr_scene_node_set_position(&bottom->node, 0, height - border_w);
        }
        if (left != NULL) {
            wlr_scene_node_set_position(&left->node, 0, border_w);
        }
        if (right != NULL) {
            wlr_scene_node_set_position(&right->node, width - border_w, border_w);
        }
        return;
    }

    struct wlr_scene_buffer *top = wlr_scene_buffer_create(tree, NULL);
    struct wlr_scene_buffer *bottom = wlr_scene_buffer_create(tree, NULL);
    struct wlr_scene_buffer *left = wlr_scene_buffer_create(tree, NULL);
    struct wlr_scene_buffer *right = wlr_scene_buffer_create(tree, NULL);

    if (top != NULL) {
        struct wlr_buffer *buf = solid_color_buffer_masked(width, border_w, c, round_mask, 0, 0, width, height);
        wlr_scene_buffer_set_buffer(top, buf);
        if (buf != NULL) {
            wlr_buffer_drop(buf);
        }
        wlr_scene_buffer_set_dest_size(top, width, border_w);
        wlr_scene_node_set_position(&top->node, 0, 0);
    }

    if (bottom != NULL) {
        struct wlr_buffer *buf = solid_color_buffer_masked(width, border_w, c, round_mask, 0, height - border_w, width, height);
        wlr_scene_buffer_set_buffer(bottom, buf);
        if (buf != NULL) {
            wlr_buffer_drop(buf);
        }
        wlr_scene_buffer_set_dest_size(bottom, width, border_w);
        wlr_scene_node_set_position(&bottom->node, 0, height - border_w);
    }

    const int side_h = height - 2 * border_w;
    if (side_h > 0) {
        if (left != NULL) {
            struct wlr_buffer *buf = solid_color_buffer_masked(border_w, side_h, c, round_mask, 0, border_w, width, height);
            wlr_scene_buffer_set_buffer(left, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(left, border_w, side_h);
            wlr_scene_node_set_position(&left->node, 0, border_w);
        }

        if (right != NULL) {
            struct wlr_buffer *buf =
                solid_color_buffer_masked(border_w, side_h, c, round_mask, width - border_w, border_w, width, height);
            wlr_scene_buffer_set_buffer(right, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(right, border_w, side_h);
            wlr_scene_node_set_position(&right->node, width - border_w, border_w);
        }
    }
}

