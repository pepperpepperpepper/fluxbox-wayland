#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>

#include <cairo/cairo.h>
#include <glib-object.h>
#include <pango/pangocairo.h>

#include <linux/input-event-codes.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/edges.h>
#include <wlr/version.h>
#include <wlr/xwayland.h>

#include <xkbcommon/xkbcommon.h>

#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8) | 0)
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#endif

#include "wmcore/fbwm_core.h"
#include "wmcore/fbwm_output.h"
#include "wayland/fbwl_apps_rules.h"
#include "wayland/fbwl_ipc.h"
#include "wayland/fbwl_keybindings.h"
#include "wayland/fbwl_keys_parse.h"
#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_menu_parse.h"
#include "wayland/fbwl_output.h"
#include "wayland/fbwl_output_management.h"
#include "wayland/fbwl_output_power.h"
#include "wayland/fbwl_scene_layers.h"
#include "wayland/fbwl_sni_tray.h"
#include "wayland/fbwl_style_parse.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_ui_text.h"

struct fbwl_server;
struct fbwl_view;

enum fbwl_view_type {
    FBWL_VIEW_XDG,
    FBWL_VIEW_XWAYLAND,
};

struct fbwl_keybinding {
    xkb_keysym_t sym;
    uint32_t modifiers;
    enum fbwl_keybinding_action action;
    int arg;
    char *cmd;
};

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

struct fbwl_toolbar_ui {
    bool enabled;

    int x;
    int y;
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
    struct wlr_scene_buffer *clock_label;

    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *bg;
    struct wlr_scene_rect *highlight;
    struct wlr_scene_rect **cells;
    struct wlr_scene_buffer **labels;
    size_t cell_count;
};

struct fbwl_cmd_dialog_ui {
    bool open;

    int x;
    int y;
    int width;
    int height;

    char *text;

    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *bg;
    struct wlr_scene_buffer *label;
};

struct fbwl_osd_ui {
    bool enabled;
    bool visible;

    int x;
    int y;
    int width;
    int height;

    int last_workspace;

    struct wl_event_source *hide_timer;

    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *bg;
    struct wlr_scene_buffer *label;
};

struct fbwl_init_settings {
    bool set_workspaces;
    int workspaces;
    char *keys_file;
    char *apps_file;
    char *style_file;
    char *menu_file;
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

struct fbwl_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct fbwl_keyboard {
    struct wl_list link;
    struct fbwl_server *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

enum fbwl_cursor_mode {
    FBWL_CURSOR_PASSTHROUGH,
    FBWL_CURSOR_MOVE,
    FBWL_CURSOR_RESIZE,
};

struct fbwl_grab {
    enum fbwl_cursor_mode mode;
    struct fbwl_view *view;
    uint32_t button;
    uint32_t resize_edges;
    double grab_x, grab_y;
    int view_x, view_y;
    int view_w, view_h;
    int last_w, last_h;
};

struct fbwl_pointer_constraint {
    struct fbwl_server *server;
    struct wlr_pointer_constraint_v1 *constraint;
    struct wl_listener set_region;
    struct wl_listener destroy;
};

struct fbwl_session_lock_surface {
    struct wl_list link;
    struct fbwl_server *server;
    struct wlr_session_lock_surface_v1 *lock_surface;
    struct wlr_scene_tree *scene_tree;
    bool has_buffer;
    struct wl_listener surface_commit;
    struct wl_listener destroy;
};

struct fbwl_shortcuts_inhibitor {
    struct wl_list link;
    struct fbwl_server *server;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
};

struct fbwl_xdg_decoration {
    struct fbwl_server *server;
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    enum wlr_xdg_toplevel_decoration_v1_mode desired_mode;
    struct wl_listener surface_map;
    struct wl_listener request_mode;
    struct wl_listener destroy;
};

struct fbwl_idle_inhibitor {
    struct fbwl_server *server;
    struct wlr_idle_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
};

struct fbwl_text_input {
    struct fbwl_server *server;
    struct wlr_text_input_v3 *text_input;
    struct wl_listener enable;
    struct wl_listener commit;
    struct wl_listener disable;
    struct wl_listener destroy;
};

struct fbwl_input_method {
    struct fbwl_server *server;
    struct wlr_input_method_v2 *input_method;
    struct wl_listener commit;
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

    struct wlr_xdg_activation_v1 *xdg_activation;
    struct wl_listener xdg_activation_request_activate;

    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
    struct wl_listener new_xdg_decoration;

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
    struct wlr_keyboard_shortcuts_inhibit_manager_v1 *shortcuts_inhibit_mgr;
    struct wl_listener new_shortcuts_inhibitor;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *active_shortcuts_inhibitor;
    struct wl_list shortcuts_inhibitors;
    struct wl_list keyboards;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_start_drag;

    struct wlr_text_input_manager_v3 *text_input_mgr;
    struct wl_listener new_text_input;
    struct wlr_text_input_v3 *active_text_input;

    struct wlr_input_method_manager_v2 *input_method_mgr;
    struct wl_listener new_input_method;
    struct wlr_input_method_v2 *input_method;

    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
    struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener new_virtual_pointer;

    struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wl_listener new_pointer_constraint;
    struct wlr_pointer_constraint_v1 *active_pointer_constraint;
    bool pointer_phys_valid;
    double pointer_phys_x;
    double pointer_phys_y;
    struct wlr_screencopy_manager_v1 *screencopy_mgr;
    struct wlr_export_dmabuf_manager_v1 *export_dmabuf_mgr;

#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8) | 0)
    struct wlr_ext_image_copy_capture_manager_v1 *ext_image_copy_capture_mgr;
    struct wlr_ext_output_image_capture_source_manager_v1 *ext_output_image_capture_source_mgr;
#endif

    struct wlr_viewporter *viewporter;
    struct wlr_fractional_scale_manager_v1 *fractional_scale_mgr;
    struct wlr_xdg_output_manager_v1 *xdg_output_mgr;

    struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
    struct wl_listener new_idle_inhibitor;
    int idle_inhibitor_count;

    struct wlr_idle_notifier_v1 *idle_notifier;
    bool idle_inhibited;

    struct wlr_session_lock_manager_v1 *session_lock_mgr;
    struct wl_listener new_session_lock;
    struct wlr_session_lock_v1 *session_lock;
    struct wl_listener session_lock_new_surface;
    struct wl_listener session_lock_unlock;
    struct wl_listener session_lock_destroy;
    struct wl_list session_lock_surfaces;
    bool session_locked;
    bool session_lock_sent_locked;
    size_t session_lock_expected_surfaces;

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
};

static struct wlr_surface *view_wlr_surface(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }

    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->base->surface : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->surface : NULL;
    default:
        return NULL;
    }
}

static struct fbwl_view *server_view_from_surface(struct wlr_surface *surface) {
    if (surface == NULL) {
        return NULL;
    }

    struct wlr_xdg_toplevel *xdg_toplevel = wlr_xdg_toplevel_try_from_wlr_surface(surface);
    if (xdg_toplevel != NULL && xdg_toplevel->base != NULL) {
        struct wlr_scene_tree *tree = xdg_toplevel->base->data;
        if (tree != NULL && tree->node.data != NULL) {
            return tree->node.data;
        }
    }

    struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
    if (xsurface != NULL && xsurface->data != NULL) {
        return xsurface->data;
    }

    return NULL;
}

static void clear_keyboard_focus(struct fbwl_server *server);
static void server_text_input_update_focus(struct fbwl_server *server, struct wlr_surface *surface);
static void server_update_shortcuts_inhibitor(struct fbwl_server *server);
static void server_toolbar_ui_rebuild(struct fbwl_server *server);
static void server_toolbar_ui_update_current(struct fbwl_server *server);
static void server_toolbar_ui_update_position(struct fbwl_server *server);
static bool server_toolbar_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button);
static void server_toolbar_ui_clock_render(struct fbwl_server *server);
static void server_toolbar_ui_update_iconbar_focus(struct fbwl_server *server);

#ifdef HAVE_SYSTEMD
static void server_sni_on_change(void *userdata);
#endif
static void server_cmd_dialog_ui_open(struct fbwl_server *server);
static void server_cmd_dialog_ui_close(struct fbwl_server *server, const char *why);
static void server_cmd_dialog_ui_update_position(struct fbwl_server *server);
static bool server_cmd_dialog_ui_handle_key(struct fbwl_server *server, xkb_keysym_t sym, uint32_t modifiers);
static int server_osd_hide_timer(void *data);
static void server_osd_ui_update_position(struct fbwl_server *server);
static void server_osd_ui_show_workspace(struct fbwl_server *server, int workspace);
static void server_osd_ui_destroy(struct fbwl_server *server);
static void server_menu_ui_close(struct fbwl_server *server, const char *why);
static void server_menu_ui_open_root(struct fbwl_server *server, int x, int y);
static void server_menu_ui_open_window(struct fbwl_server *server, struct fbwl_view *view, int x, int y);
static bool server_menu_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button);
static ssize_t server_menu_ui_index_at(const struct fbwl_menu_ui *ui, int lx, int ly);
static void server_menu_ui_set_selected(struct fbwl_server *server, size_t idx);
static void server_menu_free(struct fbwl_server *server);
static void server_background_update_output(struct fbwl_server *server, struct fbwl_output *output);
static void server_background_update_all(struct fbwl_server *server);

static const char *view_title(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }

    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->title : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->title : NULL;
    default:
        return NULL;
    }
}

static const char *view_app_id(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }

    switch (view->type) {
    case FBWL_VIEW_XDG:
        return view->xdg_toplevel != NULL ? view->xdg_toplevel->app_id : NULL;
    case FBWL_VIEW_XWAYLAND:
        return view->xwayland_surface != NULL ? view->xwayland_surface->class : NULL;
    default:
        return NULL;
    }
}

static int view_current_width(const struct fbwl_view *view) {
    if (view == NULL) {
        return 0;
    }
    if (view->width > 0) {
        return view->width;
    }

    struct wlr_surface *surface = view_wlr_surface(view);
    if (surface != NULL) {
        return surface->current.width;
    }

    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        return view->xwayland_surface->width;
    }
    return 0;
}

static int view_current_height(const struct fbwl_view *view) {
    if (view == NULL) {
        return 0;
    }
    if (view->height > 0) {
        return view->height;
    }

    struct wlr_surface *surface = view_wlr_surface(view);
    if (surface != NULL) {
        return surface->current.height;
    }

    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        return view->xwayland_surface->height;
    }
    return 0;
}

static void view_set_activated(struct fbwl_view *view, bool activated) {
    if (view == NULL) {
        return;
    }

    switch (view->type) {
    case FBWL_VIEW_XDG:
        if (view->xdg_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(view->xdg_toplevel, activated);
        }
        break;
    case FBWL_VIEW_XWAYLAND:
        if (view->xwayland_surface != NULL) {
            wlr_xwayland_surface_activate(view->xwayland_surface, activated);
            if (activated) {
                wlr_xwayland_surface_offer_focus(view->xwayland_surface);
            }
        }
        break;
    default:
        break;
    }
}

static int decor_theme_button_size(const struct fbwl_decor_theme *theme) {
    if (theme == NULL) {
        return 0;
    }
    int size = theme->title_height - 2 * theme->button_margin;
    if (size < 1) {
        size = 1;
    }
    return size;
}

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

static void view_decor_apply_enabled(struct fbwl_view *view) {
    if (view == NULL || view->decor_tree == NULL) {
        return;
    }

    const bool show = view->decor_enabled && !view->fullscreen;
    wlr_scene_node_set_enabled(&view->decor_tree->node, show);
}

static void view_decor_set_active(struct fbwl_view *view, bool active) {
    if (view == NULL) {
        return;
    }
    if (view->server == NULL) {
        return;
    }

    view->decor_active = active;
    if (view->decor_titlebar != NULL) {
        const struct fbwl_decor_theme *theme = &view->server->decor_theme;
        const float *color = active ? theme->titlebar_active : theme->titlebar_inactive;
        wlr_scene_rect_set_color(view->decor_titlebar, color);
    }
}

static void view_decor_update_title_text(struct fbwl_view *view) {
    if (view == NULL || view->decor_tree == NULL || view->server == NULL) {
        return;
    }

    const struct fbwl_decor_theme *theme = &view->server->decor_theme;
    const int title_h = theme->title_height;
    const int w = view_current_width(view);
    if (w < 1 || title_h < 1) {
        return;
    }

    if (view->decor_title_text == NULL) {
        view->decor_title_text = wlr_scene_buffer_create(view->decor_tree, NULL);
        if (view->decor_title_text == NULL) {
            return;
        }
    }

    wlr_scene_node_set_position(&view->decor_title_text->node, 0, -title_h);

    if (!view->decor_enabled || view->fullscreen) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }

    const char *title = view_title(view);
    if (title == NULL || *title == '\0') {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }

    const int btn_size = decor_theme_button_size(theme);
    const int reserved_right =
        theme->button_margin + (btn_size * 3) + (theme->button_spacing * 2) + theme->button_margin;
    int text_w = w - reserved_right;
    if (text_w < 1) {
        text_w = w;
    }
    if (text_w < 1) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }

    if (view->decor_title_text_cache != NULL &&
            view->decor_title_text_cache_w == text_w &&
            strcmp(view->decor_title_text_cache, title) == 0) {
        return;
    }

    free(view->decor_title_text_cache);
    view->decor_title_text_cache = strdup(title);
    if (view->decor_title_text_cache == NULL) {
        view->decor_title_text_cache_w = 0;
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        return;
    }
    view->decor_title_text_cache_w = text_w;

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct wlr_buffer *buf = fbwl_text_buffer_create(title, text_w, title_h, 8, fg);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(view->decor_title_text, NULL);
        view->decor_title_text_cache_w = 0;
        return;
    }

    wlr_scene_buffer_set_buffer(view->decor_title_text, buf);
    wlr_buffer_drop(buf);

    wlr_log(WLR_INFO, "Decor: title-render %s", title);
}

static void view_decor_update(struct fbwl_view *view) {
    if (view == NULL || view->decor_tree == NULL || view->server == NULL) {
        return;
    }

    view_decor_apply_enabled(view);

    const int w = view_current_width(view);
    const int h = view_current_height(view);
    if (w < 1 || h < 1) {
        return;
    }

    const struct fbwl_decor_theme *theme = &view->server->decor_theme;
    const int border = theme->border_width;
    const int title_h = theme->title_height;

    if (view->decor_titlebar != NULL) {
        wlr_scene_rect_set_size(view->decor_titlebar, w, title_h);
        wlr_scene_node_set_position(&view->decor_titlebar->node, 0, -title_h);
        const float *color = view->decor_active ? theme->titlebar_active : theme->titlebar_inactive;
        wlr_scene_rect_set_color(view->decor_titlebar, color);
    }

    if (view->decor_border_top != NULL) {
        wlr_scene_rect_set_size(view->decor_border_top, w + 2 * border, border);
        wlr_scene_node_set_position(&view->decor_border_top->node, -border, -title_h - border);
    }
    if (view->decor_border_bottom != NULL) {
        wlr_scene_rect_set_size(view->decor_border_bottom, w + 2 * border, border);
        wlr_scene_node_set_position(&view->decor_border_bottom->node, -border, h);
    }
    if (view->decor_border_left != NULL) {
        wlr_scene_rect_set_size(view->decor_border_left, border, title_h + border + h + border);
        wlr_scene_node_set_position(&view->decor_border_left->node, -border, -title_h - border);
    }
    if (view->decor_border_right != NULL) {
        wlr_scene_rect_set_size(view->decor_border_right, border, title_h + border + h + border);
        wlr_scene_node_set_position(&view->decor_border_right->node, w, -title_h - border);
    }

    const int btn_size = decor_theme_button_size(theme);
    const int btn_y = -title_h + theme->button_margin;
    int btn_x = w - theme->button_margin - btn_size;
    if (view->decor_btn_close != NULL) {
        wlr_scene_rect_set_size(view->decor_btn_close, btn_size, btn_size);
        wlr_scene_node_set_position(&view->decor_btn_close->node, btn_x, btn_y);
    }
    btn_x -= btn_size + theme->button_spacing;
    if (view->decor_btn_max != NULL) {
        wlr_scene_rect_set_size(view->decor_btn_max, btn_size, btn_size);
        wlr_scene_node_set_position(&view->decor_btn_max->node, btn_x, btn_y);
    }
    btn_x -= btn_size + theme->button_spacing;
    if (view->decor_btn_min != NULL) {
        wlr_scene_rect_set_size(view->decor_btn_min, btn_size, btn_size);
        wlr_scene_node_set_position(&view->decor_btn_min->node, btn_x, btn_y);
    }

    view_decor_update_title_text(view);
}

static void view_decor_create(struct fbwl_view *view) {
    if (view == NULL || view->scene_tree == NULL || view->decor_tree != NULL) {
        return;
    }

    view->decor_tree = wlr_scene_tree_create(view->scene_tree);
    if (view->decor_tree == NULL) {
        return;
    }

    if (view->server != NULL) {
        const struct fbwl_decor_theme *theme = &view->server->decor_theme;
        view->decor_titlebar = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->titlebar_inactive);
        view->decor_title_text = wlr_scene_buffer_create(view->decor_tree, NULL);
        view->decor_border_top = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->border_color);
        view->decor_border_bottom = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->border_color);
        view->decor_border_left = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->border_color);
        view->decor_border_right = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->border_color);
        view->decor_btn_close = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_close_color);
        view->decor_btn_max = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_max_color);
        view->decor_btn_min = wlr_scene_rect_create(view->decor_tree, 1, 1, theme->btn_min_color);
    }

    view->decor_enabled = false;
    view->decor_active = false;
    wlr_scene_node_set_enabled(&view->decor_tree->node, false);
    view_decor_update(view);
}

static void view_decor_set_enabled(struct fbwl_view *view, bool enabled) {
    if (view == NULL) {
        return;
    }

    view->decor_enabled = enabled;
    view_decor_apply_enabled(view);
}

static struct fbwl_decor_hit view_decor_hit_test(const struct fbwl_view *view, double lx, double ly) {
    struct fbwl_decor_hit hit = {0};
    if (view == NULL || view->decor_tree == NULL || view->server == NULL ||
            !view->decor_enabled || view->fullscreen) {
        return hit;
    }

    const int w = view_current_width(view);
    const int h = view_current_height(view);
    if (w < 1 || h < 1) {
        return hit;
    }

    const struct fbwl_decor_theme *theme = &view->server->decor_theme;
    const int border = theme->border_width;
    const int title_h = theme->title_height;
    const int btn_size = decor_theme_button_size(theme);
    const int btn_margin = theme->button_margin;

    const double x = lx - view->x;
    const double y = ly - view->y;

    if (x < -border || x >= w + border) {
        return hit;
    }
    if (y < -title_h - border || y >= h + border) {
        return hit;
    }

    // Titlebar buttons
    if (y >= -title_h + btn_margin && y < -title_h + btn_margin + btn_size) {
        const int close_x0 = w - btn_margin - btn_size;
        const int max_x0 = close_x0 - theme->button_spacing - btn_size;
        const int min_x0 = max_x0 - theme->button_spacing - btn_size;

        if (x >= close_x0 && x < close_x0 + btn_size) {
            hit.kind = FBWL_DECOR_HIT_BTN_CLOSE;
            return hit;
        }
        if (x >= max_x0 && x < max_x0 + btn_size) {
            hit.kind = FBWL_DECOR_HIT_BTN_MAX;
            return hit;
        }
        if (x >= min_x0 && x < min_x0 + btn_size) {
            hit.kind = FBWL_DECOR_HIT_BTN_MIN;
            return hit;
        }
    }

    // Titlebar drag
    if (y >= -title_h && y < 0) {
        hit.kind = FBWL_DECOR_HIT_TITLEBAR;
        return hit;
    }

    uint32_t edges = WLR_EDGE_NONE;
    if (x >= -border && x < 0) {
        edges |= WLR_EDGE_LEFT;
    }
    if (x >= w && x < w + border) {
        edges |= WLR_EDGE_RIGHT;
    }
    if (y >= -title_h - border && y < -title_h) {
        edges |= WLR_EDGE_TOP;
    }
    if (y >= h && y < h + border) {
        edges |= WLR_EDGE_BOTTOM;
    }

    if (edges != WLR_EDGE_NONE) {
        hit.kind = FBWL_DECOR_HIT_RESIZE;
        hit.edges = edges;
    }
    return hit;
}

static void server_output_management_arrange_layers_on_output(void *userdata, struct wlr_output *wlr_output) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_scene_layers_arrange_layer_surfaces_on_output(server->output_layout, &server->outputs, &server->layer_surfaces,
        wlr_output);
}

static void server_output_power_set_mode(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_power_set_mode);
    if (server == NULL) {
        return;
    }
    fbwl_output_power_handle_set_mode(data, server->output_manager, &server->outputs, server->output_layout);
}

static void server_session_lock_maybe_send_locked(struct fbwl_server *server) {
    if (server == NULL || server->session_lock == NULL || server->session_lock_sent_locked) {
        return;
    }

    if (server->session_lock_expected_surfaces < 1) {
        server->session_lock_expected_surfaces = 1;
    }

    size_t surface_count = 0;
    size_t committed_count = 0;
    struct fbwl_session_lock_surface *ls;
    wl_list_for_each(ls, &server->session_lock_surfaces, link) {
        surface_count++;
        if (ls->has_buffer) {
            committed_count++;
        }
    }

    if (surface_count < server->session_lock_expected_surfaces ||
            committed_count < server->session_lock_expected_surfaces) {
        return;
    }

    wlr_session_lock_v1_send_locked(server->session_lock);
    server->session_lock_sent_locked = true;
    wlr_log(WLR_INFO, "SessionLock: locked");
}

static void session_lock_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_session_lock_surface *ls = wl_container_of(listener, ls, surface_commit);
    struct fbwl_server *server = ls != NULL ? ls->server : NULL;
    if (server == NULL || ls->lock_surface == NULL || ls->lock_surface->surface == NULL) {
        return;
    }

    if (ls->has_buffer) {
        return;
    }

    if (!wlr_surface_has_buffer(ls->lock_surface->surface)) {
        return;
    }

    ls->has_buffer = true;
    server_session_lock_maybe_send_locked(server);
}

static void session_lock_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_session_lock_surface *ls = wl_container_of(listener, ls, destroy);
    fbwl_cleanup_listener(&ls->surface_commit);
    fbwl_cleanup_listener(&ls->destroy);
    if (ls->scene_tree != NULL) {
        wlr_scene_node_destroy(&ls->scene_tree->node);
        ls->scene_tree = NULL;
    }
    wl_list_remove(&ls->link);
    free(ls);
}

static void server_session_lock_new_surface(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, session_lock_new_surface);
    struct wlr_session_lock_surface_v1 *lock_surface = data;
    if (server == NULL || lock_surface == NULL || lock_surface->surface == NULL) {
        return;
    }

    struct fbwl_session_lock_surface *ls = calloc(1, sizeof(*ls));
    if (ls == NULL) {
        return;
    }
    ls->server = server;
    ls->lock_surface = lock_surface;
    wl_list_insert(&server->session_lock_surfaces, &ls->link);

    struct wlr_scene_tree *parent =
        server->layer_overlay != NULL ? server->layer_overlay : &server->scene->tree;
    ls->scene_tree = wlr_scene_tree_create(parent);
    if (ls->scene_tree != NULL) {
        (void)wlr_scene_surface_create(ls->scene_tree, lock_surface->surface);

        struct wlr_box box = {0};
        if (server->output_layout != NULL && lock_surface->output != NULL) {
            wlr_output_layout_get_box(server->output_layout, lock_surface->output, &box);
        }
        wlr_scene_node_set_position(&ls->scene_tree->node, box.x, box.y);
        wlr_scene_node_raise_to_top(&ls->scene_tree->node);
    }

    ls->destroy.notify = session_lock_surface_destroy;
    wl_signal_add(&lock_surface->events.destroy, &ls->destroy);

    ls->has_buffer = wlr_surface_has_buffer(lock_surface->surface);
    ls->surface_commit.notify = session_lock_surface_commit;
    wl_signal_add(&lock_surface->surface->events.commit, &ls->surface_commit);

    uint32_t width = 0;
    uint32_t height = 0;
    if (lock_surface->output != NULL) {
        width = (uint32_t)lock_surface->output->width;
        height = (uint32_t)lock_surface->output->height;
    }
    if (width == 0) {
        width = 1280;
    }
    if (height == 0) {
        height = 720;
    }
    (void)wlr_session_lock_surface_v1_configure(lock_surface, width, height);

    clear_keyboard_focus(server);
    if (server->seat != NULL) {
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
        if (keyboard != NULL) {
            wlr_seat_keyboard_notify_enter(server->seat, lock_surface->surface,
                keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
        }
    }

    server_update_shortcuts_inhibitor(server);
    server_session_lock_maybe_send_locked(server);
}

