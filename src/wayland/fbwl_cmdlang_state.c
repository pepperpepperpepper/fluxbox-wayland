#include "wayland/fbwl_cmdlang.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"

enum { CMDLANG_MAX_DEPTH = 8 };

struct str_vec {
    char **items;
    size_t len;
    size_t cap;
};

struct toggle_state {
    void *userdata;
    const void *scope;
    char *key;
    size_t idx;
};

struct delay_state {
    struct fbwl_server *server;
    const void *scope;
    char *key;
    struct wl_event_source *timer;
    char *cmd_line;
};

static struct toggle_state *g_toggle_states = NULL;
static size_t g_toggle_states_len = 0;
static size_t g_toggle_states_cap = 0;

static struct delay_state *g_delay_states = NULL;
static size_t g_delay_states_len = 0;
static size_t g_delay_states_cap = 0;

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

static struct toggle_state *toggle_state_get_or_add(void *userdata, const void *scope, const char *args) {
    if (args == NULL) {
        return NULL;
    }

    char *key_copy = strdup(args);
    if (key_copy == NULL) {
        return NULL;
    }
    char *key = trim_inplace(key_copy);
    if (key == NULL || *key == '\0') {
        free(key_copy);
        return NULL;
    }
    char *key_norm = strdup(key);
    free(key_copy);
    if (key_norm == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < g_toggle_states_len; i++) {
        if (g_toggle_states[i].userdata == userdata &&
                g_toggle_states[i].scope == scope &&
                strcmp(g_toggle_states[i].key, key_norm) == 0) {
            free(key_norm);
            return &g_toggle_states[i];
        }
    }

    if (g_toggle_states_len >= g_toggle_states_cap) {
        const size_t new_cap = g_toggle_states_cap > 0 ? g_toggle_states_cap * 2 : 8;
        struct toggle_state *tmp = realloc(g_toggle_states, new_cap * sizeof(*tmp));
        if (tmp == NULL) {
            free(key_norm);
            return NULL;
        }
        g_toggle_states = tmp;
        g_toggle_states_cap = new_cap;
    }

    struct toggle_state *st = &g_toggle_states[g_toggle_states_len++];
    *st = (struct toggle_state){0};
    st->userdata = userdata;
    st->scope = scope;
    st->key = key_norm;
    st->idx = 0;
    return st;
}

