#include "wayland/fbwl_style_parse_textures.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "wayland/fbwl_texture.h"
#include "wayland/fbwl_ui_decor_theme.h"

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

static bool file_readable(const char *path) {
    return path != NULL && *path != '\0' && access(path, R_OK) == 0;
}

static char *expand_tilde_owned(const char *path) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }
    if (path[0] != '~') {
        return strdup(path);
    }

    const char *home = getenv("HOME");
    if (home == NULL || *home == '\0') {
        return strdup(path);
    }

    const char *tail = path + 1;
    if (*tail == '\0') {
        return strdup(home);
    }
    if (*tail != '/') {
        return strdup(path);
    }

    const size_t home_len = strlen(home);
    const size_t tail_len = strlen(tail);
    const size_t needed = home_len + tail_len + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }
    int n = snprintf(out, needed, "%s%s", home, tail);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

static char *path_join2_owned(const char *a, const char *b) {
    if (a == NULL || *a == '\0' || b == NULL || *b == '\0') {
        return NULL;
    }
    const size_t a_len = strlen(a);
    const size_t b_len = strlen(b);
    const bool need_slash = a[a_len - 1] != '/';
    const size_t needed = a_len + (need_slash ? 1 : 0) + b_len + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }
    int n = snprintf(out, needed, need_slash ? "%s/%s" : "%s%s", a, b);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

static bool resolve_pixmap_path(char out[static 256], const char *style_dir, const char *value) {
    if (out != NULL) {
        out[0] = '\0';
    }
    if (out == NULL || value == NULL) {
        return false;
    }

    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }
    if (*value == '\0') {
        return false;
    }

    char *expanded = expand_tilde_owned(value);
    const char *p = expanded != NULL ? expanded : value;
    char *found = NULL;
    bool ok = false;

    if (*p == '/') {
        if (file_readable(p)) {
            ok = true;
        }
        goto done;
    }

    const char *home = getenv("HOME");

    // Fluxbox theme search paths (best-effort):
    // - <style_dir>/pixmaps/<file>
    // - <style_dir>/<file>
    // - ~/.fluxbox/pixmaps/<file>
    // - /usr/share/fluxbox/pixmaps/<file>
    // - /usr/local/share/fluxbox/pixmaps/<file>
    const char *candidates[] = {
        "pixmaps",
        "",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        const char *sub = candidates[i];
        if (style_dir == NULL || *style_dir == '\0') {
            continue;
        }
        char *base = *sub != '\0' ? path_join2_owned(style_dir, sub) : strdup(style_dir);
        if (base == NULL) {
            continue;
        }
        char *joined = path_join2_owned(base, p);
        free(base);
        if (joined == NULL) {
            continue;
        }
        if (file_readable(joined)) {
            found = joined;
            p = found;
            ok = true;
            goto done;
        }
        free(joined);
    }

    if (home != NULL && *home != '\0') {
        char *dot_flux = path_join2_owned(home, ".fluxbox/pixmaps");
        if (dot_flux != NULL) {
            char *joined = path_join2_owned(dot_flux, p);
            free(dot_flux);
            if (joined != NULL && file_readable(joined)) {
                found = joined;
                p = found;
                ok = true;
                goto done;
            }
            free(joined);
        }
    }

    const char *sys_dirs[] = {"/usr/share/fluxbox/pixmaps", "/usr/local/share/fluxbox/pixmaps"};
    for (size_t i = 0; i < sizeof(sys_dirs) / sizeof(sys_dirs[0]); i++) {
        char *joined = path_join2_owned(sys_dirs[i], p);
        if (joined != NULL && file_readable(joined)) {
            found = joined;
            p = found;
            ok = true;
            goto done;
        }
        free(joined);
    }

done:
    if (ok) {
        (void)snprintf(out, 256, "%s", p);
    } else {
        (void)snprintf(out, 256, "%s", p);
    }
    free(found);
    free(expanded);
    return ok;
}

