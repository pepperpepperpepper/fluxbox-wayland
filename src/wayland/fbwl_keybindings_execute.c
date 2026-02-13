#include "wayland/fbwl_keybindings.h"
#include <ctype.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include "wayland/fbwl_cmdlang.h"
#include "wayland/fbwl_fluxbox_cmd.h"
#include "wayland/fbwl_server_keybinding_actions.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_view.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int wrap_workspace(int ws, int count) {
    if (count < 1) {
        return 0;
    }
    while (ws < 0) {
        ws += count;
    }
    while (ws >= count) {
        ws -= count;
    }
    return ws;
}

static int hooks_workspace_current(const struct fbwl_keybindings_hooks *hooks) {
    if (hooks == NULL || hooks->wm == NULL) {
        return 0;
    }
    if (hooks->workspace_current != NULL) {
        return hooks->workspace_current(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
    }
    return fbwm_core_workspace_current(hooks->wm);
}

static void hooks_workspace_switch(const struct fbwl_keybindings_hooks *hooks, int workspace0, const char *why) {
    if (hooks == NULL || hooks->wm == NULL) {
        return;
    }
    if (hooks->workspace_switch != NULL) {
        hooks->workspace_switch(hooks->userdata, hooks->cursor_x, hooks->cursor_y, workspace0, why);
        return;
    }
    fbwm_core_workspace_switch(hooks->wm, workspace0);
    if (hooks->apply_workspace_visibility != NULL) {
        hooks->apply_workspace_visibility(hooks->userdata, why);
    }
}
static struct fbwl_view *resolve_target_view(struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks) {
    if (target_view != NULL) {
        return target_view;
    }
    if (hooks == NULL || hooks->wm == NULL || hooks->wm->focused == NULL) {
        return NULL;
    }
    return hooks->wm->focused->userdata;
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
static void parse_cycle_options_inplace(char *s, bool *out_groups, bool *out_static, char **out_pattern) {
    if (out_groups != NULL) {
        *out_groups = false;
    }
    if (out_static != NULL) {
        *out_static = false;
    }
    if (out_pattern != NULL) {
        *out_pattern = s;
    }
    if (s == NULL) {
        return;
    }
    char *open = strchr(s, '{');
    if (open == NULL) {
        if (out_pattern != NULL) {
            *out_pattern = trim_inplace(s);
        }
        return;
    }
    char *close = strchr(open + 1, '}');
    if (close == NULL) {
        if (out_pattern != NULL) {
            *out_pattern = trim_inplace(s);
        }
        return;
    }
    *open = '\0';
    *close = '\0';
    char *opts = trim_inplace(open + 1);
    if (opts != NULL && *opts != '\0') {
        char *save = NULL;
        for (char *tok = strtok_r(opts, " \t", &save); tok != NULL; tok = strtok_r(NULL, " \t", &save)) {
            if (out_groups != NULL && strcasecmp(tok, "groups") == 0) {
                *out_groups = true;
                continue;
            }
            if (out_static != NULL && strcasecmp(tok, "static") == 0) {
                *out_static = true;
                continue;
            }
        }
    }
    if (out_pattern != NULL) {
        *out_pattern = trim_inplace(close + 1);
    }
}
static uint32_t resize_edges_from_arg(const struct fbwl_view *view, int cursor_x, int cursor_y, const char *arg) {
    if (arg != NULL) {
        if (strcasecmp(arg, "topleft") == 0) {
            return WLR_EDGE_TOP | WLR_EDGE_LEFT;
        }
        if (strcasecmp(arg, "topright") == 0) {
            return WLR_EDGE_TOP | WLR_EDGE_RIGHT;
        }
        if (strcasecmp(arg, "bottomleft") == 0) {
            return WLR_EDGE_BOTTOM | WLR_EDGE_LEFT;
        }
        if (strcasecmp(arg, "bottomright") == 0) {
            return WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
        }
        if (strcasecmp(arg, "nearestcorner") == 0) {
            if (view != NULL) {
                const int w = fbwl_view_current_width(view);
                const int h = fbwl_view_current_height(view);
                const int mid_x = view->x + w / 2;
                const int mid_y = view->y + h / 2;
                const bool left = cursor_x < mid_x;
                const bool top = cursor_y < mid_y;
                return (left ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT) | (top ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM);
            }
        }
    }
    return WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
}
static bool execute_action_depth(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (hooks == NULL || hooks->wm == NULL) {
        return false;
    }
    struct fbwl_view *view = resolve_target_view(target_view, hooks);
    switch (action) {
    case FBWL_KEYBIND_EXIT:
        if (hooks->terminate == NULL) {
            return false;
        }
        hooks->terminate(hooks->userdata);
        return true;
    case FBWL_KEYBIND_RESTART:
        if (hooks->restart == NULL) {
            return false;
        }
        hooks->restart(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_EXEC:
        if (hooks->spawn == NULL) {
            return false;
        }
        hooks->spawn(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_SET_ENV:
        server_keybindings_set_env(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_COMMAND_DIALOG:
        if (hooks->command_dialog_open == NULL) {
            return false;
        }
        hooks->command_dialog_open(hooks->userdata);
        return true;
    case FBWL_KEYBIND_RECONFIGURE:
        if (hooks->reconfigure == NULL) {
            return false;
        }
        hooks->reconfigure(hooks->userdata);
        return true;
    case FBWL_KEYBIND_RELOAD_STYLE:
        server_keybindings_reload_style(hooks->userdata);
        return true;
    case FBWL_KEYBIND_SET_STYLE:
        server_keybindings_set_style(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_SAVE_RC:
        server_keybindings_save_rc(hooks->userdata);
        return true;
    case FBWL_KEYBIND_SET_RESOURCE_VALUE:
        server_keybindings_set_resource_value(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_SET_RESOURCE_VALUE_DIALOG:
        server_keybindings_set_resource_value_dialog(hooks->userdata);
        return true;
    case FBWL_KEYBIND_KEYMODE:
        if (hooks->key_mode_set == NULL) {
            return false;
        }
        hooks->key_mode_set(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_BIND_KEY:
        server_keybindings_bind_key(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_IF:
        return fbwl_cmdlang_execute_if(cmd, view, hooks, depth, execute_action_depth);
    case FBWL_KEYBIND_FOREACH:
        return fbwl_cmdlang_execute_foreach(cmd, view, hooks, depth, execute_action_depth);
    case FBWL_KEYBIND_TOGGLECMD:
        return fbwl_cmdlang_execute_togglecmd(cmd, view, hooks, depth, execute_action_depth);
    case FBWL_KEYBIND_DELAY:
        return fbwl_cmdlang_execute_delay(cmd, view, hooks, depth, execute_action_depth);
    case FBWL_KEYBIND_FOCUS_NEXT: {
        if (cmd == NULL || *cmd == '\0') {
            struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, false, false, false, NULL);
            if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
                if (candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                    fbwl_tabs_activate(candidate, "keybinding-nextwindow");
                }
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle");
            }
            return true;
        }
        char *tmp = strdup(cmd);
        if (tmp == NULL) {
            struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, false, false, false, NULL);
            if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
                if (candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                    fbwl_tabs_activate(candidate, "keybinding-nextwindow");
                }
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle");
            }
            return true;
        }
        bool groups = false;
        bool static_order = false;
        char *pattern = NULL;
        parse_cycle_options_inplace(tmp, &groups, &static_order, &pattern);
        struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, false, groups, static_order, pattern);
        if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
            if (!groups && candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                fbwl_tabs_activate(candidate, "keybinding-nextwindow");
            }
            fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle");
        }
        free(tmp);
        return true;
    }
    case FBWL_KEYBIND_FOCUS_PREV: {
        if (cmd == NULL || *cmd == '\0') {
            struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, true, false, false, NULL);
            if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
                if (candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                    fbwl_tabs_activate(candidate, "keybinding-prevwindow");
                }
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-rev");
            }
            return true;
        }
        char *tmp = strdup(cmd);
        if (tmp == NULL) {
            struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, true, false, false, NULL);
            if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
                if (candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                    fbwl_tabs_activate(candidate, "keybinding-prevwindow");
                }
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-rev");
            }
            return true;
        }
        bool groups = false;
        bool static_order = false;
        char *pattern = NULL;
        parse_cycle_options_inplace(tmp, &groups, &static_order, &pattern);
        struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, true, groups, static_order, pattern);
        if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
            if (!groups && candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                fbwl_tabs_activate(candidate, "keybinding-prevwindow");
            }
            fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-rev");
        }
        free(tmp);
        return true;
    }
    case FBWL_KEYBIND_FOCUS_NEXT_GROUP: {
        if (cmd == NULL || *cmd == '\0') {
            struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, false, true, false, NULL);
            if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-group");
            }
            return true;
        }
        char *tmp = strdup(cmd);
        if (tmp == NULL) {
            struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, false, true, false, NULL);
            if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-group");
            }
            return true;
        }
        bool groups = false;
        bool static_order = false;
        char *pattern = NULL;
        parse_cycle_options_inplace(tmp, &groups, &static_order, &pattern);
        groups = true;
        struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, false, groups, static_order, pattern);
        if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
            fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-group");
        }
        free(tmp);
        return true;
    }
    case FBWL_KEYBIND_FOCUS_PREV_GROUP: {
        if (cmd == NULL || *cmd == '\0') {
            struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, true, true, false, NULL);
            if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-group-rev");
            }
            return true;
        }
        char *tmp = strdup(cmd);
        if (tmp == NULL) {
            struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, true, true, false, NULL);
            if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-group-rev");
            }
            return true;
        }
        bool groups = false;
        bool static_order = false;
        char *pattern = NULL;
        parse_cycle_options_inplace(tmp, &groups, &static_order, &pattern);
        groups = true;
        struct fbwl_view *candidate = fbwl_keybindings_pick_cycle_candidate(hooks, true, groups, static_order, pattern);
        if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
            fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "cycle-group-rev");
        }
        free(tmp);
        return true;
    }
    case FBWL_KEYBIND_GOTO_WINDOW: {
        if (arg == 0) {
            return true;
        }
        if (cmd == NULL || *cmd == '\0') {
            struct fbwl_view *candidate = fbwl_keybindings_pick_goto_candidate(hooks, arg, false, false, NULL);
            if (candidate != NULL) {
                if (candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                    fbwl_tabs_activate(candidate, "keybinding-gotowindow");
                }
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "goto-window");
                if (hooks->view_raise != NULL) {
                    hooks->view_raise(hooks->userdata, candidate, "goto-window");
                }
            }
            return true;
        }
        char *tmp = strdup(cmd);
        if (tmp == NULL) {
            struct fbwl_view *candidate = fbwl_keybindings_pick_goto_candidate(hooks, arg, false, false, NULL);
            if (candidate != NULL) {
                if (candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                    fbwl_tabs_activate(candidate, "keybinding-gotowindow");
                }
                fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "goto-window");
                if (hooks->view_raise != NULL) {
                    hooks->view_raise(hooks->userdata, candidate, "goto-window");
                }
            }
            return true;
        }
        bool groups = false;
        bool static_order = false;
        char *pattern = NULL;
        parse_cycle_options_inplace(tmp, &groups, &static_order, &pattern);
        struct fbwl_view *candidate = fbwl_keybindings_pick_goto_candidate(hooks, arg, groups, static_order, pattern);
        if (candidate != NULL) {
            if (!groups && candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                fbwl_tabs_activate(candidate, "keybinding-gotowindow");
            }
            fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "goto-window");
            if (hooks->view_raise != NULL) {
                hooks->view_raise(hooks->userdata, candidate, "goto-window");
            }
        }
        free(tmp);
        return true;
    }
    case FBWL_KEYBIND_ATTACH: {
        if (hooks->views_attach_pattern == NULL) {
            return false;
        }
        hooks->views_attach_pattern(hooks->userdata, cmd != NULL ? cmd : "", hooks->cursor_x, hooks->cursor_y);
        return true;
    }
    case FBWL_KEYBIND_SHOW_DESKTOP: {
        if (hooks->show_desktop == NULL) {
            return false;
        }
        hooks->show_desktop(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    }
    case FBWL_KEYBIND_ARRANGE_WINDOWS: {
        if (hooks->arrange_windows == NULL) {
            return false;
        }
        hooks->arrange_windows(hooks->userdata, arg, cmd != NULL ? cmd : "", hooks->cursor_x, hooks->cursor_y);
        return true;
    }
    case FBWL_KEYBIND_UNCLUTTER: {
        if (hooks->unclutter == NULL) {
            return false;
        }
        hooks->unclutter(hooks->userdata, cmd != NULL ? cmd : "", hooks->cursor_x, hooks->cursor_y);
        return true;
    }
    case FBWL_KEYBIND_TAB_NEXT: {
        if (view == NULL || view->tab_group == NULL) {
            return true;
        }
        struct fbwl_view *next = fbwl_tabs_pick_next(view);
        if (next == NULL) {
            return true;
        }
        fbwl_tabs_activate(next, "keybinding-nexttab");
        fbwm_core_focus_view(hooks->wm, &next->wm_view);
        return true;
    }
    case FBWL_KEYBIND_TAB_PREV: {
        if (view == NULL || view->tab_group == NULL) {
            return true;
        }
        struct fbwl_view *prev = fbwl_tabs_pick_prev(view);
        if (prev == NULL) {
            return true;
        }
        fbwl_tabs_activate(prev, "keybinding-prevtab");
        fbwm_core_focus_view(hooks->wm, &prev->wm_view);
        return true;
    }
    case FBWL_KEYBIND_TAB_GOTO: {
        if (view == NULL || view->tab_group == NULL) {
            return true;
        }
        struct fbwl_view *pick = fbwl_tabs_pick_index0(view, arg);
        if (pick == NULL) {
            return true;
        }
        fbwl_tabs_activate(pick, "keybinding-tab");
        fbwm_core_focus_view(hooks->wm, &pick->wm_view);
        return true;
    }
    case FBWL_KEYBIND_TAB_ACTIVATE: {
        if (view == NULL || view->tab_group == NULL) {
            return true;
        }
        int tab_index0 = -1;
        if (!fbwl_view_tabs_index_at(view, hooks->cursor_x, hooks->cursor_y, &tab_index0) || tab_index0 < 0) {
            return true;
        }
        struct fbwl_view *tab_view = fbwl_tabs_group_mapped_at(view, (size_t)tab_index0);
        if (tab_view == NULL) {
            return true;
        }
        fbwl_tabs_activate(tab_view, "keybinding-activatetab");
        fbwm_core_focus_view(hooks->wm, &tab_view->wm_view);
        if (hooks->view_raise != NULL) {
            hooks->view_raise(hooks->userdata, tab_view, "activatetab");
        }
        return true;
    }
    case FBWL_KEYBIND_MOVE_TAB_LEFT:
        (void)fbwl_tabs_move_left(view, "keybinding-movetableft");
        return true;
    case FBWL_KEYBIND_MOVE_TAB_RIGHT:
        (void)fbwl_tabs_move_right(view, "keybinding-movetabright");
        return true;
    case FBWL_KEYBIND_DETACH_CLIENT:
        if (view != NULL) {
            fbwl_tabs_detach(view, "keybinding-detachclient");
            fbwm_core_focus_view(hooks->wm, &view->wm_view);
            if (hooks->view_raise != NULL) {
                hooks->view_raise(hooks->userdata, view, "detachclient");
            }
        }
        return true;
    case FBWL_KEYBIND_TOGGLE_MAXIMIZE: {
        if (hooks->view_set_maximized == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_set_maximized(hooks->userdata, view, !view->maximized);
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_MAXIMIZE_HORIZONTAL: {
        if (hooks->view_toggle_maximize_horizontal == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_toggle_maximize_horizontal(hooks->userdata, view);
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_MAXIMIZE_VERTICAL: {
        if (hooks->view_toggle_maximize_vertical == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_toggle_maximize_vertical(hooks->userdata, view);
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_FULLSCREEN: {
        if (hooks->view_set_fullscreen == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_set_fullscreen(hooks->userdata, view, !view->fullscreen);
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_MINIMIZE: {
        if (hooks->view_set_minimized == NULL) {
            return false;
        }
        struct fbwl_view *min_view = view;
        if (view == NULL) {
            const int cur = hooks_workspace_current(hooks);
            for (struct fbwm_view *walk = hooks->wm->views.next; walk != &hooks->wm->views; walk = walk->next) {
                struct fbwl_view *candidate = walk->userdata;
                if (candidate != NULL && candidate->mapped && candidate->minimized &&
                        (walk->sticky || walk->workspace == cur)) {
                    min_view = candidate;
                    break;
                }
            }
        }
        if (min_view != NULL) {
            hooks->view_set_minimized(hooks->userdata, min_view, !min_view->minimized, "keybinding");
        }
        return true;
    }
    case FBWL_KEYBIND_DEICONIFY:
        server_keybindings_deiconify(hooks->userdata, cmd, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_WORKSPACE_SWITCH:
        hooks_workspace_switch(hooks, arg, "switch");
        return true;
    case FBWL_KEYBIND_WORKSPACE_NEXT: {
        if (arg == 0 && cmd == NULL) {
            server_keybindings_workspace_toggle_prev(hooks->userdata, hooks->cursor_x, hooks->cursor_y, "switch-toggle");
            return true;
        }
        const int cur = hooks_workspace_current(hooks);
        const int count = fbwm_core_workspace_count(hooks->wm);
        int ws = cur + arg;
        if (cmd != NULL && strcmp(cmd, "nowrap") == 0) { if (ws < 0) ws = 0; if (ws >= count) ws = count - 1; }
        else { ws = wrap_workspace(ws, count); }
        hooks_workspace_switch(hooks, ws, "switch-next");
        return true;
    }
    case FBWL_KEYBIND_WORKSPACE_PREV: {
        if (arg == 0 && cmd == NULL) {
            server_keybindings_workspace_toggle_prev(hooks->userdata, hooks->cursor_x, hooks->cursor_y, "switch-toggle");
            return true;
        }
        const int cur = hooks_workspace_current(hooks);
        const int count = fbwm_core_workspace_count(hooks->wm);
        int ws = cur - arg;
        if (cmd != NULL && strcmp(cmd, "nowrap") == 0) { if (ws < 0) ws = 0; if (ws >= count) ws = count - 1; }
        else { ws = wrap_workspace(ws, count); }
        hooks_workspace_switch(hooks, ws, "switch-prev");
        return true;
    }
    case FBWL_KEYBIND_ADD_WORKSPACE:
        server_keybindings_add_workspace(hooks->userdata);
        return true;
    case FBWL_KEYBIND_REMOVE_LAST_WORKSPACE:
        server_keybindings_remove_last_workspace(hooks->userdata);
        return true;
    case FBWL_KEYBIND_SET_WORKSPACE_NAME:
        server_keybindings_set_workspace_name(hooks->userdata, cmd, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_SET_WORKSPACE_NAME_DIALOG:
        server_keybindings_set_workspace_name_dialog(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_SEND_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        return true;
    case FBWL_KEYBIND_TAKE_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(hooks->wm, arg);
        hooks_workspace_switch(hooks, arg, "switch");
        return true;
    case FBWL_KEYBIND_SEND_TO_REL_WORKSPACE: {
        const int cur = hooks_workspace_current(hooks);
        const int count = fbwm_core_workspace_count(hooks->wm);
        fbwm_core_move_focused_to_workspace(hooks->wm, wrap_workspace(cur + arg, count));
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        return true;
    }
    case FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE: {
        const int cur = hooks_workspace_current(hooks);
        const int count = fbwm_core_workspace_count(hooks->wm);
        const int ws = wrap_workspace(cur + arg, count);
        fbwm_core_move_focused_to_workspace(hooks->wm, ws);
        hooks_workspace_switch(hooks, ws, "switch");
        return true;
    }
    case FBWL_KEYBIND_SET_HEAD:
        server_keybindings_view_set_head(hooks->userdata, view, arg);
        return true;
    case FBWL_KEYBIND_SEND_TO_REL_HEAD:
        server_keybindings_view_send_to_rel_head(hooks->userdata, view, arg);
        return true;
    case FBWL_KEYBIND_CLOSE:
    case FBWL_KEYBIND_KILL:
        if (hooks->view_close == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_close(hooks->userdata, view, action == FBWL_KEYBIND_KILL);
        }
        return true;
    case FBWL_KEYBIND_WINDOW_MENU:
        if (hooks->menu_open_window == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->menu_open_window(hooks->userdata, view, hooks->cursor_x, hooks->cursor_y);
        }
        return true;
    case FBWL_KEYBIND_ROOT_MENU:
        if (hooks->menu_open_root == NULL) {
            return false;
        }
        hooks->menu_open_root(hooks->userdata, hooks->cursor_x, hooks->cursor_y, cmd);
        return true;
    case FBWL_KEYBIND_WORKSPACE_MENU:
        if (hooks->menu_open_workspace == NULL) {
            return false;
        }
        hooks->menu_open_workspace(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_CLIENT_MENU:
        if (hooks->menu_open_client == NULL) {
            return false;
        }
        hooks->menu_open_client(hooks->userdata, hooks->cursor_x, hooks->cursor_y, cmd);
        return true;
    case FBWL_KEYBIND_TOGGLE_TOOLBAR_HIDDEN:
        server_keybindings_toggle_toolbar_hidden(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_TOGGLE_TOOLBAR_ABOVE:
        server_keybindings_toggle_toolbar_above(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_TOGGLE_SLIT_HIDDEN:
        server_keybindings_toggle_slit_hidden(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_TOGGLE_SLIT_ABOVE:
        server_keybindings_toggle_slit_above(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_HIDE_MENUS:
        if (hooks->menu_close == NULL) {
            return false;
        }
        hooks->menu_close(hooks->userdata, "binding");
        return true;
    case FBWL_KEYBIND_RAISE:
        if (hooks->view_raise == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_raise(hooks->userdata, view, "binding");
        }
        return true;
    case FBWL_KEYBIND_LOWER:
        if (hooks->view_lower == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_lower(hooks->userdata, view, "binding");
        }
        return true;
    case FBWL_KEYBIND_RAISE_LAYER:
        if (view == NULL) {
            return true;
        }
        if (arg > 0) {
            if (hooks->view_raise_layer == NULL) {
                return false;
            }
            for (int i = 0; i < arg; i++) {
                struct wlr_scene_tree *before = view->base_layer;
                hooks->view_raise_layer(hooks->userdata, view);
                if (view->base_layer == before) {
                    break;
                }
            }
            return true;
        }
        if (arg < 0) {
            if (hooks->view_lower_layer == NULL) {
                return false;
            }
            const int steps = -arg;
            for (int i = 0; i < steps; i++) {
                struct wlr_scene_tree *before = view->base_layer;
                hooks->view_lower_layer(hooks->userdata, view);
                if (view->base_layer == before) {
                    break;
                }
            }
            return true;
        }
        return true;
    case FBWL_KEYBIND_LOWER_LAYER:
        if (view == NULL) {
            return true;
        }
        if (arg > 0) {
            if (hooks->view_lower_layer == NULL) {
                return false;
            }
            for (int i = 0; i < arg; i++) {
                struct wlr_scene_tree *before = view->base_layer;
                hooks->view_lower_layer(hooks->userdata, view);
                if (view->base_layer == before) {
                    break;
                }
            }
            return true;
        }
        if (arg < 0) {
            if (hooks->view_raise_layer == NULL) {
                return false;
            }
            const int steps = -arg;
            for (int i = 0; i < steps; i++) {
                struct wlr_scene_tree *before = view->base_layer;
                hooks->view_raise_layer(hooks->userdata, view);
                if (view->base_layer == before) {
                    break;
                }
            }
            return true;
        }
        return true;
    case FBWL_KEYBIND_SET_LAYER:
        if (hooks->view_set_layer == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_set_layer(hooks->userdata, view, arg);
        }
        return true;
    case FBWL_KEYBIND_FOCUS:
        if (view != NULL) {
            fbwm_core_focus_view(hooks->wm, &view->wm_view);
        }
        return true;
    case FBWL_KEYBIND_FOCUS_DIR: {
        if (view == NULL) {
            return true;
        }
        struct fbwl_view *candidate = fbwl_keybindings_pick_dir_focus_candidate(hooks, view, (enum fbwl_focus_dir)arg);
        if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
            fbwm_core_focus_view_with_reason(hooks->wm, &candidate->wm_view, "dirfocus");
        }
        return true;
    }
    case FBWL_KEYBIND_SET_XPROP: {
        if (hooks->view_set_xprop == NULL || view == NULL || cmd == NULL) {
            return false;
        }
        const char *args = cmd;
        while (*args != '\0' && isspace((unsigned char)*args)) {
            args++;
        }
        if (*args == '\0' || args[1] == '\0' || *args == '=') {
            return false;
        }

        const char *eq = strchr(args, '=');
        const char *value = eq != NULL ? eq + 1 : "";
        const size_t name_len = eq != NULL ? (size_t)(eq - args) : strlen(args);
        if (name_len == 0) {
            return false;
        }
        char *name = strndup(args, name_len);
        if (name == NULL) {
            return false;
        }
        hooks->view_set_xprop(hooks->userdata, view, name, value);
        free(name);
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_SHADE:
        if (view != NULL) {
            fbwl_view_set_shaded(view, !view->shaded, "keybinding");
        }
        return true;
    case FBWL_KEYBIND_SHADE_ON:
        if (view != NULL) {
            fbwl_view_set_shaded(view, true, "keybinding");
        }
        return true;
    case FBWL_KEYBIND_SHADE_OFF:
        if (view != NULL) {
            fbwl_view_set_shaded(view, false, "keybinding");
        }
        return true;
    case FBWL_KEYBIND_TOGGLE_STICK:
        if (view != NULL) {
            view->wm_view.sticky = !view->wm_view.sticky;
            wlr_log(WLR_INFO, "Stick: %s %s", fbwl_view_display_title(view), view->wm_view.sticky ? "on" : "off");
            if (hooks->apply_workspace_visibility != NULL) {
                hooks->apply_workspace_visibility(hooks->userdata, view->wm_view.sticky ? "stick-on" : "stick-off");
            }
        }
        return true;
    case FBWL_KEYBIND_STICK_ON:
        if (view != NULL && !view->wm_view.sticky) {
            view->wm_view.sticky = true;
            wlr_log(WLR_INFO, "Stick: %s on", fbwl_view_display_title(view));
            if (hooks->apply_workspace_visibility != NULL) {
                hooks->apply_workspace_visibility(hooks->userdata, "stick-on");
            }
        }
        return true;
    case FBWL_KEYBIND_STICK_OFF:
        if (view != NULL && view->wm_view.sticky) {
            view->wm_view.sticky = false;
            wlr_log(WLR_INFO, "Stick: %s off", fbwl_view_display_title(view));
            if (hooks->apply_workspace_visibility != NULL) {
                hooks->apply_workspace_visibility(hooks->userdata, "stick-off");
            }
        }
        return true;
    case FBWL_KEYBIND_SET_ALPHA:
        if (view != NULL) {
            server_keybindings_view_set_alpha_cmd(hooks->userdata, view, cmd);
        }
        return true;
    case FBWL_KEYBIND_TOGGLE_DECOR:
        if (view != NULL) {
            server_keybindings_view_toggle_decor(hooks->userdata, view);
        }
        return true;
    case FBWL_KEYBIND_SET_DECOR:
        if (view != NULL) {
            server_keybindings_view_set_decor(hooks->userdata, view, cmd);
        }
        return true;
    case FBWL_KEYBIND_SET_TITLE:
        if (view != NULL) {
            server_keybindings_view_set_title(hooks->userdata, view, cmd);
        }
        return true;
    case FBWL_KEYBIND_SET_TITLE_DIALOG:
        if (view != NULL) {
            server_keybindings_view_set_title_dialog(hooks->userdata, view);
        }
        return true;
    case FBWL_KEYBIND_MARK_WINDOW:
        server_keybindings_mark_window(hooks->userdata, view, hooks->placeholder_keycode);
        return true;
    case FBWL_KEYBIND_GOTO_MARKED_WINDOW:
        server_keybindings_goto_marked_window(hooks->userdata, hooks->placeholder_keycode);
        return true;
    case FBWL_KEYBIND_CLOSE_ALL_WINDOWS:
        server_keybindings_close_all_windows(hooks->userdata);
        return true;
    case FBWL_KEYBIND_START_MOVING:
        if (hooks->grab_begin_move == NULL) {
            return false;
        }
        if (view != NULL) {
            fbwm_core_focus_view(hooks->wm, &view->wm_view);
            if (hooks->view_raise != NULL) {
                hooks->view_raise(hooks->userdata, view, "move");
            }
            hooks->grab_begin_move(hooks->userdata, view, hooks->button);
        }
        return true;
    case FBWL_KEYBIND_START_RESIZING:
        if (hooks->grab_begin_resize == NULL) {
            return false;
        }
        if (view != NULL) {
            fbwm_core_focus_view(hooks->wm, &view->wm_view);
            if (hooks->view_raise != NULL) {
                hooks->view_raise(hooks->userdata, view, "resize");
            }
            const uint32_t edges = resize_edges_from_arg(view, hooks->cursor_x, hooks->cursor_y, cmd);
            hooks->grab_begin_resize(hooks->userdata, view, hooks->button, edges);
        }
        return true;
    case FBWL_KEYBIND_START_TABBING:
        if (hooks->grab_begin_tabbing == NULL) {
            return false;
        }
        if (view != NULL) {
            struct fbwl_view *drag_view = view;
            int tab_index0 = -1;
            if (fbwl_view_tabs_index_at(view, hooks->cursor_x, hooks->cursor_y, &tab_index0) && tab_index0 >= 0) {
                struct fbwl_view *tab_view = fbwl_tabs_group_mapped_at(view, (size_t)tab_index0);
                if (tab_view != NULL) {
                    drag_view = tab_view;
                }
            }
            if (drag_view->tab_group != NULL && !fbwl_tabs_view_is_active(drag_view)) {
                fbwl_tabs_activate(drag_view, "keybinding-starttabbing");
            }
            fbwm_core_focus_view(hooks->wm, &drag_view->wm_view);
            if (hooks->view_raise != NULL) {
                hooks->view_raise(hooks->userdata, drag_view, "starttabbing");
            }
            hooks->grab_begin_tabbing(hooks->userdata, drag_view, hooks->button);
        }
        return true;
    case FBWL_KEYBIND_MOVE_TO:
        if (view != NULL) {
            server_keybindings_view_move_to_cmd(hooks->userdata, view, cmd);
        }
        return true;
    case FBWL_KEYBIND_MOVE_REL:
        if (view != NULL) {
            server_keybindings_view_move_rel_cmd(hooks->userdata, view, arg, cmd);
        }
        return true;
    case FBWL_KEYBIND_RESIZE_TO:
        if (view != NULL) {
            server_keybindings_view_resize_to_cmd(hooks->userdata, view, cmd);
        }
        return true;
    case FBWL_KEYBIND_RESIZE_REL:
        if (view != NULL) {
            server_keybindings_view_resize_rel_cmd(hooks->userdata, view, arg, cmd);
        }
        return true;
    case FBWL_KEYBIND_MACRO:
        return fbwl_cmdlang_execute_macro(cmd, target_view, hooks, depth, execute_action_depth);
    default:
        return false;
    }
}
bool fbwl_keybindings_execute_action(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks) {
    return execute_action_depth(action, arg, cmd, target_view, hooks, 0);
}