static void server_session_lock_unlock(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, session_lock_unlock);
    struct wlr_session_lock_v1 *lock = data;
    if (server == NULL || lock == NULL) {
        return;
    }
    wlr_log(WLR_INFO, "SessionLock: unlock");
    wlr_session_lock_v1_destroy(lock);
}

static void server_session_lock_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_server *server = wl_container_of(listener, server, session_lock_destroy);
    if (server == NULL) {
        return;
    }

    wlr_log(WLR_INFO, "SessionLock: destroy");

    server->session_lock = NULL;
    server->session_locked = false;
    server->session_lock_sent_locked = false;
    server->session_lock_expected_surfaces = 0;

    fbwl_cleanup_listener(&server->session_lock_new_surface);
    fbwl_cleanup_listener(&server->session_lock_unlock);
    fbwl_cleanup_listener(&server->session_lock_destroy);

    struct fbwl_session_lock_surface *ls, *tmp;
    wl_list_for_each_safe(ls, tmp, &server->session_lock_surfaces, link) {
        fbwl_cleanup_listener(&ls->surface_commit);
        fbwl_cleanup_listener(&ls->destroy);
        if (ls->scene_tree != NULL) {
            wlr_scene_node_destroy(&ls->scene_tree->node);
            ls->scene_tree = NULL;
        }
        wl_list_remove(&ls->link);
        free(ls);
    }
}

static void server_new_session_lock(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_session_lock);
    struct wlr_session_lock_v1 *lock = data;
    if (server == NULL || lock == NULL) {
        return;
    }

    if (server->session_lock != NULL) {
        wlr_log(WLR_INFO, "SessionLock: rejecting (already locked)");
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    server->session_lock = lock;
    server->session_locked = true;
    server->session_lock_sent_locked = false;
    server->session_lock_expected_surfaces = fbwl_output_count(&server->outputs);
    if (server->session_lock_expected_surfaces < 1) {
        server->session_lock_expected_surfaces = 1;
    }

    wlr_log(WLR_INFO, "SessionLock: new lock");

    clear_keyboard_focus(server);
    if (server->seat != NULL) {
        wlr_seat_pointer_clear_focus(server->seat);
    }
    server_text_input_update_focus(server, NULL);

    server->session_lock_new_surface.notify = server_session_lock_new_surface;
    wl_signal_add(&lock->events.new_surface, &server->session_lock_new_surface);
    server->session_lock_unlock.notify = server_session_lock_unlock;
    wl_signal_add(&lock->events.unlock, &server->session_lock_unlock);
    server->session_lock_destroy.notify = server_session_lock_destroy;
    wl_signal_add(&lock->events.destroy, &server->session_lock_destroy);
}

static void server_output_manager_test(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_manager_test);
    struct wlr_output_configuration_v1 *config = data;

    wlr_log(WLR_INFO, "OutputMgmt: test serial=%u", config->serial);
    const bool ok = fbwl_output_management_apply_config(server->backend, server->output_layout, &server->outputs,
        config, true, server_output_management_arrange_layers_on_output, server);
    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

static void server_output_manager_apply(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_manager_apply);
    struct wlr_output_configuration_v1 *config = data;

    wlr_log(WLR_INFO, "OutputMgmt: apply serial=%u", config->serial);
    const bool ok = fbwl_output_management_apply_config(server->backend, server->output_layout, &server->outputs,
        config, false, server_output_management_arrange_layers_on_output, server);
    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
        fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
        server_background_update_all(server);
        server_toolbar_ui_update_position(server);
        server_cmd_dialog_ui_update_position(server);
        server_osd_ui_update_position(server);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

static int handle_signal(int signo, void *data) {
    struct fbwl_server *server = data;
    wlr_log(WLR_INFO, "Signal %d received, terminating", signo);
    wl_display_terminate(server->wl_display);
    return 0;
}

static uint8_t float_to_u8_clamped(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

static uint32_t rgb24_from_rgba(const float rgba[4]) {
    if (rgba == NULL) {
        return 0;
    }
    const uint32_t r = float_to_u8_clamped(rgba[0]);
    const uint32_t g = float_to_u8_clamped(rgba[1]);
    const uint32_t b = float_to_u8_clamped(rgba[2]);
    return (r << 16) | (g << 8) | b;
}

static void server_background_update_output(struct fbwl_server *server, struct fbwl_output *output) {
    if (server == NULL || output == NULL || server->output_layout == NULL || server->layer_background == NULL ||
            output->wlr_output == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);

    if (box.width < 1 || box.height < 1) {
        if (output->background_rect != NULL) {
            wlr_scene_node_destroy(&output->background_rect->node);
            output->background_rect = NULL;
        }
        return;
    }

    if (output->background_rect == NULL) {
        output->background_rect =
            wlr_scene_rect_create(server->layer_background, box.width, box.height, server->background_color);
        if (output->background_rect == NULL) {
            wlr_log(WLR_ERROR, "Background: failed to create rect");
            return;
        }
    } else {
        wlr_scene_rect_set_size(output->background_rect, box.width, box.height);
        wlr_scene_rect_set_color(output->background_rect, server->background_color);
    }

    wlr_scene_node_set_position(&output->background_rect->node, box.x, box.y);
    wlr_scene_node_lower_to_bottom(&output->background_rect->node);

    const uint32_t rgb = rgb24_from_rgba(server->background_color);
    wlr_log(WLR_INFO, "Background: output name=%s x=%d y=%d w=%d h=%d rgb=#%06x",
        output->wlr_output->name != NULL ? output->wlr_output->name : "(unnamed)",
        box.x, box.y, box.width, box.height, rgb);
}

static void server_background_update_all(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    struct fbwl_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        server_background_update_output(server, out);
    }
}

static void server_output_destroyed(void *userdata, struct wlr_output *wlr_output) {
    struct fbwl_server *server = userdata;
    if (server == NULL || wlr_output == NULL) {
        return;
    }

    if (server->session_lock != NULL && !server->session_lock_sent_locked &&
            server->session_lock_expected_surfaces > 1) {
        server->session_lock_expected_surfaces--;
        server_session_lock_maybe_send_locked(server);
    }

    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view != NULL && view->foreign_output == wlr_output) {
            view->foreign_output = NULL;
        }
    }

    fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
    server_toolbar_ui_update_position(server);
    server_cmd_dialog_ui_update_position(server);
    server_osd_ui_update_position(server);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    struct fbwl_output *output = fbwl_output_create(&server->outputs, wlr_output,
        server->allocator, server->renderer,
        server->output_layout, server->scene, server->scene_layout,
        server_output_destroyed, server);
    if (output == NULL) {
        wlr_log(WLR_ERROR, "Output: failed to create output");
        return;
    }

    server_background_update_output(server, output);
    fbwl_scene_layers_arrange_layer_surfaces_on_output(server->output_layout, &server->outputs, &server->layer_surfaces,
        wlr_output);
    fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
    server_toolbar_ui_update_position(server);
    server_cmd_dialog_ui_update_position(server);
    server_osd_ui_update_position(server);
}

static void server_update_input_method(struct fbwl_server *server) {
    if (server == NULL || server->input_method == NULL) {
        return;
    }

    struct wlr_input_method_v2 *im = server->input_method;
    struct wlr_text_input_v3 *ti = server->active_text_input;

    const bool want_active =
        ti != NULL && ti->current_enabled && ti->focused_surface != NULL;

    if (want_active) {
        if (!im->client_active) {
            wlr_input_method_v2_send_activate(im);
        }

        const struct wlr_text_input_v3_state *st = &ti->current;
        if ((ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) != 0) {
            const char *txt = st->surrounding.text != NULL ? st->surrounding.text : "";
            wlr_input_method_v2_send_surrounding_text(im, txt,
                st->surrounding.cursor, st->surrounding.anchor);
            wlr_input_method_v2_send_text_change_cause(im, st->text_change_cause);
        }
        if ((ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) != 0) {
            wlr_input_method_v2_send_content_type(im,
                st->content_type.hint, st->content_type.purpose);
        }

        wlr_input_method_v2_send_done(im);
    } else {
        if (im->client_active) {
            wlr_input_method_v2_send_deactivate(im);
            wlr_input_method_v2_send_done(im);
        }
    }
}

static void server_text_input_update_focus(struct fbwl_server *server, struct wlr_surface *surface) {
    if (server == NULL || server->text_input_mgr == NULL) {
        return;
    }

    struct wl_client *focused_client = NULL;
    if (surface != NULL && surface->resource != NULL) {
        focused_client = wl_resource_get_client(surface->resource);
    }

    struct wlr_text_input_v3 *ti;
    wl_list_for_each(ti, &server->text_input_mgr->text_inputs, link) {
        if (ti->resource == NULL) {
            continue;
        }

        struct wl_client *client = wl_resource_get_client(ti->resource);
        if (surface != NULL && client == focused_client) {
            if (ti->focused_surface != surface) {
                wlr_text_input_v3_send_enter(ti, surface);
            }
        } else if (ti->focused_surface != NULL) {
            wlr_text_input_v3_send_leave(ti);
        }
    }

    if (server->active_text_input != NULL &&
            server->active_text_input->focused_surface != surface) {
        server->active_text_input = NULL;
        server_update_input_method(server);
    }
}

static void text_input_enable(struct wl_listener *listener, void *data) {
    struct fbwl_text_input *ti = wl_container_of(listener, ti, enable);
    struct fbwl_server *server = ti->server;
    struct wlr_text_input_v3 *text_input = data;

    if (server == NULL || text_input == NULL) {
        return;
    }

    if (server->active_text_input != NULL && server->active_text_input != text_input) {
        wlr_log(WLR_INFO, "TextInput: ignoring enable (another text input already enabled)");
        return;
    }

    server->active_text_input = text_input;
    wlr_log(WLR_INFO, "TextInput: enable features=0x%x", text_input->active_features);
    server_update_input_method(server);
}

static void text_input_commit(struct wl_listener *listener, void *data) {
    struct fbwl_text_input *ti = wl_container_of(listener, ti, commit);
    struct fbwl_server *server = ti->server;
    struct wlr_text_input_v3 *text_input = data;
    if (server == NULL || text_input == NULL) {
        return;
    }
    if (server->active_text_input == text_input) {
        server_update_input_method(server);
    }
}

static void text_input_disable(struct wl_listener *listener, void *data) {
    struct fbwl_text_input *ti = wl_container_of(listener, ti, disable);
    struct fbwl_server *server = ti->server;
    struct wlr_text_input_v3 *text_input = data;
    if (server == NULL || text_input == NULL) {
        return;
    }
    if (server->active_text_input == text_input) {
        server->active_text_input = NULL;
        wlr_log(WLR_INFO, "TextInput: disable");
        server_update_input_method(server);
    }
}

static void text_input_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_text_input *ti = wl_container_of(listener, ti, destroy);
    struct fbwl_server *server = ti->server;
    if (server != NULL && server->active_text_input == ti->text_input) {
        server->active_text_input = NULL;
        server_update_input_method(server);
    }

    fbwl_cleanup_listener(&ti->enable);
    fbwl_cleanup_listener(&ti->commit);
    fbwl_cleanup_listener(&ti->disable);
    fbwl_cleanup_listener(&ti->destroy);
    free(ti);
}

static void server_new_text_input(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_text_input);
    struct wlr_text_input_v3 *text_input = data;

    struct fbwl_text_input *ti = calloc(1, sizeof(*ti));
    if (ti == NULL) {
        wlr_log(WLR_ERROR, "TextInput: out of memory");
        return;
    }
    ti->server = server;
    ti->text_input = text_input;

    ti->enable.notify = text_input_enable;
    wl_signal_add(&text_input->events.enable, &ti->enable);
    ti->commit.notify = text_input_commit;
    wl_signal_add(&text_input->events.commit, &ti->commit);
    ti->disable.notify = text_input_disable;
    wl_signal_add(&text_input->events.disable, &ti->disable);
    ti->destroy.notify = text_input_destroy;
    wl_signal_add(&text_input->events.destroy, &ti->destroy);

    struct wlr_surface *focused = NULL;
    if (server->seat != NULL) {
        focused = server->seat->keyboard_state.focused_surface;
    }
    if (focused != NULL && focused->resource != NULL && text_input->resource != NULL) {
        struct wl_client *focused_client = wl_resource_get_client(focused->resource);
        struct wl_client *client = wl_resource_get_client(text_input->resource);
        if (client == focused_client) {
            wlr_text_input_v3_send_enter(text_input, focused);
        }
    }
}

static void input_method_commit(struct wl_listener *listener, void *data) {
    struct fbwl_input_method *fim = wl_container_of(listener, fim, commit);
    struct fbwl_server *server = fim->server;
    struct wlr_input_method_v2 *im = data;
    if (server == NULL || im == NULL) {
        return;
    }

    struct wlr_text_input_v3 *ti = server->active_text_input;
    if (ti == NULL || !ti->current_enabled) {
        return;
    }

    const uint32_t del_before = im->current.delete.before_length != 0 || im->current.delete.after_length != 0
        ? im->current.delete.before_length
        : im->pending.delete.before_length;
    const uint32_t del_after = im->current.delete.before_length != 0 || im->current.delete.after_length != 0
        ? im->current.delete.after_length
        : im->pending.delete.after_length;

    const struct wlr_input_method_v2_preedit_string *preedit =
        im->current.preedit.text != NULL ? &im->current.preedit : &im->pending.preedit;
    const char *commit_text =
        im->current.commit_text != NULL ? im->current.commit_text : im->pending.commit_text;

    bool sent = false;
    if (del_before > 0 || del_after > 0) {
        wlr_text_input_v3_send_delete_surrounding_text(ti, del_before, del_after);
        sent = true;
    }
    if (preedit->text != NULL) {
        wlr_text_input_v3_send_preedit_string(ti, preedit->text,
            preedit->cursor_begin, preedit->cursor_end);
        sent = true;
    }
    if (commit_text != NULL) {
        wlr_log(WLR_INFO, "InputMethod: commit '%s'", commit_text);
        wlr_text_input_v3_send_commit_string(ti, commit_text);
        sent = true;
    }
    if (sent) {
        wlr_text_input_v3_send_done(ti);
    }
}

static void input_method_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_input_method *fim = wl_container_of(listener, fim, destroy);
    struct fbwl_server *server = fim->server;
    if (server != NULL && server->input_method == fim->input_method) {
        server->input_method = NULL;
        server_update_input_method(server);
    }

    fbwl_cleanup_listener(&fim->commit);
    fbwl_cleanup_listener(&fim->destroy);
    free(fim);
}

static void server_new_input_method(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_input_method);
    struct wlr_input_method_v2 *input_method = data;

    if (server->input_method != NULL && server->input_method != input_method) {
        wlr_log(WLR_INFO, "InputMethod: refusing second input method");
        wlr_input_method_v2_send_unavailable(input_method);
        return;
    }

    struct fbwl_input_method *fim = calloc(1, sizeof(*fim));
    if (fim == NULL) {
        wlr_log(WLR_ERROR, "InputMethod: out of memory");
        wlr_input_method_v2_send_unavailable(input_method);
        return;
    }
    fim->server = server;
    fim->input_method = input_method;

    fim->commit.notify = input_method_commit;
    wl_signal_add(&input_method->events.commit, &fim->commit);
    fim->destroy.notify = input_method_destroy;
    wl_signal_add(&input_method->events.destroy, &fim->destroy);

    server->input_method = input_method;
    server_update_input_method(server);
}

static bool server_keyboard_shortcuts_inhibited(struct fbwl_server *server) {
    if (server == NULL || server->seat == NULL) {
        return false;
    }
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhib = server->active_shortcuts_inhibitor;
    if (inhib == NULL || !inhib->active) {
        return false;
    }
    return server->seat->keyboard_state.focused_surface == inhib->surface;
}

static struct wlr_keyboard_shortcuts_inhibitor_v1 *server_find_shortcuts_inhibitor(
        struct fbwl_server *server, struct wlr_surface *surface) {
    if (server == NULL || surface == NULL || server->seat == NULL) {
        return NULL;
    }

    struct fbwl_shortcuts_inhibitor *si;
    wl_list_for_each(si, &server->shortcuts_inhibitors, link) {
        struct wlr_keyboard_shortcuts_inhibitor_v1 *inhib = si->inhibitor;
        if (inhib != NULL && inhib->seat == server->seat && inhib->surface == surface) {
            return inhib;
        }
    }
    return NULL;
}

static void server_update_shortcuts_inhibitor(struct fbwl_server *server) {
    if (server == NULL || server->shortcuts_inhibit_mgr == NULL || server->seat == NULL) {
        return;
    }

    struct wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *want =
        server_find_shortcuts_inhibitor(server, focused_surface);

    if (want == server->active_shortcuts_inhibitor) {
        if (want != NULL && !want->active) {
            wlr_keyboard_shortcuts_inhibitor_v1_activate(want);
            wlr_log(WLR_INFO, "ShortcutsInhibit: activated");
        }
        return;
    }

    if (server->active_shortcuts_inhibitor != NULL) {
        struct wlr_keyboard_shortcuts_inhibitor_v1 *old = server->active_shortcuts_inhibitor;
        server->active_shortcuts_inhibitor = NULL;
        if (old->active) {
            wlr_keyboard_shortcuts_inhibitor_v1_deactivate(old);
            wlr_log(WLR_INFO, "ShortcutsInhibit: deactivated");
        }
    }

    if (want != NULL) {
        wlr_keyboard_shortcuts_inhibitor_v1_activate(want);
        server->active_shortcuts_inhibitor = want;
        wlr_log(WLR_INFO, "ShortcutsInhibit: activated");
    }
}

static void shortcuts_inhibitor_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_shortcuts_inhibitor *si = wl_container_of(listener, si, destroy);
    if (si == NULL) {
        return;
    }

    struct fbwl_server *server = si->server;
    if (server != NULL && server->active_shortcuts_inhibitor == si->inhibitor) {
        server->active_shortcuts_inhibitor = NULL;
    }

    fbwl_cleanup_listener(&si->destroy);
    wl_list_remove(&si->link);
    free(si);
}

static void server_new_shortcuts_inhibitor(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_shortcuts_inhibitor);
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor = data;
    if (server == NULL || inhibitor == NULL) {
        return;
    }

    wlr_log(WLR_INFO, "ShortcutsInhibit: new inhibitor");

    struct fbwl_shortcuts_inhibitor *si = calloc(1, sizeof(*si));
    if (si == NULL) {
        return;
    }
    si->server = server;
    si->inhibitor = inhibitor;
    wl_list_insert(&server->shortcuts_inhibitors, &si->link);

    si->destroy.notify = shortcuts_inhibitor_destroy;
    wl_signal_add(&inhibitor->events.destroy, &si->destroy);

    server_update_shortcuts_inhibitor(server);
}

static void focus_view(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;
    if (server != NULL && server->session_locked) {
        return;
    }
    struct wlr_seat *seat = server->seat;

    struct wlr_surface *surface = view_wlr_surface(view);
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    if (prev_surface == surface) {
        return;
    }

    wlr_log(WLR_INFO, "Focus: %s (%s)",
        view_title(view) != NULL ? view_title(view) : "(no-title)",
        view_app_id(view) != NULL ? view_app_id(view) : "(no-app-id)");

    struct fbwl_view *prev_view = server->focused_view;
    if (prev_view != NULL && prev_view != view && prev_view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_activated(prev_view->foreign_toplevel, false);
    }
    if (prev_view != NULL && prev_view != view) {
        view_decor_set_active(prev_view, false);
    }

    if (prev_surface != NULL) {
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }

        struct wlr_xwayland_surface *prev_xsurface =
            wlr_xwayland_surface_try_from_wlr_surface(prev_surface);
        if (prev_xsurface != NULL) {
            wlr_xwayland_surface_activate(prev_xsurface, false);
        }
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&view->scene_tree->node);

    view_set_activated(view, true);
    view_decor_set_active(view, true);
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel, true);
    }
    server->focused_view = view;
    server_toolbar_ui_update_iconbar_focus(server);
    if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
    server_text_input_update_focus(server, surface);
    server_update_shortcuts_inhibitor(server);
}

static void clear_keyboard_focus(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    struct wlr_surface *prev_surface = server->seat->keyboard_state.focused_surface;
    if (prev_surface != NULL) {
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }

        struct wlr_xwayland_surface *prev_xsurface =
            wlr_xwayland_surface_try_from_wlr_surface(prev_surface);
        if (prev_xsurface != NULL) {
            wlr_xwayland_surface_activate(prev_xsurface, false);
        }
    }

    if (server->focused_view != NULL && server->focused_view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_activated(server->focused_view->foreign_toplevel, false);
    }
    if (server->focused_view != NULL) {
        view_decor_set_active(server->focused_view, false);
    }
    server->focused_view = NULL;

    wlr_seat_keyboard_clear_focus(server->seat);
    server_text_input_update_focus(server, NULL);
    server_update_shortcuts_inhibitor(server);
}

static void apply_workspace_visibility(struct fbwl_server *server, const char *why) {
    const int cur = fbwm_core_workspace_current(&server->wm);
    wlr_log(WLR_INFO, "Workspace: apply current=%d reason=%s", cur + 1, why != NULL ? why : "(null)");

    if (server->osd_ui.enabled) {
        if (server->osd_ui.last_workspace != cur) {
            server->osd_ui.last_workspace = cur;
            server_osd_ui_show_workspace(server, cur);
        }
    } else {
        server->osd_ui.last_workspace = cur;
    }

    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || view->scene_tree == NULL) {
            continue;
        }

        const bool visible = fbwm_core_view_is_visible(&server->wm, wm_view);
        wlr_scene_node_set_enabled(&view->scene_tree->node, visible);

        const char *title = NULL;
        if (wm_view->ops != NULL && wm_view->ops->title != NULL) {
            title = wm_view->ops->title(wm_view);
        }
        wlr_log(WLR_INFO, "Workspace: view=%s ws=%d visible=%d",
            title != NULL ? title : "(no-title)", wm_view->workspace + 1, visible ? 1 : 0);
    }

    server_toolbar_ui_rebuild(server);

    if (server->wm.focused == NULL) {
        clear_keyboard_focus(server);
    }
}

static const char *view_display_title(const struct fbwl_view *view) {
    if (view == NULL) {
        return "(no-view)";
    }

    const char *title = view_title(view);
    if (title != NULL && *title != '\0') {
        return title;
    }

    const char *app_id = view_app_id(view);
    if (app_id != NULL && *app_id != '\0') {
        return app_id;
    }

    return "(no-title)";
}

static void view_save_geometry(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }

    const int w = view_current_width(view);
    const int h = view_current_height(view);

    view->saved_x = view->x;
    view->saved_y = view->y;
    view->saved_w = w > 0 ? w : 0;
    view->saved_h = h > 0 ? h : 0;
}

static void view_get_output_box(struct fbwl_view *view, struct wlr_output *preferred,
        struct wlr_box *box) {
    *box = (struct wlr_box){0};
    if (view == NULL || view->server == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;
    struct wlr_output *output = preferred;
    if (output == NULL) {
        const int w = view_current_width(view);
        const int h = view_current_height(view);
        double cx = view->x + (double)w / 2.0;
        double cy = view->y + (double)h / 2.0;
        output = wlr_output_layout_output_at(server->output_layout, cx, cy);
    }
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(server->output_layout);
    }

    wlr_output_layout_get_box(server->output_layout, output, box);
}

static void view_get_output_usable_box(struct fbwl_view *view, struct wlr_output *preferred,
        struct wlr_box *box) {
    *box = (struct wlr_box){0};
    if (view == NULL || view->server == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;
    struct wlr_output *output = preferred;
    if (output == NULL) {
        const int w = view_current_width(view);
        const int h = view_current_height(view);
        double cx = view->x + (double)w / 2.0;
        double cy = view->y + (double)h / 2.0;
        output = wlr_output_layout_output_at(server->output_layout, cx, cy);
    }
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(server->output_layout);
    }
    if (output == NULL) {
        return;
    }

    struct fbwl_output *out = fbwl_output_find(&server->outputs, output);
    if (out != NULL && out->usable_area.width > 0 && out->usable_area.height > 0) {
        *box = out->usable_area;
        return;
    }

    wlr_output_layout_get_box(server->output_layout, output, box);
}