bool fbwl_cmdlang_execute_togglecmd(const char *args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action) {
    if (args == NULL || hooks == NULL || exec_action == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    const void *scope = hooks->cmdlang_scope != NULL ? hooks->cmdlang_scope : hooks->userdata;
    struct toggle_state *state = toggle_state_get_or_add(hooks->userdata, scope, args);
    if (state == NULL) {
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

    const size_t pick = toks.len > 0 ? (state->idx % toks.len) : 0;
    bool ok = false;
    if (pick < toks.len) {
        char *s = trim_inplace(toks.items[pick]);
        if (s != NULL && *s != '\0') {
            ok = fbwl_cmdlang_execute_line(s, target_view, hooks, depth + 1, exec_action);
        }
    }

    if (toks.len > 0) {
        state->idx = (state->idx + 1) % toks.len;
    }

    str_vec_free(&toks);
    return ok;
}

static bool parse_u64(const char *s, uint64_t *out) {
    if (s == NULL || out == NULL) {
        return false;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s || end == NULL || errno != 0) {
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

static struct delay_state *delay_state_get_or_add(struct fbwl_server *server, const void *scope, const char *args) {
    if (args == NULL) {
        return NULL;
    }

    char *key_copy = strdup(args);
    if (key_copy == NULL) {
        return NULL;
    }
    char *key = trim_inplace(key_copy);
    if (key == NULL || *key == '\0') {
        free(key_copy);
        return NULL;
    }
    char *key_norm = strdup(key);
    free(key_copy);
    if (key_norm == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < g_delay_states_len; i++) {
        if (g_delay_states[i].server == server &&
                g_delay_states[i].scope == scope &&
                strcmp(g_delay_states[i].key, key_norm) == 0) {
            free(key_norm);
            return &g_delay_states[i];
        }
    }

    if (g_delay_states_len >= g_delay_states_cap) {
        const size_t new_cap = g_delay_states_cap > 0 ? g_delay_states_cap * 2 : 8;
        struct delay_state *tmp = realloc(g_delay_states, new_cap * sizeof(*tmp));
        if (tmp == NULL) {
            free(key_norm);
            return NULL;
        }
        g_delay_states = tmp;
        g_delay_states_cap = new_cap;
    }

    struct delay_state *st = &g_delay_states[g_delay_states_len++];
    *st = (struct delay_state){0};
    st->server = server;
    st->scope = scope;
    st->key = key_norm;
    st->timer = NULL;
    st->cmd_line = NULL;
    return st;
}

static bool exec_action_nodup_depth(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth) {
    (void)depth;
    return fbwl_keybindings_execute_action(action, arg, cmd, target_view, hooks);
}

static void execute_cmd_line_now(struct fbwl_server *server, const char *cmd_line) {
    if (server == NULL || cmd_line == NULL) {
        return;
    }
    struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
    (void)fbwl_cmdlang_execute_line(cmd_line, NULL, &hooks, 0, exec_action_nodup_depth);
}

static int delay_timer_cb(void *data) {
    struct delay_state *st = data;
    if (st == NULL || st->server == NULL || st->cmd_line == NULL) {
        return 0;
    }
    wlr_log(WLR_INFO, "Delay: fire cmd=%s", st->cmd_line);
    execute_cmd_line_now(st->server, st->cmd_line);
    return 0;
}

bool fbwl_cmdlang_execute_delay(const char *args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action) {
    (void)target_view;

    if (args == NULL || hooks == NULL || exec_action == NULL) {
        return false;
    }
    if (depth > CMDLANG_MAX_DEPTH) {
        return false;
    }

    char *cmd = NULL;
    const int consumed = cmdlang_get_string_between(args, '{', '}', " \t\n", true, &cmd);
    if (consumed <= 0 || cmd == NULL) {
        free(cmd);
        return false;
    }

    const char *rest = args + consumed;
    uint64_t usec = 200;
    uint64_t parsed = 0;
    if (parse_u64(rest, &parsed)) {
        usec = parsed;
    }

    char *cmd_trimmed = trim_inplace(cmd);
    if (cmd_trimmed == NULL || *cmd_trimmed == '\0') {
        free(cmd);
        return false;
    }

    struct fbwl_server *server = hooks->userdata;
    if (server == NULL || server->wl_display == NULL) {
        const bool ok = fbwl_cmdlang_execute_line(cmd_trimmed, NULL, hooks, depth + 1, exec_action);
        free(cmd);
        return ok;
    }

    const void *scope = hooks->cmdlang_scope != NULL ? hooks->cmdlang_scope : hooks->userdata;
    struct delay_state *st = delay_state_get_or_add(server, scope, args);
    if (st == NULL) {
        free(cmd);
        return false;
    }

    char *cmd_copy = strdup(cmd_trimmed);
    free(cmd);
    if (cmd_copy == NULL) {
        return false;
    }

    free(st->cmd_line);
    st->cmd_line = cmd_copy;

    if (st->timer == NULL) {
        struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
        if (loop == NULL) {
            execute_cmd_line_now(server, st->cmd_line);
            return true;
        }
        st->timer = wl_event_loop_add_timer(loop, delay_timer_cb, st);
        if (st->timer == NULL) {
            execute_cmd_line_now(server, st->cmd_line);
            return true;
        }
    }

    uint64_t msec = (usec + 999) / 1000;
    if (msec == 0) {
        msec = 1;
    }
    if (msec > INT_MAX) {
        msec = (uint64_t)INT_MAX;
    }

    wl_event_source_timer_update(st->timer, (int)msec);
    return true;
}
