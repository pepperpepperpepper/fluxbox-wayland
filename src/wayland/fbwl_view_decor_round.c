#include "wayland/fbwl_view_decor_internal.h"

#include <drm_fourcc.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

#include "wayland/fbwl_deco_mask.h"
#include "wayland/fbwl_output.h"
#include "wayland/fbwl_round_corners.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"

bool fbwl_view_decor_round_frame_geom(const struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        uint32_t *out_round_mask, int *out_frame_x, int *out_frame_y, int *out_frame_w, int *out_frame_h) {
    if (out_round_mask != NULL) {
        *out_round_mask = 0;
    }
    if (out_frame_x != NULL) {
        *out_frame_x = 0;
    }
    if (out_frame_y != NULL) {
        *out_frame_y = 0;
    }
    if (out_frame_w != NULL) {
        *out_frame_w = 0;
    }
    if (out_frame_h != NULL) {
        *out_frame_h = 0;
    }
    if (view == NULL || theme == NULL) {
        return false;
    }
    if (!view->decor_enabled || view->fullscreen) {
        return false;
    }
    if (!fbwl_deco_mask_has_frame(view->decor_mask)) {
        return false;
    }
    const uint32_t round_mask = theme->window_round_corners;
    if (round_mask == 0) {
        return false;
    }

    const int w = fbwl_view_current_width(view);
    int h = fbwl_view_current_height(view);
    if (w < 1 || (!view->shaded && h < 1)) {
        return false;
    }
    if (view->shaded) {
        h = 0;
    }

    const uint32_t mask = view->decor_mask;
    int border_px = theme->border_width;
    if (border_px < 0) {
        border_px = 0;
    }
    int handle_h_px = theme->handle_width;
    if (handle_h_px < 0) {
        handle_h_px = 0;
    }
    int title_h_px = theme->title_height;
    if (title_h_px < 0) {
        title_h_px = 0;
    }

    const bool titlebar_on = (mask & FBWL_DECORM_TITLEBAR) != 0;
    const bool border_on = (mask & FBWL_DECORM_BORDER) != 0;
    const bool handle_on = (mask & FBWL_DECORM_HANDLE) != 0 && !view->shaded;

    const bool show_titlebar = titlebar_on && title_h_px > 0;
    const bool show_border = border_on && border_px > 0;
    const bool show_handle = handle_on && handle_h_px > 0;
    const int frame_title_h = show_titlebar ? title_h_px : 0;
    const int border_bottom_h = show_border ? (show_handle ? (handle_h_px + 2 * border_px) : border_px) : 0;

    const int frame_x = show_border ? -border_px : 0;
    const int frame_y = -frame_title_h - (show_border ? border_px : 0);
    const int frame_w = w + (show_border ? 2 * border_px : 0);
    const int frame_bottom = show_border ? border_bottom_h : (show_handle ? handle_h_px : 0);
    const int frame_h = frame_title_h + (show_border ? border_px : 0) + h + frame_bottom;
    if (frame_w < 1 || frame_h < 1) {
        return false;
    }

    if (out_round_mask != NULL) {
        *out_round_mask = round_mask;
    }
    if (out_frame_x != NULL) {
        *out_frame_x = frame_x;
    }
    if (out_frame_y != NULL) {
        *out_frame_y = frame_y;
    }
    if (out_frame_w != NULL) {
        *out_frame_w = frame_w;
    }
    if (out_frame_h != NULL) {
        *out_frame_h = frame_h;
    }
    return true;
}

bool fbwl_view_decor_buffer_accepts_input_round_corners(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    if (buffer == NULL || sx == NULL || sy == NULL) {
        return false;
    }

    struct wlr_scene_node *walk = &buffer->node;
    while (walk != NULL && walk->data == NULL) {
        walk = walk->parent != NULL ? &walk->parent->node : NULL;
    }
    struct fbwl_view *view = walk != NULL ? walk->data : NULL;
    if (view == NULL || view->server == NULL) {
        return true;
    }

    const struct fbwl_decor_theme *theme = &view->server->decor_theme;
    uint32_t round_mask = 0;
    int frame_x = 0, frame_y = 0, frame_w = 0, frame_h = 0;
    if (!fbwl_view_decor_round_frame_geom(view, theme, &round_mask, &frame_x, &frame_y, &frame_w, &frame_h)) {
        return true;
    }

    int node_x = 0;
    int node_y = 0;
    if (!wlr_scene_node_coords(&buffer->node, &node_x, &node_y)) {
        node_x = 0;
        node_y = 0;
    }

    const int px = node_x + (int)(*sx);
    const int py = node_y + (int)(*sy);
    const int fx = px - (view->x + frame_x);
    const int fy = py - (view->y + frame_y);
    return fbwl_round_corners_point_visible(round_mask, frame_w, frame_h, fx, fy);
}

