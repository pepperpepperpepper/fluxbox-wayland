#include "wayland/fbwl_view.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_deco_mask.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_view_decor_tabs.h"

bool fbwl_view_accepts_focus(const struct fbwl_view *view) {
    if (view == NULL) {
        return false;
    }
    if (view->in_slit) {
        return false;
    }

    if (view->type != FBWL_VIEW_XWAYLAND || view->xwayland_surface == NULL) {
        return true;
    }

    const struct wlr_xwayland_surface *xsurface = view->xwayland_surface;
    return !wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK) &&
        !wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DESKTOP) &&
        !wlr_xwayland_surface_has_window_type(xsurface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH);
}

static int decor_theme_button_size(const struct fbwl_decor_theme *theme) {
    if (theme == NULL) {
        return 0;
    }
    int size = theme->title_height - 2 * theme->button_margin;
    if (size < 1) {
        size = 1;
    }
    return size;
}

static bool decor_texture_can_use_flat_rect(const struct fbwl_texture *tex) {
    if (tex == NULL) {
        return true;
    }
    if (tex->pixmap[0] != '\0') {
        return false;
    }
    const uint32_t allowed = FBWL_TEXTURE_FLAT | FBWL_TEXTURE_SOLID;
    if ((tex->type & FBWL_TEXTURE_PARENTRELATIVE) != 0) {
        return false;
    }
    return (tex->type & ~allowed) == 0;
}

static bool view_decor_button_allowed(const struct fbwl_view *view, enum fbwl_decor_hit_kind kind) {
    if (view == NULL) {
        return false;
    }
    if ((view->decor_mask & FBWL_DECORM_TITLEBAR) == 0) {
        return false;
    }

    switch (kind) {
    case FBWL_DECOR_HIT_BTN_MENU:
        return (view->decor_mask & FBWL_DECORM_MENU) != 0;
    case FBWL_DECOR_HIT_BTN_SHADE:
        return (view->decor_mask & FBWL_DECORM_SHADE) != 0;
    case FBWL_DECOR_HIT_BTN_STICK:
        return (view->decor_mask & FBWL_DECORM_STICKY) != 0;
    case FBWL_DECOR_HIT_BTN_CLOSE:
        return (view->decor_mask & FBWL_DECORM_CLOSE) != 0;
    case FBWL_DECOR_HIT_BTN_MAX:
        return (view->decor_mask & FBWL_DECORM_MAXIMIZE) != 0;
    case FBWL_DECOR_HIT_BTN_MIN:
        return (view->decor_mask & FBWL_DECORM_ICONIFY) != 0;
    case FBWL_DECOR_HIT_BTN_LHALF:
    case FBWL_DECOR_HIT_BTN_RHALF:
        return (view->decor_mask & FBWL_DECORM_MAXIMIZE) != 0;
    default:
        return false;
    }
}

static size_t view_decor_visible_buttons(const struct fbwl_view *view,
        const enum fbwl_decor_hit_kind *buttons, size_t len) {
    if (view == NULL || buttons == NULL || len == 0) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (view_decor_button_allowed(view, buttons[i])) {
            n++;
        }
    }
    return n;
}

static struct wlr_scene_rect *decor_button_rect(struct fbwl_view *view, enum fbwl_decor_hit_kind kind) {
    if (view == NULL) {
        return NULL;
    }
    switch (kind) {
    case FBWL_DECOR_HIT_BTN_MENU:
        return view->decor_btn_menu;
    case FBWL_DECOR_HIT_BTN_SHADE:
        return view->decor_btn_shade;
    case FBWL_DECOR_HIT_BTN_STICK:
        return view->decor_btn_stick;
    case FBWL_DECOR_HIT_BTN_CLOSE:
        return view->decor_btn_close;
    case FBWL_DECOR_HIT_BTN_MAX:
        return view->decor_btn_max;
    case FBWL_DECOR_HIT_BTN_MIN:
        return view->decor_btn_min;
    case FBWL_DECOR_HIT_BTN_LHALF:
        return view->decor_btn_lhalf;
    case FBWL_DECOR_HIT_BTN_RHALF:
        return view->decor_btn_rhalf;
    default:
        return NULL;
    }
}
static float alpha_to_opacity(uint8_t alpha) {
    return (float)alpha / 255.0f;
}

