#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"

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

void fbwl_init_settings_free(struct fbwl_init_settings *settings) {
    if (settings == NULL) {
        return;
    }
    free(settings->keys_file);
    settings->keys_file = NULL;
    free(settings->apps_file);
    settings->apps_file = NULL;
    free(settings->style_file);
    settings->style_file = NULL;
    free(settings->menu_file);
    settings->menu_file = NULL;
    settings->set_workspaces = false;
    settings->workspaces = 0;
}

char *fbwl_path_join(const char *dir, const char *rel) {
    if (dir == NULL || *dir == '\0' || rel == NULL || *rel == '\0') {
        return NULL;
    }

    size_t dir_len = strlen(dir);
    size_t rel_len = strlen(rel);
    const bool need_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    size_t needed = dir_len + (need_slash ? 1 : 0) + rel_len + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }

    int n = snprintf(out, needed, need_slash ? "%s/%s" : "%s%s", dir, rel);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

bool fbwl_file_exists(const char *path) {
    return path != NULL && *path != '\0' && access(path, R_OK) == 0;
}

static char *expand_tilde(const char *path) {
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

    size_t home_len = strlen(home);
    size_t tail_len = strlen(tail);
    size_t needed = home_len + tail_len + 1;
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

char *fbwl_resolve_config_path(const char *config_dir, const char *value) {
    if (value == NULL) {
        return NULL;
    }

    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }
    if (*value == '\0') {
        return NULL;
    }

    char *tmp = strdup(value);
    if (tmp == NULL) {
        return NULL;
    }
    char *s = trim_inplace(tmp);
    if (s == NULL || *s == '\0') {
        free(tmp);
        return NULL;
    }

    const size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '\'' && s[len - 1] == '\'') || (s[0] == '"' && s[len - 1] == '"'))) {
        s[len - 1] = '\0';
        s++;
    }

    char *expanded = expand_tilde(s);
    free(tmp);
    if (expanded == NULL) {
        return NULL;
    }

    if (expanded[0] == '/' || config_dir == NULL || *config_dir == '\0') {
        return expanded;
    }

    char *joined = fbwl_path_join(config_dir, expanded);
    free(expanded);
    return joined;
}

bool fbwl_init_load_file(const char *config_dir, struct fbwl_init_settings *settings) {
    if (settings == NULL) {
        return false;
    }

    fbwl_init_settings_free(settings);

    char *path = fbwl_path_join(config_dir, "init");
    if (path == NULL) {
        return false;
    }
    if (!fbwl_file_exists(path)) {
        free(path);
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Init: failed to open %s: %s", path, strerror(errno));
        free(path);
        return false;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
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

        if (strcasecmp(key, "session.screen0.workspaces") == 0) {
            char *end = NULL;
            long ws = strtol(val, &end, 10);
            if (end != val && (end == NULL || *end == '\0') && ws > 0 && ws < 1000) {
                settings->workspaces = (int)ws;
                settings->set_workspaces = true;
            }
            continue;
        }

        if (strcasecmp(key, "session.keyFile") == 0) {
            char *resolved = fbwl_resolve_config_path(config_dir, val);
            if (resolved != NULL) {
                free(settings->keys_file);
                settings->keys_file = resolved;
            }
            continue;
        }

        if (strcasecmp(key, "session.appsFile") == 0) {
            char *resolved = fbwl_resolve_config_path(config_dir, val);
            if (resolved != NULL) {
                free(settings->apps_file);
                settings->apps_file = resolved;
            }
            continue;
        }

        if (strcasecmp(key, "session.styleFile") == 0) {
            char *resolved = fbwl_resolve_config_path(config_dir, val);
            if (resolved != NULL) {
                free(settings->style_file);
                settings->style_file = resolved;
            }
            continue;
        }

        if (strcasecmp(key, "session.menuFile") == 0) {
            char *resolved = fbwl_resolve_config_path(config_dir, val);
            if (resolved != NULL) {
                free(settings->menu_file);
                settings->menu_file = resolved;
            }
            continue;
        }
    }

    free(line);
    fclose(f);

    wlr_log(WLR_INFO, "Init: loaded %s", path);
    free(path);
    return true;
}
