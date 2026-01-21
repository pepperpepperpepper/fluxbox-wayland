#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_keys_parse.h"
#include "wayland/fbwl_scene_layers.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_style_parse.h"
#include "wayland/fbwl_util.h"

static int handle_signal(int signo, void *data) {
    struct fbwl_server *server = data;
    wlr_log(WLR_INFO, "Signal %d received, terminating", signo);
    wl_display_terminate(server->wl_display);
    return 0;
}

static bool server_keybindings_add_from_keys_file(void *userdata, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd) {
    struct fbwl_server *server = userdata;
    return fbwl_keybindings_add(&server->keybindings, &server->keybinding_count, sym, modifiers, action, arg, cmd);
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

bool fbwl_server_bootstrap(struct fbwl_server *server, const struct fbwl_server_bootstrap_options *opts) {
    if (server == NULL || opts == NULL) {
        return false;
    }

    decor_theme_set_defaults(&server->decor_theme);
    if (opts->background_color != NULL) {
        memcpy(server->background_color, opts->background_color, sizeof(server->background_color));
    }
    server->startup_cmd = opts->startup_cmd;
    server->terminal_cmd = opts->terminal_cmd;
    server->has_pointer = false;
    fbwl_ipc_init(&server->ipc);
#ifdef HAVE_SYSTEMD
    wl_list_init(&server->sni.items);
#endif

    server->wl_display = wl_display_create();
    if (server->wl_display == NULL) {
        wlr_log(WLR_ERROR, "failed to create wl_display");
        return false;
    }

    if (opts->log_protocol) {
        server->protocol_logger = wl_display_add_protocol_logger(server->wl_display, fbwl_wayland_protocol_logger, NULL);
        if (server->protocol_logger == NULL) {
            wlr_log(WLR_ERROR, "failed to add Wayland protocol logger");
            return false;
        }
        wlr_log(WLR_INFO, "Wayland protocol logging enabled");
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    wl_event_loop_add_signal(loop, SIGINT, handle_signal, server);
    wl_event_loop_add_signal(loop, SIGTERM, handle_signal, server);

    server->osd_ui.enabled = true;
    server->osd_ui.visible = false;
    server->osd_ui.last_workspace = 0;
    server->osd_ui.hide_timer = wl_event_loop_add_timer(loop, server_osd_hide_timer, server);

    server->backend = wlr_backend_autocreate(loop, NULL);
    if (server->backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return false;
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (server->renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return false;
    }
    wlr_renderer_init_wl_display(server->renderer, server->wl_display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (server->allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return false;
    }

    server->compositor = wlr_compositor_create(server->wl_display, 5, server->renderer);
    if (server->compositor == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_compositor");
        return false;
    }
    server->presentation = wlr_presentation_create(server->wl_display, server->backend, 1);
    if (server->presentation == NULL) {
        wlr_log(WLR_ERROR, "failed to create presentation-time protocol");
        return false;
    }
    wlr_subcompositor_create(server->wl_display);
    wlr_data_device_manager_create(server->wl_display);
    server->single_pixel_buffer_mgr = wlr_single_pixel_buffer_manager_v1_create(server->wl_display);
    if (server->single_pixel_buffer_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create single-pixel-buffer manager");
        return false;
    }

    server->data_control_mgr = wlr_data_control_manager_v1_create(server->wl_display);
    if (server->data_control_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr-data-control manager");
        return false;
    }

    server->ext_data_control_mgr = wlr_ext_data_control_manager_v1_create(server->wl_display, 1);
    if (server->ext_data_control_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create ext-data-control manager");
        return false;
    }

    server->primary_selection_mgr = wlr_primary_selection_v1_device_manager_create(server->wl_display);
    if (server->primary_selection_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create primary selection manager");
        return false;
    }

    if (!fbwl_viewporter_init(&server->viewporter, server->wl_display)) {
        return false;
    }

    if (!fbwl_fractional_scale_init(&server->fractional_scale, server->wl_display)) {
        return false;
    }

    if (!fbwl_xdg_activation_init(&server->xdg_activation, server->wl_display, &server->wm, view_set_minimized)) {
        return false;
    }

    if (!fbwl_xdg_decoration_init(&server->xdg_decoration, server->wl_display, &server->decor_theme)) {
        return false;
    }

    if (!fbwl_idle_init(&server->idle, server->wl_display, &server->seat)) {
        return false;
    }

    const struct fbwl_session_lock_hooks sl_hooks = session_lock_hooks(server);
    if (!fbwl_session_lock_init(&server->session_lock, server->wl_display,
                &server->scene, &server->layer_overlay, &server->output_layout, &server->seat, &server->outputs, &sl_hooks)) {
        return false;
    }

    server->foreign_toplevel_mgr = wlr_foreign_toplevel_manager_v1_create(server->wl_display);
    const struct fbwl_cursor_menu_hooks pointer_constraints_hooks = server_cursor_menu_hooks(server);
    if (!fbwl_pointer_constraints_init(&server->pointer_constraints, server->wl_display,
                &server->cursor, &server->cursor_mgr, &server->scene, &server->seat, &pointer_constraints_hooks)) {
        return false;
    }
    if (!fbwl_screencopy_init(&server->screencopy, server->wl_display)) {
        return false;
    }

    if (!fbwl_export_dmabuf_init(&server->export_dmabuf, server->wl_display)) {
        return false;
    }

#if WLR_VERSION_NUM >= ((0 << 16) | (19 << 8) | 0)
    if (!fbwl_ext_image_capture_init(&server->ext_image_capture, server->wl_display)) {
        return false;
    }
#endif

    if (!fbwl_server_outputs_init(server)) {
        return false;
    }

    fbwm_core_init(&server->wm);
    int workspaces = opts->workspaces;
    const bool workspaces_set = opts->workspaces_set;
    const char *keys_file = opts->keys_file;
    const char *apps_file = opts->apps_file;
    const char *style_file = opts->style_file;
    const char *menu_file = opts->menu_file;
    const char *config_dir = opts->config_dir;

    char *keys_file_owned = NULL;
    char *apps_file_owned = NULL;
    char *style_file_owned = NULL;
    char *menu_file_owned = NULL;

    if (style_file != NULL) {
        style_file_owned = fbwl_resolve_config_path(NULL, style_file);
        if (style_file_owned != NULL) {
            style_file = style_file_owned;
        }
    }
    if (menu_file != NULL) {
        menu_file_owned = fbwl_resolve_config_path(NULL, menu_file);
        if (menu_file_owned != NULL) {
            menu_file = menu_file_owned;
        }
    }
    if (config_dir != NULL) {
        struct fbwl_init_settings init = {0};
        (void)fbwl_init_load_file(config_dir, &init);

        if (!workspaces_set && init.set_workspaces) {
            workspaces = init.workspaces;
        }

        if (keys_file == NULL) {
            if (init.keys_file != NULL) {
                keys_file_owned = init.keys_file;
                init.keys_file = NULL;
            } else {
                keys_file_owned = fbwl_path_join(config_dir, "keys");
                if (keys_file_owned != NULL && !fbwl_file_exists(keys_file_owned)) {
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
                apps_file_owned = fbwl_path_join(config_dir, "apps");
                if (apps_file_owned != NULL && !fbwl_file_exists(apps_file_owned)) {
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
                menu_file_owned = fbwl_path_join(config_dir, "menu");
                if (menu_file_owned != NULL && !fbwl_file_exists(menu_file_owned)) {
                    free(menu_file_owned);
                    menu_file_owned = NULL;
                }
            }
            menu_file = menu_file_owned;
        }

        fbwl_init_settings_free(&init);
    }
    fbwm_core_set_workspace_count(&server->wm, workspaces);
    fbwl_keybindings_add_defaults(&server->keybindings, &server->keybinding_count, server->terminal_cmd);
    if (keys_file != NULL) {
        (void)fbwl_keys_parse_file(keys_file, server_keybindings_add_from_keys_file, server, NULL);
    }
    if (apps_file != NULL) {
        (void)fbwl_apps_rules_load_file(&server->apps_rules, &server->apps_rule_count, apps_file);
    }

    if (style_file != NULL) {
        (void)fbwl_style_load_file(&server->decor_theme, style_file);
    }

    free(keys_file_owned);
    free(apps_file_owned);
    free(style_file_owned);

    if (menu_file != NULL) {
        free(server->menu_file);
        server->menu_file = strdup(menu_file);
        if (!server_menu_load_file(server, menu_file)) {
            wlr_log(WLR_ERROR, "Menu: falling back to default menu");
            server_menu_create_default(server);
        }
    } else {
        server_menu_create_default(server);
    }
    free(menu_file_owned);

    server_menu_create_window(server);
    server_toolbar_ui_rebuild(server);

    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
    server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
    server->new_xdg_popup.notify = fbwl_xdg_shell_handle_new_popup;
    wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

    server->layer_shell = wlr_layer_shell_v1_create(server->wl_display, 4);
    server->new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    server->cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(server->wl_display, 1);
    if (server->cursor_shape_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create cursor-shape manager");
        return false;
    }
    server->cursor_shape_request_set_shape.notify = cursor_shape_request_set_shape;
    wl_signal_add(&server->cursor_shape_mgr->events.request_set_shape, &server->cursor_shape_request_set_shape);

    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);
    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    wl_list_init(&server->keyboards);
    server->new_input.notify = server_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
    server->seat = wlr_seat_create(server->wl_display, "seat0");

    if (!fbwl_shortcuts_inhibit_init(&server->shortcuts_inhibit, server->wl_display, &server->seat)) {
        return false;
    }

    server->request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
    server->request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);
    server->request_set_primary_selection.notify = seat_request_set_primary_selection;
    wl_signal_add(&server->seat->events.request_set_primary_selection, &server->request_set_primary_selection);
    server->request_start_drag.notify = seat_request_start_drag;
    wl_signal_add(&server->seat->events.request_start_drag, &server->request_start_drag);

    if (!fbwl_text_input_init(&server->text_input, server->wl_display, server->seat)) {
        return false;
    }

    if (opts->enable_xwayland) {
        server->xwayland = wlr_xwayland_create(server->wl_display, server->compositor, false);
        if (server->xwayland == NULL) {
            wlr_log(WLR_ERROR, "XWayland: failed to create");
        } else {
            wlr_xwayland_set_seat(server->xwayland, server->seat);
            server->xwayland_ready.notify = server_xwayland_ready;
            wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);
            server->xwayland_new_surface.notify = server_xwayland_new_surface;
            wl_signal_add(&server->xwayland->events.new_surface, &server->xwayland_new_surface);
        }
    } else {
        wlr_log(WLR_INFO, "XWayland: disabled");
    }

    server->virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(server->wl_display);
    server->virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(server->wl_display);
    server->new_virtual_keyboard.notify = server_new_virtual_keyboard;
    wl_signal_add(&server->virtual_keyboard_mgr->events.new_virtual_keyboard,
        &server->new_virtual_keyboard);
    server->new_virtual_pointer.notify = server_new_virtual_pointer;
    wl_signal_add(&server->virtual_pointer_mgr->events.new_virtual_pointer,
        &server->new_virtual_pointer);

    const char *socket = NULL;
    if (opts->socket_name != NULL) {
        if (wl_display_add_socket(server->wl_display, opts->socket_name) != 0) {
            wlr_log(WLR_ERROR, "failed to add socket '%s'", opts->socket_name);
            return false;
        }
        socket = opts->socket_name;
    } else {
        socket = wl_display_add_socket_auto(server->wl_display);
        if (socket == NULL) {
            wlr_log(WLR_ERROR, "failed to create Wayland socket");
            return false;
        }
    }

    if (!wlr_backend_start(server->backend)) {
        wlr_log(WLR_ERROR, "failed to start backend");
        return false;
    }

    if (fbwl_ipc_start(&server->ipc, loop, socket, opts->ipc_socket_path, server_ipc_command, server)) {
        setenv("FBWL_IPC_SOCKET", fbwl_ipc_socket_path(&server->ipc), true);
    }

#ifdef HAVE_SYSTEMD
    (void)fbwl_sni_start(&server->sni, loop, server_sni_on_change, server);
#endif

    setenv("WAYLAND_DISPLAY", socket, true);
    if (server->startup_cmd != NULL) {
        fbwl_spawn(server->startup_cmd);
    }

    wlr_log(WLR_INFO, "Running fluxbox-wayland on WAYLAND_DISPLAY=%s", socket);
    return true;
}
