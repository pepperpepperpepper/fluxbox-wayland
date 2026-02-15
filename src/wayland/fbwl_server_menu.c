#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_menu_parse.h"
#include "wayland/fbwl_server_internal.h"

static const char *window_menu_default_label(const char *key) {
    if (key == NULL) {
        return "Action";
    }
    if (strcasecmp(key, "shade") == 0) {
        return "Shade";
    }
    if (strcasecmp(key, "stick") == 0) {
        return "Stick";
    }
    if (strcasecmp(key, "maximize") == 0) {
        return "Maximize";
    }
    if (strcasecmp(key, "fullscreen") == 0) {
        return "Fullscreen";
    }
    if (strcasecmp(key, "iconify") == 0 || strcasecmp(key, "minimize") == 0) {
        return "Iconify";
    }
    if (strcasecmp(key, "raise") == 0) {
        return "Raise";
    }
    if (strcasecmp(key, "lower") == 0) {
        return "Lower";
    }
    if (strcasecmp(key, "settitledialog") == 0) {
        return "Set Title...";
    }
    if (strcasecmp(key, "sendto") == 0) {
        return "Send To";
    }
    if (strcasecmp(key, "layer") == 0) {
        return "Layer";
    }
    if (strcasecmp(key, "alpha") == 0) {
        return "Alpha";
    }
    if (strcasecmp(key, "extramenus") == 0) {
        return "Remember...";
    }
    if (strcasecmp(key, "close") == 0) {
        return "Close";
    }
    if (strcasecmp(key, "kill") == 0) {
        return "Kill";
    }
    return "Action";
}

static bool window_menu_fill_sendto_submenu(struct fbwl_menu *submenu, struct fbwm_core *wm) {
    if (submenu == NULL || wm == NULL) {
        return false;
    }

    const int count = fbwm_core_workspace_count(wm);
    for (int i = 0; i < count; i++) {
        const char *name = fbwm_core_workspace_name(wm, i);
        char label[256];
        if (name != NULL && *name != '\0') {
            snprintf(label, sizeof(label), "%d: %s", i + 1, name);
        } else {
            snprintf(label, sizeof(label), "%d", i + 1);
        }
        (void)fbwl_menu_add_server_action(submenu, label, NULL, FBWL_MENU_SERVER_WINDOW_SEND_TO_WORKSPACE, i, NULL);
    }
    return true;
}

static bool window_menu_fill_layer_submenu(struct fbwl_menu *submenu) {
    if (submenu == NULL) {
        return false;
    }

    (void)fbwl_menu_add_server_action(submenu, "Above Dock", NULL, FBWL_MENU_SERVER_WINDOW_SET_LAYER, 2, NULL);
    (void)fbwl_menu_add_server_action(submenu, "Dock", NULL, FBWL_MENU_SERVER_WINDOW_SET_LAYER, 4, NULL);
    (void)fbwl_menu_add_server_action(submenu, "Top", NULL, FBWL_MENU_SERVER_WINDOW_SET_LAYER, 6, NULL);
    (void)fbwl_menu_add_server_action(submenu, "Normal", NULL, FBWL_MENU_SERVER_WINDOW_SET_LAYER, 8, NULL);
    (void)fbwl_menu_add_server_action(submenu, "Bottom", NULL, FBWL_MENU_SERVER_WINDOW_SET_LAYER, 10, NULL);
    (void)fbwl_menu_add_server_action(submenu, "Desktop", NULL, FBWL_MENU_SERVER_WINDOW_SET_LAYER, 12, NULL);
    return true;
}

static bool window_menu_fill_alpha_submenu(struct fbwl_menu *submenu) {
    if (submenu == NULL) {
        return false;
    }

    struct fbwl_menu *focused = fbwl_menu_create("Focused");
    if (focused != NULL) {
        if (!fbwl_menu_add_submenu(submenu, "Focused", focused, NULL)) {
            fbwl_menu_free(focused);
            focused = NULL;
        }
    }
    struct fbwl_menu *unfocused = fbwl_menu_create("Unfocused");
    if (unfocused != NULL) {
        if (!fbwl_menu_add_submenu(submenu, "Unfocused", unfocused, NULL)) {
            fbwl_menu_free(unfocused);
            unfocused = NULL;
        }
    }

    const int pcts[] = {100, 90, 80, 70, 60, 50, 40, 30, 20, 10};
    for (size_t i = 0; i < sizeof(pcts) / sizeof(pcts[0]); i++) {
        const int pct = pcts[i];
        const int alpha = (pct * 255 + 50) / 100;
        char label[32];
        snprintf(label, sizeof(label), "%d%%", pct);
        if (focused != NULL) {
            (void)fbwl_menu_add_server_action(focused, label, NULL, FBWL_MENU_SERVER_WINDOW_SET_ALPHA_FOCUSED, alpha, NULL);
        }
        if (unfocused != NULL) {
            (void)fbwl_menu_add_server_action(unfocused, label, NULL, FBWL_MENU_SERVER_WINDOW_SET_ALPHA_UNFOCUSED, alpha, NULL);
        }
    }
    return true;
}

static void window_menu_last_item_set_close_on_click(struct fbwl_menu *menu, bool close_on_click) {
    if (menu == NULL || menu->item_count < 1) {
        return;
    }
    menu->items[menu->item_count - 1].close_on_click = close_on_click;
}

static void window_menu_add_remember_toggle(struct fbwl_menu *submenu, const char *label, enum fbwl_menu_remember_attr attr) {
    if (submenu == NULL || label == NULL || *label == '\0') {
        return;
    }
    if (fbwl_menu_add_server_action(submenu, label, NULL, FBWL_MENU_SERVER_WINDOW_REMEMBER_TOGGLE, attr, NULL)) {
        window_menu_last_item_set_close_on_click(submenu, false);
    }
}

