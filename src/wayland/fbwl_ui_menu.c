#include "wayland/fbwl_ui_menu.h"

#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_menu_icon.h"
#include "wayland/fbwl_ui_menu_marks.h"
#include "wayland/fbwl_ui_menu_search.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <linux/input-event-codes.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

static void fbwl_ui_menu_cancel_submenu_timer(struct fbwl_menu_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->submenu_timer != NULL) {
        wl_event_source_remove(ui->submenu_timer);
        ui->submenu_timer = NULL;
    }
    ui->submenu_pending_idx = 0;
}

static void fbwl_ui_menu_destroy_scene(struct fbwl_menu_ui *ui) {
    if (ui == NULL) {
        return;
    }
    fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->title_bg = NULL;
    ui->title_label = NULL;
    ui->title_h = 0;
    ui->border_w = 0;
    ui->highlight = NULL;
    free(ui->item_rects);
    ui->item_rects = NULL;
    ui->item_rect_count = 0;
    free(ui->item_labels);
    ui->item_labels = NULL;
    ui->item_label_count = 0;
    free(ui->item_marks);
    ui->item_marks = NULL;
    ui->item_mark_count = 0;
}

static bool fbwl_ui_menu_contains_point(const struct fbwl_menu_ui *ui, int lx, int ly) {
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
    return true;
}

void fbwl_ui_menu_close(struct fbwl_menu_ui *ui, const char *why) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open) {
        return;
    }

    fbwl_ui_menu_destroy_scene(ui);
    fbwl_ui_menu_cancel_submenu_timer(ui);
    ui->open = false;
    ui->current = NULL;
    ui->depth = 0;
    ui->selected = 0;
    ui->target_view = NULL;
    ui->env = (struct fbwl_ui_menu_env){0};
    ui->hovered_idx = -1;

    wlr_log(WLR_INFO, "Menu: close reason=%s", why != NULL ? why : "(null)");
}

static void fbwl_ui_menu_update_item_label(struct fbwl_menu_ui *ui, size_t idx) {
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
    const char *render = it->label != NULL ? it->label : "(no-label)";

    const float alpha = (float)ui->alpha / 255.0f;
    float fg[4] = {ui->env.decor_theme->menu_text[0], ui->env.decor_theme->menu_text[1], ui->env.decor_theme->menu_text[2],
        ui->env.decor_theme->menu_text[3] * alpha};
    float hi_fg[4] = {ui->env.decor_theme->menu_hilite_text[0], ui->env.decor_theme->menu_hilite_text[1],
        ui->env.decor_theme->menu_hilite_text[2], ui->env.decor_theme->menu_hilite_text[3] * alpha};
    float dis_fg[4] = {ui->env.decor_theme->menu_disable_text[0], ui->env.decor_theme->menu_disable_text[1],
        ui->env.decor_theme->menu_disable_text[2], ui->env.decor_theme->menu_disable_text[3] * alpha};

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

    const int item_h = ui->item_h > 0 ? ui->item_h : 1;
    const int w = ui->width > 0 ? ui->width : 200;
    const int bevel = ui->env.decor_theme->menu_bevel_width > 0 ? ui->env.decor_theme->menu_bevel_width : 0;
    const int left_reserve = bevel + item_h + 1;
    const int right_reserve = bevel + item_h;
    const int label_w = w - left_reserve - right_reserve > 1 ? w - left_reserve - right_reserve : 1;

    struct wlr_buffer *text_buf = fbwl_text_buffer_create(render, label_w, item_h, 0, use_fg, font, effect, justify);
    if (text_buf == NULL) {
        return;
    }
    wlr_scene_buffer_set_buffer(sb, text_buf);
    wlr_buffer_drop(text_buf);
}

