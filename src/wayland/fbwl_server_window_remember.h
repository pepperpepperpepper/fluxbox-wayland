#pragma once

#include "wayland/fbwl_menu.h"

struct fbwl_server;
struct fbwl_view;

void server_window_remember_toggle(struct fbwl_server *server, struct fbwl_view *view, enum fbwl_menu_remember_attr attr);
void server_window_remember_forget(struct fbwl_server *server, struct fbwl_view *view);