static bool window_menu_fill_remember_submenu(struct fbwl_menu *submenu) {
    if (submenu == NULL) {
        return false;
    }

    window_menu_add_remember_toggle(submenu, "Workspace", FBWL_MENU_REMEMBER_WORKSPACE);
    window_menu_add_remember_toggle(submenu, "Jump To Workspace", FBWL_MENU_REMEMBER_JUMP);
    window_menu_add_remember_toggle(submenu, "Head", FBWL_MENU_REMEMBER_HEAD);
    window_menu_add_remember_toggle(submenu, "Position", FBWL_MENU_REMEMBER_POSITION);
    window_menu_add_remember_toggle(submenu, "Dimensions", FBWL_MENU_REMEMBER_DIMENSIONS);
    window_menu_add_remember_toggle(submenu, "Layer", FBWL_MENU_REMEMBER_LAYER);
    window_menu_add_remember_toggle(submenu, "Alpha", FBWL_MENU_REMEMBER_ALPHA);
    window_menu_add_remember_toggle(submenu, "Decorations", FBWL_MENU_REMEMBER_DECOR);
    window_menu_add_remember_toggle(submenu, "Sticky", FBWL_MENU_REMEMBER_STICKY);
    window_menu_add_remember_toggle(submenu, "Shaded", FBWL_MENU_REMEMBER_SHADED);
    window_menu_add_remember_toggle(submenu, "Tab", FBWL_MENU_REMEMBER_TAB);
    window_menu_add_remember_toggle(submenu, "Minimized", FBWL_MENU_REMEMBER_MINIMIZED);
    window_menu_add_remember_toggle(submenu, "Maximized", FBWL_MENU_REMEMBER_MAXIMIZED);
    window_menu_add_remember_toggle(submenu, "Fullscreen", FBWL_MENU_REMEMBER_FULLSCREEN);
    window_menu_add_remember_toggle(submenu, "Focus Hidden", FBWL_MENU_REMEMBER_FOCUS_HIDDEN);
    window_menu_add_remember_toggle(submenu, "Icon Hidden", FBWL_MENU_REMEMBER_ICON_HIDDEN);
    window_menu_add_remember_toggle(submenu, "Ignore Size Hints", FBWL_MENU_REMEMBER_IGNORE_SIZE_HINTS);
    window_menu_add_remember_toggle(submenu, "Focus Protection", FBWL_MENU_REMEMBER_FOCUS_PROTECTION);
    window_menu_add_remember_toggle(submenu, "Save On Close", FBWL_MENU_REMEMBER_SAVE_ON_CLOSE);

    (void)fbwl_menu_add_separator(submenu);

    (void)fbwl_menu_add_server_action(submenu, "Forget", NULL, FBWL_MENU_SERVER_WINDOW_REMEMBER_FORGET, 0, NULL);

    return true;
}

static bool server_window_menu_append_key(struct fbwl_server *server, struct fbwl_menu *menu,
        const char *key, const char *label) {
    if (server == NULL || menu == NULL || key == NULL) {
        return false;
    }

    const char *use_label = (label != NULL && *label != '\0') ? label : window_menu_default_label(key);

    if (strcasecmp(key, "separator") == 0) {
        return fbwl_menu_add_separator(menu);
    }
    if (strcasecmp(key, "shade") == 0) {
        return fbwl_menu_add_server_action(menu, use_label, NULL, FBWL_MENU_SERVER_WINDOW_TOGGLE_SHADE, 0, NULL);
    }
    if (strcasecmp(key, "stick") == 0) {
        return fbwl_menu_add_server_action(menu, use_label, NULL, FBWL_MENU_SERVER_WINDOW_TOGGLE_STICK, 0, NULL);
    }
    if (strcasecmp(key, "raise") == 0) {
        return fbwl_menu_add_server_action(menu, use_label, NULL, FBWL_MENU_SERVER_WINDOW_RAISE, 0, NULL);
    }
    if (strcasecmp(key, "lower") == 0) {
        return fbwl_menu_add_server_action(menu, use_label, NULL, FBWL_MENU_SERVER_WINDOW_LOWER, 0, NULL);
    }
    if (strcasecmp(key, "maximize") == 0) {
        return fbwl_menu_add_server_action(menu, use_label, NULL, FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE, 0, NULL);
    }
    if (strcasecmp(key, "fullscreen") == 0) {
        return fbwl_menu_add_view_action(menu, use_label, NULL, FBWL_MENU_VIEW_TOGGLE_FULLSCREEN);
    }
    if (strcasecmp(key, "iconify") == 0 || strcasecmp(key, "minimize") == 0) {
        return fbwl_menu_add_view_action(menu, use_label, NULL, FBWL_MENU_VIEW_TOGGLE_MINIMIZE);
    }
    if (strcasecmp(key, "close") == 0) {
        return fbwl_menu_add_view_action(menu, use_label, NULL, FBWL_MENU_VIEW_CLOSE);
    }
    if (strcasecmp(key, "kill") == 0) {
        return fbwl_menu_add_server_action(menu, use_label, NULL, FBWL_MENU_SERVER_WINDOW_KILL, 0, NULL);
    }
    if (strcasecmp(key, "settitledialog") == 0) {
        return fbwl_menu_add_server_action(menu, use_label, NULL, FBWL_MENU_SERVER_WINDOW_SET_TITLE_DIALOG, 0, NULL);
    }
    if (strcasecmp(key, "sendto") == 0) {
        struct fbwl_menu *submenu = fbwl_menu_create(use_label);
        if (submenu == NULL) {
            return false;
        }
        if (!fbwl_menu_add_submenu(menu, use_label, submenu, NULL)) {
            fbwl_menu_free(submenu);
            return false;
        }
        return window_menu_fill_sendto_submenu(submenu, &server->wm);
    }
    if (strcasecmp(key, "layer") == 0) {
        struct fbwl_menu *submenu = fbwl_menu_create(use_label);
        if (submenu == NULL) {
            return false;
        }
        if (!fbwl_menu_add_submenu(menu, use_label, submenu, NULL)) {
            fbwl_menu_free(submenu);
            return false;
        }
        return window_menu_fill_layer_submenu(submenu);
    }
    if (strcasecmp(key, "alpha") == 0) {
        struct fbwl_menu *submenu = fbwl_menu_create(use_label);
        if (submenu == NULL) {
            return false;
        }
        if (!fbwl_menu_add_submenu(menu, use_label, submenu, NULL)) {
            fbwl_menu_free(submenu);
            return false;
        }
        return window_menu_fill_alpha_submenu(submenu);
    }
    if (strcasecmp(key, "extramenus") == 0) {
        struct fbwl_menu *submenu = fbwl_menu_create(use_label);
        if (submenu == NULL) {
            return false;
        }
        if (!fbwl_menu_add_submenu(menu, use_label, submenu, NULL)) {
            fbwl_menu_free(submenu);
            return false;
        }
        return window_menu_fill_remember_submenu(submenu);
    }

    wlr_log(WLR_INFO, "WindowMenu: ignoring unsupported key [%s]", key);
    return true;
}

