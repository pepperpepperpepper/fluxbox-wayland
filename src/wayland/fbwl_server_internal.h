#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <wayland-server-core.h>
#include <wlr/version.h>

#include <xkbcommon/xkbcommon.h>

#include "wmcore/fbwm_core.h"
#include "wayland/fbwl_apps_rules.h"
#include "wayland/fbwl_grabs.h"
#include "wayland/fbwl_ipc.h"
#include "wayland/fbwl_keybindings.h"
#include "wayland/fbwl_mousebindings.h"
#include "wayland/fbwl_shortcuts_inhibit.h"
#include "wayland/fbwl_session_lock.h"
#include "wayland/fbwl_text_input.h"
#include "wayland/fbwl_pointer_constraints.h"
#include "wayland/fbwl_screencopy.h"
#include "wayland/fbwl_export_dmabuf.h"
#include "wayland/fbwl_ext_image_capture.h"
#include "wayland/fbwl_viewporter.h"
#include "wayland/fbwl_fractional_scale.h"
#include "wayland/fbwl_xdg_output.h"
#include "wayland/fbwl_idle.h"
#include "wayland/fbwl_xdg_activation.h"
#include "wayland/fbwl_xdg_decoration.h"
#include "wayland/fbwl_wallpaper.h"
#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_ui_menu.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_ui_slit.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_ui_cmd_dialog.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_osd.h"
#include "wayland/fbwl_ui_tooltip.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_xdg_shell.h"
#include "wayland/fbwl_xwayland.h"

#ifdef HAVE_SYSTEMD
#include "wayland/fbwl_sni_tray.h"
#endif

struct wl_protocol_logger;
struct wlr_backend;
struct wlr_renderer;
struct wlr_allocator;
struct wlr_compositor;
struct wlr_presentation;
struct wlr_data_control_manager_v1;
struct wlr_ext_data_control_manager_v1;
struct wlr_primary_selection_v1_device_manager;
struct wlr_single_pixel_buffer_manager_v1;
struct wlr_buffer;
struct wlr_scene;
struct wlr_scene_output_layout;
struct wlr_scene_tree;
struct wlr_output_layout;
struct wlr_output_manager_v1;
struct wlr_output_power_manager_v1;
struct wlr_xdg_shell;
struct wlr_xwayland;
struct wlr_layer_shell_v1;
struct wlr_foreign_toplevel_manager_v1;
struct wlr_cursor;
struct wlr_xcursor_manager;
struct wlr_cursor_shape_manager_v1;
struct wlr_seat;
struct wlr_keyboard_shortcuts_inhibitor_v1;
struct wlr_virtual_keyboard_manager_v1;
struct wlr_virtual_pointer_manager_v1;
struct wlr_surface;
struct wlr_pointer_button_event;
struct wlr_pointer_axis_event;

struct fbwl_view;

struct fbwl_resource_kv {
    char *key;
    char *value;
};

struct fbwl_resource_db {
    struct fbwl_resource_kv *items;
    size_t items_len;
    size_t items_cap;
};

enum fbwl_focus_model {
    FBWL_FOCUS_MODEL_CLICK_TO_FOCUS = 0,
    FBWL_FOCUS_MODEL_MOUSE_FOCUS,
    FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS,
};

enum fbwl_focus_reason {
    FBWL_FOCUS_REASON_NONE = 0,
    FBWL_FOCUS_REASON_POINTER_CLICK,
    FBWL_FOCUS_REASON_POINTER_MOTION,
    FBWL_FOCUS_REASON_KEYBIND,
    FBWL_FOCUS_REASON_MAP,
    FBWL_FOCUS_REASON_ACTIVATE,
};

#define FBWL_TITLEBAR_BUTTONS_MAX 16

struct fbwl_focus_config {
    enum fbwl_focus_model model;
    bool auto_raise;
    int auto_raise_delay_ms;
    bool click_raises;
    bool focus_new_windows;
    int no_focus_while_typing_delay_ms;
    bool focus_same_head;
    int demands_attention_timeout_ms;
    bool allow_remote_actions;
};

struct fbwl_screen_toolbar_config {
    bool enabled;
    enum fbwl_toolbar_placement placement;
    int on_head;
    int layer_num;
    int width_percent;
    int height_override;
    uint32_t tools;
    bool auto_hide;
    bool auto_raise;
    bool max_over;
    uint8_t alpha;
    char strftime_format[64];
};