static void fbwl_ui_menu_rebuild(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env) {
    if (env == NULL || env->scene == NULL || env->decor_theme == NULL) {
        return;
    }
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL) {
        return;
    }

    fbwl_ui_menu_search_reset(ui);
    fbwl_ui_menu_destroy_scene(ui);

    struct wlr_scene_tree *parent =
        env->layer_overlay != NULL ? env->layer_overlay : &env->scene->tree;
    ui->tree = wlr_scene_tree_create(parent);
    if (ui->tree == NULL) {
        ui->open = false;
        ui->current = NULL;
        ui->depth = 0;
        ui->selected = 0;
        return;
    }
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);

    const int count = (int)ui->current->item_count;
    int item_h = ui->item_h;
    if (item_h <= 0) {
        item_h = env->decor_theme->menu_item_height > 0 ? env->decor_theme->menu_item_height : 24;
    }
    if (item_h < 1) {
        item_h = 1;
    }
    ui->item_h = item_h;
    const int w = ui->width > 0 ? ui->width : 200;
    const char *title = ui->current->label;
    const bool has_title = title != NULL && *title != '\0';
    int title_h = 0;
    if (has_title) {
        title_h = env->decor_theme->menu_title_height > 0 ? env->decor_theme->menu_title_height : item_h;
        if (title_h < 1) {
            title_h = item_h;
        }
        if (title_h < 1) {
            title_h = 1;
        }
    }
    ui->title_h = title_h;
    const int h = title_h + (count > 0 ? count * item_h : item_h);

    const int bevel = env->decor_theme->menu_bevel_width > 0 ? env->decor_theme->menu_bevel_width : 0;
    const int bw = env->decor_theme->menu_border_width > 0 ? env->decor_theme->menu_border_width : 0;
    ui->border_w = bw;
    const int outer_w = w + 2 * bw;
    const int outer_h = h + 2 * bw;

    const float alpha = (float)ui->alpha / 255.0f;
    const bool frame_parentrel = fbwl_texture_is_parentrelative(&env->decor_theme->menu_frame_tex);
    const bool pseudo = frame_parentrel || (env->force_pseudo_transparency && ui->alpha < 255);
    if (pseudo) {
        fbwl_pseudo_bg_update(&ui->pseudo_bg, ui->tree, env->output_layout,
            ui->x, ui->y, 0, 0, outer_w, outer_h, env->wallpaper_mode, env->wallpaper_buf, env->background_color);
    } else {
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    }

    if (bw > 0) {
        float bc[4] = {
            env->decor_theme->menu_border_color[0],
            env->decor_theme->menu_border_color[1],
            env->decor_theme->menu_border_color[2],
            env->decor_theme->menu_border_color[3] * alpha,
        };

        struct wlr_scene_rect *top = wlr_scene_rect_create(ui->tree, outer_w, bw, bc);
        if (top != NULL) {
            wlr_scene_node_set_position(&top->node, 0, 0);
        }
        struct wlr_scene_rect *bottom = wlr_scene_rect_create(ui->tree, outer_w, bw, bc);
        if (bottom != NULL) {
            wlr_scene_node_set_position(&bottom->node, 0, outer_h - bw);
        }
        struct wlr_scene_rect *left = wlr_scene_rect_create(ui->tree, bw, outer_h - 2 * bw, bc);
        if (left != NULL) {
            wlr_scene_node_set_position(&left->node, 0, bw);
        }
        struct wlr_scene_rect *right = wlr_scene_rect_create(ui->tree, bw, outer_h - 2 * bw, bc);
        if (right != NULL) {
            wlr_scene_node_set_position(&right->node, outer_w - bw, bw);
        }
    }

    ui->bg = wlr_scene_buffer_create(ui->tree, NULL);
    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, bw, bw);
        if (!frame_parentrel) {
            struct wlr_buffer *buf = fbwl_texture_render_buffer(&env->decor_theme->menu_frame_tex, w, h);
            wlr_scene_buffer_set_buffer(ui->bg, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(ui->bg, w, h);
            wlr_scene_buffer_set_opacity(ui->bg, alpha);
        } else {
            wlr_scene_node_set_enabled(&ui->bg->node, false);
        }
    }

    if (title_h > 0) {
        const bool title_parentrel = fbwl_texture_is_parentrelative(&env->decor_theme->menu_title_tex);

        ui->title_bg = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->title_bg != NULL) {
            wlr_scene_node_set_position(&ui->title_bg->node, bw, bw);
            if (!title_parentrel) {
                struct wlr_buffer *buf = fbwl_texture_render_buffer(&env->decor_theme->menu_title_tex, w, title_h);
                wlr_scene_buffer_set_buffer(ui->title_bg, buf);
                if (buf != NULL) {
                    wlr_buffer_drop(buf);
                }
                wlr_scene_buffer_set_dest_size(ui->title_bg, w, title_h);
                wlr_scene_buffer_set_opacity(ui->title_bg, alpha);
            } else {
                wlr_scene_node_set_enabled(&ui->title_bg->node, false);
            }
        }

        float title_fg[4] = {
            env->decor_theme->menu_title_text[0],
            env->decor_theme->menu_title_text[1],
            env->decor_theme->menu_title_text[2],
            env->decor_theme->menu_title_text[3] * alpha,
        };
        const int title_pad_x = bevel > 0 ? bevel : 8;
        const char *title_font =
            env->decor_theme->menu_title_font[0] != '\0' ? env->decor_theme->menu_title_font : env->decor_theme->menu_font;
        struct wlr_buffer *title_buf =
            fbwl_text_buffer_create(title, w, title_h, title_pad_x, title_fg, title_font,
                &env->decor_theme->menu_title_effect, env->decor_theme->menu_title_justify);
        if (title_buf != NULL) {
            ui->title_label = wlr_scene_buffer_create(ui->tree, title_buf);
            if (ui->title_label != NULL) {
                wlr_scene_node_set_position(&ui->title_label->node, bw, bw);
            }
            wlr_buffer_drop(title_buf);
        }
    }

    if (ui->selected >= ui->current->item_count) {
        ui->selected = ui->current->item_count > 0 ? ui->current->item_count - 1 : 0;
    }
    const bool hilite_parentrel = fbwl_texture_is_parentrelative(&env->decor_theme->menu_hilite_tex);
    ui->highlight = wlr_scene_buffer_create(ui->tree, NULL);
    if (ui->highlight != NULL) {
        wlr_scene_node_set_position(&ui->highlight->node, bw, bw + title_h + (int)ui->selected * item_h);
        if (!hilite_parentrel) {
            struct wlr_buffer *buf = fbwl_texture_render_buffer(&env->decor_theme->menu_hilite_tex, w, item_h);
            wlr_scene_buffer_set_buffer(ui->highlight, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(ui->highlight, w, item_h);
            wlr_scene_buffer_set_opacity(ui->highlight, alpha);
        } else {
            wlr_scene_node_set_enabled(&ui->highlight->node, false);
        }
    }

    ui->item_rect_count = ui->current->item_count;
    if (ui->item_rect_count > 0) {
        ui->item_rects = calloc(ui->item_rect_count, sizeof(*ui->item_rects));
        if (ui->item_rects != NULL) {
            float item[4] = {0.00f, 0.00f, 0.00f, 0.01f};
            for (size_t i = 0; i < ui->item_rect_count; i++) {
                ui->item_rects[i] = wlr_scene_rect_create(ui->tree, w, item_h, item);
                if (ui->item_rects[i] != NULL) {
                    wlr_scene_node_set_position(&ui->item_rects[i]->node, bw, bw + title_h + (int)i * item_h);
                }
            }
        }
    }

    ui->item_label_count = ui->current->item_count;
    if (ui->item_label_count > 0) {
        ui->item_labels = calloc(ui->item_label_count, sizeof(*ui->item_labels));
    }

    ui->item_mark_count = ui->current->item_count;
    if (ui->item_mark_count > 0) {
        ui->item_marks = calloc(ui->item_mark_count, sizeof(*ui->item_marks));
    }

    float fg[4] = {env->decor_theme->menu_text[0], env->decor_theme->menu_text[1], env->decor_theme->menu_text[2],
        env->decor_theme->menu_text[3] * alpha};
    float hi_fg[4] = {env->decor_theme->menu_hilite_text[0], env->decor_theme->menu_hilite_text[1],
        env->decor_theme->menu_hilite_text[2], env->decor_theme->menu_hilite_text[3] * alpha};
    float dis_fg[4] = {env->decor_theme->menu_disable_text[0], env->decor_theme->menu_disable_text[1],
        env->decor_theme->menu_disable_text[2], env->decor_theme->menu_disable_text[3] * alpha};

    const int left_reserve = bevel + item_h + 1;
    const int right_reserve = bevel + item_h;
    const int label_w = w - left_reserve - right_reserve > 1 ? w - left_reserve - right_reserve : 1;
    const char *hilite_font =
        env->decor_theme->menu_hilite_font[0] != '\0' ? env->decor_theme->menu_hilite_font : env->decor_theme->menu_font;

    int mark_x = bw;
    if (env->decor_theme->menu_bullet_pos == 2) {
        mark_x = bw + w - item_h - bevel;
        if (mark_x < bw) {
            mark_x = bw;
        }
    }
    for (size_t i = 0; i < ui->current->item_count; i++) {
        const struct fbwl_menu_item *it = &ui->current->items[i];
        if (it->kind == FBWL_MENU_ITEM_SEPARATOR) {
            float sep[4] = {fg[0], fg[1], fg[2], 0.30f * alpha};
            struct wlr_scene_rect *line = wlr_scene_rect_create(ui->tree, w, 1, sep);
            if (line != NULL) {
                wlr_scene_node_set_position(&line->node, bw, bw + title_h + (int)i * item_h + item_h / 2);
            }
            continue;
        }

        if (ui->item_marks != NULL && i < ui->item_mark_count) {
            struct wlr_scene_buffer *mb = wlr_scene_buffer_create(ui->tree, NULL);
            ui->item_marks[i] = mb;
            if (mb != NULL) {
                wlr_scene_node_set_position(&mb->node, mark_x, bw + title_h + (int)i * item_h);
                wlr_scene_buffer_set_dest_size(mb, item_h, item_h);
                wlr_scene_buffer_set_opacity(mb, alpha);
                fbwl_ui_menu_update_item_mark(ui, i);
            }
        }

        const bool wants_icon = it->icon != NULL && *it->icon != '\0';
        if (wants_icon) {
            const int icon_px = item_h - 2 * bevel > 1 ? item_h - 2 * bevel : item_h;
            if (icon_px >= 1) {
                struct wlr_buffer *icon_buf = fbwl_ui_menu_icon_buffer_create(it->icon, icon_px);
                if (icon_buf != NULL) {
                    struct wlr_scene_buffer *ib = wlr_scene_buffer_create(ui->tree, icon_buf);
                    if (ib != NULL) {
                        const int x = bw + bevel;
                        const int y = bw + title_h + (int)i * item_h + bevel;
                        wlr_scene_node_set_position(&ib->node, x, y);
                        wlr_scene_buffer_set_opacity(ib, alpha);
                    }
                    wlr_buffer_drop(icon_buf);
                }
            }
        }

        const float *use_fg = fg;
        const char *font = env->decor_theme->menu_font;
        const struct fbwl_text_effect *effect = &env->decor_theme->menu_frame_effect;
        int justify = env->decor_theme->menu_frame_justify;
        if (it->kind == FBWL_MENU_ITEM_NOP) {
            use_fg = dis_fg;
        } else if (i == ui->selected) {
            use_fg = hi_fg;
            font = hilite_font;
            effect = &env->decor_theme->menu_hilite_effect;
            justify = env->decor_theme->menu_hilite_justify;
        }
        const char *render = it->label != NULL ? it->label : "(no-label)";
        struct wlr_buffer *text_buf = fbwl_text_buffer_create(render, label_w, item_h, 0, use_fg, font, effect, justify);
        if (text_buf != NULL) {
            struct wlr_scene_buffer *sb = wlr_scene_buffer_create(ui->tree, text_buf);
            if (sb != NULL) {
                wlr_scene_node_set_position(&sb->node, bw + left_reserve, bw + title_h + (int)i * item_h);
                if (ui->item_labels != NULL && i < ui->item_label_count) {
                    ui->item_labels[i] = sb;
                }
            }
            wlr_buffer_drop(text_buf);
        }
    }

    wlr_scene_node_raise_to_top(&ui->tree->node);
}

