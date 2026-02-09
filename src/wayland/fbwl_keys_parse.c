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

static uint32_t next_chain_id = 1;

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

static char *find_command_colon(char *s) {
    if (s == NULL) {
        return NULL;
    }
    for (char *p = s; *p != '\0'; p++) {
        if (*p != ':') {
            continue;
        }
        if (p == s || isspace((unsigned char)p[-1])) {
            return p;
        }
    }
    return NULL;
}

static bool starts_with_token(const char *s, const char *prefix) {
    if (s == NULL || prefix == NULL) {
        return false;
    }
    size_t n = strlen(prefix);
    return strncasecmp(s, prefix, n) == 0;
}

static bool mode_is_default(const char *mode) {
    return mode == NULL || *mode == '\0' || strcasecmp(mode, "default") == 0;
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

        char *colon = find_command_colon(s);
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

        const char *mode = NULL;
        size_t start = 0;
        if (ntok > 0) {
            char *mode_tok = (char *)tokens[0];
            const size_t len = mode_tok != NULL ? strlen(mode_tok) : 0;
            if (len > 0 && mode_tok[len - 1] == ':') {
                mode_tok[len - 1] = '\0';
                if (!mode_is_default(mode_tok)) {
                    mode = mode_tok;
                }
                start = 1;
            }
        }
        if (start >= ntok) {
            continue;
        }

        struct key_step {
            enum fbwl_keybinding_key_kind kind;
            uint32_t keycode;
            xkb_keysym_t sym;
            uint32_t modifiers;
        };

        struct key_step steps[16];
        size_t nsteps = 0;
        uint32_t mods = 0;
        bool ok = true;
        bool change_workspace = false;

        for (size_t i = start; i < ntok; i++) {
            const char *t = tokens[i];
            if (t == NULL || *t == '\0') {
                continue;
            }
            if (strncasecmp(t, "on", 2) == 0) {
                continue;
            }
            if (strncasecmp(t, "mouse", 5) == 0 || strncasecmp(t, "button", 6) == 0 ||
                    strncasecmp(t, "click", 5) == 0 || strncasecmp(t, "move", 4) == 0) {
                ok = false;
                break;
            }
            if (parse_keys_modifier(t, &mods)) {
                continue;
            }

            enum fbwl_keybinding_key_kind key_kind = FBWL_KEYBIND_KEYSYM;
            uint32_t keycode = 0;
            xkb_keysym_t sym = XKB_KEY_NoSymbol;

            if (strcasecmp(t, "arg") == 0) {
                key_kind = FBWL_KEYBIND_PLACEHOLDER;
            } else if (strcasecmp(t, "changeworkspace") == 0) {
                key_kind = FBWL_KEYBIND_CHANGE_WORKSPACE;
                mods = 0;
                change_workspace = true;
            } else {
                sym = xkb_keysym_from_name(t, XKB_KEYSYM_CASE_INSENSITIVE);
                if (sym != XKB_KEY_NoSymbol) {
                    key_kind = FBWL_KEYBIND_KEYSYM;
                } else {
                    bool is_keycode = true;
                    for (const char *p = t; *p != '\0'; p++) {
                        if (!isdigit((unsigned char)*p)) {
                            is_keycode = false;
                            break;
                        }
                    }
                    if (!is_keycode) {
                        ok = false;
                        break;
                    }

                    char *end = NULL;
                    long code = strtol(t, &end, 10);
                    if (end == t || *end != '\0' || code < 0 || code > 100000) {
                        ok = false;
                        break;
                    }
                    key_kind = FBWL_KEYBIND_KEYCODE;
                    keycode = (uint32_t)code;
                }
            }

            if (nsteps >= (sizeof(steps) / sizeof(steps[0]))) {
                ok = false;
                break;
            }
            steps[nsteps++] = (struct key_step){
                .kind = key_kind,
                .keycode = keycode,
                .sym = sym,
                .modifiers = mods,
            };
            mods = 0;
        }
        if (!ok || nsteps == 0 || mods != 0) {
            continue;
        }
        if (change_workspace && nsteps != 1) {
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

        if (nsteps == 1) {
            if (add_binding(userdata, steps[0].kind, steps[0].keycode, steps[0].sym, steps[0].modifiers,
                    action, action_arg, action_cmd, mode)) {
                added++;
            }
        } else if (!change_workspace) {
            const uint32_t chain_id = next_chain_id++;
            char chain_modes[16][64];
            for (size_t i = 0; i + 1 < nsteps && i < (sizeof(chain_modes) / sizeof(chain_modes[0])); i++) {
                (void)snprintf(chain_modes[i], sizeof(chain_modes[i]), "__fbwl_chain__%u_%zu", chain_id, i + 1);
            }

            bool chain_ok = true;
            for (size_t i = 0; i + 1 < nsteps; i++) {
                const char *binding_mode = (i == 0) ? mode : chain_modes[i - 1];
                const char *next_mode = chain_modes[i];
                if (!add_binding(userdata, steps[i].kind, steps[i].keycode, steps[i].sym, steps[i].modifiers,
                        FBWL_KEYBIND_KEYMODE, 0, next_mode, binding_mode)) {
                    chain_ok = false;
                } else {
                    added++;
                }
            }
            if (chain_ok) {
                const char *leaf_mode = chain_modes[nsteps - 2];
                if (add_binding(userdata, steps[nsteps - 1].kind, steps[nsteps - 1].keycode, steps[nsteps - 1].sym,
                        steps[nsteps - 1].modifiers, action, action_arg, action_cmd, leaf_mode)) {
                    added++;
                }
            }
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
    if (strcasecmp(token, "onslit") == 0) {
        return FBWL_MOUSEBIND_SLIT;
    }
    if (strcasecmp(token, "onwindow") == 0) {
        return FBWL_MOUSEBIND_WINDOW;
    }
    if (strcasecmp(token, "ontitlebar") == 0) {
        return FBWL_MOUSEBIND_TITLEBAR;
    }
    if (strcasecmp(token, "ontab") == 0) {
        return FBWL_MOUSEBIND_TAB;
    }
    if (strcasecmp(token, "ontoolbar") == 0) {
        return FBWL_MOUSEBIND_TOOLBAR;
    }
    if (strcasecmp(token, "onwindowborder") == 0) {
        return FBWL_MOUSEBIND_WINDOW_BORDER;
    }
    if (strcasecmp(token, "onleftgrip") == 0) {
        return FBWL_MOUSEBIND_LEFT_GRIP;
    }
    if (strcasecmp(token, "onrightgrip") == 0) {
        return FBWL_MOUSEBIND_RIGHT_GRIP;
    }
    return FBWL_MOUSEBIND_ANY;
}

static bool parse_mouse_event_and_button(const char *token, enum fbwl_mousebinding_event_kind *out_kind, int *out_button) {
    if (out_kind != NULL) {
        *out_kind = FBWL_MOUSEBIND_EVENT_PRESS;
    }
    if (out_button != NULL) {
        *out_button = 0;
    }
    if (token == NULL) {
        return false;
    }
    enum fbwl_mousebinding_event_kind kind = FBWL_MOUSEBIND_EVENT_PRESS;
    if (starts_with_token(token, "mouse") || starts_with_token(token, "button")) {
        kind = FBWL_MOUSEBIND_EVENT_PRESS;
    } else if (starts_with_token(token, "click")) {
        kind = FBWL_MOUSEBIND_EVENT_CLICK;
    } else if (starts_with_token(token, "move")) {
        kind = FBWL_MOUSEBIND_EVENT_MOVE;
    } else {
        return false;
    }

    const char *p = token;
    while (*p != '\0' && !isdigit((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        return false;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) {
        return false;
    }
    if (v < 1 || v > 32) {
        return false;
    }

    if (out_kind != NULL) {
        *out_kind = kind;
    }
    if (out_button != NULL) {
        *out_button = (int)v;
    }
    return true;
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

        char *colon = find_command_colon(s);
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

        const char *mode = NULL;
        size_t start = 0;
        if (ntok > 0) {
            char *mode_tok = (char *)tokens[0];
            const size_t len = mode_tok != NULL ? strlen(mode_tok) : 0;
            if (len > 0 && mode_tok[len - 1] == ':') {
                mode_tok[len - 1] = '\0';
                if (!mode_is_default(mode_tok)) {
                    mode = mode_tok;
                }
                start = 1;
            }
        }
        if (start >= ntok) {
            continue;
        }

        enum fbwl_mousebinding_context context = FBWL_MOUSEBIND_ANY;
        enum fbwl_mousebinding_event_kind event_kind = FBWL_MOUSEBIND_EVENT_PRESS;
        int button = 0;
        uint32_t mods = 0;
        bool is_double = false;

        bool ok = true;
        for (size_t i = 0; i < ntok; i++) {
            if (i < start) {
                continue;
            }
            const char *t = tokens[i];
            if (t == NULL || *t == '\0') {
                continue;
            }
            enum fbwl_mousebinding_context c = parse_mouse_context(t);
            if (c != FBWL_MOUSEBIND_ANY) {
                context = c;
                continue;
            }
            enum fbwl_mousebinding_event_kind k = FBWL_MOUSEBIND_EVENT_PRESS;
            int b = 0;
            if (parse_mouse_event_and_button(t, &k, &b)) {
                button = b;
                event_kind = k;
                continue;
            }
            if (strcasecmp(t, "double") == 0) {
                is_double = true;
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
        if (is_double && event_kind != FBWL_MOUSEBIND_EVENT_PRESS) {
            is_double = false;
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

        if (add_binding(userdata, context, event_kind, button, mods, is_double, action, action_arg, action_cmd, mode)) {
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
