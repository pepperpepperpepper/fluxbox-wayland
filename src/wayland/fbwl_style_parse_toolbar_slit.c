#include "wayland/fbwl_style_parse_toolbar_slit.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_util.h"

static bool parse_int_range(const char *s, int min_incl, int max_incl, int *out) {
    if (s == NULL || out == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || end == NULL) {
        return false;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }

    if (v < min_incl || v > max_incl) {
        return false;
    }

    *out = (int)v;
    return true;
}

static bool parse_bool(const char *s, bool *out) {
    if (s == NULL || out == NULL) {
        return false;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }
    if (strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 || strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(s, "false") == 0 || strcasecmp(s, "no") == 0 || strcasecmp(s, "off") == 0 || strcmp(s, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static void copy_color(float dst[static 4], const float src[static 4]) {
    if (dst == NULL || src == NULL) {
        return;
    }
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

static void texture_set_color(struct fbwl_texture *tex, const float c[static 4]) {
    if (tex == NULL || c == NULL) {
        return;
    }
    copy_color(tex->color, c);
    copy_color(tex->color_to, c);
}

bool fbwl_style_parse_toolbar_slit(struct fbwl_decor_theme *theme, const char *key, char *val) {
    if (theme == NULL || key == NULL || val == NULL) {
        return false;
    }

    if (strcasecmp(key, "toolbar.borderWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->toolbar_border_width = v > 20 ? 20 : v;
            theme->toolbar_border_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.borderColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_border_color, c);
            theme->toolbar_border_color_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.bevelWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->toolbar_bevel_width = v > 20 ? 20 : v;
            theme->toolbar_bevel_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.shaped") == 0) {
        bool v = false;
        if (parse_bool(val, &v)) {
            theme->toolbar_shaped = v;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.button.scale") == 0) {
        int v = 0;
        if (parse_int_range(val, 1, 10000, &v)) {
            theme->toolbar_button_scale = v;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.clock.borderWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->toolbar_clock_border_width = v > 20 ? 20 : v;
            theme->toolbar_clock_border_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.clock.borderColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_clock_border_color, c);
            theme->toolbar_clock_border_color_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.workspace.borderWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->toolbar_workspace_border_width = v > 20 ? 20 : v;
            theme->toolbar_workspace_border_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.workspace.borderColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_workspace_border_color, c);
            theme->toolbar_workspace_border_color_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.borderWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->toolbar_iconbar_border_width = v > 20 ? 20 : v;
            theme->toolbar_iconbar_border_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.borderColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_iconbar_border_color, c);
            theme->toolbar_iconbar_border_color_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.focused.borderWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->toolbar_iconbar_focused_border_width = v > 20 ? 20 : v;
            theme->toolbar_iconbar_focused_border_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.focused.borderColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_iconbar_focused_border_color, c);
            theme->toolbar_iconbar_focused_border_color_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.unfocused.borderWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->toolbar_iconbar_unfocused_border_width = v > 20 ? 20 : v;
            theme->toolbar_iconbar_unfocused_border_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.unfocused.borderColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_iconbar_unfocused_border_color, c);
            theme->toolbar_iconbar_unfocused_border_color_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.clock.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_clock_tex, c);
            theme->toolbar_clock_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.clock.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_clock_tex.color_to, c);
            theme->toolbar_clock_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.clock.picColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_clock_tex.pic_color, c);
            theme->toolbar_clock_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.workspace.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_workspace_tex, c);
            theme->toolbar_workspace_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.workspace.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_workspace_tex.color_to, c);
            theme->toolbar_workspace_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.label.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_label_tex, c);
            theme->toolbar_label_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.label.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_label_tex.color_to, c);
            theme->toolbar_label_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.windowLabel.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_windowlabel_tex, c);
            theme->toolbar_windowlabel_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.windowLabel.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_windowlabel_tex.color_to, c);
            theme->toolbar_windowlabel_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.button.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_button_tex, c);
            theme->toolbar_button_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.button.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_button_tex.color_to, c);
            theme->toolbar_button_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.button.picColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_button_tex.pic_color, c);
            theme->toolbar_button_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.button.pressed.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_button_pressed_tex, c);
            theme->toolbar_button_pressed_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.button.pressed.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_button_pressed_tex.color_to, c);
            theme->toolbar_button_pressed_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.button.pressed.picColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_button_pressed_tex.pic_color, c);
            theme->toolbar_button_pressed_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.systray.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_systray_tex, c);
            theme->toolbar_systray_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.systray.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_systray_tex.color_to, c);
            theme->toolbar_systray_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.systray.picColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_systray_tex.pic_color, c);
            theme->toolbar_systray_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_iconbar_tex, c);
            theme->toolbar_iconbar_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_iconbar_tex.color_to, c);
            theme->toolbar_iconbar_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.empty.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_iconbar_empty_tex, c);
            theme->toolbar_iconbar_empty_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.empty.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_iconbar_empty_tex.color_to, c);
            theme->toolbar_iconbar_empty_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.focused.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_iconbar_focused_tex, c);
            theme->toolbar_iconbar_focused_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.focused.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_iconbar_focused_tex.color_to, c);
            theme->toolbar_iconbar_focused_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.unfocused.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            texture_set_color(&theme->toolbar_iconbar_unfocused_tex, c);
            theme->toolbar_iconbar_unfocused_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.unfocused.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->toolbar_iconbar_unfocused_tex.color_to, c);
            theme->toolbar_iconbar_unfocused_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "slit.borderWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->slit_border_width = v > 20 ? 20 : v;
            theme->slit_border_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "slit.borderColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->slit_border_color, c);
            theme->slit_border_color_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "slit.bevelWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 999, &v)) {
            theme->slit_bevel_width = v > 20 ? 20 : v;
            theme->slit_bevel_width_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "slit.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->slit_tex.color, c);
            copy_color(theme->slit_tex.color_to, c);
            theme->slit_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "slit.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->slit_tex.color_to, c);
            theme->slit_texture_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "slit.picColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->slit_tex.pic_color, c);
            theme->slit_texture_explicit = true;
        }
        return true;
    }

    return false;
}

