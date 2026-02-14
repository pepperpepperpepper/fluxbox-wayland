#include "wayland/fbwl_menu_parse.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_menu_parse_encoding.h"
#include "wayland/fbwl_menu_parse_util.h"
#include "wmcore/fbwm_core.h"

enum {
    MENU_MAX_INCLUDE_DEPTH = 8,
};

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
            if (!fbwl_menu_parse_skip_name(name) && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                char *child = fbwl_menu_parse_path_join(dir_path, name);
                    if (child != NULL) {
                        if (fbwl_menu_parse_stat_is_regular_file(child)) {
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
            if (!fbwl_menu_parse_skip_name(name) && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                char *child = fbwl_menu_parse_path_join(dir_path, name);
                if (child != NULL) {
                    if (fbwl_menu_parse_stat_is_regular_file(child)) {
                        if (use_server_action) {
                            if (fbwl_menu_add_server_action(menu, name, icon, FBWL_MENU_SERVER_SET_WALLPAPER, 0, child)) {
                                any = true;
                            }
                        } else {
                            char *quoted = fbwl_menu_parse_shell_escape_single_quoted(child);
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
                char *child = fbwl_menu_parse_path_join(dir_path, name);
                if (child != NULL) {
                    if (fbwl_menu_parse_stat_is_regular_file(child)) {
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

    char *base_dir_owned = fbwl_menu_parse_dirname_owned(path);
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

        char *s = fbwl_menu_parse_trim_inplace(line);
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

        char *key = fbwl_menu_parse_dup_trim_range(open + 1, close);
        if (key == NULL) {
            continue;
        }

        struct fbwl_menu *cur = stack[*depth];
        const char *encoding = fbwl_menu_parse_state_encoding(st);

        if (strcasecmp(key, "begin") == 0) {
            char *label = fbwl_menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);
            if (label != NULL && *label != '\0') {
                free(cur->label);
                cur->label = label;
            } else {
                free(label);
            }
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
            char *enc = fbwl_menu_parse_brace_value(close + 1);
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
            char *label = fbwl_menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            icon_s = fbwl_menu_parse_after_delim(icon_s, '{', '}');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

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
            char *label = fbwl_menu_parse_paren_value(close + 1);
            char *cmd = fbwl_menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&cmd, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            icon_s = fbwl_menu_parse_after_delim(icon_s, '{', '}');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;
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
            char *label = fbwl_menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

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
            char *label = fbwl_menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

            (void)fbwl_menu_add_nop(cur, label, icon);
            free(label);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "include") == 0) {
            char *raw = fbwl_menu_parse_paren_value(close + 1);
            if (raw != NULL && *raw != '\0') {
                fbwl_menu_parse_convert_owned_to_utf8(&raw, encoding);
                char *resolved = fbwl_menu_parse_resolve_path(base_dir, raw);
                if (resolved != NULL) {
                    if (include_depth >= MENU_MAX_INCLUDE_DEPTH) {
                        wlr_log(WLR_ERROR, "Menu: include depth exceeded while including %s", resolved);
                    } else if (fbwl_menu_parse_stat_is_dir(resolved)) {
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
            char *label = fbwl_menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

            const char *menu_label = label != NULL ? label : "Reconfigure";
            (void)fbwl_menu_add_server_action(cur, menu_label, icon, FBWL_MENU_SERVER_RECONFIGURE, 0, NULL);
            free(label);
            free(icon_raw);
            free(icon);
            free(key);
            continue;
        }

        if (strcasecmp(key, "style") == 0) {
            char *label = fbwl_menu_parse_paren_value(close + 1);
            char *raw = fbwl_menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&raw, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            icon_s = fbwl_menu_parse_after_delim(icon_s, '{', '}');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

            if (raw != NULL && *raw != '\0') {
                char *resolved = fbwl_menu_parse_resolve_path(base_dir, raw);
                if (resolved != NULL) {
                    const char *base = label != NULL && *label != '\0' ? label : fbwl_menu_parse_basename(resolved);
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
            char *paren = fbwl_menu_parse_paren_value(close + 1);
            char *brace = fbwl_menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&paren, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&brace, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            icon_s = fbwl_menu_parse_after_delim(icon_s, '{', '}');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

            const char *raw_dir = brace != NULL ? brace : paren;
            if (raw_dir != NULL && *raw_dir != '\0') {
                char *resolved = fbwl_menu_parse_resolve_path(base_dir, raw_dir);
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
            char *label = fbwl_menu_parse_paren_value(close + 1);
            char *dir = fbwl_menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&dir, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            icon_s = fbwl_menu_parse_after_delim(icon_s, '{', '}');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

            if (dir != NULL && *dir != '\0') {
                char *resolved = fbwl_menu_parse_resolve_path(base_dir, dir);
                if (resolved != NULL) {
                    const char *submenu_label = NULL;
                    if (label != NULL && *label != '\0') {
                        submenu_label = label;
                    } else if (fbwl_menu_parse_basename(resolved) != NULL && *fbwl_menu_parse_basename(resolved) != '\0') {
                        submenu_label = fbwl_menu_parse_basename(resolved);
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
                char *resolved = fbwl_menu_parse_resolve_path(base_dir, label);
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
            char *raw_dir = fbwl_menu_parse_paren_value(close + 1);
            char *cmd = fbwl_menu_parse_brace_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&raw_dir, encoding);
            fbwl_menu_parse_convert_owned_to_utf8(&cmd, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            icon_s = fbwl_menu_parse_after_delim(icon_s, '{', '}');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

            if (raw_dir != NULL && *raw_dir != '\0') {
                char *resolved = fbwl_menu_parse_resolve_path(base_dir, raw_dir);
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
            char *label = fbwl_menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

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
            char *label = fbwl_menu_parse_paren_value(close + 1);
            fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);

            const char *icon_s = close + 1;
            icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
            char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
            fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
            char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

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

        // Generic Fluxbox command tags: allow any cmdlang line from fluxbox-keys(5) via:
        //   [command] (label) {args} <icon>
        char *label = fbwl_menu_parse_paren_value(close + 1);
        char *args = fbwl_menu_parse_brace_value(close + 1);
        fbwl_menu_parse_convert_owned_to_utf8(&label, encoding);
        fbwl_menu_parse_convert_owned_to_utf8(&args, encoding);

        const char *icon_s = close + 1;
        icon_s = fbwl_menu_parse_after_delim(icon_s, '(', ')');
        icon_s = fbwl_menu_parse_after_delim(icon_s, '{', '}');
        char *icon_raw = fbwl_menu_parse_angle_value(icon_s);
        fbwl_menu_parse_convert_owned_to_utf8(&icon_raw, encoding);
        char *icon = icon_raw != NULL ? fbwl_menu_parse_resolve_path(base_dir, icon_raw) : NULL;

        const char *use_label = (label != NULL && *label != '\0') ? label : key;
        char *cmd_line = NULL;
        if (args != NULL && *args != '\0') {
            const size_t needed = strlen(key) + 1 + strlen(args) + 1;
            cmd_line = malloc(needed);
            if (cmd_line != NULL) {
                snprintf(cmd_line, needed, "%s %s", key, args);
            }
        } else {
            cmd_line = strdup(key);
        }

        if (cmd_line != NULL) {
            (void)fbwl_menu_add_server_action(cur, use_label, icon, FBWL_MENU_SERVER_CMDLANG, 0, cmd_line);
        }

        free(cmd_line);
        free(label);
        free(args);
        free(icon_raw);
        free(icon);
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

    if (fbwl_menu_parse_stat_is_dir(path)) {
        return menu_parse_dir(stack, depth, min_depth, st, wm, path, include_depth);
    }

    if (!fbwl_menu_parse_stat_is_regular_file(path)) {
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
