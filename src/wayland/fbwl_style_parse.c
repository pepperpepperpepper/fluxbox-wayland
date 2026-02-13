#include "wayland/fbwl_style_parse.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_texture.h"
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

static void blend_color(float dst[static 4], const float src[static 4]) {
    if (dst == NULL || src == NULL) {
        return;
    }
    dst[0] = (dst[0] + src[0]) * 0.5f;
    dst[1] = (dst[1] + src[1]) * 0.5f;
    dst[2] = (dst[2] + src[2]) * 0.5f;
    dst[3] = (dst[3] + src[3]) * 0.5f;
}

static bool str_list_contains_ci(char *const *items, size_t len, const char *s) {
    if (items == NULL || s == NULL) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (items[i] != NULL && strcasecmp(items[i], s) == 0) {
            return true;
        }
    }
    return false;
}

static bool str_list_push(char ***items, size_t *len, size_t *cap, const char *s) {
    if (items == NULL || len == NULL || cap == NULL || s == NULL || *s == '\0') {
        return false;
    }
    if (*len >= *cap) {
        size_t new_cap = *cap > 0 ? (*cap * 2) : 16;
        char **tmp = realloc(*items, new_cap * sizeof(*tmp));
        if (tmp == NULL) {
            return false;
        }
        *items = tmp;
        *cap = new_cap;
    }
    char *dup = strdup(s);
    if (dup == NULL) {
        return false;
    }
    (*items)[(*len)++] = dup;
    return true;
}

static bool file_readable(const char *path) {
    return path != NULL && *path != '\0' && access(path, R_OK) == 0;
}

static char *path_dirname_owned(const char *path) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }

    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return strdup(".");
    }
    size_t len = (size_t)(slash - path);
    if (len == 0) {
        len = 1; // "/foo" -> "/"
    }

    char *out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
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

