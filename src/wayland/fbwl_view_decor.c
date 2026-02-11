#include "wayland/fbwl_view.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_icon_theme.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_menu_icon.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_xwayland_icon.h"

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

enum fbwl_tabs_edge {
    FBWL_TABS_EDGE_TOP = 0,
    FBWL_TABS_EDGE_BOTTOM,
    FBWL_TABS_EDGE_LEFT,
    FBWL_TABS_EDGE_RIGHT,
};

static enum fbwl_tabs_edge tabs_placement_edge(enum fbwl_toolbar_placement placement) {
    switch (placement) {
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        return FBWL_TABS_EDGE_TOP;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
        return FBWL_TABS_EDGE_LEFT;
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
        return FBWL_TABS_EDGE_RIGHT;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
    default:
        return FBWL_TABS_EDGE_BOTTOM;
    }
}

enum fbwl_tabs_align_3 {
    FBWL_TABS_ALIGN_MIN = 0,
    FBWL_TABS_ALIGN_CENTER,
    FBWL_TABS_ALIGN_MAX,
};

static enum fbwl_tabs_align_3 tabs_placement_align_for_edge(enum fbwl_toolbar_placement placement, enum fbwl_tabs_edge edge,
        bool intitlebar) {
    if (intitlebar) {
        switch (placement) {
        case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
        case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
            return FBWL_TABS_ALIGN_CENTER;
        case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
        case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
            return FBWL_TABS_ALIGN_MAX;
        default:
            return FBWL_TABS_ALIGN_MIN;
        }
    }

    if (edge == FBWL_TABS_EDGE_TOP || edge == FBWL_TABS_EDGE_BOTTOM) {
        switch (placement) {
        case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
        case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
            return FBWL_TABS_ALIGN_CENTER;
        case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
            return FBWL_TABS_ALIGN_MAX;
        default:
            return FBWL_TABS_ALIGN_MIN;
        }
    }

    switch (placement) {
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
        return FBWL_TABS_ALIGN_CENTER;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
        return FBWL_TABS_ALIGN_MAX;
    default:
        return FBWL_TABS_ALIGN_MIN;
    }
}

static const struct fbwl_tabs_config *view_tabs_cfg(const struct fbwl_view *view) {
    if (view == NULL || view->server == NULL) {
        return NULL;
    }
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(view->server, view);
    return cfg != NULL ? &cfg->tabs : &view->server->tabs;
}

static void view_tabs_ui_clear(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }

    if (view->decor_tabs_tree != NULL) {
        wlr_scene_node_destroy(&view->decor_tabs_tree->node);
        view->decor_tabs_tree = NULL;
    }

    free(view->decor_tab_item_lx);
    view->decor_tab_item_lx = NULL;
    free(view->decor_tab_item_w);
    view->decor_tab_item_w = NULL;
    view->decor_tab_count = 0;

    view->decor_tabs_x = 0;
    view->decor_tabs_y = 0;
    view->decor_tabs_w = 0;
    view->decor_tabs_h = 0;
    view->decor_tabs_vertical = false;
}