static const struct fbwl_tabs_config *view_tabs_cfg(const struct fbwl_view *view) {
    if (view == NULL || view->server == NULL) {
        return NULL;
    }
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(view->server, view);
    return cfg != NULL ? &cfg->tabs : &view->server->tabs;
}

void fbwl_view_decor_apply_enabled(struct fbwl_view *view) {
    if (view == NULL || view->decor_tree == NULL) {
        return;
    }
    const bool show = view->decor_enabled && !view->fullscreen && fbwl_deco_mask_has_frame(view->decor_mask);
    wlr_scene_node_set_enabled(&view->decor_tree->node, show);
}

void fbwl_view_decor_frame_extents(const struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        int *left, int *top, int *right, int *bottom) {
    if (left != NULL) {
        *left = 0;
    }
    if (top != NULL) {
        *top = 0;
    }
    if (right != NULL) {
        *right = 0;
    }
    if (bottom != NULL) {
        *bottom = 0;
    }

    if (view == NULL || theme == NULL || !view->decor_enabled || view->fullscreen) {
        return;
    }

    const uint32_t mask = view->decor_mask;
    if (!fbwl_deco_mask_has_frame(mask)) {
        return;
    }

    int border = theme->border_width;
    if (border < 0) {
        border = 0;
    }
    int title_h = theme->title_height;
    if (title_h < 0) {
        title_h = 0;
    }

    const bool has_titlebar = (mask & FBWL_DECORM_TITLEBAR) != 0;
    const bool has_border = (mask & FBWL_DECORM_BORDER) != 0;
    const bool has_handle = (mask & FBWL_DECORM_HANDLE) != 0 && !view->shaded;

    if (has_border && border > 0) {
        if (left != NULL) {
            *left = border;
        }
        if (right != NULL) {
            *right = border;
        }
        if (bottom != NULL) {
            *bottom = border;
        }
    } else if (has_handle && border > 0) {
        if (bottom != NULL) {
            *bottom = border;
        }
    }

    if (has_titlebar && title_h > 0) {
        int t = title_h;
        if (has_border && border > 0) {
            t += border;
        }
        if (top != NULL) {
            *top = t;
        }
    } else if (has_border && border > 0) {
        if (top != NULL) {
            *top = border;
        }
    }
}

