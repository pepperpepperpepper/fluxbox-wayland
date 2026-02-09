#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_menu_parse.h"
#include "wayland/fbwl_server_internal.h"

static char *trim_inplace(char *s) {
    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static char *dup_trim_range(const char *start, const char *end) {
    if (start == NULL || end == NULL || end < start) {
        return NULL;
    }
    size_t len = (size_t)(end - start);
    char *tmp = malloc(len + 1);
    if (tmp == NULL) {
        return NULL;
    }
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    char *t = trim_inplace(tmp);
    if (t == NULL || *t == '\0') {
        free(tmp);
        return NULL;
    }
    if (t == tmp) {
        return tmp;
    }
    char *out = strdup(t);
    free(tmp);
    return out;
}

static char *parse_paren_value(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    const char *open = strchr(s, '(');
    if (open == NULL) {
        return NULL;
    }
    const char *close = strchr(open + 1, ')');
    if (close == NULL) {
        return NULL;
    }
    return dup_trim_range(open + 1, close);
}

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
    if (strcasecmp(key, "iconify") == 0) {
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
        return "Extra Menus";
    }
    if (strcasecmp(key, "close") == 0) {
        return "Close";
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
        return fbwl_menu_add_view_action(menu, use_label, NULL, FBWL_MENU_VIEW_TOGGLE_MAXIMIZE);
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
        (void)fbwl_menu_add_view_action(submenu, "Fullscreen", NULL, FBWL_MENU_VIEW_TOGGLE_FULLSCREEN);
        return true;
    }

    wlr_log(WLR_INFO, "WindowMenu: ignoring unsupported key [%s]", key);
    return true;
}

static bool server_window_menu_load_file(struct fbwl_server *server, const char *path) {
    if (server == NULL || path == NULL || *path == '\0') {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "WindowMenu: failed to open %s: %s", path, strerror(errno));
        return false;
    }

    struct fbwl_menu *menu = fbwl_menu_create("Window");
    if (menu == NULL) {
        fclose(f);
        return false;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t n = 0;
    while ((n = getline(&line, &cap, f)) != -1) {
        if (n <= 0) {
            continue;
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[n - 1] = '\0';
            n--;
        }

        char *s = trim_inplace(line);
        if (s == NULL || *s == '\0' || *s == '#' || *s == '!') {
            continue;
        }

        char *open = strchr(s, '[');
        char *close = open != NULL ? strchr(open + 1, ']') : NULL;
        if (open == NULL || close == NULL || close <= open + 1) {
            continue;
        }

        char *key = dup_trim_range(open + 1, close);
        if (key == NULL) {
            continue;
        }

        if (strcasecmp(key, "begin") == 0) {
            char *label = parse_paren_value(close + 1);
            if (label != NULL && *label != '\0') {
                free(menu->label);
                menu->label = strdup(label);
            }
            free(label);
            free(key);
            continue;
        }
        if (strcasecmp(key, "end") == 0) {
            free(key);
            break;
        }

        char *label = parse_paren_value(close + 1);
        (void)server_window_menu_append_key(server, menu, key, label);
        free(label);
        free(key);
    }

    free(line);
    fclose(f);

    fbwl_menu_free(server->window_menu);
    server->window_menu = menu;
    wlr_log(WLR_INFO, "WindowMenu: loaded %zu items from %s", menu->item_count, path);
    return true;
}

void decor_theme_set_defaults(struct fbwl_decor_theme *theme) {
    if (theme == NULL) {
        return;
    }

    theme->border_width = 4;
    theme->title_height = 24;
    theme->button_margin = 4;
    theme->button_spacing = 2;
    theme->menu_item_height = 0;
    theme->toolbar_height = 0;

    strncpy(theme->window_font, "Sans", sizeof(theme->window_font));
    theme->window_font[sizeof(theme->window_font) - 1] = '\0';
    strncpy(theme->menu_font, "Sans", sizeof(theme->menu_font));
    theme->menu_font[sizeof(theme->menu_font) - 1] = '\0';
    strncpy(theme->toolbar_font, "Sans", sizeof(theme->toolbar_font));
    theme->toolbar_font[sizeof(theme->toolbar_font) - 1] = '\0';

    theme->titlebar_active[0] = 0.20f;
    theme->titlebar_active[1] = 0.20f;
    theme->titlebar_active[2] = 0.20f;
    theme->titlebar_active[3] = 1.0f;

    theme->titlebar_inactive[0] = 0.10f;
    theme->titlebar_inactive[1] = 0.10f;
    theme->titlebar_inactive[2] = 0.10f;
    theme->titlebar_inactive[3] = 1.0f;

    theme->border_color[0] = 0.05f;
    theme->border_color[1] = 0.05f;
    theme->border_color[2] = 0.05f;
    theme->border_color[3] = 1.0f;

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

    theme->menu_hilite_text[0] = theme->menu_text[0];
    theme->menu_hilite_text[1] = theme->menu_text[1];
    theme->menu_hilite_text[2] = theme->menu_text[2];
    theme->menu_hilite_text[3] = theme->menu_text[3];

    theme->menu_disable_text[0] = theme->menu_text[0];
    theme->menu_disable_text[1] = theme->menu_text[1];
    theme->menu_disable_text[2] = theme->menu_text[2];
    theme->menu_disable_text[3] = 0.55f;

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