static void view_tabs_ui_build(struct fbwl_view *view, const struct fbwl_decor_theme *theme) {
    if (view == NULL || theme == NULL || view->server == NULL || view->decor_tree == NULL) {
        return;
    }

    view_tabs_ui_clear(view);

    if (!view->decor_enabled || view->fullscreen || view->tab_group == NULL) {
        return;
    }

    const struct fbwl_tabs_config *tabs = view_tabs_cfg(view);
    if (tabs == NULL) {
        return;
    }

    const size_t tab_count = fbwl_tabs_group_mapped_count(view);
    if (tab_count < 2) {
        return;
    }

    const int w = fbwl_view_current_width(view);
    int h = fbwl_view_current_height(view);
    if (w < 1 || h < 0) {
        return;
    }
    if (view->shaded) {
        h = 0;
    }

    int border = theme->border_width;
    if (border < 0) {
        border = 0;
    }

    int title_h = theme->title_height;
    if (title_h < 1) {
        title_h = 24;
    }

    const enum fbwl_tabs_edge edge = tabs_placement_edge(tabs->placement);
    const bool vertical = !tabs->intitlebar && (edge == FBWL_TABS_EDGE_LEFT || edge == FBWL_TABS_EDGE_RIGHT);

    int thickness = title_h;
    if (vertical) {
        thickness = tabs->width_px;
        if (thickness < 1) {
            thickness = title_h;
        }
        if (thickness < 1) {
            thickness = 1;
        }
    }

    int frame_x = 0;
    int frame_y = 0;
    int frame_w = w;
    int frame_h = h;
    if (view->decor_enabled) {
        frame_x = -border;
        frame_y = -title_h - border;
        frame_w = w + 2 * border;
        frame_h = h + title_h + 2 * border;
    }
    if (frame_w < 1 || frame_h < 1) {
        return;
    }

    int region_x = 0;
    int region_y = 0;
    int region_w = 0;
    int region_h = 0;

    if (tabs->intitlebar) {
        // Inside titlebar: always horizontal.
        const int btn_size = decor_theme_button_size(theme);
        const size_t left_len = view->server->titlebar_left_len;
        const size_t right_len = view->server->titlebar_right_len;
        const int reserved_left = theme->button_margin +
            (left_len > 0 ? ((int)left_len * btn_size) + ((int)(left_len - 1) * theme->button_spacing) + theme->button_margin : 0);
        const int reserved_right = theme->button_margin +
            (right_len > 0 ? ((int)right_len * btn_size) + ((int)(right_len - 1) * theme->button_spacing) + theme->button_margin : 0);

        region_x = reserved_left;
        region_y = -title_h;
        region_w = w - reserved_left - reserved_right;
        region_h = title_h;
        if (region_w < 1) {
            region_x = 0;
            region_w = w;
        }
    } else {
        if (edge == FBWL_TABS_EDGE_TOP) {
            region_x = frame_x;
            region_y = frame_y - (thickness + border);
            region_w = frame_w;
            region_h = thickness;
        } else if (edge == FBWL_TABS_EDGE_BOTTOM) {
            region_x = frame_x;
            region_y = frame_y + frame_h + border;
            region_w = frame_w;
            region_h = thickness;
        } else if (edge == FBWL_TABS_EDGE_LEFT) {
            region_x = frame_x - (thickness + border);
            region_y = frame_y;
            region_w = thickness;
            region_h = frame_h;
        } else {
            region_x = frame_x + frame_w + border;
            region_y = frame_y;
            region_w = thickness;
            region_h = frame_h;
        }
    }

    if (region_w < 1 || region_h < 1) {
        return;
    }

    int avail_main = vertical ? region_h : region_w;
    if (avail_main < 1) {
        return;
    }

    int per = 0;
    if (vertical) {
        per = title_h;
    } else {
        per = tabs->width_px;
    }

    const int max_per = (int)(avail_main / (int)tab_count);
    if (max_per < 1) {
        return;
    }

    bool distribute = false;
    if (per < 1) {
        per = max_per;
        distribute = true;
    }
    if (per > max_per) {
        per = max_per;
        distribute = true;
    }
    if (per < 1) {
        per = 1;
    }

    int used_main = per * (int)tab_count;
    int rem = avail_main - used_main;
    if (rem < 0) {
        rem = 0;
    }
    if (!distribute) {
        rem = 0;
    }

    const int bar_main = used_main + rem;
    int align_gap = avail_main - bar_main;
    if (align_gap < 0) {
        align_gap = 0;
    }
    const enum fbwl_tabs_align_3 align =
        tabs_placement_align_for_edge(tabs->placement, edge, tabs->intitlebar);
    int align_off = 0;
    if (align == FBWL_TABS_ALIGN_CENTER) {
        align_off = align_gap / 2;
    } else if (align == FBWL_TABS_ALIGN_MAX) {
        align_off = align_gap;
    }

    int bar_x = region_x;
    int bar_y = region_y;
    int bar_w = region_w;
    int bar_h = region_h;
    if (vertical) {
        bar_y = region_y + align_off;
        bar_h = bar_main;
    } else {
        bar_x = region_x + align_off;
        bar_w = bar_main;
    }

    view->decor_tabs_tree = wlr_scene_tree_create(view->decor_tree);
    if (view->decor_tabs_tree == NULL) {
        view_tabs_ui_clear(view);
        return;
    }

    view->decor_tabs_x = bar_x;
    view->decor_tabs_y = bar_y;
    view->decor_tabs_w = bar_w;
    view->decor_tabs_h = bar_h;
    view->decor_tabs_vertical = vertical;

    view->decor_tab_item_lx = calloc(tab_count, sizeof(*view->decor_tab_item_lx));
    view->decor_tab_item_w = calloc(tab_count, sizeof(*view->decor_tab_item_w));
    if (view->decor_tab_item_lx == NULL || view->decor_tab_item_w == NULL) {
        view_tabs_ui_clear(view);
        return;
    }
    view->decor_tab_count = tab_count;

    const int pad = tabs->padding_px > 0 ? tabs->padding_px : 0;
    int icon_px = title_h >= 18 ? title_h - 8 : title_h;
    if (icon_px < 8) {
        icon_px = 8;
    }
    if (icon_px > title_h) {
        icon_px = title_h;
    }
    if (icon_px > 64) {
        icon_px = 64;
    }

    struct fbwl_view *active_tab = fbwl_tabs_group_active_view(view);
    if (active_tab == NULL) {
        active_tab = view;
    }

    for (size_t i = 0; i < tab_count; i++) {
        const int extra = (int)i < rem ? 1 : 0;
        const int len = per + extra;

        const int off = (int)i * per + ((int)i < rem ? (int)i : rem);
        view->decor_tab_item_lx[i] = off;
        view->decor_tab_item_w[i] = len;

        const int item_x = vertical ? bar_x : bar_x + off;
        const int item_y = vertical ? bar_y + off : bar_y;
        const int item_w = vertical ? thickness : len;
        const int item_h = vertical ? len : thickness;

        struct fbwl_view *tab_view = fbwl_tabs_group_mapped_at(view, i);
        if (tab_view == NULL) {
            continue;
        }

        const bool is_active_tab = tab_view == active_tab;
        const float *bg = NULL;
        const float *fg = NULL;
        if (is_active_tab) {
            bg = view->decor_active ? theme->titlebar_active : theme->titlebar_inactive;
            fg = view->decor_active ? theme->title_text_active : theme->title_text_inactive;
        } else {
            bg = theme->titlebar_inactive;
            fg = theme->title_text_inactive;
        }

        struct wlr_scene_rect *rect = wlr_scene_rect_create(view->decor_tabs_tree, item_w, item_h, bg);
        if (rect != NULL) {
            wlr_scene_node_set_position(&rect->node, item_x, item_y);
        }

        int text_x = item_x;
        int text_w = item_w;
        bool icon_loaded = false;

        if (!vertical && tabs->use_pixmap) {
            struct wlr_buffer *icon_buf = NULL;
            if (tab_view->type == FBWL_VIEW_XWAYLAND && view->server->xwayland != NULL) {
                icon_buf = fbwl_xwayland_icon_buffer_create(view->server->xwayland, tab_view->xwayland_surface, icon_px);
            }
            if (icon_buf == NULL) {
                const char *icon_name = fbwl_view_app_id(tab_view);
                char *icon_path = fbwl_icon_theme_resolve_path(icon_name);
                if (icon_path != NULL) {
                    icon_buf = fbwl_ui_menu_icon_buffer_create(icon_path, icon_px);
                    free(icon_path);
                }
            }
            if (icon_buf != NULL) {
                struct wlr_scene_buffer *sb_icon = wlr_scene_buffer_create(view->decor_tabs_tree, icon_buf);
                if (sb_icon != NULL) {
                    const int ix = item_x + pad;
                    const int iy = item_y + (item_h > icon_px ? (item_h - icon_px) / 2 : 0);
                    wlr_scene_node_set_position(&sb_icon->node, ix, iy);
                    icon_loaded = true;
                }
                wlr_buffer_drop(icon_buf);
            }
        }

        if (icon_loaded) {
            text_x = item_x + pad + icon_px;
            text_w = item_w - (pad + icon_px);
            if (text_w < 1) {
                text_w = 1;
            }
        }

        const char *label = fbwl_view_display_title(tab_view);
        struct wlr_buffer *buf = fbwl_text_buffer_create(label, text_w, item_h, pad, fg, theme->window_font);
        if (buf != NULL) {
            struct wlr_scene_buffer *sb = wlr_scene_buffer_create(view->decor_tabs_tree, buf);
            if (sb != NULL) {
                wlr_scene_node_set_position(&sb->node, text_x, item_y);
            }
            wlr_buffer_drop(buf);
        }

        wlr_log(WLR_INFO, "TabsUI: tab idx=%zu title=%s active=%d lx=%d ly=%d w=%d h=%d",
            i, label != NULL ? label : "",
            is_active_tab ? 1 : 0,
            view->x + item_x, view->y + item_y, item_w, item_h);
    }

    wlr_log(WLR_INFO, "TabsUI: bar title=%s intitlebar=%d placement=%s vertical=%d x=%d y=%d w=%d h=%d tabs=%zu",
        fbwl_view_display_title(view),
        tabs->intitlebar ? 1 : 0,
        fbwl_toolbar_placement_str(tabs->placement),
        vertical ? 1 : 0,
        view->x + bar_x, view->y + bar_y, bar_w, bar_h, tab_count);
}

