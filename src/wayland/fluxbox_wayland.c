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
#include "wayland/fbwl_cursor.h"
#include "wayland/fbwl_grabs.h"
#include "wayland/fbwl_ipc.h"
#include "wayland/fbwl_keybindings.h"
#include "wayland/fbwl_keys_parse.h"
#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_menu_parse.h"
#include "wayland/fbwl_ui_menu.h"
#include "wayland/fbwl_output.h"
#include "wayland/fbwl_output_management.h"
#include "wayland/fbwl_output_power.h"
#include "wayland/fbwl_scene_layers.h"
#include "wayland/fbwl_seat.h"
#include "wayland/fbwl_session_lock.h"
#include "wayland/fbwl_sni_tray.h"
#include "wayland/fbwl_style_parse.h"
#include "wayland/fbwl_text_input.h"
#include "wayland/fbwl_ui_cmd_dialog.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_osd.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_foreign_toplevel.h"
#include "wayland/fbwl_xdg_shell.h"
#include "wayland/fbwl_xwayland.h"

struct fbwl_server;

struct fbwl_init_settings {
    bool set_workspaces;
    int workspaces;
    char *keys_file;
    char *apps_file;
    char *style_file;
    char *menu_file;
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

    struct fbwl_text_input_state text_input;

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
};

static void clear_keyboard_focus(struct fbwl_server *server);
static void server_text_input_update_focus(struct fbwl_server *server, struct wlr_surface *surface);
static void server_update_shortcuts_inhibitor(struct fbwl_server *server);
static void server_toolbar_ui_rebuild(struct fbwl_server *server);
static void server_toolbar_ui_update_position(struct fbwl_server *server);
static bool server_toolbar_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button);
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

static void session_lock_clear_keyboard_focus(void *userdata) {
    clear_keyboard_focus(userdata);
}

static void session_lock_text_input_update_focus(void *userdata, struct wlr_surface *surface) {
    server_text_input_update_focus(userdata, surface);
}

static void session_lock_update_shortcuts_inhibitor(void *userdata) {
    server_update_shortcuts_inhibitor(userdata);
}

