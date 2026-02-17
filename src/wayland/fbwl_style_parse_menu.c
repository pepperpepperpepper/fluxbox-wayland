#include "wayland/fbwl_style_parse_menu.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "wayland/fbwl_round_corners.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_util.h"

static char *trim_inplace(char *s) {
    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static char *unquote_inplace(char *s) {
    s = trim_inplace(s);
    if (s == NULL) {
        return NULL;
    }
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\''))) {
        s[len - 1] = '\0';
        s++;
    }
    return s;
}

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

static void copy_color(float dst[static 4], const float src[static 4]) {
    if (dst == NULL || src == NULL) {
        return;
    }
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL || *needle == '\0') {
        return false;
    }
    const size_t nlen = strlen(needle);
    const size_t hlen = strlen(haystack);
    if (nlen > hlen) {
        return false;
    }
    for (size_t i = 0; i + nlen <= hlen; i++) {
        bool ok = true;
        for (size_t j = 0; j < nlen; j++) {
            if ((char)tolower((unsigned char)haystack[i + j]) != (char)tolower((unsigned char)needle[j])) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

static int parse_justify0_1_2(char *val) {
    char *j = unquote_inplace(val);
    if (j == NULL || *j == '\0') {
        return 0;
    }
    if (strcasecmp(j, "left") == 0) {
        return 0;
    }
    if (strcasecmp(j, "center") == 0) {
        return 1;
    }
    if (strcasecmp(j, "right") == 0) {
        return 2;
    }
    return 0;
}

bool fbwl_style_parse_menu(struct fbwl_decor_theme *theme, const char *key, char *val,
        struct fbwl_style_menu_parse_state *state) {
    if (theme == NULL || key == NULL || val == NULL) {
        return false;
    }

    if (strcasecmp(key, "menu.itemHeight") == 0) {
        int v = 0;
        if (parse_int_range(val, 1, 999, &v)) {
            theme->menu_item_height = v;
        }
        return true;
    }

    if (strcasecmp(key, "menu.titleHeight") == 0) {
        int v = 0;
        if (parse_int_range(val, 1, 999, &v)) {
            theme->menu_title_height = v;
        }
        return true;
    }

    if (strcasecmp(key, "menu.borderWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 20, &v)) {
            theme->menu_border_width = v;
            if (state != NULL) {
                state->border_width_explicit = true;
            }
        }
        return true;
    }

    if (strcasecmp(key, "menu.bevelWidth") == 0) {
        int v = 0;
        if (parse_int_range(val, 0, 20, &v)) {
            theme->menu_bevel_width = v;
            if (state != NULL) {
                state->bevel_width_explicit = true;
            }
        }
        return true;
    }

    if (strcasecmp(key, "menu.borderColor") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->menu_border_color, c);
            if (state != NULL) {
                state->border_color_explicit = true;
            }
        }
        return true;
    }

    if (strcasecmp(key, "menu.frame.font") == 0) {
        char *font = unquote_inplace(val);
        if (font != NULL && *font != '\0') {
            if (state == NULL || state->font_prio <= 2) {
                (void)snprintf(theme->menu_font, sizeof(theme->menu_font), "%s", font);
                if (state != NULL) {
                    state->font_prio = 2;
                }
            }
        }
        return true;
    }

    if (strcasecmp(key, "menu.title.font") == 0) {
        char *font = unquote_inplace(val);
        if (font != NULL && *font != '\0') {
            (void)snprintf(theme->menu_title_font, sizeof(theme->menu_title_font), "%s", font);
        }
        return true;
    }

    if (strcasecmp(key, "menu.hilite.font") == 0) {
        char *font = unquote_inplace(val);
        if (font != NULL && *font != '\0') {
            (void)snprintf(theme->menu_hilite_font, sizeof(theme->menu_hilite_font), "%s", font);
        }
        return true;
    }

    if (strcasecmp(key, "menu.frame.justify") == 0) {
        const int justify = parse_justify0_1_2(val);
        theme->menu_frame_justify = justify;
        if (state == NULL || !state->hilite_justify_explicit) {
            theme->menu_hilite_justify = justify;
        }
        return true;
    }

    if (strcasecmp(key, "menu.hilite.justify") == 0) {
        theme->menu_hilite_justify = parse_justify0_1_2(val);
        if (state != NULL) {
            state->hilite_justify_explicit = true;
        }
        return true;
    }

    if (strcasecmp(key, "menu.title.justify") == 0) {
        theme->menu_title_justify = parse_justify0_1_2(val);
        return true;
    }

    if (strcasecmp(key, "menu.bullet.position") == 0) {
        const int j = parse_justify0_1_2(val);
        theme->menu_bullet_pos = j == 2 ? 2 : 0;
        return true;
    }

    if (strcasecmp(key, "menu.bullet") == 0) {
        char *b = unquote_inplace(val);
        if (b == NULL) {
            return true;
        }
        if (str_contains_ci(b, "empty")) {
            theme->menu_bullet = 0;
        } else if (str_contains_ci(b, "square")) {
            theme->menu_bullet = 1;
        } else if (str_contains_ci(b, "triangle")) {
            theme->menu_bullet = 2;
        } else if (str_contains_ci(b, "diamond")) {
            theme->menu_bullet = 3;
        } else {
            theme->menu_bullet = 0;
        }
        return true;
    }

    if (strcasecmp(key, "menu.frame.underlineColor") == 0) {
        (void)fbwl_parse_color(val, theme->menu_underline_color);
        return true;
    }

    if (strcasecmp(key, "menu.frame.color") == 0 || strcasecmp(key, "menu.color") == 0) {
        if (fbwl_parse_color(val, theme->menu_bg)) {
            copy_color(theme->menu_frame_tex.color, theme->menu_bg);
        }
        return true;
    }

    if (strcasecmp(key, "menu.title.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->menu_title_tex.color, c);
        }
        return true;
    }

    if (strcasecmp(key, "menu.hilite.color") == 0) {
        if (fbwl_parse_color(val, theme->menu_hilite)) {
            copy_color(theme->menu_hilite_tex.color, theme->menu_hilite);
        }
        return true;
    }

    if (strcasecmp(key, "menu.frame.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->menu_frame_tex.color_to, c);
        }
        return true;
    }

    if (strcasecmp(key, "menu.title.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->menu_title_tex.color_to, c);
        }
        return true;
    }

    if (strcasecmp(key, "menu.hilite.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            copy_color(theme->menu_hilite_tex.color_to, c);
        }
        return true;
    }

    if (strcasecmp(key, "menu.hilite.textColor") == 0) {
        (void)fbwl_parse_color(val, theme->menu_hilite_text);
        return true;
    }

    if (strcasecmp(key, "menu.frame.textColor") == 0) {
        (void)fbwl_parse_color(val, theme->menu_text);
        return true;
    }

    if (strcasecmp(key, "menu.title.textColor") == 0) {
        (void)fbwl_parse_color(val, theme->menu_title_text);
        return true;
    }

    if (strcasecmp(key, "menu.frame.disableColor") == 0) {
        (void)fbwl_parse_color(val, theme->menu_disable_text);
        return true;
    }

    if (strcasecmp(key, "menu.roundCorners") == 0) {
        char *p = unquote_inplace(val);
        theme->menu_round_corners = fbwl_round_corners_parse(p);
        return true;
    }

    return false;
}
