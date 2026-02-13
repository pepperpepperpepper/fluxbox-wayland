#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-server-core.h>

#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_pseudo_bg.h"

struct fbwl_decor_theme;
struct fbwl_view;
struct wlr_buffer;

struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_rect;
struct wlr_scene_tree;

enum fbwl_slit_direction {
    FBWL_SLIT_DIR_VERTICAL = 0,
    FBWL_SLIT_DIR_HORIZONTAL,
};

struct fbwl_slit_item {
    struct wl_list link;
    struct fbwl_view *view;
    bool visible;
};

struct fbwl_slit_ui {
    bool enabled;

    enum fbwl_toolbar_placement placement;
    int on_head;
    int layer_num;

    bool auto_hide;
    bool auto_raise;
    bool max_over;
    bool accept_kde_dockapps;
    bool hidden;
    uint8_t alpha;
    enum fbwl_slit_direction direction;

    int x;
    int y;
    int base_x;
    int base_y;
    int thickness;
    int width;
    int height;

    struct wl_event_source *auto_timer;
    bool hovered;
    uint32_t auto_pending;

    struct wlr_scene_tree *tree;
    struct fbwl_pseudo_bg pseudo_bg;
    struct wlr_output_layout *pseudo_output_layout;
    enum fbwl_wallpaper_mode pseudo_wallpaper_mode;
    struct wlr_buffer *pseudo_wallpaper_buf;
    const float *pseudo_background_color;
    const struct fbwl_decor_theme *pseudo_decor_theme;
    bool pseudo_force_pseudo_transparency;
    struct wlr_scene_buffer *bg;

    struct wl_list items; // struct fbwl_slit_item.link
    size_t items_len;

    char **order;
    size_t order_len;
};

struct fbwl_ui_slit_env {
    struct wlr_scene *scene;
    struct wlr_scene_tree *layer_tree;
    struct wlr_output_layout *output_layout;
    struct wl_list *outputs;
    struct wl_display *wl_display;
    enum fbwl_wallpaper_mode wallpaper_mode;
    struct wlr_buffer *wallpaper_buf;
    const float *background_color;
    bool force_pseudo_transparency;
    const struct fbwl_decor_theme *decor_theme;
};

const char *fbwl_slit_direction_str(enum fbwl_slit_direction dir);

void fbwl_ui_slit_init(struct fbwl_slit_ui *ui);
void fbwl_ui_slit_destroy(struct fbwl_slit_ui *ui);

bool fbwl_ui_slit_attach_view(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, struct fbwl_view *view,
        const char *why);
void fbwl_ui_slit_detach_view(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, struct fbwl_view *view,
        const char *why);

void fbwl_ui_slit_handle_view_commit(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, struct fbwl_view *view,
        const char *why);
void fbwl_ui_slit_apply_view_geometry(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, struct fbwl_view *view,
        const char *why);

void fbwl_ui_slit_rebuild(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env);
void fbwl_ui_slit_update_position(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env);
void fbwl_ui_slit_handle_motion(struct fbwl_slit_ui *ui, const struct fbwl_ui_slit_env *env, int lx, int ly, int delay_ms);

// Load a Fluxbox-style slitlist file and update ordering. Returns true if the order list changed.
bool fbwl_ui_slit_set_order_file(struct fbwl_slit_ui *ui, const char *path);

// Save the current slit order to a Fluxbox-style slitlist file. Returns true on success.
// Note: this updates ui->order to match the written order (preserving existing order entries not currently mapped).
bool fbwl_ui_slit_save_order_file(struct fbwl_slit_ui *ui, const char *path);
