#include <stdio.h>

#include "wayland/fbwl_server_internal.h"

static struct fbwl_ui_menu_env menu_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_overlay = server != NULL ? server->layer_overlay : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
        .wl_display = server != NULL ? server->wl_display : NULL,
    };
}

void server_menu_ui_open_workspace(struct fbwl_server *server, int x, int y) {
    if (server == NULL) {
        return;
    }

    fbwl_menu_free(server->workspace_menu);
    server->workspace_menu = fbwl_menu_create("Workspaces");
    if (server->workspace_menu == NULL) {
        return;
    }

    const int count = fbwm_core_workspace_count(&server->wm);
    for (int i = 0; i < count; i++) {
        const char *name = fbwm_core_workspace_name(&server->wm, i);
        char label[256];
        if (name != NULL && *name != '\0') {
            snprintf(label, sizeof(label), "%d: %s", i + 1, name);
        } else {
            snprintf(label, sizeof(label), "%d", i + 1);
        }
        (void)fbwl_menu_add_workspace_switch(server->workspace_menu, label, i);
    }

    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_root(&server->menu_ui, &env, server->workspace_menu, x, y);

    const int cur = fbwm_core_workspace_current(&server->wm);
    if (cur >= 0) {
        fbwl_ui_menu_set_selected(&server->menu_ui, (size_t)cur);
    }
}

