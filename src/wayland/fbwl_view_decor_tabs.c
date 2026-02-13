#include "wayland/fbwl_view_decor_tabs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_deco_mask.h"
#include "wayland/fbwl_icon_theme.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_ui_menu_icon.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_xwayland_icon.h"

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

void fbwl_view_decor_tabs_ui_build(struct fbwl_view *view, const struct fbwl_decor_theme *theme) {
    if (view == NULL || theme == NULL || view->server == NULL || view->decor_tree == NULL) {
        return;
    }

    view_tabs_ui_clear(view);

    if (!view->decor_enabled || view->fullscreen || view->tab_group == NULL || (view->decor_mask & FBWL_DECORM_TAB) == 0) {
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
    if ((view->decor_mask & FBWL_DECORM_BORDER) == 0) {
        border = 0;
    }

    int title_h = theme->title_height;
    if (title_h < 1) {
        title_h = 24;
    }

    const bool intitlebar = tabs->intitlebar && (view->decor_mask & FBWL_DECORM_TITLEBAR) != 0;
    const enum fbwl_tabs_edge edge = tabs_placement_edge(tabs->placement);
    const bool vertical = !intitlebar && (edge == FBWL_TABS_EDGE_LEFT || edge == FBWL_TABS_EDGE_RIGHT);

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

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    fbwl_view_decor_frame_extents(view, theme, &left, &top, &right, &bottom);

    int frame_x = -left;
    int frame_y = -top;
    int frame_w = w + left + right;
    int frame_h = h + top + bottom;
    if (frame_w < 1 || frame_h < 1) {
        return;
    }

    int region_x = 0;
    int region_y = 0;
    int region_w = 0;
    int region_h = 0;

    if (intitlebar) {
        // Inside titlebar: always horizontal.
        const int btn_size = decor_theme_button_size(theme);
        const size_t left_len = view->server->titlebar_left_len;
        const size_t right_len = view->server->titlebar_right_len;
        const size_t left_vis = view_decor_visible_buttons(view, view->server->titlebar_left, left_len);
        const size_t right_vis = view_decor_visible_buttons(view, view->server->titlebar_right, right_len);
        const int reserved_left = theme->button_margin +
            (left_vis > 0 ? ((int)left_vis * btn_size) + ((int)(left_vis - 1) * theme->button_spacing) + theme->button_margin : 0);
        const int reserved_right = theme->button_margin +
            (right_vis > 0 ? ((int)right_vis * btn_size) + ((int)(right_vis - 1) * theme->button_spacing) + theme->button_margin : 0);

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
        intitlebar ? 1 : 0,
        fbwl_toolbar_placement_str(tabs->placement),
        vertical ? 1 : 0,
        view->x + bar_x, view->y + bar_y, bar_w, bar_h, tab_count);
}

