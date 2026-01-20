#include "wayland/fbwl_ui_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <drm_fourcc.h>

#include <cairo/cairo.h>
#include <glib-object.h>
#include <pango/pangocairo.h>

#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>

struct fbwl_cairo_buffer {
    struct wlr_buffer base;
    cairo_surface_t *surface;
};

static void fbwl_cairo_buffer_destroy(struct wlr_buffer *wlr_buffer) {
    struct fbwl_cairo_buffer *buf = wl_container_of(wlr_buffer, buf, base);
    if (buf->surface != NULL) {
        cairo_surface_destroy(buf->surface);
    }
    wlr_buffer_finish(&buf->base);
    free(buf);
}

static bool fbwl_cairo_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
        void **data, uint32_t *format, size_t *stride) {
    struct fbwl_cairo_buffer *buf = wl_container_of(wlr_buffer, buf, base);
    if (buf->surface == NULL || cairo_surface_status(buf->surface) != CAIRO_STATUS_SUCCESS) {
        return false;
    }

    if ((flags & WLR_BUFFER_DATA_PTR_ACCESS_READ) == 0) {
        return false;
    }

    cairo_surface_flush(buf->surface);

    if (data != NULL) {
        *data = cairo_image_surface_get_data(buf->surface);
    }
    if (format != NULL) {
        *format = DRM_FORMAT_ARGB8888;
    }
    if (stride != NULL) {
        *stride = (size_t)cairo_image_surface_get_stride(buf->surface);
    }

    return true;
}

static void fbwl_cairo_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
    (void)wlr_buffer;
}

static const struct wlr_buffer_impl fbwl_cairo_buffer_impl = {
    .destroy = fbwl_cairo_buffer_destroy,
    .begin_data_ptr_access = fbwl_cairo_buffer_begin_data_ptr_access,
    .end_data_ptr_access = fbwl_cairo_buffer_end_data_ptr_access,
};

struct wlr_buffer *fbwl_cairo_buffer_create(cairo_surface_t *surface) {
    if (surface == NULL) {
        return NULL;
    }

    struct fbwl_cairo_buffer *buf = calloc(1, sizeof(*buf));
    if (buf == NULL) {
        return NULL;
    }

    const int w = cairo_image_surface_get_width(surface);
    const int h = cairo_image_surface_get_height(surface);
    if (w < 1 || h < 1) {
        free(buf);
        return NULL;
    }

    buf->surface = surface;
    wlr_buffer_init(&buf->base, &fbwl_cairo_buffer_impl, w, h);
    return &buf->base;
}

struct wlr_buffer *fbwl_text_buffer_create(const char *text, int width, int height,
        int pad_x, const float rgba[static 4]) {
    if (width < 1 || height < 1 || rgba == NULL) {
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
        if (cr != NULL) {
            cairo_destroy(cr);
        }
        cairo_surface_destroy(surface);
        return NULL;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    if (layout == NULL) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return NULL;
    }

    const int font_px = height >= 18 ? height - 8 : height;
    PangoFontDescription *desc = pango_font_description_from_string("Sans");
    if (desc != NULL) {
        pango_font_description_set_absolute_size(desc, font_px * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
    }

    pango_layout_set_text(layout, text != NULL ? text : "", -1);
    const int layout_width = width - 2 * pad_x;
    if (layout_width > 0) {
        pango_layout_set_width(layout, layout_width * PANGO_SCALE);
    }
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    int text_w = 0;
    int text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    cairo_set_source_rgba(cr, rgba[0], rgba[1], rgba[2], rgba[3]);
    const int x = pad_x > 0 ? pad_x : 0;
    const int y = text_h < height ? (height - text_h) / 2 : 0;
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);

    if (desc != NULL) {
        pango_font_description_free(desc);
    }
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

