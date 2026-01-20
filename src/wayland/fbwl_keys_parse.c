#include "wayland/fbwl_keys_parse.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

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

static bool parse_keys_modifier(const char *token, uint32_t *mods) {
    if (token == NULL || mods == NULL) {
        return false;
    }

    if (strcasecmp(token, "none") == 0) {
        return true;
    }
    if (strcasecmp(token, "mod1") == 0 || strcasecmp(token, "alt") == 0) {
        *mods |= WLR_MODIFIER_ALT;
        return true;
    }
    if (strcasecmp(token, "mod4") == 0 || strcasecmp(token, "super") == 0 ||
            strcasecmp(token, "logo") == 0 || strcasecmp(token, "win") == 0) {
        *mods |= WLR_MODIFIER_LOGO;
        return true;
    }
    if (strcasecmp(token, "control") == 0 || strcasecmp(token, "ctrl") == 0) {
        *mods |= WLR_MODIFIER_CTRL;
        return true;
    }
    if (strcasecmp(token, "shift") == 0) {
        *mods |= WLR_MODIFIER_SHIFT;
        return true;
    }

    return false;
}

bool fbwl_keys_parse_file(const char *path, fbwl_keys_add_binding_fn add_binding, void *userdata,
        size_t *out_added) {
    if (path == NULL || *path == '\0' || add_binding == NULL) {
        return false;
    }
    if (out_added != NULL) {
        *out_added = 0;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Keys: failed to open %s: %s", path, strerror(errno));
        return false;
    }

    size_t added = 0;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
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

        char *colon = strchr(s, ':');
        if (colon == NULL) {
            continue;
        }
        *colon = '\0';
        char *lhs = trim_inplace(s);
        char *rhs = trim_inplace(colon + 1);
        if (lhs == NULL || rhs == NULL || *lhs == '\0' || *rhs == '\0') {
            continue;
        }

        for (char *p = lhs; *p != '\0'; p++) {
            if (*p == '+') {
                *p = ' ';
            }
        }

        uint32_t mods = 0;
        bool ok = true;

        char *save = NULL;
        char *tok = NULL;
        const char *tokens[32];
        size_t ntok = 0;
        for (tok = strtok_r(lhs, " \t", &save);
                tok != NULL;
                tok = strtok_r(NULL, " \t", &save)) {
            if (ntok < (sizeof(tokens) / sizeof(tokens[0]))) {
                tokens[ntok++] = tok;
            } else {
                ntok++;
            }
        }
        if (ntok == 0 || ntok > (sizeof(tokens) / sizeof(tokens[0]))) {
            continue;
        }

        for (size_t i = 0; i + 1 < ntok; i++) {
            if (strncasecmp(tokens[i], "on", 2) == 0) {
                continue;
            }
            if (strncasecmp(tokens[i], "mouse", 5) == 0 || strncasecmp(tokens[i], "button", 6) == 0) {
                ok = false;
                break;
            }
            if (!parse_keys_modifier(tokens[i], &mods)) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }

        const char *key_name = tokens[ntok - 1];
        if (key_name == NULL || *key_name == '\0') {
            continue;
        }
        xkb_keysym_t sym = xkb_keysym_from_name(key_name, XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == XKB_KEY_NoSymbol) {
            continue;
        }

        char *cmd = rhs;
        char *sp = cmd;
        while (*sp != '\0' && !isspace((unsigned char)*sp)) {
            sp++;
        }
        char *cmd_args = sp;
        if (*sp != '\0') {
            *sp = '\0';
            cmd_args = sp + 1;
        }
        const char *cmd_name = cmd;
        cmd_args = trim_inplace(cmd_args);

        if (strcasecmp(cmd_name, "execcommand") == 0 || strcasecmp(cmd_name, "exec") == 0) {
            if (cmd_args == NULL || *cmd_args == '\0') {
                continue;
            }
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_EXEC, 0, cmd_args)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "commanddialog") == 0 || strcasecmp(cmd_name, "rundialog") == 0) {
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_COMMAND_DIALOG, 0, NULL)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "exit") == 0) {
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_EXIT, 0, NULL)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "nextwindow") == 0) {
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_FOCUS_NEXT, 0, NULL)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "maximize") == 0) {
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_TOGGLE_MAXIMIZE, 0, NULL)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "fullscreen") == 0) {
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_TOGGLE_FULLSCREEN, 0, NULL)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "minimize") == 0 || strcasecmp(cmd_name, "iconify") == 0) {
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_TOGGLE_MINIMIZE, 0, NULL)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "workspace") == 0) {
            if (cmd_args == NULL || *cmd_args == '\0') {
                continue;
            }
            char *end = NULL;
            long ws = strtol(cmd_args, &end, 10);
            if (end == cmd_args) {
                continue;
            }
            if (ws > 0) {
                ws -= 1;
            }
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_WORKSPACE_SWITCH, (int)ws, NULL)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "sendtoworkspace") == 0) {
            if (cmd_args == NULL || *cmd_args == '\0') {
                continue;
            }
            char *end = NULL;
            long ws = strtol(cmd_args, &end, 10);
            if (end == cmd_args) {
                continue;
            }
            if (ws > 0) {
                ws -= 1;
            }
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_SEND_TO_WORKSPACE, (int)ws, NULL)) {
                added++;
            }
            continue;
        }
        if (strcasecmp(cmd_name, "taketoworkspace") == 0) {
            if (cmd_args == NULL || *cmd_args == '\0') {
                continue;
            }
            char *end = NULL;
            long ws = strtol(cmd_args, &end, 10);
            if (end == cmd_args) {
                continue;
            }
            if (ws > 0) {
                ws -= 1;
            }
            if (add_binding(userdata, sym, mods, FBWL_KEYBIND_TAKE_TO_WORKSPACE, (int)ws, NULL)) {
                added++;
            }
            continue;
        }
    }

    free(line);
    fclose(f);

    wlr_log(WLR_INFO, "Keys: loaded %zu bindings from %s", added, path);
    if (out_added != NULL) {
        *out_added = added;
    }
    return true;
}
