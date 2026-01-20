#include "wayland/fbwl_view.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wmcore/fbwm_output.h"
#include "wayland/fbwl_output.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"

struct wlr_surface *fbwl_view_wlr_surface(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }

    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->base->surface : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->surface : NULL;
    default:
        return NULL;
    }
}

struct fbwl_view *fbwl_view_from_surface(struct wlr_surface *surface) {
    if (surface == NULL) {
        return NULL;
    }

    struct wlr_xdg_toplevel *xdg_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(surface);
    if (xdg_toplevel != NULL && xdg_toplevel->base != NULL) {
        struct wlr_scene_tree *tree = xdg_toplevel->base->data;
        if (tree != NULL && tree->node.data != NULL) {
            return tree->node.data;
        }
    }

    struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
    if (xsurface != NULL && xsurface->data != NULL) {
        return xsurface->data;
    }

    return NULL;
}

const char *fbwl_view_title(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }

    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->title : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->title : NULL;
    default:
        return NULL;
    }
}

const char *fbwl_view_app_id(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }

    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->app_id : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->class : NULL;
    default:
        return NULL;
    }
}

const char *fbwl_view_display_title(const struct fbwl_view *view) {
    if (view == NULL) {
        return "(no-view)";
    }

    const char *title = fbwl_view_title(view);
    if (title != NULL && *title != '\0') {
        return title;
    }

    const char *app_id = fbwl_view_app_id(view);
    if (app_id != NULL && *app_id != '\0') {
        return app_id;
    }

    return "(no-title)";
}

int fbwl_view_current_width(const struct fbwl_view *view) {
    if (view == NULL) {
        return 0;
    }
    if (view->width > 0) {
        return view->width;
    }

    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    if (surface != NULL) {
        return surface->current.width;
    }

    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        return view->xwayland_surface->width;
    }
    return 0;
}

int fbwl_view_current_height(const struct fbwl_view *view) {
    if (view == NULL) {
        return 0;
    }
    if (view->height > 0) {
        return view->height;
    }

    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    if (surface != NULL) {
        return surface->current.height;
    }

    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        return view->xwayland_surface->height;
    }
    return 0;
}

void fbwl_view_set_activated(struct fbwl_view *view, bool activated) {
    if (view == NULL) {
        return;
    }

    switch (view->type) {
    case FBWL_VIEW_XDG:
        if (view->xdg_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(view->xdg_toplevel, activated);
        }
        break;
    case FBWL_VIEW_XWAYLAND:
        if (view->xwayland_surface != NULL) {
            wlr_xwayland_surface_activate(view->xwayland_surface, activated);
            if (activated) {
                wlr_xwayland_surface_offer_focus(view->xwayland_surface);
            }
        }
        break;
    default:
        break;
    }
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

    wlr_scene_node_set_position(&view->decor_title_text->node, 0, -title_h);

    if (!view->decor_enabled || view->fullscreen) {
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
    const int reserved_right =
        theme->button_margin + (btn_size * 3) + (theme->button_spacing * 2) + theme->button_margin;
    int text_w = w - reserved_right;
    if (text_w < 1) {
        text_w = w;
    }
    if (text_w < 1) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }

    if (view->decor_title_text_cache != NULL &&
            view->decor_title_text_cache_w == text_w &&
            strcmp(view->decor_title_text_cache, title) == 0) {
        return;
    }

    free(view->decor_title_text_cache);
    view->decor_title_text_cache = strdup(title);
    if (view->decor_title_text_cache == NULL) {
        view->decor_title_text_cache_w = 0;
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        return;
    }
    view->decor_title_text_cache_w = text_w;

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct wlr_buffer *buf = fbwl_text_buffer_create(title, text_w, title_h, 8, fg);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
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
    const int h = fbwl_view_current_height(view);
    if (w < 1 || h < 1) {
        return;
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
    int btn_x = w - theme->button_margin - btn_size;
    if (view->decor_btn_close != NULL) {
        wlr_scene_rect_set_size(view->decor_btn_close, btn_size, btn_size);
        wlr_scene_node_set_position(&view->decor_btn_close->node, btn_x, btn_y);
    }
    btn_x -= btn_size + theme->button_spacing;
    if (view->decor_btn_max != NULL) {
        wlr_scene_rect_set_size(view->decor_btn_max, btn_size, btn_size);
        wlr_scene_node_set_position(&view->decor_btn_max->node, btn_x, btn_y);
    }
    btn_x -= btn_size + theme->button_spacing;
    if (view->decor_btn_min != NULL) {
        wlr_scene_rect_set_size(view->decor_btn_min, btn_size, btn_size);
        wlr_scene_node_set_position(&view->decor_btn_min->node, btn_x, btn_y);
    }

    fbwl_view_decor_update_title_text(view, theme);
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
        view->decor_btn_close = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_close_color);
        view->decor_btn_max = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_max_color);
        view->decor_btn_min = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_min_color);
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
    const int h = fbwl_view_current_height(view);
    if (w < 1 || h < 1) {
        return hit;
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
        const int close_x0 = w - btn_margin - btn_size;
        const int max_x0 = close_x0 - theme->button_spacing - btn_size;
        const int min_x0 = max_x0 - theme->button_spacing - btn_size;

        if (x >= close_x0 && x < close_x0 + btn_size) {
            hit.kind = FBWL_DECOR_HIT_BTN_CLOSE;
            return hit;
        }
        if (x >= max_x0 && x < max_x0 + btn_size) {
            hit.kind = FBWL_DECOR_HIT_BTN_MAX;
            return hit;
        }
        if (x >= min_x0 && x < min_x0 + btn_size) {
            hit.kind = FBWL_DECOR_HIT_BTN_MIN;
            return hit;
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

void fbwl_view_save_geometry(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }

    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);

    view->saved_x = view->x;
    view->saved_y = view->y;
    view->saved_w = w > 0 ? w : 0;
    view->saved_h = h > 0 ? h : 0;
}

