#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wl_display;
struct wl_event_source;

struct fbwm_core;
struct fbwl_decor_theme;
struct fbwl_sni_watcher;
struct fbwl_view;

struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;

enum fbwl_toolbar_placement {
    FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT = 0,
    FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER,
    FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT,
    FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM,
    FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER,
    FBWL_TOOLBAR_PLACEMENT_LEFT_TOP,
    FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM,
    FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER,
    FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP,
    FBWL_TOOLBAR_PLACEMENT_TOP_LEFT,
    FBWL_TOOLBAR_PLACEMENT_TOP_CENTER,
    FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT,
};

enum fbwl_toolbar_tool {
    FBWL_TOOLBAR_TOOL_WORKSPACES = 1u << 0,
    FBWL_TOOLBAR_TOOL_ICONBAR = 1u << 1,
    FBWL_TOOLBAR_TOOL_SYSTEMTRAY = 1u << 2,
    FBWL_TOOLBAR_TOOL_CLOCK = 1u << 3,
};

struct fbwl_toolbar_ui {
    bool enabled;

    enum fbwl_toolbar_placement placement;
    int width_percent;
    int height_override;
    uint32_t tools;
    bool auto_hide;
    bool auto_raise;
    bool hidden;

    int x;
    int y;
    int base_x;
    int base_y;
    int height;
    int cell_w;
    int width;
    int ws_width;

    int iconbar_x;
    int iconbar_w;
    struct fbwl_view **iconbar_views;
    int *iconbar_item_lx;
    int *iconbar_item_w;
    struct wlr_scene_rect **iconbar_items;
    struct wlr_scene_buffer **iconbar_labels;
    size_t iconbar_count;

    int tray_x;
    int tray_w;
    int tray_icon_w;
    char **tray_ids;
    char **tray_services;
    char **tray_paths;
    int *tray_item_lx;
    int *tray_item_w;
    struct wlr_scene_rect **tray_rects;
    struct wlr_scene_buffer **tray_icons;
    size_t tray_count;

    int clock_x;
    int clock_w;
    char clock_text[16];
    struct wl_event_source *clock_timer;
    struct wl_event_source *auto_timer;
    bool hovered;
    uint32_t auto_pending;
    struct wlr_scene_buffer *clock_label;

    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *bg;
    struct wlr_scene_rect *highlight;
    struct wlr_scene_rect **cells;
    struct wlr_scene_buffer **labels;
    size_t cell_count;
};

struct fbwl_ui_toolbar_env {
    struct wlr_scene *scene;
    struct wlr_scene_tree *layer_top;
    struct wlr_output_layout *output_layout;
    struct wl_display *wl_display;
    struct fbwm_core *wm;
    const struct fbwl_decor_theme *decor_theme;
    struct fbwl_view *focused_view;
#ifdef HAVE_SYSTEMD
    struct fbwl_sni_watcher *sni;
#endif
};

struct fbwl_ui_toolbar_hooks {
    void *userdata;
    void (*apply_workspace_visibility)(void *userdata, const char *why);
    void (*view_set_minimized)(void *userdata, struct fbwl_view *view, bool minimized, const char *why);
};

void fbwl_ui_toolbar_destroy(struct fbwl_toolbar_ui *ui);
void fbwl_ui_toolbar_rebuild(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env);
void fbwl_ui_toolbar_update_position(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env);
void fbwl_ui_toolbar_handle_motion(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
    int lx, int ly, int delay_ms);
void fbwl_ui_toolbar_update_iconbar_focus(struct fbwl_toolbar_ui *ui, const struct fbwl_decor_theme *decor_theme,
    const struct fbwl_view *focused_view);
bool fbwl_ui_toolbar_handle_click(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
    const struct fbwl_ui_toolbar_hooks *hooks, int lx, int ly, uint32_t button);
