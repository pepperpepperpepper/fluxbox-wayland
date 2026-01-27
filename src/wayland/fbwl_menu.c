#include "wayland/fbwl_menu.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool fbwl_menu_reserve_items(struct fbwl_menu *menu, size_t extra) {
    if (menu == NULL) {
        return false;
    }
    if (extra > SIZE_MAX - menu->item_count) {
        return false;
    }
    size_t need = menu->item_count + extra;
    if (need <= menu->item_cap) {
        return true;
    }
    size_t new_cap = menu->item_cap > 0 ? menu->item_cap : 8;
    while (new_cap < need) {
        if (new_cap > SIZE_MAX / 2) {
            return false;
        }
        new_cap *= 2;
    }
    struct fbwl_menu_item *items = realloc(menu->items, new_cap * sizeof(*items));
    if (items == NULL) {
        return false;
    }
    menu->items = items;
    menu->item_cap = new_cap;
    return true;
}

void fbwl_menu_free(struct fbwl_menu *menu) {
    if (menu == NULL) {
        return;
    }
    for (size_t i = 0; i < menu->item_count; i++) {
        struct fbwl_menu_item *it = &menu->items[i];
        free(it->label);
        it->label = NULL;
        free(it->cmd);
        it->cmd = NULL;
        if (it->submenu != NULL) {
            fbwl_menu_free(it->submenu);
            it->submenu = NULL;
        }
    }
    free(menu->items);
    menu->items = NULL;
    menu->item_count = 0;
    menu->item_cap = 0;
    free(menu->label);
    menu->label = NULL;
    free(menu);
}

struct fbwl_menu *fbwl_menu_create(const char *label) {
    struct fbwl_menu *menu = calloc(1, sizeof(*menu));
    if (menu == NULL) {
        return NULL;
    }
    if (label != NULL && *label != '\0') {
        menu->label = strdup(label);
        if (menu->label == NULL) {
            free(menu);
            return NULL;
        }
    }
    return menu;
}

bool fbwl_menu_add_exec(struct fbwl_menu *menu, const char *label, const char *cmd) {
    if (menu == NULL || cmd == NULL || *cmd == '\0') {
        return false;
    }
    if (!fbwl_menu_reserve_items(menu, 1)) {
        return false;
    }
    struct fbwl_menu_item *it = &menu->items[menu->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = FBWL_MENU_ITEM_EXEC;
    it->label = label != NULL && *label != '\0' ? strdup(label) : strdup(cmd);
    it->cmd = strdup(cmd);
    if (it->label == NULL || it->cmd == NULL) {
        free(it->label);
        free(it->cmd);
        menu->item_count--;
        return false;
    }
    return true;
}

bool fbwl_menu_add_exit(struct fbwl_menu *menu, const char *label) {
    if (menu == NULL) {
        return false;
    }
    if (!fbwl_menu_reserve_items(menu, 1)) {
        return false;
    }
    struct fbwl_menu_item *it = &menu->items[menu->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = FBWL_MENU_ITEM_EXIT;
    it->label = label != NULL && *label != '\0' ? strdup(label) : strdup("Exit");
    if (it->label == NULL) {
        menu->item_count--;
        return false;
    }
    return true;
}

bool fbwl_menu_add_view_action(struct fbwl_menu *menu, const char *label,
        enum fbwl_menu_view_action action) {
    if (menu == NULL) {
        return false;
    }
    if (!fbwl_menu_reserve_items(menu, 1)) {
        return false;
    }
    struct fbwl_menu_item *it = &menu->items[menu->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = FBWL_MENU_ITEM_VIEW_ACTION;
    it->view_action = action;
    it->label = label != NULL && *label != '\0' ? strdup(label) : strdup("Action");
    if (it->label == NULL) {
        menu->item_count--;
        return false;
    }
    return true;
}

bool fbwl_menu_add_submenu(struct fbwl_menu *menu, const char *label, struct fbwl_menu *submenu) {
    if (menu == NULL || submenu == NULL) {
        return false;
    }
    if (!fbwl_menu_reserve_items(menu, 1)) {
        return false;
    }
    struct fbwl_menu_item *it = &menu->items[menu->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = FBWL_MENU_ITEM_SUBMENU;
    it->label = label != NULL && *label != '\0' ? strdup(label) : strdup("Submenu");
    it->submenu = submenu;
    if (it->label == NULL) {
        menu->item_count--;
        return false;
    }
    return true;
}

bool fbwl_menu_add_separator(struct fbwl_menu *menu) {
    if (menu == NULL) {
        return false;
    }
    if (!fbwl_menu_reserve_items(menu, 1)) {
        return false;
    }
    struct fbwl_menu_item *it = &menu->items[menu->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = FBWL_MENU_ITEM_SEPARATOR;
    return true;
}

bool fbwl_menu_add_nop(struct fbwl_menu *menu, const char *label) {
    if (menu == NULL) {
        return false;
    }
    if (!fbwl_menu_reserve_items(menu, 1)) {
        return false;
    }
    struct fbwl_menu_item *it = &menu->items[menu->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = FBWL_MENU_ITEM_NOP;
    it->label = label != NULL && *label != '\0' ? strdup(label) : strdup("");
    if (it->label == NULL) {
        menu->item_count--;
        return false;
    }
    return true;
}