static struct fbwl_session_lock_hooks session_lock_hooks(struct fbwl_server *server) {
    return (struct fbwl_session_lock_hooks){
        .clear_keyboard_focus = session_lock_clear_keyboard_focus,
        .text_input_update_focus = session_lock_text_input_update_focus,
        .update_shortcuts_inhibitor = session_lock_update_shortcuts_inhibitor,
        .userdata = server,
    };
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
    fbwl_session_lock_on_output_destroyed(&server->session_lock);

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

static void server_text_input_update_focus(struct fbwl_server *server, struct wlr_surface *surface) {
    if (server == NULL) {
        return;
    }

    fbwl_text_input_update_focus(&server->text_input, surface);
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
    if (server != NULL && fbwl_session_lock_is_locked(&server->session_lock)) {
        return;
    }
    struct wlr_seat *seat = server->seat;

    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    if (prev_surface == surface) {
        return;
    }

    wlr_log(WLR_INFO, "Focus: %s (%s)",
        fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
        fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)");

    struct fbwl_view *prev_view = server->focused_view;
    if (prev_view != NULL && prev_view != view && prev_view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_activated(prev_view->foreign_toplevel, false);
    }
    if (prev_view != NULL && prev_view != view) {
        fbwl_view_decor_set_active(prev_view, &server->decor_theme, false);
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

    fbwl_view_set_activated(view, true);
    fbwl_view_decor_set_active(view, &server->decor_theme, true);
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
        fbwl_view_decor_set_active(server->focused_view, &server->decor_theme, false);
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
        fbwl_view_display_title(view),
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

static uint32_t server_keyboard_modifiers(struct fbwl_server *server) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    return keyboard != NULL ? wlr_keyboard_get_modifiers(keyboard) : 0;
}

static void cursor_grab_update(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_grab_update(&server->grab, server->cursor);
}

static bool cursor_menu_is_open(void *userdata) {
    const struct fbwl_server *server = userdata;
    return server != NULL && server->menu_ui.open;
}

static ssize_t cursor_menu_index_at(void *userdata, int lx, int ly) {
    const struct fbwl_server *server = userdata;
    if (server == NULL) {
        return -1;
    }
    return server_menu_ui_index_at(&server->menu_ui, lx, ly);
}

static void cursor_menu_set_selected(void *userdata, size_t idx) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    server_menu_ui_set_selected(server, idx);
}

static struct fbwl_cursor_menu_hooks cursor_menu_hooks(struct fbwl_server *server) {
    return (struct fbwl_cursor_menu_hooks){
        .userdata = server,
        .is_open = cursor_menu_is_open,
        .index_at = cursor_menu_index_at,
        .set_selected = cursor_menu_set_selected,
    };
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    server_notify_activity(server);
    const struct fbwl_cursor_menu_hooks hooks = cursor_menu_hooks(server);
    fbwl_cursor_handle_motion(server->cursor, server->cursor_mgr, server->scene, server->seat,
        server->relative_pointer_mgr, server->pointer_constraints, &server->active_pointer_constraint,
        &server->pointer_phys_valid, &server->pointer_phys_x, &server->pointer_phys_y,
        server->grab.mode, cursor_grab_update, server,
        &hooks, event);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    server_notify_activity(server);
    const struct fbwl_cursor_menu_hooks hooks = cursor_menu_hooks(server);
    fbwl_cursor_handle_motion_absolute(server->cursor, server->cursor_mgr, server->scene, server->seat,
        server->relative_pointer_mgr, server->pointer_constraints, &server->active_pointer_constraint,
        &server->pointer_phys_valid, &server->pointer_phys_x, &server->pointer_phys_y,
        server->grab.mode, cursor_grab_update, server,
        &hooks, event);
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
                    fbwl_view_display_title(view),
                    view->x, view->y);
            }
            if (view != NULL && server->grab.mode == FBWL_CURSOR_RESIZE) {
                wlr_log(WLR_INFO, "Resize: %s w=%d h=%d",
                    fbwl_view_display_title(view),
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
        struct fbwl_view *view = fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        wlr_log(WLR_INFO, "Pointer press at %.1f,%.1f hit=%s",
            server->cursor->x, server->cursor->y,
            view != NULL ? fbwl_view_display_title(view) : "(none)");

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
                fbwl_view_current_width(view);
            server->grab.view_h =
                fbwl_view_current_height(view);
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
                fbwl_view_decor_hit_test(view, &server->decor_theme, server->cursor->x, server->cursor->y);
            if (hit.kind == FBWL_DECOR_HIT_TITLEBAR) {
                server_menu_ui_open_window(server, view, (int)server->cursor->x, (int)server->cursor->y);
                return;
            }
        }

        if (view != NULL && surface == NULL && event->button == BTN_LEFT) {
            const struct fbwl_decor_hit hit =
                fbwl_view_decor_hit_test(view, &server->decor_theme, server->cursor->x, server->cursor->y);
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
                    server->grab.view_w = fbwl_view_current_width(view);
                    server->grab.view_h = fbwl_view_current_height(view);
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
                    server->grab.view_w = fbwl_view_current_width(view);
                    server->grab.view_h = fbwl_view_current_height(view);
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
                    fbwl_view_set_maximized(view, !view->maximized, server->output_layout, &server->outputs);
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

static bool server_keybindings_add_from_keys_file(void *userdata, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd) {
    struct fbwl_server *server = userdata;
    return fbwl_keybindings_add(&server->keybindings, &server->keybinding_count, sym, modifiers, action, arg, cmd);
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

static void toolbar_ui_apply_workspace_visibility(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    apply_workspace_visibility(server, why);
}

static void toolbar_ui_view_set_minimized(void *userdata, struct fbwl_view *view, bool minimized, const char *why) {
    (void)userdata;
    view_set_minimized(view, minimized, why);
}

static struct fbwl_ui_toolbar_env toolbar_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_toolbar_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_top = server != NULL ? server->layer_top : NULL,
        .output_layout = server != NULL ? server->output_layout : NULL,
        .wl_display = server != NULL ? server->wl_display : NULL,
        .wm = server != NULL ? &server->wm : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
        .focused_view = server != NULL ? server->focused_view : NULL,
#ifdef HAVE_SYSTEMD
        .sni = server != NULL ? &server->sni : NULL,
#endif
    };
}

static struct fbwl_ui_toolbar_hooks toolbar_ui_hooks(struct fbwl_server *server) {
    return (struct fbwl_ui_toolbar_hooks){
        .userdata = server,
        .apply_workspace_visibility = toolbar_ui_apply_workspace_visibility,
        .view_set_minimized = toolbar_ui_view_set_minimized,
    };
}

static void server_toolbar_ui_rebuild(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    fbwl_ui_toolbar_rebuild(&server->toolbar_ui, &env);
}

#ifdef HAVE_SYSTEMD
static void server_sni_on_change(void *userdata) {
    server_toolbar_ui_rebuild(userdata);
}
#endif

static void server_toolbar_ui_update_position(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    fbwl_ui_toolbar_update_position(&server->toolbar_ui, &env);
}

static void server_toolbar_ui_update_iconbar_focus(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_ui_toolbar_update_iconbar_focus(&server->toolbar_ui, &server->decor_theme, server->focused_view);
}

static bool server_toolbar_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button) {
    if (server == NULL) {
        return false;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    const struct fbwl_ui_toolbar_hooks hooks = toolbar_ui_hooks(server);
    return fbwl_ui_toolbar_handle_click(&server->toolbar_ui, &env, &hooks, lx, ly, button);
}

static void server_cmd_dialog_ui_update_position(struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return;
    }
    fbwl_ui_cmd_dialog_update_position(&server->cmd_dialog_ui, server->output_layout);
}

