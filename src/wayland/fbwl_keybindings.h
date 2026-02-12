#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

struct fbwl_view;
struct fbwm_core;

enum fbwl_keybinding_action {
    FBWL_KEYBIND_EXIT,
    FBWL_KEYBIND_RESTART,
    FBWL_KEYBIND_EXEC,
    FBWL_KEYBIND_SET_ENV,
    FBWL_KEYBIND_COMMAND_DIALOG,
    FBWL_KEYBIND_RECONFIGURE,
    FBWL_KEYBIND_RELOAD_STYLE,
    FBWL_KEYBIND_SET_STYLE,
    FBWL_KEYBIND_SAVE_RC,
    FBWL_KEYBIND_SET_RESOURCE_VALUE,
    FBWL_KEYBIND_SET_RESOURCE_VALUE_DIALOG,
    FBWL_KEYBIND_FOCUS_NEXT,
    FBWL_KEYBIND_FOCUS_PREV,
    FBWL_KEYBIND_FOCUS_NEXT_GROUP,
    FBWL_KEYBIND_FOCUS_PREV_GROUP,
    FBWL_KEYBIND_GOTO_WINDOW,
    FBWL_KEYBIND_ATTACH,
    FBWL_KEYBIND_SHOW_DESKTOP,
    FBWL_KEYBIND_ARRANGE_WINDOWS,
    FBWL_KEYBIND_UNCLUTTER,
    FBWL_KEYBIND_TAB_NEXT,
    FBWL_KEYBIND_TAB_PREV,
    FBWL_KEYBIND_TAB_GOTO,
    FBWL_KEYBIND_TAB_ACTIVATE,
    FBWL_KEYBIND_MOVE_TAB_LEFT,
    FBWL_KEYBIND_MOVE_TAB_RIGHT,
    FBWL_KEYBIND_DETACH_CLIENT,
    FBWL_KEYBIND_TOGGLE_MAXIMIZE,
    FBWL_KEYBIND_TOGGLE_MAXIMIZE_HORIZONTAL,
    FBWL_KEYBIND_TOGGLE_MAXIMIZE_VERTICAL,
    FBWL_KEYBIND_TOGGLE_FULLSCREEN,
    FBWL_KEYBIND_TOGGLE_MINIMIZE,
    FBWL_KEYBIND_WORKSPACE_SWITCH,
    FBWL_KEYBIND_WORKSPACE_NEXT,
    FBWL_KEYBIND_WORKSPACE_PREV,
    FBWL_KEYBIND_ADD_WORKSPACE,
    FBWL_KEYBIND_REMOVE_LAST_WORKSPACE,
    FBWL_KEYBIND_SET_WORKSPACE_NAME,
    FBWL_KEYBIND_SET_WORKSPACE_NAME_DIALOG,
    FBWL_KEYBIND_SEND_TO_WORKSPACE,
    FBWL_KEYBIND_TAKE_TO_WORKSPACE,
    FBWL_KEYBIND_SEND_TO_REL_WORKSPACE,
    FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE,
    FBWL_KEYBIND_SET_HEAD,
    FBWL_KEYBIND_SEND_TO_REL_HEAD,
    FBWL_KEYBIND_CLOSE,
    FBWL_KEYBIND_KILL,
    FBWL_KEYBIND_WINDOW_MENU,
    FBWL_KEYBIND_ROOT_MENU,
    FBWL_KEYBIND_WORKSPACE_MENU,
    FBWL_KEYBIND_CLIENT_MENU,
    FBWL_KEYBIND_TOGGLE_TOOLBAR_HIDDEN,
    FBWL_KEYBIND_TOGGLE_TOOLBAR_ABOVE,
    FBWL_KEYBIND_TOGGLE_SLIT_HIDDEN,
    FBWL_KEYBIND_TOGGLE_SLIT_ABOVE,
    FBWL_KEYBIND_HIDE_MENUS,
    FBWL_KEYBIND_RAISE,
    FBWL_KEYBIND_LOWER,
    FBWL_KEYBIND_RAISE_LAYER,
    FBWL_KEYBIND_LOWER_LAYER,
    FBWL_KEYBIND_SET_LAYER,
    FBWL_KEYBIND_FOCUS,
    FBWL_KEYBIND_FOCUS_DIR,
    FBWL_KEYBIND_SET_XPROP,
    FBWL_KEYBIND_START_MOVING,
    FBWL_KEYBIND_START_RESIZING,
    FBWL_KEYBIND_START_TABBING,
    FBWL_KEYBIND_MOVE_TO,
    FBWL_KEYBIND_MOVE_REL,
    FBWL_KEYBIND_RESIZE_TO,
    FBWL_KEYBIND_RESIZE_REL,
    FBWL_KEYBIND_KEYMODE,
    FBWL_KEYBIND_MACRO,
    FBWL_KEYBIND_IF,
    FBWL_KEYBIND_FOREACH,
    FBWL_KEYBIND_TOGGLECMD,
    FBWL_KEYBIND_DELAY,
    FBWL_KEYBIND_TOGGLE_SHADE,
    FBWL_KEYBIND_SHADE_ON,
    FBWL_KEYBIND_SHADE_OFF,
    FBWL_KEYBIND_TOGGLE_STICK,
    FBWL_KEYBIND_STICK_ON,
    FBWL_KEYBIND_STICK_OFF,
    FBWL_KEYBIND_SET_ALPHA,
    FBWL_KEYBIND_TOGGLE_DECOR,
    FBWL_KEYBIND_SET_DECOR,
    FBWL_KEYBIND_SET_TITLE,
    FBWL_KEYBIND_SET_TITLE_DIALOG,
    FBWL_KEYBIND_DEICONIFY,
    FBWL_KEYBIND_MARK_WINDOW,
    FBWL_KEYBIND_GOTO_MARKED_WINDOW,
    FBWL_KEYBIND_CLOSE_ALL_WINDOWS,
};

