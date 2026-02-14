#include "wayland/fbwl_ui_toolbar.h"
#include "wmcore/fbwm_core.h"
#include "wayland/fbwl_ui_toolbar_build.h"
#include "wayland/fbwl_ui_toolbar_layout.h"
#ifdef HAVE_SYSTEMD
#include "wayland/fbwl_sni_pin.h"
#include "wayland/fbwl_sni_tray.h"
#endif
#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_string_list.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_view.h"
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
enum fbwl_toolbar_pending_action {
    FBWL_TOOLBAR_PENDING_HIDE = 1u << 0,
    FBWL_TOOLBAR_PENDING_RAISE = 1u << 1,
    FBWL_TOOLBAR_PENDING_LOWER = 1u << 2,
};
enum fbwl_toolbar_edge {
    FBWL_TOOLBAR_EDGE_TOP,
    FBWL_TOOLBAR_EDGE_BOTTOM,
    FBWL_TOOLBAR_EDGE_LEFT,
    FBWL_TOOLBAR_EDGE_RIGHT,
};
static bool toolbar_edge_is_vertical(enum fbwl_toolbar_edge edge) {
    return edge == FBWL_TOOLBAR_EDGE_LEFT || edge == FBWL_TOOLBAR_EDGE_RIGHT;
}
static enum fbwl_toolbar_edge toolbar_placement_edge(enum fbwl_toolbar_placement placement) {
    switch (placement) {
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        return FBWL_TOOLBAR_EDGE_TOP;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        return FBWL_TOOLBAR_EDGE_BOTTOM;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
        return FBWL_TOOLBAR_EDGE_LEFT;
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        return FBWL_TOOLBAR_EDGE_RIGHT;
    default:
        return FBWL_TOOLBAR_EDGE_BOTTOM;
    }
}
// For top/bottom: align is left/center/right.
// For left/right: align is top/center/bottom.
static int toolbar_placement_align(enum fbwl_toolbar_placement placement) {
    switch (placement) {
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        return 0;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
        return 1;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
        return 2;
    default:
        return 1;
    }
}

static const struct fbwl_toolbar_button_cfg *toolbar_button_cfg_find(const struct fbwl_toolbar_ui *ui, const char *name) {
    if (ui == NULL || name == NULL || *name == '\0' || ui->buttons == NULL || ui->buttons_len == 0) {
        return NULL;
    }
    for (size_t i = 0; i < ui->buttons_len; i++) {
        if (ui->buttons[i].name != NULL && strcmp(ui->buttons[i].name, name) == 0) {
            return &ui->buttons[i];
        }
    }
    return NULL;
}