static void server_cmd_dialog_ui_close(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_cmd_dialog_close(&server->cmd_dialog_ui, why);
}

static void server_cmd_dialog_ui_open(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL) {
        return;
    }

    server_menu_ui_close(server, "cmd-dialog-open");
    fbwl_ui_cmd_dialog_open(&server->cmd_dialog_ui, server->scene, server->layer_overlay,
        &server->decor_theme, server->output_layout);
}

static bool server_cmd_dialog_ui_handle_key(struct fbwl_server *server, xkb_keysym_t sym, uint32_t modifiers) {
    if (server == NULL) {
        return false;
    }
    return fbwl_ui_cmd_dialog_handle_key(&server->cmd_dialog_ui, sym, modifiers);
}

static void server_osd_ui_hide(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_osd_hide(&server->osd_ui, why);
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
    fbwl_ui_osd_update_position(&server->osd_ui, server->output_layout);
}

static void server_osd_ui_show_workspace(struct fbwl_server *server, int workspace) {
    if (server == NULL || server->scene == NULL) {
        return;
    }
    fbwl_ui_osd_show_workspace(&server->osd_ui, server->scene, server->layer_top,
        &server->decor_theme, server->output_layout, workspace);
}

static void server_osd_ui_destroy(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_osd_destroy(&server->osd_ui);
}

static void menu_ui_spawn(void *userdata, const char *cmd) {
    (void)userdata;
    fbwl_spawn(cmd);
}

static void menu_ui_terminate(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    wl_display_terminate(server->wl_display);
}

static void menu_ui_view_close(void *userdata, struct fbwl_view *view) {
    (void)userdata;
    if (view == NULL) {
        return;
    }
    if (view->type == FBWL_VIEW_XDG) {
        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
    } else if (view->type == FBWL_VIEW_XWAYLAND) {
        wlr_xwayland_surface_close(view->xwayland_surface);
    }
}

