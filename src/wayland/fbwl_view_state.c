#include "wayland/fbwl_view.h"

#include <stdlib.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_deco_mask.h"
#include "wayland/fbwl_server_internal.h"

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

    free(view->xwayland_role_cache);
    view->xwayland_role_cache = NULL;

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

static float alpha_to_opacity(uint8_t alpha) {
    return (float)alpha / 255.0f;
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
