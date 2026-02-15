#pragma once

#include <stdbool.h>
#include <stddef.h>

enum fbwl_menu_item_kind {
    FBWL_MENU_ITEM_EXEC = 0,
    FBWL_MENU_ITEM_EXIT,
    FBWL_MENU_ITEM_SUBMENU,
    FBWL_MENU_ITEM_SERVER_ACTION,
    FBWL_MENU_ITEM_VIEW_ACTION,
    FBWL_MENU_ITEM_WORKSPACE_SWITCH,
    FBWL_MENU_ITEM_SEPARATOR,
    FBWL_MENU_ITEM_NOP,
};

enum fbwl_menu_view_action {
    FBWL_MENU_VIEW_CLOSE = 0,
    FBWL_MENU_VIEW_TOGGLE_MINIMIZE,
    FBWL_MENU_VIEW_TOGGLE_MAXIMIZE,
    FBWL_MENU_VIEW_TOGGLE_FULLSCREEN,
};

enum fbwl_menu_server_action {
    FBWL_MENU_SERVER_RECONFIGURE = 0,
    FBWL_MENU_SERVER_SET_STYLE,
    FBWL_MENU_SERVER_SET_WALLPAPER,
    FBWL_MENU_SERVER_SET_FOCUS_MODEL,
    FBWL_MENU_SERVER_TOGGLE_AUTO_RAISE,
    FBWL_MENU_SERVER_TOGGLE_CLICK_RAISES,
    FBWL_MENU_SERVER_TOGGLE_FOCUS_NEW_WINDOWS,
    FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT,
    FBWL_MENU_SERVER_SET_ROW_PLACEMENT_DIRECTION,
    FBWL_MENU_SERVER_SET_COL_PLACEMENT_DIRECTION,
    FBWL_MENU_SERVER_WINDOW_TOGGLE_SHADE,
    FBWL_MENU_SERVER_WINDOW_TOGGLE_STICK,
    FBWL_MENU_SERVER_WINDOW_RAISE,
    FBWL_MENU_SERVER_WINDOW_LOWER,
    FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE,
    FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE_HORIZONTAL,
    FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE_VERTICAL,
    FBWL_MENU_SERVER_WINDOW_SEND_TO_WORKSPACE,
    FBWL_MENU_SERVER_WINDOW_TAKE_TO_WORKSPACE,
    FBWL_MENU_SERVER_WINDOW_SET_LAYER,
    FBWL_MENU_SERVER_WINDOW_SET_ALPHA_FOCUSED,
    FBWL_MENU_SERVER_WINDOW_SET_ALPHA_UNFOCUSED,
    FBWL_MENU_SERVER_WINDOW_SET_TITLE_DIALOG,
    FBWL_MENU_SERVER_WINDOW_KILL,
    FBWL_MENU_SERVER_WINDOW_REMEMBER_TOGGLE,
    FBWL_MENU_SERVER_WINDOW_REMEMBER_FORGET,
    FBWL_MENU_SERVER_FOCUS_VIEW,
    FBWL_MENU_SERVER_SLIT_SET_PLACEMENT,
    FBWL_MENU_SERVER_SLIT_SET_LAYER,
    FBWL_MENU_SERVER_SLIT_SET_ON_HEAD,
    FBWL_MENU_SERVER_SLIT_TOGGLE_AUTO_HIDE,
    FBWL_MENU_SERVER_SLIT_TOGGLE_AUTO_RAISE,
    FBWL_MENU_SERVER_SLIT_TOGGLE_MAX_OVER,
    FBWL_MENU_SERVER_SLIT_SET_ALPHA,
    FBWL_MENU_SERVER_SLIT_CYCLE_UP,
    FBWL_MENU_SERVER_SLIT_CYCLE_DOWN,
    FBWL_MENU_SERVER_SLIT_TOGGLE_CLIENT_VISIBLE,
    FBWL_MENU_SERVER_SLIT_SAVE_CLIENT_LIST,
    FBWL_MENU_SERVER_SLIT_CLIENT_UP,
    FBWL_MENU_SERVER_SLIT_CLIENT_DOWN,
    FBWL_MENU_SERVER_SLIT_ALPHA_PROMPT,
    FBWL_MENU_SERVER_CMDLANG,
};

enum fbwl_menu_remember_attr {
    FBWL_MENU_REMEMBER_FOCUS_HIDDEN = 0,
    FBWL_MENU_REMEMBER_ICON_HIDDEN,
    FBWL_MENU_REMEMBER_WORKSPACE,
    FBWL_MENU_REMEMBER_STICKY,
    FBWL_MENU_REMEMBER_JUMP,
    FBWL_MENU_REMEMBER_HEAD,
    FBWL_MENU_REMEMBER_DIMENSIONS,
    FBWL_MENU_REMEMBER_IGNORE_SIZE_HINTS,
    FBWL_MENU_REMEMBER_POSITION,
    FBWL_MENU_REMEMBER_MINIMIZED,
    FBWL_MENU_REMEMBER_MAXIMIZED,
    FBWL_MENU_REMEMBER_FULLSCREEN,
    FBWL_MENU_REMEMBER_SHADED,
    FBWL_MENU_REMEMBER_TAB,
    FBWL_MENU_REMEMBER_ALPHA,
    FBWL_MENU_REMEMBER_FOCUS_PROTECTION,
    FBWL_MENU_REMEMBER_DECOR,
    FBWL_MENU_REMEMBER_LAYER,
    FBWL_MENU_REMEMBER_SAVE_ON_CLOSE,
};

struct fbwl_menu;

struct fbwl_menu_item {
    enum fbwl_menu_item_kind kind;
    char *label;
    char *cmd;
    char *icon;
    struct fbwl_menu *submenu;
    enum fbwl_menu_server_action server_action;
    enum fbwl_menu_view_action view_action;
    int arg;
    bool close_on_click;
    bool toggle;
    bool selected;
};

struct fbwl_menu {
    char *label;
    struct fbwl_menu_item *items;
    size_t item_count;
    size_t item_cap;
};

struct fbwl_menu *fbwl_menu_create(const char *label);
void fbwl_menu_free(struct fbwl_menu *menu);

bool fbwl_menu_add_exec(struct fbwl_menu *menu, const char *label, const char *cmd, const char *icon);
bool fbwl_menu_add_exit(struct fbwl_menu *menu, const char *label, const char *icon);
bool fbwl_menu_add_server_action(struct fbwl_menu *menu, const char *label, const char *icon,
    enum fbwl_menu_server_action action, int arg, const char *cmd);
bool fbwl_menu_add_view_action(struct fbwl_menu *menu, const char *label, const char *icon,
    enum fbwl_menu_view_action action);
bool fbwl_menu_add_workspace_switch(struct fbwl_menu *menu, const char *label, int workspace0, const char *icon);
bool fbwl_menu_add_submenu(struct fbwl_menu *menu, const char *label, struct fbwl_menu *submenu, const char *icon);
bool fbwl_menu_add_separator(struct fbwl_menu *menu);
bool fbwl_menu_add_nop(struct fbwl_menu *menu, const char *label, const char *icon);