static void fbwl_ui_menu_enter_submenu(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env, size_t idx,
        const char *reason, int delay_ms) {
    if (ui == NULL || env == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL) {
        return;
    }
    if (idx >= ui->current->item_count) {
        return;
    }

    struct fbwl_menu_item *it = &ui->current->items[idx];
    if (it->kind != FBWL_MENU_ITEM_SUBMENU || it->submenu == NULL) {
        return;
    }

    if (ui->depth + 1 >= (sizeof(ui->stack) / sizeof(ui->stack[0]))) {
        return;
    }

    fbwl_ui_menu_cancel_submenu_timer(ui);

    const char *label = it->label != NULL ? it->label : "(no-label)";

    ui->depth++;
    ui->stack[ui->depth] = it->submenu;
    ui->current = it->submenu;
    ui->selected = 0;
    ui->hovered_idx = -1;
    fbwl_ui_menu_rebuild(ui, env);
    if (delay_ms >= 0) {
        wlr_log(WLR_INFO, "Menu: enter-submenu reason=%s delay_ms=%d label=%s items=%zu",
            reason != NULL ? reason : "(null)", delay_ms, label, ui->current->item_count);
    } else {
        wlr_log(WLR_INFO, "Menu: enter-submenu reason=%s label=%s items=%zu",
            reason != NULL ? reason : "(null)", label, ui->current->item_count);
    }
}

