#pragma once

struct fbwl_decor_theme {
    int border_width;
    int title_height;
    int button_margin;
    int button_spacing;
    int menu_item_height;
    int toolbar_height;
    char window_font[128];
    char menu_font[128];
    char toolbar_font[128];
    float titlebar_active[4];
    float titlebar_inactive[4];
    float border_color[4];
    float title_text_active[4];
    float title_text_inactive[4];
    float menu_bg[4];
    float menu_hilite[4];
    float menu_text[4];
    float menu_hilite_text[4];
    float menu_disable_text[4];
    float toolbar_bg[4];
    float toolbar_hilite[4];
    float toolbar_text[4];
    float toolbar_iconbar_focused[4];
    float btn_menu_color[4];
    float btn_shade_color[4];
    float btn_stick_color[4];
    float btn_close_color[4];
    float btn_max_color[4];
    float btn_min_color[4];
    float btn_lhalf_color[4];
    float btn_rhalf_color[4];
};
