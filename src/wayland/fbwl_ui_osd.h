#pragma once

#include <stdbool.h>

struct wl_event_source;
struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;

struct fbwl_decor_theme;

struct fbwl_osd_ui {
    bool enabled;
    bool visible;

    int x;
    int y;
    int width;
    int height;

    int last_workspace;

    struct wl_event_source *hide_timer;

    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *bg;
    struct wlr_scene_buffer *label;
};

void fbwl_ui_osd_hide(struct fbwl_osd_ui *ui, const char *why);
void fbwl_ui_osd_update_position(struct fbwl_osd_ui *ui, struct wlr_output_layout *output_layout);
void fbwl_ui_osd_show_workspace(struct fbwl_osd_ui *ui,
    struct wlr_scene *scene, struct wlr_scene_tree *layer_top,
    const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout,
    int workspace, const char *workspace_name);
void fbwl_ui_osd_destroy(struct fbwl_osd_ui *ui);