static bool cmd_is_tag_no_args(const char *cmd, const char *tag) {
    if (cmd == NULL || tag == NULL || *tag == '\0') {
        return false;
    }
    while (*cmd != '\0' && isspace((unsigned char)*cmd)) {
        cmd++;
    }
    if (*cmd == '\0') {
        return false;
    }

    const char *end = cmd;
    while (*end != '\0' && !isspace((unsigned char)*end)) {
        end++;
    }
    const size_t tok_len = (size_t)(end - cmd);
    if (tok_len == 0) {
        return false;
    }
    if (strncasecmp(cmd, tag, tok_len) != 0 || tag[tok_len] != '\0') {
        return false;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    return *end == '\0';
}

static void window_menu_item_set_default_label(struct fbwl_menu_item *it, const char *tag) {
    if (it == NULL || tag == NULL || *tag == '\0') {
        return;
    }
    if (it->label != NULL && *it->label != '\0' && strcasecmp(it->label, tag) != 0) {
        return;
    }
    const char *want = window_menu_default_label(tag);
    if (want == NULL || *want == '\0') {
        return;
    }
    char *dup = strdup(want);
    if (dup == NULL) {
        return;
    }
    free(it->label);
    it->label = dup;
}

static void window_menu_convert_cmdlang_item(struct fbwl_server *server, struct fbwl_menu_item *it) {
    if (server == NULL || it == NULL) {
        return;
    }
    if (it->kind != FBWL_MENU_ITEM_SERVER_ACTION || it->server_action != FBWL_MENU_SERVER_CMDLANG || it->cmd == NULL) {
        return;
    }

    if (cmd_is_tag_no_args(it->cmd, "shade")) {
        window_menu_item_set_default_label(it, "shade");
        free(it->cmd);
        it->cmd = NULL;
        it->server_action = FBWL_MENU_SERVER_WINDOW_TOGGLE_SHADE;
        it->arg = 0;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "stick")) {
        window_menu_item_set_default_label(it, "stick");
        free(it->cmd);
        it->cmd = NULL;
        it->server_action = FBWL_MENU_SERVER_WINDOW_TOGGLE_STICK;
        it->arg = 0;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "raise")) {
        window_menu_item_set_default_label(it, "raise");
        free(it->cmd);
        it->cmd = NULL;
        it->server_action = FBWL_MENU_SERVER_WINDOW_RAISE;
        it->arg = 0;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "lower")) {
        window_menu_item_set_default_label(it, "lower");
        free(it->cmd);
        it->cmd = NULL;
        it->server_action = FBWL_MENU_SERVER_WINDOW_LOWER;
        it->arg = 0;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "maximize")) {
        window_menu_item_set_default_label(it, "maximize");
        free(it->cmd);
        it->cmd = NULL;
        it->server_action = FBWL_MENU_SERVER_WINDOW_TOGGLE_MAXIMIZE;
        it->arg = 0;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "fullscreen")) {
        window_menu_item_set_default_label(it, "fullscreen");
        free(it->cmd);
        it->cmd = NULL;
        it->kind = FBWL_MENU_ITEM_VIEW_ACTION;
        it->view_action = FBWL_MENU_VIEW_TOGGLE_FULLSCREEN;
        return;
    }
    const bool is_minimize = cmd_is_tag_no_args(it->cmd, "minimize");
    if (cmd_is_tag_no_args(it->cmd, "iconify") || is_minimize) {
        window_menu_item_set_default_label(it, is_minimize ? "minimize" : "iconify");
        free(it->cmd);
        it->cmd = NULL;
        it->kind = FBWL_MENU_ITEM_VIEW_ACTION;
        it->view_action = FBWL_MENU_VIEW_TOGGLE_MINIMIZE;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "close")) {
        window_menu_item_set_default_label(it, "close");
        free(it->cmd);
        it->cmd = NULL;
        it->kind = FBWL_MENU_ITEM_VIEW_ACTION;
        it->view_action = FBWL_MENU_VIEW_CLOSE;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "kill")) {
        window_menu_item_set_default_label(it, "kill");
        free(it->cmd);
        it->cmd = NULL;
        it->server_action = FBWL_MENU_SERVER_WINDOW_KILL;
        it->arg = 0;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "settitledialog")) {
        window_menu_item_set_default_label(it, "settitledialog");
        free(it->cmd);
        it->cmd = NULL;
        it->server_action = FBWL_MENU_SERVER_WINDOW_SET_TITLE_DIALOG;
        it->arg = 0;
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "sendto")) {
        window_menu_item_set_default_label(it, "sendto");
        struct fbwl_menu *submenu = fbwl_menu_create(it->label);
        if (submenu == NULL) {
            return;
        }
        free(it->cmd);
        it->cmd = NULL;
        it->kind = FBWL_MENU_ITEM_SUBMENU;
        it->submenu = submenu;
        (void)window_menu_fill_sendto_submenu(submenu, &server->wm);
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "layer")) {
        window_menu_item_set_default_label(it, "layer");
        struct fbwl_menu *submenu = fbwl_menu_create(it->label);
        if (submenu == NULL) {
            return;
        }
        free(it->cmd);
        it->cmd = NULL;
        it->kind = FBWL_MENU_ITEM_SUBMENU;
        it->submenu = submenu;
        (void)window_menu_fill_layer_submenu(submenu);
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "alpha")) {
        window_menu_item_set_default_label(it, "alpha");
        struct fbwl_menu *submenu = fbwl_menu_create(it->label);
        if (submenu == NULL) {
            return;
        }
        free(it->cmd);
        it->cmd = NULL;
        it->kind = FBWL_MENU_ITEM_SUBMENU;
        it->submenu = submenu;
        (void)window_menu_fill_alpha_submenu(submenu);
        return;
    }
    if (cmd_is_tag_no_args(it->cmd, "extramenus")) {
        window_menu_item_set_default_label(it, "extramenus");
        struct fbwl_menu *submenu = fbwl_menu_create(it->label);
        if (submenu == NULL) {
            return;
        }
        free(it->cmd);
        it->cmd = NULL;
        it->kind = FBWL_MENU_ITEM_SUBMENU;
        it->submenu = submenu;
        (void)window_menu_fill_remember_submenu(submenu);
        return;
    }
}

