#pragma once

#include <stdbool.h>

struct wl_display;
struct wl_event_source;

struct fbwl_decor_theme;

struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;

struct fbwl_ui_tooltip_env {
    struct wlr_scene *scene;
    struct wlr_scene_tree *layer_overlay;
    struct wl_display *wl_display;
    struct wlr_output_layout *output_layout;
    const struct fbwl_decor_theme *decor_theme;
};

struct fbwl_tooltip_ui {
    bool visible;
    bool pending;
    int delay_ms;

    int anchor_x;
    int anchor_y;
    int x;
    int y;
    int width;
    int height;

    char *text;

    struct wl_event_source *timer;
    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *bg;
    struct wlr_scene_buffer *label;

    struct fbwl_ui_tooltip_env env;
};

void fbwl_ui_tooltip_destroy(struct fbwl_tooltip_ui *ui);
void fbwl_ui_tooltip_hide(struct fbwl_tooltip_ui *ui, const char *why);
void fbwl_ui_tooltip_request(struct fbwl_tooltip_ui *ui, const struct fbwl_ui_tooltip_env *env,
    int lx, int ly, int delay_ms, const char *text);