bool fbwl_style_parse_textures(struct fbwl_decor_theme *theme, const char *key, char *val, const char *style_dir) {
    if (theme == NULL || key == NULL || val == NULL) {
        return false;
    }

    // Texture descriptors.
    if (strcasecmp(key, "window.title.focus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_title_focus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.title.unfocus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_title_unfocus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.label.focus") == 0 || strcasecmp(key, "window.label.active") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_label_focus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.label.unfocus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_label_unfocus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.button.focus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_button_focus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.button.unfocus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_button_unfocus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.button.pressed") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_button_pressed_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.handle.focus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_handle_focus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.handle.unfocus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_handle_unfocus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.grip.focus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_grip_focus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.grip.unfocus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_grip_unfocus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.tab.label.focus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_tab_label_focus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "window.tab.label.unfocus") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->window_tab_label_unfocus_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "menu.frame") == 0 || strcasecmp(key, "menu") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->menu_frame_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "menu.title") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->menu_title_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "menu.hilite") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->menu_hilite_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "slit") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->slit_tex.type = type;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.clock") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_clock_tex.type = type;
            theme->toolbar_clock_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.workspace") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_workspace_tex.type = type;
            theme->toolbar_workspace_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.label") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_label_tex.type = type;
            theme->toolbar_label_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.windowLabel") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_windowlabel_tex.type = type;
            theme->toolbar_windowlabel_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.button") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_button_tex.type = type;
            theme->toolbar_button_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.button.pressed") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_button_pressed_tex.type = type;
            theme->toolbar_button_pressed_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.systray") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_systray_tex.type = type;
            theme->toolbar_systray_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_iconbar_tex.type = type;
            theme->toolbar_iconbar_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.empty") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_iconbar_empty_tex.type = type;
            theme->toolbar_iconbar_empty_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.focused") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_iconbar_focused_tex.type = type;
            theme->toolbar_iconbar_focused_texture_explicit = true;
        }
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.unfocused") == 0) {
        uint32_t type = 0;
        if (fbwl_texture_parse_type(val, &type)) {
            theme->toolbar_iconbar_unfocused_tex.type = type;
            theme->toolbar_iconbar_unfocused_texture_explicit = true;
        }
        return true;
    }

    // Pixmap paths.
    if (strcasecmp(key, "window.title.focus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_title_focus_tex.pixmap, sizeof(theme->window_title_focus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.title.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_title_unfocus_tex.pixmap, sizeof(theme->window_title_unfocus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.label.focus.pixmap") == 0 || strcasecmp(key, "window.label.active.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_label_focus_tex.pixmap, sizeof(theme->window_label_focus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.label.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_label_unfocus_tex.pixmap, sizeof(theme->window_label_unfocus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.button.focus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_button_focus_tex.pixmap, sizeof(theme->window_button_focus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.button.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_button_unfocus_tex.pixmap, sizeof(theme->window_button_unfocus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.button.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_button_pressed_tex.pixmap, sizeof(theme->window_button_pressed_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.handle.focus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_handle_focus_tex.pixmap, sizeof(theme->window_handle_focus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.handle.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_handle_unfocus_tex.pixmap, sizeof(theme->window_handle_unfocus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.grip.focus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_grip_focus_tex.pixmap, sizeof(theme->window_grip_focus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.grip.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_grip_unfocus_tex.pixmap, sizeof(theme->window_grip_unfocus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.tab.label.focus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_tab_label_focus_tex.pixmap, sizeof(theme->window_tab_label_focus_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.tab.label.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_tab_label_unfocus_tex.pixmap, sizeof(theme->window_tab_label_unfocus_tex.pixmap), "%s", resolved);
        return true;
    }

    // Window button pixmaps.
    if (strcasecmp(key, "window.menuicon.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_menuicon_pm.focus, sizeof(theme->window_menuicon_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.menuicon.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_menuicon_pm.unfocus, sizeof(theme->window_menuicon_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.menuicon.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_menuicon_pm.pressed, sizeof(theme->window_menuicon_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.shade.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_shade_pm.focus, sizeof(theme->window_shade_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.shade.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_shade_pm.unfocus, sizeof(theme->window_shade_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.shade.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_shade_pm.pressed, sizeof(theme->window_shade_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.unshade.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_unshade_pm.focus, sizeof(theme->window_unshade_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.unshade.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_unshade_pm.unfocus, sizeof(theme->window_unshade_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.unshade.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_unshade_pm.pressed, sizeof(theme->window_unshade_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.stick.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_stick_pm.focus, sizeof(theme->window_stick_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.stick.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_stick_pm.unfocus, sizeof(theme->window_stick_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.stick.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_stick_pm.pressed, sizeof(theme->window_stick_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.stuck.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_stuck_pm.focus, sizeof(theme->window_stuck_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.stuck.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_stuck_pm.unfocus, sizeof(theme->window_stuck_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.stuck.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_stuck_pm.pressed, sizeof(theme->window_stuck_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.close.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_close_pm.focus, sizeof(theme->window_close_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.close.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_close_pm.unfocus, sizeof(theme->window_close_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.close.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_close_pm.pressed, sizeof(theme->window_close_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.maximize.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_maximize_pm.focus, sizeof(theme->window_maximize_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.maximize.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_maximize_pm.unfocus, sizeof(theme->window_maximize_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.maximize.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_maximize_pm.pressed, sizeof(theme->window_maximize_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.iconify.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_iconify_pm.focus, sizeof(theme->window_iconify_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.iconify.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_iconify_pm.unfocus, sizeof(theme->window_iconify_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.iconify.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_iconify_pm.pressed, sizeof(theme->window_iconify_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.lhalf.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_lhalf_pm.focus, sizeof(theme->window_lhalf_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.lhalf.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_lhalf_pm.unfocus, sizeof(theme->window_lhalf_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.lhalf.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_lhalf_pm.pressed, sizeof(theme->window_lhalf_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.rhalf.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_rhalf_pm.focus, sizeof(theme->window_rhalf_pm.focus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.rhalf.unfocus.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_rhalf_pm.unfocus, sizeof(theme->window_rhalf_pm.unfocus), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "window.rhalf.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->window_rhalf_pm.pressed, sizeof(theme->window_rhalf_pm.pressed), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.frame.pixmap") == 0 || strcasecmp(key, "menu.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_frame_tex.pixmap, sizeof(theme->menu_frame_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.title.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_title_tex.pixmap, sizeof(theme->menu_title_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.hilite.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_hilite_tex.pixmap, sizeof(theme->menu_hilite_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.submenu.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_submenu_pixmap, sizeof(theme->menu_submenu_pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.selected.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_selected_pixmap, sizeof(theme->menu_selected_pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.unselected.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_unselected_pixmap, sizeof(theme->menu_unselected_pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.hilite.submenu.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_hilite_submenu_pixmap, sizeof(theme->menu_hilite_submenu_pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.hilite.selected.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_hilite_selected_pixmap, sizeof(theme->menu_hilite_selected_pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "menu.hilite.unselected.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->menu_hilite_unselected_pixmap, sizeof(theme->menu_hilite_unselected_pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "toolbar.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_tex.pixmap, sizeof(theme->toolbar_tex.pixmap), "%s", resolved);
        return true;
    }
    if (strcasecmp(key, "toolbar.clock.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_clock_tex.pixmap, sizeof(theme->toolbar_clock_tex.pixmap), "%s", resolved);
        theme->toolbar_clock_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.workspace.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_workspace_tex.pixmap, sizeof(theme->toolbar_workspace_tex.pixmap), "%s", resolved);
        theme->toolbar_workspace_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.label.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_label_tex.pixmap, sizeof(theme->toolbar_label_tex.pixmap), "%s", resolved);
        theme->toolbar_label_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.windowLabel.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_windowlabel_tex.pixmap, sizeof(theme->toolbar_windowlabel_tex.pixmap), "%s", resolved);
        theme->toolbar_windowlabel_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.button.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_button_tex.pixmap, sizeof(theme->toolbar_button_tex.pixmap), "%s", resolved);
        theme->toolbar_button_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.button.pressed.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_button_pressed_tex.pixmap, sizeof(theme->toolbar_button_pressed_tex.pixmap), "%s", resolved);
        theme->toolbar_button_pressed_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.systray.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_systray_tex.pixmap, sizeof(theme->toolbar_systray_tex.pixmap), "%s", resolved);
        theme->toolbar_systray_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_iconbar_tex.pixmap, sizeof(theme->toolbar_iconbar_tex.pixmap), "%s", resolved);
        theme->toolbar_iconbar_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.empty.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_iconbar_empty_tex.pixmap, sizeof(theme->toolbar_iconbar_empty_tex.pixmap), "%s", resolved);
        theme->toolbar_iconbar_empty_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.focused.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_iconbar_focused_tex.pixmap, sizeof(theme->toolbar_iconbar_focused_tex.pixmap), "%s", resolved);
        theme->toolbar_iconbar_focused_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "toolbar.iconbar.unfocused.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->toolbar_iconbar_unfocused_tex.pixmap, sizeof(theme->toolbar_iconbar_unfocused_tex.pixmap), "%s", resolved);
        theme->toolbar_iconbar_unfocused_texture_explicit = true;
        return true;
    }
    if (strcasecmp(key, "slit.pixmap") == 0) {
        char *p = unquote_inplace(val);
        char resolved[256] = {0};
        (void)resolve_pixmap_path(resolved, style_dir, p);
        (void)snprintf(theme->slit_tex.pixmap, sizeof(theme->slit_tex.pixmap), "%s", resolved);
        return true;
    }

    return false;
}
