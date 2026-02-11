#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wlr_buffer;
struct wl_display;
struct wl_event_source;
struct wl_list;

struct fbwm_core;
struct fbwl_decor_theme;
struct fbwl_sni_watcher;
struct fbwl_view;

struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;
struct wlr_xwayland;

#include "wayland/fbwl_pseudo_bg.h"

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
    FBWL_TOOLBAR_TOOL_BUTTONS = 1u << 4,
};

enum { FBWL_TOOLBAR_BUTTON_COMMANDS_MAX = 5 };

struct fbwl_toolbar_button_cfg {
    char *name;
    char *label;
    char *commands[FBWL_TOOLBAR_BUTTON_COMMANDS_MAX];
};

enum fbwl_iconbar_alignment {
    FBWL_ICONBAR_ALIGN_LEFT = 0,
    FBWL_ICONBAR_ALIGN_RELATIVE,
    FBWL_ICONBAR_ALIGN_RELATIVE_SMART,
    FBWL_ICONBAR_ALIGN_RIGHT,
};

struct fbwl_toolbar_ui {
    bool enabled;

    enum fbwl_toolbar_placement placement;
    int on_head;
    int layer_num;
    int width_percent;
    int height_override;
    uint32_t tools;
    // From init: session.screenN.toolbar.tools (lowercased, ordered).
    char **tools_order;
    size_t tools_order_len;
    bool auto_hide;
    bool auto_raise;
    bool max_over;
    bool hidden;
    uint8_t alpha;

    float text_color[4];
    char font[128];
    char strftime_format[64];

    // From init: session.screenN.iconbar.* (applied from the toolbar "screen").
    char iconbar_mode[256];
    enum fbwl_iconbar_alignment iconbar_alignment;
    int iconbar_icon_width_px;
    int iconbar_icon_text_padding_px;
    bool iconbar_use_pixmap;
    char iconbar_iconified_prefix[64];
    char iconbar_iconified_suffix[64];

    // From init: session.screenN.{systray.pinLeft|systray.pinRight} (or pinLeft/pinRight alias).
    // For Wayland/SNI, we match these values against StatusNotifierItem "Id".
    char **systray_pin_left;
    size_t systray_pin_left_len;
    char **systray_pin_right;
    size_t systray_pin_right_len;

    int x;
    int y;
    int base_x;
    int base_y;
    int thickness;
    int height;
    int cell_w;
    int width;
    int ws_width;

    struct fbwl_toolbar_button_cfg *buttons;
    size_t buttons_len;
    int buttons_x;
    int buttons_w;
    int *button_item_lx;
    int *button_item_w;
    const char **button_item_tokens;
    struct wlr_scene_rect **button_rects;
    struct wlr_scene_buffer **button_labels;
    size_t button_count;

    int iconbar_x;
    int iconbar_w;
    struct fbwl_view **iconbar_views;
    char **iconbar_texts;
    int *iconbar_item_lx;
    int *iconbar_item_w;
    struct wlr_scene_rect **iconbar_items;
    struct wlr_scene_buffer **iconbar_labels;
    bool *iconbar_needs_tooltip;
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
    struct fbwl_pseudo_bg pseudo_bg;
    struct wlr_output_layout *pseudo_output_layout;
    struct wlr_buffer *pseudo_wallpaper_buf;
    const float *pseudo_background_color;
    const struct fbwl_decor_theme *pseudo_decor_theme;
    bool pseudo_force_pseudo_transparency;
    struct wlr_scene_rect *bg;
    struct wlr_scene_rect *highlight;
    struct wlr_scene_rect **cells;
    struct wlr_scene_buffer **labels;
    size_t cell_count;
};

struct fbwl_ui_toolbar_env {
    struct wlr_scene *scene;
    struct wlr_scene_tree *layer_tree;
    struct wlr_output_layout *output_layout;
    const struct wl_list *outputs;
    struct wl_display *wl_display;
    struct wlr_buffer *wallpaper_buf;
    const float *background_color;
    bool force_pseudo_transparency;
    struct fbwm_core *wm;
    struct wlr_xwayland *xwayland;
    const struct fbwl_decor_theme *decor_theme;
    struct fbwl_view *focused_view;
    bool cursor_valid;
    double cursor_x;
    double cursor_y;
    struct wlr_scene_tree *layer_background;
    struct wlr_scene_tree *layer_bottom;
    struct wlr_scene_tree *layer_normal;
    struct wlr_scene_tree *layer_fullscreen;
    struct wlr_scene_tree *layer_top;
    struct wlr_scene_tree *layer_overlay;
#ifdef HAVE_SYSTEMD
    struct fbwl_sni_watcher *sni;
#endif
};

struct fbwl_ui_toolbar_hooks {
    void *userdata;
    void (*apply_workspace_visibility)(void *userdata, const char *why);
    void (*view_set_minimized)(void *userdata, struct fbwl_view *view, bool minimized, const char *why);
    void (*execute_command)(void *userdata, const char *cmd_line, int lx, int ly, uint32_t button);
};

void fbwl_ui_toolbar_buttons_clear(struct fbwl_toolbar_ui *ui);
bool fbwl_ui_toolbar_buttons_replace(struct fbwl_toolbar_ui *ui, struct fbwl_toolbar_button_cfg *buttons, size_t len);

void fbwl_ui_toolbar_destroy(struct fbwl_toolbar_ui *ui);
void fbwl_ui_toolbar_rebuild(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env);
void fbwl_ui_toolbar_update_position(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env);
void fbwl_ui_toolbar_handle_motion(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
    int lx, int ly, int delay_ms);
void fbwl_ui_toolbar_update_iconbar_focus(struct fbwl_toolbar_ui *ui, const struct fbwl_decor_theme *decor_theme,
    const struct fbwl_view *focused_view);
bool fbwl_ui_toolbar_handle_click(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
    const struct fbwl_ui_toolbar_hooks *hooks, int lx, int ly, uint32_t button);
bool fbwl_ui_toolbar_tooltip_text_at(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
    int lx, int ly, const char **out_text);
