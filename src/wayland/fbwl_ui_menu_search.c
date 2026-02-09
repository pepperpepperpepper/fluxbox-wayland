#include "wayland/fbwl_ui_menu_search.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include <xkbcommon/xkbcommon-keysyms.h>

static bool menu_item_is_searchable(const struct fbwl_menu_item *it) {
    if (it == NULL) {
        return false;
    }
    switch (it->kind) {
    case FBWL_MENU_ITEM_SEPARATOR:
    case FBWL_MENU_ITEM_NOP:
        return false;
    default:
        return true;
    }
}

static bool match_itemstart(const char *label, const char *pattern) {
    if (pattern == NULL || *pattern == '\0') {
        return true;
    }
    if (label == NULL || *label == '\0') {
        return false;
    }

    for (size_t i = 0; pattern[i] != '\0'; i++) {
        if (label[i] == '\0') {
            return false;
        }
        if ((char)tolower((unsigned char)label[i]) != pattern[i]) {
            return false;
        }
    }
    return true;
}

static bool match_somewhere(const char *label, const char *pattern) {
    if (pattern == NULL || *pattern == '\0') {
        return true;
    }
    if (label == NULL || *label == '\0') {
        return false;
    }

    const size_t plen = strlen(pattern);
    const size_t llen = strlen(label);
    if (plen > llen) {
        return false;
    }
    for (size_t start = 0; start + plen <= llen; start++) {
        bool ok = true;
        for (size_t j = 0; j < plen; j++) {
            if ((char)tolower((unsigned char)label[start + j]) != pattern[j]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

static bool menu_item_matches(const struct fbwl_menu_item *it, enum fbwl_menu_search_mode mode, const char *pattern) {
    if (!menu_item_is_searchable(it)) {
        return false;
    }
    const char *label = it->label != NULL ? it->label : "";
    switch (mode) {
    case FBWL_MENU_SEARCH_NOWHERE:
        return false;
    case FBWL_MENU_SEARCH_SOMEWHERE:
        return match_somewhere(label, pattern);
    case FBWL_MENU_SEARCH_ITEMSTART:
    default:
        return match_itemstart(label, pattern);
    }
}

static bool menu_search_would_match(const struct fbwl_menu_ui *ui, const char *pattern) {
    if (ui == NULL || !ui->open || ui->current == NULL || pattern == NULL) {
        return false;
    }
    for (size_t i = 0; i < ui->current->item_count; i++) {
        if (menu_item_matches(&ui->current->items[i], ui->search_mode, pattern)) {
            return true;
        }
    }
    return false;
}

static void menu_search_select_next_match(struct fbwl_menu_ui *ui, const char *pattern) {
    if (ui == NULL || !ui->open || ui->current == NULL || ui->current->item_count == 0) {
        return;
    }

    size_t start = ui->selected;
    if (start >= ui->current->item_count) {
        start = 0;
    }

    for (size_t step = 1; step <= ui->current->item_count; step++) {
        const size_t idx = (start + step) % ui->current->item_count;
        if (menu_item_matches(&ui->current->items[idx], ui->search_mode, pattern)) {
            fbwl_ui_menu_set_selected(ui, idx);
            return;
        }
    }
}

enum fbwl_menu_search_mode fbwl_menu_search_mode_parse(const char *s) {
    if (s == NULL || *s == '\0') {
        return FBWL_MENU_SEARCH_ITEMSTART;
    }
    if (strcasecmp(s, "nowhere") == 0) {
        return FBWL_MENU_SEARCH_NOWHERE;
    }
    if (strcasecmp(s, "somewhere") == 0) {
        return FBWL_MENU_SEARCH_SOMEWHERE;
    }
    return FBWL_MENU_SEARCH_ITEMSTART;
}

void fbwl_ui_menu_search_reset(struct fbwl_menu_ui *ui) {
    if (ui == NULL) {
        return;
    }
    ui->search_pattern[0] = '\0';
}

bool fbwl_ui_menu_search_handle_key(struct fbwl_menu_ui *ui, xkb_keysym_t sym) {
    if (ui == NULL || !ui->open || ui->current == NULL) {
        return false;
    }

    if (sym == XKB_KEY_BackSpace) {
        const size_t len = strlen(ui->search_pattern);
        if (len == 0) {
            return false;
        }
        ui->search_pattern[len - 1] = '\0';
        return true;
    }

    char buf[16] = {0};
    (void)xkb_keysym_to_utf8(sym, buf, sizeof(buf));
    if (buf[0] == '\0' || buf[1] != '\0') {
        return false;
    }

    const unsigned char c = (unsigned char)buf[0];
    if (c < 0x20 || c == 0x7f) {
        return false;
    }

    if (ui->search_mode == FBWL_MENU_SEARCH_NOWHERE) {
        return true;
    }

    const size_t len = strlen(ui->search_pattern);
    if (len + 1 >= sizeof(ui->search_pattern)) {
        return true;
    }

    char pattern[sizeof(ui->search_pattern)];
    memcpy(pattern, ui->search_pattern, len);
    pattern[len] = (char)tolower(c);
    pattern[len + 1] = '\0';

    if (!menu_search_would_match(ui, pattern)) {
        return true;
    }

    memcpy(ui->search_pattern, pattern, len + 2);

    const size_t sel = ui->selected < ui->current->item_count ? ui->selected : 0;
    if (!menu_item_matches(&ui->current->items[sel], ui->search_mode, ui->search_pattern)) {
        menu_search_select_next_match(ui, ui->search_pattern);
    }

    return true;
}