static void view_foreign_set_output(struct fbwl_view *view, struct wlr_output *output) {
    if (view == NULL || view->foreign_toplevel == NULL) {
        return;
    }

    if (output == view->foreign_output) {
        return;
    }

    if (view->foreign_output != NULL) {
        wlr_foreign_toplevel_handle_v1_output_leave(view->foreign_toplevel, view->foreign_output);
    }
    view->foreign_output = output;
    if (output != NULL) {
        wlr_foreign_toplevel_handle_v1_output_enter(view->foreign_toplevel, output);
    }
}

static void view_foreign_update_output_from_position(struct fbwl_view *view) {
    if (view == NULL || view->server == NULL) {
        return;
    }

    struct wlr_output *output = wlr_output_layout_output_at(view->server->output_layout,
        view->x + 1, view->y + 1);
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(view->server->output_layout);
    }
    view_foreign_set_output(view, output);
}

struct fbwl_wm_output {
    struct fbwm_output wm_output;
    struct fbwl_server *server;
    struct wlr_output *wlr_output;
};

static const char *fbwl_wm_output_name(const struct fbwm_output *wm_output) {
    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    if (output == NULL || output->wlr_output == NULL) {
        return NULL;
    }
    return output->wlr_output->name;
}

static bool fbwl_wm_output_full_box(const struct fbwm_output *wm_output, struct fbwm_box *out) {
    if (out != NULL) {
        *out = (struct fbwm_box){0};
    }

    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    if (output == NULL || output->server == NULL || output->server->output_layout == NULL ||
            output->wlr_output == NULL || out == NULL) {
        return false;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &box);
    *out = (struct fbwm_box){
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height,
    };
    return true;
}

static bool fbwl_wm_output_usable_box(const struct fbwm_output *wm_output, struct fbwm_box *out) {
    if (out != NULL) {
        *out = (struct fbwm_box){0};
    }

    const struct fbwl_wm_output *output = wm_output != NULL ? wm_output->userdata : NULL;
    if (output == NULL || output->server == NULL || output->wlr_output == NULL || out == NULL) {
        return false;
    }

    struct fbwl_output *out_data = fbwl_output_find(&output->server->outputs, output->wlr_output);
    if (out_data == NULL || out_data->usable_area.width < 1 || out_data->usable_area.height < 1) {
        return false;
    }

    *out = (struct fbwm_box){
        .x = out_data->usable_area.x,
        .y = out_data->usable_area.y,
        .width = out_data->usable_area.width,
        .height = out_data->usable_area.height,
    };
    return true;
}

static const struct fbwm_output_ops fbwl_wm_output_ops = {
    .name = fbwl_wm_output_name,
    .full_box = fbwl_wm_output_full_box,
    .usable_box = fbwl_wm_output_usable_box,
};

static void view_place_initial(struct fbwl_view *view) {
    if (view == NULL || view->server == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;

    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(server->output_layout);
    }

    struct fbwl_wm_output wm_out = {0};
    const struct fbwm_output *wm_output = NULL;
    if (output != NULL) {
        wm_out.server = server;
        wm_out.wlr_output = output;
        fbwm_output_init(&wm_out.wm_output, &fbwl_wm_output_ops, &wm_out);
        wm_output = &wm_out.wm_output;
    }

    struct fbwm_box full = {0};
    struct fbwm_box box = {0};
    if (wm_output != NULL) {
        (void)fbwm_output_get_full_box(wm_output, &full);
        if (!fbwm_output_get_usable_box(wm_output, &box) || box.width < 1 || box.height < 1) {
            box = full;
        }
    }

    int x = 0;
    int y = 0;
    fbwm_core_place_next(&server->wm, wm_output, &x, &y);

    view->x = x;
    view->y = y;
    wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);

    wlr_log(WLR_INFO, "Place: %s out=%s x=%d y=%d usable=%d,%d %dx%d full=%d,%d %dx%d",
        view_display_title(view),
        fbwm_output_name(wm_output) != NULL ? fbwm_output_name(wm_output) : "(none)",
        view->x, view->y,
        box.x, box.y, box.width, box.height,
        full.x, full.y, full.width, full.height);
    view_foreign_set_output(view, output);
}

static void view_set_maximized(struct fbwl_view *view, bool maximized) {
    if (view == NULL) {
        return;
    }

    if (view->fullscreen) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    if (maximized == view->maximized) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    if (maximized) {
        view_save_geometry(view);

        struct wlr_box box;
        view_get_output_usable_box(view, NULL, &box);
        if (box.width < 1 || box.height < 1) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
                wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
            }
            return;
        }

        view->maximized = true;
        view->x = box.x;
        view->y = box.y;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, true);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_maximized(view->xwayland_surface, true, true);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, true);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, box.width, box.height);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)box.width, (uint16_t)box.height);
        }
        view_foreign_update_output_from_position(view);
        wlr_log(WLR_INFO, "Maximize: %s on w=%d h=%d", view_display_title(view), box.width, box.height);
    } else {
        view->maximized = false;
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, false);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_maximized(view->xwayland_surface, false, false);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, false);
        }
        view->x = view->saved_x;
        view->y = view->saved_y;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel,
                view->saved_w > 0 ? view->saved_w : 0,
                view->saved_h > 0 ? view->saved_h : 0);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)(view->saved_w > 0 ? view->saved_w : 1),
                (uint16_t)(view->saved_h > 0 ? view->saved_h : 1));
        }
        view_foreign_update_output_from_position(view);
        wlr_log(WLR_INFO, "Maximize: %s off", view_display_title(view));
    }
}

static void view_set_minimized(struct fbwl_view *view, bool minimized, const char *why) {
    if (view == NULL || view->server == NULL) {
        return;
    }

    if (minimized == view->minimized) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    view->minimized = minimized;
    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_set_minimized(view->xwayland_surface, minimized);
    }
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, minimized);
    }
    wlr_log(WLR_INFO, "Minimize: %s %s reason=%s",
        view_display_title(view),
        minimized ? "on" : "off",
        why != NULL ? why : "(null)");

    if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
    }

    apply_workspace_visibility(view->server, minimized ? "minimize-on" : "minimize-off");

    if (minimized) {
        fbwm_core_refocus(&view->server->wm);
        if (view->server->wm.focused == NULL) {
            clear_keyboard_focus(view->server);
        }
        return;
    }

    if (fbwm_core_view_is_visible(&view->server->wm, &view->wm_view)) {
        fbwm_core_focus_view(&view->server->wm, &view->wm_view);
        return;
    }

    fbwm_core_refocus(&view->server->wm);
    if (view->server->wm.focused == NULL) {
        clear_keyboard_focus(view->server);
    }
}

static void view_set_fullscreen(struct fbwl_view *view, bool fullscreen, struct wlr_output *output) {
    if (view == NULL) {
        return;
    }

    if (fullscreen == view->fullscreen) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    if (fullscreen) {
        if (view->maximized) {
            view_set_maximized(view, false);
        }

        view_save_geometry(view);

        struct wlr_box box;
        view_get_output_box(view, output, &box);
        if (box.width < 1 || box.height < 1) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
                wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
            }
            return;
        }

        view->fullscreen = true;
        view_decor_apply_enabled(view);
        if (view->server->layer_fullscreen != NULL) {
            wlr_scene_node_reparent(&view->scene_tree->node, view->server->layer_fullscreen);
        }
        view->x = box.x;
        view->y = box.y;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, true);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, true);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, true);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, box.width, box.height);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)box.width, (uint16_t)box.height);
        }
        view_foreign_update_output_from_position(view);
        wlr_log(WLR_INFO, "Fullscreen: %s on w=%d h=%d", view_display_title(view), box.width, box.height);
    } else {
        view->fullscreen = false;
        view_decor_apply_enabled(view);
        if (view->server->layer_normal != NULL) {
            wlr_scene_node_reparent(&view->scene_tree->node, view->server->layer_normal);
        }
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, false);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_set_fullscreen(view->xwayland_surface, false);
        }
        if (view->foreign_toplevel != NULL) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, false);
        }
        view->x = view->saved_x;
        view->y = view->saved_y;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel,
                view->saved_w > 0 ? view->saved_w : 0,
                view->saved_h > 0 ? view->saved_h : 0);
        } else if (view->type == FBWL_VIEW_XWAYLAND) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)(view->saved_w > 0 ? view->saved_w : 1),
                (uint16_t)(view->saved_h > 0 ? view->saved_h : 1));
        }
        view_foreign_update_output_from_position(view);
        wlr_log(WLR_INFO, "Fullscreen: %s off", view_display_title(view));
    }
}

static struct fbwl_view *view_at(struct fbwl_server *server, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    *surface = NULL;
    *sx = 0;
    *sy = 0;

    double node_x = 0, node_y = 0;
    struct wlr_scene_node *node =
        wlr_scene_node_at(&server->scene->tree.node, lx, ly, &node_x, &node_y);
    if (node == NULL) {
        return NULL;
    }

    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *scene_surface =
            wlr_scene_surface_try_from_buffer(scene_buffer);
        if (scene_surface != NULL) {
            *surface = scene_surface->surface;
            *sx = node_x;
            *sy = node_y;
        }
    }

    struct wlr_scene_node *walk = node;
    while (walk != NULL && walk->data == NULL) {
        walk = walk->parent != NULL ? &walk->parent->node : NULL;
    }
    return walk != NULL ? walk->data : NULL;
}

static void server_update_pointer_constraint(struct fbwl_server *server) {
    if (server == NULL || server->pointer_constraints == NULL || server->seat == NULL) {
        return;
    }

    struct wlr_surface *focused_surface = server->seat->pointer_state.focused_surface;
    struct wlr_pointer_constraint_v1 *constraint = NULL;
    if (focused_surface != NULL) {
        constraint = wlr_pointer_constraints_v1_constraint_for_surface(
            server->pointer_constraints, focused_surface, server->seat);
    }

    if (constraint == server->active_pointer_constraint) {
        return;
    }

    if (server->active_pointer_constraint != NULL) {
        struct wlr_pointer_constraint_v1 *old = server->active_pointer_constraint;
        server->active_pointer_constraint = NULL;
        wlr_pointer_constraint_v1_send_deactivated(old);
    }

    if (constraint != NULL) {
        server->active_pointer_constraint = constraint;
        wlr_pointer_constraint_v1_send_activated(constraint);
    }
}

static void server_set_idle_inhibited(struct fbwl_server *server, bool inhibited, const char *why) {
    if (server == NULL) {
        return;
    }
    if (server->idle_inhibited == inhibited) {
        return;
    }
    server->idle_inhibited = inhibited;
    if (server->idle_notifier != NULL) {
        wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, inhibited);
    }
    wlr_log(WLR_INFO, "Idle: inhibited=%d reason=%s", inhibited ? 1 : 0, why != NULL ? why : "(null)");
}

static void server_notify_activity(struct fbwl_server *server) {
    if (server == NULL || server->idle_notifier == NULL || server->seat == NULL) {
        return;
    }
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
}

static void process_cursor_motion(struct fbwl_server *server, uint32_t time_msec) {
    if (server != NULL && server->menu_ui.open) {
        const ssize_t idx =
            server_menu_ui_index_at(&server->menu_ui, (int)server->cursor->x, (int)server->cursor->y);
        if (idx >= 0) {
            server_menu_ui_set_selected(server, (size_t)idx);
        }

        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
        struct wlr_surface *prev_surface = server->seat->pointer_state.focused_surface;
        wlr_seat_pointer_clear_focus(server->seat);
        if (prev_surface != server->seat->pointer_state.focused_surface) {
            server_update_pointer_constraint(server);
        }
        return;
    }

    double sx = 0, sy = 0;
    struct wlr_surface *surface = NULL;
    (void)view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (surface != NULL) {
        struct wlr_surface *prev_surface = server->seat->pointer_state.focused_surface;
        wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
        if (prev_surface != server->seat->pointer_state.focused_surface) {
            server_update_pointer_constraint(server);
        }
        return;
    }

    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    struct wlr_surface *prev_surface = server->seat->pointer_state.focused_surface;
    wlr_seat_pointer_clear_focus(server->seat);
    if (prev_surface != server->seat->pointer_state.focused_surface) {
        server_update_pointer_constraint(server);
    }
}

static uint32_t server_keyboard_modifiers(struct fbwl_server *server) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    return keyboard != NULL ? wlr_keyboard_get_modifiers(keyboard) : 0;
}

static void grab_update(struct fbwl_server *server) {
    struct fbwl_view *view = server->grab.view;
    if (view == NULL) {
        return;
    }

    const double dx = server->cursor->x - server->grab.grab_x;
    const double dy = server->cursor->y - server->grab.grab_y;

    if (server->grab.mode == FBWL_CURSOR_MOVE) {
        view->x = server->grab.view_x + (int)dx;
        view->y = server->grab.view_y + (int)dy;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            const int w = view_current_width(view);
            const int h = view_current_height(view);
            if (w > 0 && h > 0) {
                wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                    (uint16_t)w, (uint16_t)h);
            }
        }
        return;
    }

    if (server->grab.mode == FBWL_CURSOR_RESIZE) {
        uint32_t edges = server->grab.resize_edges;
        if (edges == WLR_EDGE_NONE) {
            edges = WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
        }

        int x = server->grab.view_x;
        int y = server->grab.view_y;
        int w = server->grab.view_w;
        int h = server->grab.view_h;

        if ((edges & WLR_EDGE_LEFT) != 0) {
            x = server->grab.view_x + (int)dx;
            w = server->grab.view_w - (int)dx;
        } else if ((edges & WLR_EDGE_RIGHT) != 0) {
            w = server->grab.view_w + (int)dx;
        }

        if ((edges & WLR_EDGE_TOP) != 0) {
            y = server->grab.view_y + (int)dy;
            h = server->grab.view_h - (int)dy;
        } else if ((edges & WLR_EDGE_BOTTOM) != 0) {
            h = server->grab.view_h + (int)dy;
        }

        if (w < 1) {
            w = 1;
            if ((edges & WLR_EDGE_LEFT) != 0) {
                x = server->grab.view_x + (server->grab.view_w - 1);
            }
        }
        if (h < 1) {
            h = 1;
            if ((edges & WLR_EDGE_TOP) != 0) {
                y = server->grab.view_y + (server->grab.view_h - 1);
            }
        }

        view->x = x;
        view->y = y;
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);

        server->grab.last_w = w;
        server->grab.last_h = h;
        if (view->type == FBWL_VIEW_XDG) {
            wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
        } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
                (uint16_t)w, (uint16_t)h);
        }
        return;
    }
}

static void clamp_to_box(double *x, double *y, const struct wlr_box *box) {
    if (x == NULL || y == NULL || box == NULL) {
        return;
    }

    double min_x = box->x;
    double min_y = box->y;
    double max_x = box->x;
    double max_y = box->y;
    if (box->width > 0) {
        max_x = box->x + box->width - 1;
    }
    if (box->height > 0) {
        max_y = box->y + box->height - 1;
    }

    if (*x < min_x) {
        *x = min_x;
    }
    if (*x > max_x) {
        *x = max_x;
    }
    if (*y < min_y) {
        *y = min_y;
    }
    if (*y > max_y) {
        *y = max_y;
    }
}

static bool server_get_confine_box(struct fbwl_server *server,
        struct wlr_pointer_constraint_v1 *constraint,
        struct wlr_box *box) {
    if (server == NULL || constraint == NULL || box == NULL) {
        return false;
    }
    if (constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED || constraint->surface == NULL) {
        return false;
    }

    struct wlr_surface *surface = NULL;
    double sx = 0, sy = 0;
    (void)view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (surface != constraint->surface) {
        return false;
    }

    const double origin_x = server->cursor->x - sx;
    const double origin_y = server->cursor->y - sy;

    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = constraint->surface->current.width;
    int32_t y2 = constraint->surface->current.height;

    if (pixman_region32_not_empty(&constraint->region)) {
        const pixman_box32_t *ext = pixman_region32_extents(&constraint->region);
        if (ext != NULL) {
            x1 = ext->x1;
            y1 = ext->y1;
            x2 = ext->x2;
            y2 = ext->y2;
        }
    }

    if (x2 <= x1) {
        x2 = x1 + 1;
    }
    if (y2 <= y1) {
        y2 = y1 + 1;
    }

    box->x = (int)(origin_x + x1);
    box->y = (int)(origin_y + y1);
    box->width = x2 - x1;
    box->height = y2 - y1;
    return true;
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    server_notify_activity(server);

    const double dx = event->delta_x;
    const double dy = event->delta_y;
    const double unaccel_dx = event->unaccel_dx;
    const double unaccel_dy = event->unaccel_dy;

    if (!server->pointer_phys_valid) {
        server->pointer_phys_x = server->cursor->x;
        server->pointer_phys_y = server->cursor->y;
        server->pointer_phys_valid = true;
    }
    server->pointer_phys_x += dx;
    server->pointer_phys_y += dy;

    if (server->relative_pointer_mgr != NULL) {
        wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointer_mgr,
            server->seat, (uint64_t)event->time_msec * 1000, dx, dy, unaccel_dx, unaccel_dy);
    }

    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
        grab_update(server);
        return;
    }

    if (server->active_pointer_constraint != NULL &&
            server->active_pointer_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        process_cursor_motion(server, event->time_msec);
        return;
    }

    if (server->active_pointer_constraint != NULL &&
            server->active_pointer_constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
        double x = server->cursor->x + dx;
        double y = server->cursor->y + dy;
        struct wlr_box box = {0};
        if (server_get_confine_box(server, server->active_pointer_constraint, &box)) {
            clamp_to_box(&x, &y, &box);
            if (!wlr_cursor_warp(server->cursor, &event->pointer->base, x, y)) {
                wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
            }
        } else {
            wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
        }
    } else {
        wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
    }

    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    server_notify_activity(server);

    double lx = 0, ly = 0;
    wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base,
        event->x, event->y, &lx, &ly);

    double dx = 0, dy = 0;
    if (server->pointer_phys_valid) {
        dx = lx - server->pointer_phys_x;
        dy = ly - server->pointer_phys_y;
    }
    server->pointer_phys_x = lx;
    server->pointer_phys_y = ly;
    server->pointer_phys_valid = true;

    if (server->relative_pointer_mgr != NULL) {
        wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointer_mgr,
            server->seat, (uint64_t)event->time_msec * 1000, dx, dy, dx, dy);
    }

    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
        grab_update(server);
        return;
    }

    if (server->active_pointer_constraint != NULL &&
            server->active_pointer_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        process_cursor_motion(server, event->time_msec);
        return;
    }

    if (server->active_pointer_constraint != NULL &&
            server->active_pointer_constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
        struct wlr_box box = {0};
        if (server_get_confine_box(server, server->active_pointer_constraint, &box)) {
            double x = lx;
            double y = ly;
            clamp_to_box(&x, &y, &box);
            wlr_cursor_warp_closest(server->cursor, &event->pointer->base, x, y);
        } else {
            wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
        }
    } else {
        wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    }

    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    server_notify_activity(server);

    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
                event->button == server->grab.button) {
            struct fbwl_view *view = server->grab.view;
            if (view != NULL && server->grab.mode == FBWL_CURSOR_MOVE) {
                wlr_log(WLR_INFO, "Move: %s x=%d y=%d",
                    view_display_title(view),
                    view->x, view->y);
            }
            if (view != NULL && server->grab.mode == FBWL_CURSOR_RESIZE) {
                wlr_log(WLR_INFO, "Resize: %s w=%d h=%d",
                    view_display_title(view),
                    server->grab.last_w, server->grab.last_h);
                if (view->type == FBWL_VIEW_XDG) {
                    wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, false);
                }
            }
            server->grab.mode = FBWL_CURSOR_PASSTHROUGH;
            server->grab.view = NULL;
            server->grab.button = 0;
            server->grab.resize_edges = WLR_EDGE_NONE;
        }
        return;
    }

    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (server->cmd_dialog_ui.open) {
            server_cmd_dialog_ui_close(server, "pointer");
            return;
        }

        if (server->menu_ui.open) {
            if (server_menu_ui_handle_click(server,
                    (int)server->cursor->x, (int)server->cursor->y, event->button)) {
                return;
            }
        }

        if (server_toolbar_ui_handle_click(server,
                (int)server->cursor->x, (int)server->cursor->y, event->button)) {
            return;
        }

        double sx = 0, sy = 0;
        struct wlr_surface *surface = NULL;
        struct fbwl_view *view = view_at(server, server->cursor->x, server->cursor->y,
            &surface, &sx, &sy);
        wlr_log(WLR_INFO, "Pointer press at %.1f,%.1f hit=%s",
            server->cursor->x, server->cursor->y,
            view != NULL ? view_display_title(view) : "(none)");

        const uint32_t modifiers = server_keyboard_modifiers(server);
        const bool alt = (modifiers & WLR_MODIFIER_ALT) != 0;

        if (view == NULL && surface == NULL && event->button == BTN_RIGHT) {
            server_menu_ui_open_root(server, (int)server->cursor->x, (int)server->cursor->y);
            return;
        }

        if (alt && view != NULL && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
            fbwm_core_focus_view(&server->wm, &view->wm_view);

            server->grab.view = view;
            server->grab.button = event->button;
            server->grab.resize_edges = (event->button == BTN_RIGHT) ? (WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM) : WLR_EDGE_NONE;
            server->grab.grab_x = server->cursor->x;
            server->grab.grab_y = server->cursor->y;
            server->grab.view_x = view->x;
            server->grab.view_y = view->y;
            server->grab.view_w =
                view_current_width(view);
            server->grab.view_h =
                view_current_height(view);
            server->grab.last_w = server->grab.view_w;
            server->grab.last_h = server->grab.view_h;

            if (event->button == BTN_LEFT) {
                server->grab.mode = FBWL_CURSOR_MOVE;
            } else {
                server->grab.mode = FBWL_CURSOR_RESIZE;
                if (view->type == FBWL_VIEW_XDG) {
                    wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, true);
                }
            }
            return;
        }

        if (view != NULL && surface == NULL && event->button == BTN_RIGHT) {
            const struct fbwl_decor_hit hit =
                view_decor_hit_test(view, server->cursor->x, server->cursor->y);
            if (hit.kind == FBWL_DECOR_HIT_TITLEBAR) {
                server_menu_ui_open_window(server, view, (int)server->cursor->x, (int)server->cursor->y);
                return;
            }
        }

        if (view != NULL && surface == NULL && event->button == BTN_LEFT) {
            const struct fbwl_decor_hit hit =
                view_decor_hit_test(view, server->cursor->x, server->cursor->y);
            if (hit.kind != FBWL_DECOR_HIT_NONE) {
                fbwm_core_focus_view(&server->wm, &view->wm_view);

                if (hit.kind == FBWL_DECOR_HIT_TITLEBAR) {
                    server->grab.view = view;
                    server->grab.button = event->button;
                    server->grab.resize_edges = WLR_EDGE_NONE;
                    server->grab.grab_x = server->cursor->x;
                    server->grab.grab_y = server->cursor->y;
                    server->grab.view_x = view->x;
                    server->grab.view_y = view->y;
                    server->grab.view_w = view_current_width(view);
                    server->grab.view_h = view_current_height(view);
                    server->grab.last_w = server->grab.view_w;
                    server->grab.last_h = server->grab.view_h;
                    server->grab.mode = FBWL_CURSOR_MOVE;
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_RESIZE) {
                    server->grab.view = view;
                    server->grab.button = event->button;
                    server->grab.resize_edges = hit.edges;
                    server->grab.grab_x = server->cursor->x;
                    server->grab.grab_y = server->cursor->y;
                    server->grab.view_x = view->x;
                    server->grab.view_y = view->y;
                    server->grab.view_w = view_current_width(view);
                    server->grab.view_h = view_current_height(view);
                    server->grab.last_w = server->grab.view_w;
                    server->grab.last_h = server->grab.view_h;
                    server->grab.mode = FBWL_CURSOR_RESIZE;
                    if (view->type == FBWL_VIEW_XDG) {
                        wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, true);
                    }
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_CLOSE) {
                    if (view->type == FBWL_VIEW_XDG) {
                        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
                    } else if (view->type == FBWL_VIEW_XWAYLAND) {
                        wlr_xwayland_surface_close(view->xwayland_surface);
                    }
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_MAX) {
                    view_set_maximized(view, !view->maximized);
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_MIN) {
                    view_set_minimized(view, !view->minimized, "decor-button");
                    return;
                }
            }
        }

        if (view != NULL) {
            fbwm_core_focus_view(&server->wm, &view->wm_view);
        }
    }

    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
        event->button, event->state);
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    server_notify_activity(server);
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    server_notify_activity(keyboard->server);
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
        &keyboard->wlr_keyboard->modifiers);
}