struct fbwl_screen_menu_config {
    int delay_ms;
    uint8_t alpha;
    bool client_menu_use_pixmap;
};

struct fbwl_screen_slit_config {
    enum fbwl_toolbar_placement placement;
    int on_head;
    int layer_num;
    bool auto_hide;
    bool auto_raise;
    bool max_over;
    bool accept_kde_dockapps;
    uint8_t alpha;
    enum fbwl_slit_direction direction;
};

struct fbwl_screen_iconbar_config {
    char mode[256];
    enum fbwl_iconbar_alignment alignment;
    int icon_width_px;
    int icon_text_padding_px;
    bool use_pixmap;
    char iconified_prefix[64];
    char iconified_suffix[64];
};

struct fbwl_screen_struts_config {
    int left_px;
    int right_px;
    int top_px;
    int bottom_px;
};

struct fbwl_screen_config {
    struct fbwl_focus_config focus;

    int tooltip_delay_ms;

    struct fbwl_screen_iconbar_config iconbar;

    bool opaque_move;
    bool opaque_resize;
    int opaque_resize_delay_ms;
    bool full_maximization;
    bool max_ignore_increment;
    bool max_disable_move;
    bool max_disable_resize;
    bool workspace_warping;
    bool workspace_warping_horizontal;
    bool workspace_warping_vertical;
    int workspace_warping_horizontal_offset;
    int workspace_warping_vertical_offset;
    bool show_window_position;
    int edge_snap_threshold_px;
    int edge_resize_snap_threshold_px;

    struct fbwl_screen_struts_config struts;

    enum fbwm_window_placement_strategy placement_strategy;
    enum fbwm_row_placement_direction placement_row_dir;
    enum fbwm_col_placement_direction placement_col_dir;

    struct fbwl_tabs_config tabs;
    struct fbwl_screen_slit_config slit;
    struct fbwl_screen_toolbar_config toolbar;
    struct fbwl_screen_menu_config menu;
};

struct fbwl_server;

struct fbwl_shortcuts_inhibitor {
    struct wl_list link;
    struct fbwl_server *server;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
};

struct fbwl_marked_window {
    uint32_t keycode;
    uint64_t create_seq;
};

struct fbwl_marked_windows {
    struct fbwl_marked_window *items;
    size_t len;
    size_t cap;
};

struct fbwl_server {
    struct wl_display *wl_display;
    struct wl_protocol_logger *protocol_logger;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_compositor *compositor;
    struct wlr_presentation *presentation;

    struct wlr_data_control_manager_v1 *data_control_mgr;
    struct wlr_ext_data_control_manager_v1 *ext_data_control_mgr;
    struct wlr_primary_selection_v1_device_manager *primary_selection_mgr;
    struct wlr_single_pixel_buffer_manager_v1 *single_pixel_buffer_mgr;

    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_scene_tree *layer_background;
    struct wlr_scene_tree *layer_bottom;
    struct wlr_scene_tree *layer_normal;
    struct wlr_scene_tree *layer_fullscreen;
    struct wlr_scene_tree *layer_top;
    struct wlr_scene_tree *layer_overlay;

    float background_color[4];
    char *wallpaper_path;
    enum fbwl_wallpaper_mode wallpaper_mode;
    struct wlr_buffer *wallpaper_buf;

    struct fbwl_decor_theme decor_theme;
    enum fbwl_decor_hit_kind titlebar_left[FBWL_TITLEBAR_BUTTONS_MAX];
    size_t titlebar_left_len;
    enum fbwl_decor_hit_kind titlebar_right[FBWL_TITLEBAR_BUTTONS_MAX];
    size_t titlebar_right_len;
    struct fbwl_menu *root_menu;
    struct fbwl_menu *custom_menu;
    struct fbwl_menu *window_menu;
    struct fbwl_menu *workspace_menu;
    struct fbwl_menu *client_menu;
    struct fbwl_menu *slit_menu;
    char *config_dir;
    char *keys_file;
    char *apps_file;
    char *style_file;
    char *style_overlay_file;
    char *menu_file;
    char *window_menu_file;
    char *slitlist_file;
    bool workspaces_override;
    bool keys_file_override;
    bool apps_file_override;
    bool style_file_override;
    bool menu_file_override;
    struct fbwl_menu_ui menu_ui;
    struct fbwl_toolbar_ui toolbar_ui;
    struct fbwl_slit_ui slit_ui;
    struct fbwl_tooltip_ui tooltip_ui;
    struct fbwl_cmd_dialog_ui cmd_dialog_ui;
    uint64_t cmd_dialog_target_create_seq;
    struct fbwl_osd_ui osd_ui;
    struct fbwl_osd_ui move_osd_ui;
    struct fbwl_tabs_config tabs;
    struct wl_list tab_groups;
    struct fbwl_screen_config *screen_configs;
    size_t screen_configs_len;

