#pragma once

#include <stdbool.h>

struct fbwm_core;
struct fbwl_menu;

bool fbwl_menu_parse_file(struct fbwl_menu *root, struct fbwm_core *wm, const char *path);
