#include "wayland/fbwl_mousebindings.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_keyboard.h>

#define FBWL_MOUSEMOD_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | \
    WLR_MODIFIER_MOD2 | WLR_MODIFIER_MOD3 | WLR_MODIFIER_MOD5)

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

static bool context_matches(enum fbwl_mousebinding_context binding, enum fbwl_mousebinding_context actual) {
    if (binding == FBWL_MOUSEBIND_ANY) {
        return true;
    }
    if (binding == actual) {
        return true;
    }
    if (binding == FBWL_MOUSEBIND_WINDOW) {
        return actual == FBWL_MOUSEBIND_TITLEBAR || actual == FBWL_MOUSEBIND_WINDOW_BORDER;
    }
    return false;
}

void fbwl_mousebindings_free(struct fbwl_mousebinding **bindings, size_t *count) {
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

bool fbwl_mousebindings_add(struct fbwl_mousebinding **bindings, size_t *count, enum fbwl_mousebinding_context context,
        int button, uint32_t modifiers, enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
    if (bindings == NULL || count == NULL) {
        return false;
    }
    if (button < 1 || button > 32) {
        return false;
    }

    struct fbwl_mousebinding *tmp = realloc(*bindings, (*count + 1) * sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    *bindings = tmp;

    struct fbwl_mousebinding *binding = &(*bindings)[(*count)++];
    binding->context = context;
    binding->button = button;
    binding->modifiers = modifiers & FBWL_MOUSEMOD_MASK;
    binding->action = action;
    binding->arg = arg;
    binding->cmd = cmd != NULL ? strdup(cmd) : NULL;
    binding->mode = mode != NULL ? strdup(mode) : NULL;
    return true;
}

bool fbwl_mousebindings_handle(const struct fbwl_mousebinding *bindings, size_t count, enum fbwl_mousebinding_context context,
        int button, uint32_t modifiers, struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks) {
    if (bindings == NULL || count == 0 || hooks == NULL) {
        return false;
    }

    modifiers &= FBWL_MOUSEMOD_MASK;
    for (size_t i = count; i-- > 0;) {
        const struct fbwl_mousebinding *binding = &bindings[i];
        if (!mode_matches(binding->mode, hooks->key_mode)) {
            continue;
        }
        if (!context_matches(binding->context, context)) {
            continue;
        }
        if (binding->button != button) {
            continue;
        }
        if (binding->modifiers != modifiers) {
            continue;
        }
        return fbwl_keybindings_execute_action(binding->action, binding->arg, binding->cmd, target_view, hooks);
    }

    return false;
}
