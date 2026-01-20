#pragma once

#include <stdbool.h>

struct fbwl_menu;

bool fbwl_menu_parse_file(struct fbwl_menu *root, const char *path);