void fbwl_view_decor_set_active(struct fbwl_view *view, const struct fbwl_decor_theme *theme, bool active) {
    if (view == NULL || theme == NULL) {
        return;
    }
    view->decor_active = active;
    fbwl_view_decor_update(view, theme);
    fbwl_view_alpha_apply(view);
}
void fbwl_view_decor_update_title_text(struct fbwl_view *view, const struct fbwl_decor_theme *theme) {
    if (view == NULL || view->decor_tree == NULL || theme == NULL) {
        return;
    }
    if (view->decor_title_text == NULL) {
        view->decor_title_text = wlr_scene_buffer_create(view->decor_tree, NULL);
        if (view->decor_title_text == NULL) {
            return;
        }
    }

    const bool titlebar_on = view->decor_enabled && !view->fullscreen && (view->decor_mask & FBWL_DECORM_TITLEBAR) != 0;
    const int title_h = titlebar_on ? theme->title_height : 0;
    const int w = fbwl_view_current_width(view);

    if (!titlebar_on || w < 1 || title_h < 1) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }

    const struct fbwl_tabs_config *tabs = view_tabs_cfg(view);
    if (tabs != NULL && tabs->intitlebar && (view->decor_mask & FBWL_DECORM_TAB) != 0 &&
            view->tab_group != NULL && fbwl_tabs_group_mapped_count(view) >= 2) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }

    const char *title = fbwl_view_title(view);
    if (title == NULL || *title == '\0') {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }
    const int btn_size = decor_theme_button_size(theme);
    const struct fbwl_server *server = view->server;
    const size_t left_len = server != NULL ? server->titlebar_left_len : 0;
    const size_t right_len = server != NULL ? server->titlebar_right_len : 0;
    const size_t left_vis = server != NULL ? view_decor_visible_buttons(view, server->titlebar_left, left_len) : 0;
    const size_t right_vis = server != NULL ? view_decor_visible_buttons(view, server->titlebar_right, right_len) : 0;
    const int reserved_left = theme->button_margin +
        (left_vis > 0 ? ((int)left_vis * btn_size) + ((int)(left_vis - 1) * theme->button_spacing) + theme->button_margin : 0);
    const int reserved_right = theme->button_margin +
        (right_vis > 0 ? ((int)right_vis * btn_size) + ((int)(right_vis - 1) * theme->button_spacing) + theme->button_margin : 0);
    int text_x = reserved_left;
    int text_w = w - reserved_left - reserved_right;
    if (text_w < 1) {
        text_x = 0;
        text_w = w;
    }
    if (text_w < 1) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }
    wlr_scene_node_set_position(&view->decor_title_text->node, text_x, -title_h);
    if (view->decor_title_text_cache != NULL &&
            view->decor_title_text_cache_w == text_w &&
            view->decor_title_text_cache_active == view->decor_active &&
            strcmp(view->decor_title_text_cache, title) == 0) {
        return;
    }
    free(view->decor_title_text_cache);
    view->decor_title_text_cache = strdup(title);
    if (view->decor_title_text_cache == NULL) {
        view->decor_title_text_cache_w = 0;
        view->decor_title_text_cache_active = false;
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        return;
    }
    view->decor_title_text_cache_w = text_w;
    view->decor_title_text_cache_active = view->decor_active;
    const float *fg = view->decor_active ? theme->title_text_active : theme->title_text_inactive;
    const struct fbwl_text_effect *effect =
        view->decor_active ? &theme->window_label_focus_effect : &theme->window_label_unfocus_effect;
    struct wlr_buffer *buf = fbwl_text_buffer_create(title, text_w, title_h, 8, fg, theme->window_font, effect);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        view->decor_title_text_cache_active = false;
        return;
    }
    wlr_scene_buffer_set_buffer(view->decor_title_text, buf);
    wlr_buffer_drop(buf);
    wlr_log(WLR_INFO, "Decor: title-render %s", title);
}
void fbwl_view_decor_update(struct fbwl_view *view, const struct fbwl_decor_theme *theme) {
    if (view == NULL || view->decor_tree == NULL || theme == NULL) {
        return;
    }
    fbwl_view_decor_apply_enabled(view);
    const int w = fbwl_view_current_width(view);
    int h = fbwl_view_current_height(view);
    if (w < 1 || (!view->shaded && h < 1)) {
        return;
    }
    if (view->shaded) {
        h = 0;
    }

    const uint32_t mask = view->decor_mask;
    int border_px = theme->border_width;
    if (border_px < 0) {
        border_px = 0;
    }
    int title_h_px = theme->title_height;
    if (title_h_px < 0) {
        title_h_px = 0;
    }

    const bool titlebar_on = view->decor_enabled && !view->fullscreen && (mask & FBWL_DECORM_TITLEBAR) != 0;
    const bool border_on = view->decor_enabled && !view->fullscreen && (mask & FBWL_DECORM_BORDER) != 0;
    const bool handle_on = view->decor_enabled && !view->fullscreen && !view->shaded && (mask & FBWL_DECORM_HANDLE) != 0;

    const int frame_title_h = titlebar_on ? title_h_px : 0;
    const int bottom_thick = border_on ? border_px : (handle_on ? border_px : 0);

    const bool show_titlebar = titlebar_on && frame_title_h > 0;
    const bool show_border = border_on && border_px > 0;
    const bool show_bottom = bottom_thick > 0;

    const struct fbwl_texture *title_tex = view->decor_active ? &theme->window_title_focus_tex : &theme->window_title_unfocus_tex;
    const bool title_parentrel = fbwl_texture_is_parentrelative(title_tex);
    const bool title_use_rect = decor_texture_can_use_flat_rect(title_tex);
    bool title_use_buffer = show_titlebar && !title_use_rect && !title_parentrel;
    if (view->decor_titlebar != NULL) {
        wlr_scene_node_set_enabled(&view->decor_titlebar->node, show_titlebar);
        if (show_titlebar) {
            wlr_scene_rect_set_size(view->decor_titlebar, w, frame_title_h);
            wlr_scene_node_set_position(&view->decor_titlebar->node, 0, -frame_title_h);
        }
    }
    if (view->decor_titlebar_tex != NULL) {
        wlr_scene_node_set_enabled(&view->decor_titlebar_tex->node, title_use_buffer);
        if (title_use_buffer) {
            struct wlr_buffer *buf = fbwl_texture_render_buffer(title_tex, w, frame_title_h);
            if (buf != NULL) {
                wlr_scene_buffer_set_buffer(view->decor_titlebar_tex, buf);
                wlr_buffer_drop(buf);
                wlr_scene_buffer_set_dest_size(view->decor_titlebar_tex, w, frame_title_h);
                wlr_scene_node_set_position(&view->decor_titlebar_tex->node, 0, -frame_title_h);
                wlr_scene_node_lower_to_bottom(&view->decor_titlebar_tex->node);
            } else {
                title_use_buffer = false;
                wlr_scene_node_set_enabled(&view->decor_titlebar_tex->node, false);
                wlr_scene_buffer_set_buffer(view->decor_titlebar_tex, NULL);
            }
        } else {
            wlr_scene_buffer_set_buffer(view->decor_titlebar_tex, NULL);
        }
    }
    if (show_titlebar && title_parentrel) {
        const struct fbwl_server *server = view->server;
        fbwl_pseudo_bg_update(&view->decor_titlebar_pseudo_bg, view->decor_tree,
            server != NULL ? server->output_layout : NULL,
            view->x, view->y - frame_title_h, 0, -frame_title_h, w, frame_title_h,
            server != NULL ? server->wallpaper_mode : FBWL_WALLPAPER_MODE_STRETCH,
            server != NULL ? server->wallpaper_buf : NULL,
            server != NULL ? server->background_color : NULL);
    } else {
        fbwl_pseudo_bg_destroy(&view->decor_titlebar_pseudo_bg);
    }
    if (view->decor_titlebar != NULL && show_titlebar) {
        if (title_use_rect || (!title_use_buffer && !title_parentrel)) {
            wlr_scene_rect_set_color(view->decor_titlebar, title_tex->color);
        } else {
            float input[4] = {0.0f, 0.0f, 0.0f, 0.01f};
            wlr_scene_rect_set_color(view->decor_titlebar, input);
        }
    }
    if (view->decor_border_top != NULL) {
        wlr_scene_node_set_enabled(&view->decor_border_top->node, show_border);
        if (show_border) {
            wlr_scene_rect_set_size(view->decor_border_top, w + 2 * border_px, border_px);
            wlr_scene_node_set_position(&view->decor_border_top->node, -border_px, -frame_title_h - border_px);
        }
    }
    if (view->decor_border_bottom != NULL) {
        wlr_scene_node_set_enabled(&view->decor_border_bottom->node, show_bottom);
        if (show_bottom) {
            const int bw = border_on ? (w + 2 * border_px) : w;
            const int bx = border_on ? -border_px : 0;
            wlr_scene_rect_set_size(view->decor_border_bottom, bw, bottom_thick);
            wlr_scene_node_set_position(&view->decor_border_bottom->node, bx, h);
        }
    }
    if (view->decor_border_left != NULL) {
        wlr_scene_node_set_enabled(&view->decor_border_left->node, show_border);
        if (show_border) {
            wlr_scene_rect_set_size(view->decor_border_left, border_px, frame_title_h + border_px + h + border_px);
            wlr_scene_node_set_position(&view->decor_border_left->node, -border_px, -frame_title_h - border_px);
        }
    }
    if (view->decor_border_right != NULL) {
        wlr_scene_node_set_enabled(&view->decor_border_right->node, show_border);
        if (show_border) {
            wlr_scene_rect_set_size(view->decor_border_right, border_px, frame_title_h + border_px + h + border_px);
            wlr_scene_node_set_position(&view->decor_border_right->node, w, -frame_title_h - border_px);
        }
    }
    const int btn_size = decor_theme_button_size(theme);
    const int btn_y = -frame_title_h + theme->button_margin;
    struct wlr_scene_rect *btns[] = { view->decor_btn_menu, view->decor_btn_shade, view->decor_btn_stick,
        view->decor_btn_close, view->decor_btn_max, view->decor_btn_min, view->decor_btn_lhalf, view->decor_btn_rhalf };
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
        if (btns[i] != NULL) {
            wlr_scene_node_set_enabled(&btns[i]->node, false);
        }
    }

    const struct fbwl_server *server = view->server;
    if (server != NULL && show_titlebar) {
        int btn_x = theme->button_margin;
        for (size_t i = 0; i < server->titlebar_left_len; i++) {
            const enum fbwl_decor_hit_kind kind = server->titlebar_left[i];
            if (!view_decor_button_allowed(view, kind)) {
                continue;
            }
            struct wlr_scene_rect *rect = decor_button_rect(view, kind);
            if (rect != NULL) {
                wlr_scene_rect_set_size(rect, btn_size, btn_size);
                wlr_scene_node_set_position(&rect->node, btn_x, btn_y);
                wlr_scene_node_set_enabled(&rect->node, true);
                btn_x += btn_size + theme->button_spacing;
            }
        }
        btn_x = w - theme->button_margin - btn_size;
        for (size_t i = server->titlebar_right_len; i-- > 0; ) {
            const enum fbwl_decor_hit_kind kind = server->titlebar_right[i];
            if (!view_decor_button_allowed(view, kind)) {
                continue;
            }
            struct wlr_scene_rect *rect = decor_button_rect(view, kind);
            if (rect != NULL) {
                wlr_scene_rect_set_size(rect, btn_size, btn_size);
                wlr_scene_node_set_position(&rect->node, btn_x, btn_y);
                wlr_scene_node_set_enabled(&rect->node, true);
                btn_x -= btn_size + theme->button_spacing;
            }
        }
    }
    fbwl_view_decor_update_title_text(view, theme);
    fbwl_view_decor_tabs_ui_build(view, theme);
}
void fbwl_view_decor_create(struct fbwl_view *view, const struct fbwl_decor_theme *theme) {
    if (view == NULL || view->scene_tree == NULL || view->decor_tree != NULL) {
        return;
    }
    view->decor_tree = wlr_scene_tree_create(view->scene_tree);
    if (view->decor_tree == NULL) {
        return;
    }
    if (theme != NULL) {
        view->decor_titlebar_tex = wlr_scene_buffer_create(view->decor_tree, NULL);
        view->decor_titlebar = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->titlebar_inactive);
        view->decor_title_text = wlr_scene_buffer_create(view->decor_tree, NULL);
        view->decor_border_top = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->border_color);
        view->decor_border_bottom = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->border_color);
        view->decor_border_left = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->border_color);
        view->decor_border_right = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->border_color);
        view->decor_btn_menu = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_menu_color);
        view->decor_btn_shade = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_shade_color);
        view->decor_btn_stick = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_stick_color);
        view->decor_btn_close = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_close_color);
        view->decor_btn_max = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_max_color);
        view->decor_btn_min = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_min_color);
        view->decor_btn_lhalf = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_lhalf_color);
        view->decor_btn_rhalf = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_rhalf_color);
    }
    view->decor_enabled = false;
    view->decor_mask = FBWL_DECOR_NORMAL;
    view->decor_active = false;
    wlr_scene_node_set_enabled(&view->decor_tree->node, false);
    fbwl_view_decor_update(view, theme);
}
void fbwl_view_decor_set_enabled(struct fbwl_view *view, bool enabled) {
    if (view == NULL) {
        return;
    }
    view->decor_enabled = enabled;
    fbwl_view_decor_apply_enabled(view);
}
struct fbwl_decor_hit fbwl_view_decor_hit_test(const struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        double lx, double ly) {
    struct fbwl_decor_hit hit = {0};
    if (view == NULL || theme == NULL || view->decor_tree == NULL ||
            !view->decor_enabled || view->fullscreen) {
        return hit;
    }
    if (!fbwl_deco_mask_has_frame(view->decor_mask)) {
        return hit;
    }
    const int w = fbwl_view_current_width(view);
    int h = fbwl_view_current_height(view);
    if (w < 1 || (!view->shaded && h < 1)) {
        return hit;
    }
    if (view->shaded) {
        h = 0;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    fbwl_view_decor_frame_extents(view, theme, &left, &top, &right, &bottom);

    int border = (view->decor_mask & FBWL_DECORM_BORDER) != 0 ? theme->border_width : 0;
    if (border < 0) {
        border = 0;
    }
    const bool titlebar_on = (view->decor_mask & FBWL_DECORM_TITLEBAR) != 0;
    int title_h = titlebar_on ? theme->title_height : 0;
    if (title_h < 0) {
        title_h = 0;
    }

    const int btn_size = decor_theme_button_size(theme);
    const int btn_margin = theme->button_margin;
    const double x = lx - view->x;
    const double y = ly - view->y;
    if (x < -left || x >= w + right) {
        return hit;
    }
    if (y < -top || y >= h + bottom) {
        return hit;
    }
    // Titlebar buttons
    if (titlebar_on && y >= -title_h + btn_margin && y < -title_h + btn_margin + btn_size) {
        const struct fbwl_server *server = view->server;
        const size_t right_len = server != NULL ? server->titlebar_right_len : 0;
        const size_t left_len = server != NULL ? server->titlebar_left_len : 0;
        int btn_x = w - btn_margin - btn_size;
        for (size_t i = right_len; i-- > 0; ) {
            const enum fbwl_decor_hit_kind kind = server->titlebar_right[i];
            if (!view_decor_button_allowed(view, kind)) {
                continue;
            }
            if (x >= btn_x && x < btn_x + btn_size) {
                hit.kind = kind;
                return hit;
            }
            btn_x -= btn_size + theme->button_spacing;
        }
        btn_x = btn_margin;
        for (size_t i = 0; i < left_len; i++) {
            const enum fbwl_decor_hit_kind kind = server->titlebar_left[i];
            if (!view_decor_button_allowed(view, kind)) {
                continue;
            }
            if (x >= btn_x && x < btn_x + btn_size) {
                hit.kind = kind;
                return hit;
            }
            btn_x += btn_size + theme->button_spacing;
        }
    }
    // Titlebar drag
    if (titlebar_on && y >= -title_h && y < 0) {
        hit.kind = FBWL_DECOR_HIT_TITLEBAR;
        return hit;
    }
    uint32_t edges = WLR_EDGE_NONE;
    if (border > 0 && x >= -border && x < 0) {
        edges |= WLR_EDGE_LEFT;
    }
    if (border > 0 && x >= w && x < w + border) {
        edges |= WLR_EDGE_RIGHT;
    }
    if (border > 0 && y >= -title_h - border && y < -title_h) {
        edges |= WLR_EDGE_TOP;
    }
    if (bottom > 0 && y >= h && y < h + bottom) {
        edges |= WLR_EDGE_BOTTOM;
    }
    if (edges != WLR_EDGE_NONE) {
        hit.kind = FBWL_DECOR_HIT_RESIZE;
        hit.edges = edges;
    }
    return hit;
}

bool fbwl_view_tabs_bar_contains(const struct fbwl_view *view, double lx, double ly) {
    if (view == NULL || view->decor_tree == NULL || !view->decor_enabled || view->fullscreen || (view->decor_mask & FBWL_DECORM_TAB) == 0) {
        return false;
    }
    if (view->decor_tab_count < 1 || view->decor_tab_item_lx == NULL || view->decor_tab_item_w == NULL) {
        return false;
    }

    const int w = view->decor_tabs_w;
    const int h = view->decor_tabs_h;
    if (w < 1 || h < 1) {
        return false;
    }

    const int x = (int)(lx - view->x);
    const int y = (int)(ly - view->y);
    return x >= view->decor_tabs_x && y >= view->decor_tabs_y &&
        x < view->decor_tabs_x + w && y < view->decor_tabs_y + h;
}

bool fbwl_view_tabs_index_at(const struct fbwl_view *view, double lx, double ly, int *out_tab_index0) {
    if (out_tab_index0 != NULL) {
        *out_tab_index0 = -1;
    }

    if (view == NULL || !fbwl_view_tabs_bar_contains(view, lx, ly)) {
        return false;
    }

    const int x = (int)(lx - view->x);
    const int y = (int)(ly - view->y);
    const int main = view->decor_tabs_vertical ? (y - view->decor_tabs_y) : (x - view->decor_tabs_x);
    if (main < 0) {
        return false;
    }

    for (size_t i = 0; i < view->decor_tab_count; i++) {
        const int off = view->decor_tab_item_lx[i];
        const int len = view->decor_tab_item_w[i];
        if (main >= off && main < off + len) {
            if (out_tab_index0 != NULL) {
                *out_tab_index0 = (int)i;
            }
            return true;
        }
    }

    return false;
}

void fbwl_view_cleanup(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }

