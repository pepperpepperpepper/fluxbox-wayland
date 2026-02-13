#pragma once

#include <stdbool.h>

struct fbwl_decor_theme;

bool fbwl_style_parse_font_effects(struct fbwl_decor_theme *theme, const char *key, const char *val);