enum fbwl_focus_dir {
    FBWL_FOCUS_DIR_LEFT = 0,
    FBWL_FOCUS_DIR_RIGHT = 1,
    FBWL_FOCUS_DIR_UP = 2,
    FBWL_FOCUS_DIR_DOWN = 3,
};

enum fbwl_arrange_windows_method {
    FBWL_ARRANGE_WINDOWS_UNSPECIFIED = 0,
    FBWL_ARRANGE_WINDOWS_VERTICAL = 1,
    FBWL_ARRANGE_WINDOWS_HORIZONTAL = 2,
    FBWL_ARRANGE_WINDOWS_STACK_LEFT = 3,
    FBWL_ARRANGE_WINDOWS_STACK_RIGHT = 4,
    FBWL_ARRANGE_WINDOWS_STACK_TOP = 5,
    FBWL_ARRANGE_WINDOWS_STACK_BOTTOM = 6,
};

enum fbwl_keybinding_key_kind {
    FBWL_KEYBIND_KEYSYM = 0,
    FBWL_KEYBIND_KEYCODE,
    FBWL_KEYBIND_PLACEHOLDER,
    FBWL_KEYBIND_CHANGE_WORKSPACE,
};

struct fbwl_keybinding {
    enum fbwl_keybinding_key_kind key_kind;
    xkb_keysym_t sym;
    uint32_t keycode;
    uint32_t modifiers;
    enum fbwl_keybinding_action action;
    int arg;
    char *cmd;
    char *mode;
};

