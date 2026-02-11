#include "wayland/fbwl_ui_tooltip.h"

#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

static void fbwl_ui_tooltip_destroy_scene(struct fbwl_tooltip_ui *ui) {
    if (ui == NULL) {
        return;
    }
    fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->label = NULL;
}

static void fbwl_ui_tooltip_cancel_timer(struct fbwl_tooltip_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->timer != NULL) {
        wl_event_source_remove(ui->timer);
        ui->timer = NULL;
    }
    ui->pending = false;
}

void fbwl_ui_tooltip_hide(struct fbwl_tooltip_ui *ui, const char *why) {
    if (ui == NULL) {
        return;
    }

    const bool had = ui->pending || ui->visible;
    fbwl_ui_tooltip_cancel_timer(ui);

    ui->visible = false;
    free(ui->text);
    ui->text = NULL;

    if (ui->tree != NULL) {
        wlr_scene_node_set_enabled(&ui->tree->node, false);
    }
    if (had) {
        wlr_log(WLR_INFO, "Tooltip: hide reason=%s", why != NULL ? why : "(null)");
    }
}

static bool tooltip_has_env(const struct fbwl_tooltip_ui *ui) {
    if (ui == NULL) {
        return false;
    }
    return ui->env.scene != NULL && ui->env.layer_overlay != NULL && ui->env.wl_display != NULL &&
        ui->env.output_layout != NULL && ui->env.decor_theme != NULL;
}

static void fbwl_ui_tooltip_show_now(struct fbwl_tooltip_ui *ui) {
    if (ui == NULL || ui->text == NULL || *ui->text == '\0') {
        return;
    }
    if (!tooltip_has_env(ui)) {
        return;
    }

    struct wlr_output *out = wlr_output_layout_output_at(ui->env.output_layout, ui->anchor_x, ui->anchor_y);
    if (out == NULL) {
        return;
    }
    struct wlr_box box = {0};
    wlr_output_layout_get_box(ui->env.output_layout, out, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    const int h = ui->env.decor_theme->title_height > 0 ? ui->env.decor_theme->title_height : 24;
    const int pad = 8;
    int text_w = 0;
    if (!fbwl_text_measure(ui->text, h, ui->env.decor_theme->toolbar_font, &text_w, NULL)) {
        text_w = 0;
    }

    int w = text_w + 2 * pad;
    if (w < 1) {
        w = 1;
    }
    if (w > box.width) {
        w = box.width;
    }

    if (ui->tree == NULL) {
        ui->tree = wlr_scene_tree_create(ui->env.layer_overlay);
        if (ui->tree == NULL) {
            return;
        }

        const float alpha = 0.95f;
        float bg[4] = {ui->env.decor_theme->toolbar_bg[0], ui->env.decor_theme->toolbar_bg[1],
            ui->env.decor_theme->toolbar_bg[2], ui->env.decor_theme->toolbar_bg[3] * alpha};
        ui->bg = wlr_scene_rect_create(ui->tree, w, h, bg);
        if (ui->bg != NULL) {
            wlr_scene_node_set_position(&ui->bg->node, 0, 0);
        }

        ui->label = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->label != NULL) {
            wlr_scene_node_set_position(&ui->label->node, 0, 0);
        }
    }

    if (ui->bg != NULL) {
        wlr_scene_rect_set_size(ui->bg, w, h);
    }

    float fg[4] = {ui->env.decor_theme->toolbar_text[0], ui->env.decor_theme->toolbar_text[1],
        ui->env.decor_theme->toolbar_text[2], ui->env.decor_theme->toolbar_text[3]};
    struct wlr_buffer *buf = fbwl_text_buffer_create(ui->text, w, h, pad, fg, ui->env.decor_theme->toolbar_font);
    if (buf != NULL) {
        if (ui->label != NULL) {
            wlr_scene_buffer_set_buffer(ui->label, buf);
        }
        wlr_buffer_drop(buf);
    } else if (ui->label != NULL) {
        wlr_scene_buffer_set_buffer(ui->label, NULL);
    }

    int x = ui->anchor_x - w / 2;
    int y = ui->anchor_y;
    const int yoffset = 10;
    if (y - yoffset - h >= box.y) {
        y = y - yoffset - h;
    } else {
        y = y + yoffset;
    }

    const int left = box.x;
    const int right = box.x + box.width;
    if (x + w > right) {
        x = right - w;
    }
    if (x < left) {
        x = left;
    }

    ui->x = x;
    ui->y = y;
    ui->width = w;
    ui->height = h;

    const float bg_alpha = 0.95f;
    const bool pseudo = ui->env.force_pseudo_transparency && ui->env.decor_theme->toolbar_bg[3] * bg_alpha < 0.999f;
    if (pseudo) {
        fbwl_pseudo_bg_update(&ui->pseudo_bg, ui->tree, ui->env.output_layout,
            ui->x, ui->y, 0, 0, ui->width, ui->height,
            ui->env.wallpaper_buf, ui->env.background_color);
    } else {
        fbwl_pseudo_bg_destroy(&ui->pseudo_bg);
    }
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
    wlr_scene_node_set_enabled(&ui->tree->node, true);
    wlr_scene_node_raise_to_top(&ui->tree->node);
    ui->visible = true;

    wlr_log(WLR_INFO, "Tooltip: show delay=%d x=%d y=%d w=%d h=%d text=%s", ui->delay_ms, ui->x, ui->y, ui->width, ui->height, ui->text);
}

