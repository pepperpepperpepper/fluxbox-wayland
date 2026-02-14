#include "wayland/fbwl_menu_parse_util.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool fbwl_menu_parse_skip_name(const char *name) {
    if (name == NULL || *name == '\0') {
        return true;
    }
    if (name[0] == '.') {
        return true;
    }
    const size_t len = strlen(name);
    if (len > 0 && name[len - 1] == '~') {
        return true;
    }
    return false;
}

char *fbwl_menu_parse_trim_inplace(char *s) {
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

char *fbwl_menu_parse_dup_trim_range(const char *start, const char *end) {
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
    char *t = fbwl_menu_parse_trim_inplace(tmp);
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

char *fbwl_menu_parse_paren_value(const char *s) {
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
    return fbwl_menu_parse_dup_trim_range(open + 1, close);
}

static const char *menu_find_matching_brace(const char *open) {
    if (open == NULL || *open != '{') {
        return NULL;
    }
    int nesting = 0;
    for (const char *q = open + 1; *q != '\0'; q++) {
        if (*q == '{' && q[-1] != '\\') {
            nesting++;
            continue;
        }
        if (*q == '}' && q[-1] != '\\') {
            if (nesting > 0) {
                nesting--;
                continue;
            }
            return q;
        }
    }
    return NULL;
}

char *fbwl_menu_parse_brace_value(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    const char *open = strchr(s, '{');
    if (open == NULL) {
        return NULL;
    }
    const char *close = menu_find_matching_brace(open);
    if (close == NULL) {
        return NULL;
    }
    return fbwl_menu_parse_dup_trim_range(open + 1, close);
}

char *fbwl_menu_parse_angle_value(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    const char *open = strchr(s, '<');
    if (open == NULL) {
        return NULL;
    }
    const char *close = strchr(open + 1, '>');
    if (close == NULL) {
        return NULL;
    }
    return fbwl_menu_parse_dup_trim_range(open + 1, close);
}

const char *fbwl_menu_parse_after_delim(const char *s, char open_ch, char close_ch) {
    if (s == NULL) {
        return NULL;
    }
    const char *open = strchr(s, open_ch);
    if (open == NULL) {
        return s;
    }
    const char *close = NULL;
    if (open_ch == '{' && close_ch == '}') {
        close = menu_find_matching_brace(open);
    } else {
        close = strchr(open + 1, close_ch);
    }
    if (close == NULL) {
        return s;
    }
    return close + 1;
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

char *fbwl_menu_parse_dirname_owned(const char *path) {
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

char *fbwl_menu_parse_path_join(const char *dir, const char *rel) {
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

char *fbwl_menu_parse_expand_tilde_owned(const char *path) {
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

char *fbwl_menu_parse_resolve_path(const char *base_dir, const char *path) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }

    char *expanded = fbwl_menu_parse_expand_tilde_owned(path);
    if (expanded == NULL) {
        return NULL;
    }

    if (expanded[0] == '/' || base_dir == NULL || *base_dir == '\0') {
        return expanded;
    }

    char *joined = fbwl_menu_parse_path_join(base_dir, expanded);
    free(expanded);
    return joined;
}

bool fbwl_menu_parse_stat_is_dir(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool fbwl_menu_parse_stat_is_regular_file(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

char *fbwl_menu_parse_shell_escape_single_quoted(const char *s) {
    if (s == NULL) {
        return NULL;
    }

    size_t quotes = 0;
    const size_t len = strlen(s);
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') {
            quotes++;
        }
    }

    size_t needed = len + quotes * 3 + 2 + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }

    char *p = out;
    *p++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = s[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return out;
}

const char *fbwl_menu_parse_basename(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

