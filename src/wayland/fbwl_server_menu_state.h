#pragma once

struct fbwl_menu;
struct fbwl_server;
struct fbwl_view;

void server_menu_sync_toggle_states(struct fbwl_server *server, struct fbwl_menu *menu,
    struct fbwl_view *target_view, int menu_x, int menu_y);