void fbwl_view_decor_apply_enabled(struct fbwl_view *view) {
    if (view == NULL || view->decor_tree == NULL) {
        return;
    }
    const bool show = view->decor_enabled && !view->fullscreen;
    wlr_scene_node_set_enabled(&view->decor_tree->node, show);
}
void fbwl_view_decor_set_active(struct fbwl_view *view, const struct fbwl_decor_theme *theme, bool active) {
    if (view == NULL || theme == NULL) {
        return;
    }
    view->decor_active = active;
    if (view->decor_titlebar != NULL) {
        const float *color = active ? theme->titlebar_active : theme->titlebar_inactive;
        wlr_scene_rect_set_color(view->decor_titlebar, color);
    }
    fbwl_view_decor_update_title_text(view, theme);
    view_tabs_ui_build(view, theme);
    fbwl_view_alpha_apply(view);
}
void fbwl_view_decor_update_title_text(struct fbwl_view *view, const struct fbwl_decor_theme *theme) {
    if (view == NULL || view->decor_tree == NULL || theme == NULL) {
        return;
    }
    const int title_h = theme->title_height;
    const int w = fbwl_view_current_width(view);
    if (w < 1 || title_h < 1) {
        return;
    }
    if (view->decor_title_text == NULL) {
        view->decor_title_text = wlr_scene_buffer_create(view->decor_tree, NULL);
        if (view->decor_title_text == NULL) {
            return;
        }
    }
    if (!view->decor_enabled || view->fullscreen) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }

    const struct fbwl_tabs_config *tabs = view_tabs_cfg(view);
    if (tabs != NULL && tabs->intitlebar && view->tab_group != NULL && fbwl_tabs_group_mapped_count(view) >= 2) {
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
    const size_t left_len = view->server != NULL ? view->server->titlebar_left_len : 0;
    const size_t right_len = view->server != NULL ? view->server->titlebar_right_len : 0;
    const int reserved_left = theme->button_margin +
        (left_len > 0 ? ((int)left_len * btn_size) + ((int)(left_len - 1) * theme->button_spacing) + theme->button_margin : 0);
    const int reserved_right = theme->button_margin +
        (right_len > 0 ? ((int)right_len * btn_size) + ((int)(right_len - 1) * theme->button_spacing) + theme->button_margin : 0);
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
    struct wlr_buffer *buf = fbwl_text_buffer_create(title, text_w, title_h, 8, fg, theme->window_font);
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
    const int border = theme->border_width;
    const int title_h = theme->title_height;
    if (view->decor_titlebar != NULL) {
        wlr_scene_rect_set_size(view->decor_titlebar, w, title_h);
        wlr_scene_node_set_position(&view->decor_titlebar->node, 0, -title_h);
        const float *color = view->decor_active ? theme->titlebar_active : theme->titlebar_inactive;
        wlr_scene_rect_set_color(view->decor_titlebar, color);
    }
    if (view->decor_border_top != NULL) {
        wlr_scene_rect_set_size(view->decor_border_top, w + 2 * border, border);
        wlr_scene_node_set_position(&view->decor_border_top->node, -border, -title_h - border);
    }
    if (view->decor_border_bottom != NULL) {
        wlr_scene_rect_set_size(view->decor_border_bottom, w + 2 * border, border);
        wlr_scene_node_set_position(&view->decor_border_bottom->node, -border, h);
    }
    if (view->decor_border_left != NULL) {
        wlr_scene_rect_set_size(view->decor_border_left, border, title_h + border + h + border);
        wlr_scene_node_set_position(&view->decor_border_left->node, -border, -title_h - border);
    }
    if (view->decor_border_right != NULL) {
        wlr_scene_rect_set_size(view->decor_border_right, border, title_h + border + h + border);
        wlr_scene_node_set_position(&view->decor_border_right->node, w, -title_h - border);
    }
    const int btn_size = decor_theme_button_size(theme);
    const int btn_y = -title_h + theme->button_margin;
    struct wlr_scene_rect *btns[] = { view->decor_btn_menu, view->decor_btn_shade, view->decor_btn_stick,
        view->decor_btn_close, view->decor_btn_max, view->decor_btn_min, view->decor_btn_lhalf, view->decor_btn_rhalf };
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
        if (btns[i] != NULL) {
            wlr_scene_node_set_enabled(&btns[i]->node, false);
        }
    }

    const struct fbwl_server *server = view->server;
    if (server != NULL) {
        int btn_x = theme->button_margin;
        for (size_t i = 0; i < server->titlebar_left_len; i++) {
            struct wlr_scene_rect *rect = decor_button_rect(view, server->titlebar_left[i]);
            if (rect != NULL) {
                wlr_scene_rect_set_size(rect, btn_size, btn_size);
                wlr_scene_node_set_position(&rect->node, btn_x, btn_y);
                wlr_scene_node_set_enabled(&rect->node, true);
                btn_x += btn_size + theme->button_spacing;
            }
        }
        btn_x = w - theme->button_margin - btn_size;
        for (size_t i = server->titlebar_right_len; i-- > 0; ) {
            struct wlr_scene_rect *rect = decor_button_rect(view, server->titlebar_right[i]);
            if (rect != NULL) {
                wlr_scene_rect_set_size(rect, btn_size, btn_size);
                wlr_scene_node_set_position(&rect->node, btn_x, btn_y);
                wlr_scene_node_set_enabled(&rect->node, true);
                btn_x -= btn_size + theme->button_spacing;
            }
        }
    }
    fbwl_view_decor_update_title_text(view, theme);
    view_tabs_ui_build(view, theme);
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
    const int w = fbwl_view_current_width(view);
    int h = fbwl_view_current_height(view);
    if (w < 1 || (!view->shaded && h < 1)) {
        return hit;
    }
    if (view->shaded) {
        h = 0;
    }
    const int border = theme->border_width;
    const int title_h = theme->title_height;
    const int btn_size = decor_theme_button_size(theme);
    const int btn_margin = theme->button_margin;
    const double x = lx - view->x;
    const double y = ly - view->y;
    if (x < -border || x >= w + border) {
        return hit;
    }
    if (y < -title_h - border || y >= h + border) {
        return hit;
    }
    // Titlebar buttons
    if (y >= -title_h + btn_margin && y < -title_h + btn_margin + btn_size) {
        const struct fbwl_server *server = view->server;
        const size_t right_len = server != NULL ? server->titlebar_right_len : 0;
        const size_t left_len = server != NULL ? server->titlebar_left_len : 0;
        int btn_x = w - btn_margin - btn_size;
        for (size_t i = right_len; i-- > 0; ) {
            if (x >= btn_x && x < btn_x + btn_size) {
                hit.kind = server->titlebar_right[i];
                return hit;
            }
            btn_x -= btn_size + theme->button_spacing;
        }
        btn_x = btn_margin;
        for (size_t i = 0; i < left_len; i++) {
            if (x >= btn_x && x < btn_x + btn_size) {
                hit.kind = server->titlebar_left[i];
                return hit;
            }
            btn_x += btn_size + theme->button_spacing;
        }
    }
    // Titlebar drag
    if (y >= -title_h && y < 0) {
        hit.kind = FBWL_DECOR_HIT_TITLEBAR;
        return hit;
    }
    uint32_t edges = WLR_EDGE_NONE;
    if (x >= -border && x < 0) {
        edges |= WLR_EDGE_LEFT;
    }
    if (x >= w && x < w + border) {
        edges |= WLR_EDGE_RIGHT;
    }
    if (y >= -title_h - border && y < -title_h) {
        edges |= WLR_EDGE_TOP;
    }
    if (y >= h && y < h + border) {
        edges |= WLR_EDGE_BOTTOM;
    }
    if (edges != WLR_EDGE_NONE) {
        hit.kind = FBWL_DECOR_HIT_RESIZE;
        hit.edges = edges;
    }
    return hit;
}

bool fbwl_view_tabs_bar_contains(const struct fbwl_view *view, double lx, double ly) {
    if (view == NULL || view->decor_tree == NULL || !view->decor_enabled || view->fullscreen) {
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
