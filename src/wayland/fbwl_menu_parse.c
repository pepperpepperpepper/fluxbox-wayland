#include "wayland/fbwl_menu_parse.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_menu.h"

enum {
    MENU_MAX_INCLUDE_DEPTH = 8,
};

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

static char *dup_trim_range(const char *start, const char *end) {
    if (start == NULL || end == NULL || end < start) {
        return NULL;
    }
    size_t len = (size_t)(end - start);
    char *tmp = malloc(len + 1);
    if (tmp == NULL) {
        return NULL;
    }
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    char *t = trim_inplace(tmp);
    if (t == NULL || *t == '\0') {
        free(tmp);
        return NULL;
    }
    if (t == tmp) {
        return tmp;
    }
    char *out = strdup(t);
    free(tmp);
    return out;
}

static char *menu_parse_paren_value(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    const char *open = strchr(s, '(');
    if (open == NULL) {
        return NULL;
    }
    const char *close = strchr(open + 1, ')');
    if (close == NULL) {
        return NULL;
    }
    return dup_trim_range(open + 1, close);
}

static char *menu_parse_brace_value(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    const char *open = strchr(s, '{');
    if (open == NULL) {
        return NULL;
    }
    const char *close = strchr(open + 1, '}');
    if (close == NULL) {
        return NULL;
    }
    return dup_trim_range(open + 1, close);
}

static char *menu_strdup_range(const char *start, const char *end) {
    if (start == NULL || end == NULL || end < start) {
        return NULL;
    }
    size_t len = (size_t)(end - start);
    char *out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *menu_dirname_owned(const char *path) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }

    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return strdup(".");
    }
    if (slash == path) {
        return strdup("/");
    }
    return menu_strdup_range(path, slash);
}

static char *menu_path_join(const char *dir, const char *rel) {
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

static char *menu_expand_tilde(const char *path) {
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

static char *menu_resolve_path(const char *base_dir, const char *path) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }

    char *expanded = menu_expand_tilde(path);
    if (expanded == NULL) {
        return NULL;
    }

    if (expanded[0] == '/' || base_dir == NULL || *base_dir == '\0') {
        return expanded;
    }

    char *joined = menu_path_join(base_dir, expanded);
    free(expanded);
    return joined;
}