static void menu_ui_view_set_minimized(void *userdata, struct fbwl_view *view, bool minimized, const char *why) {
    (void)userdata;
    view_set_minimized(view, minimized, why);
}

static void menu_ui_view_set_maximized(void *userdata, struct fbwl_view *view, bool maximized) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_view_set_maximized(view, maximized, server->output_layout, &server->outputs);
}

static void menu_ui_view_set_fullscreen(void *userdata, struct fbwl_view *view, bool fullscreen) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_view_set_fullscreen(view, fullscreen, server->output_layout, &server->outputs,
        server->layer_normal, server->layer_fullscreen, NULL);
}

static struct fbwl_ui_menu_env menu_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_overlay = server != NULL ? server->layer_overlay : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
    };
}

static struct fbwl_ui_menu_hooks menu_ui_hooks(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_hooks){
        .userdata = server,
        .spawn = menu_ui_spawn,
        .terminate = menu_ui_terminate,
        .view_close = menu_ui_view_close,
        .view_set_minimized = menu_ui_view_set_minimized,
        .view_set_maximized = menu_ui_view_set_maximized,
        .view_set_fullscreen = menu_ui_view_set_fullscreen,
    };
}

static void server_menu_ui_close(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_menu_close(&server->menu_ui, why);
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

    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_root(&server->menu_ui, &env, server->root_menu, x, y);
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

    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_window(&server->menu_ui, &env, server->window_menu, view, x, y);
}

static ssize_t server_menu_ui_index_at(const struct fbwl_menu_ui *ui, int lx, int ly) {
    return fbwl_ui_menu_index_at(ui, lx, ly);
}

static void server_menu_ui_set_selected(struct fbwl_server *server, size_t idx) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_menu_set_selected(&server->menu_ui, idx);
}

static bool server_menu_ui_handle_keypress(struct fbwl_server *server, xkb_keysym_t sym) {
    if (server == NULL) {
        return false;
    }
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    const struct fbwl_ui_menu_hooks hooks = menu_ui_hooks(server);
    return fbwl_ui_menu_handle_keypress(&server->menu_ui, &env, &hooks, sym);
}

static bool server_menu_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button) {
    if (server == NULL) {
        return false;
    }
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    const struct fbwl_ui_menu_hooks hooks = menu_ui_hooks(server);
    return fbwl_ui_menu_handle_click(&server->menu_ui, &env, &hooks, lx, ly, button);
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

static void keybindings_terminate(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->wl_display == NULL) {
        return;
    }
    wl_display_terminate(server->wl_display);
}

static void keybindings_spawn(void *userdata, const char *cmd) {
    (void)userdata;
    fbwl_spawn(cmd);
}

static void keybindings_command_dialog_open(void *userdata) {
    struct fbwl_server *server = userdata;
    server_cmd_dialog_ui_open(server);
}

static void keybindings_apply_workspace_visibility(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    apply_workspace_visibility(server, why);
}

static void keybindings_view_set_maximized(void *userdata, struct fbwl_view *view, bool maximized) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_view_set_maximized(view, maximized, server->output_layout, &server->outputs);
}

static void keybindings_view_set_fullscreen(void *userdata, struct fbwl_view *view, bool fullscreen) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_view_set_fullscreen(view, fullscreen, server->output_layout, &server->outputs,
        server->layer_normal, server->layer_fullscreen, NULL);
}

static void keybindings_view_set_minimized(void *userdata, struct fbwl_view *view, bool minimized, const char *why) {
    (void)userdata;
    view_set_minimized(view, minimized, why);
}

