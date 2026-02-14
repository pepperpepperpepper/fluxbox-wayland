#include "wayland/fbwl_ui_decor_icons.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <cairo/cairo.h>

#include <wlr/interfaces/wlr_buffer.h>

#include "wayland/fbwl_texture.h"
#include "wayland/fbwl_ui_text.h"

static void cairo_set_rgba_f(cairo_t *cr, const float rgba[static 4]) {
    if (cr == NULL || rgba == NULL) {
        return;
    }
    cairo_set_source_rgba(cr, rgba[0], rgba[1], rgba[2], rgba[3]);
}

static inline double clampd(double v, double lo, double hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

struct wlr_buffer *fbwl_decor_icon_render_builtin(enum fbwl_decor_hit_kind kind, bool toggled, int size_px,
        const float rgba[static 4]) {
    if (size_px < 1 || rgba == NULL) {
        return NULL;
    }
    if (size_px > 512) {
        size_px = 512;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size_px, size_px);
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

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    const double px = (double)size_px;
    const double lw = clampd(px / 10.0, 1.0, 3.0);
    const double pad = clampd(px * 0.22, 2.0, px / 2.0);
    const double mid = px / 2.0;

    cairo_set_line_width(cr, lw);
    cairo_set_rgba_f(cr, rgba);

    switch (kind) {
    case FBWL_DECOR_HIT_BTN_CLOSE: {
        cairo_move_to(cr, pad, pad);
        cairo_line_to(cr, px - pad, px - pad);
        cairo_move_to(cr, px - pad, pad);
        cairo_line_to(cr, pad, px - pad);
        cairo_stroke(cr);
        break;
    }
    case FBWL_DECOR_HIT_BTN_MAX: {
        const double w = px - 2.0 * pad;
        const double h = w;
        if (toggled) {
            // Simple "restore" look: two overlapping rectangles.
            cairo_rectangle(cr, pad + lw, pad, w - lw, h - lw);
            cairo_stroke(cr);
            cairo_rectangle(cr, pad, pad + lw, w - lw, h - lw);
            cairo_stroke(cr);
        } else {
            cairo_rectangle(cr, pad, pad, w, h);
            cairo_stroke(cr);
        }
        break;
    }
    case FBWL_DECOR_HIT_BTN_MIN: {
        const double y = px - pad;
        cairo_move_to(cr, pad, y);
        cairo_line_to(cr, px - pad, y);
        cairo_stroke(cr);
        break;
    }
    case FBWL_DECOR_HIT_BTN_MENU: {
        const double w = px - 2.0 * pad;
        const double x0 = pad;
        const double y0 = clampd(px * 0.32, 2.0, px - 2.0);
        const double y1 = clampd(px * 0.50, 2.0, px - 2.0);
        const double y2 = clampd(px * 0.68, 2.0, px - 2.0);
        cairo_move_to(cr, x0, y0);
        cairo_line_to(cr, x0 + w, y0);
        cairo_move_to(cr, x0, y1);
        cairo_line_to(cr, x0 + w, y1);
        cairo_move_to(cr, x0, y2);
        cairo_line_to(cr, x0 + w, y2);
        cairo_stroke(cr);
        break;
    }
    case FBWL_DECOR_HIT_BTN_SHADE: {
        const double x0 = pad;
        const double x1 = px - pad;
        const double y_tip = toggled ? (px - pad) : pad;
        const double y_base = toggled ? pad : (px - pad);
        cairo_move_to(cr, mid, y_tip);
        cairo_line_to(cr, x0, y_base);
        cairo_line_to(cr, x1, y_base);
        cairo_close_path(cr);
        cairo_stroke(cr);
        break;
    }
    case FBWL_DECOR_HIT_BTN_STICK: {
        const double r = clampd((px - 2.0 * pad) / 2.0, 2.0, px / 2.0);
        cairo_arc(cr, mid, mid, r, 0.0, 2.0 * 3.141592653589793);
        if (toggled) {
            cairo_fill(cr);
        } else {
            cairo_stroke(cr);
        }
        break;
    }
    case FBWL_DECOR_HIT_BTN_LHALF: {
        const double w = px - 2.0 * pad;
        const double h = w;
        cairo_rectangle(cr, pad, pad, w, h);
        cairo_stroke(cr);
        cairo_rectangle(cr, pad, pad, w / 2.0, h);
        cairo_fill(cr);
        break;
    }
    case FBWL_DECOR_HIT_BTN_RHALF: {
        const double w = px - 2.0 * pad;
        const double h = w;
        cairo_rectangle(cr, pad, pad, w, h);
        cairo_stroke(cr);
        cairo_rectangle(cr, pad + w / 2.0, pad, w / 2.0, h);
        cairo_fill(cr);
        break;
    }
    default:
        break;
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    return buf;
}

struct wlr_buffer *fbwl_decor_icon_render_pixmap(const char *pixmap_path, int size_px) {
    if (pixmap_path == NULL || *pixmap_path == '\0' || size_px < 1) {
        return NULL;
    }

    struct fbwl_texture tex = {0};
    fbwl_texture_init(&tex);
    tex.type = FBWL_TEXTURE_FLAT | FBWL_TEXTURE_SOLID;
    (void)snprintf(tex.pixmap, sizeof(tex.pixmap), "%s", pixmap_path);
    return fbwl_texture_render_buffer(&tex, size_px, size_px);
}
