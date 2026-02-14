#include "wayland/fbwl_ui_slit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_string_list.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_slit_order.h"
#include "wayland/fbwl_view.h"

enum fbwl_slit_pending_action {
    FBWL_SLIT_PENDING_HIDE = 1u << 0,
    FBWL_SLIT_PENDING_RAISE = 1u << 1,
    FBWL_SLIT_PENDING_LOWER = 1u << 2,
};

enum fbwl_slit_edge {
    FBWL_SLIT_EDGE_TOP,
    FBWL_SLIT_EDGE_BOTTOM,
    FBWL_SLIT_EDGE_LEFT,
    FBWL_SLIT_EDGE_RIGHT,
};

static enum fbwl_slit_edge slit_placement_edge(enum fbwl_toolbar_placement placement) {
    switch (placement) {
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        return FBWL_SLIT_EDGE_TOP;
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        return FBWL_SLIT_EDGE_BOTTOM;
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
        return FBWL_SLIT_EDGE_LEFT;
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        return FBWL_SLIT_EDGE_RIGHT;
    default:
        return FBWL_SLIT_EDGE_BOTTOM;
    }
}

// For top/bottom: align is left/center/right.
// For left/right: align is top/center/bottom.
static int slit_placement_align(enum fbwl_toolbar_placement placement) {
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

static const char *slit_edge_str(enum fbwl_slit_edge edge) {
    switch (edge) {
    case FBWL_SLIT_EDGE_TOP:
        return "top";
    case FBWL_SLIT_EDGE_BOTTOM:
        return "bottom";
    case FBWL_SLIT_EDGE_LEFT:
        return "left";
    case FBWL_SLIT_EDGE_RIGHT:
        return "right";
    default:
        return "bottom";
    }
}

const char *fbwl_slit_direction_str(enum fbwl_slit_direction dir) {
    switch (dir) {
    case FBWL_SLIT_DIR_HORIZONTAL:
        return "Horizontal";
    case FBWL_SLIT_DIR_VERTICAL:
    default:
        return "Vertical";
    }
}

static int clamp_i16(int v) {
    if (v < -32768) {
        return -32768;
    }
    if (v > 32767) {
        return 32767;
    }
    return v;
}

static void color_with_alpha(float out[4], const float in[4], uint8_t alpha) {
    if (out == NULL) {
        return;
    }
    const float a = (float)alpha / 255.0f;
    out[0] = in != NULL ? in[0] : 0.0f;
    out[1] = in != NULL ? in[1] : 0.0f;
    out[2] = in != NULL ? in[2] : 0.0f;
    out[3] = (in != NULL ? in[3] : 1.0f) * a;
}

static bool slit_is_topmost_at(const struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, int lx, int ly) {
    if (ui == NULL || env == NULL || env->scene == NULL || ui->tree == NULL) {
        return true;
    }

    double sx = 0, sy = 0;
    struct wlr_scene_node *node =
        wlr_scene_node_at(&env->scene->tree.node, (double)lx, (double)ly, &sx, &sy);
    for (struct wlr_scene_node *walk = node; walk != NULL; walk = walk->parent != NULL ? &walk->parent->node : NULL) {
        if (walk == &ui->tree->node) {
            return true;
        }
    }
    return false;
}

static void slit_hidden_offset(const struct fbwl_slit_ui *ui, int *dx, int *dy) {
    if (dx != NULL) {
        *dx = 0;
    }
    if (dy != NULL) {
        *dy = 0;
    }
    if (ui == NULL || !ui->hidden) {
        return;
    }
    const enum fbwl_slit_edge edge = slit_placement_edge(ui->placement);
    const bool vertical = edge == FBWL_SLIT_EDGE_LEFT || edge == FBWL_SLIT_EDGE_RIGHT;
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
    case FBWL_SLIT_EDGE_TOP:
        if (dy != NULL) {
            *dy = -delta;
        }
        break;
    case FBWL_SLIT_EDGE_BOTTOM:
        if (dy != NULL) {
            *dy = delta;
        }
        break;
    case FBWL_SLIT_EDGE_LEFT:
        if (dx != NULL) {
            *dx = -delta;
        }
        break;
    case FBWL_SLIT_EDGE_RIGHT:
        if (dx != NULL) {
            *dx = delta;
        }
        break;
    }
}

