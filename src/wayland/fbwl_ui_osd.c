#include "wayland/fbwl_ui_osd.h"

#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

static void fbwl_ui_osd_destroy_scene(struct fbwl_osd_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->label = NULL;
}

void fbwl_ui_osd_hide(struct fbwl_osd_ui *ui, const char *why) {
    if (ui == NULL) {
        return;
    }
    if (!ui->enabled || !ui->visible) {
        return;
    }
    ui->visible = false;
    if (ui->tree != NULL) {
        wlr_scene_node_set_enabled(&ui->tree->node, false);
    }
    wlr_log(WLR_INFO, "OSD: hide reason=%s", why != NULL ? why : "(null)");
}

void fbwl_ui_osd_update_position(struct fbwl_osd_ui *ui, struct wlr_output_layout *output_layout) {
    if (ui == NULL || output_layout == NULL) {
        return;
    }
    if (!ui->enabled || !ui->visible || ui->tree == NULL) {
        return;
    }

    struct wlr_output *out = wlr_output_layout_get_center_output(output_layout);
    if (out == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(output_layout, out, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    int x = box.x + (box.width - ui->width) / 2;
    int y = box.y + 12;
    if (x < box.x) {
        x = box.x;
    }
    if (y < box.y) {
        y = box.y;
    }

    if (x == ui->x && y == ui->y) {
        return;
    }

    ui->x = x;
    ui->y = y;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
}

static void fbwl_ui_osd_show_text(struct fbwl_osd_ui *ui,
        struct wlr_scene *scene, struct wlr_scene_tree *layer_top,
        const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout,
        const char *msg, int hide_ms) {
    if (ui == NULL || scene == NULL || decor_theme == NULL) {
        return;
    }
    if (!ui->enabled) {
        return;
    }

    if (ui->height < 1) {
        ui->height = decor_theme->title_height > 0 ? decor_theme->title_height : 24;
    }
    if (ui->width < 1) {
        ui->width = 200;
    }

    if (ui->tree == NULL) {
        struct wlr_scene_tree *parent = layer_top != NULL ? layer_top : &scene->tree;
        ui->tree = wlr_scene_tree_create(parent);
        if (ui->tree == NULL) {
            return;
        }

        float bg[4] = {decor_theme->titlebar_active[0], decor_theme->titlebar_active[1],
            decor_theme->titlebar_active[2], 0.85f};
        ui->bg = wlr_scene_rect_create(ui->tree, ui->width, ui->height, bg);
        if (ui->bg != NULL) {
            wlr_scene_node_set_position(&ui->bg->node, 0, 0);
        }

        ui->label = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->label != NULL) {
            wlr_scene_node_set_position(&ui->label->node, 0, 0);
        }
    }

    if (ui->tree != NULL) {
        wlr_scene_node_set_enabled(&ui->tree->node, true);
    }
    ui->visible = true;

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct wlr_buffer *buf = fbwl_text_buffer_create(msg != NULL ? msg : "", ui->width, ui->height, 8, fg,
        decor_theme->window_font, &decor_theme->window_label_focus_effect, 0);
    if (buf != NULL) {
        if (ui->label != NULL) {
            wlr_scene_buffer_set_buffer(ui->label, buf);
        }
        wlr_buffer_drop(buf);
    }

    if (hide_ms >= 0 && ui->hide_timer != NULL) {
        wl_event_source_timer_update(ui->hide_timer, hide_ms);
    }

    fbwl_ui_osd_update_position(ui, output_layout);
    wlr_scene_node_raise_to_top(&ui->tree->node);
}

void fbwl_ui_osd_show_workspace(struct fbwl_osd_ui *ui,
        struct wlr_scene *scene, struct wlr_scene_tree *layer_top,
        const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout,
        int workspace, const char *workspace_name) {
    if (ui == NULL || scene == NULL || decor_theme == NULL) {
        return;
    }
    if (!ui->enabled) {
        return;
    }

    char msg[256];
    if (workspace_name != NULL && *workspace_name != '\0') {
        if (snprintf(msg, sizeof(msg), "Workspace %d: %s", workspace + 1, workspace_name) < 0) {
            msg[0] = '\0';
        }
    } else if (snprintf(msg, sizeof(msg), "Workspace %d", workspace + 1) < 0) {
        msg[0] = '\0';
    }
    fbwl_ui_osd_show_text(ui, scene, layer_top, decor_theme, output_layout, msg, 600);
    wlr_log(WLR_INFO, "OSD: show workspace=%d", workspace + 1);
}

void fbwl_ui_osd_show_attention(struct fbwl_osd_ui *ui,
        struct wlr_scene *scene, struct wlr_scene_tree *layer_top,
        const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout,
        const char *title) {
    if (ui == NULL || scene == NULL || decor_theme == NULL) {
        return;
    }
    if (!ui->enabled) {
        return;
    }

    char msg[256];
    const char *t = title != NULL ? title : "";
    if (snprintf(msg, sizeof(msg), "Attention: %s", t) < 0) {
        msg[0] = '\0';
    }
    fbwl_ui_osd_show_text(ui, scene, layer_top, decor_theme, output_layout, msg, 600);
    wlr_log(WLR_INFO, "OSD: show attention title=%s", title != NULL ? title : "(null)");
}

void fbwl_ui_osd_show_window_position(struct fbwl_osd_ui *ui,
        struct wlr_scene *scene, struct wlr_scene_tree *layer_top,
        const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout,
        int x, int y) {
    if (ui == NULL) {
        return;
    }
    char msg[64];
    if (snprintf(msg, sizeof(msg), "%d x %d", x, y) < 0) {
        msg[0] = '\0';
    }
    fbwl_ui_osd_show_text(ui, scene, layer_top, decor_theme, output_layout, msg, -1);
    wlr_log(WLR_INFO, "OSD: show windowposition x=%d y=%d", x, y);
}

void fbwl_ui_osd_show_window_geometry(struct fbwl_osd_ui *ui,
        struct wlr_scene *scene, struct wlr_scene_tree *layer_top,
        const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout,
        int width, int height) {
    if (ui == NULL) {
        return;
    }
    char msg[64];
    if (snprintf(msg, sizeof(msg), "%d x %d", width, height) < 0) {
        msg[0] = '\0';
    }
    fbwl_ui_osd_show_text(ui, scene, layer_top, decor_theme, output_layout, msg, -1);
    wlr_log(WLR_INFO, "OSD: show windowgeometry w=%d h=%d", width, height);
}

void fbwl_ui_osd_destroy(struct fbwl_osd_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->hide_timer != NULL) {
        wl_event_source_remove(ui->hide_timer);
        ui->hide_timer = NULL;
    }
    fbwl_ui_osd_destroy_scene(ui);
    ui->enabled = false;
    ui->visible = false;
}
