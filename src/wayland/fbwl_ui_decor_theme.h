#pragma once

#include "wayland/fbwl_texture.h"
#include "wayland/fbwl_text_effect.h"

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

    // Fluxbox/X11 font effects (shadow/halo). These match the style resources
    // `*.font.effect`, `*.font.shadow.*`, and `*.font.halo.*`.
    struct fbwl_text_effect window_label_focus_effect;
    struct fbwl_text_effect window_label_unfocus_effect;
    struct fbwl_text_effect menu_frame_effect;
    struct fbwl_text_effect menu_title_effect;
    struct fbwl_text_effect toolbar_workspace_effect;
    struct fbwl_text_effect toolbar_iconbar_focused_effect;
    struct fbwl_text_effect toolbar_iconbar_unfocused_effect;
    struct fbwl_text_effect toolbar_clock_effect;
    struct fbwl_text_effect toolbar_label_effect;
    struct fbwl_text_effect toolbar_windowlabel_effect;

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

    // Fluxbox/X11 style textures (used by the Wayland UI renderer).
    struct fbwl_texture window_title_focus_tex;
    struct fbwl_texture window_title_unfocus_tex;
    struct fbwl_texture menu_frame_tex;
    struct fbwl_texture menu_hilite_tex;
    struct fbwl_texture toolbar_tex;
    struct fbwl_texture slit_tex; // when unset, Fluxbox uses toolbar look
};
