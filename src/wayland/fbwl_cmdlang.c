#include "wayland/fbwl_cmdlang.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "wayland/fbwl_fluxbox_cmd.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_ui_toolbar_iconbar_pattern.h"
#include "wayland/fbwl_view.h"
#include "wmcore/fbwm_core.h"

enum { CMDLANG_MAX_DEPTH = 8 };

struct str_vec {
    char **items;
    size_t len;
    size_t cap;
};

struct view_vec {
    struct fbwl_view **items;
    size_t len;
    size_t cap;
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
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static bool rest_empty(const char *s) {
    if (s == NULL) {
        return true;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return *s == '\0';
}

static bool str_vec_push(struct str_vec *vec, char *s) {
    if (vec == NULL || s == NULL) {
        return false;
    }
    if (vec->len >= vec->cap) {
        const size_t new_cap = vec->cap > 0 ? vec->cap * 2 : 8;
        char **tmp = realloc(vec->items, new_cap * sizeof(*tmp));
        if (tmp == NULL) {
            return false;
        }
        vec->items = tmp;
        vec->cap = new_cap;
    }
    vec->items[vec->len++] = s;
    return true;
}

static void str_vec_free(struct str_vec *vec) {
    if (vec == NULL) {
        return;
    }
    for (size_t i = 0; i < vec->len; i++) {
        free(vec->items[i]);
    }
    free(vec->items);
    *vec = (struct str_vec){0};
}

static bool view_vec_push(struct view_vec *vec, struct fbwl_view *view) {
    if (vec == NULL || view == NULL) {
        return false;
    }
    if (vec->len >= vec->cap) {
        const size_t new_cap = vec->cap > 0 ? vec->cap * 2 : 16;
        struct fbwl_view **tmp = realloc(vec->items, new_cap * sizeof(*tmp));
        if (tmp == NULL) {
            return false;
        }
        vec->items = tmp;
        vec->cap = new_cap;
    }
    vec->items[vec->len++] = view;
    return true;
}

static void view_vec_free(struct view_vec *vec) {
    if (vec == NULL) {
        return;
    }
    free(vec->items);
    *vec = (struct view_vec){0};
}

static int view_create_seq_cmp(const void *a, const void *b) {
    const struct fbwl_view *av = *(const struct fbwl_view *const *)a;
    const struct fbwl_view *bv = *(const struct fbwl_view *const *)b;
    if (av == NULL || bv == NULL) {
        return 0;
    }
    if (av->create_seq < bv->create_seq) {
        return -1;
    }
    if (av->create_seq > bv->create_seq) {
        return 1;
    }
    if (av < bv) {
        return -1;
    }
    if (av > bv) {
        return 1;
    }
    return 0;
}

static int cmdlang_get_string_between(const char *instr, char first, char last,
        const char *ok_chars, bool allow_nesting, char **out_token) {
    if (instr == NULL) {
        return 0;
    }
    if (ok_chars == NULL) {
        ok_chars = " \t\n";
    }

    const char *p = instr;
    while (*p != '\0' && strchr(ok_chars, *p) != NULL) {
        p++;
    }
    if (*p == '\0' || *p != first) {
        return 0;
    }

    const char *open = p;
    int nesting = 0;
    for (const char *q = open + 1; *q != '\0'; q++) {
        if (allow_nesting && *q == first && q[-1] != '\\') {
            nesting++;
            continue;
        }
        if (*q == last && q[-1] != '\\') {
            if (allow_nesting && nesting > 0) {
                nesting--;
                continue;
            }
            if (out_token != NULL) {
                const size_t len = (size_t)(q - (open + 1));
                char *tok = strndup(open + 1, len);
                if (tok == NULL) {
                    return 0;
                }
                *out_token = tok;
            }
            return (int)((q - instr) + 1);
        }
    }
    return 0;
}

static bool cmdlang_tokens_between(struct str_vec *out, const char *in, char first, char last,
        const char *ok_chars, bool allow_nesting, const char **out_rest) {
    if (out == NULL) {
        return false;
    }
    *out = (struct str_vec){0};
    size_t pos = 0;
    while (in != NULL) {
        char *tok = NULL;
        const int n = cmdlang_get_string_between(in + pos, first, last, ok_chars, allow_nesting, &tok);
        if (n <= 0) {
            break;
        }
        if (tok == NULL || !str_vec_push(out, tok)) {
            free(tok);
            str_vec_free(out);
            return false;
        }
        pos += (size_t)n;
    }
    if (out_rest != NULL) {
        *out_rest = in != NULL ? in + pos : NULL;
    }
    return true;
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

static int current_workspace0(const struct fbwl_keybindings_hooks *hooks) {
    if (hooks == NULL || hooks->wm == NULL) {
        return 0;
    }
    if (hooks->workspace_current != NULL) {
        return hooks->workspace_current(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
    }
    return fbwm_core_workspace_current(hooks->wm);
}

static void build_pattern_env(struct fbwl_ui_toolbar_env *env, const struct fbwl_keybindings_hooks *hooks,
        const struct fbwl_view *view) {
    if (env == NULL) {
        return;
    }

    *env = (struct fbwl_ui_toolbar_env){0};

    if (hooks != NULL) {
        env->wm = hooks->wm;
        env->cursor_valid = true;
        env->cursor_x = (double)hooks->cursor_x;
        env->cursor_y = (double)hooks->cursor_y;
        if (hooks->wm != NULL && hooks->wm->focused != NULL) {
            env->focused_view = hooks->wm->focused->userdata;
        }
    }

    struct fbwl_server *server = view != NULL ? view->server : NULL;
    if (server != NULL) {
        env->wl_display = server->wl_display;
        env->output_layout = server->output_layout;
        env->outputs = &server->outputs;
        env->xwayland = server->xwayland;
        env->layer_background = server->layer_background;
        env->layer_bottom = server->layer_bottom;
        env->layer_normal = server->layer_normal;
        env->layer_fullscreen = server->layer_fullscreen;
        env->layer_top = server->layer_top;
        env->layer_overlay = server->layer_overlay;
        env->decor_theme = &server->decor_theme;
#ifdef HAVE_SYSTEMD
        env->sni = &server->sni;
#endif
    }
}

static bool cmdlang_eval_bool(const char *expr, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth);

static bool cmdlang_matches(const char *pattern, struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks) {
    if (pattern == NULL || hooks == NULL) {
        return false;
    }

    struct fbwl_view *view = resolve_target_view(target_view, hooks);
    if (view == NULL) {
        return false;
    }

    char *tmp = strdup(pattern);
    if (tmp == NULL) {
        return false;
    }

    struct fbwl_iconbar_pattern pat = {0};
    fbwl_iconbar_pattern_parse_inplace(&pat, tmp);

    struct fbwl_ui_toolbar_env env = {0};
    build_pattern_env(&env, hooks, view);
    const bool ok = fbwl_client_pattern_matches(&pat, &env, view, current_workspace0(hooks));

    fbwl_iconbar_pattern_free(&pat);
    free(tmp);
    return ok;
}

static bool cmdlang_some(const char *cond, const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (hooks == NULL || hooks->wm == NULL || cond == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }
    for (struct fbwm_view *walk = hooks->wm->views.next; walk != &hooks->wm->views; walk = walk->next) {
        struct fbwl_view *view = walk != NULL ? walk->userdata : NULL;
        if (view == NULL) {
            continue;
        }
        if (cmdlang_eval_bool(cond, view, hooks, depth + 1)) {
            return true;
        }
    }
    return false;
}

static bool cmdlang_every(const char *cond, const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (hooks == NULL || hooks->wm == NULL || cond == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }
    for (struct fbwm_view *walk = hooks->wm->views.next; walk != &hooks->wm->views; walk = walk->next) {
        struct fbwl_view *view = walk != NULL ? walk->userdata : NULL;
        if (view == NULL) {
            continue;
        }
        if (!cmdlang_eval_bool(cond, view, hooks, depth + 1)) {
            return false;
        }
    }
    return true;
}

static bool cmdlang_and(const char *args, struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (args == NULL || hooks == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    struct str_vec toks = {0};
    const char *rest = NULL;
    if (!cmdlang_tokens_between(&toks, args, '{', '}', " \t\n", true, &rest)) {
        return false;
    }
    if (toks.len == 0 || !rest_empty(rest)) {
        str_vec_free(&toks);
        return false;
    }

    for (size_t i = 0; i < toks.len; i++) {
        char *s = trim_inplace(toks.items[i]);
        if (s == NULL || *s == '\0') {
            str_vec_free(&toks);
            return false;
        }
        if (!cmdlang_eval_bool(s, target_view, hooks, depth + 1)) {
            str_vec_free(&toks);
            return false;
        }
    }
    str_vec_free(&toks);
    return true;
}

static bool cmdlang_or(const char *args, struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (args == NULL || hooks == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    struct str_vec toks = {0};
    const char *rest = NULL;
    if (!cmdlang_tokens_between(&toks, args, '{', '}', " \t\n", true, &rest)) {
        return false;
    }
    if (toks.len == 0 || !rest_empty(rest)) {
        str_vec_free(&toks);
        return false;
    }

    for (size_t i = 0; i < toks.len; i++) {
        char *s = trim_inplace(toks.items[i]);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (cmdlang_eval_bool(s, target_view, hooks, depth + 1)) {
            str_vec_free(&toks);
            return true;
        }
    }
    str_vec_free(&toks);
    return false;
}

static bool cmdlang_xor(const char *args, struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (args == NULL || hooks == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    struct str_vec toks = {0};
    const char *rest = NULL;
    if (!cmdlang_tokens_between(&toks, args, '{', '}', " \t\n", true, &rest)) {
        return false;
    }
    if (toks.len == 0 || !rest_empty(rest)) {
        str_vec_free(&toks);
        return false;
    }

    bool acc = false;
    for (size_t i = 0; i < toks.len; i++) {
        char *s = trim_inplace(toks.items[i]);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (cmdlang_eval_bool(s, target_view, hooks, depth + 1)) {
            acc = !acc;
        }
    }
    str_vec_free(&toks);
    return acc;
}

static bool cmdlang_eval_bool(const char *expr, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth) {
    if (expr == NULL || hooks == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    char *copy = strdup(expr);
    if (copy == NULL) {
        return false;
    }

    char *s = trim_inplace(copy);
    if (s == NULL || *s == '\0') {
        free(copy);
        return false;
    }

    char *sp = s;
    while (*sp != '\0' && !isspace((unsigned char)*sp)) {
        sp++;
    }
    char *args = sp;
    if (*sp != '\0') {
        *sp = '\0';
        args = sp + 1;
    }
    args = trim_inplace(args);

    bool ok = false;
    if (strcasecmp(s, "matches") == 0) {
        ok = cmdlang_matches(args, target_view, hooks);
    } else if (strcasecmp(s, "some") == 0) {
        ok = cmdlang_some(args, hooks, depth);
    } else if (strcasecmp(s, "every") == 0) {
        ok = cmdlang_every(args, hooks, depth);
    } else if (strcasecmp(s, "not") == 0) {
        ok = !cmdlang_eval_bool(args, target_view, hooks, depth + 1);
    } else if (strcasecmp(s, "and") == 0) {
        ok = cmdlang_and(args, target_view, hooks, depth);
    } else if (strcasecmp(s, "or") == 0) {
        ok = cmdlang_or(args, target_view, hooks, depth);
    } else if (strcasecmp(s, "xor") == 0) {
        ok = cmdlang_xor(args, target_view, hooks, depth);
    } else {
        ok = false;
    }

    free(copy);
    return ok;
}

bool fbwl_cmdlang_execute_line(const char *cmd_line, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action) {
    if (cmd_line == NULL || hooks == NULL || exec_action == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    char *copy = strdup(cmd_line);
    if (copy == NULL) {
        return false;
    }

    char *s = trim_inplace(copy);
    if (s == NULL || *s == '\0') {
        free(copy);
        return false;
    }

    char *sp = s;
    while (*sp != '\0' && !isspace((unsigned char)*sp)) {
        sp++;
    }
    char *cmd_args = sp;
    if (*sp != '\0') {
        *sp = '\0';
        cmd_args = sp + 1;
    }
    const char *cmd_name = s;
    cmd_args = trim_inplace(cmd_args);

    enum fbwl_keybinding_action action;
    int action_arg = 0;
    const char *action_cmd = NULL;
    if (!fbwl_fluxbox_cmd_resolve(cmd_name, cmd_args, &action, &action_arg, &action_cmd)) {
        free(copy);
        return false;
    }

    const bool ok = exec_action(action, action_arg, action_cmd, target_view, hooks, depth);
    free(copy);
    return ok;
}

bool fbwl_cmdlang_execute_macro(const char *macro_args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action) {
    if (macro_args == NULL || hooks == NULL || exec_action == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    struct str_vec toks = {0};
    const char *rest = NULL;
    if (!cmdlang_tokens_between(&toks, macro_args, '{', '}', " \t\n", true, &rest)) {
        return false;
    }
    if (toks.len == 0 || !rest_empty(rest)) {
        str_vec_free(&toks);
        return false;
    }

    bool any = false;
    for (size_t i = 0; i < toks.len; i++) {
        char *s = trim_inplace(toks.items[i]);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (fbwl_cmdlang_execute_line(s, target_view, hooks, depth + 1, exec_action)) {
            any = true;
        }
    }

    str_vec_free(&toks);
    return any;
}

static void parse_foreach_options_inplace(char *s, bool *out_groups, bool *out_static, char **out_cond) {
    if (out_groups != NULL) {
        *out_groups = false;
    }
    if (out_static != NULL) {
        *out_static = false;
    }
    if (out_cond != NULL) {
        *out_cond = s;
    }
    if (s == NULL) {
        return;
    }

    char *p = trim_inplace(s);
    if (p == NULL || *p == '\0') {
        if (out_cond != NULL) {
            *out_cond = p;
        }
        return;
    }

    if (*p != '{') {
        if (out_cond != NULL) {
            *out_cond = p;
        }
        return;
    }

    char *opts = NULL;
    const int consumed = cmdlang_get_string_between(p, '{', '}', " \t\n", true, &opts);
    if (consumed <= 0 || opts == NULL) {
        free(opts);
        if (out_cond != NULL) {
            *out_cond = p;
        }
        return;
    }

    char *o = trim_inplace(opts);
    if (o != NULL && *o != '\0') {
        char *save = NULL;
        for (char *tok = strtok_r(o, " \t", &save); tok != NULL; tok = strtok_r(NULL, " \t", &save)) {
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

    free(opts);
    if (out_cond != NULL) {
        *out_cond = trim_inplace(p + consumed);
    }
}

bool fbwl_cmdlang_execute_foreach(const char *args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action) {
    (void)target_view;

    if (args == NULL || hooks == NULL || hooks->wm == NULL || exec_action == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    struct str_vec toks = {0};
    const char *rest = NULL;
    if (!cmdlang_tokens_between(&toks, args, '{', '}', " \t\n", true, &rest)) {
        return false;
    }
    if (toks.len == 0 || toks.len > 2 || !rest_empty(rest)) {
        str_vec_free(&toks);
        return false;
    }

    char *cmd_line = trim_inplace(toks.items[0]);
    if (cmd_line == NULL || *cmd_line == '\0') {
        str_vec_free(&toks);
        return false;
    }

    bool groups = false;
    bool static_order = false;
    char *cond_expr = NULL;
    if (toks.len > 1) {
        parse_foreach_options_inplace(toks.items[1], &groups, &static_order, &cond_expr);
        if (cond_expr != NULL && *cond_expr == '\0') {
            cond_expr = NULL;
        }
    }

    struct view_vec views = {0};
    for (struct fbwm_view *walk = hooks->wm->views.next; walk != &hooks->wm->views; walk = walk->next) {
        struct fbwl_view *view = walk != NULL ? walk->userdata : NULL;
        if (view == NULL) {
            continue;
        }
        if (groups && view->tab_group != NULL && !fbwl_tabs_view_is_active(view)) {
            continue;
        }
        (void)view_vec_push(&views, view);
    }

    if (static_order && views.len > 1) {
        qsort(views.items, views.len, sizeof(views.items[0]), view_create_seq_cmp);
    }

    bool any = false;
    for (size_t i = 0; i < views.len; i++) {
        struct fbwl_view *view = views.items[i];
        if (view == NULL) {
            continue;
        }
        if (cond_expr != NULL && !cmdlang_eval_bool(cond_expr, view, hooks, depth + 1)) {
            continue;
        }
        if (fbwl_cmdlang_execute_line(cmd_line, view, hooks, depth + 1, exec_action)) {
            any = true;
        }
    }

    view_vec_free(&views);
    str_vec_free(&toks);
    return any;
}

bool fbwl_cmdlang_execute_if(const char *args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action) {
    if (args == NULL || hooks == NULL || exec_action == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    struct str_vec toks = {0};
    const char *rest = NULL;
    if (!cmdlang_tokens_between(&toks, args, '{', '}', " \t\n", true, &rest)) {
        return false;
    }
    if (toks.len < 2 || toks.len > 3 || !rest_empty(rest)) {
        str_vec_free(&toks);
        return false;
    }

    char *cond = trim_inplace(toks.items[0]);
    char *then_cmd = trim_inplace(toks.items[1]);
    char *else_cmd = toks.len > 2 ? trim_inplace(toks.items[2]) : NULL;

    const bool ok = cmdlang_eval_bool(cond, target_view, hooks, depth + 1);
    bool exec_ok = false;
    if (ok) {
        if (then_cmd != NULL && *then_cmd != '\0') {
            exec_ok = fbwl_cmdlang_execute_line(then_cmd, target_view, hooks, depth + 1, exec_action);
        }
    } else if (else_cmd != NULL && *else_cmd != '\0') {
        exec_ok = fbwl_cmdlang_execute_line(else_cmd, target_view, hooks, depth + 1, exec_action);
    }

    str_vec_free(&toks);
    return exec_ok;
}