static void window_menu_convert_recurse(struct fbwl_server *server, struct fbwl_menu *menu) {
    if (server == NULL || menu == NULL) {
        return;
    }
    for (size_t i = 0; i < menu->item_count; i++) {
        struct fbwl_menu_item *it = &menu->items[i];
        window_menu_convert_cmdlang_item(server, it);
        if (it->submenu != NULL) {
            window_menu_convert_recurse(server, it->submenu);
        }
    }
}

static bool server_window_menu_load_file(struct fbwl_server *server, const char *path) {
    if (server == NULL || path == NULL || *path == '\0') {
        return false;
    }

    struct fbwl_menu *menu = fbwl_menu_create("Window");
    if (menu == NULL) {
        return false;
    }
    if (!fbwl_menu_parse_file(menu, &server->wm, path)) {
        fbwl_menu_free(menu);
        return false;
    }

    window_menu_convert_recurse(server, menu);

    fbwl_menu_free(server->window_menu);
    server->window_menu = menu;
    wlr_log(WLR_INFO, "WindowMenu: loaded %zu items from %s", menu->item_count, path);
    return true;
}

static void text_effect_set_defaults(struct fbwl_text_effect *effect) {
    if (effect == NULL) {
        return;
    }

    memset(effect, 0, sizeof(*effect));
    effect->kind = FBWL_TEXT_EFFECT_NONE;

    effect->shadow_color[0] = 0.0f;
    effect->shadow_color[1] = 0.0f;
    effect->shadow_color[2] = 0.0f;
    effect->shadow_color[3] = 1.0f;
    effect->shadow_x = 2;
    effect->shadow_y = 2;

    effect->halo_color[0] = 1.0f;
    effect->halo_color[1] = 1.0f;
    effect->halo_color[2] = 1.0f;
    effect->halo_color[3] = 1.0f;
}

