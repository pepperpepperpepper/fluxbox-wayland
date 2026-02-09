#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include "wayland/fbwl_fluxbox_cmd.h"
#include "wayland/fbwl_icon_theme.h"
#include "wayland/fbwl_keybindings.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_server_menu_actions.h"
#include "wayland/fbwl_string_list.h"
#include "wayland/fbwl_ui_toolbar_iconbar_pattern.h"
#include "wayland/fbwl_ui_menu_icon.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"

void apply_workspace_visibility(struct fbwl_server *server, const char *why) {
    const size_t heads = fbwm_core_head_count(&server->wm);
    size_t cursor_head = 0;
    if (server->cursor != NULL) {
        cursor_head = fbwl_server_screen_index_at(server, server->cursor->x, server->cursor->y);
    }
    const int cur = fbwm_core_workspace_current_for_head(&server->wm, cursor_head);
    wlr_log(WLR_INFO, "Workspace: apply current=%d reason=%s head=%zu heads=%zu",
        cur + 1, why != NULL ? why : "(null)", cursor_head, heads);

    fbwl_tabs_repair(server);

    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || view->scene_tree == NULL) {
            continue;
        }

        const bool visible_ws = fbwm_core_view_is_visible(&server->wm, wm_view);
        const bool visible = visible_ws && fbwl_tabs_view_is_active(view);
        wlr_scene_node_set_enabled(&view->scene_tree->node, visible);

        const char *title = NULL;
        if (wm_view->ops != NULL && wm_view->ops->title != NULL) {
            title = wm_view->ops->title(wm_view);
        }
        const int view_head = wm_view->ops != NULL && wm_view->ops->head != NULL ? wm_view->ops->head(wm_view) : 0;
        const size_t view_head0 = view_head > 0 ? (size_t)view_head : 0;
        const int head_ws = fbwm_core_workspace_current_for_head(&server->wm, view_head0);
        wlr_log(WLR_INFO, "Workspace: view=%s ws=%d visible=%d head=%d head_ws=%d",
            title != NULL ? title : "(no-title)", wm_view->workspace + 1, visible ? 1 : 0, view_head, head_ws + 1);
    }

    server_toolbar_ui_rebuild(server);

    if (server->wm.focused == NULL) {
        clear_keyboard_focus(server);
    }

    server_strict_mousefocus_recheck(server, why);
}

void server_workspace_switch_on_head(struct fbwl_server *server, size_t head, int workspace0, const char *why) {
    if (server == NULL) {
        return;
    }

    const int count = fbwm_core_workspace_count(&server->wm);
    if (workspace0 < 0 || workspace0 >= count) {
        return;
    }

    const int cur = fbwm_core_workspace_current_for_head(&server->wm, head);
    if (workspace0 == cur) {
        return;
    }

    fbwm_core_workspace_switch_on_head(&server->wm, head, workspace0);
    wlr_log(WLR_INFO, "Workspace: switch head=%zu ws=%d reason=%s",
        head, workspace0 + 1, why != NULL ? why : "(null)");

    if (server->osd_ui.enabled && server->scene != NULL) {
        const char *name = fbwm_core_workspace_name(&server->wm, workspace0);
        fbwl_ui_osd_show_workspace(&server->osd_ui, server->scene, server->layer_top,
            &server->decor_theme, server->output_layout, workspace0, name);
    }

    apply_workspace_visibility(server, why);

    if (!server->change_workspace_binding_active) {
        server->change_workspace_binding_active = true;
        struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
        (void)fbwl_keybindings_handle_change_workspace(server->keybindings, server->keybinding_count, &hooks);
        server->change_workspace_binding_active = false;
    }
}

static struct wlr_scene_tree *toolbar_ui_layer_tree(struct fbwl_server *server) {
    if (server == NULL) {
        return NULL;
    }

    struct wlr_scene_tree *fallback = server->layer_top;

    const int n = server->toolbar_ui.layer_num;
    struct wlr_scene_tree *tree = NULL;
    if (n <= 0) {
        tree = server->layer_overlay;
    } else if (n <= 6) {
        tree = server->layer_top;
    } else if (n <= 8) {
        tree = server->layer_normal;
    } else if (n <= 10) {
        tree = server->layer_bottom;
    } else {
        tree = server->layer_background;
    }

    return tree != NULL ? tree : fallback;
}

