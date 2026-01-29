#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wayland/fbwl_keybindings.h"

enum fbwl_mousebinding_context {
    FBWL_MOUSEBIND_ANY = 0,
    FBWL_MOUSEBIND_DESKTOP,
    FBWL_MOUSEBIND_WINDOW,
    FBWL_MOUSEBIND_TITLEBAR,
    FBWL_MOUSEBIND_TOOLBAR,
    FBWL_MOUSEBIND_WINDOW_BORDER,
};

struct fbwl_mousebinding {
    enum fbwl_mousebinding_context context;
    int button;
    uint32_t modifiers;
    enum fbwl_keybinding_action action;
    int arg;
    char *cmd;
    char *mode;
};

void fbwl_mousebindings_free(struct fbwl_mousebinding **bindings, size_t *count);

bool fbwl_mousebindings_add(struct fbwl_mousebinding **bindings, size_t *count, enum fbwl_mousebinding_context context,
    int button, uint32_t modifiers, enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode);

bool fbwl_mousebindings_handle(const struct fbwl_mousebinding *bindings, size_t count, enum fbwl_mousebinding_context context,
    int button, uint32_t modifiers, struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks);
