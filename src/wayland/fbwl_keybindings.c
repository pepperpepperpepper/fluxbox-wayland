#include "wayland/fbwl_keybindings.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include "wayland/fbwl_cmdlang.h"
#include "wayland/fbwl_fluxbox_cmd.h"
#include "wayland/fbwl_server_keybinding_actions.h"
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
        free((*bindings)[i].mode);
        (*bindings)[i].mode = NULL;
    }
    free(*bindings);
    *bindings = NULL;
    *count = 0;
}
bool fbwl_keybindings_add(struct fbwl_keybinding **bindings, size_t *count, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
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
    binding->mode = mode != NULL ? strdup(mode) : NULL;
    return true;
}
bool fbwl_keybindings_add_keycode(struct fbwl_keybinding **bindings, size_t *count, uint32_t keycode, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
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
    binding->mode = mode != NULL ? strdup(mode) : NULL;
    return true;
}

bool fbwl_keybindings_add_placeholder(struct fbwl_keybinding **bindings, size_t *count, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
    if (bindings == NULL || count == NULL) {
        return false;
    }
    struct fbwl_keybinding *tmp = realloc(*bindings, (*count + 1) * sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    *bindings = tmp;
    struct fbwl_keybinding *binding = &(*bindings)[(*count)++];
    binding->key_kind = FBWL_KEYBIND_PLACEHOLDER;
    binding->sym = XKB_KEY_NoSymbol;
    binding->keycode = 0;
    binding->modifiers = modifiers & FBWL_KEYMOD_MASK;
    binding->action = action;
    binding->arg = arg;
    binding->cmd = cmd != NULL ? strdup(cmd) : NULL;
    binding->mode = mode != NULL ? strdup(mode) : NULL;
    return true;
}

bool fbwl_keybindings_add_change_workspace(struct fbwl_keybinding **bindings, size_t *count,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
    if (bindings == NULL || count == NULL) {
        return false;
    }
    struct fbwl_keybinding *tmp = realloc(*bindings, (*count + 1) * sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    *bindings = tmp;
    struct fbwl_keybinding *binding = &(*bindings)[(*count)++];
    binding->key_kind = FBWL_KEYBIND_CHANGE_WORKSPACE;
    binding->sym = XKB_KEY_NoSymbol;
    binding->keycode = 0;
    binding->modifiers = 0;
    binding->action = action;
    binding->arg = arg;
    binding->cmd = cmd != NULL ? strdup(cmd) : NULL;
    binding->mode = mode != NULL ? strdup(mode) : NULL;
    return true;
}
void fbwl_keybindings_add_defaults(struct fbwl_keybinding **bindings, size_t *count, const char *terminal_cmd) {
    fbwl_keybindings_add(bindings, count, XKB_KEY_Escape, WLR_MODIFIER_ALT, FBWL_KEYBIND_EXIT, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_Return, WLR_MODIFIER_ALT, FBWL_KEYBIND_EXEC, 0, terminal_cmd, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_F2, WLR_MODIFIER_ALT, FBWL_KEYBIND_COMMAND_DIALOG, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_F1, WLR_MODIFIER_ALT, FBWL_KEYBIND_FOCUS_NEXT, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_m, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_MAXIMIZE, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_f, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_FULLSCREEN, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_i, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_MINIMIZE, 0, NULL, NULL);
    for (int i = 0; i < 9; i++) {
        fbwl_keybindings_add(bindings, count, XKB_KEY_1 + i, WLR_MODIFIER_ALT, FBWL_KEYBIND_WORKSPACE_SWITCH, i, NULL, NULL);
        fbwl_keybindings_add(bindings, count, XKB_KEY_1 + i, WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL,
            FBWL_KEYBIND_SEND_TO_WORKSPACE, i, NULL, NULL);
    }
}
static bool mode_is_default(const char *mode) {
    return mode == NULL || *mode == '\0' || strcasecmp(mode, "default") == 0;
}
static bool mode_matches(const char *binding_mode, const char *current_mode) {
    const bool binding_default = mode_is_default(binding_mode);
    const bool current_default = mode_is_default(current_mode);
    if (binding_default && current_default) {
        return true;
    }
    if (binding_default || current_default) {
        return false;
    }
    return strcmp(binding_mode, current_mode) == 0;
}
bool fbwl_keybindings_handle(const struct fbwl_keybinding *bindings, size_t count, uint32_t keycode, xkb_keysym_t sym,
        uint32_t modifiers, const struct fbwl_keybindings_hooks *hooks) {
    if (bindings == NULL || count == 0 || hooks == NULL) {
        return false;
    }
    sym = xkb_keysym_to_lower(sym);
    modifiers &= FBWL_KEYMOD_MASK;
    const struct fbwl_keybinding *placeholder = NULL;
    for (size_t i = count; i-- > 0;) {
        const struct fbwl_keybinding *binding = &bindings[i];
        if (!mode_matches(binding->mode, hooks->key_mode)) {
            continue;
        }
        if (binding->modifiers != modifiers) {
            continue;
        }
        if (binding->key_kind == FBWL_KEYBIND_PLACEHOLDER) {
            if (placeholder == NULL) {
                placeholder = binding;
            }
            continue;
        }
        if (binding->key_kind == FBWL_KEYBIND_KEYCODE) {
            if (binding->keycode == keycode) {
                struct fbwl_keybindings_hooks tmp = *hooks;
                tmp.cmdlang_scope = binding;
                return fbwl_keybindings_execute_action(binding->action, binding->arg, binding->cmd, NULL, &tmp);
            }
            continue;
        }
        if (binding->sym == sym) {
            struct fbwl_keybindings_hooks tmp = *hooks;
            tmp.cmdlang_scope = binding;
            return fbwl_keybindings_execute_action(binding->action, binding->arg, binding->cmd, NULL, &tmp);
        }
    }
    if (placeholder != NULL) {
        struct fbwl_keybindings_hooks tmp = *hooks;
        tmp.placeholder_keycode = keycode;
        tmp.placeholder_sym = sym;
        tmp.cmdlang_scope = placeholder;
        return fbwl_keybindings_execute_action(placeholder->action, placeholder->arg, placeholder->cmd, NULL, &tmp);
    }
    return false;
}

bool fbwl_keybindings_handle_change_workspace(const struct fbwl_keybinding *bindings, size_t count,
        const struct fbwl_keybindings_hooks *hooks) {
    if (bindings == NULL || count == 0 || hooks == NULL) {
        return false;
    }
    for (size_t i = count; i-- > 0;) {
        const struct fbwl_keybinding *binding = &bindings[i];
        if (binding->key_kind != FBWL_KEYBIND_CHANGE_WORKSPACE) {
            continue;
        }
        if (binding->modifiers != 0) {
            continue;
        }
        if (!mode_matches(binding->mode, hooks->key_mode)) {
            continue;
        }
        struct fbwl_keybindings_hooks tmp = *hooks;
        tmp.cmdlang_scope = binding;
        return fbwl_keybindings_execute_action(binding->action, binding->arg, binding->cmd, NULL, &tmp);
    }
    return false;
}