static struct wlr_scene_tree *slit_ui_layer_tree(struct fbwl_server *server) {
    if (server == NULL) {
        return NULL;
    }

    struct wlr_scene_tree *fallback = server->layer_top;

    const int n = server->slit_ui.layer_num;
    struct wlr_scene_tree *tree = NULL;
    if (n <= 0) {
        tree = server->layer_overlay;
    } else if (n <= 6) {
        tree = server->layer_top;
    } else if (n <= 8) {
        tree = server->layer_normal;
    } else if (n <= 10) {
        tree = server->layer_bottom;
    } else {
        tree = server->layer_background;
    }

    return tree != NULL ? tree : fallback;
}

static void toolbar_ui_apply_workspace_visibility(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    apply_workspace_visibility(server, why);
}

static void toolbar_ui_view_set_minimized(void *userdata, struct fbwl_view *view, bool minimized, const char *why) {
    (void)userdata;
    view_set_minimized(view, minimized, why);
}

static char *toolbar_trim_inplace(char *s) {
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

static void toolbar_ui_execute_command(void *userdata, const char *cmd_line, int lx, int ly, uint32_t button) {
    struct fbwl_server *server = userdata;
    if (server == NULL || cmd_line == NULL) {
        return;
    }

    char *copy = strdup(cmd_line);
    if (copy == NULL) {
        return;
    }

    char *s = toolbar_trim_inplace(copy);
    if (s == NULL || *s == '\0') {
        free(copy);
        return;
    }

    char *sp = s;
    while (*sp != '\0' && !isspace((unsigned char)*sp)) {
        sp++;
    }
    char *cmd_args = sp;
    if (*sp != '\0') {
        *sp = '\0';
        cmd_args = sp + 1;
    }
    const char *cmd_name = s;
    cmd_args = toolbar_trim_inplace(cmd_args);

    enum fbwl_keybinding_action action;
    int action_arg = 0;
    const char *action_cmd = NULL;
    if (!fbwl_fluxbox_cmd_resolve(cmd_name, cmd_args, &action, &action_arg, &action_cmd)) {
        wlr_log(WLR_ERROR, "Toolbar: unknown command: %s", cmd_name);
        free(copy);
        return;
    }

    struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
    hooks.cursor_x = lx;
    hooks.cursor_y = ly;
    hooks.button = (button == 4 || button == 5) ? 0 : button;
    (void)fbwl_keybindings_execute_action(action, action_arg, action_cmd, NULL, &hooks);

    free(copy);
}

static struct fbwl_ui_toolbar_env toolbar_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_toolbar_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_tree = toolbar_ui_layer_tree(server),
        .output_layout = server != NULL ? server->output_layout : NULL,
        .outputs = server != NULL ? &server->outputs : NULL,
        .wl_display = server != NULL ? server->wl_display : NULL,
        .wm = server != NULL ? &server->wm : NULL,
        .xwayland = server != NULL ? server->xwayland : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
        .focused_view = server != NULL ? server->focused_view : NULL,
        .cursor_valid = server != NULL && server->cursor != NULL,
        .cursor_x = server != NULL && server->cursor != NULL ? server->cursor->x : 0.0,
        .cursor_y = server != NULL && server->cursor != NULL ? server->cursor->y : 0.0,
        .layer_background = server != NULL ? server->layer_background : NULL,
        .layer_bottom = server != NULL ? server->layer_bottom : NULL,
        .layer_normal = server != NULL ? server->layer_normal : NULL,
        .layer_fullscreen = server != NULL ? server->layer_fullscreen : NULL,
        .layer_top = server != NULL ? server->layer_top : NULL,
        .layer_overlay = server != NULL ? server->layer_overlay : NULL,
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
        .execute_command = toolbar_ui_execute_command,
    };
}

void server_toolbar_ui_handle_motion(struct fbwl_server *server) {
    if (server == NULL || server->cursor == NULL) {
        return;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    fbwl_ui_toolbar_handle_motion(&server->toolbar_ui, &env,
        (int)server->cursor->x, (int)server->cursor->y, server->focus.auto_raise_delay_ms);
}

static struct fbwl_ui_slit_env slit_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_slit_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_tree = slit_ui_layer_tree(server),
        .output_layout = server != NULL ? server->output_layout : NULL,
        .outputs = server != NULL ? &server->outputs : NULL,
        .wl_display = server != NULL ? server->wl_display : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
    };
}