void fbwl_style_apply_toolbar_slit_fallbacks(struct fbwl_decor_theme *theme) {
    if (theme == NULL) {
        return;
    }

    int border_w = theme->border_width;
    if (border_w < 0) {
        border_w = 0;
    }
    if (border_w > 20) {
        border_w = 20;
    }

    int bevel_w = theme->bevel_width;
    if (bevel_w < 0) {
        bevel_w = 0;
    }
    if (bevel_w > 20) {
        bevel_w = 20;
    }

    if (!theme->toolbar_border_width_explicit) {
        theme->toolbar_border_width = border_w;
    }
    if (!theme->toolbar_border_color_explicit) {
        copy_color(theme->toolbar_border_color, theme->border_color_unfocus);
    }
    if (!theme->toolbar_bevel_width_explicit) {
        theme->toolbar_bevel_width = bevel_w;
    }

    if (!theme->slit_border_width_explicit) {
        theme->slit_border_width = border_w;
    }
    if (!theme->slit_border_color_explicit) {
        copy_color(theme->slit_border_color, theme->border_color_unfocus);
    }

    if (theme->slit_bevel_width < 0) {
        theme->slit_bevel_width = 0;
    }
    if (theme->slit_bevel_width > 20) {
        theme->slit_bevel_width = 20;
    }

    if (!theme->slit_texture_explicit) {
        theme->slit_tex = theme->toolbar_tex;
    }

    // Toolbar tool border fallbacks (Fluxbox/X11).
    if (theme->toolbar_clock_border_width < 0) {
        theme->toolbar_clock_border_width = 0;
    }
    if (theme->toolbar_clock_border_width > 20) {
        theme->toolbar_clock_border_width = 20;
    }
    if (theme->toolbar_workspace_border_width < 0) {
        theme->toolbar_workspace_border_width = 0;
    }
    if (theme->toolbar_workspace_border_width > 20) {
        theme->toolbar_workspace_border_width = 20;
    }

    if (!theme->toolbar_iconbar_border_width_explicit) {
        theme->toolbar_iconbar_border_width = border_w;
    }
    if (theme->toolbar_iconbar_border_width < 0) {
        theme->toolbar_iconbar_border_width = 0;
    }
    if (theme->toolbar_iconbar_border_width > 20) {
        theme->toolbar_iconbar_border_width = 20;
    }
    if (!theme->toolbar_iconbar_border_color_explicit) {
        copy_color(theme->toolbar_iconbar_border_color, theme->border_color_unfocus);
    }
    if (!theme->toolbar_iconbar_focused_border_width_explicit) {
        theme->toolbar_iconbar_focused_border_width = theme->toolbar_iconbar_border_width;
    }
    if (!theme->toolbar_iconbar_focused_border_color_explicit) {
        copy_color(theme->toolbar_iconbar_focused_border_color, theme->toolbar_iconbar_border_color);
    }
    if (!theme->toolbar_iconbar_unfocused_border_width_explicit) {
        theme->toolbar_iconbar_unfocused_border_width = theme->toolbar_iconbar_border_width;
    }
    if (!theme->toolbar_iconbar_unfocused_border_color_explicit) {
        copy_color(theme->toolbar_iconbar_unfocused_border_color, theme->toolbar_iconbar_border_color);
    }

    // Toolbar tool texture fallbacks (Fluxbox/X11).
    if (!theme->toolbar_clock_texture_explicit) {
        theme->toolbar_clock_tex = theme->toolbar_tex;
    }
    if (!theme->toolbar_label_texture_explicit) {
        theme->toolbar_label_tex = theme->toolbar_tex;
    }
    if (!theme->toolbar_windowlabel_texture_explicit) {
        theme->toolbar_windowlabel_tex = theme->toolbar_tex;
    }
    if (!theme->toolbar_workspace_texture_explicit) {
        theme->toolbar_workspace_tex = theme->toolbar_label_tex;
    }
    if (!theme->toolbar_button_texture_explicit) {
        theme->toolbar_button_tex = theme->toolbar_clock_tex;
    }
    if (!theme->toolbar_button_pressed_texture_explicit) {
        theme->toolbar_button_pressed_tex = theme->toolbar_button_tex;
        uint32_t type = theme->toolbar_button_pressed_tex.type;
        const uint32_t bevels = FBWL_TEXTURE_SUNKEN | FBWL_TEXTURE_RAISED;
        if ((type & bevels) != 0) {
            type ^= bevels;
            theme->toolbar_button_pressed_tex.type = type;
        }
    }
    if (!theme->toolbar_systray_texture_explicit) {
        theme->toolbar_systray_tex = theme->toolbar_clock_tex;
    }
    if (!theme->toolbar_iconbar_texture_explicit) {
        theme->toolbar_iconbar_tex = theme->toolbar_windowlabel_tex;
    }
    if (!theme->toolbar_iconbar_focused_texture_explicit) {
        theme->toolbar_iconbar_focused_tex = theme->toolbar_windowlabel_tex;
    }
    if (!theme->toolbar_iconbar_unfocused_texture_explicit) {
        theme->toolbar_iconbar_unfocused_tex = theme->toolbar_windowlabel_tex;
    }
    if (!theme->toolbar_iconbar_empty_texture_explicit) {
        theme->toolbar_iconbar_empty_tex = theme->toolbar_iconbar_tex;
    }
}
