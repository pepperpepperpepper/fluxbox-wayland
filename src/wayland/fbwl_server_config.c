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

static bool strip_outer_quotes_inplace(char *s) {
    if (s == NULL) {
        return false;
    }
    const size_t len = strlen(s);
    if (len < 2) {
        return false;
    }
    if (!((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\''))) {
        return false;
    }
    memmove(s, s + 1, len - 1);
    s[len - 2] = '\0';
    return true;
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

static bool resource_db_reserve(struct fbwl_resource_db *db, size_t n) {
    if (db == NULL) {
        return false;
    }
    if (n <= db->items_cap) {
        return true;
    }
    size_t new_cap = db->items_cap > 0 ? db->items_cap : 16;
    while (new_cap < n) {
        new_cap *= 2;
    }

    void *p = realloc(db->items, new_cap * sizeof(db->items[0]));
    if (p == NULL) {
        return false;
    }
    db->items = p;
    db->items_cap = new_cap;
    return true;
}

static bool resource_db_set_owned(struct fbwl_resource_db *db, char *key, char *value) {
    if (db == NULL || key == NULL || *key == '\0' || value == NULL) {
        free(key);
        free(value);
        return false;
    }

    for (size_t i = 0; i < db->items_len; i++) {
        if (strcasecmp(db->items[i].key, key) == 0) {
            free(db->items[i].value);
            db->items[i].value = value;
            free(key);
            return true;
        }
    }

    if (!resource_db_reserve(db, db->items_len + 1)) {
        free(key);
        free(value);
        return false;
    }

    db->items[db->items_len].key = key;
    db->items[db->items_len].value = value;
    db->items_len++;
    return true;
}

void fbwl_resource_db_free(struct fbwl_resource_db *db) {
    if (db == NULL) {
        return;
    }
    for (size_t i = 0; i < db->items_len; i++) {
        free(db->items[i].key);
        free(db->items[i].value);
    }
    free(db->items);
    db->items = NULL;
    db->items_len = 0;
    db->items_cap = 0;
}

static bool fbwl_resource_db_load_file(struct fbwl_resource_db *db, const char *path) {
    if (db == NULL || path == NULL || *path == '\0') {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Init: failed to open %s: %s", path, strerror(errno));
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

        char *key_dup = strdup(key);
        char *val_dup = strdup(val);
        if (key_dup == NULL || val_dup == NULL) {
            free(key_dup);
            free(val_dup);
            continue;
        }
        strip_outer_quotes_inplace(val_dup);
        (void)resource_db_set_owned(db, key_dup, val_dup);
    }

    free(line);
    fclose(f);
    return true;
}

bool fbwl_resource_db_load_init(struct fbwl_resource_db *db, const char *config_dir) {
    if (db == NULL) {
        return false;
    }
    fbwl_resource_db_free(db);

    char *path = fbwl_path_join(config_dir, "init");
    if (path == NULL) {
        return false;
    }
    if (!fbwl_file_exists(path)) {
        free(path);
        return false;
    }

    const bool ok = fbwl_resource_db_load_file(db, path);
    if (ok) {
        wlr_log(WLR_INFO, "Init: loaded %s", path);
    }
    free(path);
    return ok;
}

const char *fbwl_resource_db_get(const struct fbwl_resource_db *db, const char *key) {
    if (db == NULL || key == NULL || *key == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < db->items_len; i++) {
        if (strcasecmp(db->items[i].key, key) == 0) {
            return db->items[i].value;
        }
    }
    return NULL;
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

    if (strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 || strcasecmp(s, "on") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(s, "false") == 0 || strcasecmp(s, "no") == 0 || strcasecmp(s, "off") == 0) {
        *out = false;
        return true;
    }

    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end != s && end != NULL) {
        while (*end != '\0' && isspace((unsigned char)*end)) {
            end++;
        }
        if (*end == '\0') {
            *out = v != 0;
            return true;
        }
    }

    return false;
}

bool fbwl_resource_db_get_bool(const struct fbwl_resource_db *db, const char *key, bool *out) {
    return parse_bool(fbwl_resource_db_get(db, key), out);
}

bool fbwl_resource_db_get_int(const struct fbwl_resource_db *db, const char *key, int *out) {
    if (out == NULL) {
        return false;
    }
    const char *s = fbwl_resource_db_get(db, key);
    if (s == NULL) {
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
    *out = (int)v;
    return true;
}

bool fbwl_resource_db_get_color(const struct fbwl_resource_db *db, const char *key, float rgba[static 4]) {
    return fbwl_parse_hex_color(fbwl_resource_db_get(db, key), rgba);
}

char *fbwl_resource_db_resolve_path(const struct fbwl_resource_db *db, const char *config_dir, const char *key) {
    const char *val = fbwl_resource_db_get(db, key);
    if (val == NULL || *val == '\0') {
        return NULL;
    }
    return fbwl_resolve_config_path(config_dir, val);
}

char *fbwl_resource_db_discover_path(const struct fbwl_resource_db *db, const char *config_dir, const char *key,
        const char *fallback_rel) {
    char *resolved = fbwl_resource_db_resolve_path(db, config_dir, key);
    if (resolved != NULL) {
        return resolved;
    }
    if (fallback_rel == NULL || config_dir == NULL || *config_dir == '\0') {
        return NULL;
    }
    char *joined = fbwl_path_join(config_dir, fallback_rel);
    if (joined != NULL && !fbwl_file_exists(joined)) {
        free(joined);
        joined = NULL;
    }
    return joined;
}