void server_slit_ui_handle_motion(struct fbwl_server *server) {
    if (server == NULL || server->cursor == NULL) {
        return;
    }

    const struct fbwl_ui_slit_env env = slit_ui_env(server);
    fbwl_ui_slit_handle_motion(&server->slit_ui, &env,
        (int)server->cursor->x, (int)server->cursor->y, server->focus.auto_raise_delay_ms);
}

static struct fbwl_ui_tooltip_env tooltip_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_tooltip_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_overlay = server != NULL ? server->layer_overlay : NULL,
        .wl_display = server != NULL ? server->wl_display : NULL,
        .output_layout = server != NULL ? server->output_layout : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
    };
}

void server_tooltip_ui_handle_motion(struct fbwl_server *server) {
    if (server == NULL || server->cursor == NULL) {
        return;
    }

    const int x = (int)server->cursor->x;
    const int y = (int)server->cursor->y;

    const struct fbwl_ui_toolbar_env tb_env = toolbar_ui_env(server);
    const char *text = NULL;
    if (!fbwl_ui_toolbar_tooltip_text_at(&server->toolbar_ui, &tb_env, x, y, &text)) {
        fbwl_ui_tooltip_hide(&server->tooltip_ui, "motion-out");
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_at(server, x, y);
    const int delay_ms = cfg != NULL ? cfg->tooltip_delay_ms : 500;

    const struct fbwl_ui_tooltip_env env = tooltip_ui_env(server);
    fbwl_ui_tooltip_request(&server->tooltip_ui, &env, x, y, delay_ms, text);
}

void server_tabs_ui_handle_motion(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL || server->cursor == NULL) {
        return;
    }
    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        return;
    }
    if (server->cmd_dialog_ui.open || server->menu_ui.open) {
        return;
    }

    double sx = 0, sy = 0;
    struct wlr_surface *surface = NULL;
    struct fbwl_view *view = fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (view == NULL || surface != NULL) {
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const struct fbwl_tabs_config *tabs = cfg != NULL ? &cfg->tabs : &server->tabs;
    if (tabs->focus_model != FBWL_TAB_FOCUS_MOUSE) {
        return;
    }

    int tab_index0 = -1;
    if (!fbwl_view_tabs_index_at(view, server->cursor->x, server->cursor->y, &tab_index0) || tab_index0 < 0) {
        return;
    }

    struct fbwl_view *tab_view = fbwl_tabs_group_mapped_at(view, (size_t)tab_index0);
    if (tab_view == NULL || tab_view->tab_group == NULL) {
        return;
    }
    if (fbwl_tabs_view_is_active(tab_view)) {
        return;
    }

    wlr_log(WLR_INFO, "TabsUI: hover idx=%d title=%s", tab_index0, fbwl_view_display_title(tab_view));
    fbwl_tabs_activate(tab_view, "tab-hover");

    const enum fbwl_focus_reason prev_reason = server->focus_reason;
    server->focus_reason = FBWL_FOCUS_REASON_POINTER_MOTION;
    fbwm_core_focus_view(&server->wm, &tab_view->wm_view);
    server->focus_reason = prev_reason;
}

void server_toolbar_ui_rebuild(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    fbwl_ui_toolbar_rebuild(&server->toolbar_ui, &env);
}

void server_slit_ui_rebuild(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const struct fbwl_ui_slit_env env = slit_ui_env(server);
    fbwl_ui_slit_rebuild(&server->slit_ui, &env);
}

#ifdef HAVE_SYSTEMD
void server_sni_on_change(void *userdata) {
    server_toolbar_ui_rebuild(userdata);
}
#endif

void server_toolbar_ui_update_position(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    fbwl_ui_toolbar_update_position(&server->toolbar_ui, &env);
}

void server_slit_ui_update_position(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const struct fbwl_ui_slit_env env = slit_ui_env(server);
    fbwl_ui_slit_update_position(&server->slit_ui, &env);
}

void server_toolbar_ui_update_iconbar_focus(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_ui_toolbar_update_iconbar_focus(&server->toolbar_ui, &server->decor_theme, server->focused_view);
}