static void toolbar_hidden_offset(const struct fbwl_toolbar_ui *ui, int *dx, int *dy) {
    if (dx != NULL) {
        *dx = 0;
    }
    if (dy != NULL) {
        *dy = 0;
    }
    if (ui == NULL || !ui->hidden) {
        return;
    }
    const enum fbwl_toolbar_edge edge = toolbar_placement_edge(ui->placement);
    const bool vertical = toolbar_edge_is_vertical(edge);
    const int thickness = vertical ? ui->width : ui->height;
    if (thickness < 1) {
        return;
    }
    const int peek = 2;
    int delta = thickness - peek;
    if (delta < 0) {
        delta = 0;
    }
    switch (edge) {
    case FBWL_TOOLBAR_EDGE_TOP:
        if (dy != NULL) {
            *dy = -delta;
        }
        break;
    case FBWL_TOOLBAR_EDGE_BOTTOM:
        if (dy != NULL) {
            *dy = delta;
        }
        break;
    case FBWL_TOOLBAR_EDGE_LEFT:
        if (dx != NULL) {
            *dx = -delta;
        }
        break;
    case FBWL_TOOLBAR_EDGE_RIGHT:
        if (dx != NULL) {
            *dx = delta;
        }
        break;
    }
}
static bool toolbar_is_topmost_at(const struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env, int lx, int ly) {
    if (ui == NULL || env == NULL || env->scene == NULL || ui->tree == NULL) { return true; }
    double sx = 0, sy = 0;
    struct wlr_scene_node *node = wlr_scene_node_at(&env->scene->tree.node, (double)lx, (double)ly, &sx, &sy);
    for (struct wlr_scene_node *walk = node; walk != NULL; walk = walk->parent != NULL ? &walk->parent->node : NULL) {
        if (walk == &ui->tree->node) { return true; }
    }
    return false;
}
static void fbwl_ui_toolbar_apply_position(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL || !ui->enabled || ui->tree == NULL) {
        return;
    }
    int dx = 0;
    int dy = 0;
    toolbar_hidden_offset(ui, &dx, &dy);
    const int x = ui->base_x + dx;
    const int y = ui->base_y + dy;
    if (x == ui->x && y == ui->y) {
        return;
    }
    ui->x = x;
    ui->y = y;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
    const bool parentrel = ui->pseudo_decor_theme != NULL && fbwl_texture_is_parentrelative(&ui->pseudo_decor_theme->toolbar_tex);
    const bool pseudo = parentrel || (ui->pseudo_force_pseudo_transparency && ui->alpha < 255);
    if (pseudo) {
        fbwl_pseudo_bg_update(&ui->pseudo_bg, ui->tree, ui->pseudo_output_layout,
            ui->x, ui->y, 0, 0, ui->width, ui->height,
            ui->pseudo_wallpaper_mode, ui->pseudo_wallpaper_buf, ui->pseudo_background_color);
    } else {
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    }
}
static void fbwl_ui_toolbar_destroy_scene(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->clock_timer != NULL) {
        wl_event_source_remove(ui->clock_timer);
        ui->clock_timer = NULL;
    }
    if (ui->auto_timer != NULL) {
        wl_event_source_remove(ui->auto_timer);
        ui->auto_timer = NULL;
    }
    fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    ui->pseudo_output_layout = NULL;
    ui->pseudo_wallpaper_mode = FBWL_WALLPAPER_MODE_STRETCH;
    ui->pseudo_wallpaper_buf = NULL;
    ui->pseudo_background_color = NULL;
    ui->pseudo_decor_theme = NULL;
    ui->pseudo_force_pseudo_transparency = false;
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->highlight = NULL;
    free(ui->cells);
    ui->cells = NULL;
    free(ui->labels);
    ui->labels = NULL;
    ui->cell_count = 0;
    free(ui->button_item_lx);
    ui->button_item_lx = NULL;
    free(ui->button_item_w);
    ui->button_item_w = NULL;
    free(ui->button_item_tokens);
    ui->button_item_tokens = NULL;
    free(ui->button_bgs);
    ui->button_bgs = NULL;
    free(ui->button_labels);
    ui->button_labels = NULL;
    ui->button_count = 0;
    ui->buttons_x = 0;
    ui->buttons_w = 0;
    free(ui->iconbar_views);
    ui->iconbar_views = NULL;
    if (ui->iconbar_texts != NULL) {
        for (size_t i = 0; i < ui->iconbar_count; i++) {
            free(ui->iconbar_texts[i]);
        }
    }
    free(ui->iconbar_texts);
    ui->iconbar_texts = NULL;
    free(ui->iconbar_item_lx);
    ui->iconbar_item_lx = NULL;
    free(ui->iconbar_item_w);
    ui->iconbar_item_w = NULL;
    ui->iconbar_bg = NULL;
    free(ui->iconbar_bgs);
    ui->iconbar_bgs = NULL;
    free(ui->iconbar_labels);
    ui->iconbar_labels = NULL;
    free(ui->iconbar_needs_tooltip);
    ui->iconbar_needs_tooltip = NULL;
    ui->iconbar_count = 0;
    if (ui->tray_ids != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_ids[i]);
        }
    }
    free(ui->tray_ids);
    ui->tray_ids = NULL;
    if (ui->tray_services != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_services[i]);
        }
    }
    free(ui->tray_services);
    ui->tray_services = NULL;
    if (ui->tray_paths != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_paths[i]);
        }
    }
    free(ui->tray_paths);
    ui->tray_paths = NULL;
    free(ui->tray_item_lx);
    ui->tray_item_lx = NULL;
    free(ui->tray_item_w);
    ui->tray_item_w = NULL;
    free(ui->tray_rects);
    ui->tray_rects = NULL;
    free(ui->tray_icons);
    ui->tray_icons = NULL;
    ui->tray_count = 0;
    ui->tray_x = 0;
    ui->tray_w = 0;
    ui->tray_icon_w = 0;
    ui->clock_label = NULL;
    ui->clock_bg = NULL;
    ui->tray_bg = NULL;
    ui->clock_text[0] = '\0';
    ui->font[0] = '\0';
    ui->hovered = false;
    ui->auto_pending = 0;
    ui->base_x = 0;
    ui->base_y = 0;
    ui->border_w = 0;
    ui->bevel_w = 0;
    ui->thickness = 0;
}
void fbwl_ui_toolbar_destroy(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }

    fbwl_string_list_free(ui->tools_order, ui->tools_order_len);
    ui->tools_order = NULL;
    ui->tools_order_len = 0;

    fbwl_string_list_free(ui->systray_pin_left, ui->systray_pin_left_len);
    ui->systray_pin_left = NULL;
    ui->systray_pin_left_len = 0;
    fbwl_string_list_free(ui->systray_pin_right, ui->systray_pin_right_len);
    ui->systray_pin_right = NULL;
    ui->systray_pin_right_len = 0;

    fbwl_ui_toolbar_buttons_clear(ui);
    fbwl_ui_toolbar_destroy_scene(ui);
}
static void fbwl_ui_toolbar_clock_render(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (!ui->enabled || ui->tree == NULL || ui->clock_label == NULL || ui->clock_w < 1 || ui->thickness < 1) {
        return;
    }
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL) {
        return;
    }
    char text[sizeof(ui->clock_text)];
    const char *fmt = (ui->strftime_format[0] != '\0') ? ui->strftime_format : "%H:%M";
    if (strftime(text, sizeof(text), fmt, &tm) == 0) {
        return;
    }
    if (strcmp(text, ui->clock_text) == 0) {
        return;
    }
    strncpy(ui->clock_text, text, sizeof(ui->clock_text));
    ui->clock_text[sizeof(ui->clock_text) - 1] = '\0';
    const enum fbwl_toolbar_edge edge = toolbar_placement_edge(ui->placement);
    const bool vertical = toolbar_edge_is_vertical(edge);
    const int w = vertical ? ui->thickness : ui->clock_w;
    const int h = vertical ? ui->clock_w : ui->thickness;
    const int pad = ui->thickness >= 24 ? 8 : 2;
    const struct fbwl_text_effect *effect =
        ui->pseudo_decor_theme != NULL ? &ui->pseudo_decor_theme->toolbar_clock_effect : NULL;
    struct wlr_buffer *buf = fbwl_text_buffer_create(ui->clock_text, w, h, pad, ui->text_color, ui->font, effect, 0);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(ui->clock_label, NULL);
        return;
    }
    wlr_scene_buffer_set_buffer(ui->clock_label, buf);
    wlr_buffer_drop(buf);
    wlr_log(WLR_INFO, "Toolbar: clock text=%s", ui->clock_text);
}
static int fbwl_ui_toolbar_clock_timer(void *data) {
    struct fbwl_toolbar_ui *ui = data;
    if (ui == NULL) {
        return 0;
    }
    fbwl_ui_toolbar_clock_render(ui);
    if (ui->clock_timer != NULL) {
        wl_event_source_timer_update(ui->clock_timer, 1000);
    }
    return 0;
}
void fbwl_ui_toolbar_update_iconbar_focus(struct fbwl_toolbar_ui *ui, const struct fbwl_decor_theme *decor_theme, const struct fbwl_view *focused_view) {
    if (ui == NULL || decor_theme == NULL) {
        return;
    }
    if (!ui->enabled || ui->tree == NULL || ui->iconbar_count < 1 || ui->iconbar_bgs == NULL || ui->iconbar_views == NULL ||
            ui->iconbar_item_w == NULL) {
        return;
    }
    const float alpha = (float)ui->alpha / 255.0f;
    const enum fbwl_toolbar_edge edge = toolbar_placement_edge(ui->placement);
    const bool vertical = toolbar_edge_is_vertical(edge);
    size_t urgent_logged = 0;
    for (size_t i = 0; i < ui->iconbar_count; i++) {
        struct wlr_scene_buffer *sb = ui->iconbar_bgs[i];
        if (sb == NULL) {
            continue;
        }
        const struct fbwl_view *view = ui->iconbar_views[i];
        const bool urgent = view != NULL && fbwl_view_is_urgent(view);
        const bool focused_or_urgent = view != NULL && (view == focused_view || urgent);
        const struct fbwl_texture *tex = focused_or_urgent ? &decor_theme->toolbar_iconbar_focused_tex : &decor_theme->toolbar_iconbar_unfocused_tex;
        const bool parentrel = fbwl_texture_is_parentrelative(tex);
        if (!parentrel) {
            const int iw = ui->iconbar_item_w[i];
            const int w = vertical ? ui->thickness : iw;
            const int h = vertical ? iw : ui->thickness;
            struct wlr_buffer *buf = fbwl_texture_render_buffer(tex, w > 0 ? w : 1, h > 0 ? h : 1);
            wlr_scene_buffer_set_buffer(sb, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(sb, w > 0 ? w : 1, h > 0 ? h : 1);
            wlr_scene_buffer_set_opacity(sb, alpha);
            wlr_scene_node_set_enabled(&sb->node, true);
        } else {
            wlr_scene_node_set_enabled(&sb->node, false);
        }
        if (urgent && view != focused_view && urgent_logged < 3) {
            urgent_logged++;
            wlr_log(WLR_INFO, "Toolbar: iconbar attention title=%s", fbwl_view_display_title(view));
        }
    }
}
void fbwl_ui_toolbar_rebuild(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env) {
    if (ui == NULL || env == NULL || env->scene == NULL || env->decor_theme == NULL || env->wm == NULL) {
        return;
    }
    fbwl_ui_toolbar_destroy_scene(ui);
    if (!ui->enabled) {
        return;
    }
    const float alpha = (float)ui->alpha / 255.0f;
    struct wlr_scene_tree *parent = env->layer_tree != NULL ? env->layer_tree : &env->scene->tree;
    ui->tree = wlr_scene_tree_create(parent);
    if (ui->tree == NULL) {
        return;
    }
    ui->pseudo_output_layout = env->output_layout;
    ui->pseudo_wallpaper_mode = env->wallpaper_mode;
    ui->pseudo_wallpaper_buf = env->wallpaper_buf;
    ui->pseudo_background_color = env->background_color;
    ui->pseudo_decor_theme = env->decor_theme;
    ui->pseudo_force_pseudo_transparency = env->force_pseudo_transparency;
    ui->x = 0;
    ui->y = 0;
    ui->base_x = 0;
    ui->base_y = 0;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
    int h = ui->height_override;
    if (h <= 0) {
        h = env->decor_theme->toolbar_height > 0 ? env->decor_theme->toolbar_height : (env->decor_theme->title_height > 0 ? env->decor_theme->title_height : 24);
    }
    if (h < 1) {
        h = 1;
    }
    ui->thickness = h;
    int border_w = env->decor_theme->toolbar_border_width;
    if (border_w < 0) {
        border_w = 0;
    }
    if (border_w > 20) {
        border_w = 20;
    }
    int bevel_w = env->decor_theme->toolbar_bevel_width;
    if (bevel_w < 0) {
        bevel_w = 0;
    }
    if (bevel_w > 20) {
        bevel_w = 20;
    }
    ui->border_w = border_w;
    ui->bevel_w = bevel_w;
    const enum fbwl_toolbar_edge edge = toolbar_placement_edge(ui->placement);
    const bool vertical = toolbar_edge_is_vertical(edge);
    int cell_w = h * 2;
    if (cell_w < 32) {
        cell_w = 32;
    }
    if (cell_w > 256) {
        cell_w = 256;
    }
    ui->cell_w = cell_w;
    fbwl_ui_toolbar_layout_apply(ui, env, vertical);
    memcpy(ui->text_color, env->decor_theme->toolbar_text, sizeof(ui->text_color));
    ui->text_color[3] *= alpha;
    (void)snprintf(ui->font, sizeof(ui->font), "%s", env->decor_theme->toolbar_font);
    const float *fg = ui->text_color;
    const bool parentrel = fbwl_texture_is_parentrelative(&env->decor_theme->toolbar_tex);
    ui->bg = wlr_scene_buffer_create(ui->tree, NULL);
    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, border_w, border_w);
        if (!parentrel) {
            const int inner_w = ui->width - 2 * border_w;
            const int inner_h = ui->height - 2 * border_w;
            struct wlr_buffer *buf = fbwl_texture_render_buffer(&env->decor_theme->toolbar_tex,
                inner_w > 0 ? inner_w : 1,
                inner_h > 0 ? inner_h : 1);
            wlr_scene_buffer_set_buffer(ui->bg, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(ui->bg, inner_w > 0 ? inner_w : 1, inner_h > 0 ? inner_h : 1);
            wlr_scene_buffer_set_opacity(ui->bg, alpha);
        } else {
            wlr_scene_node_set_enabled(&ui->bg->node, false);
        }
    }

    if (border_w > 0) {
        float c[4] = {
            env->decor_theme->toolbar_border_color[0],
            env->decor_theme->toolbar_border_color[1],
            env->decor_theme->toolbar_border_color[2],
            env->decor_theme->toolbar_border_color[3] * alpha,
        };
        struct wlr_scene_rect *top = wlr_scene_rect_create(ui->tree, ui->width, border_w, c);
        struct wlr_scene_rect *bottom = wlr_scene_rect_create(ui->tree, ui->width, border_w, c);
        struct wlr_scene_rect *left = wlr_scene_rect_create(ui->tree, border_w, ui->height - 2 * border_w, c);
        struct wlr_scene_rect *right = wlr_scene_rect_create(ui->tree, border_w, ui->height - 2 * border_w, c);
        if (top != NULL) {
            wlr_scene_node_set_position(&top->node, 0, 0);
        }
        if (bottom != NULL) {
            wlr_scene_node_set_position(&bottom->node, 0, ui->height - border_w);
        }
        if (left != NULL) {
            wlr_scene_node_set_position(&left->node, 0, border_w);
        }
        if (right != NULL) {
            wlr_scene_node_set_position(&right->node, ui->width - border_w, border_w);
        }
    }
    ui->highlight = NULL;
    fbwl_ui_toolbar_build_buttons(ui, env, vertical, fg);
    fbwl_ui_toolbar_build_iconbar(ui, env, vertical, fg);
    fbwl_ui_toolbar_build_tray(ui, env, vertical, alpha);
    if ((ui->tools & FBWL_TOOLBAR_TOOL_CLOCK) != 0 && ui->clock_w > 0) {
        ui->clock_bg = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->clock_bg != NULL) {
            wlr_scene_node_set_position(&ui->clock_bg->node,
                vertical ? (ui->border_w + ui->bevel_w) : ui->clock_x,
                vertical ? ui->clock_x : (ui->border_w + ui->bevel_w));
            const struct fbwl_texture *tex = env->decor_theme != NULL ? &env->decor_theme->toolbar_clock_tex : NULL;
            const bool parentrel_clock = fbwl_texture_is_parentrelative(tex);
            if (tex != NULL && !parentrel_clock) {
                const int w = vertical ? ui->thickness : ui->clock_w;
                const int h = vertical ? ui->clock_w : ui->thickness;
                struct wlr_buffer *buf = fbwl_texture_render_buffer(tex, w > 0 ? w : 1, h > 0 ? h : 1);
                wlr_scene_buffer_set_buffer(ui->clock_bg, buf);
                if (buf != NULL) {
                    wlr_buffer_drop(buf);
                }
                wlr_scene_buffer_set_dest_size(ui->clock_bg, w > 0 ? w : 1, h > 0 ? h : 1);
                wlr_scene_buffer_set_opacity(ui->clock_bg, alpha);
            } else {
                wlr_scene_node_set_enabled(&ui->clock_bg->node, false);
            }
        }
        ui->clock_label = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->clock_label != NULL) {
            wlr_scene_node_set_position(&ui->clock_label->node,
                vertical ? (ui->border_w + ui->bevel_w) : ui->clock_x,
                vertical ? ui->clock_x : (ui->border_w + ui->bevel_w));
            fbwl_ui_toolbar_clock_render(ui);
        }
        struct wl_event_loop *loop = env->wl_display != NULL ? wl_display_get_event_loop(env->wl_display) : NULL;
        if (loop != NULL) {
            ui->clock_timer = wl_event_loop_add_timer(loop, fbwl_ui_toolbar_clock_timer, ui);
            if (ui->clock_timer != NULL) {
                wl_event_source_timer_update(ui->clock_timer, 1000);
            }
        }
    }
    fbwl_ui_toolbar_update_iconbar_focus(ui, env->decor_theme, env->focused_view);
    wlr_scene_node_raise_to_top(&ui->tree->node);
    wlr_log(WLR_INFO, "Toolbar: built x=%d y=%d w=%d h=%d cell_w=%d onhead=%d layer=%d workspaces=%zu buttons=%zu iconbar=%zu tray=%zu clock_w=%d",
        ui->x, ui->y, ui->width, ui->height, ui->cell_w, ui->on_head + 1, ui->layer_num, ui->cell_count,
        ui->button_count, ui->iconbar_count, ui->tray_count, ui->clock_w);
    fbwl_ui_toolbar_update_position(ui, env);
}
void fbwl_ui_toolbar_update_position(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env) {
    if (ui == NULL || env == NULL || env->output_layout == NULL) {
        return;
    }
    if (!ui->enabled || ui->tree == NULL) {
        return;
    }
    ui->pseudo_output_layout = env->output_layout;
    ui->pseudo_wallpaper_mode = env->wallpaper_mode;
    ui->pseudo_wallpaper_buf = env->wallpaper_buf;
    ui->pseudo_background_color = env->background_color;
    ui->pseudo_decor_theme = env->decor_theme;
    ui->pseudo_force_pseudo_transparency = env->force_pseudo_transparency;
    const size_t on_head = ui->on_head >= 0 ? (size_t)ui->on_head : 0;
    struct wlr_output *out = fbwl_screen_map_output_for_screen(env->output_layout, env->outputs, on_head);
    if (out == NULL) {
        return;
    }
    struct wlr_box box = {0};
    wlr_output_layout_get_box(env->output_layout, out, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }
    int percent = ui->width_percent;
    if (percent < 1 || percent > 100) {
        percent = 100;
    }
    const enum fbwl_toolbar_edge edge = toolbar_placement_edge(ui->placement);
    const bool vertical = toolbar_edge_is_vertical(edge);
    int border_w = env->decor_theme != NULL ? env->decor_theme->toolbar_border_width : 0;
    if (border_w < 0) {
        border_w = 0;
    }
    if (border_w > 20) {
        border_w = 20;
    }
    int bevel_w = env->decor_theme != NULL ? env->decor_theme->toolbar_bevel_width : 0;
    if (bevel_w < 0) {
        bevel_w = 0;
    }
    if (bevel_w > 20) {
        bevel_w = 20;
    }

    const int output_main = vertical ? box.height : box.width;
    int avail = output_main - 2 * border_w;
    if (avail < 1) {
        avail = 1;
    }
    int inner_main = (avail * percent) / 100;
    if (inner_main < 1) {
        inner_main = 1;
    }
    int payload_main = inner_main - 2 * bevel_w;
    if (payload_main < ui->ws_width) {
        payload_main = ui->ws_width;
    }
    if (payload_main < 1) {
        payload_main = 1;
    }
    inner_main = payload_main + 2 * bevel_w;
    const int desired_outer_main = inner_main + 2 * border_w;
    if ((vertical && desired_outer_main != ui->height) || (!vertical && desired_outer_main != ui->width)) {
        fbwl_ui_toolbar_rebuild(ui, env);
        return;
    }
    const int align = toolbar_placement_align(ui->placement);
    int base_x = box.x;
    int base_y = box.y;
    if (vertical) {
        if (edge == FBWL_TOOLBAR_EDGE_RIGHT) {
            base_x = box.x + box.width - ui->width;
        }
        if (ui->height > 0 && ui->height < box.height) {
            if (align == 2) {
                base_y = box.y + box.height - ui->height;
            } else if (align == 1) {
                base_y = box.y + (box.height - ui->height) / 2;
            }
        }
    } else {
        if (ui->width > 0 && ui->width < box.width) {
            if (align == 2) {
                base_x = box.x + box.width - ui->width;
            } else if (align == 1) {
                base_x = box.x + (box.width - ui->width) / 2;
            }
        }
        if (edge == FBWL_TOOLBAR_EDGE_BOTTOM) {
            base_y = box.y + box.height - ui->height;
        }
    }
    if (base_x < box.x) {
        base_x = box.x;
    }
    if (base_y < box.y) {
        base_y = box.y;
    }
    const int prev_x = ui->x;
    const int prev_y = ui->y;
    ui->base_x = base_x;
    ui->base_y = base_y;
    fbwl_ui_toolbar_apply_position(ui);
    const bool parentrel = ui->pseudo_decor_theme != NULL && fbwl_texture_is_parentrelative(&ui->pseudo_decor_theme->toolbar_tex);
    const bool pseudo = parentrel || (ui->pseudo_force_pseudo_transparency && ui->alpha < 255);
    if (pseudo) {
        fbwl_pseudo_bg_update(&ui->pseudo_bg, ui->tree, ui->pseudo_output_layout,
            ui->x, ui->y, 0, 0, ui->width, ui->height,
            ui->pseudo_wallpaper_mode, ui->pseudo_wallpaper_buf, ui->pseudo_background_color);
    } else {
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    }
    if (ui->x != prev_x || ui->y != prev_y) {
        wlr_log(WLR_INFO, "Toolbar: position x=%d y=%d h=%d cell_w=%d workspaces=%zu w=%d thickness=%d alpha=%u",
            ui->x, ui->y, ui->height, ui->cell_w, ui->cell_count, ui->width, ui->thickness, (unsigned)ui->alpha);
    }
}
static void toolbar_cancel_auto_timer(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->auto_timer != NULL) {
        wl_event_source_remove(ui->auto_timer);
        ui->auto_timer = NULL;
    }
    ui->auto_pending = 0;
}
static int fbwl_ui_toolbar_auto_timer(void *data) {
    struct fbwl_toolbar_ui *ui = data;
    if (ui == NULL || !ui->enabled || ui->tree == NULL) {
        return 0;
    }
    const uint32_t pending = ui->auto_pending;
    ui->auto_pending = 0;
    if ((pending & FBWL_TOOLBAR_PENDING_HIDE) != 0) {
        if (ui->auto_hide && !ui->hovered && !ui->hidden) {
            ui->hidden = true;
            fbwl_ui_toolbar_apply_position(ui);
            wlr_log(WLR_INFO, "Toolbar: autoHide hide");
        }
    }
    if ((pending & FBWL_TOOLBAR_PENDING_RAISE) != 0) {
        if (ui->auto_raise && ui->hovered) {
            wlr_scene_node_raise_to_top(&ui->tree->node);
            wlr_log(WLR_INFO, "Toolbar: autoRaise raise");
        }
    }
    if ((pending & FBWL_TOOLBAR_PENDING_LOWER) != 0) {
        if (ui->auto_raise && !ui->hovered) {
            wlr_scene_node_lower_to_bottom(&ui->tree->node);
            wlr_log(WLR_INFO, "Toolbar: autoRaise lower");
        }
    }
    toolbar_cancel_auto_timer(ui);
    return 0;
}
void fbwl_ui_toolbar_handle_motion(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env, int lx, int ly, int delay_ms) {
    if (ui == NULL || env == NULL || env->wl_display == NULL) {
        return;
    }
    if (!ui->enabled || ui->tree == NULL || ui->width < 1 || ui->height < 1) {
        return;
    }
    if (!ui->auto_hide && !ui->auto_raise) {
        return;
    }
    ui->pseudo_output_layout = env->output_layout;
    ui->pseudo_wallpaper_mode = env->wallpaper_mode;
    ui->pseudo_wallpaper_buf = env->wallpaper_buf;
    ui->pseudo_background_color = env->background_color;
    ui->pseudo_decor_theme = env->decor_theme;
    ui->pseudo_force_pseudo_transparency = env->force_pseudo_transparency;
    const bool was_hovered = ui->hovered;
    bool hovered =
        lx >= ui->x && lx < ui->x + ui->width &&
        ly >= ui->y && ly < ui->y + ui->height;
    if (hovered) {
        hovered = toolbar_is_topmost_at(ui, env, lx, ly);
    }
    ui->hovered = hovered;
    if (hovered && !was_hovered) {
        toolbar_cancel_auto_timer(ui);
        if (ui->auto_hide && ui->hidden) {
            ui->hidden = false;
            fbwl_ui_toolbar_apply_position(ui);
            wlr_log(WLR_INFO, "Toolbar: autoHide show");
        }
        if (ui->auto_raise) {
            ui->auto_pending |= FBWL_TOOLBAR_PENDING_RAISE;
        }
    } else if (!hovered && was_hovered) {
        toolbar_cancel_auto_timer(ui);
        if (ui->auto_hide && !ui->hidden) {
            ui->auto_pending |= FBWL_TOOLBAR_PENDING_HIDE;
        }
        if (ui->auto_raise) {
            ui->auto_pending |= FBWL_TOOLBAR_PENDING_LOWER;
        }
    } else {
        return;
    }
    if (ui->auto_pending == 0) {
        return;
    }
    struct wl_event_loop *loop = wl_display_get_event_loop(env->wl_display);
    if (loop == NULL) {
        ui->auto_pending = 0;
        return;
    }
    ui->auto_timer = wl_event_loop_add_timer(loop, fbwl_ui_toolbar_auto_timer, ui);
    if (ui->auto_timer == NULL) {
        ui->auto_pending = 0;
        return;
    }
    if (delay_ms < 0) {
        delay_ms = 0;
    }
    wl_event_source_timer_update(ui->auto_timer, delay_ms);
}
bool fbwl_ui_toolbar_handle_click(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
        const struct fbwl_ui_toolbar_hooks *hooks, int lx, int ly, uint32_t button) {
    if (ui == NULL || env == NULL || env->wm == NULL) {
        return false;
    }
    if (!ui->enabled || ui->tree == NULL || ui->width < 1 || ui->height < 1) {
        return false;
    }
    const int x = lx - ui->x;
    const int y = ly - ui->y;
    if (x < 0 || x >= ui->width || y < 0 || y >= ui->height) {
        return false;
    }
    if (!toolbar_is_topmost_at(ui, env, lx, ly)) {
        return false;
    }
    const enum fbwl_toolbar_edge edge = toolbar_placement_edge(ui->placement);
    const bool vertical = toolbar_edge_is_vertical(edge);
    const int main = vertical ? y : x;
    if (ui->button_count > 0 && ui->button_item_lx != NULL && ui->button_item_w != NULL && ui->button_item_tokens != NULL) {
        for (size_t i = 0; i < ui->button_count; i++) {
            const int ix = ui->button_item_lx[i];
            const int iw = ui->button_item_w[i];
            if (main < ix || main >= ix + iw) {
                continue;
            }

            const char *tok = ui->button_item_tokens[i];
            if (tok == NULL || *tok == '\0') {
                return true;
            }

            if (strncmp(tok, "button.", 7) == 0) {
                int cmd_idx = -1;
                if (button == BTN_LEFT) {
                    cmd_idx = 0;
                } else if (button == BTN_MIDDLE) {
                    cmd_idx = 1;
                } else if (button == BTN_RIGHT) {
                    cmd_idx = 2;
                } else if (button == 4) {
                    cmd_idx = 3;
                } else if (button == 5) {
                    cmd_idx = 4;
                }

                if (cmd_idx >= 0) {
                    const char *name = tok + 7;
                    const struct fbwl_toolbar_button_cfg *cfg = toolbar_button_cfg_find(ui, name);
                    const char *cmd = (cfg != NULL) ? cfg->commands[cmd_idx] : NULL;
                    wlr_log(WLR_INFO, "Toolbar: click button.%s button=%d cmd=%s", name, cmd_idx + 1, cmd != NULL ? cmd : "");
                    if (hooks != NULL && hooks->execute_command != NULL && cmd != NULL && *cmd != '\0') {
                        hooks->execute_command(hooks->userdata, cmd, lx, ly, button);
                    }
                }
                return true;
            }

            const char *cmd = NULL;
            if (strcmp(tok, "prevworkspace") == 0 && button == BTN_LEFT) {
                cmd = "prevworkspace";
            } else if (strcmp(tok, "nextworkspace") == 0 && button == BTN_LEFT) {
                cmd = "nextworkspace";
            } else if (strcmp(tok, "workspacename") == 0 && button == BTN_LEFT) {
                cmd = "prevworkspace";
            } else if (strcmp(tok, "workspacename") == 0 && button == BTN_RIGHT) {
                cmd = "nextworkspace";
            } else if (strcmp(tok, "prevwindow") == 0 && button == BTN_LEFT) {
                cmd = "prevwindow (workspace=[current])";
            } else if (strcmp(tok, "nextwindow") == 0 && button == BTN_LEFT) {
                cmd = "nextwindow (workspace=[current])";
            }

            wlr_log(WLR_INFO, "Toolbar: click tool=%s cmd=%s", tok, cmd != NULL ? cmd : "");
            if (cmd != NULL && hooks != NULL && hooks->execute_command != NULL) {
                hooks->execute_command(hooks->userdata, cmd, lx, ly, button);
            }
            return true;
        }
    }
    if (button == BTN_LEFT &&
            ui->iconbar_count > 0 && ui->iconbar_views != NULL && ui->iconbar_item_lx != NULL &&
            ui->iconbar_item_w != NULL) {
        for (size_t i = 0; i < ui->iconbar_count; i++) {
            const int ix = ui->iconbar_item_lx[i];
            const int iw = ui->iconbar_item_w[i];
            if (main < ix || main >= ix + iw) {
                continue;
            }
            struct fbwl_view *view = ui->iconbar_views[i];
            if (view == NULL) {
                return true;
            }
            wlr_log(WLR_INFO, "Toolbar: click iconbar idx=%zu title=%s", i, fbwl_view_display_title(view));
            if (view->minimized) {
                if (hooks != NULL && hooks->view_set_minimized != NULL) {
                    hooks->view_set_minimized(hooks->userdata, view, false, "toolbar-iconbar");
                }
            }
            if (!view->wm_view.sticky &&
                    view->wm_view.workspace != fbwm_core_workspace_current_for_head(env->wm,
                        ui->on_head >= 0 ? (size_t)ui->on_head : 0)) {
                fbwm_core_workspace_switch_on_head(env->wm, ui->on_head >= 0 ? (size_t)ui->on_head : 0,
                    view->wm_view.workspace);
                if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
                    hooks->apply_workspace_visibility(hooks->userdata, "toolbar-iconbar-switch");
                }
            }
            fbwm_core_focus_view(env->wm, &view->wm_view);
            return true;
        }
    }
    if (ui->tray_count > 0 && ui->tray_item_lx != NULL && ui->tray_item_w != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            const int ix = ui->tray_item_lx[i];
            const int iw = ui->tray_item_w[i];
            if (main < ix || main >= ix + iw) {
                continue;
            }
            const char *id = (ui->tray_ids != NULL && ui->tray_ids[i] != NULL) ? ui->tray_ids[i] : "";
            const char *method = NULL;
            const char *action = NULL;
            if (button == BTN_LEFT) {
                method = "Activate";
                action = "activate";
            } else if (button == BTN_MIDDLE) {
                method = "SecondaryActivate";
                action = "secondary-activate";
            } else if (button == BTN_RIGHT) {
                method = "ContextMenu";
                action = "context-menu";
            } else {
                return false;
            }
            wlr_log(WLR_INFO, "Toolbar: click tray idx=%zu id=%s action=%s", i, id, action);
#ifdef HAVE_SYSTEMD
            if (env->sni != NULL && ui->tray_services != NULL && ui->tray_paths != NULL) {
                const char *service = ui->tray_services[i];
                const char *path = ui->tray_paths[i];
                fbwl_sni_send_item_action(env->sni, id, service, path, method, action, lx, ly);
            }
#endif
            return true;
        }
    }
    return button == BTN_LEFT;
}