static void spawn(const char *cmd) {
    if (cmd == NULL || *cmd == '\0') {
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
        _exit(127);
    }
}

#define FBWL_KEYMOD_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)

static void server_keybindings_free(struct fbwl_server *server) {
    if (server == NULL || server->keybindings == NULL) {
        return;
    }
    for (size_t i = 0; i < server->keybinding_count; i++) {
        free(server->keybindings[i].cmd);
        server->keybindings[i].cmd = NULL;
    }
    free(server->keybindings);
    server->keybindings = NULL;
    server->keybinding_count = 0;
}

static bool server_keybindings_add(struct fbwl_server *server, xkb_keysym_t sym,
        uint32_t modifiers, enum fbwl_keybinding_action action, int arg, const char *cmd) {
    if (server == NULL) {
        return false;
    }

    struct fbwl_keybinding *bindings =
        realloc(server->keybindings, (server->keybinding_count + 1) * sizeof(*bindings));
    if (bindings == NULL) {
        return false;
    }
    server->keybindings = bindings;

    struct fbwl_keybinding *binding = &server->keybindings[server->keybinding_count++];
    binding->sym = xkb_keysym_to_lower(sym);
    binding->modifiers = modifiers & FBWL_KEYMOD_MASK;
    binding->action = action;
    binding->arg = arg;
    binding->cmd = cmd != NULL ? strdup(cmd) : NULL;
    return true;
}

static bool server_keybindings_add_from_keys_file(void *userdata, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd) {
    struct fbwl_server *server = userdata;
    return server_keybindings_add(server, sym, modifiers, action, arg, cmd);
}

static bool server_keybindings_execute(struct fbwl_server *server, const struct fbwl_keybinding *binding) {
    if (server == NULL || binding == NULL) {
        return false;
    }

    switch (binding->action) {
    case FBWL_KEYBIND_EXIT:
        wl_display_terminate(server->wl_display);
        return true;
    case FBWL_KEYBIND_EXEC:
        spawn(binding->cmd);
        return true;
    case FBWL_KEYBIND_COMMAND_DIALOG:
        server_cmd_dialog_ui_open(server);
        return true;
    case FBWL_KEYBIND_FOCUS_NEXT:
        fbwm_core_focus_next(&server->wm);
        return true;
    case FBWL_KEYBIND_TOGGLE_MAXIMIZE: {
        struct fbwm_view *wm_view = server->wm.focused;
        if (wm_view != NULL) {
            struct fbwl_view *view = wm_view->userdata;
            if (view != NULL) {
                view_set_maximized(view, !view->maximized);
            }
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_FULLSCREEN: {
        struct fbwm_view *wm_view = server->wm.focused;
        if (wm_view != NULL) {
            struct fbwl_view *view = wm_view->userdata;
            if (view != NULL) {
                view_set_fullscreen(view, !view->fullscreen, NULL);
            }
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_MINIMIZE: {
        struct fbwl_view *view = NULL;
        struct fbwm_view *wm_view = server->wm.focused;
        if (wm_view != NULL) {
            view = wm_view->userdata;
        }

        if (view == NULL) {
            const int cur = fbwm_core_workspace_current(&server->wm);
            for (struct fbwm_view *walk = server->wm.views.next;
                    walk != &server->wm.views;
                    walk = walk->next) {
                struct fbwl_view *candidate = walk->userdata;
                if (candidate != NULL && candidate->mapped && candidate->minimized &&
                        (walk->sticky || walk->workspace == cur)) {
                    view = candidate;
                    break;
                }
            }
        }

        if (view != NULL) {
            view_set_minimized(view, !view->minimized, "keybinding");
        }
        return true;
    }
    case FBWL_KEYBIND_WORKSPACE_SWITCH:
        fbwm_core_workspace_switch(&server->wm, binding->arg);
        apply_workspace_visibility(server, "switch");
        return true;
    case FBWL_KEYBIND_SEND_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(&server->wm, binding->arg);
        apply_workspace_visibility(server, "move-focused");
        return true;
    case FBWL_KEYBIND_TAKE_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(&server->wm, binding->arg);
        apply_workspace_visibility(server, "move-focused");
        fbwm_core_workspace_switch(&server->wm, binding->arg);
        apply_workspace_visibility(server, "switch");
        return true;
    default:
        return false;
    }
}

static bool server_handle_keybinding(struct fbwl_server *server, xkb_keysym_t sym, uint32_t modifiers) {
    if (server == NULL || server->keybindings == NULL || server->keybinding_count == 0) {
        return false;
    }

    sym = xkb_keysym_to_lower(sym);
    modifiers &= FBWL_KEYMOD_MASK;
    for (ssize_t i = (ssize_t)server->keybinding_count - 1; i >= 0; i--) {
        const struct fbwl_keybinding *binding = &server->keybindings[i];
        if (binding->sym == sym && binding->modifiers == modifiers) {
            return server_keybindings_execute(server, binding);
        }
    }
    return false;
}

static char *trim_inplace(char *s) {
    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static void init_settings_free(struct fbwl_init_settings *settings) {
    if (settings == NULL) {
        return;
    }
    free(settings->keys_file);
    settings->keys_file = NULL;
    free(settings->apps_file);
    settings->apps_file = NULL;
    free(settings->style_file);
    settings->style_file = NULL;
    free(settings->menu_file);
    settings->menu_file = NULL;
    settings->set_workspaces = false;
    settings->workspaces = 0;
}

static char *path_join(const char *dir, const char *rel) {
    if (dir == NULL || *dir == '\0' || rel == NULL || *rel == '\0') {
        return NULL;
    }

    size_t dir_len = strlen(dir);
    size_t rel_len = strlen(rel);
    const bool need_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    size_t needed = dir_len + (need_slash ? 1 : 0) + rel_len + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }

    int n = snprintf(out, needed, need_slash ? "%s/%s" : "%s%s", dir, rel);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

static bool file_exists(const char *path) {
    return path != NULL && *path != '\0' && access(path, R_OK) == 0;
}

static char *expand_tilde(const char *path) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }
    if (path[0] != '~') {
        return strdup(path);
    }

    const char *home = getenv("HOME");
    if (home == NULL || *home == '\0') {
        return strdup(path);
    }

    const char *tail = path + 1;
    if (*tail == '\0') {
        return strdup(home);
    }
    if (*tail != '/') {
        return strdup(path);
    }

    size_t home_len = strlen(home);
    size_t tail_len = strlen(tail);
    size_t needed = home_len + tail_len + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }
    int n = snprintf(out, needed, "%s%s", home, tail);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

static char *resolve_config_path(const char *config_dir, const char *value) {
    if (value == NULL) {
        return NULL;
    }

    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }
    if (*value == '\0') {
        return NULL;
    }

    char *tmp = strdup(value);
    if (tmp == NULL) {
        return NULL;
    }
    char *s = trim_inplace(tmp);
    if (s == NULL || *s == '\0') {
        free(tmp);
        return NULL;
    }

    const size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '\'' && s[len - 1] == '\'') || (s[0] == '"' && s[len - 1] == '"'))) {
        s[len - 1] = '\0';
        s++;
    }

    char *expanded = expand_tilde(s);
    free(tmp);
    if (expanded == NULL) {
        return NULL;
    }

    if (expanded[0] == '/' || config_dir == NULL || *config_dir == '\0') {
        return expanded;
    }

    char *joined = path_join(config_dir, expanded);
    free(expanded);
    return joined;
}

static bool init_load_file(const char *config_dir, struct fbwl_init_settings *settings) {
    if (settings == NULL) {
        return false;
    }

    init_settings_free(settings);

    char *path = path_join(config_dir, "init");
    if (path == NULL) {
        return false;
    }
    if (!file_exists(path)) {
        free(path);
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Init: failed to open %s: %s", path, strerror(errno));
        free(path);
        return false;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    while ((nread = getline(&line, &cap, f)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        char *s = trim_inplace(line);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (*s == '#' || *s == '!') {
            continue;
        }

        char *sep = strchr(s, ':');
        if (sep == NULL) {
            continue;
        }
        *sep = '\0';
        char *key = trim_inplace(s);
        char *val = trim_inplace(sep + 1);
        if (key == NULL || *key == '\0' || val == NULL || *val == '\0') {
            continue;
        }

        if (strcasecmp(key, "session.screen0.workspaces") == 0) {
            char *end = NULL;
            long ws = strtol(val, &end, 10);
            if (end != val && (end == NULL || *end == '\0') && ws > 0 && ws < 1000) {
                settings->workspaces = (int)ws;
                settings->set_workspaces = true;
            }
            continue;
        }

        if (strcasecmp(key, "session.keyFile") == 0) {
            char *resolved = resolve_config_path(config_dir, val);
            if (resolved != NULL) {
                free(settings->keys_file);
                settings->keys_file = resolved;
            }
            continue;
        }

        if (strcasecmp(key, "session.appsFile") == 0) {
            char *resolved = resolve_config_path(config_dir, val);
            if (resolved != NULL) {
                free(settings->apps_file);
                settings->apps_file = resolved;
            }
            continue;
        }

        if (strcasecmp(key, "session.styleFile") == 0) {
            char *resolved = resolve_config_path(config_dir, val);
            if (resolved != NULL) {
                free(settings->style_file);
                settings->style_file = resolved;
            }
            continue;
        }

        if (strcasecmp(key, "session.menuFile") == 0) {
            char *resolved = resolve_config_path(config_dir, val);
            if (resolved != NULL) {
                free(settings->menu_file);
                settings->menu_file = resolved;
            }
            continue;
        }
    }

    free(line);
    fclose(f);

    wlr_log(WLR_INFO, "Init: loaded %s", path);
    free(path);
    return true;
}

static void decor_theme_set_defaults(struct fbwl_decor_theme *theme) {
    if (theme == NULL) {
        return;
    }

    theme->border_width = 4;
    theme->title_height = 24;
    theme->button_margin = 4;
    theme->button_spacing = 2;

    theme->titlebar_active[0] = 0.20f;
    theme->titlebar_active[1] = 0.20f;
    theme->titlebar_active[2] = 0.20f;
    theme->titlebar_active[3] = 1.0f;

    theme->titlebar_inactive[0] = 0.10f;
    theme->titlebar_inactive[1] = 0.10f;
    theme->titlebar_inactive[2] = 0.10f;
    theme->titlebar_inactive[3] = 1.0f;

    theme->border_color[0] = 0.05f;
    theme->border_color[1] = 0.05f;
    theme->border_color[2] = 0.05f;
    theme->border_color[3] = 1.0f;

    theme->btn_close_color[0] = 0.80f;
    theme->btn_close_color[1] = 0.15f;
    theme->btn_close_color[2] = 0.15f;
    theme->btn_close_color[3] = 1.0f;

    theme->btn_max_color[0] = 0.15f;
    theme->btn_max_color[1] = 0.65f;
    theme->btn_max_color[2] = 0.15f;
    theme->btn_max_color[3] = 1.0f;

    theme->btn_min_color[0] = 0.80f;
    theme->btn_min_color[1] = 0.65f;
    theme->btn_min_color[2] = 0.15f;
    theme->btn_min_color[3] = 1.0f;
}


static void server_menu_create_default(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_menu_free(server->root_menu);
    server->root_menu = fbwl_menu_create("Fluxbox");
    if (server->root_menu == NULL) {
        return;
    }

    (void)fbwl_menu_add_exec(server->root_menu, "Terminal", server->terminal_cmd);
    (void)fbwl_menu_add_exit(server->root_menu, "Exit");
}

static void server_menu_create_window(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_menu_free(server->window_menu);
    server->window_menu = fbwl_menu_create("Window");
    if (server->window_menu == NULL) {
        return;
    }

    (void)fbwl_menu_add_view_action(server->window_menu, "Close", FBWL_MENU_VIEW_CLOSE);
    (void)fbwl_menu_add_view_action(server->window_menu, "Minimize", FBWL_MENU_VIEW_TOGGLE_MINIMIZE);
    (void)fbwl_menu_add_view_action(server->window_menu, "Maximize", FBWL_MENU_VIEW_TOGGLE_MAXIMIZE);
    (void)fbwl_menu_add_view_action(server->window_menu, "Fullscreen", FBWL_MENU_VIEW_TOGGLE_FULLSCREEN);
}

static bool server_menu_load_file(struct fbwl_server *server, const char *path) {
    if (server == NULL || path == NULL || *path == '\0') {
        return false;
    }

    fbwl_menu_free(server->root_menu);
    server->root_menu = fbwl_menu_create(NULL);
    if (server->root_menu == NULL) {
        return false;
    }

    if (!fbwl_menu_parse_file(server->root_menu, path)) {
        fbwl_menu_free(server->root_menu);
        server->root_menu = NULL;
        return false;
    }

    wlr_log(WLR_INFO, "Menu: loaded %zu items from %s", server->root_menu->item_count, path);
    return true;
}

static void server_toolbar_ui_destroy_scene(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->clock_timer != NULL) {
        wl_event_source_remove(ui->clock_timer);
        ui->clock_timer = NULL;
    }
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->highlight = NULL;
    free(ui->cells);
    ui->cells = NULL;
    free(ui->labels);
    ui->labels = NULL;
    ui->cell_count = 0;

    free(ui->iconbar_views);
    ui->iconbar_views = NULL;
    free(ui->iconbar_item_lx);
    ui->iconbar_item_lx = NULL;
    free(ui->iconbar_item_w);
    ui->iconbar_item_w = NULL;
    free(ui->iconbar_items);
    ui->iconbar_items = NULL;
    free(ui->iconbar_labels);
    ui->iconbar_labels = NULL;
    ui->iconbar_count = 0;

    if (ui->tray_ids != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_ids[i]);
        }
    }
    free(ui->tray_ids);
    ui->tray_ids = NULL;

    if (ui->tray_services != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_services[i]);
        }
    }
    free(ui->tray_services);
    ui->tray_services = NULL;

    if (ui->tray_paths != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            free(ui->tray_paths[i]);
        }
    }
    free(ui->tray_paths);
    ui->tray_paths = NULL;

    free(ui->tray_item_lx);
    ui->tray_item_lx = NULL;
    free(ui->tray_item_w);
    ui->tray_item_w = NULL;
    free(ui->tray_rects);
    ui->tray_rects = NULL;
    free(ui->tray_icons);
    ui->tray_icons = NULL;
    ui->tray_count = 0;
    ui->tray_x = 0;
    ui->tray_w = 0;
    ui->tray_icon_w = 0;

    ui->clock_label = NULL;
    ui->clock_text[0] = '\0';
}

static void server_toolbar_ui_update_current(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    struct fbwl_toolbar_ui *ui = &server->toolbar_ui;
    if (!ui->enabled || ui->highlight == NULL || ui->cell_w < 1 || ui->cell_count < 1) {
        return;
    }

    int cur = fbwm_core_workspace_current(&server->wm);
    if (cur < 0) {
        cur = 0;
    }
    if ((size_t)cur >= ui->cell_count) {
        cur = (int)ui->cell_count - 1;
    }
    wlr_scene_node_set_position(&ui->highlight->node, cur * ui->cell_w, 0);
}

static int server_toolbar_clock_timer(void *data) {
    struct fbwl_server *server = data;
    if (server == NULL) {
        return 0;
    }

    server_toolbar_ui_clock_render(server);

    struct fbwl_toolbar_ui *ui = &server->toolbar_ui;
    if (ui->clock_timer != NULL) {
        wl_event_source_timer_update(ui->clock_timer, 1000);
    }
    return 0;
}

static void server_toolbar_ui_clock_render(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    struct fbwl_toolbar_ui *ui = &server->toolbar_ui;
    if (!ui->enabled || ui->tree == NULL || ui->clock_label == NULL || ui->clock_w < 1 || ui->height < 1) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL) {
        return;
    }

    char text[sizeof(ui->clock_text)];
    if (strftime(text, sizeof(text), "%H:%M", &tm) == 0) {
        return;
    }

    if (strcmp(text, ui->clock_text) == 0) {
        return;
    }

    strncpy(ui->clock_text, text, sizeof(ui->clock_text));
    ui->clock_text[sizeof(ui->clock_text) - 1] = '\0';

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct wlr_buffer *buf = fbwl_text_buffer_create(ui->clock_text, ui->clock_w, ui->height, 8, fg);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(ui->clock_label, NULL);
        return;
    }

    wlr_scene_buffer_set_buffer(ui->clock_label, buf);
    wlr_buffer_drop(buf);
    wlr_log(WLR_INFO, "Toolbar: clock text=%s", ui->clock_text);
}

static void server_toolbar_ui_update_iconbar_focus(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    struct fbwl_toolbar_ui *ui = &server->toolbar_ui;
    if (!ui->enabled || ui->tree == NULL || ui->iconbar_count < 1 || ui->iconbar_items == NULL ||
            ui->iconbar_views == NULL) {
        return;
    }

    float active[4] = {server->decor_theme.titlebar_active[0], server->decor_theme.titlebar_active[1],
        server->decor_theme.titlebar_active[2], 0.65f};
    float inactive[4] = {0.00f, 0.00f, 0.00f, 0.01f};

    for (size_t i = 0; i < ui->iconbar_count; i++) {
        if (ui->iconbar_items[i] == NULL) {
            continue;
        }
        const bool focused = ui->iconbar_views[i] != NULL && ui->iconbar_views[i] == server->focused_view;
        wlr_scene_rect_set_color(ui->iconbar_items[i], focused ? active : inactive);
    }
}

static void server_toolbar_ui_rebuild(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL) {
        return;
    }

    struct fbwl_toolbar_ui *ui = &server->toolbar_ui;
    ui->enabled = true;

    server_toolbar_ui_destroy_scene(ui);

    if (!ui->enabled) {
        return;
    }

    struct wlr_scene_tree *parent =
        server->layer_top != NULL ? server->layer_top : &server->scene->tree;
    ui->tree = wlr_scene_tree_create(parent);
    if (ui->tree == NULL) {
        return;
    }

    ui->x = 0;
    ui->y = 0;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);

    int workspaces = fbwm_core_workspace_count(&server->wm);
    if (workspaces < 1) {
        workspaces = 1;
    }

    const int h = server->decor_theme.title_height > 0 ? server->decor_theme.title_height : 24;
    int cell_w = h * 2;
    if (cell_w < 32) {
        cell_w = 32;
    }
    if (cell_w > 256) {
        cell_w = 256;
    }

    ui->height = h;
    ui->cell_w = cell_w;
    ui->cell_count = (size_t)workspaces;
    ui->ws_width = (int)ui->cell_count * ui->cell_w;

    int output_w = 0;
    if (server->output_layout != NULL) {
        struct wlr_output *out = wlr_output_layout_get_center_output(server->output_layout);
        if (out != NULL) {
            struct wlr_box box = {0};
            wlr_output_layout_get_box(server->output_layout, out, &box);
            output_w = box.width;
        }
    }

    ui->width = output_w > ui->ws_width ? output_w : ui->ws_width;
    ui->clock_w = 120;
    const int avail_right = ui->width - ui->ws_width;
    if (avail_right < 1) {
        ui->clock_w = 0;
    } else if (ui->clock_w > avail_right) {
        ui->clock_w = avail_right;
    }
    if (ui->clock_w < 0) {
        ui->clock_w = 0;
    }
    ui->clock_x = ui->width - ui->clock_w;

    size_t tray_count = 0;
#ifdef HAVE_SYSTEMD
    {
        if (server->sni.items.prev != NULL && server->sni.items.next != NULL) {
            struct fbwl_sni_item *item;
            wl_list_for_each(item, &server->sni.items, link) {
                if (item->status != FBWL_SNI_STATUS_PASSIVE) {
                    tray_count++;
                }
            }
        }
    }
