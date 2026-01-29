#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_keys_parse.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_style_parse.h"

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

static void apply_workspace_names_from_init(struct fbwm_core *wm, const char *csv) {
    if (wm == NULL || csv == NULL) {
        return;
    }

    fbwm_core_clear_workspace_names(wm);

    char *tmp = strdup(csv);
    if (tmp == NULL) {
        return;
    }

    char *saveptr = NULL;
    char *tok = strtok_r(tmp, ",", &saveptr);
    int idx = 0;
    while (tok != NULL && idx < 1000) {
        char *name = trim_inplace(tok);
        if (name != NULL && *name != '\0') {
            (void)fbwm_core_set_workspace_name(wm, idx, name);
            idx++;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }

    free(tmp);
}

static enum fbwl_focus_model parse_focus_model(const char *s) {
    if (s == NULL) {
        return FBWL_FOCUS_MODEL_CLICK_TO_FOCUS;
    }
    if (strcasecmp(s, "MouseFocus") == 0) {
        return FBWL_FOCUS_MODEL_MOUSE_FOCUS;
    }
    if (strcasecmp(s, "StrictMouseFocus") == 0) {
        return FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS;
    }
    return FBWL_FOCUS_MODEL_CLICK_TO_FOCUS;
}

static const char *focus_model_str(enum fbwl_focus_model model) {
    switch (model) {
    case FBWL_FOCUS_MODEL_CLICK_TO_FOCUS:
        return "ClickToFocus";
    case FBWL_FOCUS_MODEL_MOUSE_FOCUS:
        return "MouseFocus";
    case FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS:
        return "StrictMouseFocus";
    default:
        return "ClickToFocus";
    }
}

static enum fbwm_window_placement_strategy parse_window_placement(const char *s) {
    if (s == NULL) {
        return FBWM_PLACE_ROW_SMART;
    }
    if (strcasecmp(s, "RowSmartPlacement") == 0) {
        return FBWM_PLACE_ROW_SMART;
    }
    if (strcasecmp(s, "ColSmartPlacement") == 0) {
        return FBWM_PLACE_COL_SMART;
    }
    if (strcasecmp(s, "CascadePlacement") == 0) {
        return FBWM_PLACE_CASCADE;
    }
    if (strcasecmp(s, "UnderMousePlacement") == 0) {
        return FBWM_PLACE_UNDER_MOUSE;
    }
    if (strcasecmp(s, "RowMinOverlapPlacement") == 0) {
        return FBWM_PLACE_ROW_MIN_OVERLAP;
    }
    if (strcasecmp(s, "ColMinOverlapPlacement") == 0) {
        return FBWM_PLACE_COL_MIN_OVERLAP;
    }
    if (strcasecmp(s, "AutotabPlacement") == 0) {
        return FBWM_PLACE_AUTOTAB;
    }
    return FBWM_PLACE_ROW_SMART;
}

static const char *window_placement_str(enum fbwm_window_placement_strategy placement) {
    switch (placement) {
    case FBWM_PLACE_ROW_SMART:
        return "RowSmartPlacement";
    case FBWM_PLACE_COL_SMART:
        return "ColSmartPlacement";
    case FBWM_PLACE_CASCADE:
        return "CascadePlacement";
    case FBWM_PLACE_UNDER_MOUSE:
        return "UnderMousePlacement";
    case FBWM_PLACE_ROW_MIN_OVERLAP:
        return "RowMinOverlapPlacement";
    case FBWM_PLACE_COL_MIN_OVERLAP:
        return "ColMinOverlapPlacement";
    case FBWM_PLACE_AUTOTAB:
        return "AutotabPlacement";
    default:
        return "RowSmartPlacement";
    }
}

static enum fbwl_toolbar_placement parse_toolbar_placement(const char *s) {
    if (s == NULL) {
        return FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER;
    }
    if (strcasecmp(s, "BottomLeft") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT;
    }
    if (strcasecmp(s, "BottomCenter") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER;
    }
    if (strcasecmp(s, "BottomRight") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT;
    }
    if (strcasecmp(s, "LeftBottom") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM;
    }
    if (strcasecmp(s, "LeftCenter") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER;
    }
    if (strcasecmp(s, "LeftTop") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_LEFT_TOP;
    }
    if (strcasecmp(s, "RightBottom") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM;
    }
    if (strcasecmp(s, "RightCenter") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER;
    }
    if (strcasecmp(s, "RightTop") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP;
    }
    if (strcasecmp(s, "TopLeft") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_TOP_LEFT;
    }
    if (strcasecmp(s, "TopCenter") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_TOP_CENTER;
    }
    if (strcasecmp(s, "TopRight") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT;
    }
    return FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER;
}

static const char *toolbar_placement_str(enum fbwl_toolbar_placement placement) {
    switch (placement) {
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
        return "BottomLeft";
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
        return "BottomCenter";
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        return "BottomRight";
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
        return "LeftBottom";
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
        return "LeftCenter";
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
        return "LeftTop";
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
        return "RightBottom";
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
        return "RightCenter";
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        return "RightTop";
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
        return "TopLeft";
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
        return "TopCenter";
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        return "TopRight";
    default:
        return "BottomCenter";
    }
}

static uint32_t toolbar_tools_default(void) {
    return FBWL_TOOLBAR_TOOL_WORKSPACES | FBWL_TOOLBAR_TOOL_ICONBAR | FBWL_TOOLBAR_TOOL_SYSTEMTRAY | FBWL_TOOLBAR_TOOL_CLOCK;
}

static uint32_t parse_toolbar_tools(const char *s) {
    if (s == NULL || *s == '\0') {
        return toolbar_tools_default();
    }

    uint32_t tools = 0;
    char *copy = strdup(s);
    if (copy == NULL) {
        return toolbar_tools_default();
    }

    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
        while (*tok != '\0' && isspace((unsigned char)*tok)) {
            tok++;
        }
        char *end = tok + strlen(tok);
        while (end > tok && isspace((unsigned char)end[-1])) {
            end--;
        }
        *end = '\0';
        if (*tok == '\0') {
            continue;
        }

        for (char *p = tok; *p != '\0'; p++) {
            *p = (char)tolower((unsigned char)*p);
        }

        if (strcmp(tok, "workspacename") == 0 || strcmp(tok, "prevworkspace") == 0 || strcmp(tok, "nextworkspace") == 0) {
            tools |= FBWL_TOOLBAR_TOOL_WORKSPACES;
        } else if (strcmp(tok, "iconbar") == 0 || strcmp(tok, "prevwindow") == 0 || strcmp(tok, "nextwindow") == 0) {
            tools |= FBWL_TOOLBAR_TOOL_ICONBAR;
        } else if (strcmp(tok, "systemtray") == 0) {
            tools |= FBWL_TOOLBAR_TOOL_SYSTEMTRAY;
        } else if (strcmp(tok, "clock") == 0) {
            tools |= FBWL_TOOLBAR_TOOL_CLOCK;
        } else if (strncmp(tok, "button.", 7) == 0) {
            // not implemented yet
        }
    }

    free(copy);
    if (tools == 0) {
        tools = toolbar_tools_default();
    }
    return tools;
}