void fbwl_view_get_output_box(const struct fbwl_view *view, struct wlr_output_layout *output_layout,
        struct wlr_output *preferred, struct wlr_box *box) {
    *box = (struct wlr_box){0};
    if (view == NULL || output_layout == NULL) {
        return;
    }

    struct wlr_output *output = preferred;
    if (output == NULL) {
        const int w = fbwl_view_current_width(view);
        const int h = fbwl_view_current_height(view);
        double cx = view->x + (double)w / 2.0;
        double cy = view->y + (double)h / 2.0;
        output = wlr_output_layout_output_at(output_layout, cx, cy);
    }
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }

    wlr_output_layout_get_box(output_layout, output, box);
}

void fbwl_view_get_output_usable_box(const struct fbwl_view *view, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_output *preferred, struct wlr_box *box) {
    *box = (struct wlr_box){0};
    if (view == NULL || output_layout == NULL || outputs == NULL) {
        return;
    }

    struct wlr_output *output = preferred;
    if (output == NULL) {
        const int w = fbwl_view_current_width(view);
        const int h = fbwl_view_current_height(view);
        double cx = view->x + (double)w / 2.0;
        double cy = view->y + (double)h / 2.0;
        output = wlr_output_layout_output_at(output_layout, cx, cy);
    }
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }
    if (output == NULL) {
        return;
    }

    struct fbwl_output *out = fbwl_output_find(outputs, output);
    if (out != NULL && out->usable_area.width > 0 && out->usable_area.height > 0) {
        *box = out->usable_area;
        return;
    }

    wlr_output_layout_get_box(output_layout, output, box);
}

static void view_foreign_set_output(struct fbwl_view *view, struct wlr_output *output) {
    if (view == NULL || view->foreign_toplevel == NULL) {
        return;
    }

    if (output == view->foreign_output) {
        return;
    }

    if (view->foreign_output != NULL) {
        wlr_foreign_toplevel_handle_v1_output_leave(view->foreign_toplevel, view->foreign_output);
    }
    view->foreign_output = output;
    if (output != NULL) {
        wlr_foreign_toplevel_handle_v1_output_enter(view->foreign_toplevel, output);
    }
}

void fbwl_view_foreign_update_output_from_position(struct fbwl_view *view, struct wlr_output_layout *output_layout) {
    if (view == NULL || output_layout == NULL) {
        return;
    }

    struct wlr_output *output = wlr_output_layout_output_at(output_layout, view->x + 1, view->y + 1);
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }
    view_foreign_set_output(view, output);
}

