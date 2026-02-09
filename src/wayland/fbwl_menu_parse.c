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
#include "wayland/fbwl_menu_parse_encoding.h"
#include "wmcore/fbwm_core.h"

enum {
    MENU_MAX_INCLUDE_DEPTH = 8,
};

static bool menu_skip_name(const char *name) {
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
static char *menu_parse_brace_value(const char *s) {
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
    return dup_trim_range(open + 1, close);
}

static char *menu_parse_angle_value(const char *s) {
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
    return dup_trim_range(open + 1, close);
}

static const char *menu_after_delim(const char *s, char open_ch, char close_ch) {
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

static char *menu_shell_escape_single_quoted(const char *s) {
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

static const char *menu_basename(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static bool menu_add_styles_from_dir(struct fbwl_menu *menu, const char *dir_path, const char *icon) {
    if (menu == NULL || dir_path == NULL || *dir_path == '\0') {
        return false;
    }

    struct dirent **namelist = NULL;
    int n = scandir(dir_path, &namelist, NULL, alphasort);
    if (n < 0) {
        wlr_log(WLR_ERROR, "Menu: failed to scan styles dir %s: %s", dir_path, strerror(errno));
        return true;
    }

    bool any = false;
    for (int i = 0; i < n; i++) {
        struct dirent *ent = namelist[i];
        if (ent != NULL && ent->d_name != NULL) {
            const char *name = ent->d_name;
            if (!menu_skip_name(name) && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                char *child = menu_path_join(dir_path, name);
                    if (child != NULL) {
                        if (menu_stat_is_regular_file(child)) {
                            if (fbwl_menu_add_server_action(menu, name, icon, FBWL_MENU_SERVER_SET_STYLE, 0, child)) {
                                any = true;
                            }
                        }
                        free(child);
                    }
            }
        }
        free(namelist[i]);
    }
    free(namelist);

    if (!any) {
        wlr_log(WLR_INFO, "Menu: no styles found in %s", dir_path);
    }
    return true;
}

static bool menu_add_wallpapers_from_dir(struct fbwl_menu *menu, const char *dir_path, const char *cmd_base,
        const char *icon) {
    if (menu == NULL || dir_path == NULL || *dir_path == '\0') {
        return false;
    }

    const bool use_server_action = cmd_base == NULL || *cmd_base == '\0';
    const char *prog = cmd_base != NULL && *cmd_base != '\0' ? cmd_base : "fbsetbg";

    struct dirent **namelist = NULL;
    int n = scandir(dir_path, &namelist, NULL, alphasort);
    if (n < 0) {
        wlr_log(WLR_ERROR, "Menu: failed to scan wallpapers dir %s: %s", dir_path, strerror(errno));
        return true;
    }

    bool any = false;
    for (int i = 0; i < n; i++) {
        struct dirent *ent = namelist[i];
        if (ent != NULL && ent->d_name != NULL) {
            const char *name = ent->d_name;
            if (!menu_skip_name(name) && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                char *child = menu_path_join(dir_path, name);
                if (child != NULL) {
                    if (menu_stat_is_regular_file(child)) {
                        if (use_server_action) {
                            if (fbwl_menu_add_server_action(menu, name, icon, FBWL_MENU_SERVER_SET_WALLPAPER, 0, child)) {
                                any = true;
                            }
                        } else {
                            char *quoted = menu_shell_escape_single_quoted(child);
                            if (quoted != NULL) {
                                size_t needed = strlen(prog) + 1 + strlen(quoted) + 1;
                                char *cmd = malloc(needed);
                                if (cmd != NULL) {
                                    snprintf(cmd, needed, "%s %s", prog, quoted);
                                    if (fbwl_menu_add_exec(menu, name, cmd, icon)) {
                                        any = true;
                                    }
                                    free(cmd);
                                }
                                free(quoted);
                            }
                        }
                    }
                    free(child);
                }
            }
        }
        free(namelist[i]);
    }
    free(namelist);

    if (!any) {
        wlr_log(WLR_INFO, "Menu: no wallpapers found in %s", dir_path);
    }
    return true;
}

static bool menu_parse_path(struct fbwl_menu *stack[16], size_t *depth, size_t min_depth,
        struct fbwl_menu_parse_state *st,
        struct fbwm_core *wm, const char *path, int include_depth, bool required);

static bool menu_parse_dir(struct fbwl_menu *stack[16], size_t *depth, size_t min_depth,
        struct fbwl_menu_parse_state *st,
        struct fbwm_core *wm, const char *dir_path, int include_depth) {
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
                        (void)menu_parse_path(stack, depth, min_depth, st, wm, child, include_depth + 1, false);
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
        struct fbwl_menu_parse_state *st, struct fbwm_core *wm, const char *path, int include_depth, bool required) {
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
        const char *encoding = fbwl_menu_parse_state_encoding(st);

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

        if (strcasecmp(key, "encoding") == 0) {
            char *enc = menu_parse_brace_value(close + 1);
            if (enc != NULL) {
                fbwl_menu_parse_convert_owned_to_utf8(&enc, encoding);
                (void)fbwl_menu_parse_state_push_encoding(st, enc);
            }
            free(enc);
            free(key);
            continue;
        }

        if (strcasecmp(key, "endencoding") == 0) {
            (void)fbwl_menu_parse_state_pop_encoding(st);
            free(key);
            continue;
        }

        if (strcasecmp(key, "submenu") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            icon_s = menu_after_delim(icon_s, '{', '}');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            struct fbwl_menu *submenu = fbwl_menu_create(label);
            if (submenu != NULL) {
                if (fbwl_menu_add_submenu(cur, label, submenu, icon)) {
                    if (*depth + 1 < 16) {
                        (*depth)++;
                        stack[*depth] = submenu;
                    }
                } else {
                    fbwl_menu_free(submenu);
                }
            }
            free(label);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "exec") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            char *cmd = menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&cmd, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            icon_s = menu_after_delim(icon_s, '{', '}');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;
            if (cmd != NULL) {
                (void)fbwl_menu_add_exec(cur, label, cmd, icon);
            }
            free(label);
            free(cmd);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "exit") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            (void)fbwl_menu_add_exit(cur, label, icon);
            free(label);
            free(icon_raw);
            free(icon);
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
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            (void)fbwl_menu_add_nop(cur, label, icon);
            free(label);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "include") == 0) {
            char *raw = menu_parse_paren_value(close + 1);
            if (raw != NULL && *raw != '\0') {
                fbwl_menu_parse_convert_owned_to_utf8(&raw, encoding);
                char *resolved = menu_resolve_path(base_dir, raw);
                if (resolved != NULL) {
                    if (include_depth >= MENU_MAX_INCLUDE_DEPTH) {
                        wlr_log(WLR_ERROR, "Menu: include depth exceeded while including %s", resolved);
                    } else if (menu_stat_is_dir(resolved)) {
                        (void)menu_parse_dir(stack, depth, *depth, st, wm, resolved, include_depth + 1);
                    } else {
                        (void)menu_parse_path(stack, depth, *depth, st, wm, resolved, include_depth + 1, false);
                    }
                    free(resolved);
                }
            }
            free(raw);
            free(key);
            continue;
        }

        if (strcasecmp(key, "reconfig") == 0 || strcasecmp(key, "reconfigure") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            const char *menu_label = label != NULL ? label : "Reconfigure";
            (void)fbwl_menu_add_server_action(cur, menu_label, icon, FBWL_MENU_SERVER_RECONFIGURE, 0, NULL);
            free(label);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "style") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            char *raw = menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&raw, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            icon_s = menu_after_delim(icon_s, '{', '}');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            if (raw != NULL && *raw != '\0') {
                char *resolved = menu_resolve_path(base_dir, raw);
                if (resolved != NULL) {
                    const char *base = label != NULL && *label != '\0' ? label : menu_basename(resolved);
                    (void)fbwl_menu_add_server_action(cur, base, icon, FBWL_MENU_SERVER_SET_STYLE, 0, resolved);
                    free(resolved);
                }
            }
            free(label);
            free(raw);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "stylesmenu") == 0 || strcasecmp(key, "themesmenu") == 0) {
            char *paren = menu_parse_paren_value(close + 1);
            char *brace = menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&paren, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&brace, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            icon_s = menu_after_delim(icon_s, '{', '}');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            const char *raw_dir = brace != NULL ? brace : paren;
            if (raw_dir != NULL && *raw_dir != '\0') {
                char *resolved = menu_resolve_path(base_dir, raw_dir);
                if (resolved != NULL) {
                    (void)menu_add_styles_from_dir(cur, resolved, icon);
                    free(resolved);
                }
            }
            free(paren);
            free(brace);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "stylesdir") == 0 || strcasecmp(key, "themesdir") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            char *dir = menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&dir, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            icon_s = menu_after_delim(icon_s, '{', '}');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            if (dir != NULL && *dir != '\0') {
                char *resolved = menu_resolve_path(base_dir, dir);
                if (resolved != NULL) {
                    const char *submenu_label = NULL;
                    if (label != NULL && *label != '\0') {
                        submenu_label = label;
                    } else if (menu_basename(resolved) != NULL && *menu_basename(resolved) != '\0') {
                        submenu_label = menu_basename(resolved);
                    } else {
                        submenu_label = "Styles";
                    }

                    struct fbwl_menu *submenu = fbwl_menu_create(submenu_label);
                    if (submenu != NULL) {
                        if (fbwl_menu_add_submenu(cur, submenu_label, submenu, icon)) {
                            (void)menu_add_styles_from_dir(submenu, resolved, NULL);
                        } else {
                            fbwl_menu_free(submenu);
                        }
                    }
                    free(resolved);
                }
            } else if (label != NULL && *label != '\0') {
                char *resolved = menu_resolve_path(base_dir, label);
                if (resolved != NULL) {
                    (void)menu_add_styles_from_dir(cur, resolved, icon);
                    free(resolved);
                }
            }
            free(label);
            free(dir);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "wallpapers") == 0 || strcasecmp(key, "wallpapermenu") == 0 ||
                strcasecmp(key, "rootcommands") == 0) {
            char *raw_dir = menu_parse_paren_value(close + 1);
            char *cmd = menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&raw_dir, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&cmd, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            icon_s = menu_after_delim(icon_s, '{', '}');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            if (raw_dir != NULL && *raw_dir != '\0') {
                char *resolved = menu_resolve_path(base_dir, raw_dir);
                if (resolved != NULL) {
                    (void)menu_add_wallpapers_from_dir(cur, resolved, cmd, icon);
                    free(resolved);
                }
            }
            free(raw_dir);
            free(cmd);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "workspaces") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            const char *menu_label = label != NULL ? label : "Workspaces";
            struct fbwl_menu *submenu = fbwl_menu_create(menu_label);
            if (submenu != NULL) {
                if (fbwl_menu_add_submenu(cur, menu_label, submenu, icon)) {
                    if (wm == NULL) {
                        wlr_log(WLR_ERROR, "Menu: ignoring [workspaces] without wm context");
                    } else {
                        const int count = fbwm_core_workspace_count(wm);
                        for (int i = 0; i < count; i++) {
                            const char *name = fbwm_core_workspace_name(wm, i);
                            char ws_label[256];
                            if (name != NULL && *name != '\0') {
                                snprintf(ws_label, sizeof(ws_label), "%d: %s", i + 1, name);
                            } else {
                                snprintf(ws_label, sizeof(ws_label), "%d", i + 1);
                            }
                            (void)fbwl_menu_add_workspace_switch(submenu, ws_label, i, NULL);
                        }
                    }
                } else {
                    fbwl_menu_free(submenu);
                }
            }
            free(label);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "config") == 0) {
            char *label = menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = menu_after_delim(icon_s, '(', ')');
            char *icon_raw = menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? menu_resolve_path(base_dir, icon_raw) : NULL;

            const char *menu_label = label != NULL && *label != '\0' ? label : "Config";
            struct fbwl_menu *submenu = fbwl_menu_create(menu_label);
            if (submenu != NULL) {
                if (fbwl_menu_add_submenu(cur, menu_label, submenu, icon)) {
                    struct fbwl_menu *focus_menu = fbwl_menu_create("Focus Model");
                    if (focus_menu != NULL) {
                        if (fbwl_menu_add_submenu(submenu, "Focus Model", focus_menu, NULL)) {
                            (void)fbwl_menu_add_server_action(focus_menu, "Click To Focus", NULL,
                                FBWL_MENU_SERVER_SET_FOCUS_MODEL, 0, NULL);
                            (void)fbwl_menu_add_server_action(focus_menu, "Mouse Focus", NULL,
                                FBWL_MENU_SERVER_SET_FOCUS_MODEL, 1, NULL);
                            (void)fbwl_menu_add_server_action(focus_menu, "Strict Mouse Focus", NULL,
                                FBWL_MENU_SERVER_SET_FOCUS_MODEL, 2, NULL);
                        } else {
                            fbwl_menu_free(focus_menu);
                        }
                    }

                    (void)fbwl_menu_add_separator(submenu);
                    (void)fbwl_menu_add_server_action(submenu, "Auto Raise", NULL,
                        FBWL_MENU_SERVER_TOGGLE_AUTO_RAISE, 0, NULL);
                    (void)fbwl_menu_add_server_action(submenu, "Click Raises", NULL,
                        FBWL_MENU_SERVER_TOGGLE_CLICK_RAISES, 0, NULL);
                    (void)fbwl_menu_add_server_action(submenu, "Focus New Windows", NULL,
                        FBWL_MENU_SERVER_TOGGLE_FOCUS_NEW_WINDOWS, 0, NULL);

                    (void)fbwl_menu_add_separator(submenu);
                    struct fbwl_menu *place_menu = fbwl_menu_create("Window Placement");
                    if (place_menu != NULL) {
                        if (fbwl_menu_add_submenu(submenu, "Window Placement", place_menu, NULL)) {
                            (void)fbwl_menu_add_server_action(place_menu, "Row Smart", NULL,
                                FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT, FBWM_PLACE_ROW_SMART, NULL);
                            (void)fbwl_menu_add_server_action(place_menu, "Col Smart", NULL,
                                FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT, FBWM_PLACE_COL_SMART, NULL);
                            (void)fbwl_menu_add_server_action(place_menu, "Cascade", NULL,
                                FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT, FBWM_PLACE_CASCADE, NULL);
                            (void)fbwl_menu_add_server_action(place_menu, "Under Mouse", NULL,
                                FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT, FBWM_PLACE_UNDER_MOUSE, NULL);
                            (void)fbwl_menu_add_server_action(place_menu, "Row Min Overlap", NULL,
                                FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT, FBWM_PLACE_ROW_MIN_OVERLAP, NULL);
                            (void)fbwl_menu_add_server_action(place_menu, "Col Min Overlap", NULL,
                                FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT, FBWM_PLACE_COL_MIN_OVERLAP, NULL);
                            (void)fbwl_menu_add_server_action(place_menu, "Auto Tab", NULL,
                                FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT, FBWM_PLACE_AUTOTAB, NULL);
                        } else {
                            fbwl_menu_free(place_menu);
                        }
                    }

                    struct fbwl_menu *row_menu = fbwl_menu_create("Row Direction");
                    if (row_menu != NULL) {
                        if (fbwl_menu_add_submenu(submenu, "Row Direction", row_menu, NULL)) {
                            (void)fbwl_menu_add_server_action(row_menu, "Left To Right", NULL,
                                FBWL_MENU_SERVER_SET_ROW_PLACEMENT_DIRECTION, FBWM_ROW_LEFT_TO_RIGHT, NULL);
                            (void)fbwl_menu_add_server_action(row_menu, "Right To Left", NULL,
                                FBWL_MENU_SERVER_SET_ROW_PLACEMENT_DIRECTION, FBWM_ROW_RIGHT_TO_LEFT, NULL);
                        } else {
                            fbwl_menu_free(row_menu);
                        }
                    }

                    struct fbwl_menu *col_menu = fbwl_menu_create("Col Direction");
                    if (col_menu != NULL) {
                        if (fbwl_menu_add_submenu(submenu, "Col Direction", col_menu, NULL)) {
                            (void)fbwl_menu_add_server_action(col_menu, "Top To Bottom", NULL,
                                FBWL_MENU_SERVER_SET_COL_PLACEMENT_DIRECTION, FBWM_COL_TOP_TO_BOTTOM, NULL);
                            (void)fbwl_menu_add_server_action(col_menu, "Bottom To Top", NULL,
                                FBWL_MENU_SERVER_SET_COL_PLACEMENT_DIRECTION, FBWM_COL_BOTTOM_TO_TOP, NULL);
                        } else {
                            fbwl_menu_free(col_menu);
                        }
                    }

                    (void)fbwl_menu_add_separator(submenu);
                    (void)fbwl_menu_add_server_action(submenu, "Reconfigure", NULL,
                        FBWL_MENU_SERVER_RECONFIGURE, 0, NULL);
                } else {
                    fbwl_menu_free(submenu);
                }
            }
            free(label);
            free(icon_raw);
            free(icon);
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
        struct fbwl_menu_parse_state *st,
        struct fbwm_core *wm, const char *path, int include_depth, bool required) {
    if (stack == NULL || depth == NULL || path == NULL || *path == '\0') {
        return false;
    }

    if (menu_stat_is_dir(path)) {
        return menu_parse_dir(stack, depth, min_depth, st, wm, path, include_depth);
    }

    if (!menu_stat_is_regular_file(path)) {
        if (required) {
            wlr_log(WLR_ERROR, "Menu: not a regular file: %s", path);
        }
        return !required;
    }

    return menu_parse_file_impl(stack, depth, min_depth, st, wm, path, include_depth, required);
}

bool fbwl_menu_parse_file(struct fbwl_menu *root, struct fbwm_core *wm, const char *path) {
    if (root == NULL || path == NULL || *path == '\0') {
        return false;
    }

    struct fbwl_menu *stack[16] = {0};
    size_t depth = 0;
    stack[0] = root;
    struct fbwl_menu_parse_state st = {0};
    bool ok = menu_parse_path(stack, &depth, 0, &st, wm, path, 0, true);
    fbwl_menu_parse_state_clear(&st);
    return ok;
}
