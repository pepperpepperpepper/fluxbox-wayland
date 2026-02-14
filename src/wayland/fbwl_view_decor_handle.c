#include "wayland/fbwl_view.h"

#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "wayland/fbwl_texture.h"
#include "wayland/fbwl_ui_decor_theme.h"

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

static void decor_apply_texture(struct wlr_scene_rect *rect, struct wlr_scene_buffer *tex_node,
        const struct fbwl_texture *tex, bool show, int x, int y, int w, int h) {
    if (rect != NULL) {
        wlr_scene_node_set_enabled(&rect->node, show);
        if (show) {
            wlr_scene_rect_set_size(rect, w, h);
            wlr_scene_node_set_position(&rect->node, x, y);
        }
    }

    if (tex_node == NULL) {
        if (rect != NULL && show && tex != NULL) {
            wlr_scene_rect_set_color(rect, tex->color);
        }
        return;
    }

    const bool parentrel = fbwl_texture_is_parentrelative(tex);
    const bool use_rect = decor_texture_can_use_flat_rect(tex);
    bool use_buffer = show && !use_rect && !parentrel;

    wlr_scene_node_set_enabled(&tex_node->node, use_buffer);
    if (use_buffer) {
        struct wlr_buffer *buf = fbwl_texture_render_buffer(tex, w, h);
        if (buf != NULL) {
            wlr_scene_buffer_set_buffer(tex_node, buf);
            wlr_buffer_drop(buf);
            wlr_scene_buffer_set_dest_size(tex_node, w, h);
            wlr_scene_node_set_position(&tex_node->node, x, y);
        } else {
            use_buffer = false;
            wlr_scene_node_set_enabled(&tex_node->node, false);
            wlr_scene_buffer_set_buffer(tex_node, NULL);
        }
    } else {
        wlr_scene_buffer_set_buffer(tex_node, NULL);
    }

    if (rect != NULL && show && tex != NULL) {
        if (use_rect || (!use_buffer && !parentrel)) {
            wlr_scene_rect_set_color(rect, tex->color);
        } else {
            float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            wlr_scene_rect_set_color(rect, c);
        }
    }
}

void fbwl_view_decor_handle_update(struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        bool border_visible, bool handle_on, int w, int h, int border_px) {
    if (view == NULL || theme == NULL) {
        return;
    }

    int handle_h = theme->handle_width;
    if (handle_h < 0) {
        handle_h = 0;
    }
    const bool show_handle = handle_on && w > 0 && handle_h > 0;

    if (!show_handle) {
        if (view->decor_handle != NULL) {
            wlr_scene_node_set_enabled(&view->decor_handle->node, false);
        }
        if (view->decor_handle_tex != NULL) {
            wlr_scene_node_set_enabled(&view->decor_handle_tex->node, false);
            wlr_scene_buffer_set_buffer(view->decor_handle_tex, NULL);
        }
        if (view->decor_grip_left != NULL) {
            wlr_scene_node_set_enabled(&view->decor_grip_left->node, false);
        }
        if (view->decor_grip_left_tex != NULL) {
            wlr_scene_node_set_enabled(&view->decor_grip_left_tex->node, false);
            wlr_scene_buffer_set_buffer(view->decor_grip_left_tex, NULL);
        }
        if (view->decor_grip_right != NULL) {
            wlr_scene_node_set_enabled(&view->decor_grip_right->node, false);
        }
        if (view->decor_grip_right_tex != NULL) {
            wlr_scene_node_set_enabled(&view->decor_grip_right_tex->node, false);
            wlr_scene_buffer_set_buffer(view->decor_grip_right_tex, NULL);
        }
        return;
    }

    int handle_y = h;
    if (border_visible && border_px > 0) {
        handle_y += border_px;
    }

    const struct fbwl_texture *handle_tex =
        view->decor_active ? &theme->window_handle_focus_tex : &theme->window_handle_unfocus_tex;
    if (fbwl_texture_is_parentrelative(handle_tex)) {
        const struct fbwl_texture *fallback =
            view->decor_active ? &theme->window_title_focus_tex : &theme->window_title_unfocus_tex;
        if (!fbwl_texture_is_parentrelative(fallback)) {
            handle_tex = fallback;
        }
    }

    decor_apply_texture(view->decor_handle, view->decor_handle_tex, handle_tex, true, 0, handle_y, w, handle_h);

    int grip_w = 20;
    if (grip_w * 2 > w) {
        grip_w = w / 2;
    }

    const struct fbwl_texture *grip_tex =
        view->decor_active ? &theme->window_grip_focus_tex : &theme->window_grip_unfocus_tex;
    const bool show_grips = grip_w > 0 && !fbwl_texture_is_parentrelative(grip_tex);
    decor_apply_texture(view->decor_grip_left, view->decor_grip_left_tex, grip_tex, show_grips,
        0, handle_y, grip_w, handle_h);
    decor_apply_texture(view->decor_grip_right, view->decor_grip_right_tex, grip_tex, show_grips,
        w - grip_w, handle_y, grip_w, handle_h);
}