    struct wlr_output_layout *output_layout;
    struct wlr_output_manager_v1 *output_manager;
    struct wl_listener output_manager_apply;
    struct wl_listener output_manager_test;
    struct wlr_output_power_manager_v1 *output_power_mgr;
    struct wl_listener output_power_set_mode;
    struct wl_list outputs;
    struct wl_listener new_output;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;

    struct wlr_xwayland *xwayland;
    struct wl_listener xwayland_ready;
    struct wl_listener xwayland_new_surface;
    pid_t xembed_sni_proxy_pid;

    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;
    struct wl_list layer_surfaces;

    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_mgr;

    struct fbwl_xdg_activation_state xdg_activation;

    struct fbwl_xdg_decoration_state xdg_decoration;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener cursor_shape_request_set_shape;

    struct wlr_seat *seat;
    struct fbwl_shortcuts_inhibit_state shortcuts_inhibit;
    struct wl_list keyboards;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_start_drag;

    struct fbwl_text_input_state text_input;

    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
    struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener new_virtual_pointer;

    struct fbwl_pointer_constraints_state pointer_constraints;
    struct fbwl_screencopy_state screencopy;
    struct fbwl_export_dmabuf_state export_dmabuf;

#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8) | 0)
    struct fbwl_ext_image_capture_state ext_image_capture;
#endif

    struct fbwl_viewporter_state viewporter;
    struct fbwl_fractional_scale_state fractional_scale;
    struct fbwl_xdg_output_state xdg_output;

    struct fbwl_idle_state idle;

    struct fbwl_session_lock_state session_lock;

    struct fbwl_ipc ipc;

#ifdef HAVE_SYSTEMD
    struct fbwl_sni_watcher sni;
#endif

    const char *startup_cmd;
    const char *terminal_cmd;
    bool restart_requested;
    char *restart_cmd;
    bool has_pointer;
    bool ignore_border;
    bool force_pseudo_transparency;
    int double_click_interval_ms;
    bool opaque_move;
    bool opaque_resize;
    int opaque_resize_delay_ms;
    bool full_maximization;
    bool max_ignore_increment;
    bool max_disable_move;
    bool max_disable_resize;
    bool workspace_warping;
    bool workspace_warping_horizontal;
    bool workspace_warping_vertical;
    int workspace_warping_horizontal_offset;
    int workspace_warping_vertical_offset;
    bool show_window_position;
    int edge_snap_threshold_px;
    int edge_resize_snap_threshold_px;
    int colors_per_channel;
    int cache_life_minutes;
    int cache_max_kb;
    int config_version;
    char *group_file;
    uint32_t last_button_time_msec;
    int last_button;
    bool mousebind_capture_active;
    bool mousebind_capture_has_click;
    bool mousebind_capture_has_move;
    bool mousebind_capture_moved;
    uint32_t mousebind_capture_button;
    int mousebind_capture_fb_button;
    uint32_t mousebind_capture_modifiers;
    enum fbwl_mousebinding_context mousebind_capture_context;
    int mousebind_capture_press_x;
    int mousebind_capture_press_y;
    uint64_t mousebind_capture_target_create_seq;
    bool window_alpha_defaults_configured;
    uint8_t window_alpha_default_focused;
    uint8_t window_alpha_default_unfocused;
    bool default_deco_enabled;

    struct fbwl_keybinding *keybindings;
    size_t keybinding_count;
    char *key_mode;
    bool key_mode_return_active;
    enum fbwl_keybinding_key_kind key_mode_return_kind;
    uint32_t key_mode_return_keycode;
    xkb_keysym_t key_mode_return_sym;
    uint32_t key_mode_return_modifiers;
    bool change_workspace_binding_active;
    bool keychain_active;
    char *keychain_saved_mode;
    uint64_t keychain_start_time_msec;
    struct fbwl_marked_windows marked_windows;

