#include "wayland/fbwl_keybindings.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_keyboard.h>

#include "wayland/fbwl_view.h"

#define FBWL_KEYMOD_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)

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
    binding->sym = xkb_keysym_to_lower(sym);
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

static bool fbwl_keybindings_execute(const struct fbwl_keybinding *binding, const struct fbwl_keybindings_hooks *hooks) {
    if (binding == NULL || hooks == NULL || hooks->wm == NULL) {
        return false;
    }

    switch (binding->action) {
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
        hooks->spawn(hooks->userdata, binding->cmd);
        return true;
    case FBWL_KEYBIND_COMMAND_DIALOG:
        if (hooks->command_dialog_open == NULL) {
            return false;
        }
        hooks->command_dialog_open(hooks->userdata);
        return true;
    case FBWL_KEYBIND_FOCUS_NEXT:
        fbwm_core_focus_next(hooks->wm);
        return true;
    case FBWL_KEYBIND_TOGGLE_MAXIMIZE: {
        if (hooks->view_set_maximized == NULL) {
            return false;
        }
        struct fbwm_view *wm_view = hooks->wm->focused;
        if (wm_view != NULL) {
            struct fbwl_view *view = wm_view->userdata;
            if (view != NULL) {
                hooks->view_set_maximized(hooks->userdata, view, !view->maximized);
            }
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_FULLSCREEN: {
        if (hooks->view_set_fullscreen == NULL) {
            return false;
        }
        struct fbwm_view *wm_view = hooks->wm->focused;
        if (wm_view != NULL) {
            struct fbwl_view *view = wm_view->userdata;
            if (view != NULL) {
                hooks->view_set_fullscreen(hooks->userdata, view, !view->fullscreen);
            }
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_MINIMIZE: {
        if (hooks->view_set_minimized == NULL) {
            return false;
        }
        struct fbwl_view *view = NULL;
        struct fbwm_view *wm_view = hooks->wm->focused;
        if (wm_view != NULL) {
            view = wm_view->userdata;
        }

        if (view == NULL) {
            const int cur = fbwm_core_workspace_current(hooks->wm);
            for (struct fbwm_view *walk = hooks->wm->views.next; walk != &hooks->wm->views; walk = walk->next) {
                struct fbwl_view *candidate = walk->userdata;
                if (candidate != NULL && candidate->mapped && candidate->minimized &&
                        (walk->sticky || walk->workspace == cur)) {
                    view = candidate;
                    break;
                }
            }
        }

        if (view != NULL) {
            hooks->view_set_minimized(hooks->userdata, view, !view->minimized, "keybinding");
        }
        return true;
    }
    case FBWL_KEYBIND_WORKSPACE_SWITCH:
        fbwm_core_workspace_switch(hooks->wm, binding->arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch");
        }
        return true;
    case FBWL_KEYBIND_SEND_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(hooks->wm, binding->arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        return true;
    case FBWL_KEYBIND_TAKE_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(hooks->wm, binding->arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        fbwm_core_workspace_switch(hooks->wm, binding->arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch");
        }
        return true;
    default:
        return false;
    }
}

bool fbwl_keybindings_handle(const struct fbwl_keybinding *bindings, size_t count, xkb_keysym_t sym,
        uint32_t modifiers, const struct fbwl_keybindings_hooks *hooks) {
    if (bindings == NULL || count == 0) {
        return false;
    }

    sym = xkb_keysym_to_lower(sym);
    modifiers &= FBWL_KEYMOD_MASK;
    for (size_t i = count; i-- > 0;) {
        const struct fbwl_keybinding *binding = &bindings[i];
        if (binding->sym == sym && binding->modifiers == modifiers) {
            return fbwl_keybindings_execute(binding, hooks);
        }
    }
    return false;
}