#endif

    ui->tray_x = ui->clock_x;
    ui->tray_w = 0;
    ui->tray_icon_w = 0;
    ui->tray_count = 0;

    int avail_mid = ui->width - ui->ws_width - ui->clock_w;
    if (avail_mid < 0) {
        avail_mid = 0;
    }

    int tray_icon_w = ui->height;
    if (tray_icon_w < 1) {
        tray_icon_w = 1;
    }

    if (tray_count > 0 && avail_mid > 0) {
        size_t max_fit = (size_t)(avail_mid / tray_icon_w);
        if (tray_count > max_fit) {
            tray_count = max_fit;
        }
        if (tray_count > 0) {
            ui->tray_icon_w = tray_icon_w;
            ui->tray_w = (int)tray_count * ui->tray_icon_w;
            ui->tray_x = ui->clock_x - ui->tray_w;
            ui->tray_count = tray_count;
        }
    }

    ui->iconbar_x = ui->ws_width;
    ui->iconbar_w = ui->tray_x - ui->iconbar_x;
    if (ui->iconbar_w < 0) {
        ui->iconbar_w = 0;
    }

    float bg[4] = {server->decor_theme.titlebar_inactive[0], server->decor_theme.titlebar_inactive[1],
        server->decor_theme.titlebar_inactive[2], 0.85f};
    float hi[4] = {server->decor_theme.titlebar_active[0], server->decor_theme.titlebar_active[1],
        server->decor_theme.titlebar_active[2], 0.85f};
    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    ui->bg = wlr_scene_rect_create(ui->tree, ui->width, ui->height, bg);
    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, 0, 0);
    }

    ui->highlight = wlr_scene_rect_create(ui->tree, ui->cell_w, ui->height, hi);
    if (ui->highlight != NULL) {
        server_toolbar_ui_update_current(server);
    }

    if (ui->cell_count > 0) {
        ui->cells = calloc(ui->cell_count, sizeof(*ui->cells));
        ui->labels = calloc(ui->cell_count, sizeof(*ui->labels));

        if (ui->cells != NULL) {
            float item[4] = {0.00f, 0.00f, 0.00f, 0.01f};
            for (size_t i = 0; i < ui->cell_count; i++) {
                ui->cells[i] = wlr_scene_rect_create(ui->tree, ui->cell_w, ui->height, item);
                if (ui->cells[i] != NULL) {
                    wlr_scene_node_set_position(&ui->cells[i]->node, (int)i * ui->cell_w, 0);
                }
            }
        }

        for (size_t i = 0; i < ui->cell_count; i++) {
            char label[16];
            if (snprintf(label, sizeof(label), "%zu", i + 1) < 0) {
                continue;
            }
            struct wlr_buffer *buf = fbwl_text_buffer_create(label, ui->cell_w, ui->height, 8, fg);
            if (buf != NULL) {
                struct wlr_scene_buffer *sb = wlr_scene_buffer_create(ui->tree, buf);
                if (sb != NULL) {
                    wlr_scene_node_set_position(&sb->node, (int)i * ui->cell_w, 0);
                    if (ui->labels != NULL) {
                        ui->labels[i] = sb;
                    }
                }
                wlr_buffer_drop(buf);
            }
        }
    }

    const int cur_ws = fbwm_core_workspace_current(&server->wm);
    size_t icon_count = 0;
    for (struct fbwm_view *wm_view = server->wm.views.next; wm_view != &server->wm.views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped) {
            continue;
        }
        if (!wm_view->sticky && wm_view->workspace != cur_ws) {
            continue;
        }
        icon_count++;
    }

    if (ui->iconbar_w > 0 && icon_count > 0) {
        ui->iconbar_views = calloc(icon_count, sizeof(*ui->iconbar_views));
        ui->iconbar_item_lx = calloc(icon_count, sizeof(*ui->iconbar_item_lx));
        ui->iconbar_item_w = calloc(icon_count, sizeof(*ui->iconbar_item_w));
        ui->iconbar_items = calloc(icon_count, sizeof(*ui->iconbar_items));
        ui->iconbar_labels = calloc(icon_count, sizeof(*ui->iconbar_labels));

        if (ui->iconbar_views == NULL || ui->iconbar_item_lx == NULL || ui->iconbar_item_w == NULL ||
                ui->iconbar_items == NULL || ui->iconbar_labels == NULL) {
            free(ui->iconbar_views);
            ui->iconbar_views = NULL;
            free(ui->iconbar_item_lx);
            ui->iconbar_item_lx = NULL;
            free(ui->iconbar_item_w);
            ui->iconbar_item_w = NULL;
            free(ui->iconbar_items);
            ui->iconbar_items = NULL;
            free(ui->iconbar_labels);
            ui->iconbar_labels = NULL;
            ui->iconbar_count = 0;
        } else {
            ui->iconbar_count = icon_count;
            int xoff = ui->iconbar_x;
            const int base_w = ui->iconbar_w / (int)icon_count;
            const int rem = ui->iconbar_w % (int)icon_count;

            size_t idx = 0;
            for (struct fbwm_view *wm_view = server->wm.views.next;
                    wm_view != &server->wm.views;
                    wm_view = wm_view->next) {
                struct fbwl_view *view = wm_view->userdata;
                if (view == NULL || !view->mapped) {
                    continue;
                }
                if (!wm_view->sticky && wm_view->workspace != cur_ws) {
                    continue;
                }
                if (idx >= icon_count) {
                    break;
                }

                int iw = base_w + ((int)idx < rem ? 1 : 0);
                if (iw < 1) {
                    iw = 1;
                }

                ui->iconbar_views[idx] = view;
                ui->iconbar_item_lx[idx] = xoff;
                ui->iconbar_item_w[idx] = iw;

                float item[4] = {0.00f, 0.00f, 0.00f, 0.01f};
                ui->iconbar_items[idx] = wlr_scene_rect_create(ui->tree, iw, ui->height, item);
                if (ui->iconbar_items[idx] != NULL) {
                    wlr_scene_node_set_position(&ui->iconbar_items[idx]->node, xoff, 0);
                }

                struct wlr_buffer *buf = fbwl_text_buffer_create(view_display_title(view), iw, ui->height, 8, fg);
                if (buf != NULL) {
                    struct wlr_scene_buffer *sb = wlr_scene_buffer_create(ui->tree, buf);
                    if (sb != NULL) {
                        wlr_scene_node_set_position(&sb->node, xoff, 0);
                        ui->iconbar_labels[idx] = sb;
                    }
                    wlr_buffer_drop(buf);
                }

                wlr_log(WLR_INFO, "Toolbar: iconbar item idx=%zu lx=%d w=%d title=%s minimized=%d",
                    idx, xoff, iw, view_display_title(view), view->minimized ? 1 : 0);

                xoff += iw;
                idx++;
            }
            ui->iconbar_count = idx;
        }
    }

    if (ui->tray_count > 0 && ui->tray_w > 0 && ui->tray_icon_w > 0) {
        ui->tray_ids = calloc(ui->tray_count, sizeof(*ui->tray_ids));
        ui->tray_services = calloc(ui->tray_count, sizeof(*ui->tray_services));
        ui->tray_paths = calloc(ui->tray_count, sizeof(*ui->tray_paths));
        ui->tray_item_lx = calloc(ui->tray_count, sizeof(*ui->tray_item_lx));
        ui->tray_item_w = calloc(ui->tray_count, sizeof(*ui->tray_item_w));
        ui->tray_rects = calloc(ui->tray_count, sizeof(*ui->tray_rects));
        ui->tray_icons = calloc(ui->tray_count, sizeof(*ui->tray_icons));

        if (ui->tray_ids == NULL || ui->tray_services == NULL || ui->tray_paths == NULL ||
                ui->tray_item_lx == NULL || ui->tray_item_w == NULL || ui->tray_rects == NULL ||
                ui->tray_icons == NULL) {
            if (ui->tray_ids != NULL) {
                for (size_t i = 0; i < ui->tray_count; i++) {
                    free(ui->tray_ids[i]);
                }
            }
            free(ui->tray_ids);
            ui->tray_ids = NULL;

            if (ui->tray_services != NULL) {
                for (size_t i = 0; i < ui->tray_count; i++) {
                    free(ui->tray_services[i]);
                }
            }
            free(ui->tray_services);
            ui->tray_services = NULL;

            if (ui->tray_paths != NULL) {
                for (size_t i = 0; i < ui->tray_count; i++) {
                    free(ui->tray_paths[i]);
                }
            }
            free(ui->tray_paths);
            ui->tray_paths = NULL;

            free(ui->tray_item_lx);
            ui->tray_item_lx = NULL;
            free(ui->tray_item_w);
            ui->tray_item_w = NULL;
            free(ui->tray_rects);
            ui->tray_rects = NULL;
            free(ui->tray_icons);
            ui->tray_icons = NULL;
            ui->tray_count = 0;
        } else {
            int xoff = ui->tray_x;
            const int pad = ui->height >= 8 ? 2 : 0;
            int size = ui->height - 2 * pad;
            if (size < 1) {
                size = 1;
            }

            float item[4] = {server->decor_theme.titlebar_active[0], server->decor_theme.titlebar_active[1],
                server->decor_theme.titlebar_active[2], 0.65f};

#ifdef HAVE_SYSTEMD
            size_t idx = 0;
            if (server->sni.items.prev != NULL && server->sni.items.next != NULL) {
                struct fbwl_sni_item *sni;
                wl_list_for_each(sni, &server->sni.items, link) {
                    if (sni->status == FBWL_SNI_STATUS_PASSIVE) {
                        continue;
                    }
                    if (idx >= ui->tray_count) {
                        break;
                    }

                    ui->tray_item_lx[idx] = xoff;
                    ui->tray_item_w[idx] = ui->tray_icon_w;

                    ui->tray_ids[idx] = strdup(sni->id != NULL ? sni->id : "");
                    ui->tray_services[idx] = strdup(sni->service != NULL ? sni->service : "");
                    ui->tray_paths[idx] = strdup(sni->path != NULL ? sni->path : "");

                    ui->tray_rects[idx] = wlr_scene_rect_create(ui->tree, size, size, item);
                    if (ui->tray_rects[idx] != NULL) {
                        wlr_scene_node_set_position(&ui->tray_rects[idx]->node, xoff + pad, pad);
                    }

                    ui->tray_icons[idx] = wlr_scene_buffer_create(ui->tree, sni->icon_buf);
                    if (ui->tray_icons[idx] != NULL) {
                        wlr_scene_node_set_position(&ui->tray_icons[idx]->node, xoff + pad, pad);
                        wlr_scene_buffer_set_dest_size(ui->tray_icons[idx], size, size);
                    }

                    wlr_log(WLR_INFO, "Toolbar: tray item idx=%zu lx=%d w=%d id=%s",
                        idx, xoff, ui->tray_icon_w, sni->id != NULL ? sni->id : "");

                    xoff += ui->tray_icon_w;
                    idx++;
                }
            }
            ui->tray_count = idx;
#endif
        }
    }

    if (ui->clock_w > 0) {
        ui->clock_label = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->clock_label != NULL) {
            wlr_scene_node_set_position(&ui->clock_label->node, ui->clock_x, 0);
            server_toolbar_ui_clock_render(server);
        }

        struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
        if (loop != NULL) {
            ui->clock_timer = wl_event_loop_add_timer(loop, server_toolbar_clock_timer, server);
            if (ui->clock_timer != NULL) {
                wl_event_source_timer_update(ui->clock_timer, 1000);
            }
        }
    }

    server_toolbar_ui_update_iconbar_focus(server);

    wlr_scene_node_raise_to_top(&ui->tree->node);
    wlr_log(WLR_INFO, "Toolbar: built x=%d y=%d w=%d h=%d cell_w=%d workspaces=%zu iconbar=%zu tray=%zu clock_w=%d",
        ui->x, ui->y, ui->width, ui->height, ui->cell_w, ui->cell_count, ui->iconbar_count, ui->tray_count,
        ui->clock_w);
    server_toolbar_ui_update_position(server);
}

#ifdef HAVE_SYSTEMD
static void server_sni_on_change(void *userdata) {
    server_toolbar_ui_rebuild(userdata);
}
#endif

static void server_toolbar_ui_update_position(struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return;
    }
    struct fbwl_toolbar_ui *ui = &server->toolbar_ui;
    if (!ui->enabled || ui->tree == NULL) {
        return;
    }

    struct wlr_output *out = wlr_output_layout_get_center_output(server->output_layout);
    if (out == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, out, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    const int desired_w = box.width > ui->ws_width ? box.width : ui->ws_width;
    if (desired_w != ui->width) {
        server_toolbar_ui_rebuild(server);
        return;
    }

    int x = box.x;
    int y = box.y + box.height - ui->height;
    if (y < box.y) {
        y = box.y;
    }

    if (x == ui->x && y == ui->y) {
        return;
    }

    ui->x = x;
    ui->y = y;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
    wlr_log(WLR_INFO, "Toolbar: position x=%d y=%d h=%d cell_w=%d workspaces=%zu",
        ui->x, ui->y, ui->height, ui->cell_w, ui->cell_count);
}

static bool server_toolbar_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button) {
    if (server == NULL) {
        return false;
    }
    struct fbwl_toolbar_ui *ui = &server->toolbar_ui;
    if (!ui->enabled || ui->tree == NULL || ui->cell_w < 1 || ui->height < 1 || ui->cell_count < 1) {
        return false;
    }

    const int x = lx - ui->x;
    const int y = ly - ui->y;
    if (x < 0 || x >= ui->width || y < 0 || y >= ui->height) {
        return false;
    }

    if (x < ui->ws_width) {
        if (button != BTN_LEFT) {
            return false;
        }

        const int idx = x / ui->cell_w;
        if (idx < 0 || (size_t)idx >= ui->cell_count) {
            return true;
        }

        wlr_log(WLR_INFO, "Toolbar: click workspace=%d", idx + 1);
        fbwm_core_workspace_switch(&server->wm, idx);
        apply_workspace_visibility(server, "toolbar");
        return true;
    }

    if (button == BTN_LEFT &&
            ui->iconbar_count > 0 && ui->iconbar_views != NULL && ui->iconbar_item_lx != NULL &&
            ui->iconbar_item_w != NULL) {
        for (size_t i = 0; i < ui->iconbar_count; i++) {
            const int ix = ui->iconbar_item_lx[i];
            const int iw = ui->iconbar_item_w[i];
            if (x < ix || x >= ix + iw) {
                continue;
            }

            struct fbwl_view *view = ui->iconbar_views[i];
            if (view == NULL) {
                return true;
            }

            wlr_log(WLR_INFO, "Toolbar: click iconbar idx=%zu title=%s", i, view_display_title(view));

            if (view->minimized) {
                view_set_minimized(view, false, "toolbar-iconbar");
            }

            if (!view->wm_view.sticky &&
                    view->wm_view.workspace != fbwm_core_workspace_current(&server->wm)) {
                fbwm_core_workspace_switch(&server->wm, view->wm_view.workspace);
                apply_workspace_visibility(server, "toolbar-iconbar-switch");
            }

            fbwm_core_focus_view(&server->wm, &view->wm_view);
            return true;
        }
    }

    if (ui->tray_count > 0 && ui->tray_item_lx != NULL && ui->tray_item_w != NULL) {
        for (size_t i = 0; i < ui->tray_count; i++) {
            const int ix = ui->tray_item_lx[i];
            const int iw = ui->tray_item_w[i];
            if (x < ix || x >= ix + iw) {
                continue;
            }

            const char *id = (ui->tray_ids != NULL && ui->tray_ids[i] != NULL) ? ui->tray_ids[i] : "";
            const char *method = NULL;
            const char *action = NULL;
            if (button == BTN_LEFT) {
                method = "Activate";
                action = "activate";
            } else if (button == BTN_MIDDLE) {
                method = "SecondaryActivate";
                action = "secondary-activate";
            } else if (button == BTN_RIGHT) {
                method = "ContextMenu";
                action = "context-menu";
            } else {
                return false;
            }

            wlr_log(WLR_INFO, "Toolbar: click tray idx=%zu id=%s action=%s", i, id, action);

#ifdef HAVE_SYSTEMD
            if (ui->tray_services != NULL && ui->tray_paths != NULL) {
                const char *service = ui->tray_services[i];
                const char *path = ui->tray_paths[i];
                fbwl_sni_send_item_action(&server->sni, id, service, path, method, action, lx, ly);
            }
#endif

            return true;
        }
    }
    return button == BTN_LEFT;
}

static void server_cmd_dialog_ui_destroy_scene(struct fbwl_cmd_dialog_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->label = NULL;
}

static void server_cmd_dialog_ui_render(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL) {
        return;
    }

    struct fbwl_cmd_dialog_ui *ui = &server->cmd_dialog_ui;
    if (!ui->open || ui->tree == NULL) {
        return;
    }

    if (ui->label == NULL) {
        ui->label = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->label == NULL) {
            return;
        }
        wlr_scene_node_set_position(&ui->label->node, 0, 0);
    }

    const char *txt = ui->text != NULL ? ui->text : "";
    const char *prefix = "Run: ";
    size_t need = strlen(prefix) + strlen(txt) + 1;
    char *render = malloc(need);
    if (render == NULL) {
        return;
    }
    (void)snprintf(render, need, "%s%s", prefix, txt);

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct wlr_buffer *buf = fbwl_text_buffer_create(render, ui->width, ui->height, 8, fg);
    free(render);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(ui->label, NULL);
        return;
    }

    wlr_scene_buffer_set_buffer(ui->label, buf);
    wlr_buffer_drop(buf);
}

static void server_cmd_dialog_ui_update_position(struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return;
    }
    struct fbwl_cmd_dialog_ui *ui = &server->cmd_dialog_ui;
    if (!ui->open || ui->tree == NULL) {
        return;
    }

    struct wlr_output *out = wlr_output_layout_get_center_output(server->output_layout);
    if (out == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, out, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    int x = box.x + (box.width - ui->width) / 2;
    int y = box.y + box.height / 4;
    if (x < box.x) {
        x = box.x;
    }
    if (y < box.y) {
        y = box.y;
    }

    if (x == ui->x && y == ui->y) {
        return;
    }

    ui->x = x;
    ui->y = y;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
    wlr_log(WLR_INFO, "CmdDialog: position x=%d y=%d w=%d h=%d", ui->x, ui->y, ui->width, ui->height);
}

static void server_cmd_dialog_ui_close(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }

    struct fbwl_cmd_dialog_ui *ui = &server->cmd_dialog_ui;
    if (!ui->open) {
        return;
    }

    server_cmd_dialog_ui_destroy_scene(ui);
    ui->open = false;
    free(ui->text);
    ui->text = NULL;

    wlr_log(WLR_INFO, "CmdDialog: close reason=%s", why != NULL ? why : "(null)");
}

static void server_cmd_dialog_ui_open(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL) {
        return;
    }

    server_menu_ui_close(server, "cmd-dialog-open");
    server_cmd_dialog_ui_close(server, "reopen");

    struct fbwl_cmd_dialog_ui *ui = &server->cmd_dialog_ui;
    ui->open = true;

    ui->height = server->decor_theme.title_height > 0 ? server->decor_theme.title_height : 24;
    ui->width = 600;
    if (ui->width < 200) {
        ui->width = 200;
    }

    free(ui->text);
    ui->text = strdup("");
    if (ui->text == NULL) {
        ui->open = false;
        return;
    }

    struct wlr_scene_tree *parent =
        server->layer_overlay != NULL ? server->layer_overlay : &server->scene->tree;
    ui->tree = wlr_scene_tree_create(parent);
    if (ui->tree == NULL) {
        ui->open = false;
        free(ui->text);
        ui->text = NULL;
        return;
    }

    float bg[4] = {server->decor_theme.titlebar_inactive[0], server->decor_theme.titlebar_inactive[1],
        server->decor_theme.titlebar_inactive[2], 0.95f};
    ui->bg = wlr_scene_rect_create(ui->tree, ui->width, ui->height, bg);
    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, 0, 0);
    }

    server_cmd_dialog_ui_render(server);
    wlr_scene_node_raise_to_top(&ui->tree->node);
    server_cmd_dialog_ui_update_position(server);

    wlr_log(WLR_INFO, "CmdDialog: open");
}

static bool cmd_dialog_text_backspace(struct fbwl_cmd_dialog_ui *ui) {
    if (ui == NULL || ui->text == NULL) {
        return false;
    }
    size_t len = strlen(ui->text);
    if (len == 0) {
        return false;
    }

    size_t i = len - 1;
    while (i > 0 && ((unsigned char)ui->text[i] & 0xC0) == 0x80) {
        i--;
    }
    ui->text[i] = '\0';
    return true;
}

static bool server_cmd_dialog_ui_handle_key(struct fbwl_server *server, xkb_keysym_t sym, uint32_t modifiers) {
    if (server == NULL) {
        return false;
    }
    struct fbwl_cmd_dialog_ui *ui = &server->cmd_dialog_ui;
    if (!ui->open) {
        return false;
    }

    if (sym == XKB_KEY_Escape) {
        server_cmd_dialog_ui_close(server, "escape");
        return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        const char *cmd = ui->text != NULL ? ui->text : "";
        if (*cmd != '\0') {
            wlr_log(WLR_INFO, "CmdDialog: execute cmd=%s", cmd);
            spawn(cmd);
            server_cmd_dialog_ui_close(server, "execute");
        } else {
            server_cmd_dialog_ui_close(server, "empty-enter");
        }
        return true;
    }
    if (sym == XKB_KEY_BackSpace) {
        if (cmd_dialog_text_backspace(ui)) {
            server_cmd_dialog_ui_render(server);
        }
        return true;
    }

    if ((modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO)) != 0) {
        return true;
    }

    char utf8[16];
    int n = xkb_keysym_to_utf8(sym, utf8, sizeof(utf8));
    if (n <= 0 || (size_t)n >= sizeof(utf8)) {
        return true;
    }
    utf8[n] = '\0';

    size_t cur = ui->text != NULL ? strlen(ui->text) : 0;
    if (cur > 4096) {
        return true;
    }

    char *next = realloc(ui->text, cur + (size_t)n + 1);
    if (next == NULL) {
        return true;
    }
    ui->text = next;
    memcpy(ui->text + cur, utf8, (size_t)n + 1);
    server_cmd_dialog_ui_render(server);
    return true;
}

static void server_osd_ui_destroy_scene(struct fbwl_osd_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->label = NULL;
}

static void server_osd_ui_hide(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    struct fbwl_osd_ui *ui = &server->osd_ui;
    if (!ui->enabled || !ui->visible) {
        return;
    }
    ui->visible = false;
    if (ui->tree != NULL) {
        wlr_scene_node_set_enabled(&ui->tree->node, false);
    }
    wlr_log(WLR_INFO, "OSD: hide reason=%s", why != NULL ? why : "(null)");
}

static int server_osd_hide_timer(void *data) {
    struct fbwl_server *server = data;
    server_osd_ui_hide(server, "timer");
    return 0;
}

static void server_osd_ui_update_position(struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return;
    }
    struct fbwl_osd_ui *ui = &server->osd_ui;
    if (!ui->enabled || !ui->visible || ui->tree == NULL) {
        return;
    }

    struct wlr_output *out = wlr_output_layout_get_center_output(server->output_layout);
    if (out == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, out, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    int x = box.x + (box.width - ui->width) / 2;
    int y = box.y + 12;
    if (x < box.x) {
        x = box.x;
    }
    if (y < box.y) {
        y = box.y;
    }

    if (x == ui->x && y == ui->y) {
        return;
    }

    ui->x = x;
    ui->y = y;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
}

static void server_osd_ui_show_workspace(struct fbwl_server *server, int workspace) {
    if (server == NULL || server->scene == NULL) {
        return;
    }
    struct fbwl_osd_ui *ui = &server->osd_ui;
    if (!ui->enabled) {
        return;
    }

    if (ui->height < 1) {
        ui->height = server->decor_theme.title_height > 0 ? server->decor_theme.title_height : 24;
    }
    if (ui->width < 1) {
        ui->width = 200;
    }

    if (ui->tree == NULL) {
        struct wlr_scene_tree *parent =
            server->layer_top != NULL ? server->layer_top : &server->scene->tree;
        ui->tree = wlr_scene_tree_create(parent);
        if (ui->tree == NULL) {
            return;
        }

        float bg[4] = {server->decor_theme.titlebar_active[0], server->decor_theme.titlebar_active[1],
            server->decor_theme.titlebar_active[2], 0.85f};
        ui->bg = wlr_scene_rect_create(ui->tree, ui->width, ui->height, bg);
        if (ui->bg != NULL) {
            wlr_scene_node_set_position(&ui->bg->node, 0, 0);
        }

        ui->label = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->label != NULL) {
            wlr_scene_node_set_position(&ui->label->node, 0, 0);
        }
    }

    if (ui->tree != NULL) {
        wlr_scene_node_set_enabled(&ui->tree->node, true);
    }
    ui->visible = true;

    char msg[64];
    if (snprintf(msg, sizeof(msg), "Workspace %d", workspace + 1) < 0) {
        msg[0] = '\0';
    }

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct wlr_buffer *buf = fbwl_text_buffer_create(msg, ui->width, ui->height, 8, fg);
    if (buf != NULL) {
        if (ui->label != NULL) {
            wlr_scene_buffer_set_buffer(ui->label, buf);
        }
        wlr_buffer_drop(buf);
    }

    if (ui->hide_timer != NULL) {
        wl_event_source_timer_update(ui->hide_timer, 600);
    }

    server_osd_ui_update_position(server);
    wlr_scene_node_raise_to_top(&ui->tree->node);
    wlr_log(WLR_INFO, "OSD: show workspace=%d", workspace + 1);
}

static void server_osd_ui_destroy(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    struct fbwl_osd_ui *ui = &server->osd_ui;
    if (ui->hide_timer != NULL) {
        wl_event_source_remove(ui->hide_timer);
        ui->hide_timer = NULL;
    }
    server_osd_ui_destroy_scene(ui);
    ui->enabled = false;
    ui->visible = false;
}

static void server_menu_ui_destroy_scene(struct fbwl_menu_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->highlight = NULL;
    free(ui->item_rects);
    ui->item_rects = NULL;
    ui->item_rect_count = 0;
}

static void server_menu_ui_close(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }

    if (!server->menu_ui.open) {
        return;
    }

    server_menu_ui_destroy_scene(&server->menu_ui);
    server->menu_ui.open = false;
    server->menu_ui.current = NULL;
    server->menu_ui.depth = 0;
    server->menu_ui.selected = 0;
    server->menu_ui.target_view = NULL;

    wlr_log(WLR_INFO, "Menu: close reason=%s", why != NULL ? why : "(null)");
}