void fbwl_ui_menu_open_root(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
        struct fbwl_menu *root_menu, int x, int y) {
    if (ui == NULL || env == NULL || root_menu == NULL) {
        return;
    }

    fbwl_ui_menu_close(ui, "reopen");

    ui->open = true;
    ui->current = root_menu;
    ui->depth = 0;
    ui->stack[0] = root_menu;
    ui->selected = 0;
    ui->target_view = NULL;
    ui->env = *env;
    ui->hovered_idx = -1;
    fbwl_ui_menu_cancel_submenu_timer(ui);

    ui->x = x;
    ui->y = y;
    ui->width = 200;
    if (env->decor_theme != NULL) {
        ui->item_h = env->decor_theme->menu_item_height > 0 ? env->decor_theme->menu_item_height : 24;
    } else {
        ui->item_h = 0;
    }

    fbwl_ui_menu_rebuild(ui, env);
    wlr_log(WLR_INFO, "Menu: open at x=%d y=%d items=%zu alpha=%u delay_ms=%d",
        x, y, ui->current->item_count, (unsigned)ui->alpha, ui->menu_delay_ms);
}

void fbwl_ui_menu_open_window(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
        struct fbwl_menu *window_menu, struct fbwl_view *view, int x, int y) {
    if (ui == NULL || env == NULL || window_menu == NULL || view == NULL) {
        return;
    }

    fbwl_ui_menu_close(ui, "reopen-window");

    ui->open = true;
    ui->current = window_menu;
    ui->depth = 0;
    ui->stack[0] = window_menu;
    ui->selected = 0;
    ui->target_view = view;
    ui->env = *env;
    ui->hovered_idx = -1;
    fbwl_ui_menu_cancel_submenu_timer(ui);

    ui->x = x;
    ui->y = y;
    ui->width = 200;
    if (env->decor_theme != NULL) {
        ui->item_h = env->decor_theme->menu_item_height > 0 ? env->decor_theme->menu_item_height : 24;
    } else {
        ui->item_h = 0;
    }

    fbwl_ui_menu_rebuild(ui, env);
    wlr_log(WLR_INFO, "Menu: open-window title=%s x=%d y=%d items=%zu alpha=%u delay_ms=%d",
        fbwl_view_display_title(view), x, y, ui->current->item_count, (unsigned)ui->alpha, ui->menu_delay_ms);
}

