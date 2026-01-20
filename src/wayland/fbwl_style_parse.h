#pragma once

#include <stdbool.h>

#include "wayland/fbwl_ui_decor_theme.h"

bool fbwl_style_load_file(struct fbwl_decor_theme *theme, const char *path);
