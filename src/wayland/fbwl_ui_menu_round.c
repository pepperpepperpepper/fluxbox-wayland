#include "wayland/fbwl_ui_menu_round.h"

#include <stdbool.h>

#include <cairo/cairo.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "wayland/fbwl_round_corners.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_menu.h"
#include "wayland/fbwl_ui_text.h"

bool fbwl_ui_menu_contains_point(const struct fbwl_menu_ui *ui, int lx, int ly) {
    if (ui == NULL || !ui->open || ui->current == NULL) {
        return false;
    }

    const int x = lx - ui->x;
    const int y = ly - ui->y;
    const int bw = ui->border_w > 0 ? ui->border_w : 0;
    const int item_h = ui->item_h > 0 ? ui->item_h : 1;
    const int w = ui->width > 0 ? ui->width : 1;
    const int count = ui->current != NULL ? (int)ui->current->item_count : 0;
    const int title_h = ui->title_h > 0 ? ui->title_h : 0;
    const int h = title_h + (count > 0 ? count * item_h : item_h);
    const int outer_w = w + 2 * bw;
    const int outer_h = h + 2 * bw;
    if (x < 0 || x >= outer_w || y < 0 || y >= outer_h) {
        return false;
    }

    const uint32_t round_mask = fbwl_ui_menu_round_mask(ui);
    if (round_mask != 0 && !fbwl_round_corners_point_visible(round_mask, outer_w, outer_h, x, y)) {
        return false;
    }
    return true;
}

uint32_t fbwl_ui_menu_round_mask(const struct fbwl_menu_ui *ui) {
    if (ui == NULL || ui->env.decor_theme == NULL) {
        return 0;
    }
    return ui->env.decor_theme->menu_round_corners;
}

void fbwl_ui_menu_outer_size(const struct fbwl_menu_ui *ui, int *out_w, int *out_h) {
    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (ui == NULL || ui->current == NULL) {
        return;
    }

    const int bw = ui->border_w > 0 ? ui->border_w : 0;
    const int item_h = ui->item_h > 0 ? ui->item_h : 1;
    const int w = ui->width > 0 ? ui->width : 1;
    const int count = (int)ui->current->item_count;
    const int title_h = ui->title_h > 0 ? ui->title_h : 0;
    const int h = title_h + (count > 0 ? count * item_h : item_h);

    if (out_w != NULL) {
        *out_w = w + 2 * bw;
    }
    if (out_h != NULL) {
        *out_h = h + 2 * bw;
    }
}

struct wlr_buffer *fbwl_ui_menu_solid_color_buffer_masked(int width, int height, const float rgba[static 4],
        int offset_x, int offset_y, int frame_w, int frame_h, uint32_t round_mask) {
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

void fbwl_ui_menu_update_highlight(struct fbwl_menu_ui *ui) {
    if (ui == NULL || !ui->open || ui->current == NULL || ui->env.decor_theme == NULL || ui->highlight == NULL) {
        return;
    }

    const struct fbwl_decor_theme *theme = ui->env.decor_theme;
    const bool hilite_parentrel = fbwl_texture_is_parentrelative(&theme->menu_hilite_tex);
    if (hilite_parentrel) {
        wlr_scene_node_set_enabled(&ui->highlight->node, false);
        wlr_scene_buffer_set_buffer(ui->highlight, NULL);
        return;
    }

    const int bw = ui->border_w > 0 ? ui->border_w : 0;
    const int item_h = ui->item_h > 0 ? ui->item_h : 1;
    const int w = ui->width > 0 ? ui->width : 1;
    const float alpha = (float)ui->alpha / 255.0f;

    struct wlr_buffer *buf = fbwl_texture_render_buffer(&theme->menu_hilite_tex, w, item_h);
    if (buf != NULL) {
        const uint32_t round_mask = fbwl_ui_menu_round_mask(ui);
        if (round_mask != 0) {
            int outer_w = 0;
            int outer_h = 0;
            fbwl_ui_menu_outer_size(ui, &outer_w, &outer_h);
            const int off_x = bw;
            const int off_y = bw + ui->title_h + (int)ui->selected * item_h;
            buf = fbwl_round_corners_mask_buffer_owned(buf, off_x, off_y, outer_w, outer_h, round_mask);
        }
    }

    wlr_scene_buffer_set_buffer(ui->highlight, buf);
    if (buf != NULL) {
        wlr_buffer_drop(buf);
    }
    wlr_scene_node_set_enabled(&ui->highlight->node, buf != NULL);
    wlr_scene_buffer_set_dest_size(ui->highlight, w, item_h);
    wlr_scene_buffer_set_opacity(ui->highlight, alpha);
}
