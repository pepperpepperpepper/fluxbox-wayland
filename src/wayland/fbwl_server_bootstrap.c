#include <ctype.h>
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
#include "wayland/fbwl_string_list.h"
#include "wayland/fbwl_ui_menu_icon.h"
#include "wayland/fbwl_style_parse.h"
#include "wayland/fbwl_ui_menu_search.h"
#include "wayland/fbwl_util.h"

static int handle_signal(int signo, void *data) {
    struct fbwl_server *server = data;
    wlr_log(WLR_INFO, "Signal %d received, terminating", signo);
    wl_display_terminate(server->wl_display);
    return 0;
}

static bool server_keybindings_add_from_keys_file(void *userdata, enum fbwl_keybinding_key_kind key_kind,
        uint32_t keycode, xkb_keysym_t sym, uint32_t modifiers, enum fbwl_keybinding_action action, int arg,
        const char *cmd, const char *mode) {
    struct fbwl_server *server = userdata;
    if (key_kind == FBWL_KEYBIND_PLACEHOLDER) {
        return fbwl_keybindings_add_placeholder(&server->keybindings, &server->keybinding_count, modifiers,
            action, arg, cmd, mode);
    }
    if (key_kind == FBWL_KEYBIND_CHANGE_WORKSPACE) {
        return fbwl_keybindings_add_change_workspace(&server->keybindings, &server->keybinding_count,
            action, arg, cmd, mode);
    }
    if (key_kind == FBWL_KEYBIND_KEYCODE) {
        return fbwl_keybindings_add_keycode(&server->keybindings, &server->keybinding_count, keycode, modifiers,
            action, arg, cmd, mode);
    }
    return fbwl_keybindings_add(&server->keybindings, &server->keybinding_count, sym, modifiers, action, arg, cmd, mode);
}

static bool cmd_contains_startmoving(const char *s) {
    if (s == NULL) {
        return false;
    }
    const char *needle = "startmoving";
    for (const char *p = s; *p != '\0'; p++) {
        const char *h = p;
        const char *n = needle;
        while (*h != '\0' && *n != '\0' && tolower((unsigned char)*h) == *n) {
            h++;
            n++;
        }
        if (*n == '\0') {
            return true;
        }
    }
    return false;
}

