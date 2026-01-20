#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

struct fbwl_view;
struct fbwm_core;

enum fbwl_keybinding_action {
    FBWL_KEYBIND_EXIT,
    FBWL_KEYBIND_EXEC,
    FBWL_KEYBIND_COMMAND_DIALOG,
    FBWL_KEYBIND_FOCUS_NEXT,
    FBWL_KEYBIND_TOGGLE_MAXIMIZE,
    FBWL_KEYBIND_TOGGLE_FULLSCREEN,
    FBWL_KEYBIND_TOGGLE_MINIMIZE,
    FBWL_KEYBIND_WORKSPACE_SWITCH,
    FBWL_KEYBIND_SEND_TO_WORKSPACE,
    FBWL_KEYBIND_TAKE_TO_WORKSPACE,
};

struct fbwl_keybinding {
    xkb_keysym_t sym;
    uint32_t modifiers;
    enum fbwl_keybinding_action action;
    int arg;
    char *cmd;
};

struct fbwl_keybindings_hooks {
    void *userdata;
    struct fbwm_core *wm;
    void (*terminate)(void *userdata);
    void (*spawn)(void *userdata, const char *cmd);
    void (*command_dialog_open)(void *userdata);
    void (*apply_workspace_visibility)(void *userdata, const char *why);
    void (*view_set_maximized)(void *userdata, struct fbwl_view *view, bool maximized);
    void (*view_set_fullscreen)(void *userdata, struct fbwl_view *view, bool fullscreen);
    void (*view_set_minimized)(void *userdata, struct fbwl_view *view, bool minimized, const char *why);
};

void fbwl_keybindings_free(struct fbwl_keybinding **bindings, size_t *count);

bool fbwl_keybindings_add(struct fbwl_keybinding **bindings, size_t *count, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd);

void fbwl_keybindings_add_defaults(struct fbwl_keybinding **bindings, size_t *count, const char *terminal_cmd);

bool fbwl_keybindings_handle(const struct fbwl_keybinding *bindings, size_t count, xkb_keysym_t sym,
        uint32_t modifiers, const struct fbwl_keybindings_hooks *hooks);
