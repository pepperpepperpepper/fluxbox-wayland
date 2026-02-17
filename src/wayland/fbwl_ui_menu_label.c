#include "wayland/fbwl_ui_menu_label.h"

#include "wayland/fbwl_round_corners.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_menu.h"
#include "wayland/fbwl_ui_menu_round.h"
#include "wayland/fbwl_ui_menu_search.h"
#include "wayland/fbwl_ui_text.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

static void fbwl_ui_menu_render_item_label_internal(struct fbwl_menu_ui *ui, const struct fbwl_menu_item *it,
        struct wlr_scene_buffer *sb, size_t idx) {
    if (ui == NULL || it == NULL || sb == NULL || ui->env.decor_theme == NULL) {
        return;
    }

    const char *render = it->label != NULL ? it->label : "(no-label)";

    const float alpha = (float)ui->alpha / 255.0f;
    float fg[4] = {ui->env.decor_theme->menu_text[0], ui->env.decor_theme->menu_text[1], ui->env.decor_theme->menu_text[2],
        ui->env.decor_theme->menu_text[3] * alpha};
    float hi_fg[4] = {ui->env.decor_theme->menu_hilite_text[0], ui->env.decor_theme->menu_hilite_text[1],
        ui->env.decor_theme->menu_hilite_text[2], ui->env.decor_theme->menu_hilite_text[3] * alpha};
    float dis_fg[4] = {ui->env.decor_theme->menu_disable_text[0], ui->env.decor_theme->menu_disable_text[1],
        ui->env.decor_theme->menu_disable_text[2], ui->env.decor_theme->menu_disable_text[3] * alpha};
    float underline_fg[4] = {ui->env.decor_theme->menu_underline_color[0], ui->env.decor_theme->menu_underline_color[1],
        ui->env.decor_theme->menu_underline_color[2], ui->env.decor_theme->menu_underline_color[3] * alpha};

    const float *use_fg = fg;
    const char *font = ui->env.decor_theme->menu_font;
    const struct fbwl_text_effect *effect = &ui->env.decor_theme->menu_frame_effect;
    int justify = ui->env.decor_theme->menu_frame_justify;
    if (it->kind == FBWL_MENU_ITEM_NOP) {
        use_fg = dis_fg;
    } else if (idx == ui->selected) {
        use_fg = hi_fg;
        font = ui->env.decor_theme->menu_hilite_font[0] != '\0' ? ui->env.decor_theme->menu_hilite_font : font;
        effect = &ui->env.decor_theme->menu_hilite_effect;
        justify = ui->env.decor_theme->menu_hilite_justify;
    }

    size_t match_start = 0;
    int underline_start = -1;
    int underline_len = 0;
    if (fbwl_ui_menu_search_get_match(ui, it, &match_start)) {
        underline_start = match_start <= (size_t)INT32_MAX ? (int)match_start : INT32_MAX;
        const size_t plen = strlen(ui->search_pattern);
        underline_len = plen <= (size_t)INT32_MAX ? (int)plen : INT32_MAX;
    }
    const char *underline_font =
        ui->env.decor_theme->menu_hilite_font[0] != '\0' ? ui->env.decor_theme->menu_hilite_font : ui->env.decor_theme->menu_font;
    const int underline_justify = ui->env.decor_theme->menu_hilite_justify;

    const int item_h = ui->item_h > 0 ? ui->item_h : 1;
    const int w = ui->width > 0 ? ui->width : 200;
    const int bw = ui->border_w > 0 ? ui->border_w : 0;
    const int title_h = ui->title_h > 0 ? ui->title_h : 0;
    const int bevel = ui->env.decor_theme->menu_bevel_width > 0 ? ui->env.decor_theme->menu_bevel_width : 0;
    const int left_reserve = bevel + item_h + 1;
    const int right_reserve = bevel + item_h;
    const int label_w = w - left_reserve - right_reserve > 1 ? w - left_reserve - right_reserve : 1;

    struct wlr_buffer *text_buf =
        fbwl_text_buffer_create_underlined(render, label_w, item_h, 0, use_fg, font, effect, justify,
            underline_fg, underline_font, underline_justify, underline_start, underline_len);
    if (text_buf == NULL) {
        return;
    }

    const uint32_t round_mask = fbwl_ui_menu_round_mask(ui);
    if (round_mask != 0) {
        int outer_w = 0;
        int outer_h = 0;
        fbwl_ui_menu_outer_size(ui, &outer_w, &outer_h);
        const int off_x = bw + left_reserve;
        const int off_y = bw + title_h + (int)idx * item_h;
        text_buf = fbwl_round_corners_mask_buffer_owned(text_buf, off_x, off_y, outer_w, outer_h, round_mask);
    }
    wlr_scene_buffer_set_buffer(sb, text_buf);
    wlr_buffer_drop(text_buf);
}

void fbwl_ui_menu_update_item_label(struct fbwl_menu_ui *ui, size_t idx) {
    if (ui == NULL || !ui->open || ui->current == NULL || ui->env.decor_theme == NULL) {
        return;
    }
    if (ui->item_labels == NULL || idx >= ui->item_label_count || idx >= ui->current->item_count) {
        return;
    }
    struct wlr_scene_buffer *sb = ui->item_labels[idx];
    if (sb == NULL) {
        return;
    }

    const struct fbwl_menu_item *it = &ui->current->items[idx];
    if (it->kind == FBWL_MENU_ITEM_SEPARATOR) {
        return;
    }
    fbwl_ui_menu_render_item_label_internal(ui, it, sb, idx);
}

void fbwl_ui_menu_update_all_item_labels(struct fbwl_menu_ui *ui) {
    if (ui == NULL || !ui->open || ui->current == NULL) {
        return;
    }
    for (size_t i = 0; i < ui->current->item_count; i++) {
        fbwl_ui_menu_update_item_label(ui, i);
    }
}

void fbwl_ui_menu_render_item_label(struct fbwl_menu_ui *ui, struct wlr_scene_buffer *sb, size_t idx) {
    if (ui == NULL || sb == NULL || !ui->open || ui->current == NULL || idx >= ui->current->item_count) {
        return;
    }
    const struct fbwl_menu_item *it = &ui->current->items[idx];
    if (it->kind == FBWL_MENU_ITEM_SEPARATOR) {
        return;
    }
    fbwl_ui_menu_render_item_label_internal(ui, it, sb, idx);
}