static struct fbwl_keybindings_hooks keybindings_hooks(struct fbwl_server *server) {
    return (struct fbwl_keybindings_hooks){
        .userdata = server,
        .wm = server != NULL ? &server->wm : NULL,
        .terminate = keybindings_terminate,
        .spawn = keybindings_spawn,
        .command_dialog_open = keybindings_command_dialog_open,
        .apply_workspace_visibility = keybindings_apply_workspace_visibility,
        .view_set_maximized = keybindings_view_set_maximized,
        .view_set_fullscreen = keybindings_view_set_fullscreen,
        .view_set_minimized = keybindings_view_set_minimized,
    };
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
                ws, count, fbwl_view_display_title(view));
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
        fbwl_view_set_maximized(view, rule->maximized, view->server->output_layout, &view->server->outputs);
    }
    if (rule->set_fullscreen) {
        fbwl_view_set_fullscreen(view, rule->fullscreen, view->server->output_layout, &view->server->outputs,
            view->server->layer_normal, view->server->layer_fullscreen, NULL);
    }
    if (rule->set_minimized) {
        view_set_minimized(view, rule->minimized, "apps");
    }
}

static void seat_notify_activity(void *userdata) {
    struct fbwl_server *server = userdata;
    server_notify_activity(server);
}

static bool seat_menu_is_open(void *userdata) {
    struct fbwl_server *server = userdata;
    return server != NULL && server->menu_ui.open;
}

static bool seat_menu_handle_key(void *userdata, xkb_keysym_t sym) {
    struct fbwl_server *server = userdata;
    return server_menu_ui_handle_keypress(server, sym);
}

static bool seat_cmd_dialog_is_open(void *userdata) {
    struct fbwl_server *server = userdata;
    return server != NULL && server->cmd_dialog_ui.open;
}

static bool seat_cmd_dialog_handle_key(void *userdata, xkb_keysym_t sym, uint32_t modifiers) {
    struct fbwl_server *server = userdata;
    return server_cmd_dialog_ui_handle_key(server, sym, modifiers);
}

static bool seat_shortcuts_inhibited(void *userdata) {
    struct fbwl_server *server = userdata;
    return server_keyboard_shortcuts_inhibited(server);
}

static bool seat_keybindings_handle(void *userdata, xkb_keysym_t sym, uint32_t modifiers) {
    struct fbwl_server *server = userdata;
    const struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
    return fbwl_keybindings_handle(server->keybindings, server->keybinding_count, sym, modifiers, &hooks);
}

static struct fbwl_seat_keyboard_hooks seat_keyboard_hooks(struct fbwl_server *server) {
    return (struct fbwl_seat_keyboard_hooks){
        .userdata = server,
        .notify_activity = seat_notify_activity,
        .menu_is_open = seat_menu_is_open,
        .menu_handle_key = seat_menu_handle_key,
        .cmd_dialog_is_open = seat_cmd_dialog_is_open,
        .cmd_dialog_handle_key = seat_cmd_dialog_handle_key,
        .shortcuts_inhibited = seat_shortcuts_inhibited,
        .keybindings_handle = seat_keybindings_handle,
    };
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_cursor);
    fbwl_seat_handle_request_cursor(server->seat, server->cursor, data);
}

static void cursor_shape_request_set_shape(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_shape_request_set_shape);
    fbwl_cursor_handle_shape_request(server != NULL ? server->seat : NULL,
        server != NULL ? server->cursor : NULL,
        server != NULL ? server->cursor_mgr : NULL,
        data);
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_set_selection);
    fbwl_seat_handle_request_set_selection(server->seat, data);
}

static void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_set_primary_selection);
    fbwl_seat_handle_request_set_primary_selection(server->seat, data);
}

static void seat_request_start_drag(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_start_drag);
    fbwl_seat_handle_request_start_drag(server->seat, data);
}