struct fbwl_keybindings_hooks {
    void *userdata;
    const void *cmdlang_scope;
    struct fbwm_core *wm;
    const char *key_mode;
    uint32_t placeholder_keycode;
    xkb_keysym_t placeholder_sym;
    bool (*cycle_view_allowed)(void *userdata, const struct fbwl_view *view);
    void (*key_mode_set)(void *userdata, const char *mode);
    void (*terminate)(void *userdata);
    void (*restart)(void *userdata, const char *cmd);
    void (*spawn)(void *userdata, const char *cmd);
    void (*command_dialog_open)(void *userdata);
    void (*reconfigure)(void *userdata);
    void (*apply_workspace_visibility)(void *userdata, const char *why);
    int (*workspace_current)(void *userdata, int x, int y);
    void (*workspace_switch)(void *userdata, int x, int y, int workspace0, const char *why);
    void (*view_set_maximized)(void *userdata, struct fbwl_view *view, bool maximized);
    void (*view_toggle_maximize_horizontal)(void *userdata, struct fbwl_view *view);
    void (*view_toggle_maximize_vertical)(void *userdata, struct fbwl_view *view);
    void (*view_set_fullscreen)(void *userdata, struct fbwl_view *view, bool fullscreen);
    void (*view_set_minimized)(void *userdata, struct fbwl_view *view, bool minimized, const char *why);
    void (*view_close)(void *userdata, struct fbwl_view *view, bool force);
    void (*view_raise)(void *userdata, struct fbwl_view *view, const char *why);
    void (*view_lower)(void *userdata, struct fbwl_view *view, const char *why);
    void (*view_raise_layer)(void *userdata, struct fbwl_view *view);
    void (*view_lower_layer)(void *userdata, struct fbwl_view *view);
    void (*view_set_layer)(void *userdata, struct fbwl_view *view, int layer);
    void (*view_set_xprop)(void *userdata, struct fbwl_view *view, const char *name, const char *value);
    void (*views_attach_pattern)(void *userdata, const char *pattern, int cursor_x, int cursor_y);
    void (*show_desktop)(void *userdata, int cursor_x, int cursor_y);
    void (*arrange_windows)(void *userdata, int method, const char *pattern, int cursor_x, int cursor_y);
    void (*unclutter)(void *userdata, const char *pattern, int cursor_x, int cursor_y);
    void (*menu_open_root)(void *userdata, int x, int y, const char *menu_file);
    void (*menu_open_workspace)(void *userdata, int x, int y);
    void (*menu_open_client)(void *userdata, int x, int y, const char *pattern);
    void (*menu_open_window)(void *userdata, struct fbwl_view *view, int x, int y);
    void (*menu_close)(void *userdata, const char *why);
    void (*grab_begin_move)(void *userdata, struct fbwl_view *view, uint32_t button);
    void (*grab_begin_resize)(void *userdata, struct fbwl_view *view, uint32_t button, uint32_t edges);
    void (*grab_begin_tabbing)(void *userdata, struct fbwl_view *view, uint32_t button);
    int cursor_x;
    int cursor_y;
    uint32_t button;
};

void fbwl_keybindings_free(struct fbwl_keybinding **bindings, size_t *count);

bool fbwl_keybindings_add(struct fbwl_keybinding **bindings, size_t *count, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode);

bool fbwl_keybindings_add_keycode(struct fbwl_keybinding **bindings, size_t *count, uint32_t keycode, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode);

bool fbwl_keybindings_add_placeholder(struct fbwl_keybinding **bindings, size_t *count, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode);

bool fbwl_keybindings_add_change_workspace(struct fbwl_keybinding **bindings, size_t *count,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode);

void fbwl_keybindings_add_defaults(struct fbwl_keybinding **bindings, size_t *count, const char *terminal_cmd);

bool fbwl_keybindings_execute_action(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks);

bool fbwl_keybindings_handle(const struct fbwl_keybinding *bindings, size_t count, uint32_t keycode, xkb_keysym_t sym,
        uint32_t modifiers, const struct fbwl_keybindings_hooks *hooks);

bool fbwl_keybindings_handle_change_workspace(const struct fbwl_keybinding *bindings, size_t count,
        const struct fbwl_keybindings_hooks *hooks);

struct fbwl_view *fbwl_keybindings_pick_cycle_candidate(const struct fbwl_keybindings_hooks *hooks, bool reverse,
        bool groups, bool static_order, char *pattern);

struct fbwl_view *fbwl_keybindings_pick_goto_candidate(const struct fbwl_keybindings_hooks *hooks, int num, bool groups,
        bool static_order, char *pattern);

struct fbwl_view *fbwl_keybindings_pick_dir_focus_candidate(const struct fbwl_keybindings_hooks *hooks,
        const struct fbwl_view *reference, enum fbwl_focus_dir dir);