bool server_toolbar_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button) {
    if (server == NULL) {
        return false;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    const struct fbwl_ui_toolbar_hooks hooks = toolbar_ui_hooks(server);
    return fbwl_ui_toolbar_handle_click(&server->toolbar_ui, &env, &hooks, lx, ly, button);
}

bool server_slit_ui_attach_view(struct fbwl_server *server, struct fbwl_view *view, const char *why) {
    if (server == NULL || view == NULL) {
        return false;
    }
    const struct fbwl_ui_slit_env env = slit_ui_env(server);
    bool ok = fbwl_ui_slit_attach_view(&server->slit_ui, &env, view, why);
    if (ok && server->slitlist_file != NULL && *server->slitlist_file != '\0') {
        (void)fbwl_ui_slit_save_order_file(&server->slit_ui, server->slitlist_file);
    }
    return ok;
}

void server_slit_ui_detach_view(struct fbwl_server *server, struct fbwl_view *view, const char *why) {
    if (server == NULL || view == NULL) {
        return;
    }
    const struct fbwl_ui_slit_env env = slit_ui_env(server);
    fbwl_ui_slit_detach_view(&server->slit_ui, &env, view, why);
}

void server_slit_ui_handle_view_commit(struct fbwl_server *server, struct fbwl_view *view, const char *why) {
    if (server == NULL || view == NULL) {
        return;
    }
    const struct fbwl_ui_slit_env env = slit_ui_env(server);
    fbwl_ui_slit_handle_view_commit(&server->slit_ui, &env, view, why);
}

void server_slit_ui_apply_view_geometry(struct fbwl_server *server, struct fbwl_view *view, const char *why) {
    if (server == NULL || view == NULL) {
        return;
    }
    const struct fbwl_ui_slit_env env = slit_ui_env(server);
    fbwl_ui_slit_apply_view_geometry(&server->slit_ui, &env, view, why);
}

static void server_toolbar_buttons_free(struct fbwl_toolbar_button_cfg *buttons, size_t len) {
    if (buttons == NULL) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        struct fbwl_toolbar_button_cfg *cfg = &buttons[i];
        free(cfg->name);
        cfg->name = NULL;
        free(cfg->label);
        cfg->label = NULL;
        for (size_t j = 0; j < FBWL_TOOLBAR_BUTTON_COMMANDS_MAX; j++) {
            free(cfg->commands[j]);
            cfg->commands[j] = NULL;
        }
    }
    free(buttons);
}

