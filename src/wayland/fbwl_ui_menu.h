#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <xkbcommon/xkbcommon.h>

struct fbwl_decor_theme;
struct fbwl_menu;
struct fbwl_view;

struct wlr_scene;
struct wlr_scene_rect;
struct wlr_scene_tree;

struct fbwl_menu_ui {
    bool open;
    struct fbwl_menu *current;
    struct fbwl_menu *stack[16];
    size_t depth;
    size_t selected;
    struct fbwl_view *target_view;

    int x;
    int y;
    int width;
    int item_h;

    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *bg;
    struct wlr_scene_rect *highlight;
    struct wlr_scene_rect **item_rects;
    size_t item_rect_count;
};

struct fbwl_ui_menu_env {
    struct wlr_scene *scene;
    struct wlr_scene_tree *layer_overlay;
    const struct fbwl_decor_theme *decor_theme;
};

struct fbwl_ui_menu_hooks {
    void *userdata;
    void (*spawn)(void *userdata, const char *cmd);
    void (*terminate)(void *userdata);
    void (*view_close)(void *userdata, struct fbwl_view *view);
    void (*view_set_minimized)(void *userdata, struct fbwl_view *view, bool minimized, const char *why);
    void (*view_set_maximized)(void *userdata, struct fbwl_view *view, bool maximized);
    void (*view_set_fullscreen)(void *userdata, struct fbwl_view *view, bool fullscreen);
};

void fbwl_ui_menu_close(struct fbwl_menu_ui *ui, const char *why);
void fbwl_ui_menu_open_root(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
    struct fbwl_menu *root_menu, int x, int y);
void fbwl_ui_menu_open_window(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
    struct fbwl_menu *window_menu, struct fbwl_view *view, int x, int y);

ssize_t fbwl_ui_menu_index_at(const struct fbwl_menu_ui *ui, int lx, int ly);
void fbwl_ui_menu_set_selected(struct fbwl_menu_ui *ui, size_t idx);

bool fbwl_ui_menu_handle_keypress(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
    const struct fbwl_ui_menu_hooks *hooks, xkb_keysym_t sym);
bool fbwl_ui_menu_handle_click(struct fbwl_menu_ui *ui, const struct fbwl_ui_menu_env *env,
    const struct fbwl_ui_menu_hooks *hooks, int lx, int ly, uint32_t button);

