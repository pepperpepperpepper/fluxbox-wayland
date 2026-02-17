#include "wayland/fbwl_view_decor_internal.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_deco_mask.h"
#include "wayland/fbwl_round_corners.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_texture.h"
#include "wayland/fbwl_ui_decor_icons.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"

int fbwl_view_decor_button_size(const struct fbwl_decor_theme *theme) {
    if (theme == NULL) {
        return 0;
    }
    int bevel = theme->window_bevel_width;
    if (bevel < 0) {
        bevel = 0;
    }
    if (bevel > 20) {
        bevel = 20;
    }
    if (theme->title_height > 0) {
        const int max_bevel = (theme->title_height - 1) / 2;
        if (bevel > max_bevel) {
            bevel = max_bevel;
        }
    }
    int size = theme->title_height - 2 * bevel;
    if (size < 1) {
        size = 1;
    }
    return size;
}

int fbwl_view_decor_window_bevel_px(const struct fbwl_decor_theme *theme, int title_h) {
    if (theme == NULL || title_h < 1) {
        return 0;
    }
    int bevel = theme->window_bevel_width;
    if (bevel < 0) {
        bevel = 0;
    }
    if (bevel > 20) {
        bevel = 20;
    }
    const int max_bevel = (title_h - 1) / 2;
    if (bevel > max_bevel) {
        bevel = max_bevel;
    }
    return bevel;
}