    struct fbwl_mousebinding *mousebindings;
    size_t mousebinding_count;

    struct fbwl_apps_rule *apps_rules;
    size_t apps_rule_count;
    bool apps_rules_rewrite_safe;
    uint64_t apps_rules_generation;
    uint64_t view_create_seq;

    struct fbwm_core wm;
    struct fbwl_view *focused_view;

    struct fbwl_grab grab;

    struct fbwl_focus_config focus;
    enum fbwl_focus_reason focus_reason;
    struct wl_event_source *auto_raise_timer;
    struct fbwl_view *auto_raise_pending_view;
};

struct fbwl_server_bootstrap_options {
    const char *socket_name;
    const char *ipc_socket_path;
    const char *startup_cmd;
    const char *terminal_cmd;
    const char *keys_file;
    const char *apps_file;
    const char *style_file;
    const char *menu_file;
    const char *config_dir;
    const float *background_color;
    int workspaces;
    bool workspaces_set;
    bool enable_xwayland;
    bool log_protocol;
};

char *fbwl_path_join(const char *dir, const char *rel);
bool fbwl_file_exists(const char *path);
char *fbwl_resolve_config_path(const char *config_dir, const char *value);

enum fbwl_focus_model fbwl_parse_focus_model(const char *s);
const char *fbwl_focus_model_str(enum fbwl_focus_model model);
enum fbwm_window_placement_strategy fbwl_parse_window_placement(const char *s);
const char *fbwl_window_placement_str(enum fbwm_window_placement_strategy placement);
enum fbwl_toolbar_placement fbwl_parse_toolbar_placement(const char *s);
const char *fbwl_toolbar_placement_str(enum fbwl_toolbar_placement placement);
uint32_t fbwl_toolbar_tools_default(void);
uint32_t fbwl_parse_toolbar_tools(const char *s);
bool fbwl_parse_layer_num(const char *s, int *out_layer);
enum fbwm_row_placement_direction fbwl_parse_row_dir(const char *s);
const char *fbwl_row_dir_str(enum fbwm_row_placement_direction dir);
enum fbwm_col_placement_direction fbwl_parse_col_dir(const char *s);
const char *fbwl_col_dir_str(enum fbwm_col_placement_direction dir);
enum fbwl_tab_focus_model fbwl_parse_tab_focus_model(const char *s);
const char *fbwl_tab_focus_model_str(enum fbwl_tab_focus_model model);
enum fbwl_tabs_attach_area fbwl_parse_tabs_attach_area(const char *s);
const char *fbwl_tabs_attach_area_str(enum fbwl_tabs_attach_area area);
bool fbwl_titlebar_buttons_parse(const char *s, enum fbwl_decor_hit_kind *out, size_t cap, size_t *out_len);
void fbwl_apply_workspace_names_from_init(struct fbwm_core *wm, const char *csv);

void fbwl_resource_db_free(struct fbwl_resource_db *db);
bool fbwl_resource_db_load_init(struct fbwl_resource_db *db, const char *config_dir);
const char *fbwl_resource_db_get(const struct fbwl_resource_db *db, const char *key);
size_t fbwl_resource_db_max_screen_index(const struct fbwl_resource_db *db);
const char *fbwl_resource_db_get_screen(const struct fbwl_resource_db *db, size_t screen, const char *suffix);
bool fbwl_resource_db_get_bool(const struct fbwl_resource_db *db, const char *key, bool *out);
bool fbwl_resource_db_get_screen_bool(const struct fbwl_resource_db *db, size_t screen, const char *suffix, bool *out);
bool fbwl_resource_db_get_int(const struct fbwl_resource_db *db, const char *key, int *out);
bool fbwl_resource_db_get_screen_int(const struct fbwl_resource_db *db, size_t screen, const char *suffix, int *out);
bool fbwl_resource_db_get_color(const struct fbwl_resource_db *db, const char *key, float rgba[static 4]);
char *fbwl_resource_db_resolve_path(const struct fbwl_resource_db *db, const char *config_dir, const char *key);
char *fbwl_resource_db_discover_path(const struct fbwl_resource_db *db, const char *config_dir, const char *key,
        const char *fallback_rel);

