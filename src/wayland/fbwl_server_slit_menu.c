#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_server_internal.h"

static void menu_last_item_set_close_on_click(struct fbwl_menu *menu, bool close_on_click) {
    if (menu == NULL || menu->item_count == 0) {
        return;
    }
    menu->items[menu->item_count - 1].close_on_click = close_on_click;
}

static struct fbwl_ui_menu_env menu_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_overlay = server != NULL ? server->layer_overlay : NULL,
        .output_layout = server != NULL ? server->output_layout : NULL,
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

static const char *slit_match_name(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }
    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        const char *name = view->xwayland_surface->instance;
        if (name != NULL && *name != '\0') {
            return name;
        }
        name = view->xwayland_surface->class;
        if (name != NULL && *name != '\0') {
            return name;
        }
    }
    const char *name = fbwl_view_app_id(view);
    if (name != NULL && *name != '\0') {
        return name;
    }
    name = fbwl_view_title(view);
    if (name != NULL && *name != '\0') {
        return name;
    }
    return NULL;
}

static struct fbwl_menu *slit_menu_build(struct fbwl_server *server) {
    if (server == NULL) {
        return NULL;
    }

    struct fbwl_menu *menu = fbwl_menu_create("Slit");
    if (menu == NULL) {
        return NULL;
    }