bool fbwl_style_load_file(struct fbwl_decor_theme *theme, const char *path) {
    if (theme == NULL || path == NULL || *path == '\0') {
        return false;
    }

    const char *load_path = path;
    char *dir_resolved_owned = NULL;
    struct stat st = {0};
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        const char *candidates[] = {"theme.cfg", "style.cfg"};
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            const char *name = candidates[i];
            const size_t needed = strlen(path) + 1 + strlen(name) + 1;
            char *joined = malloc(needed);
            if (joined == NULL) {
                continue;
            }
            int n = snprintf(joined, needed, "%s/%s", path, name);
            if (n < 0 || (size_t)n >= needed) {
                free(joined);
                continue;
            }
            if (stat(joined, &st) == 0 && S_ISREG(st.st_mode)) {
                dir_resolved_owned = joined;
                load_path = dir_resolved_owned;
                break;
            }
            free(joined);
        }
        if (dir_resolved_owned == NULL) {
            wlr_log(WLR_ERROR, "Style: no theme.cfg/style.cfg in %s", path);
            return false;
        }
    }

    FILE *f = fopen(load_path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Style: failed to open %s: %s", load_path, strerror(errno));
        free(dir_resolved_owned);
        return false;
    }

    char *style_dir = path_dirname_owned(load_path);

    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    bool border_width_explicit = false;
    bool border_color_explicit = false;
    bool titlebar_active_set = false;
    bool titlebar_inactive_set = false;
    bool menu_bg_set = false;
    bool menu_hilite_set = false;
    bool toolbar_bg_set = false;
    bool toolbar_hilite_set = false;
    bool btn_set = false;
    int toolbar_text_prio = 0;
    int window_font_prio = 0;
    int menu_font_prio = 0;
    int toolbar_font_prio = 0;
    char **unknown_keys = NULL;
    size_t unknown_keys_len = 0;
    size_t unknown_keys_cap = 0;
    size_t unknown_logged = 0;
    while ((nread = getline(&line, &cap, f)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        char *s = trim_inplace(line);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (*s == '#' || *s == '!') {
            continue;
        }

        char *sep = strchr(s, ':');
        if (sep == NULL) {
            continue;
        }
        *sep = '\0';
        char *key = trim_inplace(s);
        char *val = trim_inplace(sep + 1);
        if (key == NULL || *key == '\0' || val == NULL || *val == '\0') {
            continue;
        }

        // Texture descriptors.
        if (strcasecmp(key, "window.title.focus") == 0 || strcasecmp(key, "window.label.focus") == 0 ||
                strcasecmp(key, "window.label.active") == 0) {
            uint32_t type = 0;
            if (fbwl_texture_parse_type(val, &type)) {
                theme->window_title_focus_tex.type = type;
            }
            continue;
        }
        if (strcasecmp(key, "window.title.unfocus") == 0 || strcasecmp(key, "window.label.unfocus") == 0) {
            uint32_t type = 0;
            if (fbwl_texture_parse_type(val, &type)) {
                theme->window_title_unfocus_tex.type = type;
            }
            continue;
        }
        if (strcasecmp(key, "menu.frame") == 0 || strcasecmp(key, "menu") == 0) {
            uint32_t type = 0;
            if (fbwl_texture_parse_type(val, &type)) {
                theme->menu_frame_tex.type = type;
            }
            continue;
        }
        if (strcasecmp(key, "menu.hilite") == 0) {
            uint32_t type = 0;
            if (fbwl_texture_parse_type(val, &type)) {
                theme->menu_hilite_tex.type = type;
            }
            continue;
        }
        if (strcasecmp(key, "toolbar") == 0) {
            uint32_t type = 0;
            if (fbwl_texture_parse_type(val, &type)) {
                theme->toolbar_tex.type = type;
            }
            continue;
        }
        if (strcasecmp(key, "slit") == 0) {
            uint32_t type = 0;
            if (fbwl_texture_parse_type(val, &type)) {
                theme->slit_tex.type = type;
            }
            continue;
        }

        // Pixmap paths.
        if (strcasecmp(key, "window.title.focus.pixmap") == 0 || strcasecmp(key, "window.label.focus.pixmap") == 0 ||
                strcasecmp(key, "window.label.active.pixmap") == 0) {
            char *p = unquote_inplace(val);
            char resolved[256] = {0};
            (void)resolve_pixmap_path(resolved, style_dir, p);
            (void)snprintf(theme->window_title_focus_tex.pixmap, sizeof(theme->window_title_focus_tex.pixmap), "%s", resolved);
            continue;
        }
        if (strcasecmp(key, "window.title.unfocus.pixmap") == 0 || strcasecmp(key, "window.label.unfocus.pixmap") == 0) {
            char *p = unquote_inplace(val);
            char resolved[256] = {0};
            (void)resolve_pixmap_path(resolved, style_dir, p);
            (void)snprintf(theme->window_title_unfocus_tex.pixmap, sizeof(theme->window_title_unfocus_tex.pixmap), "%s", resolved);
            continue;
        }
        if (strcasecmp(key, "menu.frame.pixmap") == 0 || strcasecmp(key, "menu.pixmap") == 0) {
            char *p = unquote_inplace(val);
            char resolved[256] = {0};
            (void)resolve_pixmap_path(resolved, style_dir, p);
            (void)snprintf(theme->menu_frame_tex.pixmap, sizeof(theme->menu_frame_tex.pixmap), "%s", resolved);
            continue;
        }
        if (strcasecmp(key, "menu.hilite.pixmap") == 0) {
            char *p = unquote_inplace(val);
            char resolved[256] = {0};
            (void)resolve_pixmap_path(resolved, style_dir, p);
            (void)snprintf(theme->menu_hilite_tex.pixmap, sizeof(theme->menu_hilite_tex.pixmap), "%s", resolved);
            continue;
        }
        if (strcasecmp(key, "toolbar.pixmap") == 0) {
            char *p = unquote_inplace(val);
            char resolved[256] = {0};
            (void)resolve_pixmap_path(resolved, style_dir, p);
            (void)snprintf(theme->toolbar_tex.pixmap, sizeof(theme->toolbar_tex.pixmap), "%s", resolved);
            continue;
        }
        if (strcasecmp(key, "slit.pixmap") == 0) {
            char *p = unquote_inplace(val);
            char resolved[256] = {0};
            (void)resolve_pixmap_path(resolved, style_dir, p);
            (void)snprintf(theme->slit_tex.pixmap, sizeof(theme->slit_tex.pixmap), "%s", resolved);
            continue;
        }

        // Basic Xrm-style wildcards used in many themes.
        if (strcasecmp(key, "*color") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->titlebar_active, c);
                titlebar_active_set = true;
                copy_color(theme->window_title_focus_tex.color, c);
                copy_color(theme->window_title_focus_tex.color_to, c);
                copy_color(theme->menu_bg, c);
                menu_bg_set = true;
                copy_color(theme->menu_frame_tex.color, c);
                copy_color(theme->menu_frame_tex.color_to, c);
                copy_color(theme->menu_hilite, c);
                menu_hilite_set = true;
                copy_color(theme->menu_hilite_tex.color, c);
                copy_color(theme->menu_hilite_tex.color_to, c);
                copy_color(theme->toolbar_bg, c);
                toolbar_bg_set = true;
                copy_color(theme->toolbar_tex.color, c);
                copy_color(theme->toolbar_tex.color_to, c);
                copy_color(theme->toolbar_hilite, c);
                toolbar_hilite_set = true;
                copy_color(theme->toolbar_iconbar_focused, c);
            }
            continue;
        }

        if (strcasecmp(key, "*colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->window_title_focus_tex.color_to, c);
                copy_color(theme->menu_frame_tex.color_to, c);
                copy_color(theme->menu_hilite_tex.color_to, c);
                copy_color(theme->toolbar_tex.color_to, c);
            }
            continue;
        }

        if (strcasecmp(key, "*unfocus.color") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->titlebar_inactive, c);
                titlebar_inactive_set = true;
                copy_color(theme->window_title_unfocus_tex.color, c);
                copy_color(theme->window_title_unfocus_tex.color_to, c);
            }
            continue;
        }

        if (strcasecmp(key, "*unfocus.colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->window_title_unfocus_tex.color_to, c);
            }
            continue;
        }

        if (strcasecmp(key, "*textColor") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->title_text_active, c);
                copy_color(theme->title_text_inactive, c);
                copy_color(theme->menu_text, c);
                copy_color(theme->toolbar_text, c);
            }
            continue;
        }

        if (strcasecmp(key, "*unfocus.textColor") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->title_text_inactive, c);
            }
            continue;
        }

        if (strcasecmp(key, "*.font") == 0 || strcasecmp(key, "*font") == 0) {
            char *font = unquote_inplace(val);
            if (font != NULL && *font != '\0') {
                if (window_font_prio <= 0) {
                    (void)snprintf(theme->window_font, sizeof(theme->window_font), "%s", font);
                    window_font_prio = 0;
                }
                if (menu_font_prio <= 0) {
                    (void)snprintf(theme->menu_font, sizeof(theme->menu_font), "%s", font);
                    menu_font_prio = 0;
                }
                if (toolbar_font_prio <= 0) {
                    (void)snprintf(theme->toolbar_font, sizeof(theme->toolbar_font), "%s", font);
                    toolbar_font_prio = 0;
                }
            }
            continue;
        }

        if (strcasecmp(key, "window.borderWidth") == 0) {
            int v = 0;
            if (parse_int_range(val, 1, 999, &v)) {
                theme->border_width = v;
                border_width_explicit = true;
            }
            continue;
        }

        if (strcasecmp(key, "borderWidth") == 0) {
            int v = 0;
            if (parse_int_range(val, 1, 999, &v)) {
                theme->border_width = v;
                border_width_explicit = true;
            }
            continue;
        }

        if (strcasecmp(key, "handleWidth") == 0) {
            int v = 0;
            if (!border_width_explicit && parse_int_range(val, 1, 999, &v)) {
                theme->border_width = v;
                border_width_explicit = true;
            }
            continue;
        }

        if (strcasecmp(key, "window.font") == 0) {
            char *font = unquote_inplace(val);
            if (font != NULL && *font != '\0' && window_font_prio <= 1) {
                (void)snprintf(theme->window_font, sizeof(theme->window_font), "%s", font);
                window_font_prio = 1;
            }
            continue;
        }

        if (strcasecmp(key, "window.label.focus.font") == 0 ||
                strcasecmp(key, "window.label.unfocus.font") == 0 ||
                strcasecmp(key, "window.label.active.font") == 0) {
            char *font = unquote_inplace(val);
            if (font != NULL && *font != '\0' && window_font_prio <= 2) {
                (void)snprintf(theme->window_font, sizeof(theme->window_font), "%s", font);
                window_font_prio = 2;
            }
            continue;
        }

        if (strcasecmp(key, "window.title.height") == 0) {
            int v = 0;
            if (parse_int_range(val, 1, 999, &v)) {
                theme->title_height = v;
            }
            continue;
        }

        if (strcasecmp(key, "window.borderColor") == 0) {
            if (fbwl_parse_color(val, theme->border_color)) {
                border_color_explicit = true;
            }
            continue;
        }

        if (strcasecmp(key, "borderColor") == 0) {
            if (fbwl_parse_color(val, theme->border_color)) {
                border_color_explicit = true;
            }
            continue;
        }

        if (!border_color_explicit &&
                (strcasecmp(key, "window.frame.focusColor") == 0 ||
                    strcasecmp(key, "window.frame.unfocusColor") == 0)) {
            (void)fbwl_parse_color(val, theme->border_color);
            continue;
        }

        if (!border_color_explicit &&
                (strcasecmp(key, "window.handle.focus.color") == 0 ||
                    strcasecmp(key, "window.handle.unfocus.color") == 0 ||
                    strcasecmp(key, "window.grip.focus.color") == 0 ||
                    strcasecmp(key, "window.grip.unfocus.color") == 0)) {
            (void)fbwl_parse_color(val, theme->border_color);
            continue;
        }

        if (strcasecmp(key, "window.title.focus.color") == 0) {
            (void)fbwl_parse_color(val, theme->titlebar_active);
            titlebar_active_set = true;
            copy_color(theme->window_title_focus_tex.color, theme->titlebar_active);
            continue;
        }

        if (strcasecmp(key, "window.title.unfocus.color") == 0) {
            (void)fbwl_parse_color(val, theme->titlebar_inactive);
            titlebar_inactive_set = true;
            copy_color(theme->window_title_unfocus_tex.color, theme->titlebar_inactive);
            continue;
        }

        if (strcasecmp(key, "window.label.focus.color") == 0 ||
                strcasecmp(key, "window.label.active.color") == 0) {
            (void)fbwl_parse_color(val, theme->titlebar_active);
            titlebar_active_set = true;
            copy_color(theme->window_title_focus_tex.color, theme->titlebar_active);
            continue;
        }

        if (strcasecmp(key, "window.label.unfocus.color") == 0) {
            (void)fbwl_parse_color(val, theme->titlebar_inactive);
            titlebar_inactive_set = true;
            copy_color(theme->window_title_unfocus_tex.color, theme->titlebar_inactive);
            continue;
        }

        if (strcasecmp(key, "window.title.focus.colorTo") == 0 ||
                strcasecmp(key, "window.label.focus.colorTo") == 0 ||
                strcasecmp(key, "window.label.active.colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->window_title_focus_tex.color_to, c);
            }
            continue;
        }

        if (strcasecmp(key, "window.title.unfocus.colorTo") == 0 ||
                strcasecmp(key, "window.label.unfocus.colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->window_title_unfocus_tex.color_to, c);
            }
            continue;
        }

        if (strcasecmp(key, "window.label.focus.textColor") == 0 ||
                strcasecmp(key, "window.label.active.textColor") == 0) {
            (void)fbwl_parse_color(val, theme->title_text_active);
            continue;
        }

        if (strcasecmp(key, "window.label.unfocus.textColor") == 0) {
            (void)fbwl_parse_color(val, theme->title_text_inactive);
            continue;
        }

        if (strcasecmp(key, "window.button.focus.color") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->btn_menu_color, c);
                copy_color(theme->btn_shade_color, c);
                copy_color(theme->btn_stick_color, c);
                copy_color(theme->btn_close_color, c);
                copy_color(theme->btn_max_color, c);
                copy_color(theme->btn_min_color, c);
                copy_color(theme->btn_lhalf_color, c);
                copy_color(theme->btn_rhalf_color, c);
                btn_set = true;
            }
            continue;
        }

        if (strcasecmp(key, "window.button.focus.colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                if (btn_set) {
                    blend_color(theme->btn_menu_color, c);
                    blend_color(theme->btn_shade_color, c);
                    blend_color(theme->btn_stick_color, c);
                    blend_color(theme->btn_close_color, c);
                    blend_color(theme->btn_max_color, c);
                    blend_color(theme->btn_min_color, c);
                    blend_color(theme->btn_lhalf_color, c);
                    blend_color(theme->btn_rhalf_color, c);
                } else {
                    copy_color(theme->btn_menu_color, c);
                    copy_color(theme->btn_shade_color, c);
                    copy_color(theme->btn_stick_color, c);
                    copy_color(theme->btn_close_color, c);
                    copy_color(theme->btn_max_color, c);
                    copy_color(theme->btn_min_color, c);
                    copy_color(theme->btn_lhalf_color, c);
                    copy_color(theme->btn_rhalf_color, c);
                }
                btn_set = true;
            }
            continue;
        }

        if (strcasecmp(key, "menu.itemHeight") == 0) {
            int v = 0;
            if (parse_int_range(val, 1, 999, &v)) {
                theme->menu_item_height = v;
            }
            continue;
        }

        if (strcasecmp(key, "menu.frame.font") == 0) {
            char *font = unquote_inplace(val);
            if (font != NULL && *font != '\0' && menu_font_prio <= 2) {
                (void)snprintf(theme->menu_font, sizeof(theme->menu_font), "%s", font);
                menu_font_prio = 2;
            }
            continue;
        }

        if (strcasecmp(key, "menu.title.font") == 0) {
            char *font = unquote_inplace(val);
            if (font != NULL && *font != '\0' && menu_font_prio <= 1) {
                (void)snprintf(theme->menu_font, sizeof(theme->menu_font), "%s", font);
                menu_font_prio = 1;
            }
            continue;
        }

        if (strcasecmp(key, "menu.frame.color") == 0 || strcasecmp(key, "menu.color") == 0) {
            if (fbwl_parse_color(val, theme->menu_bg)) {
                menu_bg_set = true;
                copy_color(theme->menu_frame_tex.color, theme->menu_bg);
            }
            continue;
        }

        if (strcasecmp(key, "menu.hilite.color") == 0) {
            if (fbwl_parse_color(val, theme->menu_hilite)) {
                menu_hilite_set = true;
                copy_color(theme->menu_hilite_tex.color, theme->menu_hilite);
            }
            continue;
        }

        if (strcasecmp(key, "menu.frame.colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->menu_frame_tex.color_to, c);
            }
            continue;
        }

        if (strcasecmp(key, "menu.hilite.colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->menu_hilite_tex.color_to, c);
            }
            continue;
        }

        if (strcasecmp(key, "menu.hilite.textColor") == 0) {
            (void)fbwl_parse_color(val, theme->menu_hilite_text);
            continue;
        }

        if (strcasecmp(key, "menu.frame.textColor") == 0 ||
                strcasecmp(key, "menu.title.textColor") == 0) {
            (void)fbwl_parse_color(val, theme->menu_text);
            continue;
        }

        if (strcasecmp(key, "menu.frame.disableColor") == 0) {
            (void)fbwl_parse_color(val, theme->menu_disable_text);
            continue;
        }

        if (strcasecmp(key, "toolbar.height") == 0) {
            int v = 0;
            if (parse_int_range(val, 1, 999, &v)) {
                theme->toolbar_height = v;
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.font") == 0) {
            char *font = unquote_inplace(val);
            if (font != NULL && *font != '\0' && toolbar_font_prio <= 1) {
                (void)snprintf(theme->toolbar_font, sizeof(theme->toolbar_font), "%s", font);
                toolbar_font_prio = 1;
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.workspace.font") == 0 ||
                strcasecmp(key, "toolbar.iconbar.focused.font") == 0 ||
                strcasecmp(key, "toolbar.iconbar.unfocused.font") == 0 ||
                strcasecmp(key, "toolbar.clock.font") == 0 ||
                strcasecmp(key, "toolbar.label.font") == 0 ||
                strcasecmp(key, "toolbar.windowLabel.font") == 0) {
            char *font = unquote_inplace(val);
            if (font != NULL && *font != '\0' && toolbar_font_prio <= 2) {
                (void)snprintf(theme->toolbar_font, sizeof(theme->toolbar_font), "%s", font);
                toolbar_font_prio = 2;
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.color") == 0) {
            if (fbwl_parse_color(val, theme->toolbar_bg)) {
                toolbar_bg_set = true;
                copy_color(theme->toolbar_tex.color, theme->toolbar_bg);
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                copy_color(theme->toolbar_tex.color_to, c);
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.workspace.color") == 0) {
            if (fbwl_parse_color(val, theme->toolbar_hilite)) {
                toolbar_hilite_set = true;
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.workspace.colorTo") == 0) {
            float c[4] = {0};
            if (fbwl_parse_color(val, c)) {
                if (toolbar_hilite_set) {
                    blend_color(theme->toolbar_hilite, c);
                } else {
                    copy_color(theme->toolbar_hilite, c);
                }
                toolbar_hilite_set = true;
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.iconbar.focused.color") == 0) {
            (void)fbwl_parse_color(val, theme->toolbar_iconbar_focused);
            continue;
        }

        if (strcasecmp(key, "toolbar.workspace.textColor") == 0 ||
                strcasecmp(key, "toolbar.iconbar.focused.textColor") == 0 ||
                strcasecmp(key, "toolbar.iconbar.unfocused.textColor") == 0 ||
                strcasecmp(key, "toolbar.clock.textColor") == 0) {
            if (toolbar_text_prio <= 2) {
                (void)fbwl_parse_color(val, theme->toolbar_text);
                toolbar_text_prio = 2;
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.label.textColor") == 0 ||
                strcasecmp(key, "toolbar.windowLabel.textColor") == 0) {
            if (toolbar_text_prio <= 2) {
                (void)fbwl_parse_color(val, theme->toolbar_text);
                toolbar_text_prio = 2;
            }
            continue;
        }

        if (strcasecmp(key, "toolbar.textColor") == 0) {
            if (toolbar_text_prio <= 1) {
                (void)fbwl_parse_color(val, theme->toolbar_text);
                toolbar_text_prio = 1;
            }
            continue;
        }

        if (!str_list_contains_ci(unknown_keys, unknown_keys_len, key)) {
            if (str_list_push(&unknown_keys, &unknown_keys_len, &unknown_keys_cap, key)) {
                if (unknown_logged < 20) {
                    wlr_log(WLR_INFO, "Style: ignored key=%s", key);
                    unknown_logged++;
                }
            }
        }
    }

    free(line);
    fclose(f);
    free(style_dir);

    if (unknown_keys_len > unknown_logged) {
        wlr_log(WLR_INFO, "Style: ignored %zu additional unknown keys", unknown_keys_len - unknown_logged);
    }
    for (size_t i = 0; i < unknown_keys_len; i++) {
        free(unknown_keys[i]);
    }
    free(unknown_keys);

    wlr_log(WLR_INFO, "Style: loaded %s (border=%d title_h=%d)",
        load_path, theme->border_width, theme->title_height);
    free(dir_resolved_owned);
    return true;
}
