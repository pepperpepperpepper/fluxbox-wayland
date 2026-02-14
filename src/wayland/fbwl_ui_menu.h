#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <xkbcommon/xkbcommon.h>

#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_pseudo_bg.h"

struct wl_display;
struct wl_event_source;

struct fbwl_decor_theme;
struct fbwl_view;

struct wlr_scene;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;
struct wlr_buffer;
struct wlr_output_layout;

struct fbwl_ui_menu_env {
    struct wlr_scene *scene;
    struct wlr_scene_tree *layer_overlay;
    struct wlr_output_layout *output_layout;
    enum fbwl_wallpaper_mode wallpaper_mode;
    struct wlr_buffer *wallpaper_buf;
    const float *background_color;
    const struct fbwl_decor_theme *decor_theme;
    struct wl_display *wl_display;
    bool force_pseudo_transparency;
};

enum fbwl_menu_search_mode {
    FBWL_MENU_SEARCH_NOWHERE = 0,
    FBWL_MENU_SEARCH_ITEMSTART,
    FBWL_MENU_SEARCH_SOMEWHERE,
};

struct fbwl_menu_ui {
    bool open;
    struct fbwl_menu *current;
    struct fbwl_menu *stack[16];
    size_t depth;
    size_t selected;
    struct fbwl_view *target_view;

    struct fbwl_ui_menu_env env;
    uint8_t alpha;
    int menu_delay_ms;
    enum fbwl_menu_search_mode search_mode;
    char search_pattern[64];
    struct wl_event_source *submenu_timer;
    ssize_t hovered_idx;
    size_t submenu_pending_idx;

    int x;
    int y;
    int width;
    int border_w;
    int title_h;
    int item_h;

    struct wlr_scene_tree *tree;
    struct fbwl_pseudo_bg pseudo_bg;
    struct wlr_scene_buffer *bg;
    struct wlr_scene_buffer *title_bg;
    struct wlr_scene_buffer *title_label;
    struct wlr_scene_buffer *highlight;
    struct wlr_scene_rect **item_rects;
    size_t item_rect_count;
    struct wlr_scene_buffer **item_labels;
    size_t item_label_count;
    struct wlr_scene_buffer **item_marks;
    size_t item_mark_count;
};

struct fbwl_ui_menu_hooks {
    void *userdata;
    void (*spawn)(void *userdata, const char *cmd);
    void (*terminate)(void *userdata);
    void (*server_action)(void *userdata, enum fbwl_menu_server_action action, int arg, const char *cmd);
    void (*view_close)(void *userdata, struct fbwl_view *view);
    void (*view_set_minimized)(void *userdata, struct fbwl_view *view, bool minimized, const char *why);
    void (*view_set_maximized)(void *userdata, struct fbwl_view *view, bool maximized);
    void (*view_set_fullscreen)(void *userdata, struct fbwl_view *view, bool fullscreen);
    void (*workspace_switch)(void *userdata, int workspace0);
};

void fbwl_ui_menu_close(struct fbwl_menu_ui *ui, const char *why);
void fbwl_ui_menu_open_root(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
    struct fbwl_menu *root_menu, int x, int y);
void fbwl_ui_menu_open_window(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
    struct fbwl_menu *window_menu, struct fbwl_view *view, int x, int y);

ssize_t fbwl_ui_menu_index_at(const struct fbwl_menu_ui *ui, int lx, int ly);
void fbwl_ui_menu_set_selected(struct fbwl_menu_ui *ui, size_t idx);
void fbwl_ui_menu_handle_motion(struct fbwl_menu_ui *ui, int lx, int ly);

bool fbwl_ui_menu_handle_keypress(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
    const struct fbwl_ui_menu_hooks *hooks, xkb_keysym_t sym);
bool fbwl_ui_menu_handle_click(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
    const struct fbwl_ui_menu_hooks *hooks, int lx, int ly, uint32_t button);