bool fbwl_view_decor_texture_can_use_flat_rect(const struct fbwl_texture *tex) {
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

bool fbwl_view_decor_button_allowed(const struct fbwl_view *view, enum fbwl_decor_hit_kind kind) {
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

size_t fbwl_view_decor_visible_buttons(const struct fbwl_view *view,
        const enum fbwl_decor_hit_kind *buttons, size_t len) {
    if (view == NULL || buttons == NULL || len == 0) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (fbwl_view_decor_button_allowed(view, buttons[i])) {
            n++;
        }
    }
    return n;
}

struct wlr_scene_rect *fbwl_view_decor_button_rect(struct fbwl_view *view, enum fbwl_decor_hit_kind kind) {
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

struct wlr_scene_buffer *fbwl_view_decor_button_tex(struct fbwl_view *view, enum fbwl_decor_hit_kind kind) {
    if (view == NULL) {
        return NULL;
    }
    switch (kind) {
    case FBWL_DECOR_HIT_BTN_MENU:
        return view->decor_btn_menu_tex;
    case FBWL_DECOR_HIT_BTN_SHADE:
        return view->decor_btn_shade_tex;
    case FBWL_DECOR_HIT_BTN_STICK:
        return view->decor_btn_stick_tex;
    case FBWL_DECOR_HIT_BTN_CLOSE:
        return view->decor_btn_close_tex;
    case FBWL_DECOR_HIT_BTN_MAX:
        return view->decor_btn_max_tex;
    case FBWL_DECOR_HIT_BTN_MIN:
        return view->decor_btn_min_tex;
    case FBWL_DECOR_HIT_BTN_LHALF:
        return view->decor_btn_lhalf_tex;
    case FBWL_DECOR_HIT_BTN_RHALF:
        return view->decor_btn_rhalf_tex;
    default:
        return NULL;
    }
}

struct wlr_scene_buffer *fbwl_view_decor_button_icon(struct fbwl_view *view, enum fbwl_decor_hit_kind kind) {
    if (view == NULL) {
        return NULL;
    }
    switch (kind) {
    case FBWL_DECOR_HIT_BTN_MENU:
        return view->decor_btn_menu_icon;
    case FBWL_DECOR_HIT_BTN_SHADE:
        return view->decor_btn_shade_icon;
    case FBWL_DECOR_HIT_BTN_STICK:
        return view->decor_btn_stick_icon;
    case FBWL_DECOR_HIT_BTN_CLOSE:
        return view->decor_btn_close_icon;
    case FBWL_DECOR_HIT_BTN_MAX:
        return view->decor_btn_max_icon;
    case FBWL_DECOR_HIT_BTN_MIN:
        return view->decor_btn_min_icon;
    case FBWL_DECOR_HIT_BTN_LHALF:
        return view->decor_btn_lhalf_icon;
    case FBWL_DECOR_HIT_BTN_RHALF:
        return view->decor_btn_rhalf_icon;
    default:
        return NULL;
    }
}

static const struct fbwl_tabs_config *view_tabs_cfg(const struct fbwl_view *view) {
    if (view == NULL || view->server == NULL) {
        return NULL;
    }
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(view->server, view);
    return cfg != NULL ? &cfg->tabs : &view->server->tabs;
}

static bool pixmap_triplet_any_set(const struct fbwl_pixmap_triplet *pm) {
    if (pm == NULL) {
        return false;
    }
    return pm->focus[0] != '\0' || pm->unfocus[0] != '\0' || pm->pressed[0] != '\0';
}

static const char *pixmap_triplet_pick(const struct fbwl_pixmap_triplet *pm, bool active) {
    if (pm == NULL) {
        return NULL;
    }
    if (!active && pm->unfocus[0] != '\0') {
        return pm->unfocus;
    }
    if (pm->focus[0] != '\0') {
        return pm->focus;
    }
    if (pm->unfocus[0] != '\0') {
        return pm->unfocus;
    }
    return NULL;
}

static const char *view_decor_button_icon_pixmap(const struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        enum fbwl_decor_hit_kind kind, bool active, bool pressed, bool *out_toggled) {
    if (out_toggled != NULL) {
        *out_toggled = false;
    }
    if (view == NULL || theme == NULL) {
        return NULL;
    }

    bool toggled = false;
    const struct fbwl_pixmap_triplet *pm = NULL;
    switch (kind) {
    case FBWL_DECOR_HIT_BTN_MENU:
        pm = &theme->window_menuicon_pm;
        break;
    case FBWL_DECOR_HIT_BTN_SHADE:
        toggled = view->shaded;
        if (toggled && pixmap_triplet_any_set(&theme->window_unshade_pm)) {
            pm = &theme->window_unshade_pm;
        } else {
            pm = &theme->window_shade_pm;
        }
        break;
    case FBWL_DECOR_HIT_BTN_STICK:
        toggled = view->wm_view.sticky;
        if (toggled && pixmap_triplet_any_set(&theme->window_stuck_pm)) {
            pm = &theme->window_stuck_pm;
        } else {
            pm = &theme->window_stick_pm;
        }
        break;
    case FBWL_DECOR_HIT_BTN_CLOSE:
        pm = &theme->window_close_pm;
        break;
    case FBWL_DECOR_HIT_BTN_MAX:
        toggled = view->maximized || view->maximized_h || view->maximized_v || view->fullscreen;
        pm = &theme->window_maximize_pm;
        break;
    case FBWL_DECOR_HIT_BTN_MIN:
        pm = &theme->window_iconify_pm;
        break;
    case FBWL_DECOR_HIT_BTN_LHALF:
        pm = &theme->window_lhalf_pm;
        break;
    case FBWL_DECOR_HIT_BTN_RHALF:
        pm = &theme->window_rhalf_pm;
        break;
    default:
        break;
    }

    if (out_toggled != NULL) {
        *out_toggled = toggled;
    }
    if ((pressed || toggled) && pm != NULL && pm->pressed[0] != '\0') {
        return pm->pressed;
    }
    return pixmap_triplet_pick(pm, active);
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
        view->decor_title_text_cache_h = 0;
        return;
    }

    const struct fbwl_tabs_config *tabs = view_tabs_cfg(view);
    if (tabs != NULL && tabs->intitlebar && (view->decor_mask & FBWL_DECORM_TAB) != 0 &&
            view->tab_group != NULL && fbwl_tabs_group_mapped_count(view) >= 2) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        view->decor_title_text_cache_h = 0;
        return;
    }

    const char *title = fbwl_view_title(view);
    if (title == NULL || *title == '\0') {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        view->decor_title_text_cache_h = 0;
        return;
    }
    const int bevel = fbwl_view_decor_window_bevel_px(theme, title_h);
    const int btn_size = fbwl_view_decor_button_size(theme);
    const struct fbwl_server *server = view->server;
    const size_t left_len = server != NULL ? server->titlebar_left_len : 0;
    const size_t right_len = server != NULL ? server->titlebar_right_len : 0;
    const size_t left_vis = server != NULL ? fbwl_view_decor_visible_buttons(view, server->titlebar_left, left_len) : 0;
    const size_t right_vis = server != NULL ? fbwl_view_decor_visible_buttons(view, server->titlebar_right, right_len) : 0;
    const int reserved_left = 2 * bevel + ((int)left_vis * (btn_size + bevel));
    const int reserved_right = bevel + ((int)right_vis * (btn_size + bevel));
    int text_x = reserved_left;
    int text_w = w - reserved_left - reserved_right;
    if (text_w < 1) {
        text_x = 0;
        text_w = w;
    }
    if (text_w < 1) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        view->decor_title_text_cache_h = 0;
        return;
    }
    wlr_scene_node_set_position(&view->decor_title_text->node, text_x, -title_h + bevel);
    if (view->decor_title_text_cache != NULL &&
            view->decor_title_text_cache_w == text_w &&
            view->decor_title_text_cache_h == btn_size &&
            view->decor_title_text_cache_active == view->decor_active &&
            strcmp(view->decor_title_text_cache, title) == 0) {
        return;
    }
    free(view->decor_title_text_cache);
    view->decor_title_text_cache = strdup(title);
    if (view->decor_title_text_cache == NULL) {
        view->decor_title_text_cache_w = 0;
        view->decor_title_text_cache_h = 0;
        view->decor_title_text_cache_active = false;
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        return;
    }
    view->decor_title_text_cache_w = text_w;
    view->decor_title_text_cache_h = btn_size;
    view->decor_title_text_cache_active = view->decor_active;
    const float *fg = view->decor_active ? theme->title_text_active : theme->title_text_inactive;
    const struct fbwl_text_effect *effect =
        view->decor_active ? &theme->window_label_focus_effect : &theme->window_label_unfocus_effect;
    struct wlr_buffer *buf = fbwl_text_buffer_create(title, text_w, btn_size, 8, fg, theme->window_font, effect,
        theme->window_justify);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        view->decor_title_text_cache_h = 0;
        view->decor_title_text_cache_active = false;
        return;
    }
    uint32_t round_mask = 0;
    int frame_x = 0, frame_y = 0, frame_w = 0, frame_h = 0;
    if (fbwl_view_decor_round_frame_geom(view, theme, &round_mask, &frame_x, &frame_y, &frame_w, &frame_h)) {
        const int off_x = text_x - frame_x;
        const int off_y = (-title_h + bevel) - frame_y;
        buf = fbwl_round_corners_mask_buffer_owned(buf, off_x, off_y, frame_w, frame_h, round_mask);
    }
    wlr_scene_buffer_set_buffer(view->decor_title_text, buf);
    wlr_buffer_drop(buf);
    wlr_log(WLR_INFO, "Decor: title-render %s justify=%d", title, theme->window_justify);
}