static void server_new_pointer_constraint(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_pointer_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;
    const struct fbwl_cursor_menu_hooks hooks = cursor_menu_hooks(server);
    fbwl_cursor_handle_new_pointer_constraint(server->cursor, server->cursor_mgr, server->scene, server->seat,
        server->pointer_constraints, &server->active_pointer_constraint, &hooks, constraint);
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
        fbwl_view_decor_create(view, view->server != NULL ? &view->server->decor_theme : NULL);
        fbwl_view_decor_set_enabled(view, xd->desired_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        fbwl_view_decor_update(view, view->server != NULL ? &view->server->decor_theme : NULL);
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

    struct fbwl_view *view = fbwl_view_from_surface(event->surface);
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

static void server_new_input(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    const struct fbwl_seat_keyboard_hooks hooks = seat_keyboard_hooks(server);
    fbwl_seat_add_input_device(server->seat, server->cursor, &server->keyboards, &server->has_pointer, device, &hooks);
}

static void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *vkbd = data;
    wlr_log(WLR_INFO, "New virtual keyboard");
    const struct fbwl_seat_keyboard_hooks hooks = seat_keyboard_hooks(server);
    fbwl_seat_add_input_device(server->seat, server->cursor, &server->keyboards, &server->has_pointer,
        &vkbd->keyboard.base, &hooks);
}

static void server_new_virtual_pointer(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_virtual_pointer);
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    wlr_log(WLR_INFO, "New virtual pointer");
    const struct fbwl_seat_keyboard_hooks hooks = seat_keyboard_hooks(server);
    fbwl_seat_add_input_device(server->seat, server->cursor, &server->keyboards, &server->has_pointer,
        &event->new_pointer->pointer.base, &hooks);
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
    return fbwl_view_title(view);
}

static const char *fbwl_wm_view_app_id(const struct fbwm_view *wm_view) {
    const struct fbwl_view *view = wm_view->userdata;
    return fbwl_view_app_id(view);
}

static const struct fbwm_view_ops fbwl_wm_view_ops = {
    .focus = fbwl_wm_view_focus,
    .is_mapped = fbwl_wm_view_is_mapped,
    .title = fbwl_wm_view_title,
    .app_id = fbwl_wm_view_app_id,
};

static void xdg_shell_apply_workspace_visibility(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    apply_workspace_visibility(server, why);
}

static void xdg_shell_toolbar_rebuild(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    server_toolbar_ui_rebuild(server);
}

static void xdg_shell_clear_keyboard_focus(void *userdata) {
    struct fbwl_server *server = userdata;
    clear_keyboard_focus(server);
}

static void xdg_shell_clear_focused_view_if_matches(void *userdata, struct fbwl_view *view) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->focused_view == view) {
        server->focused_view = NULL;
    }
}

static struct fbwl_xdg_shell_hooks xdg_shell_hooks(struct fbwl_server *server) {
    return (struct fbwl_xdg_shell_hooks){
        .userdata = server,
        .apply_workspace_visibility = xdg_shell_apply_workspace_visibility,
        .toolbar_rebuild = xdg_shell_toolbar_rebuild,
        .clear_keyboard_focus = xdg_shell_clear_keyboard_focus,
        .clear_focused_view_if_matches = xdg_shell_clear_focused_view_if_matches,
        .apps_rules_apply_pre_map = server_apps_rules_apply_pre_map,
        .apps_rules_apply_post_map = server_apps_rules_apply_post_map,
        .view_set_minimized = view_set_minimized,
    };
}

static struct fbwl_xwayland_hooks xwayland_hooks(struct fbwl_server *server) {
    return (struct fbwl_xwayland_hooks){
        .userdata = server,
        .apply_workspace_visibility = xdg_shell_apply_workspace_visibility,
        .toolbar_rebuild = xdg_shell_toolbar_rebuild,
        .clear_keyboard_focus = xdg_shell_clear_keyboard_focus,
        .clear_focused_view_if_matches = xdg_shell_clear_focused_view_if_matches,
        .apps_rules_apply_pre_map = server_apps_rules_apply_pre_map,
        .apps_rules_apply_post_map = server_apps_rules_apply_post_map,
        .view_set_minimized = view_set_minimized,
    };
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, map);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_map(view, &server->wm, server->output_layout, &server->outputs,
        server->cursor->x, server->cursor->y, server->apps_rules, server->apps_rule_count, &hooks);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, unmap);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_unmap(view, &server->wm, &hooks);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, commit);
    struct fbwl_server *server = view->server;
    fbwl_xdg_shell_handle_toplevel_commit(view, &server->decor_theme);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_maximize);
    struct fbwl_server *server = view->server;
    fbwl_xdg_shell_handle_toplevel_request_maximize(view, server->output_layout, &server->outputs);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_fullscreen);
    struct fbwl_server *server = view->server;
    fbwl_xdg_shell_handle_toplevel_request_fullscreen(view, server->output_layout, &server->outputs,
        server->layer_normal, server->layer_fullscreen);
}

