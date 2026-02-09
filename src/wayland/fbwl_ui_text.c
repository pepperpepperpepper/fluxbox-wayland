#include "wayland/fbwl_ui_text.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static void normalize_font_spec(const char *in, char *out, size_t out_size) {
    if (out == NULL || out_size < 1) {
        return;
    }
    out[0] = '\0';

    if (in == NULL) {
        return;
    }

    while (*in != '\0' && isspace((unsigned char)*in)) {
        in++;
    }
    if (*in == '\0') {
        return;
    }

    size_t len = strlen(in);
    while (len > 0 && isspace((unsigned char)in[len - 1])) {
        len--;
    }
    if (len < 1) {
        return;
    }

    if (len >= 2 && ((in[0] == '"' && in[len - 1] == '"') || (in[0] == '\'' && in[len - 1] == '\''))) {
        in++;
        len -= 2;
    }
    if (len < 1) {
        return;
    }

    if (in[0] == '-') {
        size_t n = len < out_size - 1 ? len : out_size - 1;
        memcpy(out, in, n);
        out[n] = '\0';
        return;
    }

    size_t j = 0;
    bool prev_space = false;
    for (size_t i = 0; i < len; i++) {
        char ch = in[i];
        if (ch == ':') {
            ch = ' ';
        } else if (ch == '-' && i + 1 < len && isdigit((unsigned char)in[i + 1])) {
            ch = ' ';
        }
        if (isspace((unsigned char)ch)) {
            if (prev_space) {
                continue;
            }
            ch = ' ';
            prev_space = true;
        } else {
            prev_space = false;
        }
        if (j + 1 >= out_size) {
            break;
        }
        out[j++] = ch;
    }
    while (j > 0 && out[j - 1] == ' ') {
        j--;
    }
    out[j] = '\0';
}

struct wlr_buffer *fbwl_text_buffer_create(const char *text, int width, int height,
        int pad_x, const float rgba[static 4], const char *font) {
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
    const char *font_in = font != NULL && *font != '\0' ? font : "Sans";
    char font_norm[256];
    normalize_font_spec(font_in, font_norm, sizeof(font_norm));
    const char *font_use = font_norm[0] != '\0' ? font_norm : font_in;
    PangoFontDescription *desc = pango_font_description_from_string(font_use);
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

bool fbwl_text_measure(const char *text, int height, const char *font, int *out_w, int *out_h) {
    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (height < 1) {
        return false;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return false;
    }

    cairo_t *cr = cairo_create(surface);
    if (cr == NULL || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        if (cr != NULL) {
            cairo_destroy(cr);
        }
        cairo_surface_destroy(surface);
        return false;
    }

    PangoLayout *layout = pango_cairo_create_layout(cr);
    if (layout == NULL) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return false;
    }

    const int font_px = height >= 18 ? height - 8 : height;
    const char *font_in = font != NULL && *font != '\0' ? font : "Sans";
    char font_norm[256];
    normalize_font_spec(font_in, font_norm, sizeof(font_norm));
    const char *font_use = font_norm[0] != '\0' ? font_norm : font_in;
    PangoFontDescription *desc = pango_font_description_from_string(font_use);
    if (desc != NULL) {
        pango_font_description_set_absolute_size(desc, font_px * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
    }

    pango_layout_set_text(layout, text != NULL ? text : "", -1);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

    int text_w = 0;
    int text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    if (desc != NULL) {
        pango_font_description_free(desc);
    }
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    if (out_w != NULL) {
        *out_w = text_w;
    }
    if (out_h != NULL) {
        *out_h = text_h;
    }

    return true;
}

bool fbwl_text_fits(const char *text, int width, int height, int pad_x, const char *font) {
    if (width < 1 || height < 1) {
        return false;
    }

    int text_w = 0;
    if (!fbwl_text_measure(text, height, font, &text_w, NULL)) {
        return true;
    }

    int avail = width - 2 * (pad_x > 0 ? pad_x : 0);
    if (avail < 0) {
        avail = 0;
    }
    return text_w <= avail;
}