static void server_menu_ui_rebuild(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL) {
        return;
    }
    struct fbwl_menu_ui *ui = &server->menu_ui;
    if (!ui->open || ui->current == NULL) {
        return;
    }

    server_menu_ui_destroy_scene(ui);

    struct wlr_scene_tree *parent =
        server->layer_overlay != NULL ? server->layer_overlay : &server->scene->tree;
    ui->tree = wlr_scene_tree_create(parent);
    if (ui->tree == NULL) {
        ui->open = false;
        ui->current = NULL;
        ui->depth = 0;
        ui->selected = 0;
        return;
    }
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);

    const int count = (int)ui->current->item_count;
    const int item_h = ui->item_h > 0 ? ui->item_h : server->decor_theme.title_height;
    const int w = ui->width > 0 ? ui->width : 200;
    const int h = count > 0 ? count * item_h : item_h;

    float bg[4] = {server->decor_theme.titlebar_inactive[0], server->decor_theme.titlebar_inactive[1],
        server->decor_theme.titlebar_inactive[2], 0.95f};
    float hi[4] = {server->decor_theme.titlebar_active[0], server->decor_theme.titlebar_active[1],
        server->decor_theme.titlebar_active[2], 0.95f};

    ui->bg = wlr_scene_rect_create(ui->tree, w, h, bg);
    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, 0, 0);
    }

    if (ui->selected >= ui->current->item_count) {
        ui->selected = ui->current->item_count > 0 ? ui->current->item_count - 1 : 0;
    }
    ui->highlight = wlr_scene_rect_create(ui->tree, w, item_h, hi);
    if (ui->highlight != NULL) {
        wlr_scene_node_set_position(&ui->highlight->node, 0, (int)ui->selected * item_h);
    }

    ui->item_rect_count = ui->current->item_count;
    if (ui->item_rect_count > 0) {
        ui->item_rects = calloc(ui->item_rect_count, sizeof(*ui->item_rects));
        if (ui->item_rects != NULL) {
            float item[4] = {0.00f, 0.00f, 0.00f, 0.01f};
            for (size_t i = 0; i < ui->item_rect_count; i++) {
                ui->item_rects[i] = wlr_scene_rect_create(ui->tree, w, item_h, item);
                if (ui->item_rects[i] != NULL) {
                    wlr_scene_node_set_position(&ui->item_rects[i]->node, 0, (int)i * item_h);
                }
            }
        }
    }

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (size_t i = 0; i < ui->current->item_count; i++) {
        const struct fbwl_menu_item *it = &ui->current->items[i];
        const char *label = it->label != NULL ? it->label : "(no-label)";
        char render_label[512];
        const char *render = label;
        if (it->kind == FBWL_MENU_ITEM_SUBMENU) {
            if (snprintf(render_label, sizeof(render_label), "%s  >", label) >= 0) {
                render = render_label;
            }
        }

        struct wlr_buffer *text_buf = fbwl_text_buffer_create(render, w, item_h, 8, fg);
        if (text_buf != NULL) {
            struct wlr_scene_buffer *sb = wlr_scene_buffer_create(ui->tree, text_buf);
            if (sb != NULL) {
                wlr_scene_node_set_position(&sb->node, 0, (int)i * item_h);
            }
            wlr_buffer_drop(text_buf);
        }
    }

    wlr_scene_node_raise_to_top(&ui->tree->node);
}

static void server_menu_ui_open_root(struct fbwl_server *server, int x, int y) {
    if (server == NULL) {
        return;
    }
    if (server->root_menu == NULL) {
        server_menu_create_default(server);
    }
    if (server->root_menu == NULL) {
        return;
    }

    server_menu_ui_close(server, "reopen");

    struct fbwl_menu_ui *ui = &server->menu_ui;
    ui->open = true;
    ui->current = server->root_menu;
    ui->depth = 0;
    ui->stack[0] = server->root_menu;
    ui->selected = 0;
    ui->target_view = NULL;

    ui->x = x;
    ui->y = y;
    ui->width = 200;
    ui->item_h = server->decor_theme.title_height;

    server_menu_ui_rebuild(server);
    wlr_log(WLR_INFO, "Menu: open at x=%d y=%d items=%zu", x, y, ui->current->item_count);
}

static void server_menu_ui_open_window(struct fbwl_server *server, struct fbwl_view *view, int x, int y) {
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->window_menu == NULL) {
        server_menu_create_window(server);
    }
    if (server->window_menu == NULL) {
        return;
    }

    server_menu_ui_close(server, "reopen-window");

    struct fbwl_menu_ui *ui = &server->menu_ui;
    ui->open = true;
    ui->current = server->window_menu;
    ui->depth = 0;
    ui->stack[0] = server->window_menu;
    ui->selected = 0;
    ui->target_view = view;

    ui->x = x;
    ui->y = y;
    ui->width = 200;
    ui->item_h = server->decor_theme.title_height;

    server_menu_ui_rebuild(server);
    wlr_log(WLR_INFO, "Menu: open-window title=%s x=%d y=%d items=%zu",
        view_display_title(view), x, y, ui->current->item_count);
}

static ssize_t server_menu_ui_index_at(const struct fbwl_menu_ui *ui, int lx, int ly) {
    if (ui == NULL || !ui->open || ui->current == NULL) {
        return -1;
    }
    const int x = lx - ui->x;
    const int y = ly - ui->y;
    const int item_h = ui->item_h > 0 ? ui->item_h : 1;
    const int w = ui->width > 0 ? ui->width : 1;
    const int h = (int)ui->current->item_count * item_h;
    if (x < 0 || x >= w || y < 0 || y >= h) {
        return -1;
    }
    const ssize_t idx = y / item_h;
    if (idx < 0 || (size_t)idx >= ui->current->item_count) {
        return -1;
    }
    return idx;
}

static void server_menu_ui_set_selected(struct fbwl_server *server, size_t idx) {
    if (server == NULL) {
        return;
    }
    struct fbwl_menu_ui *ui = &server->menu_ui;
    if (!ui->open || ui->current == NULL) {
        return;
    }
    if (ui->current->item_count == 0) {
        ui->selected = 0;
        return;
    }
    if (idx >= ui->current->item_count) {
        idx = ui->current->item_count - 1;
    }
    ui->selected = idx;
    if (ui->highlight != NULL) {
        const int item_h = ui->item_h > 0 ? ui->item_h : 1;
        wlr_scene_node_set_position(&ui->highlight->node, 0, (int)ui->selected * item_h);
    }
}

static void server_menu_ui_activate_selected(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    struct fbwl_menu_ui *ui = &server->menu_ui;
    if (!ui->open || ui->current == NULL || ui->current->item_count == 0) {
        return;
    }
    if (ui->selected >= ui->current->item_count) {
        ui->selected = ui->current->item_count - 1;
    }

    struct fbwl_menu_item *it = &ui->current->items[ui->selected];
    const char *label = it->label != NULL ? it->label : "(no-label)";

    if (it->kind == FBWL_MENU_ITEM_EXEC) {
        wlr_log(WLR_INFO, "Menu: exec label=%s cmd=%s", label, it->cmd != NULL ? it->cmd : "(null)");
        spawn(it->cmd);
        server_menu_ui_close(server, "exec");
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_EXIT) {
        wlr_log(WLR_INFO, "Menu: exit label=%s", label);
        server_menu_ui_close(server, "exit");
        wl_display_terminate(server->wl_display);
        return;
    }
    if (it->kind == FBWL_MENU_ITEM_VIEW_ACTION) {
        struct fbwl_view *view = ui->target_view;
        if (view == NULL) {
            server_menu_ui_close(server, "window-action-no-view");
            return;
        }

        switch (it->view_action) {
        case FBWL_MENU_VIEW_CLOSE:
            wlr_log(WLR_INFO, "Menu: window-close title=%s", view_display_title(view));
            if (view->type == FBWL_VIEW_XDG) {
                wlr_xdg_toplevel_send_close(view->xdg_toplevel);
            } else if (view->type == FBWL_VIEW_XWAYLAND) {
                wlr_xwayland_surface_close(view->xwayland_surface);
            }
            server_menu_ui_close(server, "window-close");
            return;
        case FBWL_MENU_VIEW_TOGGLE_MINIMIZE:
            wlr_log(WLR_INFO, "Menu: window-minimize title=%s", view_display_title(view));
            view_set_minimized(view, !view->minimized, "window-menu");
            server_menu_ui_close(server, "window-minimize");
            return;
        case FBWL_MENU_VIEW_TOGGLE_MAXIMIZE:
            wlr_log(WLR_INFO, "Menu: window-maximize title=%s", view_display_title(view));
            view_set_maximized(view, !view->maximized);
            server_menu_ui_close(server, "window-maximize");
            return;
        case FBWL_MENU_VIEW_TOGGLE_FULLSCREEN:
            wlr_log(WLR_INFO, "Menu: window-fullscreen title=%s", view_display_title(view));
            view_set_fullscreen(view, !view->fullscreen, NULL);
            server_menu_ui_close(server, "window-fullscreen");
            return;
        default:
            server_menu_ui_close(server, "window-action-unknown");
            return;
        }
    }
    if (it->kind == FBWL_MENU_ITEM_SUBMENU && it->submenu != NULL) {
        if (ui->depth + 1 < (sizeof(ui->stack) / sizeof(ui->stack[0]))) {
            ui->depth++;
            ui->stack[ui->depth] = it->submenu;
            ui->current = it->submenu;
            ui->selected = 0;
            server_menu_ui_rebuild(server);
            wlr_log(WLR_INFO, "Menu: enter-submenu label=%s items=%zu", label, ui->current->item_count);
        }
        return;
    }
}

static bool server_menu_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button) {
    if (server == NULL) {
        return false;
    }
    struct fbwl_menu_ui *ui = &server->menu_ui;
    if (!ui->open || ui->current == NULL) {
        return false;
    }

    const ssize_t idx = server_menu_ui_index_at(ui, lx, ly);
    if (idx < 0) {
        server_menu_ui_close(server, "click-outside");
        return true;
    }

    server_menu_ui_set_selected(server, (size_t)idx);
    if (button == BTN_LEFT) {
        server_menu_ui_activate_selected(server);
    } else if (button == BTN_RIGHT) {
        server_menu_ui_close(server, "right-click");
    }
    return true;
}

static void server_menu_free(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    server_menu_ui_close(server, "free");
    fbwl_menu_free(server->root_menu);
    server->root_menu = NULL;
    fbwl_menu_free(server->window_menu);
    server->window_menu = NULL;
    free(server->menu_file);
    server->menu_file = NULL;
}

static void server_keybindings_add_defaults(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    server_keybindings_add(server, XKB_KEY_Escape, WLR_MODIFIER_ALT, FBWL_KEYBIND_EXIT, 0, NULL);
    server_keybindings_add(server, XKB_KEY_Return, WLR_MODIFIER_ALT,
        FBWL_KEYBIND_EXEC, 0, server->terminal_cmd);
    server_keybindings_add(server, XKB_KEY_F2, WLR_MODIFIER_ALT,
        FBWL_KEYBIND_COMMAND_DIALOG, 0, NULL);
    server_keybindings_add(server, XKB_KEY_F1, WLR_MODIFIER_ALT, FBWL_KEYBIND_FOCUS_NEXT, 0, NULL);
    server_keybindings_add(server, XKB_KEY_m, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_MAXIMIZE, 0, NULL);
    server_keybindings_add(server, XKB_KEY_f, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_FULLSCREEN, 0, NULL);
    server_keybindings_add(server, XKB_KEY_i, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_MINIMIZE, 0, NULL);
    for (int i = 0; i < 9; i++) {
        server_keybindings_add(server, XKB_KEY_1 + i, WLR_MODIFIER_ALT,
            FBWL_KEYBIND_WORKSPACE_SWITCH, i, NULL);
        server_keybindings_add(server, XKB_KEY_1 + i, WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL,
            FBWL_KEYBIND_SEND_TO_WORKSPACE, i, NULL);
    }
}

static void server_apps_rules_apply_pre_map(struct fbwl_view *view,
        const struct fbwl_apps_rule *rule) {
    if (view == NULL || view->server == NULL || rule == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;

    if (rule->set_workspace) {
        int ws = rule->workspace;
        const int count = fbwm_core_workspace_count(&server->wm);
        if (ws < 0 || ws >= count) {
            wlr_log(WLR_ERROR, "Apps: ignoring out-of-range workspace_id=%d (count=%d) for %s",
                ws, count, view_display_title(view));
        } else {
            view->wm_view.workspace = ws;
        }
    }

    if (rule->set_sticky) {
        view->wm_view.sticky = rule->sticky;
    }

    if (rule->set_workspace && rule->set_jump && rule->jump) {
        const int count = fbwm_core_workspace_count(&server->wm);
        if (rule->workspace >= 0 && rule->workspace < count) {
            fbwm_core_workspace_switch(&server->wm, rule->workspace);
        }
    }
}

static void server_apps_rules_apply_post_map(struct fbwl_view *view,
        const struct fbwl_apps_rule *rule) {
    if (view == NULL || rule == NULL) {
        return;
    }

    if (rule->set_maximized) {
        view_set_maximized(view, rule->maximized);
    }
    if (rule->set_fullscreen) {
        view_set_fullscreen(view, rule->fullscreen, NULL);
    }
    if (rule->set_minimized) {
        view_set_minimized(view, rule->minimized, "apps");
    }
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct fbwl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct fbwl_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;
    server_notify_activity(server);

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
        keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && server->menu_ui.open) {
        for (int i = 0; i < nsyms; i++) {
            const xkb_keysym_t sym = syms[i];
            if (sym == XKB_KEY_Escape) {
                server_menu_ui_close(server, "escape");
                handled = true;
                break;
            }
            if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
                server_menu_ui_activate_selected(server);
                handled = true;
                break;
            }
            if (sym == XKB_KEY_Down) {
                server_menu_ui_set_selected(server, server->menu_ui.selected + 1);
                handled = true;
                break;
            }
            if (sym == XKB_KEY_Up) {
                size_t idx = server->menu_ui.selected;
                if (idx > 0) {
                    idx--;
                }
                server_menu_ui_set_selected(server, idx);
                handled = true;
                break;
            }
            if (sym == XKB_KEY_Left || sym == XKB_KEY_BackSpace) {
                if (server->menu_ui.depth > 0) {
                    server->menu_ui.depth--;
                    server->menu_ui.current = server->menu_ui.stack[server->menu_ui.depth];
                    server->menu_ui.selected = 0;
                    server_menu_ui_rebuild(server);
                    wlr_log(WLR_INFO, "Menu: back items=%zu", server->menu_ui.current != NULL ? server->menu_ui.current->item_count : 0);
                } else {
                    server_menu_ui_close(server, "back");
                }
                handled = true;
                break;
            }
        }
    }

    if (!handled && server->cmd_dialog_ui.open) {
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            for (int i = 0; i < nsyms; i++) {
                if (server_cmd_dialog_ui_handle_key(server, syms[i], modifiers)) {
                    break;
                }
            }
        }
        handled = true;
    }

    if (!handled && !server_keyboard_shortcuts_inhibited(server) &&
            event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            handled = server_handle_keybinding(server, syms[i], modifiers);
            if (handled) {
                break;
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(server->seat,
            event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);

    fbwl_cleanup_listener(&keyboard->modifiers);
    fbwl_cleanup_listener(&keyboard->key);
    fbwl_cleanup_listener(&keyboard->destroy);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

static void server_new_keyboard(struct fbwl_server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct fbwl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    if (wlr_input_device_get_virtual_keyboard(device) == NULL) {
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
            XKB_KEYMAP_COMPILE_NO_FLAGS);

        wlr_keyboard_set_keymap(wlr_keyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
    }

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wl_list_insert(&server->keyboards, &keyboard->link);
    wlr_seat_set_keyboard(server->seat, wlr_keyboard);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    struct wlr_seat_client *focused_client =
        server->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

static void cursor_shape_request_set_shape(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_shape_request_set_shape);
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

    if (server == NULL || server->seat == NULL || server->cursor == NULL || server->cursor_mgr == NULL) {
        return;
    }

    struct wlr_seat_client *focused_client =
        server->seat->pointer_state.focused_client;
    if (focused_client != event->seat_client) {
        return;
    }
    if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER) {
        return;
    }

    const char *name = wlr_cursor_shape_v1_name(event->shape);
    if (name == NULL) {
        name = "default";
    }

    wlr_log(WLR_INFO, "CursorShape: name=%s", name);
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, name);
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

static void seat_request_start_drag(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_start_drag);
    struct wlr_seat_request_start_drag_event *event = data;

    if (!wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
        wlr_log(WLR_ERROR, "DnD: rejected start_drag request due to invalid serial");
        if (event->drag != NULL && event->drag->source != NULL) {
            wlr_data_source_destroy(event->drag->source);
        }
        return;
    }

    wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
}

static void pointer_constraint_set_region(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_pointer_constraint *pc = wl_container_of(listener, pc, set_region);
    struct fbwl_server *server = pc->server;
    if (server == NULL || pc->constraint == NULL) {
        return;
    }
    if (pc->constraint != server->active_pointer_constraint) {
        return;
    }
    if (pc->constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED) {
        return;
    }

    struct wlr_box box = {0};
    if (!server_get_confine_box(server, pc->constraint, &box)) {
        return;
    }
    double x = server->cursor->x;
    double y = server->cursor->y;
    clamp_to_box(&x, &y, &box);
    (void)wlr_cursor_warp(server->cursor, NULL, x, y);
    process_cursor_motion(server, 0);
}

static void pointer_constraint_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_pointer_constraint *pc = wl_container_of(listener, pc, destroy);
    struct fbwl_server *server = pc->server;
    if (server != NULL && server->active_pointer_constraint == pc->constraint) {
        server->active_pointer_constraint = NULL;
    }
    fbwl_cleanup_listener(&pc->set_region);
    fbwl_cleanup_listener(&pc->destroy);
    free(pc);
}

static void server_new_pointer_constraint(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_pointer_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;

    struct fbwl_pointer_constraint *pc = calloc(1, sizeof(*pc));
    pc->server = server;
    pc->constraint = constraint;
    pc->set_region.notify = pointer_constraint_set_region;
    wl_signal_add(&constraint->events.set_region, &pc->set_region);
    pc->destroy.notify = pointer_constraint_destroy;
    wl_signal_add(&constraint->events.destroy, &pc->destroy);

    server_update_pointer_constraint(server);
}

static void xdg_decoration_apply(struct fbwl_xdg_decoration *xd) {
    if (xd == NULL || xd->decoration == NULL || xd->decoration->toplevel == NULL ||
            xd->decoration->toplevel->base == NULL) {
        return;
    }
    if (!xd->decoration->toplevel->base->initialized) {
        return;
    }

    (void)wlr_xdg_toplevel_decoration_v1_set_mode(xd->decoration, xd->desired_mode);

    struct fbwl_view *view = NULL;
    if (xd->decoration->toplevel->base->data != NULL) {
        struct wlr_scene_tree *tree = xd->decoration->toplevel->base->data;
        view = tree->node.data;
    }
    if (view != NULL) {
        view_decor_create(view);
        view_decor_set_enabled(view, xd->desired_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        view_decor_update(view);
    }
}

static void xdg_decoration_surface_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_xdg_decoration *xd = wl_container_of(listener, xd, surface_map);
    xdg_decoration_apply(xd);
    fbwl_cleanup_listener(&xd->surface_map);
}

static void xdg_decoration_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_xdg_decoration *xd = wl_container_of(listener, xd, destroy);
    fbwl_cleanup_listener(&xd->surface_map);
    fbwl_cleanup_listener(&xd->request_mode);
    fbwl_cleanup_listener(&xd->destroy);
    free(xd);
}

static void xdg_decoration_request_mode(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_xdg_decoration *xd = wl_container_of(listener, xd, request_mode);
    xd->desired_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    xdg_decoration_apply(xd);
}

static void server_new_xdg_decoration(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_xdg_decoration);
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

    struct fbwl_xdg_decoration *xd = calloc(1, sizeof(*xd));
    xd->server = server;
    xd->decoration = decoration;
    xd->desired_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

    xd->request_mode.notify = xdg_decoration_request_mode;
    wl_signal_add(&decoration->events.request_mode, &xd->request_mode);
    xd->destroy.notify = xdg_decoration_destroy;
    wl_signal_add(&decoration->events.destroy, &xd->destroy);

    if (decoration->toplevel != NULL && decoration->toplevel->base != NULL &&
            decoration->toplevel->base->initialized) {
        xdg_decoration_apply(xd);
    } else if (decoration->toplevel != NULL && decoration->toplevel->base != NULL &&
            decoration->toplevel->base->surface != NULL) {
        xd->surface_map.notify = xdg_decoration_surface_map;
        wl_signal_add(&decoration->toplevel->base->surface->events.map, &xd->surface_map);
    }
}

static void server_xdg_activation_request_activate(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, xdg_activation_request_activate);
    struct wlr_xdg_activation_v1_request_activate_event *event = data;
    if (event == NULL || event->surface == NULL) {
        return;
    }

    struct fbwl_view *view = server_view_from_surface(event->surface);
    if (view == NULL) {
        wlr_log(WLR_INFO, "XDG activation: request for unknown surface");
        return;
    }

    if (view->minimized) {
        view_set_minimized(view, false, "xdg-activation");
    }
    fbwm_core_focus_view(&server->wm, &view->wm_view);
}

static void idle_inhibitor_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_idle_inhibitor *ii = wl_container_of(listener, ii, destroy);
    struct fbwl_server *server = ii->server;
    fbwl_cleanup_listener(&ii->destroy);
    if (server != NULL && server->idle_inhibitor_count > 0) {
        server->idle_inhibitor_count--;
        server_set_idle_inhibited(server, server->idle_inhibitor_count > 0, "destroy-inhibitor");
    }
    free(ii);
}

static void server_new_idle_inhibitor(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_idle_inhibitor);
    struct wlr_idle_inhibitor_v1 *inhibitor = data;

    struct fbwl_idle_inhibitor *ii = calloc(1, sizeof(*ii));
    ii->server = server;
    ii->inhibitor = inhibitor;
    ii->destroy.notify = idle_inhibitor_destroy;
    wl_signal_add(&inhibitor->events.destroy, &ii->destroy);

    server->idle_inhibitor_count++;
    server_set_idle_inhibited(server, true, "new-inhibitor");
}

static void server_add_input_device(struct fbwl_server *server, struct wlr_input_device *device);

static void server_new_input(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    server_add_input_device(server, device);
}

static void server_add_input_device(struct fbwl_server *server, struct wlr_input_device *device) {
    wlr_log(WLR_INFO, "New input device: type=%d name=%s",
        device->type, device->name != NULL ? device->name : "(null)");
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server->has_pointer = true;
        wlr_cursor_attach_input_device(server->cursor, device);
        break;
    default:
        break;
    }

    uint32_t caps = 0;
    if (!wl_list_empty(&server->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    if (server->has_pointer) {
        caps |= WL_SEAT_CAPABILITY_POINTER;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

static void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *vkbd = data;
    wlr_log(WLR_INFO, "New virtual keyboard");
    server_add_input_device(server, &vkbd->keyboard.base);
}

static void server_new_virtual_pointer(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_virtual_pointer);
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    wlr_log(WLR_INFO, "New virtual pointer");
    server_add_input_device(server, &event->new_pointer->pointer.base);
}

static void fbwl_wm_view_focus(struct fbwm_view *wm_view) {
    struct fbwl_view *view = wm_view->userdata;
    focus_view(view);
}

static bool fbwl_wm_view_is_mapped(const struct fbwm_view *wm_view) {
    const struct fbwl_view *view = wm_view->userdata;
    return view != NULL && view->mapped && !view->minimized;
}

static const char *fbwl_wm_view_title(const struct fbwm_view *wm_view) {
    const struct fbwl_view *view = wm_view->userdata;
    return view_title(view);
}

static const char *fbwl_wm_view_app_id(const struct fbwm_view *wm_view) {
    const struct fbwl_view *view = wm_view->userdata;
    return view_app_id(view);
}

static const struct fbwm_view_ops fbwl_wm_view_ops = {
    .focus = fbwl_wm_view_focus,
    .is_mapped = fbwl_wm_view_is_mapped,
    .title = fbwl_wm_view_title,
    .app_id = fbwl_wm_view_app_id,
};

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, map);
    if (!view->placed) {
        view_place_initial(view);
        view->placed = true;
    }
    view->mapped = true;
    view->minimized = false;
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, false);
    }
    view->wm_view.workspace = fbwm_core_workspace_current(&view->server->wm);

    const struct fbwl_apps_rule *apps_rule = NULL;
    size_t apps_rule_idx = 0;
    if (!view->apps_rules_applied) {
        const char *app_id = view_app_id(view);
        const char *instance = NULL;
        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            instance = view->xwayland_surface->instance;
        } else {
            instance = app_id;
        }
        const char *title = view_title(view);
        apps_rule = fbwl_apps_rules_match(view->server->apps_rules, view->server->apps_rule_count,
            app_id, instance, title, &apps_rule_idx);
        if (apps_rule != NULL) {
            wlr_log(WLR_INFO, "Apps: match rule=%zu title=%s app_id=%s",
                apps_rule_idx,
                view_title(view) != NULL ? view_title(view) : "(no-title)",
                view_app_id(view) != NULL ? view_app_id(view) : "(no-app-id)");

            server_apps_rules_apply_pre_map(view, apps_rule);
            view->apps_rules_applied = true;

            wlr_log(WLR_INFO,
                "Apps: applied title=%s app_id=%s workspace_id=%d sticky=%d jump=%d minimized=%d maximized=%d fullscreen=%d",
                view_display_title(view),
                view_app_id(view) != NULL ? view_app_id(view) : "(no-app-id)",
                apps_rule->set_workspace ? apps_rule->workspace : -1,
                apps_rule->set_sticky ? (apps_rule->sticky ? 1 : 0) : -1,
                apps_rule->set_jump ? (apps_rule->jump ? 1 : 0) : -1,
                apps_rule->set_minimized ? (apps_rule->minimized ? 1 : 0) : -1,
                apps_rule->set_maximized ? (apps_rule->maximized ? 1 : 0) : -1,
                apps_rule->set_fullscreen ? (apps_rule->fullscreen ? 1 : 0) : -1);
        }
    }

    fbwm_core_view_map(&view->server->wm, &view->wm_view);
    apply_workspace_visibility(view->server, "map");
    if (apps_rule != NULL) {
        server_apps_rules_apply_post_map(view, apps_rule);
    }
    fbwm_core_focus_view(&view->server->wm, &view->wm_view);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    view->minimized = false;
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, false);
    }
    fbwm_core_view_unmap(&view->server->wm, &view->wm_view);
    apply_workspace_visibility(view->server, "unmap");
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, commit);
    if (view->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
    }

    struct wlr_surface *surface = view_wlr_surface(view);
    const int w = surface->current.width;
    const int h = surface->current.height;
    if (w > 0 && h > 0 && (w != view->width || h != view->height)) {
        view->width = w;
        view->height = h;
        wlr_log(WLR_INFO, "Surface size: %s %dx%d",
            view_display_title(view),
            w, h);
    }
    view_decor_update(view);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_maximize);
    view_set_maximized(view, view->xdg_toplevel->requested.maximized);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_fullscreen);
    view_set_fullscreen(view, view->xdg_toplevel->requested.fullscreen,
        view->xdg_toplevel->requested.fullscreen_output);
}

