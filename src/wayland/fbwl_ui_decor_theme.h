#pragma once

#include "wayland/fbwl_texture.h"
#include "wayland/fbwl_text_effect.h"

struct fbwl_pixmap_triplet {
    char focus[256];
    char unfocus[256];
    char pressed[256];
};

struct fbwl_decor_theme {
    int border_width;
    int bevel_width;
    int window_bevel_width;
    int handle_width;
    int title_height;
    int button_margin;
    int button_spacing;
    int window_justify; // 0=left, 1=center, 2=right
    bool background_loaded;
    char background_options[128];
    char background_pixmap[256];
    int background_mod_x;
    int background_mod_y;
    int menu_item_height;
    int menu_title_height;
    int menu_border_width;
    int menu_bevel_width;
    int menu_frame_justify; // 0=left, 1=center, 2=right
    int menu_hilite_justify; // 0=left, 1=center, 2=right
    int menu_title_justify; // 0=left, 1=center, 2=right
    int menu_bullet; // 0=empty, 1=square, 2=triangle, 3=diamond
    int menu_bullet_pos; // 0=left, 2=right
    int toolbar_height;
    int toolbar_border_width;
    int toolbar_bevel_width;
    int toolbar_clock_justify; // 0=left, 1=center, 2=right
    int toolbar_workspace_justify; // 0=left, 1=center, 2=right
    int toolbar_iconbar_focused_justify; // 0=left, 1=center, 2=right
    int toolbar_iconbar_unfocused_justify; // 0=left, 1=center, 2=right
    int toolbar_clock_border_width;
    int toolbar_workspace_border_width;
    int toolbar_iconbar_border_width;
    int toolbar_iconbar_focused_border_width;
    int toolbar_iconbar_unfocused_border_width;
    int slit_border_width;
    int slit_bevel_width;
    char window_font[128];
    char menu_font[128];
    char menu_title_font[128]; // optional; falls back to menu_font
    char menu_hilite_font[128]; // optional; falls back to menu_font
    char toolbar_font[128];

    // Fluxbox/X11 font effects (shadow/halo). These match the style resources
    // `*.font.effect`, `*.font.shadow.*`, and `*.font.halo.*`.
    struct fbwl_text_effect window_label_focus_effect;
    struct fbwl_text_effect window_label_unfocus_effect;
    struct fbwl_text_effect menu_frame_effect;
    struct fbwl_text_effect menu_title_effect;
    struct fbwl_text_effect menu_hilite_effect;
    struct fbwl_text_effect toolbar_workspace_effect;
    struct fbwl_text_effect toolbar_iconbar_focused_effect;
    struct fbwl_text_effect toolbar_iconbar_unfocused_effect;
    struct fbwl_text_effect toolbar_clock_effect;
    struct fbwl_text_effect toolbar_label_effect;
    struct fbwl_text_effect toolbar_windowlabel_effect;

    float titlebar_active[4];
    float titlebar_inactive[4];
    float border_color_focus[4];
    float border_color_unfocus[4];
    float title_text_active[4];
    float title_text_inactive[4];
    float menu_bg[4];
    float menu_hilite[4];
    float menu_text[4];
    float menu_title_text[4];
    float menu_hilite_text[4];
    float menu_disable_text[4];
    float menu_underline_color[4];
    float menu_border_color[4];
    float toolbar_bg[4];
    float toolbar_hilite[4];
    float toolbar_text[4];
    float toolbar_iconbar_focused[4];
    float toolbar_border_color[4];
    float toolbar_clock_border_color[4];
    float toolbar_workspace_border_color[4];
    float toolbar_iconbar_border_color[4];
    float toolbar_iconbar_focused_border_color[4];
    float toolbar_iconbar_unfocused_border_color[4];
    float slit_border_color[4];
    float btn_menu_color[4];
    float btn_shade_color[4];
    float btn_stick_color[4];
    float btn_close_color[4];
    float btn_max_color[4];
    float btn_min_color[4];
    float btn_lhalf_color[4];
    float btn_rhalf_color[4];

    int window_tab_border_width;
    float window_tab_border_color[4];
    int window_tab_justify; // 0=left, 1=center, 2=right
    char window_tab_font[128];
    float window_tab_label_focus_text[4];
    float window_tab_label_unfocus_text[4];