void decor_theme_set_defaults(struct fbwl_decor_theme *theme) {
    if (theme == NULL) {
        return;
    }

    theme->border_width = 4;
    theme->bevel_width = 0;
    theme->window_bevel_width = 4;
    theme->handle_width = 0;
    theme->title_height = 24;
    theme->button_margin = 4;
    theme->button_spacing = 2;
    theme->window_justify = 0;
    theme->background_loaded = false;
    theme->background_options[0] = '\0';
    theme->background_pixmap[0] = '\0';
    theme->background_mod_x = 1;
    theme->background_mod_y = 1;
    theme->menu_item_height = theme->title_height;
    theme->menu_title_height = 0;
    theme->menu_border_width = 0;
    theme->menu_bevel_width = 0;
    theme->menu_frame_justify = 0;
    theme->menu_hilite_justify = 0;
    theme->menu_title_justify = 0;
    theme->menu_bullet = 0;
    theme->menu_bullet_pos = 0;
    theme->toolbar_height = 0;
    theme->toolbar_border_width = theme->border_width;
    theme->toolbar_bevel_width = theme->bevel_width;
    theme->toolbar_clock_justify = 0;
    theme->toolbar_workspace_justify = 0;
    theme->toolbar_iconbar_focused_justify = 0;
    theme->toolbar_iconbar_unfocused_justify = 0;
    theme->toolbar_clock_border_width = 0;
    theme->toolbar_workspace_border_width = 0;
    theme->toolbar_iconbar_border_width = 0;
    theme->toolbar_iconbar_focused_border_width = 0;
    theme->toolbar_iconbar_unfocused_border_width = 0;
    theme->slit_border_width = theme->border_width;
    theme->slit_bevel_width = 0;

    strncpy(theme->window_font, "Sans", sizeof(theme->window_font));
    theme->window_font[sizeof(theme->window_font) - 1] = '\0';
    strncpy(theme->menu_font, "Sans", sizeof(theme->menu_font));
    theme->menu_font[sizeof(theme->menu_font) - 1] = '\0';
    theme->menu_title_font[0] = '\0';
    theme->menu_hilite_font[0] = '\0';
    strncpy(theme->toolbar_font, "Sans", sizeof(theme->toolbar_font));
    theme->toolbar_font[sizeof(theme->toolbar_font) - 1] = '\0';

    text_effect_set_defaults(&theme->window_label_focus_effect);
    text_effect_set_defaults(&theme->window_label_unfocus_effect);
    text_effect_set_defaults(&theme->menu_frame_effect);
    text_effect_set_defaults(&theme->menu_title_effect);
    text_effect_set_defaults(&theme->menu_hilite_effect);
    text_effect_set_defaults(&theme->toolbar_workspace_effect);
    text_effect_set_defaults(&theme->toolbar_iconbar_focused_effect);
    text_effect_set_defaults(&theme->toolbar_iconbar_unfocused_effect);
    text_effect_set_defaults(&theme->toolbar_clock_effect);
    text_effect_set_defaults(&theme->toolbar_label_effect);
    text_effect_set_defaults(&theme->toolbar_windowlabel_effect);

    theme->titlebar_active[0] = 0.20f;
    theme->titlebar_active[1] = 0.20f;
    theme->titlebar_active[2] = 0.20f;
    theme->titlebar_active[3] = 1.0f;

    theme->titlebar_inactive[0] = 0.10f;
    theme->titlebar_inactive[1] = 0.10f;
    theme->titlebar_inactive[2] = 0.10f;
    theme->titlebar_inactive[3] = 1.0f;

    theme->border_color_focus[0] = 0.05f;
    theme->border_color_focus[1] = 0.05f;
    theme->border_color_focus[2] = 0.05f;
    theme->border_color_focus[3] = 1.0f;

    theme->border_color_unfocus[0] = theme->border_color_focus[0];
    theme->border_color_unfocus[1] = theme->border_color_focus[1];
    theme->border_color_unfocus[2] = theme->border_color_focus[2];
    theme->border_color_unfocus[3] = theme->border_color_focus[3];

    theme->menu_border_color[0] = theme->border_color_unfocus[0];
    theme->menu_border_color[1] = theme->border_color_unfocus[1];
    theme->menu_border_color[2] = theme->border_color_unfocus[2];
    theme->menu_border_color[3] = theme->border_color_unfocus[3];

    memcpy(theme->toolbar_border_color, theme->border_color_unfocus, sizeof(theme->toolbar_border_color));
    memcpy(theme->toolbar_clock_border_color, theme->border_color_unfocus, sizeof(theme->toolbar_clock_border_color));
    memcpy(theme->toolbar_workspace_border_color, theme->border_color_unfocus, sizeof(theme->toolbar_workspace_border_color));
    memcpy(theme->toolbar_iconbar_border_color, theme->border_color_unfocus, sizeof(theme->toolbar_iconbar_border_color));
    memcpy(theme->toolbar_iconbar_focused_border_color, theme->border_color_unfocus, sizeof(theme->toolbar_iconbar_focused_border_color));
    memcpy(theme->toolbar_iconbar_unfocused_border_color, theme->border_color_unfocus, sizeof(theme->toolbar_iconbar_unfocused_border_color));
    memcpy(theme->slit_border_color, theme->border_color_unfocus, sizeof(theme->slit_border_color));

    theme->title_text_active[0] = 1.0f;
    theme->title_text_active[1] = 1.0f;
    theme->title_text_active[2] = 1.0f;
    theme->title_text_active[3] = 1.0f;

    theme->title_text_inactive[0] = 1.0f;
    theme->title_text_inactive[1] = 1.0f;
    theme->title_text_inactive[2] = 1.0f;
    theme->title_text_inactive[3] = 1.0f;

    theme->menu_bg[0] = theme->titlebar_inactive[0];
    theme->menu_bg[1] = theme->titlebar_inactive[1];
    theme->menu_bg[2] = theme->titlebar_inactive[2];
    theme->menu_bg[3] = 1.0f;

    theme->menu_hilite[0] = theme->titlebar_active[0];
    theme->menu_hilite[1] = theme->titlebar_active[1];
    theme->menu_hilite[2] = theme->titlebar_active[2];
    theme->menu_hilite[3] = 1.0f;

    theme->menu_text[0] = 1.0f;
    theme->menu_text[1] = 1.0f;
    theme->menu_text[2] = 1.0f;
    theme->menu_text[3] = 1.0f;

    theme->menu_title_text[0] = theme->menu_text[0];
    theme->menu_title_text[1] = theme->menu_text[1];
    theme->menu_title_text[2] = theme->menu_text[2];
    theme->menu_title_text[3] = theme->menu_text[3];

    theme->menu_hilite_text[0] = theme->menu_text[0];
    theme->menu_hilite_text[1] = theme->menu_text[1];
    theme->menu_hilite_text[2] = theme->menu_text[2];
    theme->menu_hilite_text[3] = theme->menu_text[3];

    theme->menu_underline_color[0] = theme->menu_hilite_text[0];
    theme->menu_underline_color[1] = theme->menu_hilite_text[1];
    theme->menu_underline_color[2] = theme->menu_hilite_text[2];
    theme->menu_underline_color[3] = theme->menu_hilite_text[3];

    theme->menu_disable_text[0] = theme->menu_text[0];
    theme->menu_disable_text[1] = theme->menu_text[1];
    theme->menu_disable_text[2] = theme->menu_text[2];
    theme->menu_disable_text[3] = 0.55f;

    theme->menu_submenu_pixmap[0] = '\0';
    theme->menu_selected_pixmap[0] = '\0';
    theme->menu_unselected_pixmap[0] = '\0';
    theme->menu_hilite_submenu_pixmap[0] = '\0';
    theme->menu_hilite_selected_pixmap[0] = '\0';
    theme->menu_hilite_unselected_pixmap[0] = '\0';

    theme->toolbar_border_width_explicit = false;
    theme->toolbar_border_color_explicit = false;
    theme->toolbar_bevel_width_explicit = false;
    theme->toolbar_clock_border_width_explicit = false;
    theme->toolbar_clock_border_color_explicit = false;
    theme->toolbar_workspace_border_width_explicit = false;
    theme->toolbar_workspace_border_color_explicit = false;
    theme->toolbar_iconbar_border_width_explicit = false;
    theme->toolbar_iconbar_border_color_explicit = false;
    theme->toolbar_iconbar_focused_border_width_explicit = false;
    theme->toolbar_iconbar_focused_border_color_explicit = false;
    theme->toolbar_iconbar_unfocused_border_width_explicit = false;
    theme->toolbar_iconbar_unfocused_border_color_explicit = false;
    theme->slit_border_width_explicit = false;
    theme->slit_border_color_explicit = false;
    theme->slit_bevel_width_explicit = false;
    theme->slit_texture_explicit = false;
    theme->toolbar_clock_texture_explicit = false;
    theme->toolbar_workspace_texture_explicit = false;
    theme->toolbar_label_texture_explicit = false;
    theme->toolbar_windowlabel_texture_explicit = false;
    theme->toolbar_button_texture_explicit = false;
    theme->toolbar_button_pressed_texture_explicit = false;
    theme->toolbar_systray_texture_explicit = false;
    theme->toolbar_iconbar_texture_explicit = false;
    theme->toolbar_iconbar_empty_texture_explicit = false;
    theme->toolbar_iconbar_focused_texture_explicit = false;
    theme->toolbar_iconbar_unfocused_texture_explicit = false;
    theme->window_bevel_width_explicit = false;

    theme->toolbar_bg[0] = theme->titlebar_inactive[0];
    theme->toolbar_bg[1] = theme->titlebar_inactive[1];
    theme->toolbar_bg[2] = theme->titlebar_inactive[2];
    theme->toolbar_bg[3] = 1.0f;

    theme->toolbar_hilite[0] = theme->titlebar_active[0];
    theme->toolbar_hilite[1] = theme->titlebar_active[1];
    theme->toolbar_hilite[2] = theme->titlebar_active[2];
    theme->toolbar_hilite[3] = 1.0f;

    theme->toolbar_text[0] = 1.0f;
    theme->toolbar_text[1] = 1.0f;
    theme->toolbar_text[2] = 1.0f;
    theme->toolbar_text[3] = 1.0f;

    theme->toolbar_iconbar_focused[0] = theme->titlebar_active[0];
    theme->toolbar_iconbar_focused[1] = theme->titlebar_active[1];
    theme->toolbar_iconbar_focused[2] = theme->titlebar_active[2];
    theme->toolbar_iconbar_focused[3] = 1.0f;

    theme->btn_menu_color[0] = 0.15f;
    theme->btn_menu_color[1] = 0.15f;
    theme->btn_menu_color[2] = 0.80f;
    theme->btn_menu_color[3] = 1.0f;

    theme->btn_shade_color[0] = 0.40f;
    theme->btn_shade_color[1] = 0.40f;
    theme->btn_shade_color[2] = 0.40f;
    theme->btn_shade_color[3] = 1.0f;

    theme->btn_stick_color[0] = 0.80f;
    theme->btn_stick_color[1] = 0.50f;
    theme->btn_stick_color[2] = 0.15f;
    theme->btn_stick_color[3] = 1.0f;

    theme->btn_close_color[0] = 0.80f;
    theme->btn_close_color[1] = 0.15f;
    theme->btn_close_color[2] = 0.15f;
    theme->btn_close_color[3] = 1.0f;

    theme->btn_max_color[0] = 0.15f;
    theme->btn_max_color[1] = 0.65f;
    theme->btn_max_color[2] = 0.15f;
    theme->btn_max_color[3] = 1.0f;

    theme->btn_min_color[0] = 0.80f;
    theme->btn_min_color[1] = 0.65f;
    theme->btn_min_color[2] = 0.15f;
    theme->btn_min_color[3] = 1.0f;

    theme->btn_lhalf_color[0] = 0.55f;
    theme->btn_lhalf_color[1] = 0.15f;
    theme->btn_lhalf_color[2] = 0.80f;
    theme->btn_lhalf_color[3] = 1.0f;

    theme->btn_rhalf_color[0] = 0.15f;
    theme->btn_rhalf_color[1] = 0.80f;
    theme->btn_rhalf_color[2] = 0.80f;
    theme->btn_rhalf_color[3] = 1.0f;

    theme->window_tab_border_width = 0;
    theme->window_tab_border_color[0] = theme->border_color_unfocus[0];
    theme->window_tab_border_color[1] = theme->border_color_unfocus[1];
    theme->window_tab_border_color[2] = theme->border_color_unfocus[2];
    theme->window_tab_border_color[3] = theme->border_color_unfocus[3];
    theme->window_tab_justify = 0;
    strncpy(theme->window_tab_font, theme->window_font, sizeof(theme->window_tab_font));
    theme->window_tab_font[sizeof(theme->window_tab_font) - 1] = '\0';
    theme->window_tab_label_focus_text[0] = theme->title_text_active[0];
    theme->window_tab_label_focus_text[1] = theme->title_text_active[1];
    theme->window_tab_label_focus_text[2] = theme->title_text_active[2];
    theme->window_tab_label_focus_text[3] = theme->title_text_active[3];
    theme->window_tab_label_unfocus_text[0] = theme->title_text_inactive[0];
    theme->window_tab_label_unfocus_text[1] = theme->title_text_inactive[1];
    theme->window_tab_label_unfocus_text[2] = theme->title_text_inactive[2];
    theme->window_tab_label_unfocus_text[3] = theme->title_text_inactive[3];

    // Texture defaults: match the current simplified "flat color" renderer.
    fbwl_texture_init(&theme->window_title_focus_tex);
    memcpy(theme->window_title_focus_tex.color, theme->titlebar_active, sizeof(theme->window_title_focus_tex.color));
    memcpy(theme->window_title_focus_tex.color_to, theme->titlebar_active, sizeof(theme->window_title_focus_tex.color_to));

    fbwl_texture_init(&theme->window_title_unfocus_tex);
    memcpy(theme->window_title_unfocus_tex.color, theme->titlebar_inactive, sizeof(theme->window_title_unfocus_tex.color));
    memcpy(theme->window_title_unfocus_tex.color_to, theme->titlebar_inactive, sizeof(theme->window_title_unfocus_tex.color_to));

    fbwl_texture_init(&theme->window_label_focus_tex);
    memcpy(theme->window_label_focus_tex.color, theme->titlebar_active, sizeof(theme->window_label_focus_tex.color));
    memcpy(theme->window_label_focus_tex.color_to, theme->titlebar_active, sizeof(theme->window_label_focus_tex.color_to));
    theme->window_label_focus_tex.type = theme->window_title_focus_tex.type;
    memcpy(theme->window_label_focus_tex.pic_color, theme->window_title_focus_tex.pic_color,
        sizeof(theme->window_label_focus_tex.pic_color));

    fbwl_texture_init(&theme->window_label_unfocus_tex);
    memcpy(theme->window_label_unfocus_tex.color, theme->titlebar_inactive, sizeof(theme->window_label_unfocus_tex.color));
    memcpy(theme->window_label_unfocus_tex.color_to, theme->titlebar_inactive, sizeof(theme->window_label_unfocus_tex.color_to));
    theme->window_label_unfocus_tex.type = theme->window_title_unfocus_tex.type;
    memcpy(theme->window_label_unfocus_tex.pic_color, theme->window_title_unfocus_tex.pic_color,
        sizeof(theme->window_label_unfocus_tex.pic_color));

    // Fluxbox themes commonly set window.button.* to ParentRelative to inherit the title texture.
    fbwl_texture_init(&theme->window_button_focus_tex);
    theme->window_button_focus_tex.type = FBWL_TEXTURE_PARENTRELATIVE;

    fbwl_texture_init(&theme->window_button_unfocus_tex);
    theme->window_button_unfocus_tex.type = FBWL_TEXTURE_PARENTRELATIVE;

    fbwl_texture_init(&theme->window_button_pressed_tex);
    theme->window_button_pressed_tex.type = theme->window_button_focus_tex.type;

    fbwl_texture_init(&theme->window_handle_focus_tex);
    memcpy(theme->window_handle_focus_tex.color, theme->border_color_focus, sizeof(theme->window_handle_focus_tex.color));
    memcpy(theme->window_handle_focus_tex.color_to, theme->border_color_focus, sizeof(theme->window_handle_focus_tex.color_to));

    fbwl_texture_init(&theme->window_handle_unfocus_tex);
    memcpy(theme->window_handle_unfocus_tex.color, theme->border_color_unfocus, sizeof(theme->window_handle_unfocus_tex.color));
    memcpy(theme->window_handle_unfocus_tex.color_to, theme->border_color_unfocus, sizeof(theme->window_handle_unfocus_tex.color_to));

    fbwl_texture_init(&theme->window_grip_focus_tex);
    memcpy(theme->window_grip_focus_tex.color, theme->border_color_focus, sizeof(theme->window_grip_focus_tex.color));
    memcpy(theme->window_grip_focus_tex.color_to, theme->border_color_focus, sizeof(theme->window_grip_focus_tex.color_to));

    fbwl_texture_init(&theme->window_grip_unfocus_tex);
    memcpy(theme->window_grip_unfocus_tex.color, theme->border_color_unfocus, sizeof(theme->window_grip_unfocus_tex.color));
    memcpy(theme->window_grip_unfocus_tex.color_to, theme->border_color_unfocus, sizeof(theme->window_grip_unfocus_tex.color_to));

    fbwl_texture_init(&theme->window_tab_label_focus_tex);
    memcpy(theme->window_tab_label_focus_tex.color, theme->titlebar_active, sizeof(theme->window_tab_label_focus_tex.color));
    memcpy(theme->window_tab_label_focus_tex.color_to, theme->titlebar_active, sizeof(theme->window_tab_label_focus_tex.color_to));
    theme->window_tab_label_focus_tex.type = theme->window_title_focus_tex.type;

    fbwl_texture_init(&theme->window_tab_label_unfocus_tex);
    memcpy(theme->window_tab_label_unfocus_tex.color, theme->titlebar_inactive, sizeof(theme->window_tab_label_unfocus_tex.color));
    memcpy(theme->window_tab_label_unfocus_tex.color_to, theme->titlebar_inactive, sizeof(theme->window_tab_label_unfocus_tex.color_to));
    theme->window_tab_label_unfocus_tex.type = theme->window_title_unfocus_tex.type;

    fbwl_texture_init(&theme->menu_frame_tex);
    memcpy(theme->menu_frame_tex.color, theme->menu_bg, sizeof(theme->menu_frame_tex.color));
    memcpy(theme->menu_frame_tex.color_to, theme->menu_bg, sizeof(theme->menu_frame_tex.color_to));

    fbwl_texture_init(&theme->menu_title_tex);
    memcpy(theme->menu_title_tex.color, theme->menu_bg, sizeof(theme->menu_title_tex.color));
    memcpy(theme->menu_title_tex.color_to, theme->menu_bg, sizeof(theme->menu_title_tex.color_to));

    fbwl_texture_init(&theme->menu_hilite_tex);
    memcpy(theme->menu_hilite_tex.color, theme->menu_hilite, sizeof(theme->menu_hilite_tex.color));
    memcpy(theme->menu_hilite_tex.color_to, theme->menu_hilite, sizeof(theme->menu_hilite_tex.color_to));

    fbwl_texture_init(&theme->toolbar_tex);
    memcpy(theme->toolbar_tex.color, theme->toolbar_bg, sizeof(theme->toolbar_tex.color));
    memcpy(theme->toolbar_tex.color_to, theme->toolbar_bg, sizeof(theme->toolbar_tex.color_to));

    // Fluxbox/X11: slit inherits toolbar look unless overridden.
    theme->slit_tex = theme->toolbar_tex;

    // Toolbar tool defaults: start as toolbar look; style fallbacks are applied after parsing.
    theme->toolbar_clock_tex = theme->toolbar_tex;
    theme->toolbar_workspace_tex = theme->toolbar_tex;
    theme->toolbar_label_tex = theme->toolbar_tex;
    theme->toolbar_windowlabel_tex = theme->toolbar_tex;
    theme->toolbar_button_tex = theme->toolbar_tex;
    theme->toolbar_button_pressed_tex = theme->toolbar_tex;
    theme->toolbar_systray_tex = theme->toolbar_tex;
    theme->toolbar_iconbar_tex = theme->toolbar_tex;
    theme->toolbar_iconbar_empty_tex = theme->toolbar_tex;
    theme->toolbar_iconbar_focused_tex = theme->toolbar_tex;
    theme->toolbar_iconbar_unfocused_tex = theme->toolbar_tex;

    fbwl_texture_init(&theme->background_tex);
}