static bool menu_stat_is_dir(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

static bool menu_stat_is_regular_file(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

static bool menu_parse_path(struct fbwl_menu *stack[16], size_t *depth, size_t min_depth,
        const char *path, int include_depth, bool required);

static bool menu_parse_dir(struct fbwl_menu *stack[16], size_t *depth, size_t min_depth,
        const char *dir_path, int include_depth) {
    if (stack == NULL || depth == NULL || dir_path == NULL || *dir_path == '\0') {
        return true;
    }
    if (include_depth >= MENU_MAX_INCLUDE_DEPTH) {
        wlr_log(WLR_ERROR, "Menu: include depth exceeded while including dir %s", dir_path);
        return true;
    }

    struct dirent **namelist = NULL;
    int n = scandir(dir_path, &namelist, NULL, alphasort);
    if (n < 0) {
        wlr_log(WLR_ERROR, "Menu: failed to scan dir %s: %s", dir_path, strerror(errno));
        return true;
    }

    for (int i = 0; i < n; i++) {
        struct dirent *ent = namelist[i];
        if (ent != NULL && ent->d_name != NULL) {
            const char *name = ent->d_name;
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                char *child = menu_path_join(dir_path, name);
                if (child != NULL) {
                    if (menu_stat_is_regular_file(child)) {
                        (void)menu_parse_path(stack, depth, min_depth, child, include_depth + 1, false);
                    }
                    free(child);
                }
            }
        }
        free(namelist[i]);
    }
    free(namelist);
    return true;
}

static bool menu_parse_file_impl(struct fbwl_menu *stack[16], size_t *depth, size_t min_depth,
        const char *path, int include_depth, bool required) {
    if (stack == NULL || depth == NULL || path == NULL || *path == '\0') {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Menu: failed to open %s: %s", path, strerror(errno));
        return !required;
    }

    char *base_dir_owned = menu_dirname_owned(path);
    const char *base_dir = base_dir_owned != NULL ? base_dir_owned : ".";

    char *line = NULL;
    size_t cap = 0;
    ssize_t n = 0;
    while ((n = getline(&line, &cap, f)) != -1) {
        if (n <= 0) {
            continue;
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[n - 1] = '\0';
            n--;
        }

        char *s = trim_inplace(line);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (*s == '#' || *s == '!') {
            continue;
        }

        char *open = strchr(s, '[');
        char *close = open != NULL ? strchr(open + 1, ']') : NULL;
        if (open == NULL || close == NULL || close <= open + 1) {
            continue;
        }

        char *key = dup_trim_range(open + 1, close);
        if (key == NULL) {
            continue;
        }

        struct fbwl_menu *cur = stack[*depth];

        if (strcasecmp(key, "begin") == 0) {
            free(key);
            continue;
        }

        if (strcasecmp(key, "end") == 0) {
            if (*depth > min_depth) {
                (*depth)--;
            }
            free(key);
            continue;
        }

        if (strcasecmp(key, "submenu") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            struct fbwl_menu *submenu = fbwl_menu_create(label);
            if (submenu != NULL) {
                if (fbwl_menu_add_submenu(cur, label, submenu)) {
                    if (*depth + 1 < 16) {
                        (*depth)++;
                        stack[*depth] = submenu;
                    }
                } else {
                    fbwl_menu_free(submenu);
                }
            }
            free(label);
            free(key);
            continue;
        }

        if (strcasecmp(key, "exec") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            char *cmd = menu_parse_brace_value(close + 1);
            if (cmd != NULL) {
                (void)fbwl_menu_add_exec(cur, label, cmd);
            }
            free(label);
            free(cmd);
            free(key);
            continue;
        }

        if (strcasecmp(key, "exit") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            (void)fbwl_menu_add_exit(cur, label);
            free(label);
            free(key);
            continue;
        }

        if (strcasecmp(key, "separator") == 0) {
            (void)fbwl_menu_add_separator(cur);
            free(key);
            continue;
        }

        if (strcasecmp(key, "nop") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            (void)fbwl_menu_add_nop(cur, label);
            free(label);
            free(key);
            continue;
        }

        if (strcasecmp(key, "include") == 0) {
            char *raw = menu_parse_paren_value(close + 1);
            if (raw != NULL && *raw != '\0') {
                char *resolved = menu_resolve_path(base_dir, raw);
                if (resolved != NULL) {
                    if (include_depth >= MENU_MAX_INCLUDE_DEPTH) {
                        wlr_log(WLR_ERROR, "Menu: include depth exceeded while including %s", resolved);
                    } else if (menu_stat_is_dir(resolved)) {
                        (void)menu_parse_dir(stack, depth, *depth, resolved, include_depth + 1);
                    } else {
                        (void)menu_parse_path(stack, depth, *depth, resolved, include_depth + 1, false);
                    }
                    free(resolved);
                }
            }
            free(raw);
            free(key);
            continue;
        }

        free(key);
    }

    free(line);
    free(base_dir_owned);
    fclose(f);
    return true;
}

static bool menu_parse_path(struct fbwl_menu *stack[16], size_t *depth, size_t min_depth,
        const char *path, int include_depth, bool required) {
    if (stack == NULL || depth == NULL || path == NULL || *path == '\0') {
        return false;
    }

    if (menu_stat_is_dir(path)) {
        return menu_parse_dir(stack, depth, min_depth, path, include_depth);
    }

    if (!menu_stat_is_regular_file(path)) {
        if (required) {
            wlr_log(WLR_ERROR, "Menu: not a regular file: %s", path);
        }
        return !required;
    }

    return menu_parse_file_impl(stack, depth, min_depth, path, include_depth, required);
}

bool fbwl_menu_parse_file(struct fbwl_menu *root, const char *path) {
    if (root == NULL || path == NULL || *path == '\0') {
        return false;
    }

    struct fbwl_menu *stack[16] = {0};
    size_t depth = 0;
    stack[0] = root;
    return menu_parse_path(stack, &depth, 0, path, 0, true);
}