struct wlr_buffer *fbwl_view_decor_solid_color_buffer_masked(int width, int height, const float rgba[static 4],
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

static bool wallpaper_compute_src_box(enum fbwl_wallpaper_mode wallpaper_mode,
        struct wlr_output_layout *output_layout,
        struct wlr_buffer *wallpaper_buf, int global_x, int global_y, int width, int height,
        struct wlr_fbox *out_src_box) {
    if (output_layout == NULL || wallpaper_buf == NULL || out_src_box == NULL || width < 1 || height < 1) {
        return false;
    }

    const double cx = (double)global_x + (double)width / 2.0;
    const double cy = (double)global_y + (double)height / 2.0;
    struct wlr_output *output = wlr_output_layout_output_at(output_layout, cx, cy);
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }
    if (output == NULL) {
        return false;
    }

    struct wlr_box output_box = {0};
    wlr_output_layout_get_box(output_layout, output, &output_box);
    if (output_box.width < 1 || output_box.height < 1) {
        return false;
    }

    const double buf_w = (double)wallpaper_buf->width;
    const double buf_h = (double)wallpaper_buf->height;
    if (buf_w <= 0.0 || buf_h <= 0.0) {
        return false;
    }

    double base_x = 0.0;
    double base_y = 0.0;
    double base_w = buf_w;
    double base_h = buf_h;
    if (wallpaper_mode == FBWL_WALLPAPER_MODE_FILL) {
        const double out_aspect = (double)output_box.width / (double)output_box.height;
        const double buf_aspect = buf_w / buf_h;
        if (buf_aspect > out_aspect) {
            const double crop_w = buf_h * out_aspect;
            if (crop_w > 0.0 && crop_w < buf_w) {
                base_w = crop_w;
                base_x = (buf_w - crop_w) / 2.0;
            }
        } else if (buf_aspect < out_aspect) {
            const double crop_h = buf_w / out_aspect;
            if (crop_h > 0.0 && crop_h < buf_h) {
                base_h = crop_h;
                base_y = (buf_h - crop_h) / 2.0;
            }
        }
    }
    if (base_w <= 0.0 || base_h <= 0.0) {
        return false;
    }

    double sx = base_x + ((double)global_x - (double)output_box.x) / (double)output_box.width * base_w;
    double sy = base_y + ((double)global_y - (double)output_box.y) / (double)output_box.height * base_h;
    double sw = (double)width / (double)output_box.width * base_w;
    double sh = (double)height / (double)output_box.height * base_h;
    if (sw <= 0.0 || sh <= 0.0) {
        return false;
    }

    const double min_x = base_x;
    const double min_y = base_y;
    const double max_x = base_x + base_w;
    const double max_y = base_y + base_h;

    if (sx < min_x) {
        sw -= (min_x - sx);
        sx = min_x;
    }
    if (sy < min_y) {
        sh -= (min_y - sy);
        sy = min_y;
    }
    if (sx + sw > max_x) {
        sw = max_x - sx;
    }
    if (sy + sh > max_y) {
        sh = max_y - sy;
    }
    if (sw <= 0.0 || sh <= 0.0) {
        return false;
    }

    *out_src_box = (struct wlr_fbox){
        .x = sx,
        .y = sy,
        .width = sw,
        .height = sh,
    };
    return true;
}