    struct fbwl_menu *placement = fbwl_menu_create("Placement");
    if (placement != NULL) {
        struct {
            const char *label;
            enum fbwl_toolbar_placement placement;
        } items[] = {
            {"Top Left", FBWL_TOOLBAR_PLACEMENT_TOP_LEFT},
            {"Top Center", FBWL_TOOLBAR_PLACEMENT_TOP_CENTER},
            {"Top Right", FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT},
            {"Left Top", FBWL_TOOLBAR_PLACEMENT_LEFT_TOP},
            {"Left Center", FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER},
            {"Left Bottom", FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM},
            {"Right Top", FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP},
            {"Right Center", FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER},
            {"Right Bottom", FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM},
            {"Bottom Left", FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT},
            {"Bottom Center", FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER},
            {"Bottom Right", FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT},
        };
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            char label[64];
            const bool selected = server->slit_ui.placement == items[i].placement;
            (void)snprintf(label, sizeof(label), "%s%s", selected ? "* " : "", items[i].label);
            (void)fbwl_menu_add_server_action(placement, label, NULL, FBWL_MENU_SERVER_SLIT_SET_PLACEMENT,
                (int)items[i].placement, NULL);
            menu_last_item_set_close_on_click(placement, false);
        }
        if (!fbwl_menu_add_submenu(menu, "Placement", placement, NULL)) {
            fbwl_menu_free(placement);
            placement = NULL;
        }
    }

    struct fbwl_menu *layer = fbwl_menu_create("Layer");
    if (layer != NULL) {
        struct {
            const char *label;
            int layer_num;
        } items[] = {
            {"Above Dock", 2},
            {"Dock", 4},
            {"Top", 6},
            {"Normal", 8},
            {"Bottom", 10},
            {"Desktop", 12},
        };
        for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            char label[64];
            const bool selected = server->slit_ui.layer_num == items[i].layer_num;
            (void)snprintf(label, sizeof(label), "%s%s", selected ? "* " : "", items[i].label);
            (void)fbwl_menu_add_server_action(layer, label, NULL, FBWL_MENU_SERVER_SLIT_SET_LAYER, items[i].layer_num, NULL);
            menu_last_item_set_close_on_click(layer, false);
        }
        if (!fbwl_menu_add_submenu(menu, "Layer...", layer, NULL)) {
            fbwl_menu_free(layer);
            layer = NULL;
        }
    }

    if (server->screen_configs != NULL && server->screen_configs_len > 1) {
        struct fbwl_menu *heads = fbwl_menu_create("On Head");
        if (heads != NULL) {
            for (size_t i = 0; i < server->screen_configs_len; i++) {
                char label[64];
                const bool selected = server->slit_ui.on_head >= 0 && (size_t)server->slit_ui.on_head == i;
                (void)snprintf(label, sizeof(label), "%sHead %zu", selected ? "* " : "", i + 1);
                (void)fbwl_menu_add_server_action(heads, label, NULL, FBWL_MENU_SERVER_SLIT_SET_ON_HEAD, (int)i, NULL);
                menu_last_item_set_close_on_click(heads, false);
            }
            if (!fbwl_menu_add_submenu(menu, "On Head...", heads, NULL)) {
                fbwl_menu_free(heads);
                heads = NULL;
            }
        }
    }

    char label[64];
    (void)snprintf(label, sizeof(label), "%sAuto hide", server->slit_ui.auto_hide ? "[x] " : "[ ] ");
    (void)fbwl_menu_add_server_action(menu, label, NULL, FBWL_MENU_SERVER_SLIT_TOGGLE_AUTO_HIDE, 0, NULL);
    menu_last_item_set_close_on_click(menu, false);

    (void)snprintf(label, sizeof(label), "%sAuto raise", server->slit_ui.auto_raise ? "[x] " : "[ ] ");
    (void)fbwl_menu_add_server_action(menu, label, NULL, FBWL_MENU_SERVER_SLIT_TOGGLE_AUTO_RAISE, 0, NULL);
    menu_last_item_set_close_on_click(menu, false);

    (void)snprintf(label, sizeof(label), "%sMaximize Over", server->slit_ui.max_over ? "[x] " : "[ ] ");
    (void)fbwl_menu_add_server_action(menu, label, NULL, FBWL_MENU_SERVER_SLIT_TOGGLE_MAX_OVER, 0, NULL);
    menu_last_item_set_close_on_click(menu, false);

    struct fbwl_menu *alpha = fbwl_menu_create("Alpha");
    if (alpha != NULL) {
        (void)fbwl_menu_add_server_action(alpha, "Set Alpha...", NULL, FBWL_MENU_SERVER_SLIT_ALPHA_PROMPT, 0, NULL);
        (void)fbwl_menu_add_separator(alpha);
        const int pcts[] = {100, 90, 80, 70, 60, 50, 40, 30, 20, 10};
        for (size_t i = 0; i < sizeof(pcts) / sizeof(pcts[0]); i++) {
            const int pct = pcts[i];
            const int a = (pct * 255 + 50) / 100;
            char a_label[64];
            const bool selected = server->slit_ui.alpha == (uint8_t)a;
            (void)snprintf(a_label, sizeof(a_label), "%s%d%%", selected ? "* " : "", pct);
            (void)fbwl_menu_add_server_action(alpha, a_label, NULL, FBWL_MENU_SERVER_SLIT_SET_ALPHA, a, NULL);
            menu_last_item_set_close_on_click(alpha, false);
        }
        if (!fbwl_menu_add_submenu(menu, "Alpha", alpha, NULL)) {
            fbwl_menu_free(alpha);
            alpha = NULL;
        }
    }

    struct fbwl_menu *clients = fbwl_menu_create("Clients");
    if (clients != NULL) {
        (void)fbwl_menu_add_server_action(clients, "Cycle Up", NULL, FBWL_MENU_SERVER_SLIT_CYCLE_UP, 0, NULL);
        (void)fbwl_menu_add_server_action(clients, "Cycle Down", NULL, FBWL_MENU_SERVER_SLIT_CYCLE_DOWN, 0, NULL);
        (void)fbwl_menu_add_separator(clients);

        size_t client_count = 0;
        const struct fbwl_slit_item *it;
        wl_list_for_each(it, &server->slit_ui.items, link) {
            const struct fbwl_view *view = it->view;
            if (view == NULL) {
                continue;
            }
            const char *name = slit_match_name(view);
            if (name == NULL || *name == '\0') {
                name = fbwl_view_display_title(view);
            }
            if (name == NULL || *name == '\0') {
                name = "(unnamed)";
            }

            char seq[32];
            (void)snprintf(seq, sizeof(seq), "%llu", (unsigned long long)view->create_seq);

            char item_label[256];
            (void)snprintf(item_label, sizeof(item_label), "%s%s", it->visible ? "[x] " : "[ ] ", name);
            (void)fbwl_menu_add_server_action(clients, item_label, NULL, FBWL_MENU_SERVER_SLIT_TOGGLE_CLIENT_VISIBLE, 0, seq);
            menu_last_item_set_close_on_click(clients, false);
            client_count++;
        }
        if (client_count == 0) {
            (void)fbwl_menu_add_nop(clients, "(none)", NULL);
        }

        (void)fbwl_menu_add_separator(clients);
        (void)fbwl_menu_add_server_action(clients, "Save SlitList", NULL, FBWL_MENU_SERVER_SLIT_SAVE_CLIENT_LIST, 0, NULL);

        if (!fbwl_menu_add_submenu(menu, "Clients", clients, NULL)) {
            fbwl_menu_free(clients);
            clients = NULL;
        }
    }

    return menu;
}

void server_menu_ui_open_slit(struct fbwl_server *server, int x, int y) {
    if (server == NULL) {
        return;
    }

    fbwl_menu_free(server->slit_menu);
    server->slit_menu = slit_menu_build(server);
    if (server->slit_menu == NULL) {
        wlr_log(WLR_ERROR, "SlitMenu: failed to build");
        return;
    }

    menu_ui_apply_screen_config(server, x, y);
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_root(&server->menu_ui, &env, server->slit_menu, x, y);
    wlr_log(WLR_INFO, "SlitMenu: open");
}
