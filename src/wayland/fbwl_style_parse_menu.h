#pragma once

#include <stdbool.h>

struct fbwl_decor_theme;

struct fbwl_style_menu_parse_state {
    int font_prio; // 0="*font", 2="menu.frame.font"
    bool border_width_explicit;
    bool border_color_explicit;
    bool bevel_width_explicit;
    bool hilite_justify_explicit;
};

bool fbwl_style_parse_menu(struct fbwl_decor_theme *theme, const char *key, char *val,
    struct fbwl_style_menu_parse_state *state);