static enum fbwm_row_placement_direction parse_row_dir(const char *s) {
    if (s != NULL && strcasecmp(s, "RightToLeft") == 0) {
        return FBWM_ROW_RIGHT_TO_LEFT;
    }
    return FBWM_ROW_LEFT_TO_RIGHT;
}

static const char *row_dir_str(enum fbwm_row_placement_direction dir) {
    return dir == FBWM_ROW_RIGHT_TO_LEFT ? "RightToLeft" : "LeftToRight";
}

static enum fbwm_col_placement_direction parse_col_dir(const char *s) {
    if (s != NULL && strcasecmp(s, "BottomToTop") == 0) {
        return FBWM_COL_BOTTOM_TO_TOP;
    }
    return FBWM_COL_TOP_TO_BOTTOM;
}

static const char *col_dir_str(enum fbwm_col_placement_direction dir) {
    return dir == FBWM_COL_BOTTOM_TO_TOP ? "BottomToTop" : "TopToBottom";
}

static enum fbwl_tab_focus_model parse_tab_focus_model(const char *s) {
    if (s == NULL) {
        return FBWL_TAB_FOCUS_CLICK;
    }
    if (strcasecmp(s, "MouseTabFocus") == 0) {
        return FBWL_TAB_FOCUS_MOUSE;
    }
    if (strcasecmp(s, "ClickTabFocus") == 0) {
        return FBWL_TAB_FOCUS_CLICK;
    }
    return FBWL_TAB_FOCUS_CLICK;
}