static struct fbwl_slit_item *slit_find_item(struct fbwl_slit_ui *ui, const struct fbwl_view *view) {
    if (ui == NULL || view == NULL) {
        return NULL;
    }
    struct fbwl_slit_item *it;
    wl_list_for_each(it, &ui->items, link) {
        if (it->view == view) {
            return it;
        }
    }
    return NULL;
}

static size_t slit_mapped_client_count(const struct fbwl_slit_ui *ui) {
    if (ui == NULL) {
        return 0;
    }
    size_t n = 0;
    const struct fbwl_slit_item *it;
    wl_list_for_each(it, &ui->items, link) {
        if (it->visible && it->view != NULL && it->view->mapped) {
            n++;
        }
    }
    return n;
}

static struct wlr_output *slit_output(const struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env) {
    if (ui == NULL || env == NULL || env->output_layout == NULL || env->outputs == NULL) {
        return NULL;
    }
    const size_t on_head = ui->on_head >= 0 ? (size_t)ui->on_head : 0;
    struct wlr_output *out = fbwl_screen_map_output_for_screen(env->output_layout, env->outputs, on_head);
    if (out == NULL) {
        out = wlr_output_layout_get_center_output(env->output_layout);
    }
    return out;
}

static void slit_compute_size(struct fbwl_slit_ui *ui, int *out_w, int *out_h) {
    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    if (ui == NULL || out_w == NULL || out_h == NULL) {
        return;
    }

    int bevel = ui->bevel_w;
    if (bevel < 0) {
        bevel = 0;
    }
    if (bevel > 20) {
        bevel = 20;
    }
    int w = 0;
    int h = 0;
    size_t n = 0;
    struct fbwl_slit_item *it;
    wl_list_for_each(it, &ui->items, link) {
        if (!it->visible) {
            continue;
        }
        struct fbwl_view *view = it->view;
        if (view == NULL || !view->mapped) {
            continue;
        }
        const int vw = fbwl_view_current_width(view);
        const int vh = fbwl_view_current_height(view);
        if (vw < 1 || vh < 1) {
            continue;
        }
        if (ui->direction == FBWL_SLIT_DIR_VERTICAL) {
            if (vw > w) {
                w = vw;
            }
            h += vh + bevel;
        } else {
            if (vh > h) {
                h = vh;
            }
            w += vw + bevel;
        }
        n++;
    }
    if (w < 1) {
        w = 1;
    } else {
        w += bevel;
    }
    if (h < 1) {
        h = 1;
    } else {
        h += bevel * 2;
    }
    *out_w = w;
    *out_h = h;
}

static void fbwl_ui_slit_apply_position(struct fbwl_slit_ui *ui) {
    if (ui == NULL || !ui->enabled || ui->tree == NULL) {
        return;
    }

    int dx = 0;
    int dy = 0;
    slit_hidden_offset(ui, &dx, &dy);
    const int x = ui->base_x + dx;
    const int y = ui->base_y + dy;
    if (x == ui->x && y == ui->y) {
        return;
    }
    ui->x = x;
    ui->y = y;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
    const bool parentrel = ui->pseudo_decor_theme != NULL && fbwl_texture_is_parentrelative(&ui->pseudo_decor_theme->slit_tex);
    const bool pseudo = parentrel || (ui->pseudo_force_pseudo_transparency && ui->alpha < 255);
    if (pseudo) {
        fbwl_pseudo_bg_update(&ui->pseudo_bg, ui->tree, ui->pseudo_output_layout,
            ui->x, ui->y, 0, 0, ui->width, ui->height,
            ui->pseudo_wallpaper_mode, ui->pseudo_wallpaper_buf, ui->pseudo_background_color);
    } else {
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    }
}

static void slit_sync_client_geometries(const struct fbwl_slit_ui *ui) {
    if (ui == NULL) {
        return;
    }
    struct fbwl_slit_item *it;
    wl_list_for_each(it, &ui->items, link) {
        if (!it->visible) {
            continue;
        }
        struct fbwl_view *view = it->view;
        if (view == NULL || view->scene_tree == NULL) {
            continue;
        }
        int gx = 0;
        int gy = 0;
        (void)wlr_scene_node_coords(&view->scene_tree->node, &gx, &gy);
        view->x = gx;
        view->y = gy;
        fbwl_view_pseudo_bg_update(view, "slit-sync-geometries");

        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            int vw = fbwl_view_current_width(view);
            int vh = fbwl_view_current_height(view);
            if (vw < 1) {
                vw = view->xwayland_surface->width;
            }
            if (vh < 1) {
                vh = view->xwayland_surface->height;
            }
            if (vw < 1) {
                vw = 1;
            }
            if (vh < 1) {
                vh = 1;
            }

            wlr_xwayland_surface_configure(view->xwayland_surface,
                (int16_t)clamp_i16(view->x), (int16_t)clamp_i16(view->y),
                (uint16_t)vw, (uint16_t)vh);
        }
    }
}

