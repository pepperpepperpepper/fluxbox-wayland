#include "wayland/fbwl_keybindings.h"

#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/edges.h>

#include "wayland/fbwl_fluxbox_cmd.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_view.h"

#define FBWL_KEYMOD_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | \
    WLR_MODIFIER_MOD2 | WLR_MODIFIER_MOD3 | WLR_MODIFIER_MOD5)

void fbwl_keybindings_free(struct fbwl_keybinding **bindings, size_t *count) {
    if (bindings == NULL || *bindings == NULL || count == NULL) {
        return;
    }
    for (size_t i = 0; i < *count; i++) {
        free((*bindings)[i].cmd);
        (*bindings)[i].cmd = NULL;
    }
    free(*bindings);
    *bindings = NULL;
    *count = 0;
}

bool fbwl_keybindings_add(struct fbwl_keybinding **bindings, size_t *count, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd) {
    if (bindings == NULL || count == NULL) {
        return false;
    }

    struct fbwl_keybinding *tmp = realloc(*bindings, (*count + 1) * sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    *bindings = tmp;

    struct fbwl_keybinding *binding = &(*bindings)[(*count)++];
    binding->key_kind = FBWL_KEYBIND_KEYSYM;
    binding->sym = xkb_keysym_to_lower(sym);
    binding->keycode = 0;
    binding->modifiers = modifiers & FBWL_KEYMOD_MASK;
    binding->action = action;
    binding->arg = arg;
    binding->cmd = cmd != NULL ? strdup(cmd) : NULL;
    return true;
}

bool fbwl_keybindings_add_keycode(struct fbwl_keybinding **bindings, size_t *count, uint32_t keycode, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd) {
    if (bindings == NULL || count == NULL) {
        return false;
    }

    struct fbwl_keybinding *tmp = realloc(*bindings, (*count + 1) * sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    *bindings = tmp;

    struct fbwl_keybinding *binding = &(*bindings)[(*count)++];
    binding->key_kind = FBWL_KEYBIND_KEYCODE;
    binding->sym = XKB_KEY_NoSymbol;
    binding->keycode = keycode;
    binding->modifiers = modifiers & FBWL_KEYMOD_MASK;
    binding->action = action;
    binding->arg = arg;
    binding->cmd = cmd != NULL ? strdup(cmd) : NULL;
    return true;
}

void fbwl_keybindings_add_defaults(struct fbwl_keybinding **bindings, size_t *count, const char *terminal_cmd) {
    fbwl_keybindings_add(bindings, count, XKB_KEY_Escape, WLR_MODIFIER_ALT, FBWL_KEYBIND_EXIT, 0, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_Return, WLR_MODIFIER_ALT, FBWL_KEYBIND_EXEC, 0, terminal_cmd);
    fbwl_keybindings_add(bindings, count, XKB_KEY_F2, WLR_MODIFIER_ALT, FBWL_KEYBIND_COMMAND_DIALOG, 0, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_F1, WLR_MODIFIER_ALT, FBWL_KEYBIND_FOCUS_NEXT, 0, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_m, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_MAXIMIZE, 0, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_f, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_FULLSCREEN, 0, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_i, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_MINIMIZE, 0, NULL);
    for (int i = 0; i < 9; i++) {
        fbwl_keybindings_add(bindings, count, XKB_KEY_1 + i, WLR_MODIFIER_ALT, FBWL_KEYBIND_WORKSPACE_SWITCH, i, NULL);
        fbwl_keybindings_add(bindings, count, XKB_KEY_1 + i, WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL,
            FBWL_KEYBIND_SEND_TO_WORKSPACE, i, NULL);
    }
}

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

static struct fbwl_view *resolve_target_view(struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks) {
    if (target_view != NULL) {
        return target_view;
    }
    if (hooks == NULL || hooks->wm == NULL || hooks->wm->focused == NULL) {
        return NULL;
    }
    return hooks->wm->focused->userdata;
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
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth);

static bool execute_macro_depth(const char *macro, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (macro == NULL || hooks == NULL) {
        return false;
    }
    if (depth > 8) {
        return false;
    }

    bool any = false;
    const char *p = macro;
    while (*p != '\0') {
        while (*p != '\0' && *p != '{') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char *open = p;
        const char *close = strchr(open + 1, '}');
        if (close == NULL) {
            break;
        }

        size_t len = (size_t)(close - (open + 1));
        char *chunk = strndup(open + 1, len);
        if (chunk != NULL) {
            char *s = chunk;
            while (*s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) {
                s++;
            }

            char *end = s + strlen(s);
            while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
                end[-1] = '\0';
                end--;
            }

            if (*s != '\0') {
                char *name = s;
                while (*s != '\0' && *s != ' ' && *s != '\t') {
                    s++;
                }
                char *args = s;
                if (*s != '\0') {
                    *s = '\0';
                    args = s + 1;
                }
                while (*args != '\0' && (*args == ' ' || *args == '\t')) {
                    args++;
                }

                enum fbwl_keybinding_action sub_action;
                int sub_arg = 0;
                const char *sub_cmd = NULL;
                if (fbwl_fluxbox_cmd_resolve(name, args, &sub_action, &sub_arg, &sub_cmd)) {
                    if (execute_action_depth(sub_action, sub_arg, sub_cmd, target_view, hooks, depth + 1)) {
                        any = true;
                    }
                }
            }
            free(chunk);
        }

        p = close + 1;
    }

    return any;
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
    case FBWL_KEYBIND_EXEC:
        if (hooks->spawn == NULL) {
            return false;
        }
        hooks->spawn(hooks->userdata, cmd);
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
    case FBWL_KEYBIND_FOCUS_NEXT:
        fbwm_core_focus_next(hooks->wm);
        return true;
    case FBWL_KEYBIND_FOCUS_PREV:
        fbwm_core_focus_prev(hooks->wm);
        return true;
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
    case FBWL_KEYBIND_TOGGLE_MAXIMIZE: {
        if (hooks->view_set_maximized == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_set_maximized(hooks->userdata, view, !view->maximized);
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
            const int cur = fbwm_core_workspace_current(hooks->wm);
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
    case FBWL_KEYBIND_WORKSPACE_SWITCH:
        fbwm_core_workspace_switch(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch");
        }
        return true;
    case FBWL_KEYBIND_WORKSPACE_NEXT: {
        const int cur = fbwm_core_workspace_current(hooks->wm);
        const int count = fbwm_core_workspace_count(hooks->wm);
        fbwm_core_workspace_switch(hooks->wm, wrap_workspace(cur + 1, count));
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch-next");
        }
        return true;
    }
    case FBWL_KEYBIND_WORKSPACE_PREV: {
        const int cur = fbwm_core_workspace_current(hooks->wm);
        const int count = fbwm_core_workspace_count(hooks->wm);
        fbwm_core_workspace_switch(hooks->wm, wrap_workspace(cur - 1, count));
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch-prev");
        }
        return true;
    }
    case FBWL_KEYBIND_SEND_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        return true;
    case FBWL_KEYBIND_TAKE_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        fbwm_core_workspace_switch(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch");
        }
        return true;
    case FBWL_KEYBIND_SEND_TO_REL_WORKSPACE: {
        const int cur = fbwm_core_workspace_current(hooks->wm);
        const int count = fbwm_core_workspace_count(hooks->wm);
        fbwm_core_move_focused_to_workspace(hooks->wm, wrap_workspace(cur + arg, count));
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        return true;
    }
    case FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE: {
        const int cur = fbwm_core_workspace_current(hooks->wm);
        const int count = fbwm_core_workspace_count(hooks->wm);
        const int ws = wrap_workspace(cur + arg, count);
        fbwm_core_move_focused_to_workspace(hooks->wm, ws);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        fbwm_core_workspace_switch(hooks->wm, ws);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch");
        }
        return true;
    }
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
        hooks->menu_open_root(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
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
    case FBWL_KEYBIND_FOCUS:
        if (view != NULL) {
            fbwm_core_focus_view(hooks->wm, &view->wm_view);
        }
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
            uint32_t button = hooks->button != 0 ? hooks->button : (uint32_t)BTN_LEFT;
            hooks->grab_begin_move(hooks->userdata, view, button);
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
            uint32_t button = hooks->button != 0 ? hooks->button : (uint32_t)BTN_RIGHT;
            hooks->grab_begin_resize(hooks->userdata, view, button, edges);
        }
        return true;
    case FBWL_KEYBIND_MACRO:
        return execute_macro_depth(cmd, view, hooks, depth);
    default:
        return false;
    }
}

bool fbwl_keybindings_execute_action(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks) {
    return execute_action_depth(action, arg, cmd, target_view, hooks, 0);
}

bool fbwl_keybindings_handle(const struct fbwl_keybinding *bindings, size_t count, uint32_t keycode, xkb_keysym_t sym,
        uint32_t modifiers, const struct fbwl_keybindings_hooks *hooks) {
    if (bindings == NULL || count == 0 || hooks == NULL) {
        return false;
    }

    sym = xkb_keysym_to_lower(sym);
    modifiers &= FBWL_KEYMOD_MASK;
    for (size_t i = count; i-- > 0;) {
        const struct fbwl_keybinding *binding = &bindings[i];
        if (binding->modifiers != modifiers) {
            continue;
        }
        if (binding->key_kind == FBWL_KEYBIND_KEYCODE) {
            if (binding->keycode == keycode) {
                return fbwl_keybindings_execute_action(binding->action, binding->arg, binding->cmd, NULL, hooks);
            }
            continue;
        }
        if (binding->sym == sym) {
            return fbwl_keybindings_execute_action(binding->action, binding->arg, binding->cmd, NULL, hooks);
        }
    }
    return false;
}