static const char *tab_focus_model_str(enum fbwl_tab_focus_model model) {
    switch (model) {
    case FBWL_TAB_FOCUS_MOUSE:
        return "MouseTabFocus";
    case FBWL_TAB_FOCUS_CLICK:
    default:
        return "ClickTabFocus";
    }
}

static enum fbwl_tabs_attach_area parse_tabs_attach_area(const char *s) {
    if (s == NULL) {
        return FBWL_TABS_ATTACH_WINDOW;
    }
    if (strcasecmp(s, "Titlebar") == 0) {
        return FBWL_TABS_ATTACH_TITLEBAR;
    }
    if (strcasecmp(s, "Window") == 0) {
        return FBWL_TABS_ATTACH_WINDOW;
    }
    return FBWL_TABS_ATTACH_WINDOW;
}

static const char *tabs_attach_area_str(enum fbwl_tabs_attach_area area) {
    switch (area) {
    case FBWL_TABS_ATTACH_TITLEBAR:
        return "Titlebar";
    case FBWL_TABS_ATTACH_WINDOW:
    default:
        return "Window";
    }
}

static bool server_keybindings_add_from_keys_file(void *userdata, enum fbwl_keybinding_key_kind key_kind,
        uint32_t keycode, xkb_keysym_t sym, uint32_t modifiers, enum fbwl_keybinding_action action, int arg,
        const char *cmd, const char *mode) {
    struct fbwl_server *server = userdata;
    if (key_kind == FBWL_KEYBIND_KEYCODE) {
        return fbwl_keybindings_add_keycode(&server->keybindings, &server->keybinding_count, keycode, modifiers,
            action, arg, cmd, mode);
    }
    return fbwl_keybindings_add(&server->keybindings, &server->keybinding_count, sym, modifiers, action, arg, cmd, mode);
}

static bool server_mousebindings_add_from_keys_file(void *userdata, enum fbwl_mousebinding_context context,
        int button, uint32_t modifiers, enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
    struct fbwl_server *server = userdata;
    return fbwl_mousebindings_add(&server->mousebindings, &server->mousebinding_count, context, button, modifiers,
        action, arg, cmd, mode);
}