ssize_t fbwl_ui_menu_index_at(const struct fbwl_menu_ui *ui, int lx, int ly) {
    if (ui == NULL || !ui->open || ui->current == NULL) {
        return -1;
    }
    const int bw = ui->border_w > 0 ? ui->border_w : 0;
    const int x = lx - ui->x - bw;
    const int y = ly - ui->y - bw;
    const int item_h = ui->item_h > 0 ? ui->item_h : 1;
    const int w = ui->width > 0 ? ui->width : 1;
    const int items_h = (int)ui->current->item_count * item_h;
    const int title_h = ui->title_h > 0 ? ui->title_h : 0;
    const int h = title_h + items_h;
    if (x < 0 || x >= w || y < 0 || y >= h) {
        return -1;
    }
    if (y < title_h) {
        return -1;
    }
    const ssize_t idx = (y - title_h) / item_h;
    if (idx < 0 || (size_t)idx >= ui->current->item_count) {
        return -1;
    }
    return idx;
}

void fbwl_ui_menu_set_selected(struct fbwl_menu_ui *ui, size_t idx) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL) {
        return;
    }
    if (ui->current->item_count == 0) {
        ui->selected = 0;
        return;
    }
    if (idx >= ui->current->item_count) {
        idx = ui->current->item_count - 1;
    }
    const size_t prev = ui->selected;
    ui->selected = idx;
    if (ui->highlight != NULL) {
        const int bw = ui->border_w > 0 ? ui->border_w : 0;
        const int item_h = ui->item_h > 0 ? ui->item_h : 1;
        wlr_scene_node_set_position(&ui->highlight->node, bw, bw + ui->title_h + (int)ui->selected * item_h);
    }
    if (prev != ui->selected) {
        fbwl_ui_menu_update_item_label(ui, prev);
        fbwl_ui_menu_update_item_label(ui, ui->selected);
        fbwl_ui_menu_update_item_mark(ui, prev);
        fbwl_ui_menu_update_item_mark(ui, ui->selected);
    }
}

