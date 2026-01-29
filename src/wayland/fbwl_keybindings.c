#include "wayland/fbwl_keybindings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <linux/input-event-codes.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/edges.h>

#include "wayland/fbwl_fluxbox_cmd.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_view.h"

#define FBWL_KEYMOD_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | \
    WLR_MODIFIER_MOD2 | WLR_MODIFIER_MOD3 | WLR_MODIFIER_MOD5)

struct fbwl_cycle_pattern {
    bool workspace_set;
    bool workspace_current;
    int workspace0;
    bool workspace_negate;

    bool minimized_set;
    bool minimized;
    bool minimized_negate;

    bool maximized_set;
    bool maximized;
    bool maximized_negate;

    bool fullscreen_set;
    bool fullscreen;
    bool fullscreen_negate;

    bool stuck_set;
    bool stuck;
    bool stuck_negate;

    bool title_set;
    bool title_negate;
    char *title;

    bool class_set;
    bool class_negate;
    char *class;
};

void fbwl_keybindings_free(struct fbwl_keybinding **bindings, size_t *count) {
    if (bindings == NULL || *bindings == NULL || count == NULL) {
        return;
    }
    for (size_t i = 0; i < *count; i++) {
        free((*bindings)[i].cmd);
        (*bindings)[i].cmd = NULL;
        free((*bindings)[i].mode);
        (*bindings)[i].mode = NULL;
    }
    free(*bindings);
    *bindings = NULL;
    *count = 0;
}