static bool server_mousebindings_add_from_keys_file(void *userdata, enum fbwl_mousebinding_context context,
        enum fbwl_mousebinding_event_kind event_kind, int button, uint32_t modifiers, bool is_double,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
    struct fbwl_server *server = userdata;
    if (server != NULL && server->ignore_border && context == FBWL_MOUSEBIND_WINDOW_BORDER) {
        if (action == FBWL_KEYBIND_START_MOVING || (action == FBWL_KEYBIND_MACRO && cmd_contains_startmoving(cmd))) {
            wlr_log(WLR_INFO, "Keys: ignoring StartMoving on window border (session.ignoreBorder=true)");
            return false;
        }
    }
    return fbwl_mousebindings_add(&server->mousebindings, &server->mousebinding_count, context, event_kind, button,
        modifiers, is_double, action, arg, cmd, mode);
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
    server->wallpaper_mode = FBWL_WALLPAPER_MODE_STRETCH;
    server->style_background_first = true;
    server->startup_cmd = opts->startup_cmd;
    server->terminal_cmd = opts->terminal_cmd;
    server->has_pointer = false;
    server->ignore_border = false;
    server->force_pseudo_transparency = false;
    server->double_click_interval_ms = 250;
    server->opaque_move = true;
    server->opaque_resize = false;
    server->opaque_resize_delay_ms = 50;
    server->full_maximization = false;
    server->max_ignore_increment = true;
    server->max_disable_move = false;
    server->max_disable_resize = false;
    server->workspace_warping = true;
    server->workspace_warping_horizontal = true;
    server->workspace_warping_vertical = true;
    server->workspace_warping_horizontal_offset = 1;
    server->workspace_warping_vertical_offset = 1;
    server->show_window_position = false;
    server->edge_snap_threshold_px = 10;
    server->edge_resize_snap_threshold_px = 0;
    server->colors_per_channel = 4;
    server->cache_life_minutes = 5;
    server->cache_max_kb = 200;
    server->config_version = 0;
    free(server->group_file);
    server->group_file = NULL;
    server->last_button_time_msec = 0;
    server->last_button = 0;
    free(server->key_mode);
    server->key_mode = NULL;
    server->key_mode_return_active = false;
    server->key_mode_return_kind = FBWL_KEYBIND_KEYSYM;
    server->key_mode_return_keycode = 0;
    server->key_mode_return_sym = XKB_KEY_NoSymbol;
    server->key_mode_return_modifiers = 0;
    fbwl_ipc_init(&server->ipc);
#ifdef HAVE_SYSTEMD
    wl_list_init(&server->sni.items);
#endif
    wl_list_init(&server->tab_groups);
    server->focus = (struct fbwl_focus_config){
        .model = FBWL_FOCUS_MODEL_CLICK_TO_FOCUS,
        .auto_raise = true,
        .auto_raise_delay_ms = 250,
        .click_raises = true,
        .focus_new_windows = true,
        .no_focus_while_typing_delay_ms = 0,
        .focus_same_head = false,
        .demands_attention_timeout_ms = 500,
        .allow_remote_actions = true,
    };
    server->titlebar_left_len = 1;
    server->titlebar_left[0] = FBWL_DECOR_HIT_BTN_STICK;
    server->titlebar_right_len = 4;
    server->titlebar_right[0] = FBWL_DECOR_HIT_BTN_SHADE;
    server->titlebar_right[1] = FBWL_DECOR_HIT_BTN_MIN;
    server->titlebar_right[2] = FBWL_DECOR_HIT_BTN_MAX;
    server->titlebar_right[3] = FBWL_DECOR_HIT_BTN_CLOSE;
    server->focus_reason = FBWL_FOCUS_REASON_NONE;
    server->auto_raise_timer = NULL;
    server->auto_raise_pending_view = NULL;
    server->window_alpha_defaults_configured = false;
    server->window_alpha_default_focused = 255;
    server->window_alpha_default_unfocused = 255;
    server->default_deco_enabled = true;

    server->toolbar_ui.enabled = true;
    server->toolbar_ui.placement = FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER;
    server->toolbar_ui.on_head = 0;
    server->toolbar_ui.layer_num = 4;
    server->toolbar_ui.width_percent = 100;
    server->toolbar_ui.height_override = 0;
    server->toolbar_ui.tools = fbwl_toolbar_tools_default();
    snprintf(server->toolbar_ui.strftime_format, sizeof(server->toolbar_ui.strftime_format), "%s", "%H:%M");
    server->toolbar_ui.auto_hide = false;
    server->toolbar_ui.auto_raise = false;
    server->toolbar_ui.max_over = false;
    server->toolbar_ui.hidden = false;
    server->toolbar_ui.alpha = 255;
    snprintf(server->toolbar_ui.iconbar_mode, sizeof(server->toolbar_ui.iconbar_mode), "%s", "{static groups} (workspace)");
    server->toolbar_ui.iconbar_alignment = FBWL_ICONBAR_ALIGN_RELATIVE;
    server->toolbar_ui.iconbar_icon_width_px = 128;
    server->toolbar_ui.iconbar_icon_text_padding_px = 10;
    server->toolbar_ui.iconbar_use_pixmap = true;
    snprintf(server->toolbar_ui.iconbar_iconified_prefix, sizeof(server->toolbar_ui.iconbar_iconified_prefix), "%s", "( ");
    snprintf(server->toolbar_ui.iconbar_iconified_suffix, sizeof(server->toolbar_ui.iconbar_iconified_suffix), "%s", " )");
    // Default tool ordering (parity with Fluxbox/X11 toolbar.tools ordering semantics).
    (void)server_toolbar_ui_load_button_tools(server, NULL, 0);
    server->toolbar_ui.systray_pin_left = NULL;
    server->toolbar_ui.systray_pin_left_len = 0;
    server->toolbar_ui.systray_pin_right = NULL;
    server->toolbar_ui.systray_pin_right_len = 0;
    server->toolbar_ui.hovered = false;
    server->toolbar_ui.auto_pending = 0;

    fbwl_ui_slit_init(&server->slit_ui);

    server->menu_ui.env = (struct fbwl_ui_menu_env){0};
    server->menu_ui.alpha = 255;
    server->menu_ui.menu_delay_ms = 200;
    server->menu_ui.search_mode = FBWL_MENU_SEARCH_ITEMSTART;
    server->menu_ui.submenu_timer = NULL;
    server->menu_ui.hovered_idx = -1;
    server->menu_ui.submenu_pending_idx = 0;

    fbwl_tabs_init_defaults(&server->tabs);

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

    server->auto_raise_timer = wl_event_loop_add_timer(loop, server_auto_raise_timer, server);

    server->osd_ui.enabled = true;
    server->osd_ui.visible = false;
    server->osd_ui.last_workspace = 0;
    server->osd_ui.hide_timer = wl_event_loop_add_timer(loop, server_osd_hide_timer, server);

    server->move_osd_ui.enabled = true;
    server->move_osd_ui.visible = false;
    server->move_osd_ui.last_workspace = 0;
    server->move_osd_ui.hide_timer = NULL;

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

    if (!fbwl_xdg_activation_init(&server->xdg_activation, server->wl_display, server, &server->wm, view_set_minimized)) {
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
    fbwm_core_set_head_count(&server->wm, 1);
    fbwm_core_set_refocus_filter(&server->wm, server_refocus_candidate_allowed, server);
    int workspaces = opts->workspaces;
    const bool workspaces_set = opts->workspaces_set;
    const char *keys_file = opts->keys_file;
    const char *apps_file = opts->apps_file;
    const char *style_file = opts->style_file;
    const char *menu_file = opts->menu_file;
    const char *window_menu_file = NULL;
    const char *slitlist_file = NULL;
    const char *config_dir = opts->config_dir;

    server->workspaces_override = workspaces_set;
    server->keys_file_override = opts->keys_file != NULL;
    server->apps_file_override = opts->apps_file != NULL;
    server->style_file_override = opts->style_file != NULL;
    server->menu_file_override = opts->menu_file != NULL;

    free(server->config_dir);
    server->config_dir = config_dir != NULL ? fbwl_resolve_config_path(NULL, config_dir) : NULL;
    if (server->config_dir == NULL && config_dir != NULL) {
        server->config_dir = strdup(config_dir);
    }
    if (server->config_dir != NULL) {
        config_dir = server->config_dir;
    }

    char *keys_file_owned = NULL;
    char *apps_file_owned = NULL;
    char *style_file_owned = NULL;
    char *style_overlay_file_owned = NULL;
    char *menu_file_owned = NULL;
    char *window_menu_file_owned = NULL;
    char *slitlist_file_owned = NULL;

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
        struct fbwl_resource_db init = {0};
        (void)fbwl_resource_db_load_init(&init, config_dir);

        bool bool_val = false;
        int int_val = 0;

        fbwl_server_load_screen_configs(server, &init);

        if (fbwl_resource_db_get_bool(&init, "session.ignoreBorder", &bool_val)) {
            server->ignore_border = bool_val;
        }
        if (fbwl_resource_db_get_bool(&init, "session.forcePseudoTransparency", &bool_val)) {
            server->force_pseudo_transparency = bool_val;
        }
        if (fbwl_resource_db_get_int(&init, "session.colorsPerChannel", &int_val) && int_val > 0 && int_val < 64) {
            server->colors_per_channel = int_val;
        }
        if (fbwl_resource_db_get_int(&init, "session.cacheLife", &int_val) && int_val >= 0) {
            server->cache_life_minutes = int_val;
        }
        if (fbwl_resource_db_get_int(&init, "session.cacheMax", &int_val) && int_val >= 0) {
            server->cache_max_kb = int_val;
        }
        if (fbwl_resource_db_get_int(&init, "session.configVersion", &int_val)) {
            server->config_version = int_val;
        }
        const char *menu_search = fbwl_resource_db_get(&init, "session.menuSearch");
        if (menu_search != NULL) {
            server->menu_ui.search_mode = fbwl_menu_search_mode_parse(menu_search);
        }
        char *group_file = fbwl_resource_db_resolve_path(&init, config_dir, "session.groupFile");
        if (group_file != NULL) {
            free(server->group_file);
            server->group_file = group_file;
        }
        if (fbwl_resource_db_get_int(&init, "session.doubleClickInterval", &int_val) &&
                int_val >= 0 && int_val < 60000) {
            server->double_click_interval_ms = int_val;
        }
        const struct fbwl_screen_config *s0 = fbwl_server_screen_config(server, 0);
        if (s0 != NULL) {
            server->focus = s0->focus;
            server->edge_snap_threshold_px = s0->edge_snap_threshold_px;
            server->edge_resize_snap_threshold_px = s0->edge_resize_snap_threshold_px;
            server->opaque_move = s0->opaque_move;
            server->opaque_resize = s0->opaque_resize;
            server->opaque_resize_delay_ms = s0->opaque_resize_delay_ms;
            server->full_maximization = s0->full_maximization;
            server->max_ignore_increment = s0->max_ignore_increment;
            server->max_disable_move = s0->max_disable_move;
            server->max_disable_resize = s0->max_disable_resize;
            server->workspace_warping = s0->workspace_warping;
            server->workspace_warping_horizontal = s0->workspace_warping_horizontal;
            server->workspace_warping_vertical = s0->workspace_warping_vertical;
            server->workspace_warping_horizontal_offset = s0->workspace_warping_horizontal_offset;
            server->workspace_warping_vertical_offset = s0->workspace_warping_vertical_offset;
            server->show_window_position = s0->show_window_position;
            server->menu_ui.menu_delay_ms = s0->menu.delay_ms;
            server->menu_ui.alpha = s0->menu.alpha;
            server->tabs = s0->tabs;
            server->toolbar_ui.on_head = s0->toolbar.on_head;
            server->slit_ui.on_head = s0->slit.on_head;
            fbwm_core_set_window_placement(&server->wm, s0->placement_strategy);
            fbwm_core_set_row_placement_direction(&server->wm, s0->placement_row_dir);
            fbwm_core_set_col_placement_direction(&server->wm, s0->placement_col_dir);
        }

        const size_t toolbar_screen =
            (size_t)(server->toolbar_ui.on_head >= 0 ? server->toolbar_ui.on_head : 0);
        const struct fbwl_screen_config *tb_cfg = fbwl_server_screen_config(server, toolbar_screen);
        if (tb_cfg != NULL) {
            server->toolbar_ui.enabled = tb_cfg->toolbar.enabled;
            server->toolbar_ui.placement = tb_cfg->toolbar.placement;
            server->toolbar_ui.layer_num = tb_cfg->toolbar.layer_num;
            server->toolbar_ui.width_percent = tb_cfg->toolbar.width_percent;
            server->toolbar_ui.height_override = tb_cfg->toolbar.height_override;
            server->toolbar_ui.tools = tb_cfg->toolbar.tools;
            server->toolbar_ui.auto_hide = tb_cfg->toolbar.auto_hide;
            server->toolbar_ui.auto_raise = tb_cfg->toolbar.auto_raise;
            server->toolbar_ui.max_over = tb_cfg->toolbar.max_over;
            server->toolbar_ui.alpha = tb_cfg->toolbar.alpha;
            strncpy(server->toolbar_ui.strftime_format, tb_cfg->toolbar.strftime_format, sizeof(server->toolbar_ui.strftime_format));
            server->toolbar_ui.strftime_format[sizeof(server->toolbar_ui.strftime_format) - 1] = '\0';

            strncpy(server->toolbar_ui.iconbar_mode, tb_cfg->iconbar.mode, sizeof(server->toolbar_ui.iconbar_mode));
            server->toolbar_ui.iconbar_mode[sizeof(server->toolbar_ui.iconbar_mode) - 1] = '\0';
            server->toolbar_ui.iconbar_alignment = tb_cfg->iconbar.alignment;
            server->toolbar_ui.iconbar_icon_width_px = tb_cfg->iconbar.icon_width_px;
            server->toolbar_ui.iconbar_icon_text_padding_px = tb_cfg->iconbar.icon_text_padding_px;
            server->toolbar_ui.iconbar_use_pixmap = tb_cfg->iconbar.use_pixmap;
            strncpy(server->toolbar_ui.iconbar_iconified_prefix, tb_cfg->iconbar.iconified_prefix,
                sizeof(server->toolbar_ui.iconbar_iconified_prefix));
            server->toolbar_ui.iconbar_iconified_prefix[sizeof(server->toolbar_ui.iconbar_iconified_prefix) - 1] = '\0';
            strncpy(server->toolbar_ui.iconbar_iconified_suffix, tb_cfg->iconbar.iconified_suffix,
                sizeof(server->toolbar_ui.iconbar_iconified_suffix));
            server->toolbar_ui.iconbar_iconified_suffix[sizeof(server->toolbar_ui.iconbar_iconified_suffix) - 1] = '\0';
        }
        if (server->toolbar_ui.tools == 0) {
            server->toolbar_ui.tools = fbwl_toolbar_tools_default();
        }
        (void)server_toolbar_ui_load_button_tools(server, &init, toolbar_screen);
        server->toolbar_ui.hidden = false;

        const size_t slit_screen =
            (size_t)(server->slit_ui.on_head >= 0 ? server->slit_ui.on_head : 0);
        const struct fbwl_screen_config *sl_cfg = fbwl_server_screen_config(server, slit_screen);
        if (sl_cfg != NULL) {
            server->slit_ui.placement = sl_cfg->slit.placement;
            server->slit_ui.layer_num = sl_cfg->slit.layer_num;
            server->slit_ui.auto_hide = sl_cfg->slit.auto_hide;
            server->slit_ui.auto_raise = sl_cfg->slit.auto_raise;
            server->slit_ui.max_over = sl_cfg->slit.max_over;
            server->slit_ui.accept_kde_dockapps = sl_cfg->slit.accept_kde_dockapps;
            server->slit_ui.alpha = sl_cfg->slit.alpha;
            server->slit_ui.direction = sl_cfg->slit.direction;
        }
        server->slit_ui.hidden = false;

        server->window_alpha_defaults_configured = false;
        server->window_alpha_default_focused = 255;
        server->window_alpha_default_unfocused = 255;
        if (fbwl_resource_db_get_int(&init, "session.screen0.window.focus.alpha", &int_val)) {
            if (int_val < 0) {
                int_val = 0;
            } else if (int_val > 255) {
                int_val = 255;
            }
            server->window_alpha_default_focused = (uint8_t)int_val;
            server->window_alpha_defaults_configured = true;
        }
        if (fbwl_resource_db_get_int(&init, "session.screen0.window.unfocus.alpha", &int_val)) {
            if (int_val < 0) {
                int_val = 0;
            } else if (int_val > 255) {
                int_val = 255;
            }
            server->window_alpha_default_unfocused = (uint8_t)int_val;
            server->window_alpha_defaults_configured = true;
        }

        const char *workspace_names = fbwl_resource_db_get(&init, "session.screen0.workspaceNames");
        if (workspace_names != NULL) {
            fbwl_apply_workspace_names_from_init(&server->wm, workspace_names);
        }

        const char *pin_left = fbwl_resource_db_get_screen(&init, toolbar_screen, "systray.pinLeft");
        if (pin_left == NULL) {
            pin_left = fbwl_resource_db_get_screen(&init, toolbar_screen, "pinLeft");
        }
        (void)fbwl_string_list_set(&server->toolbar_ui.systray_pin_left, &server->toolbar_ui.systray_pin_left_len,
            pin_left);

        const char *pin_right = fbwl_resource_db_get_screen(&init, toolbar_screen, "systray.pinRight");
        if (pin_right == NULL) {
            pin_right = fbwl_resource_db_get_screen(&init, toolbar_screen, "pinRight");
        }
        (void)fbwl_string_list_set(&server->toolbar_ui.systray_pin_right, &server->toolbar_ui.systray_pin_right_len,
            pin_right);

        const char *titlebar_left = fbwl_resource_db_get(&init, "session.screen0.titlebar.left");
        if (titlebar_left == NULL) {
            titlebar_left = fbwl_resource_db_get(&init, "session.titlebar.left");
        }
        (void)fbwl_titlebar_buttons_parse(titlebar_left, server->titlebar_left, FBWL_TITLEBAR_BUTTONS_MAX,
            &server->titlebar_left_len);

        const char *titlebar_right = fbwl_resource_db_get(&init, "session.screen0.titlebar.right");
        if (titlebar_right == NULL) {
            titlebar_right = fbwl_resource_db_get(&init, "session.titlebar.right");
        }
        (void)fbwl_titlebar_buttons_parse(titlebar_right, server->titlebar_right, FBWL_TITLEBAR_BUTTONS_MAX,
            &server->titlebar_right_len);

        const char *default_deco = fbwl_resource_db_get(&init, "session.screen0.defaultDeco");
        if (default_deco != NULL) {
            server->default_deco_enabled = strcasecmp(default_deco, "NONE") != 0;
        }

        wlr_log(WLR_INFO,
            "Init: focusModel=%s autoRaise=%d autoRaiseDelay=%d clickRaises=%d focusNewWindows=%d noFocusWhileTypingDelay=%d focusSameHead=%d demandsAttentionTimeout=%d allowRemoteActions=%d windowPlacement=%s rowDir=%s colDir=%s",
            fbwl_focus_model_str(server->focus.model),
            server->focus.auto_raise ? 1 : 0,
            server->focus.auto_raise_delay_ms,
            server->focus.click_raises ? 1 : 0,
            server->focus.focus_new_windows ? 1 : 0,
            server->focus.no_focus_while_typing_delay_ms,
            server->focus.focus_same_head ? 1 : 0,
            server->focus.demands_attention_timeout_ms,
            server->focus.allow_remote_actions ? 1 : 0,
            fbwl_window_placement_str(fbwm_core_window_placement(&server->wm)),
            fbwl_row_dir_str(fbwm_core_row_placement_direction(&server->wm)),
            fbwl_col_dir_str(fbwm_core_col_placement_direction(&server->wm)));
        wlr_log(WLR_INFO,
            "Init: globals ignoreBorder=%d forcePseudoTransparency=%d configVersion=%d cacheLife=%d cacheMax=%d colorsPerChannel=%d groupFile=%s",
            server->ignore_border ? 1 : 0,
            server->force_pseudo_transparency ? 1 : 0,
            server->config_version,
            server->cache_life_minutes,
            server->cache_max_kb,
            server->colors_per_channel,
            server->group_file != NULL ? server->group_file : "(null)");
        if (server->group_file != NULL) {
            wlr_log(WLR_INFO, "Init: session.groupFile is deprecated; grouping uses apps file (ignoring %s)", server->group_file);
        }

        wlr_log(WLR_INFO,
            "Init: toolbar visible=%d placement=%s onhead=%d layer=%d autoHide=%d autoRaise=%d maxOver=%d widthPercent=%d height=%d tools=0x%x",
            server->toolbar_ui.enabled ? 1 : 0,
            fbwl_toolbar_placement_str(server->toolbar_ui.placement),
            server->toolbar_ui.on_head + 1,
            server->toolbar_ui.layer_num,
            server->toolbar_ui.auto_hide ? 1 : 0,
            server->toolbar_ui.auto_raise ? 1 : 0,
            server->toolbar_ui.max_over ? 1 : 0,
            server->toolbar_ui.width_percent,
            server->toolbar_ui.height_override,
            server->toolbar_ui.tools);

        wlr_log(WLR_INFO,
            "Init: slit placement=%s onhead=%d layer=%d autoHide=%d autoRaise=%d maxOver=%d alpha=%u dir=%s acceptKdeDockapps=%d",
            fbwl_toolbar_placement_str(server->slit_ui.placement),
            server->slit_ui.on_head + 1,
            server->slit_ui.layer_num,
            server->slit_ui.auto_hide ? 1 : 0,
            server->slit_ui.auto_raise ? 1 : 0,
            server->slit_ui.max_over ? 1 : 0,
            (unsigned)server->slit_ui.alpha,
            fbwl_slit_direction_str(server->slit_ui.direction),
            server->slit_ui.accept_kde_dockapps ? 1 : 0);

        wlr_log(WLR_INFO, "Init: menuDelay=%d", server->menu_ui.menu_delay_ms);

        wlr_log(WLR_INFO,
            "Init: tabs intitlebar=%d maxOver=%d usePixmap=%d placement=%s width=%d padding=%d attachArea=%s tabFocusModel=%s",
            server->tabs.intitlebar ? 1 : 0,
            server->tabs.max_over ? 1 : 0,
            server->tabs.use_pixmap ? 1 : 0,
            fbwl_toolbar_placement_str(server->tabs.placement),
            server->tabs.width_px,
            server->tabs.padding_px,
            fbwl_tabs_attach_area_str(server->tabs.attach_area),
            fbwl_tab_focus_model_str(server->tabs.focus_model));
        wlr_log(WLR_INFO, "Init: defaultDeco=%s mapped_ssd=%d",
            default_deco != NULL ? default_deco : "NORMAL",
            server->default_deco_enabled ? 1 : 0);

        if (!workspaces_set) {
            int ws = 0;
            if (fbwl_resource_db_get_int(&init, "session.screen0.workspaces", &ws) && ws > 0 && ws < 1000) {
                workspaces = ws;
            }
        }

        if (keys_file == NULL) {
            keys_file_owned = fbwl_resource_db_discover_path(&init, config_dir, "session.keyFile", "keys");
            keys_file = keys_file_owned;
        }

        if (apps_file == NULL) {
            apps_file_owned = fbwl_resource_db_discover_path(&init, config_dir, "session.appsFile", "apps");
            apps_file = apps_file_owned;
        }

        if (style_file == NULL) {
            style_file_owned = fbwl_resource_db_resolve_path(&init, config_dir, "session.styleFile");
            if (style_file_owned != NULL) {
                style_file = style_file_owned;
            }
        }

        style_overlay_file_owned = fbwl_resource_db_resolve_path(&init, config_dir, "session.styleOverlay");

        if (menu_file == NULL) {
            menu_file_owned = fbwl_resource_db_discover_path(&init, config_dir, "session.menuFile", "menu");
            menu_file = menu_file_owned;
        }

        window_menu_file_owned = fbwl_resource_db_discover_path(&init, config_dir, "session.screen0.windowMenu", "windowmenu");
        if (window_menu_file_owned == NULL) {
            window_menu_file_owned = fbwl_resource_db_discover_path(&init, config_dir, "session.windowMenu", "windowmenu");
        }
        window_menu_file = window_menu_file_owned;

        slitlist_file_owned = fbwl_resource_db_resolve_path(&init, config_dir, "session.slitlistFile");
        if (slitlist_file_owned != NULL) {
            slitlist_file = slitlist_file_owned;
        } else {
            slitlist_file_owned = fbwl_path_join(config_dir, "slitlist");
            slitlist_file = slitlist_file_owned;
        }

        fbwl_resource_db_free(&init);
    }

    fbwl_ui_menu_icon_cache_configure(server->cache_life_minutes, server->cache_max_kb);
    fbwl_texture_cache_configure(server->cache_life_minutes, server->cache_max_kb);

    fbwm_core_set_workspace_count(&server->wm, workspaces);
    fbwl_keybindings_add_defaults(&server->keybindings, &server->keybinding_count, server->terminal_cmd);
    free(server->keys_file);
    server->keys_file = keys_file != NULL ? strdup(keys_file) : NULL;
    free(server->apps_file);
    server->apps_file = apps_file != NULL ? strdup(apps_file) : NULL;
    free(server->style_file);
    server->style_file = style_file != NULL ? strdup(style_file) : NULL;
    free(server->style_overlay_file);
    server->style_overlay_file = style_overlay_file_owned != NULL ? strdup(style_overlay_file_owned) : NULL;
    if (keys_file != NULL) {
        (void)fbwl_keys_parse_file(keys_file, server_keybindings_add_from_keys_file, server, NULL);
        (void)fbwl_keys_parse_file_mouse(keys_file, server_mousebindings_add_from_keys_file, server, NULL);
    }
    if (apps_file != NULL) {
        bool rewrite_safe = false;
        if (fbwl_apps_rules_load_file(&server->apps_rules, &server->apps_rule_count, apps_file, &rewrite_safe)) {
            server->apps_rules_generation++;
            server->apps_rules_rewrite_safe = rewrite_safe;
        } else {
            server->apps_rules_rewrite_safe = false;
        }
    }

    if (style_file != NULL) {
        (void)fbwl_style_load_file(&server->decor_theme, style_file);
    }
    if (server->style_overlay_file != NULL && fbwl_file_exists(server->style_overlay_file)) {
        (void)fbwl_style_load_file(&server->decor_theme, server->style_overlay_file);
    }

    free(keys_file_owned);
    free(apps_file_owned);
    free(style_file_owned);
    free(style_overlay_file_owned);

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

    free(server->window_menu_file);
    server->window_menu_file = window_menu_file != NULL ? strdup(window_menu_file) : NULL;
    free(window_menu_file_owned);
    free(server->slitlist_file);
    server->slitlist_file = slitlist_file != NULL ? strdup(slitlist_file) : NULL;
    free(slitlist_file_owned);

    server_menu_create_window(server);
    server_toolbar_ui_rebuild(server);
    (void)fbwl_ui_slit_set_order_file(&server->slit_ui, server->slitlist_file);
    server_slit_ui_rebuild(server);

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
    server_background_apply_style(server, &server->decor_theme, "bootstrap-start");
    if (server->startup_cmd != NULL) {
        fbwl_spawn(server->startup_cmd);
    }

    wlr_log(WLR_INFO, "Running fluxbox-wayland on WAYLAND_DISPLAY=%s", socket);
    return true;
}