static int fbwl_ui_menu_submenu_timer(void *data) {
    struct fbwl_menu_ui *ui = data;
    if (ui == NULL || !ui->open || ui->current == NULL) {
        return 0;
    }

    const size_t idx = ui->submenu_pending_idx;
    const int delay_ms = ui->menu_delay_ms;

    fbwl_ui_menu_cancel_submenu_timer(ui);

    if (ui->hovered_idx < 0 || (size_t)ui->hovered_idx != idx) {
        return 0;
    }
    if (idx >= ui->current->item_count) {
        return 0;
    }
    if (ui->env.wl_display == NULL) {
        return 0;
    }

    fbwl_ui_menu_enter_submenu(ui, &ui->env, idx, "delay", delay_ms);
    return 0;
}

void fbwl_ui_menu_handle_motion(struct fbwl_menu_ui *ui, int lx, int ly) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL) {
        return;
    }

    const ssize_t idx = fbwl_ui_menu_index_at(ui, lx, ly);
    if (idx < 0) {
        ui->hovered_idx = -1;
        fbwl_ui_menu_cancel_submenu_timer(ui);
        return;
    }

    if (ui->hovered_idx != idx) {
        ui->hovered_idx = idx;
        fbwl_ui_menu_set_selected(ui, (size_t)idx);
    }

    if (ui->env.wl_display == NULL) {
        return;
    }

    const size_t hovered = (size_t)idx;
    if (hovered >= ui->current->item_count) {
        return;
    }

    const struct fbwl_menu_item *it = &ui->current->items[hovered];
    if (it->kind != FBWL_MENU_ITEM_SUBMENU || it->submenu == NULL) {
        fbwl_ui_menu_cancel_submenu_timer(ui);
        return;
    }

    if (ui->submenu_timer != NULL && ui->submenu_pending_idx == hovered) {
        return;
    }

    fbwl_ui_menu_cancel_submenu_timer(ui);

    int delay_ms = ui->menu_delay_ms;
    if (delay_ms < 0) {
        delay_ms = 0;
    }
    if (delay_ms == 0) {
        fbwl_ui_menu_enter_submenu(ui, &ui->env, hovered, "hover", delay_ms);
        return;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(ui->env.wl_display);
    if (loop == NULL) {
        return;
    }

    ui->submenu_pending_idx = hovered;
    ui->submenu_timer = wl_event_loop_add_timer(loop, fbwl_ui_menu_submenu_timer, ui);
    if (ui->submenu_timer == NULL) {
        ui->submenu_pending_idx = 0;
        return;
    }

    wl_event_source_timer_update(ui->submenu_timer, delay_ms);
}

