#include "wayland/fbwl_style_parse_font_effects.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_util.h"

static void strip_quotes_inplace(char *s) {
    if (s == NULL) {
        return;
    }
    size_t len = strlen(s);
    if (len < 2) {
        return;
    }
    if ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\'')) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static void trim_inplace(char *s) {
    if (s == NULL) {
        return;
    }
    char *start = s;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static bool parse_int_strict(const char *s, int *out) {
    if (out != NULL) {
        *out = 0;
    }
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
    if (end == NULL || end == s) {
        return false;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }
    if (v < -9999 || v > 9999) {
        return false;
    }
    *out = (int)v;
    return true;
}

static enum fbwl_text_effect_kind parse_effect_kind(const char *val) {
    if (val == NULL) {
        return FBWL_TEXT_EFFECT_NONE;
    }

    char buf[64];
    strncpy(buf, val, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    trim_inplace(buf);
    strip_quotes_inplace(buf);

    if (strcasecmp(buf, "shadow") == 0) {
        return FBWL_TEXT_EFFECT_SHADOW;
    }
    if (strcasecmp(buf, "halo") == 0) {
        return FBWL_TEXT_EFFECT_HALO;
    }
    return FBWL_TEXT_EFFECT_NONE;
}

static void effect_apply_kind(struct fbwl_text_effect *effect, uint8_t prio, const char *val) {
    if (effect == NULL) {
        return;
    }
    if (effect->prio_kind > prio) {
        return;
    }
    effect->kind = parse_effect_kind(val);
    effect->prio_kind = prio;
}

static void effect_apply_shadow_color(struct fbwl_text_effect *effect, uint8_t prio, const char *val) {
    if (effect == NULL) {
        return;
    }
    if (effect->prio_shadow_color > prio) {
        return;
    }
    float c[4] = {0};
    if (!fbwl_parse_color(val, c)) {
        return;
    }
    memcpy(effect->shadow_color, c, sizeof(effect->shadow_color));
    effect->prio_shadow_color = prio;
}

static void effect_apply_shadow_x(struct fbwl_text_effect *effect, uint8_t prio, const char *val) {
    if (effect == NULL) {
        return;
    }
    if (effect->prio_shadow_x > prio) {
        return;
    }
    int v = 0;
    if (!parse_int_strict(val, &v)) {
        return;
    }
    effect->shadow_x = v;
    effect->prio_shadow_x = prio;
}

static void effect_apply_shadow_y(struct fbwl_text_effect *effect, uint8_t prio, const char *val) {
    if (effect == NULL) {
        return;
    }
    if (effect->prio_shadow_y > prio) {
        return;
    }
    int v = 0;
    if (!parse_int_strict(val, &v)) {
        return;
    }
    effect->shadow_y = v;
    effect->prio_shadow_y = prio;
}

static void effect_apply_halo_color(struct fbwl_text_effect *effect, uint8_t prio, const char *val) {
    if (effect == NULL) {
        return;
    }
    if (effect->prio_halo_color > prio) {
        return;
    }
    float c[4] = {0};
    if (!fbwl_parse_color(val, c)) {
        return;
    }
    memcpy(effect->halo_color, c, sizeof(effect->halo_color));
    effect->prio_halo_color = prio;
}

static void effects_apply_all(struct fbwl_decor_theme *theme,
        void (*fn)(struct fbwl_text_effect *, uint8_t, const char *), uint8_t prio, const char *val) {
    if (theme == NULL || fn == NULL) {
        return;
    }
    fn(&theme->window_label_focus_effect, prio, val);
    fn(&theme->window_label_unfocus_effect, prio, val);
    fn(&theme->menu_frame_effect, prio, val);
    fn(&theme->menu_title_effect, prio, val);
    fn(&theme->toolbar_workspace_effect, prio, val);
    fn(&theme->toolbar_iconbar_focused_effect, prio, val);
    fn(&theme->toolbar_iconbar_unfocused_effect, prio, val);
    fn(&theme->toolbar_clock_effect, prio, val);
    fn(&theme->toolbar_label_effect, prio, val);
    fn(&theme->toolbar_windowlabel_effect, prio, val);
}

static void effects_apply_window_group(struct fbwl_decor_theme *theme,
        void (*fn)(struct fbwl_text_effect *, uint8_t, const char *), uint8_t prio, const char *val) {
    if (theme == NULL || fn == NULL) {
        return;
    }
    fn(&theme->window_label_focus_effect, prio, val);
    fn(&theme->window_label_unfocus_effect, prio, val);
}

static void effects_apply_toolbar_group(struct fbwl_decor_theme *theme,
        void (*fn)(struct fbwl_text_effect *, uint8_t, const char *), uint8_t prio, const char *val) {
    if (theme == NULL || fn == NULL) {
        return;
    }
    fn(&theme->toolbar_workspace_effect, prio, val);
    fn(&theme->toolbar_iconbar_focused_effect, prio, val);
    fn(&theme->toolbar_iconbar_unfocused_effect, prio, val);
    fn(&theme->toolbar_clock_effect, prio, val);
    fn(&theme->toolbar_label_effect, prio, val);
    fn(&theme->toolbar_windowlabel_effect, prio, val);
}

bool fbwl_style_parse_font_effects(struct fbwl_decor_theme *theme, const char *key, const char *val) {
    if (theme == NULL || key == NULL || *key == '\0' || val == NULL) {
        return false;
    }

    // Wildcards (Fluxbox supports Xrm patterns; themes commonly use these).
    if (strcasecmp(key, "*.font.effect") == 0 || strcasecmp(key, "*font.effect") == 0 ||
            strcasecmp(key, "*.effect") == 0 || strcasecmp(key, "*effect") == 0) {
        effects_apply_all(theme, effect_apply_kind, 0, val);
        return true;
    }
    if (strcasecmp(key, "*.font.shadow.color") == 0 || strcasecmp(key, "*font.shadow.color") == 0 ||
            strcasecmp(key, "*.shadow.color") == 0 || strcasecmp(key, "*shadow.color") == 0) {
        effects_apply_all(theme, effect_apply_shadow_color, 0, val);
        return true;
    }
    if (strcasecmp(key, "*.font.shadow.x") == 0 || strcasecmp(key, "*font.shadow.x") == 0 ||
            strcasecmp(key, "*.shadow.x") == 0 || strcasecmp(key, "*shadow.x") == 0) {
        effects_apply_all(theme, effect_apply_shadow_x, 0, val);
        return true;
    }
    if (strcasecmp(key, "*.font.shadow.y") == 0 || strcasecmp(key, "*font.shadow.y") == 0 ||
            strcasecmp(key, "*.shadow.y") == 0 || strcasecmp(key, "*shadow.y") == 0) {
        effects_apply_all(theme, effect_apply_shadow_y, 0, val);
        return true;
    }
    if (strcasecmp(key, "*.font.halo.color") == 0 || strcasecmp(key, "*font.halo.color") == 0 ||
            strcasecmp(key, "*.halo.color") == 0 || strcasecmp(key, "*halo.color") == 0) {
        effects_apply_all(theme, effect_apply_halo_color, 0, val);
        return true;
    }

    // Window group.
    if (strcasecmp(key, "window.font.effect") == 0 || strcasecmp(key, "window.effect") == 0) {
        effects_apply_window_group(theme, effect_apply_kind, 1, val);
        return true;
    }
    if (strcasecmp(key, "window.font.shadow.color") == 0 || strcasecmp(key, "window.shadow.color") == 0) {
        effects_apply_window_group(theme, effect_apply_shadow_color, 1, val);
        return true;
    }
    if (strcasecmp(key, "window.font.shadow.x") == 0 || strcasecmp(key, "window.shadow.x") == 0) {
        effects_apply_window_group(theme, effect_apply_shadow_x, 1, val);
        return true;
    }
    if (strcasecmp(key, "window.font.shadow.y") == 0 || strcasecmp(key, "window.shadow.y") == 0) {
        effects_apply_window_group(theme, effect_apply_shadow_y, 1, val);
        return true;
    }
    if (strcasecmp(key, "window.font.halo.color") == 0 || strcasecmp(key, "window.halo.color") == 0) {
        effects_apply_window_group(theme, effect_apply_halo_color, 1, val);
        return true;
    }

    // Window label.
    if (strcasecmp(key, "window.label.focus.font.effect") == 0 || strcasecmp(key, "window.label.focus.effect") == 0 ||
            strcasecmp(key, "window.label.active.font.effect") == 0 || strcasecmp(key, "window.label.active.effect") == 0) {
        effect_apply_kind(&theme->window_label_focus_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "window.label.focus.font.shadow.color") == 0 || strcasecmp(key, "window.label.focus.shadow.color") == 0 ||
            strcasecmp(key, "window.label.active.font.shadow.color") == 0 || strcasecmp(key, "window.label.active.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->window_label_focus_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "window.label.focus.font.shadow.x") == 0 || strcasecmp(key, "window.label.focus.shadow.x") == 0 ||
            strcasecmp(key, "window.label.active.font.shadow.x") == 0 || strcasecmp(key, "window.label.active.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->window_label_focus_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "window.label.focus.font.shadow.y") == 0 || strcasecmp(key, "window.label.focus.shadow.y") == 0 ||
            strcasecmp(key, "window.label.active.font.shadow.y") == 0 || strcasecmp(key, "window.label.active.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->window_label_focus_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "window.label.focus.font.halo.color") == 0 || strcasecmp(key, "window.label.focus.halo.color") == 0 ||
            strcasecmp(key, "window.label.active.font.halo.color") == 0 || strcasecmp(key, "window.label.active.halo.color") == 0) {
        effect_apply_halo_color(&theme->window_label_focus_effect, 2, val);
        return true;
    }

    if (strcasecmp(key, "window.label.unfocus.font.effect") == 0 || strcasecmp(key, "window.label.unfocus.effect") == 0) {
        effect_apply_kind(&theme->window_label_unfocus_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "window.label.unfocus.font.shadow.color") == 0 || strcasecmp(key, "window.label.unfocus.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->window_label_unfocus_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "window.label.unfocus.font.shadow.x") == 0 || strcasecmp(key, "window.label.unfocus.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->window_label_unfocus_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "window.label.unfocus.font.shadow.y") == 0 || strcasecmp(key, "window.label.unfocus.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->window_label_unfocus_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "window.label.unfocus.font.halo.color") == 0 || strcasecmp(key, "window.label.unfocus.halo.color") == 0) {
        effect_apply_halo_color(&theme->window_label_unfocus_effect, 2, val);
        return true;
    }

    // Menu.
    if (strcasecmp(key, "menu.frame.font.effect") == 0 || strcasecmp(key, "menu.frame.effect") == 0) {
        effect_apply_kind(&theme->menu_frame_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "menu.frame.font.shadow.color") == 0 || strcasecmp(key, "menu.frame.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->menu_frame_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "menu.frame.font.shadow.x") == 0 || strcasecmp(key, "menu.frame.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->menu_frame_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "menu.frame.font.shadow.y") == 0 || strcasecmp(key, "menu.frame.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->menu_frame_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "menu.frame.font.halo.color") == 0 || strcasecmp(key, "menu.frame.halo.color") == 0) {
        effect_apply_halo_color(&theme->menu_frame_effect, 2, val);
        return true;
    }

    if (strcasecmp(key, "menu.title.font.effect") == 0 || strcasecmp(key, "menu.title.effect") == 0) {
        effect_apply_kind(&theme->menu_title_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "menu.title.font.shadow.color") == 0 || strcasecmp(key, "menu.title.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->menu_title_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "menu.title.font.shadow.x") == 0 || strcasecmp(key, "menu.title.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->menu_title_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "menu.title.font.shadow.y") == 0 || strcasecmp(key, "menu.title.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->menu_title_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "menu.title.font.halo.color") == 0 || strcasecmp(key, "menu.title.halo.color") == 0) {
        effect_apply_halo_color(&theme->menu_title_effect, 2, val);
        return true;
    }

    // Toolbar group + specifics.
    if (strcasecmp(key, "toolbar.font.effect") == 0 || strcasecmp(key, "toolbar.effect") == 0) {
        effects_apply_toolbar_group(theme, effect_apply_kind, 1, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.font.shadow.color") == 0 || strcasecmp(key, "toolbar.shadow.color") == 0) {
        effects_apply_toolbar_group(theme, effect_apply_shadow_color, 1, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.font.shadow.x") == 0 || strcasecmp(key, "toolbar.shadow.x") == 0) {
        effects_apply_toolbar_group(theme, effect_apply_shadow_x, 1, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.font.shadow.y") == 0 || strcasecmp(key, "toolbar.shadow.y") == 0) {
        effects_apply_toolbar_group(theme, effect_apply_shadow_y, 1, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.font.halo.color") == 0 || strcasecmp(key, "toolbar.halo.color") == 0) {
        effects_apply_toolbar_group(theme, effect_apply_halo_color, 1, val);
        return true;
    }

    if (strcasecmp(key, "toolbar.workspace.font.effect") == 0 || strcasecmp(key, "toolbar.workspace.effect") == 0) {
        effect_apply_kind(&theme->toolbar_workspace_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.workspace.font.shadow.color") == 0 || strcasecmp(key, "toolbar.workspace.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->toolbar_workspace_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.workspace.font.shadow.x") == 0 || strcasecmp(key, "toolbar.workspace.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->toolbar_workspace_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.workspace.font.shadow.y") == 0 || strcasecmp(key, "toolbar.workspace.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->toolbar_workspace_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.workspace.font.halo.color") == 0 || strcasecmp(key, "toolbar.workspace.halo.color") == 0) {
        effect_apply_halo_color(&theme->toolbar_workspace_effect, 2, val);
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.focused.font.effect") == 0 || strcasecmp(key, "toolbar.iconbar.focused.effect") == 0) {
        effect_apply_kind(&theme->toolbar_iconbar_focused_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.focused.font.shadow.color") == 0 || strcasecmp(key, "toolbar.iconbar.focused.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->toolbar_iconbar_focused_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.focused.font.shadow.x") == 0 || strcasecmp(key, "toolbar.iconbar.focused.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->toolbar_iconbar_focused_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.focused.font.shadow.y") == 0 || strcasecmp(key, "toolbar.iconbar.focused.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->toolbar_iconbar_focused_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.focused.font.halo.color") == 0 || strcasecmp(key, "toolbar.iconbar.focused.halo.color") == 0) {
        effect_apply_halo_color(&theme->toolbar_iconbar_focused_effect, 2, val);
        return true;
    }

    if (strcasecmp(key, "toolbar.iconbar.unfocused.font.effect") == 0 || strcasecmp(key, "toolbar.iconbar.unfocused.effect") == 0) {
        effect_apply_kind(&theme->toolbar_iconbar_unfocused_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.unfocused.font.shadow.color") == 0 || strcasecmp(key, "toolbar.iconbar.unfocused.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->toolbar_iconbar_unfocused_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.unfocused.font.shadow.x") == 0 || strcasecmp(key, "toolbar.iconbar.unfocused.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->toolbar_iconbar_unfocused_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.unfocused.font.shadow.y") == 0 || strcasecmp(key, "toolbar.iconbar.unfocused.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->toolbar_iconbar_unfocused_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.unfocused.font.halo.color") == 0 || strcasecmp(key, "toolbar.iconbar.unfocused.halo.color") == 0) {
        effect_apply_halo_color(&theme->toolbar_iconbar_unfocused_effect, 2, val);
        return true;
    }

    if (strcasecmp(key, "toolbar.clock.font.effect") == 0 || strcasecmp(key, "toolbar.clock.effect") == 0) {
        effect_apply_kind(&theme->toolbar_clock_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.clock.font.shadow.color") == 0 || strcasecmp(key, "toolbar.clock.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->toolbar_clock_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.clock.font.shadow.x") == 0 || strcasecmp(key, "toolbar.clock.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->toolbar_clock_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.clock.font.shadow.y") == 0 || strcasecmp(key, "toolbar.clock.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->toolbar_clock_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.clock.font.halo.color") == 0 || strcasecmp(key, "toolbar.clock.halo.color") == 0) {
        effect_apply_halo_color(&theme->toolbar_clock_effect, 2, val);
        return true;
    }

    if (strcasecmp(key, "toolbar.label.font.effect") == 0 || strcasecmp(key, "toolbar.label.effect") == 0) {
        effect_apply_kind(&theme->toolbar_label_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.label.font.shadow.color") == 0 || strcasecmp(key, "toolbar.label.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->toolbar_label_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.label.font.shadow.x") == 0 || strcasecmp(key, "toolbar.label.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->toolbar_label_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.label.font.shadow.y") == 0 || strcasecmp(key, "toolbar.label.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->toolbar_label_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.label.font.halo.color") == 0 || strcasecmp(key, "toolbar.label.halo.color") == 0) {
        effect_apply_halo_color(&theme->toolbar_label_effect, 2, val);
        return true;
    }

    if (strcasecmp(key, "toolbar.windowLabel.font.effect") == 0 || strcasecmp(key, "toolbar.windowLabel.effect") == 0) {
        effect_apply_kind(&theme->toolbar_windowlabel_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.windowLabel.font.shadow.color") == 0 || strcasecmp(key, "toolbar.windowLabel.shadow.color") == 0) {
        effect_apply_shadow_color(&theme->toolbar_windowlabel_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.windowLabel.font.shadow.x") == 0 || strcasecmp(key, "toolbar.windowLabel.shadow.x") == 0) {
        effect_apply_shadow_x(&theme->toolbar_windowlabel_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.windowLabel.font.shadow.y") == 0 || strcasecmp(key, "toolbar.windowLabel.shadow.y") == 0) {
        effect_apply_shadow_y(&theme->toolbar_windowlabel_effect, 2, val);
        return true;
    }
    if (strcasecmp(key, "toolbar.windowLabel.font.halo.color") == 0 || strcasecmp(key, "toolbar.windowLabel.halo.color") == 0) {
        effect_apply_halo_color(&theme->toolbar_windowlabel_effect, 2, val);
        return true;
    }

    return false;
}