static void xdg_toplevel_request_minimize(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, request_minimize);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_request_minimize(view, &hooks);
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, set_title);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_set_title(view, &server->decor_theme, &hooks);
}

static void xdg_toplevel_set_app_id(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, set_app_id);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_set_app_id(view, &hooks);
}

static void foreign_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    const struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    struct fbwl_view *view = wl_container_of(listener, view, foreign_request_maximize);
    fbwl_view_set_maximized(view, event->maximized, view->server->output_layout, &view->server->outputs);
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
    fbwl_view_set_fullscreen(view, event->fullscreen, view->server->output_layout, &view->server->outputs,
        view->server->layer_normal, view->server->layer_fullscreen, event->output);
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
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_map(view, &server->wm, server->output_layout, &server->outputs,
        server->cursor->x, server->cursor->y, server->apps_rules, server->apps_rule_count, &hooks);
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, unmap);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_unmap(view, &server->wm, &hooks);
}

static void xwayland_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, commit);
    struct fbwl_server *server = view->server;
    fbwl_xwayland_handle_surface_commit(view, server != NULL ? &server->decor_theme : NULL);
}

static void xwayland_surface_associate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_associate);
    struct fbwl_server *server = view->server;
    if (server == NULL) {
        return;
    }

    struct wlr_scene_tree *parent =
        server->layer_normal != NULL ? server->layer_normal : &server->scene->tree;
    fbwl_xwayland_handle_surface_associate(view, parent, &server->decor_theme, server->output_layout,
        xwayland_surface_map, xwayland_surface_unmap, xwayland_surface_commit);
}

static void xwayland_surface_dissociate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_dissociate);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_dissociate(view, &server->wm, &hooks);
}

static void xwayland_surface_request_configure(struct wl_listener *listener, void *data) {
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    struct fbwl_server *server = view->server;
    fbwl_xwayland_handle_surface_request_configure(view, event,
        server != NULL ? &server->decor_theme : NULL,
        server != NULL ? server->output_layout : NULL);
}

static void xwayland_surface_request_activate(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_activate);
    struct fbwl_server *server = view->server;
    if (server == NULL) {
        return;
    }
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_request_activate(view, &server->wm, &hooks);
}

static void xwayland_surface_request_close(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_request_close);
    fbwl_xwayland_handle_surface_request_close(view);
}

static void xwayland_surface_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_set_title);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_set_title(view, server != NULL ? &server->decor_theme : NULL, &hooks);
}

static void xwayland_surface_set_class(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, xwayland_set_class);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_set_class(view, &hooks);
}

static void xwayland_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, destroy);
    struct fbwl_server *server = view->server;
    struct fbwl_xwayland_hooks hooks = xwayland_hooks(server);
    fbwl_xwayland_handle_surface_destroy(view, &server->wm, &hooks);
}

static void server_xwayland_ready(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_server *server = wl_container_of(listener, server, xwayland_ready);
    const char *display_name = server->xwayland != NULL ? server->xwayland->display_name : NULL;
    fbwl_xwayland_handle_ready(display_name);
}

