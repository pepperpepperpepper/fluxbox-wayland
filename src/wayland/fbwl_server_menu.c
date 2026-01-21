#include <stdbool.h>
#include <stdlib.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_menu_parse.h"
#include "wayland/fbwl_server_internal.h"

void decor_theme_set_defaults(struct fbwl_decor_theme *theme) {
    if (theme == NULL) {
        return;
    }

    theme->border_width = 4;
    theme->title_height = 24;
    theme->button_margin = 4;
    theme->button_spacing = 2;

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

    (void)fbwl_menu_add_exec(server->root_menu, "Terminal", server->terminal_cmd);
    (void)fbwl_menu_add_exit(server->root_menu, "Exit");
}

void server_menu_create_window(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_menu_free(server->window_menu);
    server->window_menu = fbwl_menu_create("Window");
    if (server->window_menu == NULL) {
        return;
    }

    (void)fbwl_menu_add_view_action(server->window_menu, "Close", FBWL_MENU_VIEW_CLOSE);
    (void)fbwl_menu_add_view_action(server->window_menu, "Minimize", FBWL_MENU_VIEW_TOGGLE_MINIMIZE);
    (void)fbwl_menu_add_view_action(server->window_menu, "Maximize", FBWL_MENU_VIEW_TOGGLE_MAXIMIZE);
    (void)fbwl_menu_add_view_action(server->window_menu, "Fullscreen", FBWL_MENU_VIEW_TOGGLE_FULLSCREEN);
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

    if (!fbwl_menu_parse_file(server->root_menu, path)) {
        fbwl_menu_free(server->root_menu);
        server->root_menu = NULL;
        return false;
    }

    wlr_log(WLR_INFO, "Menu: loaded %zu items from %s", server->root_menu->item_count, path);
    return true;
}