static void fbwl_ui_menu_activate_selected(struct fbwl_menu_ui *ui,
        const struct fbwl_ui_menu_env *env, const struct fbwl_ui_menu_hooks *hooks, uint32_t button) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->current == NULL || ui->current->item_count == 0) {
        return;
    }
    if (ui->selected >= ui->current->item_count) {
        ui->selected = ui->current->item_count - 1;
    }

    struct fbwl_menu_item *it = &ui->current->items[ui->selected];
    const char *label = it->label != NULL ? it->label : "(no-label)";

    if (it->kind == FBWL_MENU_ITEM_SEPARATOR || it->kind == FBWL_MENU_ITEM_NOP) {
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_EXEC) {
        wlr_log(WLR_INFO, "Menu: exec label=%s cmd=%s", label, it->cmd != NULL ? it->cmd : "(null)");
        if (hooks != NULL && hooks->spawn != NULL) {
            hooks->spawn(hooks->userdata, it->cmd);
        }
        if (it->close_on_click) {
            fbwl_ui_menu_close(ui, "exec");
        } else {
            fbwl_ui_menu_rebuild(ui, env);
        }
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_WORKSPACE_SWITCH) {
        wlr_log(WLR_INFO, "Menu: workspace-switch label=%s workspace=%d", label, it->arg + 1);
        if (hooks != NULL && hooks->workspace_switch != NULL) {
            hooks->workspace_switch(hooks->userdata, it->arg);
        }
        if (it->close_on_click) {
            fbwl_ui_menu_close(ui, "workspace-switch");
        } else {
            fbwl_ui_menu_rebuild(ui, env);
        }
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_EXIT) {
        wlr_log(WLR_INFO, "Menu: exit label=%s", label);
        if (it->close_on_click) {
            fbwl_ui_menu_close(ui, "exit");
        } else {
            fbwl_ui_menu_rebuild(ui, env);
        }
        if (hooks != NULL && hooks->terminate != NULL) {
            hooks->terminate(hooks->userdata);
        }
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_SERVER_ACTION) {
        enum fbwl_menu_server_action action = it->server_action;
        const int arg = it->arg;
        const char *cmd = it->cmd != NULL ? it->cmd : "";
        char *cmd_copy = *cmd != '\0' ? strdup(cmd) : NULL;

        if (action == FBWL_MENU_SERVER_SLIT_TOGGLE_CLIENT_VISIBLE) {
            if (button == BTN_MIDDLE || button == 4) {
                action = FBWL_MENU_SERVER_SLIT_CLIENT_UP;
            } else if (button == BTN_RIGHT || button == 5) {
                action = FBWL_MENU_SERVER_SLIT_CLIENT_DOWN;
            }
        }

        if (action == FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE) {
            if (button == BTN_MIDDLE) {
                action = FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE_VERTICAL;
            } else if (button == BTN_RIGHT) {
                action = FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE_HORIZONTAL;
            }
        }

        if (action == FBWL_MENU_SERVER_WINDOW_SEND_TO_WORKSPACE) {
            if (button == BTN_MIDDLE) {
                action = FBWL_MENU_SERVER_WINDOW_TAKE_TO_WORKSPACE;
            }
        }

        wlr_log(WLR_INFO, "Menu: server-action label=%s action=%d arg=%d cmd=%s",
            label, (int)action, arg, cmd_copy != NULL ? cmd_copy : "(null)");
        if (hooks != NULL && hooks->server_action != NULL) {
            hooks->server_action(hooks->userdata, action, arg, cmd_copy, it);
        }
        if (it->close_on_click) {
            fbwl_ui_menu_close(ui, "server-action");
        } else {
            fbwl_ui_menu_rebuild(ui, env);
        }
        free(cmd_copy);
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_VIEW_ACTION) {
        struct fbwl_view *view = ui->target_view;
        if (view == NULL) {
            fbwl_ui_menu_close(ui, "window-action-no-view");
            return;
        }

        switch (it->view_action) {
        case FBWL_MENU_VIEW_CLOSE:
            wlr_log(WLR_INFO, "Menu: window-close title=%s", fbwl_view_display_title(view));
            if (hooks != NULL && hooks->view_close != NULL) {
                hooks->view_close(hooks->userdata, view);
            }
            fbwl_ui_menu_close(ui, "window-close");
            return;
        case FBWL_MENU_VIEW_TOGGLE_MINIMIZE:
            wlr_log(WLR_INFO, "Menu: window-minimize title=%s", fbwl_view_display_title(view));
            if (hooks != NULL && hooks->view_set_minimized != NULL) {
                hooks->view_set_minimized(hooks->userdata, view, !view->minimized, "window-menu");
            }
            fbwl_ui_menu_close(ui, "window-minimize");
            return;
        case FBWL_MENU_VIEW_TOGGLE_MAXIMIZE:
            wlr_log(WLR_INFO, "Menu: window-maximize title=%s", fbwl_view_display_title(view));
            if (hooks != NULL && hooks->view_set_maximized != NULL) {
                hooks->view_set_maximized(hooks->userdata, view, !view->maximized);
            }
            fbwl_ui_menu_close(ui, "window-maximize");
            return;
        case FBWL_MENU_VIEW_TOGGLE_FULLSCREEN:
            wlr_log(WLR_INFO, "Menu: window-fullscreen title=%s", fbwl_view_display_title(view));
            if (hooks != NULL && hooks->view_set_fullscreen != NULL) {
                hooks->view_set_fullscreen(hooks->userdata, view, !view->fullscreen);
            }
            fbwl_ui_menu_close(ui, "window-fullscreen");
            return;
        default:
            fbwl_ui_menu_close(ui, "window-action-unknown");
            return;
        }
    }
    if (it->kind == FBWL_MENU_ITEM_SUBMENU && it->submenu != NULL) {
        fbwl_ui_menu_enter_submenu(ui, env, ui->selected, "activate", -1);
        return;
    }
}