static int fbwl_ui_tooltip_timer(void *data) {
    struct fbwl_tooltip_ui *ui = data;
    if (ui == NULL) {
        return 0;
    }

    struct wl_event_source *src = ui->timer;
    ui->timer = NULL;
    ui->pending = false;
    if (src != NULL) {
        wl_event_source_remove(src);
    }

    fbwl_ui_tooltip_show_now(ui);
    return 0;
}

void fbwl_ui_tooltip_request(struct fbwl_tooltip_ui *ui, const struct fbwl_ui_tooltip_env *env,
        int lx, int ly, int delay_ms, const char *text) {
    if (ui == NULL || env == NULL) {
        return;
    }

    if (delay_ms < 0 || text == NULL || *text == '\0') {
        fbwl_ui_tooltip_hide(ui, delay_ms < 0 ? "disabled" : "empty");
        return;
    }

    ui->env = *env;
    ui->anchor_x = lx;
    ui->anchor_y = ly;
    ui->delay_ms = delay_ms;

    const bool same_text = (ui->pending || ui->visible) && ui->text != NULL && strcmp(ui->text, text) == 0;
    if (same_text) {
        return;
    }

    fbwl_ui_tooltip_hide(ui, "change");

    ui->text = strdup(text);
    if (ui->text == NULL) {
        return;
    }

    if (delay_ms == 0) {
        fbwl_ui_tooltip_show_now(ui);
        return;
    }

    if (ui->env.wl_display == NULL) {
        return;
    }
    struct wl_event_loop *loop = wl_display_get_event_loop(ui->env.wl_display);
    if (loop == NULL) {
        return;
    }

    ui->timer = wl_event_loop_add_timer(loop, fbwl_ui_tooltip_timer, ui);
    if (ui->timer == NULL) {
        return;
    }

    ui->pending = true;
    if (delay_ms < 0) {
        delay_ms = 0;
    }
    wl_event_source_timer_update(ui->timer, delay_ms);
}

void fbwl_ui_tooltip_destroy(struct fbwl_tooltip_ui *ui) {
    if (ui == NULL) {
        return;
    }
    fbwl_ui_tooltip_cancel_timer(ui);
    fbwl_ui_tooltip_destroy_scene(ui);
    free(ui->text);
    ui->text = NULL;
    ui->visible = false;
    ui->pending = false;
    ui->env = (struct fbwl_ui_tooltip_env){0};
}
