#include "wayland/fbwl_style_parse_background.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wayland/fbwl_texture.h"
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

static bool file_readable(const char *path) {
    return path != NULL && *path != '\0' && access(path, R_OK) == 0;
}

static bool path_is_dir(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
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

static bool resolve_pixmap_or_dir_path(char out[static 256], const char *style_dir, const char *value) {
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
        if (file_readable(p) || path_is_dir(p)) {
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
        if (file_readable(joined) || path_is_dir(joined)) {
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
            if (joined != NULL && (file_readable(joined) || path_is_dir(joined))) {
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
        if (joined != NULL && (file_readable(joined) || path_is_dir(joined))) {
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

bool fbwl_style_parse_background(struct fbwl_decor_theme *theme, const char *key, char *val, const char *style_dir) {
    if (theme == NULL || key == NULL || val == NULL) {
        return false;
    }

    if (strcasecmp(key, "background") == 0) {
        char *opts = unquote_inplace(val);
        if (opts == NULL || *opts == '\0') {
            return true;
        }
        theme->background_loaded = true;
        (void)snprintf(theme->background_options, sizeof(theme->background_options), "%s", opts);

        uint32_t type = 0;
        if (fbwl_texture_parse_type(opts, &type)) {
            theme->background_tex.type = type;
        }
        return true;
    }

    if (strcasecmp(key, "background.pixmap") == 0) {
        char *p = unquote_inplace(val);
        if (p == NULL || *p == '\0') {
            theme->background_pixmap[0] = '\0';
            return true;
        }
        char resolved[256];
        (void)resolve_pixmap_or_dir_path(resolved, style_dir, p);
        (void)snprintf(theme->background_pixmap, sizeof(theme->background_pixmap), "%s", resolved);
        return true;
    }

    if (strcasecmp(key, "background.color") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            memcpy(theme->background_tex.color, c, sizeof(theme->background_tex.color));
        }
        return true;
    }

    if (strcasecmp(key, "background.colorTo") == 0) {
        float c[4] = {0};
        if (fbwl_parse_color(val, c)) {
            memcpy(theme->background_tex.color_to, c, sizeof(theme->background_tex.color_to));
        }
        return true;
    }

    if (strcasecmp(key, "background.modX") == 0) {
        int v = 1;
        if (!parse_int_range(val, 1, 99, &v)) {
            v = 1;
        }
        theme->background_mod_x = v;
        return true;
    }

    if (strcasecmp(key, "background.modY") == 0) {
        int v = 1;
        if (!parse_int_range(val, 1, 99, &v)) {
            v = 1;
        }
        theme->background_mod_y = v;
        return true;
    }

    return false;
}