static void slit_cancel_auto_timer(struct fbwl_slit_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->auto_timer != NULL) {
        wl_event_source_remove(ui->auto_timer);
        ui->auto_timer = NULL;
    }
    ui->auto_pending = 0;
}

static void slit_update_layout(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, const char *why) {
    if (ui == NULL || env == NULL) {
        return;
    }
    ui->pseudo_output_layout = env->output_layout;
    ui->pseudo_wallpaper_mode = env->wallpaper_mode;
    ui->pseudo_wallpaper_buf = env->wallpaper_buf;
    ui->pseudo_background_color = env->background_color;
    ui->pseudo_decor_theme = env->decor_theme;
    ui->pseudo_force_pseudo_transparency = env->force_pseudo_transparency;
    int border_w = env->decor_theme != NULL ? env->decor_theme->slit_border_width : 0;
    if (border_w < 0) {
        border_w = 0;
    }
    if (border_w > 20) {
        border_w = 20;
    }
    int bevel_w = env->decor_theme != NULL ? env->decor_theme->slit_bevel_width : 0;
    if (bevel_w < 0) {
        bevel_w = 0;
    }
    if (bevel_w > 20) {
        bevel_w = 20;
    }
    ui->border_w = border_w;
    ui->bevel_w = bevel_w;

    const size_t mapped = slit_mapped_client_count(ui);
    if (!ui->enabled || mapped == 0) {
        ui->width = 0;
        ui->height = 0;
        ui->thickness = 0;
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
        if (ui->tree != NULL) {
            wlr_scene_node_set_enabled(&ui->tree->node, false);
        }
        return;
    }

    if (ui->tree == NULL && env->layer_tree != NULL) {
        ui->tree = wlr_scene_tree_create(env->layer_tree);
        if (ui->tree == NULL) {
            return;
        }
        ui->bg = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->bg != NULL) {
            wlr_scene_node_lower_to_bottom(&ui->bg->node);
        }
    }

    if (ui->tree != NULL && env->layer_tree != NULL && ui->tree->node.parent != env->layer_tree) {
        wlr_scene_node_reparent(&ui->tree->node, env->layer_tree);
    }

    if (!ui->enabled || ui->tree == NULL || mapped == 0) {
        ui->width = 0;
        ui->height = 0;
        ui->thickness = 0;
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
        if (ui->tree != NULL) {
            wlr_scene_node_set_enabled(&ui->tree->node, false);
        }
        return;
    }

    int w = 0;
    int h = 0;
    slit_compute_size(ui, &w, &h);
    if (w < 1 || h < 1) {
        ui->width = 0;
        ui->height = 0;
        ui->thickness = 0;
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
        wlr_scene_node_set_enabled(&ui->tree->node, false);
        return;
    }

    const int inner_w = w;
    const int inner_h = h;
    ui->width = inner_w + 2 * border_w;
    ui->height = inner_h + 2 * border_w;
    const enum fbwl_slit_edge edge = slit_placement_edge(ui->placement);
    ui->thickness = (edge == FBWL_SLIT_EDGE_LEFT || edge == FBWL_SLIT_EDGE_RIGHT) ? ui->width : ui->height;

    struct wlr_output *out = slit_output(ui, env);
    struct wlr_box box = {0};
    if (out != NULL && env->output_layout != NULL) {
        wlr_output_layout_get_box(env->output_layout, out, &box);
    }
    if (box.width < 1 || box.height < 1) {
        return;
    }

    int base_x = box.x;
    int base_y = box.y;
    const int align = slit_placement_align(ui->placement);

    if (edge == FBWL_SLIT_EDGE_LEFT || edge == FBWL_SLIT_EDGE_RIGHT) {
        if (ui->height > 0 && ui->height < box.height) {
            if (align == 2) {
                base_y = box.y + box.height - ui->height;
            } else if (align == 1) {
                base_y = box.y + (box.height - ui->height) / 2;
            }
        }
        if (edge == FBWL_SLIT_EDGE_RIGHT) {
            base_x = box.x + box.width - ui->width;
        }
    } else {
        if (ui->width > 0 && ui->width < box.width) {
            if (align == 2) {
                base_x = box.x + box.width - ui->width;
            } else if (align == 1) {
                base_x = box.x + (box.width - ui->width) / 2;
            }
        }
        if (edge == FBWL_SLIT_EDGE_BOTTOM) {
            base_y = box.y + box.height - ui->height;
        }
    }

    if (base_x < box.x) {
        base_x = box.x;
    }
    if (base_y < box.y) {
        base_y = box.y;
    }

    ui->base_x = base_x;
    ui->base_y = base_y;

    wlr_scene_node_set_enabled(&ui->tree->node, true);
    fbwl_ui_slit_apply_position(ui);

    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, border_w, border_w);
        wlr_scene_node_lower_to_bottom(&ui->bg->node);
        const float alpha = (float)ui->alpha / 255.0f;
        const bool parentrel = env->decor_theme != NULL && fbwl_texture_is_parentrelative(&env->decor_theme->slit_tex);
        if (env->decor_theme != NULL && !parentrel) {
            struct wlr_buffer *buf = fbwl_texture_render_buffer(&env->decor_theme->slit_tex, inner_w, inner_h);
            wlr_scene_buffer_set_buffer(ui->bg, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(ui->bg, inner_w, inner_h);
            wlr_scene_buffer_set_opacity(ui->bg, alpha);
            wlr_scene_node_set_enabled(&ui->bg->node, true);
        } else {
            wlr_scene_buffer_set_buffer(ui->bg, NULL);
            wlr_scene_node_set_enabled(&ui->bg->node, false);
        }
    }

    if (border_w > 0 && env->decor_theme != NULL) {
        const float alpha = (float)ui->alpha / 255.0f;
        float c[4] = {
            env->decor_theme->slit_border_color[0],
            env->decor_theme->slit_border_color[1],
            env->decor_theme->slit_border_color[2],
            env->decor_theme->slit_border_color[3] * alpha,
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
    const bool parentrel = ui->pseudo_decor_theme != NULL && fbwl_texture_is_parentrelative(&ui->pseudo_decor_theme->slit_tex);
    const bool pseudo = parentrel || (ui->pseudo_force_pseudo_transparency && ui->alpha < 255);
    if (pseudo) {
        fbwl_pseudo_bg_update(&ui->pseudo_bg, ui->tree, ui->pseudo_output_layout,
            ui->x, ui->y, 0, 0, ui->width, ui->height,
            ui->pseudo_wallpaper_mode, ui->pseudo_wallpaper_buf, ui->pseudo_background_color);
    } else {
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    }

    int xoff = 0;
    int yoff = 0;
    if (ui->direction == FBWL_SLIT_DIR_VERTICAL) {
        yoff = bevel_w;
    } else {
        xoff = bevel_w;
    }
    struct fbwl_slit_item *it;
    wl_list_for_each(it, &ui->items, link) {
        if (!it->visible) {
            continue;
        }
        struct fbwl_view *view = it->view;
        if (view == NULL || !view->mapped || view->scene_tree == NULL) {
            continue;
        }
        const int vw = fbwl_view_current_width(view);
        const int vh = fbwl_view_current_height(view);
        if (vw < 1 || vh < 1) {
            continue;
        }

        if (view->scene_tree->node.parent != ui->tree) {
            wlr_scene_node_reparent(&view->scene_tree->node, ui->tree);
        }

        int lx = 0;
        int ly = 0;
        if (ui->direction == FBWL_SLIT_DIR_VERTICAL) {
            lx = (inner_w - vw) / 2;
            ly = yoff;
        } else {
            lx = xoff;
            ly = (inner_h - vh) / 2;
        }
        wlr_scene_node_set_position(&view->scene_tree->node, border_w + lx, border_w + ly);
        view->placed = true;

        if (ui->direction == FBWL_SLIT_DIR_VERTICAL) {
            yoff += vh + bevel_w;
        } else {
            xoff += vw + bevel_w;
        }
    }

    slit_sync_client_geometries(ui);

    wlr_log(WLR_INFO,
        "Slit: layout why=%s edge=%s x=%d y=%d w=%d h=%d thickness=%d clients=%zu hidden=%d dir=%s alpha=%u maxOver=%d autoHide=%d",
        why != NULL ? why : "(null)",
        slit_edge_str(edge),
        ui->x, ui->y, ui->width, ui->height, ui->thickness,
        mapped,
        ui->hidden ? 1 : 0,
        fbwl_slit_direction_str(ui->direction),
        (unsigned)ui->alpha,
        ui->max_over ? 1 : 0,
        ui->auto_hide ? 1 : 0);

    size_t idx = 0;
    wl_list_for_each(it, &ui->items, link) {
        if (!it->visible) {
            continue;
        }
        struct fbwl_view *view = it->view;
        if (view == NULL || !view->mapped) {
            continue;
        }
        const int vw = fbwl_view_current_width(view);
        const int vh = fbwl_view_current_height(view);
        if (vw < 1 || vh < 1) {
            continue;
        }
        wlr_log(WLR_INFO, "Slit: item idx=%zu title=%s x=%d y=%d w=%d h=%d",
            idx++,
            fbwl_view_display_title(view),
            view->x, view->y, vw, vh);
    }
}

static int fbwl_ui_slit_auto_timer(void *data) {
    struct fbwl_slit_ui *ui = data;
    if (ui == NULL || !ui->enabled || ui->tree == NULL) {
        return 0;
    }
    const uint32_t pending = ui->auto_pending;
    ui->auto_pending = 0;

    if ((pending & FBWL_SLIT_PENDING_HIDE) != 0) {
        if (ui->auto_hide && !ui->hovered && !ui->hidden) {
            ui->hidden = true;
            fbwl_ui_slit_apply_position(ui);
            slit_sync_client_geometries(ui);
            wlr_log(WLR_INFO, "Slit: autoHide hide");
        }
    }
    if ((pending & FBWL_SLIT_PENDING_RAISE) != 0) {
        if (ui->auto_raise && ui->hovered) {
            wlr_scene_node_raise_to_top(&ui->tree->node);
            wlr_log(WLR_INFO, "Slit: autoRaise raise");
        }
    }
    if ((pending & FBWL_SLIT_PENDING_LOWER) != 0) {
        if (ui->auto_raise && !ui->hovered) {
            wlr_scene_node_lower_to_bottom(&ui->tree->node);
            wlr_log(WLR_INFO, "Slit: autoRaise lower");
        }
    }
    slit_cancel_auto_timer(ui);
    return 0;
}

void fbwl_ui_slit_init(struct fbwl_slit_ui *ui) {
    if (ui == NULL) {
        return;
    }
    *ui = (struct fbwl_slit_ui){
        .enabled = true,
        .placement = FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM,
        .on_head = 0,
        .layer_num = 4,
        .auto_hide = false,
        .auto_raise = false,
        .max_over = false,
        .accept_kde_dockapps = true,
        .hidden = false,
        .alpha = 255,
        .direction = FBWL_SLIT_DIR_VERTICAL,
        .x = 0,
        .y = 0,
        .base_x = 0,
        .base_y = 0,
        .thickness = 0,
        .width = 0,
        .height = 0,
        .auto_timer = NULL,
        .hovered = false,
        .auto_pending = 0,
        .tree = NULL,
        .bg = NULL,
        .items_len = 0,
    };
    wl_list_init(&ui->items);
}

void fbwl_ui_slit_destroy(struct fbwl_slit_ui *ui) {
    if (ui == NULL) {
        return;
    }

    slit_cancel_auto_timer(ui);
    fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    ui->pseudo_output_layout = NULL;
    ui->pseudo_wallpaper_mode = FBWL_WALLPAPER_MODE_STRETCH;
    ui->pseudo_wallpaper_buf = NULL;
    ui->pseudo_background_color = NULL;
    ui->pseudo_decor_theme = NULL;
    ui->pseudo_force_pseudo_transparency = false;

    struct fbwl_slit_item *it;
    struct fbwl_slit_item *tmp;
    wl_list_for_each_safe(it, tmp, &ui->items, link) {
        if (it->view != NULL && it->view->scene_tree != NULL && it->view->base_layer != NULL) {
            int gx = 0, gy = 0;
            (void)wlr_scene_node_coords(&it->view->scene_tree->node, &gx, &gy);
            wlr_scene_node_reparent(&it->view->scene_tree->node, it->view->base_layer);
            wlr_scene_node_set_position(&it->view->scene_tree->node, gx, gy);
            it->view->x = gx;
            it->view->y = gy;
            fbwl_view_pseudo_bg_update(it->view, "slit-destroy");
        }
        wl_list_remove(&it->link);
        free(it);
    }
    ui->items_len = 0;

    fbwl_string_list_free(ui->order, ui->order_len);
    ui->order = NULL;
    ui->order_len = 0;

    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
}

bool fbwl_ui_slit_attach_view(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, struct fbwl_view *view,
        const char *why) {
    if (ui == NULL || env == NULL || view == NULL) {
        return false;
    }
    if (!ui->enabled) {
        return false;
    }
    if (view->scene_tree == NULL) {
        return false;
    }
    if (slit_find_item(ui, view) != NULL) {
        return true;
    }

    struct fbwl_slit_item *it = calloc(1, sizeof(*it));
    if (it == NULL) {
        return false;
    }
    it->view = view;
    it->visible = true;
    fbwl_ui_slit_order_insert_item(ui, &ui->items, it);
    ui->items_len++;

    wlr_scene_node_set_enabled(&view->scene_tree->node, true);

    wlr_log(WLR_INFO, "Slit: attach title=%s why=%s", fbwl_view_display_title(view), why != NULL ? why : "(null)");
    slit_update_layout(ui, env, "attach");
    return true;
}

void fbwl_ui_slit_detach_view(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, struct fbwl_view *view,
        const char *why) {
    if (ui == NULL || env == NULL || view == NULL) {
        return;
    }
    struct fbwl_slit_item *it = slit_find_item(ui, view);
    if (it == NULL) {
        return;
    }

    if (view->scene_tree != NULL && view->base_layer != NULL) {
        int gx = 0, gy = 0;
        (void)wlr_scene_node_coords(&view->scene_tree->node, &gx, &gy);
        wlr_scene_node_reparent(&view->scene_tree->node, view->base_layer);
        wlr_scene_node_set_position(&view->scene_tree->node, gx, gy);
        view->x = gx;
        view->y = gy;
        fbwl_view_pseudo_bg_update(view, "slit-detach");
    }

    wl_list_remove(&it->link);
    free(it);
    if (ui->items_len > 0) {
        ui->items_len--;
    }

    wlr_log(WLR_INFO, "Slit: detach title=%s why=%s", fbwl_view_display_title(view), why != NULL ? why : "(null)");
    slit_update_layout(ui, env, "detach");
}

void fbwl_ui_slit_handle_view_commit(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, struct fbwl_view *view,
        const char *why) {
    if (ui == NULL || env == NULL || view == NULL) {
        return;
    }
    if (slit_find_item(ui, view) == NULL) {
        return;
    }
    slit_update_layout(ui, env, why != NULL ? why : "commit");
}

void fbwl_ui_slit_apply_view_geometry(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, struct fbwl_view *view,
        const char *why) {
    if (ui == NULL || env == NULL || view == NULL) {
        return;
    }
    if (slit_find_item(ui, view) == NULL) {
        return;
    }
    if (view->type != FBWL_VIEW_XWAYLAND || view->xwayland_surface == NULL) {
        return;
    }

    slit_sync_client_geometries(ui);
    wlr_log(WLR_INFO, "Slit: apply-geometry title=%s why=%s", fbwl_view_display_title(view), why != NULL ? why : "(null)");
}

void fbwl_ui_slit_rebuild(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env) {
    slit_update_layout(ui, env, "rebuild");
}

void fbwl_ui_slit_update_position(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env) {
    slit_update_layout(ui, env, "update-position");
}

void fbwl_ui_slit_handle_motion(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, int lx, int ly, int delay_ms) {
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
        hovered = slit_is_topmost_at(ui, env, lx, ly);
    }
    ui->hovered = hovered;

    if (hovered && !was_hovered) {
        slit_cancel_auto_timer(ui);
        if (ui->auto_hide && ui->hidden) {
            ui->hidden = false;
            fbwl_ui_slit_apply_position(ui);
            slit_sync_client_geometries(ui);
            wlr_log(WLR_INFO, "Slit: autoHide show");
        }
        if (ui->auto_raise) {
            ui->auto_pending |= FBWL_SLIT_PENDING_RAISE;
        }
    } else if (!hovered && was_hovered) {
        slit_cancel_auto_timer(ui);
        if (ui->auto_hide && !ui->hidden) {
            ui->auto_pending |= FBWL_SLIT_PENDING_HIDE;
        }
        if (ui->auto_raise) {
            ui->auto_pending |= FBWL_SLIT_PENDING_LOWER;
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
    ui->auto_timer = wl_event_loop_add_timer(loop, fbwl_ui_slit_auto_timer, ui);
    if (ui->auto_timer == NULL) {
        ui->auto_pending = 0;
        return;
    }
    if (delay_ms < 0) {
        delay_ms = 0;
    }
    wl_event_source_timer_update(ui->auto_timer, delay_ms);
}