bool fbwl_keybindings_add(struct fbwl_keybinding **bindings, size_t *count, xkb_keysym_t sym, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
    if (bindings == NULL || count == NULL) {
        return false;
    }

    struct fbwl_keybinding *tmp = realloc(*bindings, (*count + 1) * sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    *bindings = tmp;

    struct fbwl_keybinding *binding = &(*bindings)[(*count)++];
    binding->key_kind = FBWL_KEYBIND_KEYSYM;
    binding->sym = xkb_keysym_to_lower(sym);
    binding->keycode = 0;
    binding->modifiers = modifiers & FBWL_KEYMOD_MASK;
    binding->action = action;
    binding->arg = arg;
    binding->cmd = cmd != NULL ? strdup(cmd) : NULL;
    binding->mode = mode != NULL ? strdup(mode) : NULL;
    return true;
}

bool fbwl_keybindings_add_keycode(struct fbwl_keybinding **bindings, size_t *count, uint32_t keycode, uint32_t modifiers,
        enum fbwl_keybinding_action action, int arg, const char *cmd, const char *mode) {
    if (bindings == NULL || count == NULL) {
        return false;
    }

    struct fbwl_keybinding *tmp = realloc(*bindings, (*count + 1) * sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    *bindings = tmp;

    struct fbwl_keybinding *binding = &(*bindings)[(*count)++];
    binding->key_kind = FBWL_KEYBIND_KEYCODE;
    binding->sym = XKB_KEY_NoSymbol;
    binding->keycode = keycode;
    binding->modifiers = modifiers & FBWL_KEYMOD_MASK;
    binding->action = action;
    binding->arg = arg;
    binding->cmd = cmd != NULL ? strdup(cmd) : NULL;
    binding->mode = mode != NULL ? strdup(mode) : NULL;
    return true;
}

void fbwl_keybindings_add_defaults(struct fbwl_keybinding **bindings, size_t *count, const char *terminal_cmd) {
    fbwl_keybindings_add(bindings, count, XKB_KEY_Escape, WLR_MODIFIER_ALT, FBWL_KEYBIND_EXIT, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_Return, WLR_MODIFIER_ALT, FBWL_KEYBIND_EXEC, 0, terminal_cmd, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_F2, WLR_MODIFIER_ALT, FBWL_KEYBIND_COMMAND_DIALOG, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_F1, WLR_MODIFIER_ALT, FBWL_KEYBIND_FOCUS_NEXT, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_m, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_MAXIMIZE, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_f, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_FULLSCREEN, 0, NULL, NULL);
    fbwl_keybindings_add(bindings, count, XKB_KEY_i, WLR_MODIFIER_ALT, FBWL_KEYBIND_TOGGLE_MINIMIZE, 0, NULL, NULL);
    for (int i = 0; i < 9; i++) {
        fbwl_keybindings_add(bindings, count, XKB_KEY_1 + i, WLR_MODIFIER_ALT, FBWL_KEYBIND_WORKSPACE_SWITCH, i, NULL, NULL);
        fbwl_keybindings_add(bindings, count, XKB_KEY_1 + i, WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL,
            FBWL_KEYBIND_SEND_TO_WORKSPACE, i, NULL, NULL);
    }
}

static int wrap_workspace(int ws, int count) {
    if (count < 1) {
        return 0;
    }
    while (ws < 0) {
        ws += count;
    }
    while (ws >= count) {
        ws -= count;
    }
    return ws;
}

static bool mode_is_default(const char *mode) {
    return mode == NULL || *mode == '\0' || strcasecmp(mode, "default") == 0;
}

static bool mode_matches(const char *binding_mode, const char *current_mode) {
    const bool binding_default = mode_is_default(binding_mode);
    const bool current_default = mode_is_default(current_mode);
    if (binding_default && current_default) {
        return true;
    }
    if (binding_default || current_default) {
        return false;
    }
    return strcmp(binding_mode, current_mode) == 0;
}

static struct fbwl_view *resolve_target_view(struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks) {
    if (target_view != NULL) {
        return target_view;
    }
    if (hooks == NULL || hooks->wm == NULL || hooks->wm->focused == NULL) {
        return NULL;
    }
    return hooks->wm->focused->userdata;
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

static bool parse_yes_no(const char *s, bool *out) {
    if (s == NULL || out == NULL) {
        return false;
    }

    if (strcasecmp(s, "yes") == 0 || strcasecmp(s, "true") == 0 ||
            strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(s, "no") == 0 || strcasecmp(s, "false") == 0 ||
            strcasecmp(s, "off") == 0 || strcmp(s, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static void parse_cycle_options_inplace(char *s, bool *out_groups, bool *out_static, char **out_pattern) {
    if (out_groups != NULL) {
        *out_groups = false;
    }
    if (out_static != NULL) {
        *out_static = false;
    }
    if (out_pattern != NULL) {
        *out_pattern = s;
    }
    if (s == NULL) {
        return;
    }

    char *open = strchr(s, '{');
    if (open == NULL) {
        if (out_pattern != NULL) {
            *out_pattern = trim_inplace(s);
        }
        return;
    }
    char *close = strchr(open + 1, '}');
    if (close == NULL) {
        if (out_pattern != NULL) {
            *out_pattern = trim_inplace(s);
        }
        return;
    }

    *open = '\0';
    *close = '\0';

    char *opts = trim_inplace(open + 1);
    if (opts != NULL && *opts != '\0') {
        char *save = NULL;
        for (char *tok = strtok_r(opts, " \t", &save); tok != NULL; tok = strtok_r(NULL, " \t", &save)) {
            if (out_groups != NULL && strcasecmp(tok, "groups") == 0) {
                *out_groups = true;
                continue;
            }
            if (out_static != NULL && strcasecmp(tok, "static") == 0) {
                *out_static = true;
                continue;
            }
        }
    }

    if (out_pattern != NULL) {
        *out_pattern = trim_inplace(close + 1);
    }
}

static void cycle_pattern_parse_term(struct fbwl_cycle_pattern *pat, char *term) {
    if (pat == NULL || term == NULL) {
        return;
    }

    char *s = trim_inplace(term);
    if (s == NULL || *s == '\0') {
        return;
    }

    bool negate = false;
    char *key = NULL;
    char *val = NULL;

    char *op = strstr(s, "!=");
    if (op != NULL) {
        negate = true;
        *op = '\0';
        key = trim_inplace(s);
        val = trim_inplace(op + 2);
    } else {
        op = strchr(s, '=');
        if (op == NULL) {
            return;
        }
        *op = '\0';
        key = trim_inplace(s);
        val = trim_inplace(op + 1);
    }

    if (key == NULL || *key == '\0' || val == NULL || *val == '\0') {
        return;
    }

    if (strcasecmp(key, "workspace") == 0) {
        pat->workspace_set = true;
        pat->workspace_negate = negate;
        if (strcasecmp(val, "[current]") == 0) {
            pat->workspace_current = true;
            pat->workspace0 = 0;
            return;
        }

        char *end = NULL;
        long ws = strtol(val, &end, 10);
        if (end == val || *end != '\0' || ws < -100000 || ws > 100000) {
            pat->workspace_set = false;
            pat->workspace_negate = false;
            return;
        }
        pat->workspace_current = false;
        pat->workspace0 = (int)ws;
        return;
    }

    if (strcasecmp(key, "minimized") == 0) {
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->minimized_set = true;
        pat->minimized_negate = negate;
        pat->minimized = v;
        return;
    }

    if (strcasecmp(key, "maximized") == 0) {
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->maximized_set = true;
        pat->maximized_negate = negate;
        pat->maximized = v;
        return;
    }

    if (strcasecmp(key, "fullscreen") == 0) {
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->fullscreen_set = true;
        pat->fullscreen_negate = negate;
        pat->fullscreen = v;
        return;
    }

    if (strcasecmp(key, "stuck") == 0 || strcasecmp(key, "sticky") == 0) {
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->stuck_set = true;
        pat->stuck_negate = negate;
        pat->stuck = v;
        return;
    }

    if (strcasecmp(key, "title") == 0) {
        pat->title_set = true;
        pat->title_negate = negate;
        pat->title = val;
        return;
    }

    if (strcasecmp(key, "class") == 0 || strcasecmp(key, "app_id") == 0 || strcasecmp(key, "appid") == 0) {
        pat->class_set = true;
        pat->class_negate = negate;
        pat->class = val;
        return;
    }
}

static void cycle_pattern_parse_inplace(struct fbwl_cycle_pattern *pat, char *pattern) {
    if (pat == NULL) {
        return;
    }

    memset(pat, 0, sizeof(*pat));
    if (pattern == NULL || *pattern == '\0') {
        return;
    }

    for (char *p = pattern; p != NULL && *p != '\0';) {
        char *open = strchr(p, '(');
        if (open == NULL) {
            break;
        }
        char *close = strchr(open + 1, ')');
        if (close == NULL) {
            break;
        }

        *close = '\0';
        char *inside = trim_inplace(open + 1);
        if (inside != NULL && *inside != '\0') {
            char *save = NULL;
            for (char *tok = strtok_r(inside, " \t", &save); tok != NULL; tok = strtok_r(NULL, " \t", &save)) {
                cycle_pattern_parse_term(pat, tok);
            }
        }

        p = close + 1;
    }
}

static bool contains_substr(const char *haystack, const char *needle) {
    if (needle == NULL || *needle == '\0') {
        return true;
    }
    if (haystack == NULL) {
        return false;
    }
    return strstr(haystack, needle) != NULL;
}

static bool cycle_pattern_matches(const struct fbwl_cycle_pattern *pat, const struct fbwl_view *view,
        const struct fbwl_keybindings_hooks *hooks) {
    if (pat == NULL || view == NULL || hooks == NULL || hooks->wm == NULL) {
        return false;
    }

    if (pat->workspace_set) {
        bool ok = false;
        if (view->wm_view.sticky) {
            ok = true;
        } else if (pat->workspace_current) {
            ok = view->wm_view.workspace == fbwm_core_workspace_current(hooks->wm);
        } else {
            ok = view->wm_view.workspace == pat->workspace0;
        }
        if (pat->workspace_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->minimized_set) {
        bool ok = view->minimized == pat->minimized;
        if (pat->minimized_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->maximized_set) {
        bool ok = view->maximized == pat->maximized;
        if (pat->maximized_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->fullscreen_set) {
        bool ok = view->fullscreen == pat->fullscreen;
        if (pat->fullscreen_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->stuck_set) {
        bool ok = view->wm_view.sticky == pat->stuck;
        if (pat->stuck_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->title_set) {
        bool ok = contains_substr(fbwl_view_title(view), pat->title);
        if (pat->title_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->class_set) {
        bool ok = contains_substr(fbwl_view_app_id(view), pat->class);
        if (pat->class_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    return true;
}

static struct fbwl_view *pick_cycle_candidate(const struct fbwl_keybindings_hooks *hooks, bool reverse,
        bool groups, const struct fbwl_cycle_pattern *pat) {
    if (hooks == NULL || hooks->wm == NULL || pat == NULL) {
        return NULL;
    }

    const struct fbwm_view *focused = hooks->wm->focused;
    if (reverse) {
        for (struct fbwm_view *wm_view = hooks->wm->views.next; wm_view != &hooks->wm->views; wm_view = wm_view->next) {
            if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
                continue;
            }
            struct fbwl_view *view = wm_view->userdata;
            if (view == NULL) {
                continue;
            }
            if (groups && !fbwl_tabs_view_is_active(view)) {
                continue;
            }
            if (focused != NULL && wm_view == focused) {
                continue;
            }
            if (!cycle_pattern_matches(pat, view, hooks)) {
                continue;
            }
            return view;
        }
        return NULL;
    }

    for (struct fbwm_view *wm_view = hooks->wm->views.prev; wm_view != &hooks->wm->views; wm_view = wm_view->prev) {
        if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
            continue;
        }
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL) {
            continue;
        }
        if (groups && !fbwl_tabs_view_is_active(view)) {
            continue;
        }
        if (!cycle_pattern_matches(pat, view, hooks)) {
            continue;
        }
        return view;
    }

    return NULL;
}

static uint32_t resize_edges_from_arg(const struct fbwl_view *view, int cursor_x, int cursor_y, const char *arg) {
    if (arg != NULL) {
        if (strcasecmp(arg, "topleft") == 0) {
            return WLR_EDGE_TOP | WLR_EDGE_LEFT;
        }
        if (strcasecmp(arg, "topright") == 0) {
            return WLR_EDGE_TOP | WLR_EDGE_RIGHT;
        }
        if (strcasecmp(arg, "bottomleft") == 0) {
            return WLR_EDGE_BOTTOM | WLR_EDGE_LEFT;
        }
        if (strcasecmp(arg, "bottomright") == 0) {
            return WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
        }
        if (strcasecmp(arg, "nearestcorner") == 0) {
            if (view != NULL) {
                const int w = fbwl_view_current_width(view);
                const int h = fbwl_view_current_height(view);
                const int mid_x = view->x + w / 2;
                const int mid_y = view->y + h / 2;
                const bool left = cursor_x < mid_x;
                const bool top = cursor_y < mid_y;
                return (left ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT) | (top ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM);
            }
        }
    }
    return WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
}

static bool execute_action_depth(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth);

static bool execute_macro_depth(const char *macro, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (macro == NULL || hooks == NULL) {
        return false;
    }
    if (depth > 8) {
        return false;
    }

    bool any = false;
    const char *p = macro;
    while (*p != '\0') {
        while (*p != '\0' && *p != '{') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char *open = p;
        const char *close = strchr(open + 1, '}');
        if (close == NULL) {
            break;
        }

        size_t len = (size_t)(close - (open + 1));
        char *chunk = strndup(open + 1, len);
        if (chunk != NULL) {
            char *s = chunk;
            while (*s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) {
                s++;
            }

            char *end = s + strlen(s);
            while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
                end[-1] = '\0';
                end--;
            }

            if (*s != '\0') {
                char *name = s;
                while (*s != '\0' && *s != ' ' && *s != '\t') {
                    s++;
                }
                char *args = s;
                if (*s != '\0') {
                    *s = '\0';
                    args = s + 1;
                }
                while (*args != '\0' && (*args == ' ' || *args == '\t')) {
                    args++;
                }

                enum fbwl_keybinding_action sub_action;
                int sub_arg = 0;
                const char *sub_cmd = NULL;
                if (fbwl_fluxbox_cmd_resolve(name, args, &sub_action, &sub_arg, &sub_cmd)) {
                    if (execute_action_depth(sub_action, sub_arg, sub_cmd, target_view, hooks, depth + 1)) {
                        any = true;
                    }
                }
            }
            free(chunk);
        }

        p = close + 1;
    }

    return any;
}

static bool execute_action_depth(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (hooks == NULL || hooks->wm == NULL) {
        return false;
    }

    struct fbwl_view *view = resolve_target_view(target_view, hooks);

    switch (action) {
    case FBWL_KEYBIND_EXIT:
        if (hooks->terminate == NULL) {
            return false;
        }
        hooks->terminate(hooks->userdata);
        return true;
    case FBWL_KEYBIND_EXEC:
        if (hooks->spawn == NULL) {
            return false;
        }
        hooks->spawn(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_COMMAND_DIALOG:
        if (hooks->command_dialog_open == NULL) {
            return false;
        }
        hooks->command_dialog_open(hooks->userdata);
        return true;
    case FBWL_KEYBIND_RECONFIGURE:
        if (hooks->reconfigure == NULL) {
            return false;
        }
        hooks->reconfigure(hooks->userdata);
        return true;
    case FBWL_KEYBIND_KEYMODE:
        if (hooks->key_mode_set == NULL) {
            return false;
        }
        hooks->key_mode_set(hooks->userdata, cmd);
        return true;
    case FBWL_KEYBIND_FOCUS_NEXT: {
        if (cmd == NULL || *cmd == '\0') {
            fbwm_core_focus_next(hooks->wm);
            return true;
        }
        char *tmp = strdup(cmd);
        if (tmp == NULL) {
            fbwm_core_focus_next(hooks->wm);
            return true;
        }
        bool groups = false;
        bool static_order = false;
        char *pattern = NULL;
        parse_cycle_options_inplace(tmp, &groups, &static_order, &pattern);
        (void)static_order;
        struct fbwl_cycle_pattern pat = {0};
        cycle_pattern_parse_inplace(&pat, pattern);
        struct fbwl_view *candidate = pick_cycle_candidate(hooks, false, groups, &pat);
        if (candidate != NULL && hooks->wm->focused != &candidate->wm_view) {
            if (!groups && candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                fbwl_tabs_activate(candidate, "keybinding-nextwindow");
            }
            fbwm_core_focus_view(hooks->wm, &candidate->wm_view);
        }
        free(tmp);
        return true;
    }
    case FBWL_KEYBIND_FOCUS_PREV: {
        if (cmd == NULL || *cmd == '\0') {
            fbwm_core_focus_prev(hooks->wm);
            return true;
        }
        char *tmp = strdup(cmd);
        if (tmp == NULL) {
            fbwm_core_focus_prev(hooks->wm);
            return true;
        }
        bool groups = false;
        bool static_order = false;
        char *pattern = NULL;
        parse_cycle_options_inplace(tmp, &groups, &static_order, &pattern);
        (void)static_order;
        struct fbwl_cycle_pattern pat = {0};
        cycle_pattern_parse_inplace(&pat, pattern);
        struct fbwl_view *candidate = pick_cycle_candidate(hooks, true, groups, &pat);
        if (candidate != NULL) {
            if (!groups && candidate->tab_group != NULL && !fbwl_tabs_view_is_active(candidate)) {
                fbwl_tabs_activate(candidate, "keybinding-prevwindow");
            }
            fbwm_core_focus_view(hooks->wm, &candidate->wm_view);
        }
        free(tmp);
        return true;
    }
    case FBWL_KEYBIND_TAB_NEXT: {
        if (view == NULL || view->tab_group == NULL) {
            return true;
        }
        struct fbwl_view *next = fbwl_tabs_pick_next(view);
        if (next == NULL) {
            return true;
        }
        fbwl_tabs_activate(next, "keybinding-nexttab");
        fbwm_core_focus_view(hooks->wm, &next->wm_view);
        return true;
    }
    case FBWL_KEYBIND_TAB_PREV: {
        if (view == NULL || view->tab_group == NULL) {
            return true;
        }
        struct fbwl_view *prev = fbwl_tabs_pick_prev(view);
        if (prev == NULL) {
            return true;
        }
        fbwl_tabs_activate(prev, "keybinding-prevtab");
        fbwm_core_focus_view(hooks->wm, &prev->wm_view);
        return true;
    }
    case FBWL_KEYBIND_TAB_GOTO: {
        if (view == NULL || view->tab_group == NULL) {
            return true;
        }
        struct fbwl_view *pick = fbwl_tabs_pick_index0(view, arg);
        if (pick == NULL) {
            return true;
        }
        fbwl_tabs_activate(pick, "keybinding-tab");
        fbwm_core_focus_view(hooks->wm, &pick->wm_view);
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_MAXIMIZE: {
        if (hooks->view_set_maximized == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_set_maximized(hooks->userdata, view, !view->maximized);
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_FULLSCREEN: {
        if (hooks->view_set_fullscreen == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_set_fullscreen(hooks->userdata, view, !view->fullscreen);
        }
        return true;
    }
    case FBWL_KEYBIND_TOGGLE_MINIMIZE: {
        if (hooks->view_set_minimized == NULL) {
            return false;
        }
        struct fbwl_view *min_view = view;
        if (view == NULL) {
            const int cur = fbwm_core_workspace_current(hooks->wm);
            for (struct fbwm_view *walk = hooks->wm->views.next; walk != &hooks->wm->views; walk = walk->next) {
                struct fbwl_view *candidate = walk->userdata;
                if (candidate != NULL && candidate->mapped && candidate->minimized &&
                        (walk->sticky || walk->workspace == cur)) {
                    min_view = candidate;
                    break;
                }
            }
        }

        if (min_view != NULL) {
            hooks->view_set_minimized(hooks->userdata, min_view, !min_view->minimized, "keybinding");
        }
        return true;
    }
    case FBWL_KEYBIND_WORKSPACE_SWITCH:
        fbwm_core_workspace_switch(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch");
        }
        return true;
    case FBWL_KEYBIND_WORKSPACE_NEXT: {
        const int cur = fbwm_core_workspace_current(hooks->wm);
        const int count = fbwm_core_workspace_count(hooks->wm);
        fbwm_core_workspace_switch(hooks->wm, wrap_workspace(cur + 1, count));
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch-next");
        }
        return true;
    }
    case FBWL_KEYBIND_WORKSPACE_PREV: {
        const int cur = fbwm_core_workspace_current(hooks->wm);
        const int count = fbwm_core_workspace_count(hooks->wm);
        fbwm_core_workspace_switch(hooks->wm, wrap_workspace(cur - 1, count));
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch-prev");
        }
        return true;
    }
    case FBWL_KEYBIND_SEND_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        return true;
    case FBWL_KEYBIND_TAKE_TO_WORKSPACE:
        fbwm_core_move_focused_to_workspace(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        fbwm_core_workspace_switch(hooks->wm, arg);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch");
        }
        return true;
    case FBWL_KEYBIND_SEND_TO_REL_WORKSPACE: {
        const int cur = fbwm_core_workspace_current(hooks->wm);
        const int count = fbwm_core_workspace_count(hooks->wm);
        fbwm_core_move_focused_to_workspace(hooks->wm, wrap_workspace(cur + arg, count));
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        return true;
    }
    case FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE: {
        const int cur = fbwm_core_workspace_current(hooks->wm);
        const int count = fbwm_core_workspace_count(hooks->wm);
        const int ws = wrap_workspace(cur + arg, count);
        fbwm_core_move_focused_to_workspace(hooks->wm, ws);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "move-focused");
        }
        fbwm_core_workspace_switch(hooks->wm, ws);
        if (hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "switch");
        }
        return true;
    }
    case FBWL_KEYBIND_CLOSE:
    case FBWL_KEYBIND_KILL:
        if (hooks->view_close == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_close(hooks->userdata, view, action == FBWL_KEYBIND_KILL);
        }
        return true;
    case FBWL_KEYBIND_WINDOW_MENU:
        if (hooks->menu_open_window == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->menu_open_window(hooks->userdata, view, hooks->cursor_x, hooks->cursor_y);
        }
        return true;
    case FBWL_KEYBIND_ROOT_MENU:
        if (hooks->menu_open_root == NULL) {
            return false;
        }
        hooks->menu_open_root(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_WORKSPACE_MENU:
        if (hooks->menu_open_workspace == NULL) {
            return false;
        }
        hooks->menu_open_workspace(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
        return true;
    case FBWL_KEYBIND_HIDE_MENUS:
        if (hooks->menu_close == NULL) {
            return false;
        }
        hooks->menu_close(hooks->userdata, "binding");
        return true;
    case FBWL_KEYBIND_RAISE:
        if (hooks->view_raise == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_raise(hooks->userdata, view, "binding");
        }
        return true;
    case FBWL_KEYBIND_LOWER:
        if (hooks->view_lower == NULL) {
            return false;
        }
        if (view != NULL) {
            hooks->view_lower(hooks->userdata, view, "binding");
        }
        return true;
    case FBWL_KEYBIND_FOCUS:
        if (view != NULL) {
            fbwm_core_focus_view(hooks->wm, &view->wm_view);
        }
        return true;
    case FBWL_KEYBIND_START_MOVING:
        if (hooks->grab_begin_move == NULL) {
            return false;
        }
        if (view != NULL) {
            fbwm_core_focus_view(hooks->wm, &view->wm_view);
            if (hooks->view_raise != NULL) {
                hooks->view_raise(hooks->userdata, view, "move");
            }
            uint32_t button = hooks->button != 0 ? hooks->button : (uint32_t)BTN_LEFT;
            hooks->grab_begin_move(hooks->userdata, view, button);
        }
        return true;
    case FBWL_KEYBIND_START_RESIZING:
        if (hooks->grab_begin_resize == NULL) {
            return false;
        }
        if (view != NULL) {
            fbwm_core_focus_view(hooks->wm, &view->wm_view);
            if (hooks->view_raise != NULL) {
                hooks->view_raise(hooks->userdata, view, "resize");
            }
            const uint32_t edges = resize_edges_from_arg(view, hooks->cursor_x, hooks->cursor_y, cmd);
            uint32_t button = hooks->button != 0 ? hooks->button : (uint32_t)BTN_RIGHT;
            hooks->grab_begin_resize(hooks->userdata, view, button, edges);
        }
        return true;
    case FBWL_KEYBIND_MACRO:
        return execute_macro_depth(cmd, view, hooks, depth);
    default:
        return false;
    }
}

bool fbwl_keybindings_execute_action(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks) {
    return execute_action_depth(action, arg, cmd, target_view, hooks, 0);
}

bool fbwl_keybindings_handle(const struct fbwl_keybinding *bindings, size_t count, uint32_t keycode, xkb_keysym_t sym,
        uint32_t modifiers, const struct fbwl_keybindings_hooks *hooks) {
    if (bindings == NULL || count == 0 || hooks == NULL) {
        return false;
    }

    sym = xkb_keysym_to_lower(sym);
    modifiers &= FBWL_KEYMOD_MASK;
    for (size_t i = count; i-- > 0;) {
        const struct fbwl_keybinding *binding = &bindings[i];
        if (!mode_matches(binding->mode, hooks->key_mode)) {
            continue;
        }
        if (binding->modifiers != modifiers) {
            continue;
        }
        if (binding->key_kind == FBWL_KEYBIND_KEYCODE) {
            if (binding->keycode == keycode) {
                return fbwl_keybindings_execute_action(binding->action, binding->arg, binding->cmd, NULL, hooks);
            }
            continue;
        }
        if (binding->sym == sym) {
            return fbwl_keybindings_execute_action(binding->action, binding->arg, binding->cmd, NULL, hooks);
        }
    }
    return false;
}