void server_menu_create_default(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_menu_free(server->root_menu);
    server->root_menu = fbwl_menu_create("Fluxbox");
    if (server->root_menu == NULL) {
        return;
    }

    (void)fbwl_menu_add_exec(server->root_menu, "Terminal", server->terminal_cmd, NULL);
    (void)fbwl_menu_add_exit(server->root_menu, "Exit", NULL);
}

void server_menu_create_window(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_menu_free(server->window_menu);
    server->window_menu = NULL;

    const char *window_menu_file = server->window_menu_file;
    if (window_menu_file != NULL && *window_menu_file != '\0') {
        if (server_window_menu_load_file(server, window_menu_file)) {
            return;
        }
        wlr_log(WLR_ERROR, "WindowMenu: falling back to built-in default");
    }

    server->window_menu = fbwl_menu_create("Window");
    if (server->window_menu == NULL) {
        return;
    }

    (void)server_window_menu_append_key(server, server->window_menu, "shade", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "stick", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "maximize", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "iconify", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "raise", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "lower", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "settitledialog", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "sendto", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "layer", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "alpha", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "extramenus", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "separator", NULL);
    (void)server_window_menu_append_key(server, server->window_menu, "close", NULL);
    wlr_log(WLR_INFO, "WindowMenu: using built-in default (%zu items)", server->window_menu->item_count);
}