    struct fbwl_pixmap_triplet window_menuicon_pm;
    struct fbwl_pixmap_triplet window_shade_pm;
    struct fbwl_pixmap_triplet window_unshade_pm;
    struct fbwl_pixmap_triplet window_stick_pm;
    struct fbwl_pixmap_triplet window_stuck_pm;
    struct fbwl_pixmap_triplet window_close_pm;
    struct fbwl_pixmap_triplet window_maximize_pm;
    struct fbwl_pixmap_triplet window_iconify_pm;
    struct fbwl_pixmap_triplet window_lhalf_pm;
    struct fbwl_pixmap_triplet window_rhalf_pm;

    // Fluxbox/X11 style textures (used by the Wayland UI renderer).
    struct fbwl_texture window_title_focus_tex;
    struct fbwl_texture window_title_unfocus_tex;
    struct fbwl_texture window_label_focus_tex;
    struct fbwl_texture window_label_unfocus_tex;
    struct fbwl_texture window_button_focus_tex;
    struct fbwl_texture window_button_unfocus_tex;
    struct fbwl_texture window_button_pressed_tex;
    struct fbwl_texture window_handle_focus_tex;
    struct fbwl_texture window_handle_unfocus_tex;
    struct fbwl_texture window_grip_focus_tex;
    struct fbwl_texture window_grip_unfocus_tex;
    struct fbwl_texture window_tab_label_focus_tex;
    struct fbwl_texture window_tab_label_unfocus_tex;
    struct fbwl_texture menu_title_tex;
    struct fbwl_texture menu_frame_tex;
    struct fbwl_texture menu_hilite_tex;
    struct fbwl_texture toolbar_tex;
    struct fbwl_texture slit_tex; // when unset, Fluxbox uses toolbar look
    struct fbwl_texture toolbar_clock_tex;
    struct fbwl_texture toolbar_workspace_tex;
    struct fbwl_texture toolbar_label_tex; // legacy alias for toolbar.workspace
    struct fbwl_texture toolbar_windowlabel_tex; // legacy alias used as iconbar fallback
    struct fbwl_texture toolbar_button_tex;
    struct fbwl_texture toolbar_button_pressed_tex;
    struct fbwl_texture toolbar_systray_tex;
    struct fbwl_texture toolbar_iconbar_tex;
    struct fbwl_texture toolbar_iconbar_empty_tex;
    struct fbwl_texture toolbar_iconbar_focused_tex;
    struct fbwl_texture toolbar_iconbar_unfocused_tex;
    struct fbwl_texture background_tex;

    // Menu pixmaps (best-effort).
    char menu_submenu_pixmap[256];
    char menu_selected_pixmap[256];
    char menu_unselected_pixmap[256];
    char menu_hilite_submenu_pixmap[256];
    char menu_hilite_selected_pixmap[256];
    char menu_hilite_unselected_pixmap[256];

    // Style parser bookkeeping. These flags are used to preserve Fluxbox/X11
    // fallback semantics across multiple style loads (e.g. style + overlay).
    bool toolbar_border_width_explicit;
    bool toolbar_border_color_explicit;
    bool toolbar_bevel_width_explicit;
    bool toolbar_clock_border_width_explicit;
    bool toolbar_clock_border_color_explicit;
    bool toolbar_workspace_border_width_explicit;
    bool toolbar_workspace_border_color_explicit;
    bool toolbar_iconbar_border_width_explicit;
    bool toolbar_iconbar_border_color_explicit;
    bool toolbar_iconbar_focused_border_width_explicit;
    bool toolbar_iconbar_focused_border_color_explicit;
    bool toolbar_iconbar_unfocused_border_width_explicit;
    bool toolbar_iconbar_unfocused_border_color_explicit;
    bool slit_border_width_explicit;
    bool slit_border_color_explicit;
    bool slit_bevel_width_explicit;
    bool slit_texture_explicit;
    bool toolbar_clock_texture_explicit;
    bool toolbar_workspace_texture_explicit;
    bool toolbar_label_texture_explicit;
    bool toolbar_windowlabel_texture_explicit;
    bool toolbar_button_texture_explicit;
    bool toolbar_button_pressed_texture_explicit;
    bool toolbar_systray_texture_explicit;
    bool toolbar_iconbar_texture_explicit;
    bool toolbar_iconbar_empty_texture_explicit;
    bool toolbar_iconbar_focused_texture_explicit;
    bool toolbar_iconbar_unfocused_texture_explicit;
    bool window_bevel_width_explicit;
};
