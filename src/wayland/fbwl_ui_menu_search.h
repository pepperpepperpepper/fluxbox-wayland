#pragma once

#include <stdbool.h>

#include <xkbcommon/xkbcommon.h>

#include "wayland/fbwl_ui_menu.h"

enum fbwl_menu_search_mode fbwl_menu_search_mode_parse(const char *s);

void fbwl_ui_menu_search_reset(struct fbwl_menu_ui *ui);
bool fbwl_ui_menu_search_handle_key(struct fbwl_menu_ui *ui, xkb_keysym_t sym);