bool server_menu_load_file(struct fbwl_server *server, const char *path) {
    if (server == NULL || path == NULL || *path == '\0') {
        return false;
    }

    fbwl_menu_free(server->root_menu);
    server->root_menu = fbwl_menu_create(NULL);
    if (server->root_menu == NULL) {
        return false;
    }

    if (!fbwl_menu_parse_file(server->root_menu, &server->wm, path)) {
        fbwl_menu_free(server->root_menu);
        server->root_menu = NULL;
        return false;
    }

    wlr_log(WLR_INFO, "Menu: loaded %zu items from %s", server->root_menu->item_count, path);
    return true;
}

bool server_menu_load_custom_file(struct fbwl_server *server, const char *path) {
    if (server == NULL || path == NULL || *path == '\0') {
        return false;
    }

    char *resolved = fbwl_resolve_config_path(server->config_dir, path);
    if (resolved == NULL) {
        return false;
    }

    fbwl_menu_free(server->custom_menu);
    server->custom_menu = fbwl_menu_create(NULL);
    if (server->custom_menu == NULL) {
        free(resolved);
        return false;
    }

    if (!fbwl_menu_parse_file(server->custom_menu, &server->wm, resolved)) {
        fbwl_menu_free(server->custom_menu);
        server->custom_menu = NULL;
        free(resolved);
        return false;
    }

    wlr_log(WLR_INFO, "CustomMenu: loaded %zu items from %s", server->custom_menu->item_count, resolved);
    free(resolved);
    return true;
}
