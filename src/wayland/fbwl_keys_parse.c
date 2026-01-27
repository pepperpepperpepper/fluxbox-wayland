#include "wayland/fbwl_keys_parse.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_fluxbox_cmd.h"

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

static bool starts_with_token(const char *s, const char *prefix) {
    if (s == NULL || prefix == NULL) {
        return false;
    }
    size_t n = strlen(prefix);
    return strncasecmp(s, prefix, n) == 0;
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
    if (strcasecmp(token, "mod2") == 0) {
        *mods |= WLR_MODIFIER_MOD2;
        return true;
    }
    if (strcasecmp(token, "mod3") == 0) {
        *mods |= WLR_MODIFIER_MOD3;
        return true;
    }
    if (strcasecmp(token, "mod4") == 0 || strcasecmp(token, "super") == 0 ||
            strcasecmp(token, "logo") == 0 || strcasecmp(token, "win") == 0) {
        *mods |= WLR_MODIFIER_LOGO;
        return true;
    }
    if (strcasecmp(token, "mod5") == 0) {
        *mods |= WLR_MODIFIER_MOD5;
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

        enum fbwl_keybinding_key_kind key_kind = FBWL_KEYBIND_KEYSYM;
        uint32_t keycode = 0;
        xkb_keysym_t sym = xkb_keysym_from_name(key_name, XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym != XKB_KEY_NoSymbol) {
            key_kind = FBWL_KEYBIND_KEYSYM;
        } else {
            bool is_keycode = true;
            for (const char *p = key_name; *p != '\0'; p++) {
                if (!isdigit((unsigned char)*p)) {
                    is_keycode = false;
                    break;
                }
            }
            if (!is_keycode) {
                continue;
            }

            char *end = NULL;
            long code = strtol(key_name, &end, 10);
            if (end == key_name || *end != '\0') {
                continue;
            }
            if (code < 0 || code > 100000) {
                continue;
            }
            key_kind = FBWL_KEYBIND_KEYCODE;
            keycode = (uint32_t)code;
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

        enum fbwl_keybinding_action action;
        int action_arg = 0;
        const char *action_cmd = NULL;
        if (!fbwl_fluxbox_cmd_resolve(cmd_name, cmd_args, &action, &action_arg, &action_cmd)) {
            continue;
        }

        if (add_binding(userdata, key_kind, keycode, sym, mods, action, action_arg, action_cmd)) {
            added++;
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

static enum fbwl_mousebinding_context parse_mouse_context(const char *token) {
    if (token == NULL) {
        return FBWL_MOUSEBIND_ANY;
    }
    if (strcasecmp(token, "ondesktop") == 0) {
        return FBWL_MOUSEBIND_DESKTOP;
    }
    if (strcasecmp(token, "onwindow") == 0) {
        return FBWL_MOUSEBIND_WINDOW;
    }
    if (strcasecmp(token, "ontitlebar") == 0) {
        return FBWL_MOUSEBIND_TITLEBAR;
    }
    if (strcasecmp(token, "ontoolbar") == 0) {
        return FBWL_MOUSEBIND_TOOLBAR;
    }
    if (strcasecmp(token, "onwindowborder") == 0 ||
            strcasecmp(token, "onleftgrip") == 0 ||
            strcasecmp(token, "onrightgrip") == 0) {
        return FBWL_MOUSEBIND_WINDOW_BORDER;
    }
    return FBWL_MOUSEBIND_ANY;
}

static int parse_mouse_button(const char *token) {
    if (token == NULL) {
        return 0;
    }
    if (!starts_with_token(token, "mouse") && !starts_with_token(token, "button") && !starts_with_token(token, "move")) {
        return 0;
    }

    const char *p = token;
    while (*p != '\0' && !isdigit((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        return 0;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) {
        return 0;
    }
    if (v < 1 || v > 32) {
        return 0;
    }
    return (int)v;
}

bool fbwl_keys_parse_file_mouse(const char *path, fbwl_keys_add_mouse_binding_fn add_binding, void *userdata,
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

        char *save = NULL;
        char *tok = NULL;
        const char *tokens[64];
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

        enum fbwl_mousebinding_context context = FBWL_MOUSEBIND_ANY;
        int button = 0;
        uint32_t mods = 0;

        bool ok = true;
        for (size_t i = 0; i < ntok; i++) {
            const char *t = tokens[i];
            if (t == NULL || *t == '\0') {
                continue;
            }
            enum fbwl_mousebinding_context c = parse_mouse_context(t);
            if (c != FBWL_MOUSEBIND_ANY) {
                context = c;
                continue;
            }
            int b = parse_mouse_button(t);
            if (b != 0) {
                button = b;
                continue;
            }
            if (strcasecmp(t, "double") == 0) {
                continue;
            }
            if (starts_with_token(t, "on")) {
                continue;
            }
            if (!parse_keys_modifier(t, &mods)) {
                ok = false;
                break;
            }
        }
        if (!ok || button == 0) {
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

        enum fbwl_keybinding_action action;
        int action_arg = 0;
        const char *action_cmd = NULL;
        if (!fbwl_fluxbox_cmd_resolve(cmd_name, cmd_args, &action, &action_arg, &action_cmd)) {
            continue;
        }

        if (add_binding(userdata, context, button, mods, action, action_arg, action_cmd)) {
            added++;
        }
    }

    free(line);
    fclose(f);

    wlr_log(WLR_INFO, "Keys: loaded %zu mouse bindings from %s", added, path);
    if (out_added != NULL) {
        *out_added = added;
    }
    return true;
}