struct fbwl_wm_output {
    struct fbwm_output wm_output;
    struct wlr_output_layout *output_layout;
    struct wl_list *outputs;
    struct wlr_output *wlr_output;
};

static const char *fbwl_wm_output_name(const struct fbwm_output *wm_output) {
    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    return output != NULL && output->wlr_output != NULL ? output->wlr_output->name : NULL;
}

static bool fbwl_wm_output_full_box(const struct fbwm_output *wm_output, struct fbwm_box *out) {
    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    if (output == NULL || output->output_layout == NULL || output->wlr_output == NULL || out == NULL) {
        return false;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(output->output_layout, output->wlr_output, &box);
    *out = (struct fbwm_box){
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height,
    };
    return true;
}

static bool fbwl_wm_output_usable_box(const struct fbwm_output *wm_output, struct fbwm_box *out) {
    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    if (output == NULL || output->outputs == NULL || output->wlr_output == NULL || out == NULL) {
        return false;
    }

    struct fbwl_output *out_data = fbwl_output_find(output->outputs, output->wlr_output);
    if (out_data == NULL || out_data->usable_area.width < 1 || out_data->usable_area.height < 1) {
        return false;
    }

    *out = (struct fbwm_box){
        .x = out_data->usable_area.x,
        .y = out_data->usable_area.y,
        .width = out_data->usable_area.width,
        .height = out_data->usable_area.height,
    };
    return true;
}

static const struct fbwm_output_ops fbwl_wm_output_ops = {
    .name = fbwl_wm_output_name,
    .full_box = fbwl_wm_output_full_box,
    .usable_box = fbwl_wm_output_usable_box,
};

void fbwl_view_place_initial(struct fbwl_view *view, struct fbwm_core *wm, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, double cursor_x, double cursor_y) {
    if (view == NULL || wm == NULL || output_layout == NULL) {
        return;
    }

    struct wlr_output *output = wlr_output_layout_output_at(output_layout, cursor_x, cursor_y);
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }

    struct fbwl_wm_output wm_out = {0};
    const struct fbwm_output *wm_output = NULL;
    if (output != NULL) {
        wm_out.output_layout = output_layout;
        wm_out.outputs = outputs;
        wm_out.wlr_output = output;
        fbwm_output_init(&wm_out.wm_output, &fbwl_wm_output_ops, &wm_out);
        wm_output = &wm_out.wm_output;
    }

    struct fbwm_box full = {0};
    struct fbwm_box box = {0};
    if (wm_output != NULL) {
        (void)fbwm_output_get_full_box(wm_output, &full);
        if (!fbwm_output_get_usable_box(wm_output, &box) || box.width < 1 || box.height < 1) {
            box = full;
        }
    }

    int x = 0;
    int y = 0;
    fbwm_core_place_next(wm, wm_output, &x, &y);

    view->x = x;
    view->y = y;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }

    wlr_log(WLR_INFO, "Place: %s out=%s x=%d y=%d usable=%d,%d %dx%d full=%d,%d %dx%d",
        fbwl_view_display_title(view),
        fbwm_output_name(wm_output) != NULL ? fbwm_output_name(wm_output) : "(none)",
        view->x, view->y,
        box.x, box.y, box.width, box.height,
        full.x, full.y, full.width, full.height);
    view_foreign_set_output(view, output);
}

