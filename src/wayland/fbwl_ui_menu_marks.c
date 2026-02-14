#include "wayland/fbwl_ui_menu_marks.h"

#include <stdbool.h>
#include <stddef.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "wayland/fbwl_ui_decor_icons.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_menu.h"
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

static struct wlr_buffer *menu_mark_render_square_outline(int size_px, const float rgba[static 4]) {
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
    const double lw = clampd(px / 10.0, 1.0, 2.0);
    cairo_set_line_width(cr, lw);
    cairo_set_rgba_f(cr, rgba);

    const double q = px / 4.0;
    const double half = px / 2.0;
    cairo_rectangle(cr, q, q, half, half);
    cairo_stroke(cr);

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

static struct wlr_buffer *menu_mark_render_triangle(int size_px, const float rgba[static 4], bool point_right) {
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
    cairo_set_rgba_f(cr, rgba);

    // Match Fluxbox's drawTriangle(..., scale=300) behavior (33% triangle centered in the square).
    const unsigned int width = (unsigned int)size_px;
    const unsigned int height = (unsigned int)size_px;
    const int scale = 300;
    unsigned int ax = 100u * width / (unsigned int)scale;
    unsigned int ay = 100u * height / (unsigned int)scale;
    if ((ax % 2u) == 1u) {
        ax--;
    }
    if ((ay % 2u) == 1u) {
        ay--;
    }
    if (ax < 2 || ay < 2) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return NULL;
    }

    const double mid_x = (double)width / 2.0;
    const double mid_y = (double)height / 2.0;
    const double dax = (double)ax;
    const double day = (double)ay;

    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    if (point_right) {
        x0 = mid_x + dax / 2.0;
        y0 = mid_y;
        x1 = x0 - dax;
        y1 = y0 + day / 2.0;
        x2 = x1;
        y2 = y1 - day;
    } else {
        x0 = mid_x - dax / 2.0;
        y0 = mid_y;
        x1 = x0 + dax;
        y1 = y0 - day / 2.0;
        x2 = x1;
        y2 = y1 + day;
    }

    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

static struct wlr_buffer *menu_mark_render_diamond(int size_px, const float rgba[static 4]) {
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
    cairo_set_rgba_f(cr, rgba);

    const double px = (double)size_px;
    const double mid = px / 2.0;
    const double r = mid > 3.0 ? 3.0 : mid;
    cairo_move_to(cr, mid, mid - r);
    cairo_line_to(cr, mid + r, mid);
    cairo_line_to(cr, mid, mid + r);
    cairo_line_to(cr, mid - r, mid);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

static struct wlr_buffer *menu_mark_render_selected_fallback(int size_px, const float rgba[static 4]) {
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
    cairo_set_rgba_f(cr, rgba);

    const double px = (double)size_px;
    const double q = px / 4.0;
    const double half = px / 2.0;
    cairo_rectangle(cr, q, q, half, half);
    cairo_fill(cr);

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

static struct wlr_buffer *menu_mark_buffer_create(const struct fbwl_decor_theme *theme, const struct fbwl_menu_item *it,
        bool highlighted, int size_px) {
    if (theme == NULL || it == NULL || size_px < 1) {
        return NULL;
    }

    if (it->toggle) {
        if (it->selected) {
            const char *p = NULL;
            if (highlighted && theme->menu_hilite_selected_pixmap[0] != '\0') {
                p = theme->menu_hilite_selected_pixmap;
            } else if (theme->menu_selected_pixmap[0] != '\0') {
                p = theme->menu_selected_pixmap;
            }
            if (p != NULL && *p != '\0') {
                return fbwl_decor_icon_render_pixmap(p, size_px);
            }
            return menu_mark_render_selected_fallback(size_px, theme->menu_hilite_tex.color);
        }

        const char *p = NULL;
        if (highlighted && theme->menu_hilite_unselected_pixmap[0] != '\0') {
            p = theme->menu_hilite_unselected_pixmap;
        } else if (theme->menu_unselected_pixmap[0] != '\0') {
            p = theme->menu_unselected_pixmap;
        }
        if (p != NULL && *p != '\0') {
            return fbwl_decor_icon_render_pixmap(p, size_px);
        }
        return NULL;
    }

    if (it->kind == FBWL_MENU_ITEM_SUBMENU) {
        const char *p = NULL;
        if (highlighted && theme->menu_hilite_submenu_pixmap[0] != '\0') {
            p = theme->menu_hilite_submenu_pixmap;
        } else if (theme->menu_submenu_pixmap[0] != '\0') {
            p = theme->menu_submenu_pixmap;
        }
        if (p != NULL && *p != '\0') {
            return fbwl_decor_icon_render_pixmap(p, size_px);
        }

        if (theme->menu_bullet == 0) {
            return NULL;
        }

        const float *c = highlighted ? theme->menu_hilite_text : theme->menu_text;
        const bool point_right = theme->menu_bullet_pos == 2;
        switch (theme->menu_bullet) {
        case 1:
            return menu_mark_render_square_outline(size_px, c);
        case 2:
            return menu_mark_render_triangle(size_px, c, point_right);
        case 3:
            return menu_mark_render_diamond(size_px, c);
        default:
            return NULL;
        }
    }

    return NULL;
}

void fbwl_ui_menu_update_item_mark(struct fbwl_menu_ui *ui, size_t idx) {
    if (ui == NULL || !ui->open || ui->current == NULL || ui->env.decor_theme == NULL) {
        return;
    }
    if (ui->item_marks == NULL || idx >= ui->item_mark_count || idx >= ui->current->item_count) {
        return;
    }
    struct wlr_scene_buffer *sb = ui->item_marks[idx];
    if (sb == NULL) {
        return;
    }

    const struct fbwl_menu_item *it = &ui->current->items[idx];
    if (it->kind == FBWL_MENU_ITEM_SEPARATOR) {
        wlr_scene_node_set_enabled(&sb->node, false);
        return;
    }

    const bool highlighted = idx == ui->selected;
    const int size_px = ui->item_h > 0 ? ui->item_h : 1;
    struct wlr_buffer *buf = menu_mark_buffer_create(ui->env.decor_theme, it, highlighted, size_px);
    wlr_scene_buffer_set_buffer(sb, buf);
    if (buf != NULL) {
        wlr_buffer_drop(buf);
    }
    wlr_scene_node_set_enabled(&sb->node, buf != NULL);
}

