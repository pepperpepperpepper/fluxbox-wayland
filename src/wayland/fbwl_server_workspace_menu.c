#include <stdio.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_server_menu_state.h"

static struct fbwl_ui_menu_env menu_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_overlay = server != NULL ? server->layer_overlay : NULL,
        .output_layout = server != NULL ? server->output_layout : NULL,
        .wallpaper_mode = server != NULL ? server->wallpaper_mode : FBWL_WALLPAPER_MODE_STRETCH,
        .wallpaper_buf = server != NULL ? server->wallpaper_buf : NULL,
        .background_color = server != NULL ? server->background_color : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
        .wl_display = server != NULL ? server->wl_display : NULL,
        .force_pseudo_transparency = server != NULL && server->force_pseudo_transparency,
    };
}

static void menu_ui_apply_screen_config(struct fbwl_server *server, int x, int y) {
    if (server == NULL) {
        return;
    }
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_at(server, x, y);
    if (cfg == NULL) {
        return;
    }
    server->menu_ui.menu_delay_ms = cfg->menu.delay_ms;
    server->menu_ui.alpha = cfg->menu.alpha;
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
        (void)fbwl_menu_add_workspace_switch(server->workspace_menu, label, i, NULL);
    }

    menu_ui_apply_screen_config(server, x, y);
    server_menu_sync_toggle_states(server, server->workspace_menu, NULL, x, y);
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_root(&server->menu_ui, &env, server->workspace_menu, x, y);

    const size_t head = fbwl_server_screen_index_at(server, x, y);
    const int cur = fbwm_core_workspace_current_for_head(&server->wm, head);
    if (cur >= 0) {
        fbwl_ui_menu_set_selected(&server->menu_ui, (size_t)cur);
    }
}