static void server_xwayland_new_surface(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, xwayland_new_surface);
    struct wlr_xwayland_surface *xsurface = data;
    const struct fbwl_view_foreign_toplevel_handlers *foreign_handlers = NULL;
    if (server != NULL && server->foreign_toplevel_mgr != NULL) {
        static const struct fbwl_view_foreign_toplevel_handlers handlers = {
            .request_maximize = foreign_toplevel_request_maximize,
            .request_minimize = foreign_toplevel_request_minimize,
            .request_activate = foreign_toplevel_request_activate,
            .request_fullscreen = foreign_toplevel_request_fullscreen,
            .request_close = foreign_toplevel_request_close,
        };
        foreign_handlers = &handlers;
    }

    fbwl_xwayland_handle_new_surface(server, xsurface, &fbwl_wm_view_ops,
        xwayland_surface_destroy,
        xwayland_surface_associate,
        xwayland_surface_dissociate,
        xwayland_surface_request_configure,
        xwayland_surface_request_activate,
        xwayland_surface_request_close,
        xwayland_surface_set_title,
        xwayland_surface_set_class,
        server != NULL ? server->foreign_toplevel_mgr : NULL,
        foreign_handlers);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_view *view = wl_container_of(listener, view, destroy);
    struct fbwl_server *server = view->server;
    struct fbwl_xdg_shell_hooks hooks = xdg_shell_hooks(server);
    fbwl_xdg_shell_handle_toplevel_destroy(view, &server->wm, &hooks);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    const struct fbwl_view_foreign_toplevel_handlers *foreign_handlers = NULL;
    if (server->foreign_toplevel_mgr != NULL) {
        static const struct fbwl_view_foreign_toplevel_handlers handlers = {
            .request_maximize = foreign_toplevel_request_maximize,
            .request_minimize = foreign_toplevel_request_minimize,
            .request_activate = foreign_toplevel_request_activate,
            .request_fullscreen = foreign_toplevel_request_fullscreen,
            .request_close = foreign_toplevel_request_close,
        };
        foreign_handlers = &handlers;
    }

    fbwl_xdg_shell_handle_new_toplevel(server, xdg_toplevel,
        server->layer_normal != NULL ? server->layer_normal : &server->scene->tree,
        &server->decor_theme, &fbwl_wm_view_ops,
        xdg_toplevel_map, xdg_toplevel_unmap, xdg_toplevel_commit, xdg_toplevel_destroy,
        xdg_toplevel_request_maximize, xdg_toplevel_request_fullscreen, xdg_toplevel_request_minimize,
        xdg_toplevel_set_title, xdg_toplevel_set_app_id,
        server->foreign_toplevel_mgr, foreign_handlers);
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

    const struct fbwl_session_lock_hooks sl_hooks = session_lock_hooks(&server);
    if (!fbwl_session_lock_init(&server.session_lock, server.wl_display,
                &server.scene, &server.layer_overlay, &server.output_layout, &server.seat, &server.outputs, &sl_hooks)) {
        return 1;
    }

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
    fbwl_keybindings_add_defaults(&server.keybindings, &server.keybinding_count, server.terminal_cmd);
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
    server.new_xdg_popup.notify = fbwl_xdg_shell_handle_new_popup;
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

    if (!fbwl_text_input_init(&server.text_input, server.wl_display, server.seat)) {
        return 1;
    }

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
        fbwl_spawn(server.startup_cmd);
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
    fbwl_session_lock_finish(&server.session_lock);
    fbwl_cleanup_listener(&server.output_manager_apply);
    fbwl_cleanup_listener(&server.output_manager_test);
    fbwl_cleanup_listener(&server.output_power_set_mode);
    fbwl_text_input_finish(&server.text_input);

    fbwl_cleanup_listener(&server.new_output);

    if (server.xwayland != NULL) {
        wlr_xwayland_destroy(server.xwayland);
        server.xwayland = NULL;
    }

    server_cmd_dialog_ui_close(&server, "shutdown");
    server_osd_ui_destroy(&server);
    server_menu_free(&server);
    fbwl_ui_toolbar_destroy(&server.toolbar_ui);

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
    fbwl_keybindings_free(&server.keybindings, &server.keybinding_count);
    if (server.protocol_logger != NULL) {
        wl_protocol_logger_destroy(server.protocol_logger);
        server.protocol_logger = NULL;
    }
    wl_display_destroy(server.wl_display);
    return 0;
}
