#pragma once

#include <stdbool.h>
#include <stddef.h>

enum fbwl_menu_item_kind {
    FBWL_MENU_ITEM_EXEC = 0,
    FBWL_MENU_ITEM_EXIT,
    FBWL_MENU_ITEM_SUBMENU,
    FBWL_MENU_ITEM_VIEW_ACTION,
};

enum fbwl_menu_view_action {
    FBWL_MENU_VIEW_CLOSE = 0,
    FBWL_MENU_VIEW_TOGGLE_MINIMIZE,
    FBWL_MENU_VIEW_TOGGLE_MAXIMIZE,
    FBWL_MENU_VIEW_TOGGLE_FULLSCREEN,
};

struct fbwl_menu;

struct fbwl_menu_item {
    enum fbwl_menu_item_kind kind;
    char *label;
    char *cmd;
    struct fbwl_menu *submenu;
    enum fbwl_menu_view_action view_action;
};

struct fbwl_menu {
    char *label;
    struct fbwl_menu_item *items;
    size_t item_count;
    size_t item_cap;
};

struct fbwl_menu *fbwl_menu_create(const char *label);
void fbwl_menu_free(struct fbwl_menu *menu);

bool fbwl_menu_add_exec(struct fbwl_menu *menu, const char *label, const char *cmd);
bool fbwl_menu_add_exit(struct fbwl_menu *menu, const char *label);
bool fbwl_menu_add_view_action(struct fbwl_menu *menu, const char *label,
    enum fbwl_menu_view_action action);
bool fbwl_menu_add_submenu(struct fbwl_menu *menu, const char *label, struct fbwl_menu *submenu);
