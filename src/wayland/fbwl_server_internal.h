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
#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_ui_menu.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_ui_cmd_dialog.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_osd.h"
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

struct fbwl_focus_config {
    enum fbwl_focus_model model;
    bool auto_raise;
    int auto_raise_delay_ms;
    bool click_raises;
    bool focus_new_windows;
};

struct fbwl_server;

struct fbwl_shortcuts_inhibitor {
    struct wl_list link;
    struct fbwl_server *server;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
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

    struct fbwl_decor_theme decor_theme;
    struct fbwl_menu *root_menu;
    struct fbwl_menu *window_menu;
    char *menu_file;
    struct fbwl_menu_ui menu_ui;
    struct fbwl_toolbar_ui toolbar_ui;
    struct fbwl_cmd_dialog_ui cmd_dialog_ui;
    struct fbwl_osd_ui osd_ui;

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
    bool has_pointer;

    struct fbwl_keybinding *keybindings;
    size_t keybinding_count;

    struct fbwl_apps_rule *apps_rules;
    size_t apps_rule_count;

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

void fbwl_resource_db_free(struct fbwl_resource_db *db);
bool fbwl_resource_db_load_init(struct fbwl_resource_db *db, const char *config_dir);
const char *fbwl_resource_db_get(const struct fbwl_resource_db *db, const char *key);
bool fbwl_resource_db_get_bool(const struct fbwl_resource_db *db, const char *key, bool *out);
bool fbwl_resource_db_get_int(const struct fbwl_resource_db *db, const char *key, int *out);
bool fbwl_resource_db_get_color(const struct fbwl_resource_db *db, const char *key, float rgba[static 4]);
char *fbwl_resource_db_resolve_path(const struct fbwl_resource_db *db, const char *config_dir, const char *key);
char *fbwl_resource_db_discover_path(const struct fbwl_resource_db *db, const char *config_dir, const char *key,
        const char *fallback_rel);

struct fbwl_session_lock_hooks session_lock_hooks(struct fbwl_server *server);

void server_text_input_update_focus(struct fbwl_server *server, struct wlr_surface *surface);
bool server_keyboard_shortcuts_inhibited(struct fbwl_server *server);
void server_update_shortcuts_inhibitor(struct fbwl_server *server);
void focus_view(struct fbwl_view *view);
void clear_keyboard_focus(struct fbwl_server *server);
void apply_workspace_visibility(struct fbwl_server *server, const char *why);
void view_set_minimized(struct fbwl_view *view, bool minimized, const char *why);
struct fbwl_cursor_menu_hooks server_cursor_menu_hooks(struct fbwl_server *server);

void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);

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

bool fbwl_server_outputs_init(struct fbwl_server *server);
bool fbwl_server_bootstrap(struct fbwl_server *server, const struct fbwl_server_bootstrap_options *opts);
void fbwl_server_finish(struct fbwl_server *server);
void server_toolbar_ui_rebuild(struct fbwl_server *server);
void server_toolbar_ui_update_position(struct fbwl_server *server);
void server_toolbar_ui_update_iconbar_focus(struct fbwl_server *server);
bool server_toolbar_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button);
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

struct fbwl_keybindings_hooks keybindings_hooks(struct fbwl_server *server);
void server_apps_rules_apply_pre_map(struct fbwl_view *view, const struct fbwl_apps_rule *rule);
void server_apps_rules_apply_post_map(struct fbwl_view *view, const struct fbwl_apps_rule *rule);

void server_ipc_command(void *userdata, int client_fd, char *line);

#ifdef HAVE_SYSTEMD
void server_sni_on_change(void *userdata);
#endif
