#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "wmcore/fbwm_core.h"

struct fbwl_decor_theme;
struct fbwl_server;

struct wlr_foreign_toplevel_handle_v1;
struct wlr_output;
struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;
struct wlr_surface;
struct wlr_xdg_toplevel;
struct wlr_xwayland_surface;

enum fbwl_view_type {
    FBWL_VIEW_XDG,
    FBWL_VIEW_XWAYLAND,
};

struct fbwl_view {
    struct fbwl_server *server;
    enum fbwl_view_type type;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_xwayland_surface *xwayland_surface;
    struct wlr_scene_tree *scene_tree;
    bool decor_enabled;
    bool decor_active;
    struct wlr_scene_tree *decor_tree;
    struct wlr_scene_rect *decor_titlebar;
    struct wlr_scene_buffer *decor_title_text;
    char *decor_title_text_cache;
    int decor_title_text_cache_w;
    struct wlr_scene_rect *decor_border_top;
    struct wlr_scene_rect *decor_border_bottom;
    struct wlr_scene_rect *decor_border_left;
    struct wlr_scene_rect *decor_border_right;
    struct wlr_scene_rect *decor_btn_close;
    struct wlr_scene_rect *decor_btn_max;
    struct wlr_scene_rect *decor_btn_min;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_minimize;
    struct wl_listener set_title;
    struct wl_listener set_app_id;

    struct wl_listener xwayland_associate;
    struct wl_listener xwayland_dissociate;
    struct wl_listener xwayland_request_configure;
    struct wl_listener xwayland_request_activate;
    struct wl_listener xwayland_request_close;
    struct wl_listener xwayland_set_title;
    struct wl_listener xwayland_set_class;

    struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
    struct wlr_output *foreign_output;
    struct wl_listener foreign_request_maximize;
    struct wl_listener foreign_request_minimize;
    struct wl_listener foreign_request_activate;
    struct wl_listener foreign_request_fullscreen;
    struct wl_listener foreign_request_close;

    int x, y;
    int width, height;
    bool mapped;
    bool placed;

    bool minimized;
    bool maximized;
    bool fullscreen;
    int saved_x, saved_y;
    int saved_w, saved_h;

    bool apps_rules_applied;
    struct fbwm_view wm_view;
};

enum fbwl_decor_hit_kind {
    FBWL_DECOR_HIT_NONE = 0,
    FBWL_DECOR_HIT_TITLEBAR,
    FBWL_DECOR_HIT_RESIZE,
    FBWL_DECOR_HIT_BTN_CLOSE,
    FBWL_DECOR_HIT_BTN_MAX,
    FBWL_DECOR_HIT_BTN_MIN,
};

struct fbwl_decor_hit {
    enum fbwl_decor_hit_kind kind;
    uint32_t edges;
};

struct wlr_surface *fbwl_view_wlr_surface(const struct fbwl_view *view);
struct fbwl_view *fbwl_view_from_surface(struct wlr_surface *surface);

const char *fbwl_view_title(const struct fbwl_view *view);
const char *fbwl_view_app_id(const struct fbwl_view *view);
const char *fbwl_view_display_title(const struct fbwl_view *view);

int fbwl_view_current_width(const struct fbwl_view *view);
int fbwl_view_current_height(const struct fbwl_view *view);

void fbwl_view_set_activated(struct fbwl_view *view, bool activated);

void fbwl_view_decor_apply_enabled(struct fbwl_view *view);
void fbwl_view_decor_set_active(struct fbwl_view *view, const struct fbwl_decor_theme *theme, bool active);
void fbwl_view_decor_update_title_text(struct fbwl_view *view, const struct fbwl_decor_theme *theme);
void fbwl_view_decor_update(struct fbwl_view *view, const struct fbwl_decor_theme *theme);
void fbwl_view_decor_create(struct fbwl_view *view, const struct fbwl_decor_theme *theme);
void fbwl_view_decor_set_enabled(struct fbwl_view *view, bool enabled);
struct fbwl_decor_hit fbwl_view_decor_hit_test(const struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        double lx, double ly);

void fbwl_view_save_geometry(struct fbwl_view *view);
void fbwl_view_get_output_box(const struct fbwl_view *view, struct wlr_output_layout *output_layout,
        struct wlr_output *preferred, struct wlr_box *box);
void fbwl_view_get_output_usable_box(const struct fbwl_view *view, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_output *preferred, struct wlr_box *box);
void fbwl_view_foreign_update_output_from_position(struct fbwl_view *view, struct wlr_output_layout *output_layout);

void fbwl_view_place_initial(struct fbwl_view *view, struct fbwm_core *wm, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, double cursor_x, double cursor_y);
void fbwl_view_set_maximized(struct fbwl_view *view, bool maximized, struct wlr_output_layout *output_layout,
        struct wl_list *outputs);
void fbwl_view_set_fullscreen(struct fbwl_view *view, bool fullscreen, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_scene_tree *layer_normal, struct wlr_scene_tree *layer_fullscreen,
        struct wlr_output *output);

struct fbwl_view *fbwl_view_at(struct wlr_scene *scene, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy);