void fbwl_server_load_screen_configs(struct fbwl_server *server, const struct fbwl_resource_db *init);
size_t fbwl_server_screen_index_at(const struct fbwl_server *server, double lx, double ly);
size_t fbwl_server_screen_index_for_view(const struct fbwl_server *server, const struct fbwl_view *view);
const struct fbwl_screen_config *fbwl_server_screen_config(const struct fbwl_server *server, size_t screen);
const struct fbwl_screen_config *fbwl_server_screen_config_at(const struct fbwl_server *server, double lx, double ly);
const struct fbwl_screen_config *fbwl_server_screen_config_for_view(const struct fbwl_server *server,
        const struct fbwl_view *view);

struct fbwl_session_lock_hooks session_lock_hooks(struct fbwl_server *server);

void server_text_input_update_focus(struct fbwl_server *server, struct wlr_surface *surface);
bool server_keyboard_shortcuts_inhibited(struct fbwl_server *server);
void server_update_shortcuts_inhibitor(struct fbwl_server *server);
void focus_view(struct fbwl_view *view);
bool server_focus_request_allowed(struct fbwl_server *server, struct fbwl_view *view, enum fbwl_focus_reason reason,
        const char *why);
void clear_keyboard_focus(struct fbwl_server *server);
bool server_refocus_candidate_allowed(void *userdata, const struct fbwm_view *candidate,
        const struct fbwm_view *reference);
void apply_workspace_visibility(struct fbwl_server *server, const char *why);
void server_workspace_switch_on_head(struct fbwl_server *server, size_t head, int workspace0, const char *why);
void view_set_minimized(struct fbwl_view *view, bool minimized, const char *why);
void server_raise_view(struct fbwl_view *view, const char *why);
void server_lower_view(struct fbwl_view *view, const char *why);
struct fbwl_view *server_strict_mousefocus_view_under_cursor(struct fbwl_server *server);
void server_strict_mousefocus_recheck(struct fbwl_server *server, const char *why);
void server_strict_mousefocus_recheck_after_restack(struct fbwl_server *server, struct fbwl_view *before, const char *why);
struct fbwl_cursor_menu_hooks server_cursor_menu_hooks(struct fbwl_server *server);
void server_grab_update(struct fbwl_server *server);

void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);

int server_fluxbox_mouse_button_from_event(uint32_t button);
int server_fluxbox_mouse_button_from_axis(const struct wlr_pointer_axis_event *event);
enum fbwl_mousebinding_context server_mousebinding_context_at(struct fbwl_server *server,
        struct fbwl_view *view, struct wlr_surface *surface);
bool server_mousebind_capture_handle_press(struct fbwl_server *server, struct fbwl_view *view, struct wlr_surface *surface,
        const struct wlr_pointer_button_event *event, uint32_t modifiers);
bool server_mousebind_capture_handle_release(struct fbwl_server *server, const struct wlr_pointer_button_event *event);
void server_mousebind_capture_handle_motion(struct fbwl_server *server);

void seat_request_cursor(struct wl_listener *listener, void *data);
void cursor_shape_request_set_shape(struct wl_listener *listener, void *data);
void seat_request_set_selection(struct wl_listener *listener, void *data);
void seat_request_set_primary_selection(struct wl_listener *listener, void *data);
void seat_request_start_drag(struct wl_listener *listener, void *data);
void server_new_input(struct wl_listener *listener, void *data);
void server_new_virtual_keyboard(struct wl_listener *listener, void *data);
void server_new_virtual_pointer(struct wl_listener *listener, void *data);

extern const struct fbwm_view_ops fbwl_wm_view_ops;
struct fbwl_xdg_shell_hooks xdg_shell_hooks(struct fbwl_server *server);
struct fbwl_xwayland_hooks xwayland_hooks(struct fbwl_server *server);
void server_xwayland_ready(struct wl_listener *listener, void *data);
void server_xwayland_new_surface(struct wl_listener *listener, void *data);
void server_new_xdg_toplevel(struct wl_listener *listener, void *data);