static void xdg_toplevel_request_minimize(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_minimize);
    view_set_minimized(view, view->xdg_toplevel->requested.minimized, "xdg-request");
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, set_title);
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel,
            view->xdg_toplevel->title != NULL ? view->xdg_toplevel->title : "");
    }
    view_decor_update_title_text(view);
    if (view->server != NULL) {
        server_toolbar_ui_rebuild(view->server);
    }
}

static void xdg_toplevel_set_app_id(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, set_app_id);
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel,
            view->xdg_toplevel->app_id != NULL ? view->xdg_toplevel->app_id : "");
    }
    if (view->server != NULL) {
        server_toolbar_ui_rebuild(view->server);
    }
}

static void foreign_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_maximize);
    view_set_maximized(view, event->maximized);
}

static void foreign_toplevel_request_minimize(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_minimize);
    view_set_minimized(view, event->minimized, "foreign-request");
}

static void foreign_toplevel_request_activate(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_activate);
    struct fbwl_server *server = view->server;
    if (server == NULL) {
        return;
    }

    if (view->minimized) {
        view_set_minimized(view, false, "foreign-activate");
    }

    if (!view->wm_view.sticky &&
            view->wm_view.workspace != fbwm_core_workspace_current(&server->wm)) {
        fbwm_core_workspace_switch(&server->wm, view->wm_view.workspace);
        apply_workspace_visibility(server, "foreign-activate-switch");
    }

    fbwm_core_focus_view(&server->wm, &view->wm_view);
    (void)event;
}

static void foreign_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_fullscreen);
    view_set_fullscreen(view, event->fullscreen, event->output);
}

static void foreign_toplevel_request_close(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_close);
    if (view->type == FBWL_VIEW_XDG) {
        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
    } else if (view->type == FBWL_VIEW_XWAYLAND) {
        wlr_xwayland_surface_close(view->xwayland_surface);
    }
}

static void xwayland_surface_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, map);
    if (view->xwayland_surface == NULL) {
        return;
    }

    if (!view->placed) {
        view_place_initial(view);
        view->placed = true;
    }

    const int w = view_current_width(view);
    const int h = view_current_height(view);
    if (w > 0 && h > 0) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
            (uint16_t)w, (uint16_t)h);
    }

    view->mapped = true;
    view->minimized = false;
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, false);
    }
    view->wm_view.workspace = fbwm_core_workspace_current(&view->server->wm);

    const struct fbwl_apps_rule *apps_rule = NULL;
    size_t apps_rule_idx = 0;
    if (!view->apps_rules_applied) {
        const char *app_id = view_app_id(view);
        const char *instance = NULL;
        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            instance = view->xwayland_surface->instance;
        } else {
            instance = app_id;
        }
        const char *title = view_title(view);
        apps_rule = fbwl_apps_rules_match(view->server->apps_rules, view->server->apps_rule_count,
            app_id, instance, title, &apps_rule_idx);
        if (apps_rule != NULL) {
            wlr_log(WLR_INFO, "Apps: match rule=%zu title=%s app_id=%s",
                apps_rule_idx,
                view_title(view) != NULL ? view_title(view) : "(no-title)",
                view_app_id(view) != NULL ? view_app_id(view) : "(no-app-id)");

            server_apps_rules_apply_pre_map(view, apps_rule);
            view->apps_rules_applied = true;

            wlr_log(WLR_INFO,
                "Apps: applied title=%s app_id=%s workspace_id=%d sticky=%d jump=%d minimized=%d maximized=%d fullscreen=%d",
                view_display_title(view),
                view_app_id(view) != NULL ? view_app_id(view) : "(no-app-id)",
                apps_rule->set_workspace ? apps_rule->workspace : -1,
                apps_rule->set_sticky ? (apps_rule->sticky ? 1 : 0) : -1,
                apps_rule->set_jump ? (apps_rule->jump ? 1 : 0) : -1,
                apps_rule->set_minimized ? (apps_rule->minimized ? 1 : 0) : -1,
                apps_rule->set_maximized ? (apps_rule->maximized ? 1 : 0) : -1,
                apps_rule->set_fullscreen ? (apps_rule->fullscreen ? 1 : 0) : -1);
        }
    }

    fbwm_core_view_map(&view->server->wm, &view->wm_view);
    apply_workspace_visibility(view->server, "xwayland-map");
    if (apps_rule != NULL) {
        server_apps_rules_apply_post_map(view, apps_rule);
    }
    fbwm_core_focus_view(&view->server->wm, &view->wm_view);
    wlr_log(WLR_INFO, "XWayland: map title=%s class=%s",
        view_title(view) != NULL ? view_title(view) : "(no-title)",
        view_app_id(view) != NULL ? view_app_id(view) : "(no-class)");
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    view->minimized = false;
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, false);
    }
    fbwm_core_view_unmap(&view->server->wm, &view->wm_view);
    apply_workspace_visibility(view->server, "xwayland-unmap");
    wlr_log(WLR_INFO, "XWayland: unmap title=%s", view_display_title(view));
}

static void xwayland_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, commit);
    struct wlr_surface *surface = view_wlr_surface(view);
    if (surface == NULL) {
        return;
    }

    const int w = surface->current.width;
    const int h = surface->current.height;
    if (w > 0 && h > 0 && (w != view->width || h != view->height)) {
        view->width = w;
        view->height = h;
        wlr_log(WLR_INFO, "Surface size: %s %dx%d", view_display_title(view), w, h);
    }
    view_decor_update(view);
}

static void xwayland_surface_associate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_associate);
    struct wlr_xwayland_surface *xsurface = view->xwayland_surface;
    if (view->server == NULL || xsurface == NULL || xsurface->surface == NULL) {
        return;
    }

    struct wlr_scene_tree *parent =
        view->server->layer_normal != NULL ? view->server->layer_normal : &view->server->scene->tree;
    view->scene_tree = wlr_scene_tree_create(parent);
    if (view->scene_tree == NULL) {
        return;
    }
    (void)wlr_scene_surface_create(view->scene_tree, xsurface->surface);
    view->scene_tree->node.data = view;
    view_decor_create(view);
    view_decor_set_enabled(view, true);

    view->x = xsurface->x;
    view->y = xsurface->y;
    wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    view_foreign_update_output_from_position(view);

    view->map.notify = xwayland_surface_map;
    wl_signal_add(&xsurface->surface->events.map, &view->map);
    view->unmap.notify = xwayland_surface_unmap;
    wl_signal_add(&xsurface->surface->events.unmap, &view->unmap);
    view->commit.notify = xwayland_surface_commit;
    wl_signal_add(&xsurface->surface->events.commit, &view->commit);

    wlr_log(WLR_INFO, "XWayland: associate title=%s class=%s",
        view_title(view) != NULL ? view_title(view) : "(no-title)",
        view_app_id(view) != NULL ? view_app_id(view) : "(no-class)");
}

static void xwayland_surface_dissociate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_dissociate);
    if (view->mapped) {
        view->mapped = false;
        fbwm_core_view_unmap(&view->server->wm, &view->wm_view);
        apply_workspace_visibility(view->server, "xwayland-dissociate");
    }

    fbwl_cleanup_listener(&view->map);
    fbwl_cleanup_listener(&view->unmap);
    fbwl_cleanup_listener(&view->commit);

    if (view->scene_tree != NULL) {
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree = NULL;
    }
}

static void xwayland_surface_request_configure(struct wl_listener *listener, void *data) {
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (view->xwayland_surface == NULL || event == NULL) {
        return;
    }

    view->x = event->x;
    view->y = event->y;
    view->width = event->width;
    view->height = event->height;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }
    view_decor_update(view);

    wlr_xwayland_surface_configure(view->xwayland_surface, event->x, event->y,
        event->width, event->height);
    view_foreign_update_output_from_position(view);
    wlr_log(WLR_INFO, "XWayland: request_configure title=%s x=%d y=%d w=%u h=%u mask=0x%x",
        view_display_title(view), event->x, event->y, event->width, event->height, event->mask);
}

static void xwayland_surface_request_activate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_activate);
    struct fbwl_server *server = view->server;
    if (server == NULL) {
        return;
    }

    if (view->minimized) {
        view_set_minimized(view, false, "xwayland-request-activate");
    }
    if (!view->wm_view.sticky &&
            view->wm_view.workspace != fbwm_core_workspace_current(&server->wm)) {
        fbwm_core_workspace_switch(&server->wm, view->wm_view.workspace);
        apply_workspace_visibility(server, "xwayland-request-activate-switch");
    }
    fbwm_core_focus_view(&server->wm, &view->wm_view);
}

static void xwayland_surface_request_close(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_close);
    if (view->xwayland_surface != NULL) {
        wlr_xwayland_surface_close(view->xwayland_surface);
    }
}

static void xwayland_surface_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_set_title);
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel,
            view_title(view) != NULL ? view_title(view) : "");
    }
    view_decor_update_title_text(view);
    if (view->server != NULL) {
        server_toolbar_ui_rebuild(view->server);
    }
}

static void xwayland_surface_set_class(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_set_class);
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel,
            view_app_id(view) != NULL ? view_app_id(view) : "");
    }
    if (view->server != NULL) {
        server_toolbar_ui_rebuild(view->server);
    }
}

static void xwayland_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, destroy);

    fbwl_cleanup_listener(&view->map);
    fbwl_cleanup_listener(&view->unmap);
    fbwl_cleanup_listener(&view->commit);
    fbwl_cleanup_listener(&view->destroy);
    fbwl_cleanup_listener(&view->xwayland_associate);
    fbwl_cleanup_listener(&view->xwayland_dissociate);
    fbwl_cleanup_listener(&view->xwayland_request_configure);
    fbwl_cleanup_listener(&view->xwayland_request_activate);
    fbwl_cleanup_listener(&view->xwayland_request_close);
    fbwl_cleanup_listener(&view->xwayland_set_title);
    fbwl_cleanup_listener(&view->xwayland_set_class);

    fbwl_cleanup_listener(&view->foreign_request_maximize);
    fbwl_cleanup_listener(&view->foreign_request_minimize);
    fbwl_cleanup_listener(&view->foreign_request_activate);
    fbwl_cleanup_listener(&view->foreign_request_fullscreen);
    fbwl_cleanup_listener(&view->foreign_request_close);

    if (view->server != NULL && view->server->focused_view == view) {
        view->server->focused_view = NULL;
    }

    free(view->decor_title_text_cache);
    view->decor_title_text_cache = NULL;

    if (view->scene_tree != NULL) {
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree = NULL;
    }

    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
        view->foreign_toplevel = NULL;
    }
    view->foreign_output = NULL;

    free(view->decor_title_text_cache);
    view->decor_title_text_cache = NULL;

    fbwm_core_view_destroy(&view->server->wm, &view->wm_view);
    if (view->server->wm.focused == NULL) {
        clear_keyboard_focus(view->server);
    }
    free(view);
}

static void server_xwayland_ready(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_server *server = wl_container_of(listener, server, xwayland_ready);
    const char *display_name = server->xwayland != NULL ? server->xwayland->display_name : NULL;
    if (display_name != NULL) {
        setenv("DISPLAY", display_name, true);
    }
    wlr_log(WLR_INFO, "XWayland: ready DISPLAY=%s", display_name != NULL ? display_name : "(null)");
}

static void server_xwayland_new_surface(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, xwayland_new_surface);
    struct wlr_xwayland_surface *xsurface = data;
    if (server == NULL || xsurface == NULL) {
        return;
    }

    wlr_log(WLR_INFO, "XWayland: new_surface id=0x%x override_redirect=%d title=%s class=%s",
        (unsigned)xsurface->window_id,
        xsurface->override_redirect ? 1 : 0,
        xsurface->title != NULL ? xsurface->title : "(no-title)",
        xsurface->class != NULL ? xsurface->class : "(no-class)");

    if (xsurface->override_redirect) {
        return;
    }

    struct fbwl_view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->type = FBWL_VIEW_XWAYLAND;
    view->xwayland_surface = xsurface;
    xsurface->data = view;

    fbwm_view_init(&view->wm_view, &fbwl_wm_view_ops, view);

    view->destroy.notify = xwayland_surface_destroy;
    wl_signal_add(&xsurface->events.destroy, &view->destroy);
    view->xwayland_associate.notify = xwayland_surface_associate;
    wl_signal_add(&xsurface->events.associate, &view->xwayland_associate);
    view->xwayland_dissociate.notify = xwayland_surface_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &view->xwayland_dissociate);
    view->xwayland_request_configure.notify = xwayland_surface_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &view->xwayland_request_configure);
    view->xwayland_request_activate.notify = xwayland_surface_request_activate;
    wl_signal_add(&xsurface->events.request_activate, &view->xwayland_request_activate);
    view->xwayland_request_close.notify = xwayland_surface_request_close;
    wl_signal_add(&xsurface->events.request_close, &view->xwayland_request_close);
    view->xwayland_set_title.notify = xwayland_surface_set_title;
    wl_signal_add(&xsurface->events.set_title, &view->xwayland_set_title);
    view->xwayland_set_class.notify = xwayland_surface_set_class;
    wl_signal_add(&xsurface->events.set_class, &view->xwayland_set_class);

    if (server->foreign_toplevel_mgr != NULL) {
        view->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_mgr);
        if (view->foreign_toplevel != NULL) {
            view->foreign_toplevel->data = view;
            wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel,
                xsurface->title != NULL ? xsurface->title : "");
            wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel,
                xsurface->class != NULL ? xsurface->class : "");
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, view->maximized);
            wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, view->minimized);
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, view->fullscreen);

            view->foreign_request_maximize.notify = foreign_toplevel_request_maximize;
            wl_signal_add(&view->foreign_toplevel->events.request_maximize, &view->foreign_request_maximize);
            view->foreign_request_minimize.notify = foreign_toplevel_request_minimize;
            wl_signal_add(&view->foreign_toplevel->events.request_minimize, &view->foreign_request_minimize);
            view->foreign_request_activate.notify = foreign_toplevel_request_activate;
            wl_signal_add(&view->foreign_toplevel->events.request_activate, &view->foreign_request_activate);
            view->foreign_request_fullscreen.notify = foreign_toplevel_request_fullscreen;
            wl_signal_add(&view->foreign_toplevel->events.request_fullscreen, &view->foreign_request_fullscreen);
            view->foreign_request_close.notify = foreign_toplevel_request_close;
            wl_signal_add(&view->foreign_toplevel->events.request_close, &view->foreign_request_close);
        }
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, destroy);

    fbwl_cleanup_listener(&view->map);
    fbwl_cleanup_listener(&view->unmap);
    fbwl_cleanup_listener(&view->commit);
    fbwl_cleanup_listener(&view->destroy);
    fbwl_cleanup_listener(&view->request_maximize);
    fbwl_cleanup_listener(&view->request_fullscreen);
    fbwl_cleanup_listener(&view->request_minimize);
    fbwl_cleanup_listener(&view->set_title);
    fbwl_cleanup_listener(&view->set_app_id);
    fbwl_cleanup_listener(&view->foreign_request_maximize);
    fbwl_cleanup_listener(&view->foreign_request_minimize);
    fbwl_cleanup_listener(&view->foreign_request_activate);
    fbwl_cleanup_listener(&view->foreign_request_fullscreen);
    fbwl_cleanup_listener(&view->foreign_request_close);

    if (view->server != NULL && view->server->focused_view == view) {
        view->server->focused_view = NULL;
    }

    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
        view->foreign_toplevel = NULL;
    }
    view->foreign_output = NULL;

    fbwm_core_view_destroy(&view->server->wm, &view->wm_view);
    if (view->server->wm.focused == NULL) {
        clear_keyboard_focus(view->server);
    }
    free(view);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;

    struct fbwl_view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->type = FBWL_VIEW_XDG;
    view->xdg_toplevel = xdg_toplevel;
    view->scene_tree =
        wlr_scene_xdg_surface_create(server->layer_normal != NULL ? server->layer_normal : &server->scene->tree,
            xdg_toplevel->base);
    view->scene_tree->node.data = view;
    xdg_toplevel->base->data = view->scene_tree;
    view_decor_create(view);

    fbwm_view_init(&view->wm_view, &fbwl_wm_view_ops, view);

    view->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &view->map);
    view->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &view->unmap);
    view->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &view->commit);

    view->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &view->destroy);

    view->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize, &view->request_maximize);
    view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen, &view->request_fullscreen);
    view->request_minimize.notify = xdg_toplevel_request_minimize;
    wl_signal_add(&xdg_toplevel->events.request_minimize, &view->request_minimize);

    view->set_title.notify = xdg_toplevel_set_title;
    wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);
    view->set_app_id.notify = xdg_toplevel_set_app_id;
    wl_signal_add(&xdg_toplevel->events.set_app_id, &view->set_app_id);

    if (server->foreign_toplevel_mgr != NULL) {
        view->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_mgr);
        if (view->foreign_toplevel != NULL) {
            view->foreign_toplevel->data = view;

            wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel,
                xdg_toplevel->title != NULL ? xdg_toplevel->title : "");
            wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel,
                xdg_toplevel->app_id != NULL ? xdg_toplevel->app_id : "");
            wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, view->maximized);
            wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, view->minimized);
            wlr_foreign_toplevel_handle_v1_set_fullscreen(view->foreign_toplevel, view->fullscreen);

            view->foreign_request_maximize.notify = foreign_toplevel_request_maximize;
            wl_signal_add(&view->foreign_toplevel->events.request_maximize, &view->foreign_request_maximize);
            view->foreign_request_minimize.notify = foreign_toplevel_request_minimize;
            wl_signal_add(&view->foreign_toplevel->events.request_minimize, &view->foreign_request_minimize);
            view->foreign_request_activate.notify = foreign_toplevel_request_activate;
            wl_signal_add(&view->foreign_toplevel->events.request_activate, &view->foreign_request_activate);
            view->foreign_request_fullscreen.notify = foreign_toplevel_request_fullscreen;
            wl_signal_add(&view->foreign_toplevel->events.request_fullscreen, &view->foreign_request_fullscreen);
            view->foreign_request_close.notify = foreign_toplevel_request_close;
            wl_signal_add(&view->foreign_toplevel->events.request_close, &view->foreign_request_close);
        }
    }
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_popup *popup = wl_container_of(listener, popup, commit);
    if (popup->xdg_popup->base->initial_commit) {
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_popup *popup = wl_container_of(listener, popup, destroy);
    fbwl_cleanup_listener(&popup->commit);
    fbwl_cleanup_listener(&popup->destroy);
    free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_popup *xdg_popup = data;

    struct fbwl_popup *popup = calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;

    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (parent != NULL) {
        struct wlr_scene_tree *parent_tree = parent->data;
        xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
    }

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
    popup->destroy.notify = xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_layer_surface);
    if (server == NULL) {
        return;
    }
    fbwl_scene_layers_handle_new_layer_surface(data, server->output_layout, &server->outputs, &server->layer_surfaces,
        server->layer_background, server->layer_bottom, server->layer_top, server->layer_overlay,
        server->scene != NULL ? &server->scene->tree : NULL);
}

