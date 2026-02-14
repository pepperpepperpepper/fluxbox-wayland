#pragma once

#include "wayland/fbwl_menu.h"

struct fbwl_server;

void server_menu_handle_server_action(struct fbwl_server *server,
    enum fbwl_menu_server_action action, int arg, const char *cmd, const void *cmdlang_scope);