void server_reconfigure(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    bool did_any = false;
    bool toolbar_needs_rebuild = false;
    bool apply_workspace_vis = false;

    if (server->config_dir != NULL && *server->config_dir != '\0') {
        struct fbwl_resource_db init = {0};
        if (fbwl_resource_db_load_init(&init, server->config_dir)) {
            bool bool_val = false;
            int int_val = 0;

            server->focus.model = parse_focus_model(fbwl_resource_db_get(&init, "session.screen0.focusModel"));
            if (fbwl_resource_db_get_bool(&init, "session.screen0.autoRaise", &bool_val)) {
                server->focus.auto_raise = bool_val;
            }
            if (fbwl_resource_db_get_int(&init, "session.autoRaiseDelay", &int_val) && int_val >= 0) {
                server->focus.auto_raise_delay_ms = int_val;
            }
            if (fbwl_resource_db_get_bool(&init, "session.screen0.clickRaises", &bool_val)) {
                server->focus.click_raises = bool_val;
            }
            if (fbwl_resource_db_get_bool(&init, "session.screen0.focusNewWindows", &bool_val)) {
                server->focus.focus_new_windows = bool_val;
            }

            const char *workspace_names = fbwl_resource_db_get(&init, "session.screen0.workspaceNames");
            if (workspace_names != NULL) {
                apply_workspace_names_from_init(&server->wm, workspace_names);
                toolbar_needs_rebuild = true;
            }

            const char *placement = fbwl_resource_db_get(&init, "session.screen0.windowPlacement");
            if (placement != NULL) {
                fbwm_core_set_window_placement(&server->wm, parse_window_placement(placement));
            }
            const char *row_dir = fbwl_resource_db_get(&init, "session.screen0.rowPlacementDirection");
            if (row_dir != NULL) {
                fbwm_core_set_row_placement_direction(&server->wm, parse_row_dir(row_dir));
            }
            const char *col_dir = fbwl_resource_db_get(&init, "session.screen0.colPlacementDirection");
            if (col_dir != NULL) {
                fbwm_core_set_col_placement_direction(&server->wm, parse_col_dir(col_dir));
            }

            if (fbwl_resource_db_get_bool(&init, "session.screen0.toolbar.visible", &bool_val)) {
                server->toolbar_ui.enabled = bool_val;
                toolbar_needs_rebuild = true;
            }
            if (fbwl_resource_db_get_bool(&init, "session.screen0.toolbar.autoHide", &bool_val)) {
                server->toolbar_ui.auto_hide = bool_val;
                toolbar_needs_rebuild = true;
            }
            if (fbwl_resource_db_get_bool(&init, "session.screen0.toolbar.autoRaise", &bool_val)) {
                server->toolbar_ui.auto_raise = bool_val;
                toolbar_needs_rebuild = true;
            }
            if (fbwl_resource_db_get_int(&init, "session.screen0.toolbar.widthPercent", &int_val)) {
                if (int_val < 1) {
                    int_val = 1;
                }
                if (int_val > 100) {
                    int_val = 100;
                }
                server->toolbar_ui.width_percent = int_val;
                toolbar_needs_rebuild = true;
            }
            if (fbwl_resource_db_get_int(&init, "session.screen0.toolbar.height", &int_val) && int_val >= 0) {
                server->toolbar_ui.height_override = int_val;
                toolbar_needs_rebuild = true;
            }
            const char *toolbar_placement = fbwl_resource_db_get(&init, "session.screen0.toolbar.placement");
            if (toolbar_placement != NULL) {
                server->toolbar_ui.placement = parse_toolbar_placement(toolbar_placement);
                toolbar_needs_rebuild = true;
            }
            const char *toolbar_tools = fbwl_resource_db_get(&init, "session.screen0.toolbar.tools");
            if (toolbar_tools != NULL) {
                server->toolbar_ui.tools = parse_toolbar_tools(toolbar_tools);
                toolbar_needs_rebuild = true;
            }
            if (server->toolbar_ui.tools == 0) {
                server->toolbar_ui.tools = toolbar_tools_default();
                toolbar_needs_rebuild = true;
            }
            server->toolbar_ui.hidden = false;

            if (fbwl_resource_db_get_int(&init, "session.screen0.menuDelay", &int_val) && int_val >= 0) {
                server->menu_ui.menu_delay_ms = int_val;
            }

            if (fbwl_resource_db_get_bool(&init, "session.screen0.tabs.intitlebar", &bool_val)) {
                server->tabs.intitlebar = bool_val;
            }
            if (fbwl_resource_db_get_bool(&init, "session.screen0.tabs.maxOver", &bool_val)) {
                server->tabs.max_over = bool_val;
            }
            if (fbwl_resource_db_get_bool(&init, "session.screen0.tabs.usePixmap", &bool_val)) {
                server->tabs.use_pixmap = bool_val;
            }
            const char *tab_place = fbwl_resource_db_get(&init, "session.screen0.tab.placement");
            if (tab_place != NULL) {
                server->tabs.placement = parse_toolbar_placement(tab_place);
            }
            if (fbwl_resource_db_get_int(&init, "session.screen0.tab.width", &int_val) && int_val >= 0) {
                server->tabs.width_px = int_val;
            }
            if (fbwl_resource_db_get_int(&init, "session.tabPadding", &int_val) && int_val >= 0) {
                server->tabs.padding_px = int_val;
            }
            const char *attach_area = fbwl_resource_db_get(&init, "session.tabsAttachArea");
            if (attach_area != NULL) {
                server->tabs.attach_area = parse_tabs_attach_area(attach_area);
            }
            const char *tab_focus = fbwl_resource_db_get(&init, "session.screen0.tabFocusModel");
            if (tab_focus != NULL) {
                server->tabs.focus_model = parse_tab_focus_model(tab_focus);
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
            if (!server->menu_file_override) {
                char *new_path = fbwl_resource_db_discover_path(&init, server->config_dir, "session.menuFile", "menu");
                free(server->menu_file);
                server->menu_file = new_path;
            }

            wlr_log(WLR_INFO, "Reconfigure: reloaded init from %s", server->config_dir);
            wlr_log(WLR_INFO,
                "Reconfigure: init focusModel=%s autoRaise=%d autoRaiseDelay=%d clickRaises=%d focusNewWindows=%d windowPlacement=%s rowDir=%s colDir=%s",
                focus_model_str(server->focus.model),
                server->focus.auto_raise ? 1 : 0,
                server->focus.auto_raise_delay_ms,
                server->focus.click_raises ? 1 : 0,
                server->focus.focus_new_windows ? 1 : 0,
                window_placement_str(fbwm_core_window_placement(&server->wm)),
                row_dir_str(fbwm_core_row_placement_direction(&server->wm)),
                col_dir_str(fbwm_core_col_placement_direction(&server->wm)));
            wlr_log(WLR_INFO, "Reconfigure: init toolbar visible=%d placement=%s autoHide=%d autoRaise=%d widthPercent=%d height=%d tools=0x%x",
                server->toolbar_ui.enabled ? 1 : 0,
                toolbar_placement_str(server->toolbar_ui.placement),
                server->toolbar_ui.auto_hide ? 1 : 0,
                server->toolbar_ui.auto_raise ? 1 : 0,
                server->toolbar_ui.width_percent,
                server->toolbar_ui.height_override,
                server->toolbar_ui.tools);
            wlr_log(WLR_INFO, "Reconfigure: init menuDelay=%d", server->menu_ui.menu_delay_ms);
            wlr_log(WLR_INFO,
                "Reconfigure: init tabs intitlebar=%d maxOver=%d usePixmap=%d placement=%s width=%d padding=%d attachArea=%s tabFocusModel=%s",
                server->tabs.intitlebar ? 1 : 0,
                server->tabs.max_over ? 1 : 0,
                server->tabs.use_pixmap ? 1 : 0,
                toolbar_placement_str(server->tabs.placement),
                server->tabs.width_px,
                server->tabs.padding_px,
                tabs_attach_area_str(server->tabs.attach_area),
                tab_focus_model_str(server->tabs.focus_model));
            wlr_log(WLR_INFO, "Reconfigure: init keys_file=%s apps_file=%s style_file=%s menu_file=%s workspaces=%d",
                server->keys_file != NULL ? server->keys_file : "(null)",
                server->apps_file != NULL ? server->apps_file : "(null)",
                server->style_file != NULL ? server->style_file : "(null)",
                server->menu_file != NULL ? server->menu_file : "(null)",
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
        if (fbwl_apps_rules_load_file(&server->apps_rules, &server->apps_rule_count, apps_file)) {
            wlr_log(WLR_INFO, "Reconfigure: reloaded apps from %s", apps_file);
        } else {
            wlr_log(WLR_ERROR, "Reconfigure: failed to reload apps from %s", apps_file);
        }
        did_any = true;
    }

    const char *style_file = server->style_file;
    if (style_file != NULL && *style_file != '\0') {
        struct fbwl_decor_theme new_theme = {0};
        decor_theme_set_defaults(&new_theme);
        if (fbwl_style_load_file(&new_theme, style_file)) {
            server->decor_theme = new_theme;
            server_toolbar_ui_rebuild(server);
            toolbar_needs_rebuild = false;
            wlr_log(WLR_INFO, "Reconfigure: reloaded style from %s", style_file);
            did_any = true;
        } else {
            wlr_log(WLR_ERROR, "Reconfigure: failed to reload style from %s", style_file);
        }
    }

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

    if (apply_workspace_vis) {
        apply_workspace_visibility(server, "reconfigure");
    }

    if (toolbar_needs_rebuild) {
        server_toolbar_ui_rebuild(server);
    }

    if (!did_any) {
        wlr_log(WLR_INFO, "Reconfigure: nothing to reload");
    }
}