bool fbwl_ui_toolbar_tooltip_text_at(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
        int lx, int ly, const char **out_text) {
    if (out_text != NULL) {
        *out_text = NULL;
    }
    if (ui == NULL || env == NULL) {
        return false;
    }
    if (!ui->enabled || ui->tree == NULL || ui->width < 1 || ui->height < 1) {
        return false;
    }

    const int x = lx - ui->x;
    const int y = ly - ui->y;
    if (x < 0 || x >= ui->width || y < 0 || y >= ui->height) {
        return false;
    }
    if (!toolbar_is_topmost_at(ui, env, lx, ly)) {
        return false;
    }

    const enum fbwl_toolbar_edge edge = toolbar_placement_edge(ui->placement);
    const bool vertical = toolbar_edge_is_vertical(edge);
    const int main = vertical ? y : x;

    if (ui->iconbar_count < 1 || ui->iconbar_views == NULL || ui->iconbar_item_lx == NULL || ui->iconbar_item_w == NULL) {
        return false;
    }

    for (size_t i = 0; i < ui->iconbar_count; i++) {
        const int ix = ui->iconbar_item_lx[i];
        const int iw = ui->iconbar_item_w[i];
        if (main < ix || main >= ix + iw) {
            continue;
        }
        if (ui->iconbar_needs_tooltip != NULL && !ui->iconbar_needs_tooltip[i]) {
            return false;
        }
        struct fbwl_view *view = ui->iconbar_views[i];
        if (view == NULL) {
            return false;
        }
        const char *title = NULL;
        if (ui->iconbar_texts != NULL && ui->iconbar_texts[i] != NULL) {
            title = ui->iconbar_texts[i];
        }
        if (title == NULL) {
            title = fbwl_view_display_title(view);
        }
        if (out_text != NULL) {
            *out_text = title;
        }
        return title != NULL && *title != '\0';
    }

    return false;
}
