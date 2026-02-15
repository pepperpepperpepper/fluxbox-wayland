#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_keys_parse.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_string_list.h"
#include "wayland/fbwl_style_parse.h"
#include "wayland/fbwl_ui_menu_icon.h"
#include "wayland/fbwl_ui_menu_search.h"

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

void server_reconfigure(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    bool did_any = false;
    bool toolbar_needs_rebuild = false;
    bool slit_needs_rebuild = false;
    bool apply_workspace_vis = false;
    bool decor_needs_update = false;
    bool window_alpha_defaults_changed = false;
    bool default_deco_changed = false;

    if (server->config_dir != NULL && *server->config_dir != '\0') {
        struct fbwl_resource_db init = {0};
        if (fbwl_resource_db_load_init(&init, server->config_dir)) {
            bool bool_val = false;
            int int_val = 0;

            server->ignore_border = false;
            server->force_pseudo_transparency = false;
            server->colors_per_channel = 4;
            server->cache_life_minutes = 5;
            server->cache_max_kb = 200;
            server->config_version = 0;
            free(server->group_file);
            server->group_file = NULL;

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

            fbwl_ui_menu_icon_cache_configure(server->cache_life_minutes, server->cache_max_kb);
            fbwl_texture_cache_configure(server->cache_life_minutes, server->cache_max_kb);

            server->menu_ui.search_mode = FBWL_MENU_SEARCH_ITEMSTART;
            const char *menu_search = fbwl_resource_db_get(&init, "session.menuSearch");
            if (menu_search != NULL) {
                server->menu_ui.search_mode = fbwl_menu_search_mode_parse(menu_search);
            }
            char *group_file = fbwl_resource_db_resolve_path(&init, server->config_dir, "session.groupFile");
            if (group_file != NULL) {
                free(server->group_file);
                server->group_file = group_file;
            }

            const bool old_window_alpha_configured = server->window_alpha_defaults_configured;
            const uint8_t old_window_alpha_focused = server->window_alpha_default_focused;
            const uint8_t old_window_alpha_unfocused = server->window_alpha_default_unfocused;
            const bool old_default_deco_enabled = server->default_deco_enabled;
            const bool old_toolbar_enabled = server->toolbar_ui.enabled;
            const enum fbwl_toolbar_placement old_toolbar_placement = server->toolbar_ui.placement;
            const int old_toolbar_on_head = server->toolbar_ui.on_head;
            const int old_toolbar_layer = server->toolbar_ui.layer_num;
            const int old_toolbar_width_percent = server->toolbar_ui.width_percent;
            const int old_toolbar_height_override = server->toolbar_ui.height_override;
            const uint32_t old_toolbar_tools = server->toolbar_ui.tools;
            const bool old_toolbar_auto_hide = server->toolbar_ui.auto_hide;
            const bool old_toolbar_auto_raise = server->toolbar_ui.auto_raise;
            const unsigned old_toolbar_alpha = server->toolbar_ui.alpha;
            char old_strftime_format[sizeof(server->toolbar_ui.strftime_format)];
            strncpy(old_strftime_format, server->toolbar_ui.strftime_format, sizeof(old_strftime_format));
            old_strftime_format[sizeof(old_strftime_format) - 1] = '\0';
            char old_iconbar_mode[sizeof(server->toolbar_ui.iconbar_mode)];
            strncpy(old_iconbar_mode, server->toolbar_ui.iconbar_mode, sizeof(old_iconbar_mode));
            old_iconbar_mode[sizeof(old_iconbar_mode) - 1] = '\0';
            const enum fbwl_iconbar_alignment old_iconbar_alignment = server->toolbar_ui.iconbar_alignment;
            const int old_iconbar_icon_width = server->toolbar_ui.iconbar_icon_width_px;
            const int old_iconbar_padding = server->toolbar_ui.iconbar_icon_text_padding_px;
            const bool old_iconbar_use_pixmap = server->toolbar_ui.iconbar_use_pixmap;
            char old_iconified_prefix[sizeof(server->toolbar_ui.iconbar_iconified_prefix)];
            strncpy(old_iconified_prefix, server->toolbar_ui.iconbar_iconified_prefix, sizeof(old_iconified_prefix));
            old_iconified_prefix[sizeof(old_iconified_prefix) - 1] = '\0';
            char old_iconified_suffix[sizeof(server->toolbar_ui.iconbar_iconified_suffix)];
            strncpy(old_iconified_suffix, server->toolbar_ui.iconbar_iconified_suffix, sizeof(old_iconified_suffix));
            old_iconified_suffix[sizeof(old_iconified_suffix) - 1] = '\0';
            const enum fbwl_toolbar_placement old_slit_placement = server->slit_ui.placement;
            const int old_slit_on_head = server->slit_ui.on_head;
            const int old_slit_layer = server->slit_ui.layer_num;
            const bool old_slit_auto_hide = server->slit_ui.auto_hide;
            const bool old_slit_auto_raise = server->slit_ui.auto_raise;
            const bool old_slit_max_over = server->slit_ui.max_over;
            const bool old_slit_accept_kde_dockapps = server->slit_ui.accept_kde_dockapps;
            const unsigned old_slit_alpha = server->slit_ui.alpha;
            const enum fbwl_slit_direction old_slit_direction = server->slit_ui.direction;
            const unsigned old_menu_alpha = server->menu_ui.alpha;
            server->window_alpha_defaults_configured = false;
            server->window_alpha_default_focused = 255;
            server->window_alpha_default_unfocused = 255;
            server->default_deco_enabled = true;
            server->titlebar_left_len = 1;
            server->titlebar_left[0] = FBWL_DECOR_HIT_BTN_STICK;
            server->titlebar_right_len = 4;
            server->titlebar_right[0] = FBWL_DECOR_HIT_BTN_SHADE;
            server->titlebar_right[1] = FBWL_DECOR_HIT_BTN_MIN;
            server->titlebar_right[2] = FBWL_DECOR_HIT_BTN_MAX;
            server->titlebar_right[3] = FBWL_DECOR_HIT_BTN_CLOSE;
            decor_needs_update = true;

            fbwl_server_load_screen_configs(server, &init);
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
            if (server_toolbar_ui_load_button_tools(server, &init, toolbar_screen)) {
                toolbar_needs_rebuild = true;
            }
            server->toolbar_ui.hidden = false;
            if (server->toolbar_ui.enabled != old_toolbar_enabled ||
                    server->toolbar_ui.placement != old_toolbar_placement ||
                    server->toolbar_ui.on_head != old_toolbar_on_head ||
                    server->toolbar_ui.layer_num != old_toolbar_layer ||
                    server->toolbar_ui.width_percent != old_toolbar_width_percent ||
                    server->toolbar_ui.height_override != old_toolbar_height_override ||
                    server->toolbar_ui.tools != old_toolbar_tools ||
                    server->toolbar_ui.auto_hide != old_toolbar_auto_hide ||
                    server->toolbar_ui.auto_raise != old_toolbar_auto_raise ||
                    server->toolbar_ui.alpha != old_toolbar_alpha ||
                    strcmp(server->toolbar_ui.strftime_format, old_strftime_format) != 0 ||
                    strcmp(server->toolbar_ui.iconbar_mode, old_iconbar_mode) != 0 ||
                    server->toolbar_ui.iconbar_alignment != old_iconbar_alignment ||
                    server->toolbar_ui.iconbar_icon_width_px != old_iconbar_icon_width ||
                    server->toolbar_ui.iconbar_icon_text_padding_px != old_iconbar_padding ||
                    server->toolbar_ui.iconbar_use_pixmap != old_iconbar_use_pixmap ||
                    strcmp(server->toolbar_ui.iconbar_iconified_prefix, old_iconified_prefix) != 0 ||
                    strcmp(server->toolbar_ui.iconbar_iconified_suffix, old_iconified_suffix) != 0) {
                toolbar_needs_rebuild = true;
            }

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
            server->slit_ui.hovered = false;
            server->slit_ui.auto_pending = 0;
            if (server->slit_ui.placement != old_slit_placement ||
                    server->slit_ui.on_head != old_slit_on_head ||
                    server->slit_ui.layer_num != old_slit_layer ||
                    server->slit_ui.auto_hide != old_slit_auto_hide ||
                    server->slit_ui.auto_raise != old_slit_auto_raise ||
                    server->slit_ui.max_over != old_slit_max_over ||
                    server->slit_ui.accept_kde_dockapps != old_slit_accept_kde_dockapps ||
                    server->slit_ui.alpha != old_slit_alpha ||
                    server->slit_ui.direction != old_slit_direction) {
                slit_needs_rebuild = true;
            }

            if (server->menu_ui.alpha != old_menu_alpha) {
                // No immediate rebuild; applied on next menu open.
            }

            if (fbwl_resource_db_get_int(&init, "session.doubleClickInterval", &int_val) &&
                    int_val >= 0 && int_val < 60000) {
                server->double_click_interval_ms = int_val;
            }

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
            if (server->window_alpha_defaults_configured != old_window_alpha_configured ||
                    server->window_alpha_default_focused != old_window_alpha_focused ||
                    server->window_alpha_default_unfocused != old_window_alpha_unfocused) {
                window_alpha_defaults_changed = true;
            }

            const char *workspace_names = fbwl_resource_db_get(&init, "session.screen0.workspaceNames");
            if (workspace_names != NULL) {
                fbwl_apply_workspace_names_from_init(&server->wm, workspace_names);
                toolbar_needs_rebuild = true;
            }

            const char *pin_left = fbwl_resource_db_get_screen(&init, toolbar_screen, "systray.pinLeft");
            if (pin_left == NULL) {
                pin_left = fbwl_resource_db_get_screen(&init, toolbar_screen, "pinLeft");
            }
            if (fbwl_string_list_set(&server->toolbar_ui.systray_pin_left, &server->toolbar_ui.systray_pin_left_len,
                    pin_left)) {
                toolbar_needs_rebuild = true;
            }

            const char *pin_right = fbwl_resource_db_get_screen(&init, toolbar_screen, "systray.pinRight");
            if (pin_right == NULL) {
                pin_right = fbwl_resource_db_get_screen(&init, toolbar_screen, "pinRight");
            }
            if (fbwl_string_list_set(&server->toolbar_ui.systray_pin_right, &server->toolbar_ui.systray_pin_right_len,
                    pin_right)) {
                toolbar_needs_rebuild = true;
            }

            const char *titlebar_left = fbwl_resource_db_get(&init, "session.screen0.titlebar.left");
            if (titlebar_left == NULL) {
                titlebar_left = fbwl_resource_db_get(&init, "session.titlebar.left");
            }
            if (fbwl_titlebar_buttons_parse(titlebar_left, server->titlebar_left, FBWL_TITLEBAR_BUTTONS_MAX,
                    &server->titlebar_left_len)) {
                decor_needs_update = true;
            }

            const char *titlebar_right = fbwl_resource_db_get(&init, "session.screen0.titlebar.right");
            if (titlebar_right == NULL) {
                titlebar_right = fbwl_resource_db_get(&init, "session.titlebar.right");
            }
            if (fbwl_titlebar_buttons_parse(titlebar_right, server->titlebar_right, FBWL_TITLEBAR_BUTTONS_MAX,
                    &server->titlebar_right_len)) {
                decor_needs_update = true;
            }

            const char *default_deco = fbwl_resource_db_get(&init, "session.screen0.defaultDeco");
            if (default_deco != NULL) {
                server->default_deco_enabled = strcasecmp(default_deco, "NONE") != 0;
            }
            if (server->default_deco_enabled != old_default_deco_enabled) {
                default_deco_changed = true;
            }

            if (!server->workspaces_override) {
                int ws = 0;
                if (fbwl_resource_db_get_int(&init, "session.screen0.workspaces", &ws) && ws > 0 && ws < 1000) {
                    const int cur_ws = fbwm_core_workspace_count(&server->wm);
                    if (ws != cur_ws) {
                        if (ws < cur_ws) {
                            const int target_ws = ws - 1;
                            for (struct fbwm_view *walk = server->wm.views.next; walk != &server->wm.views; walk = walk->next) {
                                if (!walk->sticky && walk->workspace >= ws) {
                                    walk->workspace = target_ws;
                                }
                            }
                        }
                        fbwm_core_set_workspace_count(&server->wm, ws);
                        apply_workspace_vis = true;
                        toolbar_needs_rebuild = true;
                    }
                }
            }

            if (!server->keys_file_override) {
                char *new_path = fbwl_resource_db_discover_path(&init, server->config_dir, "session.keyFile", "keys");
                free(server->keys_file);
                server->keys_file = new_path;
            }
            if (!server->apps_file_override) {
                char *new_path = fbwl_resource_db_discover_path(&init, server->config_dir, "session.appsFile", "apps");
                free(server->apps_file);
                server->apps_file = new_path;
            }
            if (!server->style_file_override) {
                char *new_path = fbwl_resource_db_resolve_path(&init, server->config_dir, "session.styleFile");
                free(server->style_file);
                server->style_file = new_path;
            }
            char *new_overlay_path = fbwl_resource_db_resolve_path(&init, server->config_dir, "session.styleOverlay");
            free(server->style_overlay_file);
            server->style_overlay_file = new_overlay_path;
            if (!server->menu_file_override) {
                char *new_path = fbwl_resource_db_discover_path(&init, server->config_dir, "session.menuFile", "menu");
                free(server->menu_file);
                server->menu_file = new_path;
            }
            char *new_window_menu_path =
                fbwl_resource_db_discover_path(&init, server->config_dir, "session.screen0.windowMenu", "windowmenu");
            if (new_window_menu_path == NULL) {
                new_window_menu_path =
                    fbwl_resource_db_discover_path(&init, server->config_dir, "session.windowMenu", "windowmenu");
            }
            free(server->window_menu_file);
            server->window_menu_file = new_window_menu_path;

            char *new_slitlist_path = fbwl_resource_db_resolve_path(&init, server->config_dir, "session.slitlistFile");
            if (new_slitlist_path == NULL) {
                new_slitlist_path = fbwl_path_join(server->config_dir, "slitlist");
            }
            free(server->slitlist_file);
            server->slitlist_file = new_slitlist_path;
            if (fbwl_ui_slit_set_order_file(&server->slit_ui, server->slitlist_file)) {
                slit_needs_rebuild = true;
            }

            wlr_log(WLR_INFO, "Reconfigure: reloaded init from %s", server->config_dir);
            wlr_log(WLR_INFO,
                "Reconfigure: init focusModel=%s autoRaise=%d autoRaiseDelay=%d clickRaises=%d focusNewWindows=%d noFocusWhileTypingDelay=%d focusSameHead=%d demandsAttentionTimeout=%d allowRemoteActions=%d windowPlacement=%s rowDir=%s colDir=%s",
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
                "Reconfigure: init globals ignoreBorder=%d forcePseudoTransparency=%d configVersion=%d cacheLife=%d cacheMax=%d colorsPerChannel=%d groupFile=%s",
                server->ignore_border ? 1 : 0,
                server->force_pseudo_transparency ? 1 : 0,
                server->config_version,
                server->cache_life_minutes,
                server->cache_max_kb,
                server->colors_per_channel,
                server->group_file != NULL ? server->group_file : "(null)");
            if (server->group_file != NULL) {
                wlr_log(WLR_INFO, "Reconfigure: session.groupFile is deprecated; grouping uses apps file (ignoring %s)", server->group_file);
            }
            wlr_log(WLR_INFO, "Reconfigure: init toolbar visible=%d placement=%s onhead=%d layer=%d autoHide=%d autoRaise=%d maxOver=%d widthPercent=%d height=%d tools=0x%x",
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
                "Reconfigure: init slit placement=%s onhead=%d layer=%d autoHide=%d autoRaise=%d maxOver=%d alpha=%u dir=%s acceptKdeDockapps=%d",
                fbwl_toolbar_placement_str(server->slit_ui.placement),
                server->slit_ui.on_head + 1,
                server->slit_ui.layer_num,
                server->slit_ui.auto_hide ? 1 : 0,
                server->slit_ui.auto_raise ? 1 : 0,
                server->slit_ui.max_over ? 1 : 0,
                (unsigned)server->slit_ui.alpha,
                fbwl_slit_direction_str(server->slit_ui.direction),
                server->slit_ui.accept_kde_dockapps ? 1 : 0);
            wlr_log(WLR_INFO, "Reconfigure: init menuDelay=%d", server->menu_ui.menu_delay_ms);
            wlr_log(WLR_INFO,
                "Reconfigure: init tabs intitlebar=%d maxOver=%d usePixmap=%d placement=%s width=%d padding=%d attachArea=%s tabFocusModel=%s",
                server->tabs.intitlebar ? 1 : 0,
                server->tabs.max_over ? 1 : 0,
                server->tabs.use_pixmap ? 1 : 0,
                fbwl_toolbar_placement_str(server->tabs.placement),
                server->tabs.width_px,
                server->tabs.padding_px,
                fbwl_tabs_attach_area_str(server->tabs.attach_area),
                fbwl_tab_focus_model_str(server->tabs.focus_model));
            wlr_log(WLR_INFO, "Reconfigure: init defaultDeco mapped_ssd=%d", server->default_deco_enabled ? 1 : 0);
            wlr_log(WLR_INFO, "Reconfigure: init keys_file=%s apps_file=%s style_file=%s menu_file=%s window_menu_file=%s workspaces=%d",
                server->keys_file != NULL ? server->keys_file : "(null)",
                server->apps_file != NULL ? server->apps_file : "(null)",
                server->style_file != NULL ? server->style_file : "(null)",
                server->menu_file != NULL ? server->menu_file : "(null)",
                server->window_menu_file != NULL ? server->window_menu_file : "(null)",
                fbwm_core_workspace_count(&server->wm));

            fbwl_resource_db_free(&init);
            did_any = true;
        }
    }

    const char *keys_file = server->keys_file;
    if (keys_file != NULL && *keys_file != '\0') {
        fbwl_keybindings_free(&server->keybindings, &server->keybinding_count);
        fbwl_mousebindings_free(&server->mousebindings, &server->mousebinding_count);
        free(server->key_mode);
        server->key_mode = NULL;
        server->key_mode_return_active = false;
        server->key_mode_return_kind = FBWL_KEYBIND_KEYSYM;
        server->key_mode_return_keycode = 0;
        server->key_mode_return_sym = XKB_KEY_NoSymbol;
        server->key_mode_return_modifiers = 0;

        fbwl_keybindings_add_defaults(&server->keybindings, &server->keybinding_count, server->terminal_cmd);
        (void)fbwl_keys_parse_file(keys_file, server_keybindings_add_from_keys_file, server, NULL);
        (void)fbwl_keys_parse_file_mouse(keys_file, server_mousebindings_add_from_keys_file, server, NULL);

        wlr_log(WLR_INFO, "Reconfigure: reloaded keys from %s", keys_file);
        did_any = true;
    } else {
        wlr_log(WLR_INFO, "Reconfigure: no keys file configured");
    }

    const char *apps_file = server->apps_file;
    if (apps_file != NULL && *apps_file != '\0') {
        fbwl_apps_rules_free(&server->apps_rules, &server->apps_rule_count);
        bool rewrite_safe = false;
        if (fbwl_apps_rules_load_file(&server->apps_rules, &server->apps_rule_count, apps_file, &rewrite_safe)) {
            server->apps_rules_generation++;
            server->apps_rules_rewrite_safe = rewrite_safe;
            wlr_log(WLR_INFO, "Reconfigure: reloaded apps from %s", apps_file);
        } else {
            server->apps_rules_rewrite_safe = false;
            wlr_log(WLR_ERROR, "Reconfigure: failed to reload apps from %s", apps_file);
        }
        did_any = true;
    }

    const char *style_file = server->style_file;
    const char *style_overlay_file = server->style_overlay_file;
    const bool have_overlay = style_overlay_file != NULL && fbwl_file_exists(style_overlay_file);
    if ((style_file != NULL && *style_file != '\0') || have_overlay) {
        struct fbwl_decor_theme new_theme = {0};
        decor_theme_set_defaults(&new_theme);

        if (style_file != NULL && *style_file != '\0') {
            if (!fbwl_style_load_file(&new_theme, style_file)) {
                wlr_log(WLR_ERROR, "Reconfigure: failed to reload style from %s", style_file);
                goto done_style;
            }
            wlr_log(WLR_INFO, "Reconfigure: reloaded style from %s", style_file);
            did_any = true;
        } else {
            wlr_log(WLR_INFO, "Reconfigure: no style file configured");
        }

        if (have_overlay) {
            (void)fbwl_style_load_file(&new_theme, style_overlay_file);
            wlr_log(WLR_INFO, "Reconfigure: applied style overlay from %s", style_overlay_file);
            did_any = true;
        }

        server->decor_theme = new_theme;
        server_toolbar_ui_rebuild(server);
        server_slit_ui_rebuild(server);
        server_background_apply_style(server, &server->decor_theme, "reconfigure-style");
        toolbar_needs_rebuild = false;
        slit_needs_rebuild = false;
        decor_needs_update = true;
    }
done_style:

    const char *menu_file = server->menu_file;
    if (menu_file != NULL && *menu_file != '\0') {
        if (!server_menu_load_file(server, menu_file)) {
            wlr_log(WLR_ERROR, "Reconfigure: failed to reload menu from %s", menu_file);
            server_menu_create_default(server);
        } else {
            wlr_log(WLR_INFO, "Reconfigure: reloaded menu from %s", menu_file);
        }
        did_any = true;
    }

    server_menu_create_window(server);

    if (window_alpha_defaults_changed) {
        for (struct fbwm_view *wm_view = server->wm.views.next;
                wm_view != &server->wm.views;
                wm_view = wm_view->next) {
            struct fbwl_view *view = wm_view->userdata;
            if (view == NULL) {
                continue;
            }
            if (view->alpha_is_default || (!view->alpha_set && server->window_alpha_defaults_configured)) {
                fbwl_view_set_alpha(view, server->window_alpha_default_focused, server->window_alpha_default_unfocused,
                    "reconfigure-default");
                view->alpha_is_default = true;
            }
        }
    }

    if (default_deco_changed) {
        for (struct fbwm_view *wm_view = server->wm.views.next;
                wm_view != &server->wm.views;
                wm_view = wm_view->next) {
            struct fbwl_view *view = wm_view->userdata;
            if (view == NULL || view->decor_forced) {
                continue;
            }
            bool enable = server->default_deco_enabled;
            if (view->type == FBWL_VIEW_XDG) {
                enable = enable && view->xdg_decoration_server_side;
            }
            fbwl_view_decor_set_enabled(view, enable);
        }
        decor_needs_update = true;
    }

    if (decor_needs_update) {
        for (struct fbwm_view *wm_view = server->wm.views.next;
                wm_view != &server->wm.views;
                wm_view = wm_view->next) {
            struct fbwl_view *view = wm_view->userdata;
            fbwl_view_decor_update(view, &server->decor_theme);
        }
    }

    if (apply_workspace_vis) {
        apply_workspace_visibility(server, "reconfigure");
    }

    if (toolbar_needs_rebuild) {
        server_toolbar_ui_rebuild(server);
    }

    if (slit_needs_rebuild) {
        server_slit_ui_rebuild(server);
    }

    server_pseudo_transparency_refresh(server, "reconfigure");

    if (!did_any) {
        wlr_log(WLR_INFO, "Reconfigure: nothing to reload");
    }
}
