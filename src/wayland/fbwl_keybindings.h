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
    FBWL_KEYBIND_RECONFIGURE,
    FBWL_KEYBIND_FOCUS_NEXT,
    FBWL_KEYBIND_FOCUS_PREV,
    FBWL_KEYBIND_TAB_NEXT,
    FBWL_KEYBIND_TAB_PREV,
    FBWL_KEYBIND_TAB_GOTO,
    FBWL_KEYBIND_TOGGLE_MAXIMIZE,
    FBWL_KEYBIND_TOGGLE_FULLSCREEN,
    FBWL_KEYBIND_TOGGLE_MINIMIZE,
    FBWL_KEYBIND_WORKSPACE_SWITCH,
    FBWL_KEYBIND_WORKSPACE_NEXT,
    FBWL_KEYBIND_WORKSPACE_PREV,
    FBWL_KEYBIND_SEND_TO_WORKSPACE,
    FBWL_KEYBIND_TAKE_TO_WORKSPACE,
    FBWL_KEYBIND_SEND_TO_REL_WORKSPACE,
    FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE,
    FBWL_KEYBIND_CLOSE,
    FBWL_KEYBIND_KILL,
    FBWL_KEYBIND_WINDOW_MENU,
    FBWL_KEYBIND_ROOT_MENU,
    FBWL_KEYBIND_HIDE_MENUS,
    FBWL_KEYBIND_RAISE,
    FBWL_KEYBIND_LOWER,
    FBWL_KEYBIND_FOCUS,
    FBWL_KEYBIND_START_MOVING,
    FBWL_KEYBIND_START_RESIZING,
    FBWL_KEYBIND_MACRO,
};

enum fbwl_keybinding_key_kind {
    FBWL_KEYBIND_KEYSYM = 0,
    FBWL_KEYBIND_KEYCODE,
};

struct fbwl_keybinding {
    enum fbwl_keybinding_key_kind key_kind;
    xkb_keysym_t sym;
    uint32_t keycode;
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
    void (*reconfigure)(void *userdata);
    void (*apply_workspace_visibility)(void *userdata, const char *why);
    void (*view_set_maximized)(void *userdata, struct fbwl_view *view, bool maximized);
    void (*view_set_fullscreen)(void *userdata, struct fbwl_view *view, bool fullscreen);
    void (*view_set_minimized)(void *userdata, struct fbwl_view *view, bool minimized, const char *why);
    void (*view_close)(void *userdata, struct fbwl_view *view, bool force);
    void (*view_raise)(void *userdata, struct fbwl_view *view, const char *why);
    void (*view_lower)(void *userdata, struct fbwl_view *view, const char *why);
    void (*menu_open_root)(void *userdata, int x, int y);
    void (*menu_open_window)(void *userdata, struct fbwl_view *view, int x, int y);
    void (*menu_close)(void *userdata, const char *why);
    void (*grab_begin_move)(void *userdata, struct fbwl_view *view, uint32_t button);
    void (*grab_begin_resize)(void *userdata, struct fbwl_view *view, uint32_t button, uint32_t edges);
    int cursor_x;
    int cursor_y;
    uint32_t button;
};

void fbwl_keybindings_free(struct fbwl_keybinding **bindings, size_t *count);

bool fbwl_keybindings_add(struct fbwl_keybinding **bindings, size_t *count, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd);

bool fbwl_keybindings_add_keycode(struct fbwl_keybinding **bindings, size_t *count, uint32_t keycode, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd);

void fbwl_keybindings_add_defaults(struct fbwl_keybinding **bindings, size_t *count, const char *terminal_cmd);

bool fbwl_keybindings_execute_action(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks);

bool fbwl_keybindings_handle(const struct fbwl_keybinding *bindings, size_t count, uint32_t keycode, xkb_keysym_t sym,
        uint32_t modifiers, const struct fbwl_keybindings_hooks *hooks);