struct wlr_buffer *fbwl_view_decor_wallpaper_region_buffer_masked(struct fbwl_server *server,
        int global_x, int global_y, int width, int height,
        uint32_t round_mask, int offset_x, int offset_y, int frame_w, int frame_h) {
    if (width < 1 || height < 1) {
        return NULL;
    }

    const float *background_color = server != NULL ? server->background_color : NULL;
    const enum fbwl_wallpaper_mode wallpaper_mode =
        server != NULL ? server->wallpaper_mode : FBWL_WALLPAPER_MODE_STRETCH;
    struct wlr_output_layout *output_layout = server != NULL ? server->output_layout : NULL;
    struct wlr_buffer *wallpaper_buf = server != NULL ? server->wallpaper_buf : NULL;

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

    float bg[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (background_color != NULL) {
        bg[0] = background_color[0];
        bg[1] = background_color[1];
        bg[2] = background_color[2];
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], 1.0);
    cairo_paint(cr);

    struct wlr_buffer *use_wallpaper_buf = wallpaper_buf;
    if (wallpaper_mode == FBWL_WALLPAPER_MODE_TILE && wallpaper_buf != NULL && output_layout != NULL) {
        const double cx = (double)global_x + (double)width / 2.0;
        const double cy = (double)global_y + (double)height / 2.0;
        struct wlr_output *wlr_output = wlr_output_layout_output_at(output_layout, cx, cy);
        if (wlr_output == NULL) {
            wlr_output = wlr_output_layout_get_center_output(output_layout);
        }
        struct fbwl_output *out = wlr_output != NULL ? wlr_output->data : NULL;
        if (out != NULL && out->wallpaper_tile_buf != NULL) {
            use_wallpaper_buf = out->wallpaper_tile_buf;
        }
    }

    void *src_data = NULL;
    uint32_t src_format = 0;
    size_t src_stride = 0;
    if (use_wallpaper_buf != NULL && output_layout != NULL &&
            wlr_buffer_begin_data_ptr_access(use_wallpaper_buf, WLR_BUFFER_DATA_PTR_ACCESS_READ,
                &src_data, &src_format, &src_stride) &&
            src_data != NULL && src_stride > 0 && src_format == DRM_FORMAT_ARGB8888) {
        cairo_surface_t *src_surface = cairo_image_surface_create_for_data((unsigned char *)src_data,
            CAIRO_FORMAT_ARGB32, use_wallpaper_buf->width, use_wallpaper_buf->height, (int)src_stride);

        if (src_surface != NULL && cairo_surface_status(src_surface) == CAIRO_STATUS_SUCCESS) {
            if (wallpaper_mode == FBWL_WALLPAPER_MODE_CENTER) {
                const double cx = (double)global_x + (double)width / 2.0;
                const double cy = (double)global_y + (double)height / 2.0;
                struct wlr_output *output = wlr_output_layout_output_at(output_layout, cx, cy);
                if (output == NULL) {
                    output = wlr_output_layout_get_center_output(output_layout);
                }
                struct wlr_box output_box = {0};
                if (output != NULL) {
                    wlr_output_layout_get_box(output_layout, output, &output_box);
                }

                const int img_w = use_wallpaper_buf->width;
                const int img_h = use_wallpaper_buf->height;
                const int img_x = output_box.x + (output_box.width - img_w) / 2;
                const int img_y = output_box.y + (output_box.height - img_h) / 2;

                const int reg_x1 = global_x;
                const int reg_y1 = global_y;
                const int reg_x2 = global_x + width;
                const int reg_y2 = global_y + height;
                const int img_x2 = img_x + img_w;
                const int img_y2 = img_y + img_h;

                const int ix1 = reg_x1 > img_x ? reg_x1 : img_x;
                const int iy1 = reg_y1 > img_y ? reg_y1 : img_y;
                const int ix2 = reg_x2 < img_x2 ? reg_x2 : img_x2;
                const int iy2 = reg_y2 < img_y2 ? reg_y2 : img_y2;

                const int iw = ix2 - ix1;
                const int ih = iy2 - iy1;
                if (iw > 0 && ih > 0) {
                    const int dx = ix1 - global_x;
                    const int dy = iy1 - global_y;
                    const int src_x0 = ix1 - img_x;
                    const int src_y0 = iy1 - img_y;
                    cairo_save(cr);
                    cairo_rectangle(cr, (double)dx, (double)dy, (double)iw, (double)ih);
                    cairo_clip(cr);
                    cairo_set_source_surface(cr, src_surface, (double)dx - (double)src_x0, (double)dy - (double)src_y0);
                    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
                    cairo_paint(cr);
                    cairo_restore(cr);
                }
            } else {
                struct wlr_fbox src_box = {0};
                const struct wlr_fbox *use_src_box = NULL;
                if (wallpaper_compute_src_box(wallpaper_mode, output_layout, use_wallpaper_buf,
                        global_x, global_y, width, height, &src_box)) {
                    use_src_box = &src_box;
                }

                cairo_pattern_t *pat = cairo_pattern_create_for_surface(src_surface);
                cairo_pattern_set_filter(pat, CAIRO_FILTER_BILINEAR);
                if (use_src_box != NULL) {
                    cairo_matrix_t m = {
                        .xx = use_src_box->width / (double)width,
                        .yx = 0.0,
                        .xy = 0.0,
                        .yy = use_src_box->height / (double)height,
                        .x0 = use_src_box->x,
                        .y0 = use_src_box->y,
                    };
                    cairo_pattern_set_matrix(pat, &m);
                }
                cairo_set_source(cr, pat);
                cairo_pattern_destroy(pat);
                cairo_paint(cr);
            }
        }

        if (src_surface != NULL) {
            cairo_surface_destroy(src_surface);
        }
        wlr_buffer_end_data_ptr_access(use_wallpaper_buf);
    }

    cairo_destroy(cr);

    fbwl_round_corners_apply_to_cairo_surface(surface, offset_x, offset_y, frame_w, frame_h, round_mask);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