static char *ipc_trim_inplace(char *s) {
    while (s != NULL && *s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }

    if (s == NULL || *s == '\0') {
        return s;
    }

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static void server_ipc_command(void *userdata, int client_fd, char *line) {
    struct fbwl_server *server = userdata;
    if (server == NULL || line == NULL) {
        return;
    }

    line = ipc_trim_inplace(line);
    if (line == NULL || *line == '\0') {
        fbwl_ipc_send_line(client_fd, "err empty_command");
        return;
    }

    wlr_log(WLR_INFO, "IPC: cmd=%s", line);

    char *saveptr = NULL;
    char *cmd = strtok_r(line, " \t", &saveptr);
    if (cmd == NULL) {
        fbwl_ipc_send_line(client_fd, "err empty_command");
        return;
    }

    if (strcasecmp(cmd, "ping") == 0) {
        fbwl_ipc_send_line(client_fd, "ok pong");
        return;
    }

    if (strcasecmp(cmd, "quit") == 0 || strcasecmp(cmd, "exit") == 0) {
        fbwl_ipc_send_line(client_fd, "ok quitting");
        wl_display_terminate(server->wl_display);
        return;
    }

    if (strcasecmp(cmd, "get-workspace") == 0 || strcasecmp(cmd, "getworkspace") == 0) {
        char resp[64];
        snprintf(resp, sizeof(resp), "ok workspace=%d", fbwm_core_workspace_current(&server->wm) + 1);
        fbwl_ipc_send_line(client_fd, resp);
        return;
    }

    if (strcasecmp(cmd, "workspace") == 0) {
        char *arg = strtok_r(NULL, " \t", &saveptr);
        if (arg == NULL) {
            fbwl_ipc_send_line(client_fd, "err workspace_requires_number");
            return;
        }

        char *end = NULL;
        long requested = strtol(arg, &end, 10);
        if (end == arg || (end != NULL && *end != '\0') || requested < 1) {
            fbwl_ipc_send_line(client_fd, "err invalid_workspace_number");
            return;
        }

        int ws = (int)requested - 1;
        if (ws >= fbwm_core_workspace_count(&server->wm)) {
            fbwl_ipc_send_line(client_fd, "err workspace_out_of_range");
            return;
        }

        fbwm_core_workspace_switch(&server->wm, ws);
        apply_workspace_visibility(server, "ipc");

        char resp[64];
        snprintf(resp, sizeof(resp), "ok workspace=%d", ws + 1);
        fbwl_ipc_send_line(client_fd, resp);
        return;
    }

    if (strcasecmp(cmd, "nextworkspace") == 0) {
        const int count = fbwm_core_workspace_count(&server->wm);
        const int cur = fbwm_core_workspace_current(&server->wm);
        if (count > 0) {
            fbwm_core_workspace_switch(&server->wm, (cur + 1) % count);
            apply_workspace_visibility(server, "ipc");
        }
        fbwl_ipc_send_line(client_fd, "ok");
        return;
    }

    if (strcasecmp(cmd, "prevworkspace") == 0) {
        const int count = fbwm_core_workspace_count(&server->wm);
        const int cur = fbwm_core_workspace_current(&server->wm);
        if (count > 0) {
            fbwm_core_workspace_switch(&server->wm, (cur + count - 1) % count);
            apply_workspace_visibility(server, "ipc");
        }
        fbwl_ipc_send_line(client_fd, "ok");
        return;
    }

    if (strcasecmp(cmd, "nextwindow") == 0 ||
            strcasecmp(cmd, "focus-next") == 0 || strcasecmp(cmd, "focusnext") == 0) {
        fbwm_core_focus_next(&server->wm);
        fbwl_ipc_send_line(client_fd, "ok");
        return;
    }

    fbwl_ipc_send_line(client_fd, "err unknown_command");
}


static void usage(const char *argv0) {
    printf("Usage: %s [--socket NAME] [--ipc-socket PATH] [--no-xwayland] [--bg-color #RRGGBB[AA]] [-s CMD] [--terminal CMD] [--workspaces N] [--config-dir DIR] [--keys FILE] [--apps FILE] [--style FILE] [--menu FILE] [--log-level LEVEL] [--log-protocol]\n", argv0);
    printf("Keybindings:\n");
    printf("  Alt+Return: spawn terminal\n");
    printf("  Alt+Escape: exit\n");
    printf("  Alt+F1: cycle toplevel\n");
    printf("  Alt+F2: command dialog\n");
    printf("  Alt+M: toggle maximize\n");
    printf("  Alt+F: toggle fullscreen\n");
    printf("  Alt+I: toggle minimize\n");
    printf("  Alt+[1-9]: switch workspace\n");
    printf("  Alt+Ctrl+[1-9]: move focused view to workspace\n");
}

static bool parse_log_level(const char *s, enum wlr_log_importance *out) {
    if (s == NULL || out == NULL) {
        return false;
    }

    if (strcasecmp(s, "silent") == 0 || strcmp(s, "0") == 0) {
        *out = WLR_SILENT;
        return true;
    }
    if (strcasecmp(s, "error") == 0 || strcmp(s, "1") == 0) {
        *out = WLR_ERROR;
        return true;
    }
    if (strcasecmp(s, "info") == 0 || strcmp(s, "2") == 0) {
        *out = WLR_INFO;
        return true;
    }
    if (strcasecmp(s, "debug") == 0 || strcmp(s, "3") == 0) {
        *out = WLR_DEBUG;
        return true;
    }

    return false;
}

static const char *wl_protocol_logger_type_str(enum wl_protocol_logger_type type) {
    switch (type) {
    case WL_PROTOCOL_LOGGER_REQUEST:
        return "REQ";
    case WL_PROTOCOL_LOGGER_EVENT:
        return "EVT";
    default:
        return "?";
    }
}

static void fbwl_wayland_protocol_logger(void *user_data, enum wl_protocol_logger_type type,
    const struct wl_protocol_logger_message *message) {
    (void)user_data;

    if (message == NULL || message->resource == NULL || message->message == NULL || message->message->name == NULL) {
        return;
    }

    struct wl_client *client = wl_resource_get_client(message->resource);
    pid_t pid = 0;
    if (client != NULL) {
        uid_t uid = 0;
        gid_t gid = 0;
        wl_client_get_credentials(client, &pid, &uid, &gid);
    }

    const char *class = wl_resource_get_class(message->resource);
    const uint32_t id = wl_resource_get_id(message->resource);

    fprintf(stderr, "WAYLAND %s pid=%d %s@%u.%s\n", wl_protocol_logger_type_str(type), (int)pid,
        class != NULL ? class : "?", id, message->message->name);
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    const char *ipc_socket_path = NULL;
    const char *startup_cmd = NULL;
    const char *terminal_cmd = "weston-terminal";
    const char *keys_file = NULL;
    const char *apps_file = NULL;
    const char *style_file = NULL;
    const char *menu_file = NULL;
    const char *config_dir = NULL;
    float background_color[4] = {0.08f, 0.08f, 0.08f, 1.0f};
    int workspaces = 4;
    bool workspaces_set = false;
    bool enable_xwayland = true;
    enum wlr_log_importance log_level = WLR_INFO;
    bool log_protocol = false;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"ipc-socket", required_argument, NULL, 4},
        {"no-xwayland", no_argument, NULL, 5},
        {"bg-color", required_argument, NULL, 11},
        {"terminal", required_argument, NULL, 2},
        {"workspaces", required_argument, NULL, 3},
        {"config-dir", required_argument, NULL, 8},
        {"keys", required_argument, NULL, 6},
        {"apps", required_argument, NULL, 7},
        {"style", required_argument, NULL, 9},
        {"menu", required_argument, NULL, 10},
        {"log-level", required_argument, NULL, 12},
        {"log-protocol", no_argument, NULL, 13},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "hs:", options, NULL)) != -1) {
        switch (c) {
        case 1:
            socket_name = optarg;
            break;
        case 4:
            ipc_socket_path = optarg;
            break;
        case 5:
            enable_xwayland = false;
            break;
        case 11:
            if (!fbwl_parse_hex_color(optarg, background_color)) {
                fprintf(stderr, "invalid --bg-color (expected #RRGGBB or #RRGGBBAA): %s\n", optarg);
                return 1;
            }
            break;
        case 2:
            terminal_cmd = optarg;
            break;
        case 3:
            workspaces = atoi(optarg);
            if (workspaces < 1) {
                workspaces = 1;
            }
            workspaces_set = true;
            break;
        case 8:
            config_dir = optarg;
            break;
        case 6:
            keys_file = optarg;
            break;
        case 7:
            apps_file = optarg;
            break;
        case 9:
            style_file = optarg;
            break;
        case 10:
            menu_file = optarg;
            break;
        case 12:
            if (!parse_log_level(optarg, &log_level)) {
                fprintf(stderr, "invalid --log-level (expected silent|error|info|debug or 0-3): %s\n", optarg);
                return 1;
            }
            break;
        case 13:
            log_protocol = true;
            break;
        case 's':
            startup_cmd = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }
    if (optind < argc) {
        usage(argv[0]);
        return 1;
    }

    wlr_log_init(log_level, NULL);

    struct fbwl_server server = {0};
    decor_theme_set_defaults(&server.decor_theme);
    memcpy(server.background_color, background_color, sizeof(server.background_color));
    server.startup_cmd = startup_cmd;
    server.terminal_cmd = terminal_cmd;
    server.has_pointer = false;
    fbwl_ipc_init(&server.ipc);
    wl_list_init(&server.shortcuts_inhibitors);
#ifdef HAVE_SYSTEMD
    wl_list_init(&server.sni.items);
#endif

    server.wl_display = wl_display_create();
    if (server.wl_display == NULL) {
        wlr_log(WLR_ERROR, "failed to create wl_display");
        return 1;
    }

    if (log_protocol) {
        server.protocol_logger = wl_display_add_protocol_logger(server.wl_display, fbwl_wayland_protocol_logger, NULL);
        if (server.protocol_logger == NULL) {
            wlr_log(WLR_ERROR, "failed to add Wayland protocol logger");
            return 1;
        }
        wlr_log(WLR_INFO, "Wayland protocol logging enabled");
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
    wl_event_loop_add_signal(loop, SIGINT, handle_signal, &server);
    wl_event_loop_add_signal(loop, SIGTERM, handle_signal, &server);

    server.osd_ui.enabled = true;
    server.osd_ui.visible = false;
    server.osd_ui.last_workspace = 0;
    server.osd_ui.hide_timer = wl_event_loop_add_timer(loop, server_osd_hide_timer, &server);

    server.backend = wlr_backend_autocreate(loop, NULL);
    if (server.backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return 1;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return 1;
    }
    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return 1;
    }

    server.compositor = wlr_compositor_create(server.wl_display, 5, server.renderer);
    if (server.compositor == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_compositor");
        return 1;
    }
    server.presentation = wlr_presentation_create(server.wl_display, server.backend, 1);
    if (server.presentation == NULL) {
        wlr_log(WLR_ERROR, "failed to create presentation-time protocol");
        return 1;
    }
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);
    server.single_pixel_buffer_mgr = wlr_single_pixel_buffer_manager_v1_create(server.wl_display);
    if (server.single_pixel_buffer_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create single-pixel-buffer manager");
        return 1;
    }

    server.data_control_mgr = wlr_data_control_manager_v1_create(server.wl_display);
    if (server.data_control_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr-data-control manager");
        return 1;
    }

    server.ext_data_control_mgr = wlr_ext_data_control_manager_v1_create(server.wl_display, 1);
    if (server.ext_data_control_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create ext-data-control manager");
        return 1;
    }

    server.primary_selection_mgr = wlr_primary_selection_v1_device_manager_create(server.wl_display);
    if (server.primary_selection_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create primary selection manager");
        return 1;
    }

    server.viewporter = wlr_viewporter_create(server.wl_display);
    if (server.viewporter == NULL) {
        wlr_log(WLR_ERROR, "failed to create viewporter");
        return 1;
    }

    server.fractional_scale_mgr = wlr_fractional_scale_manager_v1_create(server.wl_display, 1);
    if (server.fractional_scale_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create fractional scale manager");
        return 1;
    }

    server.xdg_activation = wlr_xdg_activation_v1_create(server.wl_display);
    if (server.xdg_activation == NULL) {
        wlr_log(WLR_ERROR, "failed to create xdg activation manager");
        return 1;
    }
    server.xdg_activation_request_activate.notify = server_xdg_activation_request_activate;
    wl_signal_add(&server.xdg_activation->events.request_activate, &server.xdg_activation_request_activate);

    server.xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(server.wl_display);
    if (server.xdg_decoration_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create xdg decoration manager");
        return 1;
    }
    server.new_xdg_decoration.notify = server_new_xdg_decoration;
    wl_signal_add(&server.xdg_decoration_mgr->events.new_toplevel_decoration, &server.new_xdg_decoration);

    server.idle_notifier = wlr_idle_notifier_v1_create(server.wl_display);
    if (server.idle_notifier == NULL) {
        wlr_log(WLR_ERROR, "failed to create idle notifier");
        return 1;
    }
    server.idle_inhibited = false;
    wlr_idle_notifier_v1_set_inhibited(server.idle_notifier, false);

    server.idle_inhibit_mgr = wlr_idle_inhibit_v1_create(server.wl_display);
    if (server.idle_inhibit_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create idle inhibit manager");
        return 1;
    }
    server.idle_inhibitor_count = 0;
    server.new_idle_inhibitor.notify = server_new_idle_inhibitor;
    wl_signal_add(&server.idle_inhibit_mgr->events.new_inhibitor, &server.new_idle_inhibitor);

    server.session_lock_mgr = wlr_session_lock_manager_v1_create(server.wl_display);
    if (server.session_lock_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create session lock manager");
        return 1;
    }
    server.new_session_lock.notify = server_new_session_lock;
    wl_signal_add(&server.session_lock_mgr->events.new_lock, &server.new_session_lock);
    server.session_lock = NULL;
    wl_list_init(&server.session_lock_surfaces);
    server.session_locked = false;
    server.session_lock_sent_locked = false;
    server.session_lock_expected_surfaces = 0;

    server.foreign_toplevel_mgr = wlr_foreign_toplevel_manager_v1_create(server.wl_display);
    server.relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(server.wl_display);
    server.pointer_constraints = wlr_pointer_constraints_v1_create(server.wl_display);
    if (server.pointer_constraints == NULL) {
        wlr_log(WLR_ERROR, "failed to create pointer constraints manager");
        return 1;
    }
    server.new_pointer_constraint.notify = server_new_pointer_constraint;
    wl_signal_add(&server.pointer_constraints->events.new_constraint, &server.new_pointer_constraint);
    server.active_pointer_constraint = NULL;
    server.pointer_phys_valid = false;
    server.screencopy_mgr = wlr_screencopy_manager_v1_create(server.wl_display);
    if (server.screencopy_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create screencopy manager");
        return 1;
    }

    server.export_dmabuf_mgr = wlr_export_dmabuf_manager_v1_create(server.wl_display);
    if (server.export_dmabuf_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create export dmabuf manager");
        return 1;
    }

#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8) | 0)
    server.ext_image_copy_capture_mgr = wlr_ext_image_copy_capture_manager_v1_create(server.wl_display, 1);
    if (server.ext_image_copy_capture_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create ext-image-copy-capture manager");
        return 1;
    }

    server.ext_output_image_capture_source_mgr = wlr_ext_output_image_capture_source_manager_v1_create(server.wl_display, 1);
    if (server.ext_output_image_capture_source_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create ext-output-image-capture-source manager");
        return 1;
    }
#endif

    server.output_layout = wlr_output_layout_create(server.wl_display);
    if (server.output_layout == NULL) {
        wlr_log(WLR_ERROR, "failed to create output layout");
        return 1;
    }

    server.output_manager = wlr_output_manager_v1_create(server.wl_display);
    if (server.output_manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create output manager");
        return 1;
    }
    server.output_manager_apply.notify = server_output_manager_apply;
    wl_signal_add(&server.output_manager->events.apply, &server.output_manager_apply);
    server.output_manager_test.notify = server_output_manager_test;
    wl_signal_add(&server.output_manager->events.test, &server.output_manager_test);

    server.output_power_mgr = wlr_output_power_manager_v1_create(server.wl_display);
    if (server.output_power_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create output power manager");
        return 1;
    }
    server.output_power_set_mode.notify = server_output_power_set_mode;
    wl_signal_add(&server.output_power_mgr->events.set_mode, &server.output_power_set_mode);

    server.xdg_output_mgr = wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
    if (server.xdg_output_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create xdg output manager");
        return 1;
    }
    wl_list_init(&server.outputs);
    wl_list_init(&server.layer_surfaces);
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    server.scene = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
    server.layer_background = wlr_scene_tree_create(&server.scene->tree);
    server.layer_bottom = wlr_scene_tree_create(&server.scene->tree);
    server.layer_normal = wlr_scene_tree_create(&server.scene->tree);
    server.layer_fullscreen = wlr_scene_tree_create(&server.scene->tree);
    server.layer_top = wlr_scene_tree_create(&server.scene->tree);
    server.layer_overlay = wlr_scene_tree_create(&server.scene->tree);

    fbwm_core_init(&server.wm);
    char *keys_file_owned = NULL;
    char *apps_file_owned = NULL;
    char *style_file_owned = NULL;
    char *menu_file_owned = NULL;

    if (style_file != NULL) {
        style_file_owned = resolve_config_path(NULL, style_file);
        if (style_file_owned != NULL) {
            style_file = style_file_owned;
        }
    }
    if (menu_file != NULL) {
        menu_file_owned = resolve_config_path(NULL, menu_file);
        if (menu_file_owned != NULL) {
            menu_file = menu_file_owned;
        }
    }
    if (config_dir != NULL) {
        struct fbwl_init_settings init = {0};
        (void)init_load_file(config_dir, &init);

        if (!workspaces_set && init.set_workspaces) {
            workspaces = init.workspaces;
        }

        if (keys_file == NULL) {
            if (init.keys_file != NULL) {
                keys_file_owned = init.keys_file;
                init.keys_file = NULL;
            } else {
                keys_file_owned = path_join(config_dir, "keys");
                if (keys_file_owned != NULL && !file_exists(keys_file_owned)) {
                    free(keys_file_owned);
                    keys_file_owned = NULL;
                }
            }
            keys_file = keys_file_owned;
        }

        if (apps_file == NULL) {
            if (init.apps_file != NULL) {
                apps_file_owned = init.apps_file;
                init.apps_file = NULL;
            } else {
                apps_file_owned = path_join(config_dir, "apps");
                if (apps_file_owned != NULL && !file_exists(apps_file_owned)) {
                    free(apps_file_owned);
                    apps_file_owned = NULL;
                }
            }
            apps_file = apps_file_owned;
        }

        if (style_file == NULL) {
            if (init.style_file != NULL) {
                style_file_owned = init.style_file;
                init.style_file = NULL;
                style_file = style_file_owned;
            }
        }

        if (menu_file == NULL) {
            if (init.menu_file != NULL) {
                menu_file_owned = init.menu_file;
                init.menu_file = NULL;
            } else {
                menu_file_owned = path_join(config_dir, "menu");
                if (menu_file_owned != NULL && !file_exists(menu_file_owned)) {
                    free(menu_file_owned);
                    menu_file_owned = NULL;
                }
            }
            menu_file = menu_file_owned;
        }

        init_settings_free(&init);
    }
    fbwm_core_set_workspace_count(&server.wm, workspaces);
    server_keybindings_add_defaults(&server);
    if (keys_file != NULL) {
        (void)fbwl_keys_parse_file(keys_file, server_keybindings_add_from_keys_file, &server, NULL);
    }
    if (apps_file != NULL) {
        (void)fbwl_apps_rules_load_file(&server.apps_rules, &server.apps_rule_count, apps_file);
    }

    if (style_file != NULL) {
        (void)fbwl_style_load_file(&server.decor_theme, style_file);
    }

    free(keys_file_owned);
    free(apps_file_owned);
    free(style_file_owned);

    if (menu_file != NULL) {
        free(server.menu_file);
        server.menu_file = strdup(menu_file);
        if (!server_menu_load_file(&server, menu_file)) {
            wlr_log(WLR_ERROR, "Menu: falling back to default menu");
            server_menu_create_default(&server);
        }
    } else {
        server_menu_create_default(&server);
    }
    free(menu_file_owned);

    server_menu_create_window(&server);
    server_toolbar_ui_rebuild(&server);

    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
    server.new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

    server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
    server.new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    server.cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(server.wl_display, 1);
    if (server.cursor_shape_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create cursor-shape manager");
        return 1;
    }
    server.cursor_shape_request_set_shape.notify = cursor_shape_request_set_shape;
    wl_signal_add(&server.cursor_shape_mgr->events.request_set_shape, &server.cursor_shape_request_set_shape);

    server.cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
    server.cursor_button.notify = server_cursor_button;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    server.cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    server.cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

    wl_list_init(&server.keyboards);
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    server.seat = wlr_seat_create(server.wl_display, "seat0");

    server.shortcuts_inhibit_mgr = wlr_keyboard_shortcuts_inhibit_v1_create(server.wl_display);
    if (server.shortcuts_inhibit_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create keyboard-shortcuts-inhibit manager");
        return 1;
    }
    server.active_shortcuts_inhibitor = NULL;
    server.new_shortcuts_inhibitor.notify = server_new_shortcuts_inhibitor;
    wl_signal_add(&server.shortcuts_inhibit_mgr->events.new_inhibitor, &server.new_shortcuts_inhibitor);

    server.request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
    server.request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);
    server.request_set_primary_selection.notify = seat_request_set_primary_selection;
    wl_signal_add(&server.seat->events.request_set_primary_selection, &server.request_set_primary_selection);
    server.request_start_drag.notify = seat_request_start_drag;
    wl_signal_add(&server.seat->events.request_start_drag, &server.request_start_drag);

    server.text_input_mgr = wlr_text_input_manager_v3_create(server.wl_display);
    if (server.text_input_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create text input manager");
        return 1;
    }
    server.new_text_input.notify = server_new_text_input;
    wl_signal_add(&server.text_input_mgr->events.text_input, &server.new_text_input);
    server.active_text_input = NULL;

    server.input_method_mgr = wlr_input_method_manager_v2_create(server.wl_display);
    if (server.input_method_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create input method manager");
        return 1;
    }
    server.new_input_method.notify = server_new_input_method;
    wl_signal_add(&server.input_method_mgr->events.input_method, &server.new_input_method);
    server.input_method = NULL;

    if (enable_xwayland) {
        server.xwayland = wlr_xwayland_create(server.wl_display, server.compositor, false);
        if (server.xwayland == NULL) {
            wlr_log(WLR_ERROR, "XWayland: failed to create");
        } else {
            wlr_xwayland_set_seat(server.xwayland, server.seat);
            server.xwayland_ready.notify = server_xwayland_ready;
            wl_signal_add(&server.xwayland->events.ready, &server.xwayland_ready);
            server.xwayland_new_surface.notify = server_xwayland_new_surface;
            wl_signal_add(&server.xwayland->events.new_surface, &server.xwayland_new_surface);
        }
    } else {
        wlr_log(WLR_INFO, "XWayland: disabled");
    }

    server.virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(server.wl_display);
    server.virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(server.wl_display);
    server.new_virtual_keyboard.notify = server_new_virtual_keyboard;
    wl_signal_add(&server.virtual_keyboard_mgr->events.new_virtual_keyboard,
        &server.new_virtual_keyboard);
    server.new_virtual_pointer.notify = server_new_virtual_pointer;
    wl_signal_add(&server.virtual_pointer_mgr->events.new_virtual_pointer,
        &server.new_virtual_pointer);

    const char *socket = NULL;
    if (socket_name != NULL) {
        if (wl_display_add_socket(server.wl_display, socket_name) != 0) {
            wlr_log(WLR_ERROR, "failed to add socket '%s'", socket_name);
            return 1;
        }
        socket = socket_name;
    } else {
        socket = wl_display_add_socket_auto(server.wl_display);
        if (socket == NULL) {
            wlr_log(WLR_ERROR, "failed to create Wayland socket");
            return 1;
        }
    }

    if (!wlr_backend_start(server.backend)) {
        wlr_log(WLR_ERROR, "failed to start backend");
        return 1;
    }

    if (fbwl_ipc_start(&server.ipc, loop, socket, ipc_socket_path, server_ipc_command, &server)) {
        setenv("FBWL_IPC_SOCKET", fbwl_ipc_socket_path(&server.ipc), true);
    }

#ifdef HAVE_SYSTEMD
    (void)fbwl_sni_start(&server.sni, loop, server_sni_on_change, &server);
#endif

    setenv("WAYLAND_DISPLAY", socket, true);
    if (server.startup_cmd != NULL) {
        spawn(server.startup_cmd);
    }

    wlr_log(WLR_INFO, "Running fluxbox-wayland on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.wl_display);

    wl_display_destroy_clients(server.wl_display);
    fbwl_ipc_finish(&server.ipc);
#ifdef HAVE_SYSTEMD
    fbwl_sni_finish(&server.sni);
#endif

    fbwl_cleanup_listener(&server.new_xdg_toplevel);
    fbwl_cleanup_listener(&server.new_xdg_popup);
    fbwl_cleanup_listener(&server.xdg_activation_request_activate);
    fbwl_cleanup_listener(&server.new_xdg_decoration);
    fbwl_cleanup_listener(&server.xwayland_ready);
    fbwl_cleanup_listener(&server.xwayland_new_surface);
    fbwl_cleanup_listener(&server.new_layer_surface);

    fbwl_cleanup_listener(&server.cursor_motion);
    fbwl_cleanup_listener(&server.cursor_motion_absolute);
    fbwl_cleanup_listener(&server.cursor_button);
    fbwl_cleanup_listener(&server.cursor_axis);
    fbwl_cleanup_listener(&server.cursor_frame);
    fbwl_cleanup_listener(&server.cursor_shape_request_set_shape);

    fbwl_cleanup_listener(&server.new_input);
    fbwl_cleanup_listener(&server.request_cursor);
    fbwl_cleanup_listener(&server.request_set_selection);
    fbwl_cleanup_listener(&server.request_set_primary_selection);
    fbwl_cleanup_listener(&server.request_start_drag);
    fbwl_cleanup_listener(&server.new_shortcuts_inhibitor);
    fbwl_cleanup_listener(&server.new_virtual_keyboard);
    fbwl_cleanup_listener(&server.new_virtual_pointer);
    fbwl_cleanup_listener(&server.new_pointer_constraint);
    fbwl_cleanup_listener(&server.new_idle_inhibitor);
    fbwl_cleanup_listener(&server.new_session_lock);
    fbwl_cleanup_listener(&server.session_lock_new_surface);
    fbwl_cleanup_listener(&server.session_lock_unlock);
    fbwl_cleanup_listener(&server.session_lock_destroy);
    fbwl_cleanup_listener(&server.output_manager_apply);
    fbwl_cleanup_listener(&server.output_manager_test);
    fbwl_cleanup_listener(&server.output_power_set_mode);
    fbwl_cleanup_listener(&server.new_text_input);
    fbwl_cleanup_listener(&server.new_input_method);

    fbwl_cleanup_listener(&server.new_output);

    if (server.xwayland != NULL) {
        wlr_xwayland_destroy(server.xwayland);
        server.xwayland = NULL;
    }

    server_cmd_dialog_ui_close(&server, "shutdown");
    server_osd_ui_destroy(&server);
    server_menu_free(&server);
    server_toolbar_ui_destroy_scene(&server.toolbar_ui);

    struct fbwl_output *out;
    wl_list_for_each(out, &server.outputs, link) {
        if (out->background_rect != NULL) {
            wlr_scene_node_destroy(&out->background_rect->node);
            out->background_rect = NULL;
        }
    }

    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    fbwl_apps_rules_free(&server.apps_rules, &server.apps_rule_count);
    server_keybindings_free(&server);
    if (server.protocol_logger != NULL) {
        wl_protocol_logger_destroy(server.protocol_logger);
        server.protocol_logger = NULL;
    }
    wl_display_destroy(server.wl_display);
    return 0;
}
