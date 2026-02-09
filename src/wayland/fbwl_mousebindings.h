#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wayland/fbwl_keybindings.h"

enum fbwl_mousebinding_context {
    FBWL_MOUSEBIND_ANY = 0,
    FBWL_MOUSEBIND_DESKTOP,
    FBWL_MOUSEBIND_SLIT,
    FBWL_MOUSEBIND_WINDOW,
    FBWL_MOUSEBIND_TITLEBAR,
    FBWL_MOUSEBIND_TAB,
    FBWL_MOUSEBIND_TOOLBAR,
    FBWL_MOUSEBIND_LEFT_GRIP,
    FBWL_MOUSEBIND_RIGHT_GRIP,
    FBWL_MOUSEBIND_WINDOW_BORDER,
};

enum fbwl_mousebinding_event_kind {
    FBWL_MOUSEBIND_EVENT_PRESS = 0,
    FBWL_MOUSEBIND_EVENT_CLICK,
    FBWL_MOUSEBIND_EVENT_MOVE,
};

struct fbwl_mousebinding {
    enum fbwl_mousebinding_context context;
    enum fbwl_mousebinding_event_kind event_kind;
    int button;
    uint32_t modifiers;
    bool is_double;
    enum fbwl_keybinding_action action;
    int arg;
    char *cmd;
    char *mode;
};

void fbwl_mousebindings_free(struct fbwl_mousebinding **bindings, size_t *count);

bool fbwl_mousebindings_add(struct fbwl_mousebinding **bindings, size_t *count, enum fbwl_mousebinding_context context,
    enum fbwl_mousebinding_event_kind event_kind, int button, uint32_t modifiers, bool is_double,
    enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode);

bool fbwl_mousebindings_handle(const struct fbwl_mousebinding *bindings, size_t count, enum fbwl_mousebinding_context context,
    enum fbwl_mousebinding_event_kind event_kind, int button, uint32_t modifiers, bool is_double,
    struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks);

bool fbwl_mousebindings_has(const struct fbwl_mousebinding *bindings, size_t count, enum fbwl_mousebinding_context context,
    enum fbwl_mousebinding_event_kind event_kind, int button, uint32_t modifiers, const struct fbwl_keybindings_hooks *hooks);