void decor_theme_set_defaults(struct fbwl_decor_theme *theme);
void server_menu_create_default(struct fbwl_server *server);
void server_menu_create_window(struct fbwl_server *server);
bool server_menu_load_file(struct fbwl_server *server, const char *path);
bool server_menu_load_custom_file(struct fbwl_server *server, const char *path);

bool fbwl_server_outputs_init(struct fbwl_server *server);
bool server_wallpaper_set(struct fbwl_server *server, const char *path, enum fbwl_wallpaper_mode mode);
void server_pseudo_transparency_refresh(struct fbwl_server *server, const char *why);
bool fbwl_server_bootstrap(struct fbwl_server *server, const struct fbwl_server_bootstrap_options *opts);
void fbwl_server_finish(struct fbwl_server *server);
void server_toolbar_ui_rebuild(struct fbwl_server *server);
void server_toolbar_ui_update_position(struct fbwl_server *server);
void server_toolbar_ui_update_iconbar_focus(struct fbwl_server *server);
bool server_toolbar_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button);
void server_toolbar_ui_handle_motion(struct fbwl_server *server);
bool server_toolbar_ui_load_button_tools(struct fbwl_server *server, const struct fbwl_resource_db *init, size_t toolbar_screen);

void server_slit_ui_rebuild(struct fbwl_server *server);
void server_slit_ui_update_position(struct fbwl_server *server);
void server_slit_ui_handle_motion(struct fbwl_server *server);
bool server_slit_ui_handle_button(struct fbwl_server *server, const struct wlr_pointer_button_event *event);
bool server_slit_ui_attach_view(struct fbwl_server *server, struct fbwl_view *view, const char *why);
void server_slit_ui_detach_view(struct fbwl_server *server, struct fbwl_view *view, const char *why);
void server_slit_ui_handle_view_commit(struct fbwl_server *server, struct fbwl_view *view, const char *why);
void server_slit_ui_apply_view_geometry(struct fbwl_server *server, struct fbwl_view *view, const char *why);
void server_tooltip_ui_handle_motion(struct fbwl_server *server);
void server_tabs_ui_handle_motion(struct fbwl_server *server);
void server_cmd_dialog_ui_update_position(struct fbwl_server *server);
void server_cmd_dialog_ui_close(struct fbwl_server *server, const char *why);
void server_cmd_dialog_ui_open(struct fbwl_server *server);
bool server_cmd_dialog_ui_handle_key(struct fbwl_server *server, xkb_keysym_t sym, uint32_t modifiers);
int server_osd_hide_timer(void *data);
int server_auto_raise_timer(void *data);
void server_osd_ui_update_position(struct fbwl_server *server);
void server_osd_ui_destroy(struct fbwl_server *server);
bool server_menu_ui_handle_keypress(struct fbwl_server *server, xkb_keysym_t sym);
bool server_menu_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button);
ssize_t server_menu_ui_index_at(const struct fbwl_menu_ui *ui, int lx, int ly);
void server_menu_ui_set_selected(struct fbwl_server *server, size_t idx);
void server_menu_free(struct fbwl_server *server);

void server_menu_ui_open_workspace(struct fbwl_server *server, int x, int y);
void server_menu_ui_close(struct fbwl_server *server, const char *why);
void server_menu_ui_open_root(struct fbwl_server *server, int x, int y, const char *menu_file);
void server_menu_ui_open_window(struct fbwl_server *server, struct fbwl_view *view, int x, int y);
void server_menu_ui_open_client(struct fbwl_server *server, int x, int y, const char *pattern);
void server_menu_ui_open_slit(struct fbwl_server *server, int x, int y);

void fbwl_server_key_mode_set(struct fbwl_server *server, const char *mode);

struct fbwl_keybindings_hooks keybindings_hooks(struct fbwl_server *server);
void server_reconfigure(struct fbwl_server *server);
void server_request_restart(struct fbwl_server *server, const char *cmd);
void server_keybindings_restart(void *userdata, const char *cmd);
void server_apps_rules_apply_pre_map(struct fbwl_view *view, const struct fbwl_apps_rule *rule);
void server_apps_rules_apply_post_map(struct fbwl_view *view, const struct fbwl_apps_rule *rule);
void server_apps_rules_save_on_close(struct fbwl_view *view);

void server_ipc_command(void *userdata, int client_fd, char *line);

#ifdef HAVE_SYSTEMD
void server_sni_on_change(void *userdata);
#endif
