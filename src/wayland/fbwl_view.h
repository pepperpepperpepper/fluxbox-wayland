#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "wmcore/fbwm_core.h"
#include "wayland/fbwl_pseudo_bg.h"

struct fbwl_decor_theme;
struct fbwl_server;
struct fbwl_tab_group;

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
    struct wlr_scene_tree *content_tree;
    struct wlr_scene_tree *base_layer;
    struct fbwl_pseudo_bg pseudo_bg;
    bool decor_enabled;
    uint32_t decor_mask;
    bool decor_active;
    bool decor_forced;
    bool xdg_decoration_server_side;
    bool shaded;
    bool alpha_set;
    uint8_t alpha_focused;
    uint8_t alpha_unfocused;
    bool alpha_is_default;
    uint32_t focus_protection;
    uint64_t last_typing_time_msec;
    struct wl_event_source *attention_timer;
    int attention_interval_ms;
    int attention_toggle_count;
    bool attention_active;
    bool attention_state;
    bool attention_from_xwayland_urgency;
    bool xwayland_urgent;
    struct wlr_scene_tree *decor_tree;
    struct fbwl_pseudo_bg decor_titlebar_pseudo_bg;
    struct wlr_scene_buffer *decor_titlebar_tex;
    struct wlr_scene_rect *decor_titlebar;
    struct wlr_scene_buffer *decor_title_text;
    char *decor_title_text_cache;
    int decor_title_text_cache_w;
    bool decor_title_text_cache_active;
    char *title_override;
    struct wlr_scene_rect *decor_border_top;
    struct wlr_scene_rect *decor_border_bottom;
    struct wlr_scene_rect *decor_border_left;
    struct wlr_scene_rect *decor_border_right;
    struct wlr_scene_rect *decor_btn_menu;
    struct wlr_scene_rect *decor_btn_shade;
    struct wlr_scene_rect *decor_btn_stick;
    struct wlr_scene_rect *decor_btn_close;
    struct wlr_scene_rect *decor_btn_max;
    struct wlr_scene_rect *decor_btn_min;
    struct wlr_scene_rect *decor_btn_lhalf;
    struct wlr_scene_rect *decor_btn_rhalf;
    struct wlr_scene_tree *decor_tabs_tree;
    int decor_tabs_x;
    int decor_tabs_y;
    int decor_tabs_w;
    int decor_tabs_h;
    bool decor_tabs_vertical;
    int *decor_tab_item_lx;
    int *decor_tab_item_w;
    size_t decor_tab_count;
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
    struct wl_listener xwayland_request_demands_attention;
    struct wl_listener xwayland_set_title;
    struct wl_listener xwayland_set_class;
    struct wl_listener xwayland_set_hints;

    struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
    struct wlr_output *foreign_output;
    struct wl_listener foreign_request_maximize;
    struct wl_listener foreign_request_minimize;
    struct wl_listener foreign_request_activate;
    struct wl_listener foreign_request_fullscreen;
    struct wl_listener foreign_request_close;

    int x, y;
    int width, height;
    int committed_width, committed_height;
    bool mapped;
    bool placed;

    bool minimized;
    bool maximized;
    bool fullscreen;
    int saved_x, saved_y;
    int saved_w, saved_h;
    bool maximized_h;
    bool maximized_v;
    int saved_x_h;
    int saved_w_h;
    int saved_y_v;
    int saved_h_v;

    bool apps_rules_applied;
    bool apps_rule_index_valid;
    size_t apps_rule_index;
    uint64_t apps_rules_generation;
    int apps_group_id;
    struct fbwm_view wm_view;
    uint64_t create_seq;

    struct fbwl_tab_group *tab_group;
    struct wl_list tab_link;

    bool focus_hidden_override_set;
    bool focus_hidden_override;
    bool icon_hidden_override_set;
    bool icon_hidden_override;

    bool ignore_size_hints_override_set;
    bool ignore_size_hints_override;

    bool tabs_enabled_override_set;
    bool tabs_enabled_override;

    bool in_slit;
};

enum fbwl_decor_hit_kind {
    FBWL_DECOR_HIT_NONE = 0,
    FBWL_DECOR_HIT_TITLEBAR,
    FBWL_DECOR_HIT_RESIZE,
    FBWL_DECOR_HIT_BTN_MENU,
    FBWL_DECOR_HIT_BTN_SHADE,
    FBWL_DECOR_HIT_BTN_STICK,
    FBWL_DECOR_HIT_BTN_CLOSE,
    FBWL_DECOR_HIT_BTN_MAX,
    FBWL_DECOR_HIT_BTN_MIN,
    FBWL_DECOR_HIT_BTN_LHALF,
    FBWL_DECOR_HIT_BTN_RHALF,
};