void fbwl_view_decor_button_apply(struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        enum fbwl_decor_hit_kind kind,
        int btn_x, int btn_y, int btn_size,
        uint32_t round_mask, int frame_x, int frame_y, int frame_w, int frame_h) {
    if (view == NULL || theme == NULL) {
        return;
    }
    const bool round_on = round_mask != 0 && frame_w > 0 && frame_h > 0;

    const bool active = view->decor_active;
    const bool pressed = view->server != NULL && view->server->decor_button_pressed_view == view &&
        view->server->decor_button_pressed_kind == kind;

    bool toggled = false;
    const char *pixmap = view_decor_button_icon_pixmap(view, theme, kind, active, pressed, &toggled);
    const struct fbwl_texture *btn_tex =
        (pressed || toggled) ? &theme->window_button_pressed_tex :
        (active ? &theme->window_button_focus_tex : &theme->window_button_unfocus_tex);

    struct wlr_scene_rect *rect = fbwl_view_decor_button_rect(view, kind);
    if (rect != NULL && !round_on) {
        wlr_scene_rect_set_size(rect, btn_size, btn_size);
        wlr_scene_node_set_position(&rect->node, btn_x, btn_y);
        wlr_scene_node_set_enabled(&rect->node, true);
    } else if (rect != NULL) {
        wlr_scene_node_set_enabled(&rect->node, false);
    }

    const bool parentrel = fbwl_texture_is_parentrelative(btn_tex);
    const bool use_rect = fbwl_view_decor_texture_can_use_flat_rect(btn_tex);
    bool use_buffer = !use_rect && !parentrel;
    if (round_on) {
        use_buffer = true;
    }

    struct wlr_scene_buffer *tex_node = fbwl_view_decor_button_tex(view, kind);
    if (tex_node != NULL) {
        wlr_scene_node_set_enabled(&tex_node->node, use_buffer);
        if (use_buffer) {
            struct wlr_buffer *buf = NULL;
            if (parentrel) {
                const float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                const int off_x = btn_x - frame_x;
                const int off_y = btn_y - frame_y;
                buf = fbwl_view_decor_solid_color_buffer_masked(btn_size, btn_size, transparent,
                    round_mask, off_x, off_y, frame_w, frame_h);
            } else {
                buf = fbwl_texture_render_buffer(btn_tex, btn_size, btn_size);
                if (round_on) {
                    const int off_x = btn_x - frame_x;
                    const int off_y = btn_y - frame_y;
                    buf = fbwl_round_corners_mask_buffer_owned(buf, off_x, off_y, frame_w, frame_h, round_mask);
                }
            }
            if (buf != NULL) {
                wlr_scene_buffer_set_buffer(tex_node, buf);
                wlr_buffer_drop(buf);
                wlr_scene_buffer_set_dest_size(tex_node, btn_size, btn_size);
                wlr_scene_node_set_position(&tex_node->node, btn_x, btn_y);
            } else {
                use_buffer = false;
                wlr_scene_node_set_enabled(&tex_node->node, false);
                wlr_scene_buffer_set_buffer(tex_node, NULL);
            }
        } else {
            wlr_scene_buffer_set_buffer(tex_node, NULL);
        }
    }

    if (rect != NULL && !round_on) {
        if (use_rect || (!use_buffer && !parentrel)) {
            wlr_scene_rect_set_color(rect, btn_tex->color);
        } else {
            float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            wlr_scene_rect_set_color(rect, c);
        }
    }

    struct wlr_buffer *icon_buf = fbwl_decor_icon_render_pixmap(pixmap, btn_size);
    if (icon_buf == NULL) {
        icon_buf = fbwl_decor_icon_render_builtin(kind, toggled, btn_size, btn_tex->pic_color);
    }
    if (icon_buf != NULL && round_on) {
        const int off_x = btn_x - frame_x;
        const int off_y = btn_y - frame_y;
        icon_buf = fbwl_round_corners_mask_buffer_owned(icon_buf, off_x, off_y, frame_w, frame_h, round_mask);
    }

    struct wlr_scene_buffer *icon_node = fbwl_view_decor_button_icon(view, kind);
    if (icon_node != NULL) {
        if (icon_buf != NULL) {
            wlr_scene_buffer_set_buffer(icon_node, icon_buf);
            wlr_buffer_drop(icon_buf);
            wlr_scene_buffer_set_dest_size(icon_node, btn_size, btn_size);
            wlr_scene_node_set_position(&icon_node->node, btn_x, btn_y);
            wlr_scene_node_set_enabled(&icon_node->node, true);
        } else {
            wlr_scene_buffer_set_buffer(icon_node, NULL);
            wlr_scene_node_set_enabled(&icon_node->node, false);
        }
    } else if (icon_buf != NULL) {
        wlr_buffer_drop(icon_buf);
    }
}
