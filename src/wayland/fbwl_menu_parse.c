#include "wayland/fbwl_menu_parse.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_menu.h"

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

bool fbwl_menu_parse_file(struct fbwl_menu *root, const char *path) {
    if (root == NULL || path == NULL || *path == '\0') {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Menu: failed to open %s: %s", path, strerror(errno));
        return false;
    }

    struct fbwl_menu *stack[16] = {0};
    size_t depth = 0;
    stack[0] = root;

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

        struct fbwl_menu *cur = stack[depth];

        if (strcasecmp(key, "begin") == 0) {
            free(key);
            continue;
        }

        if (strcasecmp(key, "end") == 0) {
            if (depth > 0) {
                depth--;
            }
            free(key);
            continue;
        }

        if (strcasecmp(key, "submenu") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            struct fbwl_menu *submenu = fbwl_menu_create(label);
            if (submenu != NULL) {
                if (fbwl_menu_add_submenu(cur, label, submenu)) {
                    if (depth + 1 < (sizeof(stack) / sizeof(stack[0]))) {
                        depth++;
                        stack[depth] = submenu;
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

        free(key);
    }

    free(line);
    fclose(f);
    return true;
}
