#include "wayland/fbwl_icon_theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static bool path_is_regular_file_limited(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        return false;
    }
    if (st.st_size <= 0 || st.st_size > (off_t)(8 * 1024 * 1024)) {
        return false;
    }
    return true;
}

static bool str_has_suffix(const char *s, const char *suffix) {
    if (s == NULL || suffix == NULL) {
        return false;
    }
    const size_t s_len = strlen(s);
    const size_t suf_len = strlen(suffix);
    if (s_len < suf_len) {
        return false;
    }
    return strcasecmp(s + (s_len - suf_len), suffix) == 0;
}

static char *path_with_suffix_if_exists(const char *base, const char *suffix) {
    if (base == NULL || base[0] == '\0' || suffix == NULL) {
        return NULL;
    }
    int n = snprintf(NULL, 0, "%s%s", base, suffix);
    if (n <= 0) {
        return NULL;
    }
    size_t len = (size_t)n + 1;
    char *path = malloc(len);
    if (path == NULL) {
        return NULL;
    }
    snprintf(path, len, "%s%s", base, suffix);
    if (path_is_regular_file_limited(path)) {
        return path;
    }
    free(path);
    return NULL;
}

static char *icon_find_in_data_dir(const char *data_dir, const char *icon_name, bool has_ext,
        const char *ext, const char *const *themes, size_t theme_count) {
    if (data_dir == NULL || data_dir[0] == '\0' || icon_name == NULL || icon_name[0] == '\0' || ext == NULL) {
        return NULL;
    }

    static const int sizes[] = {16, 22, 24, 32, 48, 64, 128};
    static const char *contexts[] = {"apps", "actions", "panel", "status", "places"};

    for (size_t ti = 0; ti < theme_count; ti++) {
        const char *theme = themes[ti];
        if (theme == NULL || theme[0] == '\0') {
            continue;
        }
        for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
            const int sz = sizes[si];
            for (size_t ci = 0; ci < sizeof(contexts) / sizeof(contexts[0]); ci++) {
                int n = snprintf(NULL, 0, "%s/icons/%s/%dx%d/%s/%s%s", data_dir, theme, sz, sz, contexts[ci],
                    icon_name, has_ext ? "" : ext);
                if (n <= 0) {
                    continue;
                }
                size_t len = (size_t)n + 1;
                char *path = malloc(len);
                if (path == NULL) {
                    continue;
                }
                snprintf(path, len, "%s/icons/%s/%dx%d/%s/%s%s", data_dir, theme, sz, sz, contexts[ci], icon_name,
                    has_ext ? "" : ext);
                if (path_is_regular_file_limited(path)) {
                    return path;
                }
                free(path);
            }
        }
    }

    // Fallback: common pixmaps dir.
    int n = snprintf(NULL, 0, "%s/pixmaps/%s%s", data_dir, icon_name, has_ext ? "" : ext);
    if (n > 0) {
        size_t len = (size_t)n + 1;
        char *path = malloc(len);
        if (path != NULL) {
            snprintf(path, len, "%s/pixmaps/%s%s", data_dir, icon_name, has_ext ? "" : ext);
            if (path_is_regular_file_limited(path)) {
                return path;
            }
            free(path);
        }
    }

    return NULL;
}

char *fbwl_icon_theme_resolve_path(const char *icon_name) {
    if (icon_name == NULL || icon_name[0] == '\0') {
        return NULL;
    }

    const bool has_png_ext = str_has_suffix(icon_name, ".png");
    const bool has_xpm_ext = str_has_suffix(icon_name, ".xpm");
    const bool has_ext = has_png_ext || has_xpm_ext;

    if (strchr(icon_name, '/') != NULL) {
        if (path_is_regular_file_limited(icon_name)) {
            return strdup(icon_name);
        }
        if (!has_ext) {
            char *path = path_with_suffix_if_exists(icon_name, ".png");
            if (path != NULL) {
                return path;
            }
            path = path_with_suffix_if_exists(icon_name, ".xpm");
            if (path != NULL) {
                return path;
            }
        }
        return NULL;
    }

    const char *themes[3] = {0};
    size_t theme_count = 0;
    const char *theme_env = getenv("FBWL_ICON_THEME");
    if (theme_env != NULL && theme_env[0] != '\0') {
        themes[theme_count++] = theme_env;
    }
    themes[theme_count++] = "hicolor";
    themes[theme_count++] = "Adwaita";

    const char *ext = has_xpm_ext ? ".xpm" : ".png";
    const bool want_ext = has_ext;

    char *data_home_tmp = NULL;
    const char *data_home = getenv("XDG_DATA_HOME");
    if (data_home == NULL || data_home[0] == '\0') {
        const char *home = getenv("HOME");
        if (home != NULL && home[0] != '\0') {
            size_t len = strlen(home) + strlen("/.local/share") + 1;
            data_home_tmp = malloc(len);
            if (data_home_tmp != NULL) {
                snprintf(data_home_tmp, len, "%s/.local/share", home);
                data_home = data_home_tmp;
            }
        }
    }

    if (data_home != NULL && data_home[0] != '\0') {
        char *path = icon_find_in_data_dir(data_home, icon_name, want_ext, ext, themes, theme_count);
        if (path != NULL) {
            free(data_home_tmp);
            return path;
        }
    }

    const char *data_dirs = getenv("XDG_DATA_DIRS");
    if (data_dirs == NULL || data_dirs[0] == '\0') {
        data_dirs = "/usr/local/share:/usr/share";
    }

    char *found = NULL;
    const char *p = data_dirs;
    while (p != NULL && *p != '\0' && found == NULL) {
        const char *colon = strchr(p, ':');
        const size_t n = colon != NULL ? (size_t)(colon - p) : strlen(p);
        if (n > 0) {
            char *base = strndup(p, n);
            if (base != NULL) {
                found = icon_find_in_data_dir(base, icon_name, want_ext, ext, themes, theme_count);
                free(base);
            }
        }
        if (colon == NULL) {
            break;
        }
        p = colon + 1;
    }

    free(data_home_tmp);
    return found;
}