struct fbwl_decor_hit {
    enum fbwl_decor_hit_kind kind;
    uint32_t edges;
};

struct wlr_surface *fbwl_view_wlr_surface(const struct fbwl_view *view);
struct fbwl_view *fbwl_view_from_surface(struct wlr_surface *surface);

const char *fbwl_view_title(const struct fbwl_view *view);
const char *fbwl_view_app_id(const struct fbwl_view *view);
const char *fbwl_view_instance(const struct fbwl_view *view);
const char *fbwl_view_role(const struct fbwl_view *view);
const char *fbwl_view_display_title(const struct fbwl_view *view);

bool fbwl_view_is_transient(const struct fbwl_view *view);
bool fbwl_view_accepts_focus(const struct fbwl_view *view);
bool fbwl_view_is_icon_hidden(const struct fbwl_view *view);
bool fbwl_view_is_focus_hidden(const struct fbwl_view *view);
bool fbwl_view_is_urgent(const struct fbwl_view *view);

int fbwl_view_current_width(const struct fbwl_view *view);
int fbwl_view_current_height(const struct fbwl_view *view);

void fbwl_view_set_activated(struct fbwl_view *view, bool activated);

void fbwl_view_decor_apply_enabled(struct fbwl_view *view);
void fbwl_view_decor_set_active(struct fbwl_view *view, const struct fbwl_decor_theme *theme, bool active);
void fbwl_view_decor_update_title_text(struct fbwl_view *view, const struct fbwl_decor_theme *theme);
void fbwl_view_decor_update(struct fbwl_view *view, const struct fbwl_decor_theme *theme);
void fbwl_view_decor_create(struct fbwl_view *view, const struct fbwl_decor_theme *theme);
void fbwl_view_decor_set_enabled(struct fbwl_view *view, bool enabled);
void fbwl_view_decor_frame_extents(const struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        int *left, int *top, int *right, int *bottom);
struct fbwl_decor_hit fbwl_view_decor_hit_test(const struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        double lx, double ly);

bool fbwl_view_tabs_bar_contains(const struct fbwl_view *view, double lx, double ly);
bool fbwl_view_tabs_index_at(const struct fbwl_view *view, double lx, double ly, int *out_tab_index0);

void fbwl_view_alpha_apply(struct fbwl_view *view);
void fbwl_view_set_alpha(struct fbwl_view *view, uint8_t focused, uint8_t unfocused, const char *why);
void fbwl_view_set_shaded(struct fbwl_view *view, bool shaded, const char *why);

void fbwl_view_pseudo_bg_update(struct fbwl_view *view, const char *why);

void fbwl_view_cleanup(struct fbwl_view *view);

void fbwl_view_save_geometry(struct fbwl_view *view);
void fbwl_view_get_output_box(const struct fbwl_view *view, struct wlr_output_layout *output_layout,
        struct wlr_output *preferred, struct wlr_box *box);
void fbwl_view_get_output_usable_box(const struct fbwl_view *view, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_output *preferred, struct wlr_box *box);
void fbwl_view_apply_tabs_maxover_box(const struct fbwl_view *view, struct wlr_box *box);
void fbwl_view_foreign_update_output_from_position(struct fbwl_view *view, struct wlr_output_layout *output_layout);

void fbwl_view_place_initial(struct fbwl_view *view, struct fbwm_core *wm, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, double cursor_x, double cursor_y);
void fbwl_view_set_maximized(struct fbwl_view *view, bool maximized, struct wlr_output_layout *output_layout,
        struct wl_list *outputs);
void fbwl_view_set_maximized_axes(struct fbwl_view *view, bool maximized_h, bool maximized_v,
        struct wlr_output_layout *output_layout, struct wl_list *outputs);
void fbwl_view_set_fullscreen(struct fbwl_view *view, bool fullscreen, struct wlr_output_layout *output_layout,
        struct wl_list *outputs, struct wlr_scene_tree *layer_normal, struct wlr_scene_tree *layer_fullscreen,
        struct wlr_output *output);

struct fbwl_view *fbwl_view_at(struct wlr_scene *scene, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy);