static bool toolbar_buttons_reserve(struct fbwl_toolbar_button_cfg **buttons, size_t *cap, size_t need) {
    if (buttons == NULL || cap == NULL) {
        return false;
    }
    if (need <= *cap) {
        return true;
    }
    size_t new_cap = *cap > 0 ? *cap : 4;
    while (new_cap < need) {
        new_cap *= 2;
    }
    struct fbwl_toolbar_button_cfg *tmp = realloc(*buttons, new_cap * sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    for (size_t i = *cap; i < new_cap; i++) {
        tmp[i] = (struct fbwl_toolbar_button_cfg){0};
    }
    *buttons = tmp;
    *cap = new_cap;
    return true;
}

static bool toolbar_buttons_contains_name(const struct fbwl_toolbar_button_cfg *buttons, size_t len, const char *name) {
    if (buttons == NULL || name == NULL) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (buttons[i].name != NULL && strcmp(buttons[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static char *toolbar_dup_trim_range(const char *s, size_t len) {
    if (s == NULL) {
        return NULL;
    }
    while (len > 0 && isspace((unsigned char)*s)) {
        s++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        len--;
    }
    if (len == 0) {
        return NULL;
    }
    char *out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static bool toolbar_commands_parse(const char *s, char *out[static FBWL_TOOLBAR_BUTTON_COMMANDS_MAX]) {
    if (out == NULL) {
        return false;
    }
    for (size_t i = 0; i < FBWL_TOOLBAR_BUTTON_COMMANDS_MAX; i++) {
        out[i] = NULL;
    }
    if (s == NULL || *s == '\0') {
        return true;
    }

    const char *p = s;
    for (size_t i = 0; i < FBWL_TOOLBAR_BUTTON_COMMANDS_MAX; i++) {
        const char *sep = strchr(p, ':');
        size_t seg_len = sep != NULL ? (size_t)(sep - p) : strlen(p);
        out[i] = toolbar_dup_trim_range(p, seg_len);
        if (seg_len > 0 && out[i] == NULL) {
            for (size_t j = 0; j < FBWL_TOOLBAR_BUTTON_COMMANDS_MAX; j++) {
                free(out[j]);
                out[j] = NULL;
            }
            return false;
        }
        if (sep == NULL) {
            break;
        }
        p = sep + 1;
    }

    return true;
}

static const char *toolbar_tools_default_string(void) {
    // Fluxbox/X11 resource default is slightly different from docs depending on version;
    // we match the C++ Toolbar.cc default in this repo for parity.
    return "prevworkspace, workspacename, nextworkspace, iconbar, systemtray, clock";
}

static bool toolbar_tools_list_has_known_tool(char **tools, size_t tools_len) {
    if (tools == NULL || tools_len == 0) {
        return false;
    }
    for (size_t i = 0; i < tools_len; i++) {
        const char *tok = tools[i];
        if (tok == NULL || *tok == '\0') {
            continue;
        }
        if (strcmp(tok, "clock") == 0 ||
                strcmp(tok, "iconbar") == 0 ||
                strcmp(tok, "systemtray") == 0 ||
                strcmp(tok, "workspacename") == 0 ||
                strcmp(tok, "prevworkspace") == 0 ||
                strcmp(tok, "nextworkspace") == 0 ||
                strcmp(tok, "prevwindow") == 0 ||
                strcmp(tok, "nextwindow") == 0) {
            return true;
        }
        if (strncmp(tok, "button.", 7) == 0) {
            return true;
        }
    }
    return false;
}

bool server_toolbar_ui_load_button_tools(struct fbwl_server *server, const struct fbwl_resource_db *init, size_t toolbar_screen) {
    if (server == NULL) {
        return false;
    }

    struct fbwl_toolbar_button_cfg *buttons = NULL;
    size_t buttons_len = 0;
    size_t buttons_cap = 0;

    const char *toolbar_tools = init != NULL ? fbwl_resource_db_get_screen(init, toolbar_screen, "toolbar.tools") : NULL;

    bool tools_changed =
        fbwl_string_list_set(&server->toolbar_ui.tools_order, &server->toolbar_ui.tools_order_len, toolbar_tools);
    if (server->toolbar_ui.tools_order_len == 0 ||
            !toolbar_tools_list_has_known_tool(server->toolbar_ui.tools_order, server->toolbar_ui.tools_order_len)) {
        tools_changed =
            fbwl_string_list_set(&server->toolbar_ui.tools_order, &server->toolbar_ui.tools_order_len,
                toolbar_tools_default_string()) ||
            tools_changed;
    }

    for (size_t i = 0; i < server->toolbar_ui.tools_order_len; i++) {
        const char *tok = server->toolbar_ui.tools_order[i];
        if (tok == NULL || strncmp(tok, "button.", 7) != 0) {
            continue;
        }

        const char *name = tok + 7;
        if (*name == '\0') {
            continue;
        }
        if (toolbar_buttons_contains_name(buttons, buttons_len, name)) {
            continue;
        }

        if (!toolbar_buttons_reserve(&buttons, &buttons_cap, buttons_len + 1)) {
            server_toolbar_buttons_free(buttons, buttons_len);
            return false;
        }

        struct fbwl_toolbar_button_cfg *cfg = &buttons[buttons_len++];
        *cfg = (struct fbwl_toolbar_button_cfg){0};
        cfg->name = strdup(name);
        if (cfg->name == NULL) {
            server_toolbar_buttons_free(buttons, buttons_len);
            return false;
        }

        if (init != NULL) {
            char key[512];
            snprintf(key, sizeof(key), "session.screen%zu.toolbar.button.%s.label", toolbar_screen, name);
            const char *label = fbwl_resource_db_get(init, key);
            if (label != NULL && *label != '\0') {
                cfg->label = strdup(label);
                if (cfg->label == NULL) {
                    server_toolbar_buttons_free(buttons, buttons_len);
                    return false;
                }
            }

            snprintf(key, sizeof(key), "session.screen%zu.toolbar.button.%s.commands", toolbar_screen, name);
            const char *commands = fbwl_resource_db_get(init, key);
            if (!toolbar_commands_parse(commands, cfg->commands)) {
                server_toolbar_buttons_free(buttons, buttons_len);
                return false;
            }
        }
    }

    const bool buttons_changed = fbwl_ui_toolbar_buttons_replace(&server->toolbar_ui, buttons, buttons_len);
    return tools_changed || buttons_changed;
}

void server_cmd_dialog_ui_update_position(struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return;
    }
    fbwl_ui_cmd_dialog_update_position(&server->cmd_dialog_ui, server->output_layout);
}

void server_cmd_dialog_ui_close(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_cmd_dialog_close(&server->cmd_dialog_ui, why);
}

void server_cmd_dialog_ui_open(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL) {
        return;
    }

    server_menu_ui_close(server, "cmd-dialog-open");
    fbwl_ui_cmd_dialog_open(&server->cmd_dialog_ui, server->scene, server->layer_overlay,
        &server->decor_theme, server->output_layout);
}

bool server_cmd_dialog_ui_handle_key(struct fbwl_server *server, xkb_keysym_t sym, uint32_t modifiers) {
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

int server_osd_hide_timer(void *data) {
    struct fbwl_server *server = data;
    server_osd_ui_hide(server, "timer");
    return 0;
}

void server_osd_ui_update_position(struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return;
    }
    fbwl_ui_osd_update_position(&server->osd_ui, server->output_layout);
    fbwl_ui_osd_update_position(&server->move_osd_ui, server->output_layout);
}

void server_osd_ui_destroy(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_osd_destroy(&server->osd_ui);
    fbwl_ui_osd_destroy(&server->move_osd_ui);
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

static void menu_ui_server_action(void *userdata, enum fbwl_menu_server_action action, int arg, const char *cmd) {
    struct fbwl_server *server = userdata;
    server_menu_handle_server_action(server, action, arg, cmd);
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

static void menu_ui_workspace_switch(void *userdata, int workspace0) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    const size_t head = fbwl_server_screen_index_at(server, server->menu_ui.x, server->menu_ui.y);
    server_workspace_switch_on_head(server, head, workspace0, "menu-workspace");
}

static struct fbwl_ui_menu_env menu_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_overlay = server != NULL ? server->layer_overlay : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
        .wl_display = server != NULL ? server->wl_display : NULL,
    };
}

static struct fbwl_ui_menu_hooks menu_ui_hooks(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_hooks){
        .userdata = server,
        .spawn = menu_ui_spawn,
        .terminate = menu_ui_terminate,
        .server_action = menu_ui_server_action,
        .view_close = menu_ui_view_close,
        .view_set_minimized = menu_ui_view_set_minimized,
        .view_set_maximized = menu_ui_view_set_maximized,
        .view_set_fullscreen = menu_ui_view_set_fullscreen,
        .workspace_switch = menu_ui_workspace_switch,
    };
}

static void menu_ui_apply_screen_config(struct fbwl_server *server, int x, int y) {
    if (server == NULL) {
        return;
    }
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_at(server, x, y);
    if (cfg == NULL) {
        return;
    }
    server->menu_ui.menu_delay_ms = cfg->menu.delay_ms;
    server->menu_ui.alpha = cfg->menu.alpha;
}

void server_menu_ui_close(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_menu_close(&server->menu_ui, why);
}

void server_menu_ui_open_root(struct fbwl_server *server, int x, int y, const char *menu_file) {
    if (server == NULL) {
        return;
    }
    struct fbwl_menu *menu = server->root_menu;
    if (menu_file != NULL && *menu_file != '\0') {
        if (!server_menu_load_custom_file(server, menu_file)) { wlr_log(WLR_ERROR, "CustomMenu: failed to load: %s", menu_file); return; }
        menu = server->custom_menu;
    } else if (menu == NULL) {
        server_menu_create_default(server);
        menu = server->root_menu;
    }
    if (menu == NULL) { return; }

    menu_ui_apply_screen_config(server, x, y);
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_root(&server->menu_ui, &env, menu, x, y);
}

void server_menu_ui_open_window(struct fbwl_server *server, struct fbwl_view *view, int x, int y) {
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->window_menu == NULL) {
        server_menu_create_window(server);
    }
    if (server->window_menu == NULL) {
        return;
    }

    menu_ui_apply_screen_config(server, x, y);
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_window(&server->menu_ui, &env, server->window_menu, view, x, y);
}

void server_menu_ui_open_client(struct fbwl_server *server, int x, int y, const char *pattern) {
    if (server == NULL) {
        return;
    }
    fbwl_menu_free(server->client_menu);
    server->client_menu = fbwl_menu_create("Windows");
    if (server->client_menu == NULL) {
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_at(server, x, y);
    const bool use_pixmap = cfg != NULL ? cfg->menu.client_menu_use_pixmap : true;

    char *tmp_pat = (pattern != NULL && *pattern != '\0') ? strdup(pattern) : NULL;
    struct fbwl_iconbar_pattern pat = {0};
    struct fbwl_ui_toolbar_env pat_env = toolbar_ui_env(server);
    if (tmp_pat != NULL) {
        fbwl_iconbar_pattern_parse_inplace(&pat, tmp_pat);
        pat_env.cursor_valid = true; pat_env.cursor_x = (double)x; pat_env.cursor_y = (double)y;
    }

    size_t item_idx = 0;
    for (struct fbwm_view *walk = server->wm.views.next; walk != &server->wm.views; walk = walk->next) {
        struct fbwl_view *view = walk->userdata;
        if (view == NULL || !view->mapped) {
            continue;
        }
        const int view_head = walk->ops != NULL && walk->ops->head != NULL ? walk->ops->head(walk) : 0;
        const size_t head0 = view_head >= 0 ? (size_t)view_head : 0;
        const int cur_ws = fbwm_core_workspace_current_for_head(&server->wm, head0);
        if (walk->workspace != cur_ws && !walk->sticky) {
            continue;
        }
        if (tmp_pat != NULL && !fbwl_client_pattern_matches(&pat, &pat_env, view, cur_ws)) {
            continue;
        }

        char seq[32];
        snprintf(seq, sizeof(seq), "%llu", (unsigned long long)view->create_seq);

        char *icon_path = NULL;
        bool icon_loaded = false;
        if (use_pixmap) {
            icon_path = fbwl_icon_theme_resolve_path(fbwl_view_app_id(view));
            if (icon_path != NULL) {
                struct wlr_buffer *buf = fbwl_ui_menu_icon_buffer_create(icon_path, 16);
                if (buf != NULL) {
                    icon_loaded = true;
                    wlr_buffer_drop(buf);
                }
            }
        }

        (void)fbwl_menu_add_server_action(server->client_menu, fbwl_view_display_title(view), icon_path,
            FBWL_MENU_SERVER_FOCUS_VIEW, 0, seq);

        wlr_log(WLR_INFO, "ClientMenu: item idx=%zu title=%s minimized=%d icon=%d",
            item_idx, fbwl_view_display_title(view), view->minimized ? 1 : 0, icon_loaded ? 1 : 0);

        free(icon_path);
        item_idx++;
    }

    fbwl_iconbar_pattern_free(&pat); free(tmp_pat);

    if (server->client_menu->item_count == 0) {
        (void)fbwl_menu_add_nop(server->client_menu, "(none)", NULL);
    }

    menu_ui_apply_screen_config(server, x, y);
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_root(&server->menu_ui, &env, server->client_menu, x, y);
}

ssize_t server_menu_ui_index_at(const struct fbwl_menu_ui *ui, int lx, int ly) {
    return fbwl_ui_menu_index_at(ui, lx, ly);
}

void server_menu_ui_set_selected(struct fbwl_server *server, size_t idx) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_menu_set_selected(&server->menu_ui, idx);
}

bool server_menu_ui_handle_keypress(struct fbwl_server *server, xkb_keysym_t sym) {
    if (server == NULL) {
        return false;
    }
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    const struct fbwl_ui_menu_hooks hooks = menu_ui_hooks(server);
    return fbwl_ui_menu_handle_keypress(&server->menu_ui, &env, &hooks, sym);
}

bool server_menu_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button) {
    if (server == NULL) {
        return false;
    }
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    const struct fbwl_ui_menu_hooks hooks = menu_ui_hooks(server);
    return fbwl_ui_menu_handle_click(&server->menu_ui, &env, &hooks, lx, ly, button);
}

void server_menu_free(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    server_menu_ui_close(server, "free");
    fbwl_menu_free(server->root_menu);
    server->root_menu = NULL;
    fbwl_menu_free(server->custom_menu);
    server->custom_menu = NULL;
    fbwl_menu_free(server->window_menu);
    server->window_menu = NULL;
    fbwl_menu_free(server->workspace_menu);
    server->workspace_menu = NULL;
    fbwl_menu_free(server->client_menu);
    server->client_menu = NULL;
    fbwl_menu_free(server->slit_menu);
    server->slit_menu = NULL;
    free(server->menu_file);
    server->menu_file = NULL;
    free(server->window_menu_file);
    server->window_menu_file = NULL;
}