    free(view->title_override);
    view->title_override = NULL;

    free(view->decor_title_text_cache);
    view->decor_title_text_cache = NULL;
    view->decor_title_text_cache_w = 0;
    view->decor_title_text_cache_active = false;

    free(view->decor_tab_item_lx);
    view->decor_tab_item_lx = NULL;
    free(view->decor_tab_item_w);
    view->decor_tab_item_w = NULL;
    view->decor_tab_count = 0;

    view->decor_tabs_tree = NULL;
    view->decor_tabs_x = 0;
    view->decor_tabs_y = 0;
    view->decor_tabs_w = 0;
    view->decor_tabs_h = 0;
    view->decor_tabs_vertical = false;
}

struct alpha_apply_ctx {
    struct fbwl_view *view;
    float opacity;
};

static void alpha_apply_iter(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data) {
    (void)sx;
    (void)sy;
    if (buffer == NULL || user_data == NULL) {
        return;
    }
    const struct alpha_apply_ctx *ctx = user_data;
    struct fbwl_view *view = ctx != NULL ? ctx->view : NULL;
    const float opacity = ctx != NULL ? ctx->opacity : 1.0f;
    if (view != NULL && view->pseudo_bg.image != NULL && buffer == view->pseudo_bg.image) {
        return;
    }
    wlr_scene_buffer_set_opacity(buffer, opacity);
}
void fbwl_view_alpha_apply(struct fbwl_view *view) {
    if (view == NULL || view->scene_tree == NULL || !view->alpha_set) {
        return;
    }
    const uint8_t alpha = view->decor_active ? view->alpha_focused : view->alpha_unfocused;
    const float opacity = alpha_to_opacity(alpha);
    struct alpha_apply_ctx ctx = { .view = view, .opacity = opacity };
    wlr_scene_node_for_each_buffer(&view->scene_tree->node, alpha_apply_iter, &ctx);
}
void fbwl_view_set_alpha(struct fbwl_view *view, uint8_t focused, uint8_t unfocused, const char *why) {
    if (view == NULL) {
        return;
    }
    view->alpha_set = true;
    view->alpha_focused = focused;
    view->alpha_unfocused = unfocused;
    fbwl_view_alpha_apply(view);
    fbwl_view_pseudo_bg_update(view, why != NULL ? why : "alpha-set");
    wlr_log(WLR_INFO, "Alpha: %s focused=%u unfocused=%u reason=%s",
        fbwl_view_display_title(view),
        (unsigned int)focused,
        (unsigned int)unfocused,
        why != NULL ? why : "(null)");
}
void fbwl_view_set_shaded(struct fbwl_view *view, bool shaded, const char *why) {
    if (view == NULL) {
        return;
    }
    if (view->fullscreen && shaded) {
        wlr_log(WLR_INFO, "Shade: ignoring request while fullscreen title=%s reason=%s",
            fbwl_view_display_title(view),
            why != NULL ? why : "(null)");
        return;
    }
    if (shaded == view->shaded) {
        return;
    }

    struct fbwl_server *server = view->server;
    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);

    view->shaded = shaded;
    if (view->content_tree != NULL) {
        wlr_scene_node_set_enabled(&view->content_tree->node, !shaded);
    }
    if (view->server != NULL) {
        fbwl_view_decor_update(view, &view->server->decor_theme);
    }
    wlr_log(WLR_INFO, "Shade: %s %s reason=%s",
        fbwl_view_display_title(view),
        shaded ? "on" : "off",
        why != NULL ? why : "(null)");
    server_strict_mousefocus_recheck_after_restack(server, before, shaded ? "shade-on" : "shade-off");
    if (server != NULL) {
        server_toolbar_ui_rebuild(server);
    }
}