bool fbwl_ui_menu_handle_keypress(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
        const struct fbwl_ui_menu_hooks *hooks, xkb_keysym_t sym) {
    if (ui == NULL) {
        return false;
    }
    if (!ui->open) {
        return false;
    }

    if (sym == XKB_KEY_BackSpace && fbwl_ui_menu_search_handle_key(ui, sym)) {
        return true;
    }
    if (sym == XKB_KEY_Escape) {
        fbwl_ui_menu_search_reset(ui);
        fbwl_ui_menu_close(ui, "escape");
        return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        fbwl_ui_menu_search_reset(ui);
        fbwl_ui_menu_activate_selected(ui, env, hooks, BTN_LEFT);
        return true;
    }
    if (sym == XKB_KEY_Down) {
        fbwl_ui_menu_search_reset(ui);
        fbwl_ui_menu_set_selected(ui, ui->selected + 1);
        return true;
    }
    if (sym == XKB_KEY_Up) {
        fbwl_ui_menu_search_reset(ui);
        size_t idx = ui->selected;
        if (idx > 0) {
            idx--;
        }
        fbwl_ui_menu_set_selected(ui, idx);
        return true;
    }
    if (sym == XKB_KEY_Left || sym == XKB_KEY_BackSpace) {
        fbwl_ui_menu_search_reset(ui);
        if (ui->depth > 0) {
            ui->depth--;
            ui->current = ui->stack[ui->depth];
            ui->selected = 0;
            fbwl_ui_menu_rebuild(ui, env);
            wlr_log(WLR_INFO, "Menu: back items=%zu", ui->current != NULL ? ui->current->item_count : 0);
        } else {
            fbwl_ui_menu_close(ui, "back");
        }
        return true;
    }

    return fbwl_ui_menu_search_handle_key(ui, sym);
}

bool fbwl_ui_menu_handle_click(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
        const struct fbwl_ui_menu_hooks *hooks, int lx, int ly, uint32_t button) {
    if (ui == NULL) {
        return false;
    }
    if (!ui->open || ui->current == NULL) {
        return false;
    }

    if (!fbwl_ui_menu_contains_point(ui, lx, ly)) {
        fbwl_ui_menu_close(ui, "click-outside");
        return true;
    }

    const ssize_t idx = fbwl_ui_menu_index_at(ui, lx, ly);
    if (idx < 0) {
        if (button == BTN_RIGHT) {
            fbwl_ui_menu_close(ui, "right-click-bg");
        }
        return true;
    }

    fbwl_ui_menu_set_selected(ui, (size_t)idx);
    const struct fbwl_menu_item *it = &ui->current->items[(size_t)idx];
    if (button == BTN_LEFT) {
        fbwl_ui_menu_activate_selected(ui, env, hooks, button);
    } else if (button == BTN_RIGHT) {
        if (!it->close_on_click || (it->kind == FBWL_MENU_ITEM_SERVER_ACTION &&
                it->server_action == FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE)) {
            fbwl_ui_menu_activate_selected(ui, env, hooks, button);
        } else {
            fbwl_ui_menu_close(ui, "right-click");
        }
    } else if (button == BTN_MIDDLE || button == 4 || button == 5) {
        if (!it->close_on_click ||
                (it->kind == FBWL_MENU_ITEM_SERVER_ACTION &&
                    (it->server_action == FBWL_MENU_SERVER_WINDOW_SEND_TO_WORKSPACE ||
                        it->server_action == FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE))) {
            fbwl_ui_menu_activate_selected(ui, env, hooks, button);
        }
    }
    return true;
}