void fbwl_view_set_maximized(struct fbwl_view *view, bool maximized, struct wlr_output_layout *output_layout,
        struct wl_list *outputs) {
    if (view == NULL) {
        return;
    }

    if (view->fullscreen) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    if (maximized == view->maximized) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    if (maximized) {
        fbwl_view_save_geometry(view);

        struct wlr_box box;
        fbwl_view_get_output_usable_box(view, output_layout, outputs, NULL, &box);
        if (box.width < 1 || box.height < 1) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
                wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
            }
            return;
        }

        view->maximized = true;
        view->x = box.x;
        view->y = box.y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
            wlr_scene_node_raise_to_top(&view->scene_tree->node);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, true);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_maximized(view->xwayland_surface, true, true);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, true);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, box.width, box.height);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)box.width, (uint16_t)box.height);
        }
        fbwl_view_foreign_update_output_from_position(view, output_layout);
        wlr_log(WLR_INFO, "Maximize: %s on w=%d h=%d", fbwl_view_display_title(view), box.width, box.height);
    } else {
        view->maximized = false;
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, false);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_maximized(view->xwayland_surface, false, false);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, false);
        }
        view->x = view->saved_x;
        view->y = view->saved_y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
            wlr_scene_node_raise_to_top(&view->scene_tree->node);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel,
                view->saved_w > 0 ? view->saved_w : 0,
                view->saved_h > 0 ? view->saved_h : 0);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)(view->saved_w > 0 ? view->saved_w : 1),
                (uint16_t)(view->saved_h > 0 ? view->saved_h : 1));
        }
        fbwl_view_foreign_update_output_from_position(view, output_layout);
        wlr_log(WLR_INFO, "Maximize: %s off", fbwl_view_display_title(view));
    }
}

void fbwl_view_set_fullscreen(struct fbwl_view *view, bool fullscreen, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_scene_tree *layer_normal, struct wlr_scene_tree *layer_fullscreen,
        struct wlr_output *output) {
    if (view == NULL) {
        return;
    }

    if (fullscreen == view->fullscreen) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    if (fullscreen) {
        if (view->maximized) {
            fbwl_view_set_maximized(view, false, output_layout, outputs);
        }

        fbwl_view_save_geometry(view);

        struct wlr_box box;
        fbwl_view_get_output_box(view, output_layout, output, &box);
        if (box.width < 1 || box.height < 1) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
                wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
            }
            return;
        }

        view->fullscreen = true;
        fbwl_view_decor_apply_enabled(view);
        if (layer_fullscreen != NULL && view->scene_tree != NULL) {
            wlr_scene_node_reparent(&view->scene_tree->node, layer_fullscreen);
        }
        view->x = box.x;
        view->y = box.y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
            wlr_scene_node_raise_to_top(&view->scene_tree->node);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, true);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, true);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, true);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, box.width, box.height);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)box.width, (uint16_t)box.height);
        }
        fbwl_view_foreign_update_output_from_position(view, output_layout);
        wlr_log(WLR_INFO, "Fullscreen: %s on w=%d h=%d", fbwl_view_display_title(view), box.width, box.height);
    } else {
        view->fullscreen = false;
        fbwl_view_decor_apply_enabled(view);
        if (layer_normal != NULL && view->scene_tree != NULL) {
            wlr_scene_node_reparent(&view->scene_tree->node, layer_normal);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, false);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, false);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, false);
        }
        view->x = view->saved_x;
        view->y = view->saved_y;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
            wlr_scene_node_raise_to_top(&view->scene_tree->node);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel,
                view->saved_w > 0 ? view->saved_w : 0,
                view->saved_h > 0 ? view->saved_h : 0);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)(view->saved_w > 0 ? view->saved_w : 1),
                (uint16_t)(view->saved_h > 0 ? view->saved_h : 1));
        }
        fbwl_view_foreign_update_output_from_position(view, output_layout);
        wlr_log(WLR_INFO, "Fullscreen: %s off", fbwl_view_display_title(view));
    }
}

struct fbwl_view *fbwl_view_at(struct wlr_scene *scene, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    *surface = NULL;
    *sx = 0;
    *sy = 0;

    if (scene == NULL) {
        return NULL;
    }

    double node_x = 0, node_y = 0;
    struct wlr_scene_node *node = wlr_scene_node_at(&scene->tree.node, lx, ly, &node_x, &node_y);
    if (node == NULL) {
        return NULL;
    }

    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *scene_surface =
            wlr_scene_surface_try_from_buffer(scene_buffer);
        if (scene_surface != NULL) {
            *surface = scene_surface->surface;
            *sx = node_x;
            *sy = node_y;
        }
    }

    struct wlr_scene_node *walk = node;
    while (walk != NULL && walk->data == NULL) {
        walk = walk->parent != NULL ? &walk->parent->node : NULL;
    }
    return walk != NULL ? walk->data : NULL;
}
