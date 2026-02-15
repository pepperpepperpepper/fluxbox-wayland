#pragma once

#include <stdbool.h>

struct fbwl_decor_theme;

bool fbwl_style_parse_background(struct fbwl_decor_theme *theme, const char *key, char *val, const char *style_dir);
