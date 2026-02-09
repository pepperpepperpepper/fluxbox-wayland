#include "wayland/fbwl_ui_toolbar_iconbar_pattern.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wmcore/fbwm_core.h"
#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_view.h"

static void iconbar_pattern_regex_clear(regex_t *re, bool *valid) {
    if (re == NULL || valid == NULL) {
        return;
    }
    if (*valid) {
        regfree(re);
        *valid = false;
    }
}

static bool iconbar_pattern_regex_set(regex_t *re, bool *valid, const char *pattern, const char *why) {
    if (re == NULL || valid == NULL) {
        return false;
    }

    iconbar_pattern_regex_clear(re, valid);

    if (pattern == NULL) {
        return false;
    }

    const size_t pat_len = strlen(pattern);
    char *anchored = malloc(pat_len + 3);
    if (anchored == NULL) {
        return false;
    }
    snprintf(anchored, pat_len + 3, "^%s$", pattern);

    int rc = regcomp(re, anchored, REG_EXTENDED | REG_NOSUB);
    free(anchored);
    if (rc != 0) {
        char errbuf[256];
        errbuf[0] = '\0';
        regerror(rc, re, errbuf, sizeof(errbuf));
        wlr_log(WLR_ERROR, "Iconbar: invalid regex %s='%s': %s",
            why != NULL ? why : "(unknown)",
            pattern,
            errbuf);
        *valid = false;
        return false;
    }

    *valid = true;
    return true;
}

void fbwl_iconbar_pattern_free(struct fbwl_iconbar_pattern *pat) {
    if (pat == NULL) {
        return;
    }

    iconbar_pattern_regex_clear(&pat->workspacename_regex, &pat->workspacename_regex_valid);
    iconbar_pattern_regex_clear(&pat->title_regex, &pat->title_regex_valid);
    iconbar_pattern_regex_clear(&pat->name_regex, &pat->name_regex_valid);
    iconbar_pattern_regex_clear(&pat->role_regex, &pat->role_regex_valid);
    iconbar_pattern_regex_clear(&pat->class_regex, &pat->class_regex_valid);

    if (pat->xprops != NULL) {
        for (size_t i = 0; i < pat->xprops_len; i++) {
            iconbar_pattern_regex_clear(&pat->xprops[i].regex, &pat->xprops[i].regex_valid);
        }
        free(pat->xprops);
        pat->xprops = NULL;
    }
    pat->xprops_len = 0;
    pat->xprops_cap = 0;
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
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static const char *safe_cstr(const char *s) {
    return s != NULL ? s : "";
}

static bool match_bool_term(bool current, bool negate, bool candidate, bool pattern_value, bool focused_value,
        bool focused_present) {
    bool ok = current ? (focused_present && candidate == focused_value) : (candidate == pattern_value);
    if (negate) {
        ok = !ok;
    }
    return ok;
}

static bool match_regex_or_current(bool current, bool negate, bool regex_valid, const regex_t *regex, const char *candidate,
        const char *focused, bool focused_present) {
    bool ok = false;
    if (current) {
        if (!focused_present) {
            return false;
        }
        ok = strcmp(safe_cstr(candidate), safe_cstr(focused)) == 0;
    } else {
        if (!regex_valid || regex == NULL) {
            return false;
        }
        ok = regexec(regex, safe_cstr(candidate), 0, NULL, 0) == 0;
    }
    if (negate) {
        ok = !ok;
    }
    return ok;
}

static xcb_atom_t intern_atom(xcb_connection_t *conn, const char *name) {
    if (conn == NULL || name == NULL || *name == '\0') {
        return XCB_ATOM_NONE;
    }
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return XCB_ATOM_NONE;
    }
    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

static struct fbwl_iconbar_xprop_term *iconbar_pattern_xprop_push(struct fbwl_iconbar_pattern *pat) {
    if (pat == NULL) {
        return NULL;
    }
    if (pat->xprops_len >= pat->xprops_cap) {
        const size_t new_cap = pat->xprops_cap > 0 ? pat->xprops_cap * 2 : 4;
        struct fbwl_iconbar_xprop_term *tmp = realloc(pat->xprops, new_cap * sizeof(*tmp));
        if (tmp == NULL) {
            return NULL;
        }
        pat->xprops = tmp;
        pat->xprops_cap = new_cap;
    }
    struct fbwl_iconbar_xprop_term *t = &pat->xprops[pat->xprops_len++];
    memset(t, 0, sizeof(*t));
    return t;
}

static bool match_xprop_term(const struct fbwl_ui_toolbar_env *env, const struct fbwl_view *view,
        const struct fbwl_iconbar_xprop_term *term) {
    if (env == NULL || view == NULL || term == NULL || !term->regex_valid) {
        return false;
    }

    const char *text = "";
    char *text_alloc = NULL;
    uint32_t cardinal = 0;

    xcb_connection_t *conn = env->xwayland != NULL ? wlr_xwayland_get_xwm_connection(env->xwayland) : NULL;
    xcb_window_t win = view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL ? view->xwayland_surface->window_id
                                                                                          : XCB_WINDOW_NONE;
    xcb_atom_t prop = intern_atom(conn, term->name);

    if (conn != NULL && win != XCB_WINDOW_NONE && prop != XCB_ATOM_NONE) {
        // xcb_get_property's long_length is in 32-bit units.
        const uint32_t max_u32 = 16384; // 64 KiB
        xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, win, prop, XCB_ATOM_ANY, 0, max_u32);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(conn, cookie, NULL);
        if (reply != NULL) {
            const int len = xcb_get_property_value_length(reply);
            if (reply->format == 8 && len > 0) {
                text_alloc = malloc((size_t)len + 1);
                if (text_alloc != NULL) {
                    memcpy(text_alloc, xcb_get_property_value(reply), (size_t)len);
                    text_alloc[len] = '\0';
                    text = text_alloc;
                }
            }
            free(reply);
        }

        xcb_get_property_cookie_t c2 = xcb_get_property(conn, 0, win, prop, XCB_ATOM_CARDINAL, 0, 1);
        xcb_get_property_reply_t *r2 = xcb_get_property_reply(conn, c2, NULL);
        if (r2 != NULL) {
            if (r2->format == 32 && xcb_get_property_value_length(r2) >= 4) {
                const uint32_t *vals = (const uint32_t *)xcb_get_property_value(r2);
                if (vals != NULL) {
                    cardinal = vals[0];
                }
            }
            free(r2);
        }
    }

    const bool text_ok = regexec(&term->regex, text, 0, NULL, 0) == 0;
    char numbuf[32];
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned)cardinal);
    const bool num_ok = regexec(&term->regex, numbuf, 0, NULL, 0) == 0;

    free(text_alloc);

    bool ok = text_ok || num_ok;
    if (term->negate) {
        ok = !ok;
    }
    return ok;
}

static bool parse_yes_no(const char *s, bool *out) {
    if (s == NULL || out == NULL) {
        return false;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }
    if (strcasecmp(s, "yes") == 0 || strcasecmp(s, "true") == 0 || strcmp(s, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(s, "no") == 0 || strcasecmp(s, "false") == 0 || strcmp(s, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

enum fbwl_iconbar_layer_kind {
    FBWL_ICONBAR_LAYER_UNKNOWN = 0,
    FBWL_ICONBAR_LAYER_ABOVE_DOCK,
    FBWL_ICONBAR_LAYER_DOCK,
    FBWL_ICONBAR_LAYER_TOP,
    FBWL_ICONBAR_LAYER_NORMAL,
    FBWL_ICONBAR_LAYER_BOTTOM,
    FBWL_ICONBAR_LAYER_DESKTOP,
};

static int parse_layer_kind(const char *s) {
    if (s == NULL) {
        return FBWL_ICONBAR_LAYER_UNKNOWN;
    }
    if (strcasecmp(s, "abovedock") == 0) {
        return FBWL_ICONBAR_LAYER_ABOVE_DOCK;
    }
    if (strcasecmp(s, "dock") == 0) {
        return FBWL_ICONBAR_LAYER_DOCK;
    }
    if (strcasecmp(s, "top") == 0) {
        return FBWL_ICONBAR_LAYER_TOP;
    }
    if (strcasecmp(s, "normal") == 0) {
        return FBWL_ICONBAR_LAYER_NORMAL;
    }
    if (strcasecmp(s, "bottom") == 0) {
        return FBWL_ICONBAR_LAYER_BOTTOM;
    }
    if (strcasecmp(s, "desktop") == 0) {
        return FBWL_ICONBAR_LAYER_DESKTOP;
    }
    return FBWL_ICONBAR_LAYER_UNKNOWN;
}

static bool iconbar_pattern_is_known_key(const char *s) {
    if (s == NULL || *s == '\0') {
        return false;
    }
    return strcasecmp(s, "workspace") == 0 || strcasecmp(s, "minimized") == 0 || strcasecmp(s, "maximized") == 0 ||
        strcasecmp(s, "maximizedhorizontal") == 0 || strcasecmp(s, "maximizedvertical") == 0 ||
        strcasecmp(s, "fullscreen") == 0 || strcasecmp(s, "shaded") == 0 || strcasecmp(s, "stuck") == 0 ||
        strcasecmp(s, "sticky") == 0 || strcasecmp(s, "transient") == 0 || strcasecmp(s, "urgent") == 0 ||
        strcasecmp(s, "iconhidden") == 0 || strcasecmp(s, "focushidden") == 0 || strcasecmp(s, "workspacename") == 0 ||
        strcasecmp(s, "head") == 0 || strcasecmp(s, "layer") == 0 || strcasecmp(s, "screen") == 0 ||
        strcasecmp(s, "title") == 0 || strcasecmp(s, "name") == 0 || strcasecmp(s, "role") == 0 ||
        strcasecmp(s, "class") == 0 || strcasecmp(s, "app_id") == 0 || strcasecmp(s, "appid") == 0;
}

static void iconbar_pattern_parse_term(struct fbwl_iconbar_pattern *pat, char *term) {
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
            if (iconbar_pattern_is_known_key(s) || (*s == '@' && s[1] != '\0')) {
                key = s;
                val = "[current]";
                goto parse_key_val;
            }
            // Fluxbox ClientPattern default property is Name (instance).
            pat->name_set = true;
            pat->name_negate = false;
            pat->name = s;
            (void)iconbar_pattern_regex_set(&pat->name_regex, &pat->name_regex_valid, s, "name");
            return;
        }
        *op = '\0';
        key = trim_inplace(s);
        val = trim_inplace(op + 1);
    }

parse_key_val:
    if (key == NULL || *key == '\0' || val == NULL || *val == '\0') {
        return;
    }

    if (*key == '@' && key[1] != '\0') {
        struct fbwl_iconbar_xprop_term *t = iconbar_pattern_xprop_push(pat);
        if (t == NULL) {
            return;
        }
        t->name = key + 1;
        t->negate = negate;
        (void)iconbar_pattern_regex_set(&t->regex, &t->regex_valid, val, key);
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
        if (strcasecmp(val, "[current]") == 0) {
            pat->minimized_set = true;
            pat->minimized_negate = negate;
            pat->minimized_current = true;
            pat->minimized = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->minimized_set = true;
        pat->minimized_negate = negate;
        pat->minimized_current = false;
        pat->minimized = v;
        return;
    }
    if (strcasecmp(key, "maximized") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->maximized_set = true;
            pat->maximized_negate = negate;
            pat->maximized_current = true;
            pat->maximized = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->maximized_set = true;
        pat->maximized_negate = negate;
        pat->maximized_current = false;
        pat->maximized = v;
        return;
    }
    if (strcasecmp(key, "maximizedhorizontal") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->maximized_h_set = true;
            pat->maximized_h_negate = negate;
            pat->maximized_h_current = true;
            pat->maximized_h = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->maximized_h_set = true;
        pat->maximized_h_negate = negate;
        pat->maximized_h_current = false;
        pat->maximized_h = v;
        return;
    }
    if (strcasecmp(key, "maximizedvertical") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->maximized_v_set = true;
            pat->maximized_v_negate = negate;
            pat->maximized_v_current = true;
            pat->maximized_v = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->maximized_v_set = true;
        pat->maximized_v_negate = negate;
        pat->maximized_v_current = false;
        pat->maximized_v = v;
        return;
    }
    if (strcasecmp(key, "fullscreen") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->fullscreen_set = true;
            pat->fullscreen_negate = negate;
            pat->fullscreen_current = true;
            pat->fullscreen = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->fullscreen_set = true;
        pat->fullscreen_negate = negate;
        pat->fullscreen_current = false;
        pat->fullscreen = v;
        return;
    }
    if (strcasecmp(key, "shaded") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->shaded_set = true;
            pat->shaded_negate = negate;
            pat->shaded_current = true;
            pat->shaded = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->shaded_set = true;
        pat->shaded_negate = negate;
        pat->shaded_current = false;
        pat->shaded = v;
        return;
    }
    if (strcasecmp(key, "stuck") == 0 || strcasecmp(key, "sticky") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->stuck_set = true;
            pat->stuck_negate = negate;
            pat->stuck_current = true;
            pat->stuck = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->stuck_set = true;
        pat->stuck_negate = negate;
        pat->stuck_current = false;
        pat->stuck = v;
        return;
    }
    if (strcasecmp(key, "transient") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->transient_set = true;
            pat->transient_negate = negate;
            pat->transient_current = true;
            pat->transient = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->transient_set = true;
        pat->transient_negate = negate;
        pat->transient_current = false;
        pat->transient = v;
        return;
    }
    if (strcasecmp(key, "urgent") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->urgent_set = true;
            pat->urgent_negate = negate;
            pat->urgent_current = true;
            pat->urgent = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->urgent_set = true;
        pat->urgent_negate = negate;
        pat->urgent_current = false;
        pat->urgent = v;
        return;
    }
    if (strcasecmp(key, "iconhidden") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->iconhidden_set = true;
            pat->iconhidden_negate = negate;
            pat->iconhidden_current = true;
            pat->iconhidden = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->iconhidden_set = true;
        pat->iconhidden_negate = negate;
        pat->iconhidden_current = false;
        pat->iconhidden = v;
        return;
    }
    if (strcasecmp(key, "focushidden") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->focushidden_set = true;
            pat->focushidden_negate = negate;
            pat->focushidden_current = true;
            pat->focushidden = false;
            return;
        }
        bool v = false;
        if (!parse_yes_no(val, &v)) {
            return;
        }
        pat->focushidden_set = true;
        pat->focushidden_negate = negate;
        pat->focushidden_current = false;
        pat->focushidden = v;
        return;
    }
    if (strcasecmp(key, "workspacename") == 0) {
        pat->workspacename_set = true;
        pat->workspacename_negate = negate;
        pat->workspacename = val;
        if (strcasecmp(val, "[current]") == 0) {
            pat->workspacename_current = true;
        } else {
            pat->workspacename_current = false;
            (void)iconbar_pattern_regex_set(&pat->workspacename_regex, &pat->workspacename_regex_valid, val, "workspacename");
        }
        return;
    }
    if (strcasecmp(key, "head") == 0) {
        pat->head_set = true;
        pat->head_negate = negate;
        if (strcasecmp(val, "[current]") == 0) {
            pat->head_mouse = false;
            pat->head_current = true;
            pat->head0 = 0;
            return;
        }
        if (strcasecmp(val, "[mouse]") == 0) {
            pat->head_mouse = true;
            pat->head_current = false;
            pat->head0 = 0;
            return;
        }
        char *end = NULL;
        long head = strtol(val, &end, 10);
        if (end == val || *end != '\0' || head < -100000 || head > 100000) {
            pat->head_set = false;
            pat->head_negate = false;
            pat->head_mouse = false;
            pat->head_current = false;
            return;
        }
        pat->head_mouse = false;
        pat->head_current = false;
        pat->head0 = (int)head;
        return;
    }
    if (strcasecmp(key, "layer") == 0) {
        pat->layer_set = true;
        pat->layer_negate = negate;
        if (strcasecmp(val, "[current]") == 0) {
            pat->layer_current = true;
            pat->layer_kind = FBWL_ICONBAR_LAYER_UNKNOWN;
        } else {
            pat->layer_current = false;
            pat->layer_kind = parse_layer_kind(val);
        }
        return;
    }
    if (strcasecmp(key, "screen") == 0) {
        if (strcasecmp(val, "[current]") == 0) {
            pat->screen_set = true;
            pat->screen_negate = negate;
            pat->screen_current = true;
            pat->screen0 = 0;
            return;
        }
        char *end = NULL;
        long screen = strtol(val, &end, 10);
        if (end == val || *end != '\0' || screen < -100000 || screen > 100000) {
            return;
        }
        pat->screen_set = true;
        pat->screen_negate = negate;
        pat->screen_current = false;
        pat->screen0 = (int)screen;
        return;
    }
    if (strcasecmp(key, "title") == 0) {
        pat->title_set = true;
        pat->title_negate = negate;
        pat->title = val;
        if (strcasecmp(val, "[current]") == 0) {
            pat->title_current = true;
        } else {
            pat->title_current = false;
            (void)iconbar_pattern_regex_set(&pat->title_regex, &pat->title_regex_valid, val, "title");
        }
        return;
    }
    if (strcasecmp(key, "name") == 0) {
        pat->name_set = true;
        pat->name_negate = negate;
        pat->name = val;
        if (strcasecmp(val, "[current]") == 0) {
            pat->name_current = true;
        } else {
            pat->name_current = false;
            (void)iconbar_pattern_regex_set(&pat->name_regex, &pat->name_regex_valid, val, "name");
        }
        return;
    }
    if (strcasecmp(key, "role") == 0) {
        pat->role_set = true;
        pat->role_negate = negate;
        pat->role = val;
        if (strcasecmp(val, "[current]") == 0) {
            pat->role_current = true;
        } else {
            pat->role_current = false;
            (void)iconbar_pattern_regex_set(&pat->role_regex, &pat->role_regex_valid, val, "role");
        }
        return;
    }
    if (strcasecmp(key, "class") == 0 || strcasecmp(key, "app_id") == 0 || strcasecmp(key, "appid") == 0 ||
            strcasecmp(key, "appid") == 0) {
        pat->class_set = true;
        pat->class_negate = negate;
        pat->class = val;
        if (strcasecmp(val, "[current]") == 0) {
            pat->class_current = true;
        } else {
            pat->class_current = false;
            (void)iconbar_pattern_regex_set(&pat->class_regex, &pat->class_regex_valid, val, "class");
        }
        return;
    }
}

void fbwl_iconbar_pattern_parse_inplace(struct fbwl_iconbar_pattern *pat, char *pattern) {
    if (pat == NULL) {
        return;
    }
    fbwl_iconbar_pattern_free(pat);
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
                iconbar_pattern_parse_term(pat, tok);
            }
        }
        p = close + 1;
    }
}

static int view_layer_kind(const struct fbwl_ui_toolbar_env *env, const struct fbwl_view *view) {
    if (view == NULL || env == NULL) {
        return FBWL_ICONBAR_LAYER_UNKNOWN;
    }

    struct wlr_scene_tree *layer = view->base_layer != NULL ? view->base_layer : env->layer_normal;
    if (layer == env->layer_overlay) {
        return FBWL_ICONBAR_LAYER_ABOVE_DOCK;
    }
    if (layer == env->layer_top) {
        return FBWL_ICONBAR_LAYER_TOP;
    }
    if (layer == env->layer_bottom) {
        return FBWL_ICONBAR_LAYER_BOTTOM;
    }
    if (layer == env->layer_background) {
        return FBWL_ICONBAR_LAYER_DESKTOP;
    }
    return FBWL_ICONBAR_LAYER_NORMAL;
}

static bool view_head0(const struct fbwl_ui_toolbar_env *env, const struct fbwl_view *view, int *out_head0) {
    if (env == NULL || env->output_layout == NULL || env->outputs == NULL || out_head0 == NULL || view == NULL) {
        return false;
    }

    struct wlr_output *out = view->foreign_output;
    if (out == NULL) {
        out = wlr_output_layout_output_at(env->output_layout, view->x + 1, view->y + 1);
    }
    if (out == NULL) {
        out = wlr_output_layout_get_center_output(env->output_layout);
    }
    if (out == NULL) {
        return false;
    }

    bool found = false;
    size_t screen = fbwl_screen_map_screen_for_output(env->output_layout, env->outputs, out, &found);
    if (!found) {
        return false;
    }

    if (screen > (size_t)INT_MAX) {
        return false;
    }
    *out_head0 = (int)screen;
    return true;
}

static bool cursor_head0(const struct fbwl_ui_toolbar_env *env, int *out_head0) {
    if (env == NULL || !env->cursor_valid || env->output_layout == NULL || env->outputs == NULL || out_head0 == NULL) {
        return false;
    }

    struct wlr_output *out = wlr_output_layout_output_at(env->output_layout, env->cursor_x, env->cursor_y);
    if (out == NULL) {
        out = wlr_output_layout_get_center_output(env->output_layout);
    }
    if (out == NULL) {
        return false;
    }

    bool found = false;
    size_t screen = fbwl_screen_map_screen_for_output(env->output_layout, env->outputs, out, &found);
    if (!found) {
        return false;
    }

    if (screen > (size_t)INT_MAX) {
        return false;
    }
    *out_head0 = (int)screen;
    return true;
}

bool fbwl_iconbar_pattern_matches(const struct fbwl_iconbar_pattern *pat, const struct fbwl_ui_toolbar_env *env,
        const struct fbwl_view *view, int current_ws) {
    if (pat == NULL || env == NULL || view == NULL) {
        return false;
    }

    // Fluxbox/X11 iconbar always appends "(iconhidden=no)" to the pattern.
    if (fbwl_view_is_icon_hidden(view)) {
        return false;
    }

    return fbwl_client_pattern_matches(pat, env, view, current_ws);
}

bool fbwl_client_pattern_matches(const struct fbwl_iconbar_pattern *pat, const struct fbwl_ui_toolbar_env *env,
        const struct fbwl_view *view, int current_ws) {
    if (pat == NULL || env == NULL || view == NULL) {
        return false;
    }

    const struct fbwl_view *focused_view = env->focused_view;
    const bool focus_present = focused_view != NULL;

    if (pat->workspace_set) {
        bool ok = false;
        if (view->wm_view.sticky) {
            ok = true;
        } else if (pat->workspace_current) {
            ok = view->wm_view.workspace == current_ws;
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

    if (pat->minimized_set && !match_bool_term(pat->minimized_current, pat->minimized_negate, view->minimized,
            pat->minimized, focus_present ? focused_view->minimized : false, focus_present)) {
        return false;
    }
    if (pat->maximized_set && !match_bool_term(pat->maximized_current, pat->maximized_negate, view->maximized,
            pat->maximized, focus_present ? focused_view->maximized : false, focus_present)) {
        return false;
    }
    if (pat->maximized_h_set && !match_bool_term(pat->maximized_h_current, pat->maximized_h_negate, view->maximized_h,
            pat->maximized_h, focus_present ? focused_view->maximized_h : false, focus_present)) {
        return false;
    }
    if (pat->maximized_v_set && !match_bool_term(pat->maximized_v_current, pat->maximized_v_negate, view->maximized_v,
            pat->maximized_v, focus_present ? focused_view->maximized_v : false, focus_present)) {
        return false;
    }
    if (pat->fullscreen_set && !match_bool_term(pat->fullscreen_current, pat->fullscreen_negate, view->fullscreen,
            pat->fullscreen, focus_present ? focused_view->fullscreen : false, focus_present)) {
        return false;
    }
    if (pat->shaded_set && !match_bool_term(pat->shaded_current, pat->shaded_negate, view->shaded, pat->shaded,
            focus_present ? focused_view->shaded : false, focus_present)) {
        return false;
    }
    if (pat->stuck_set && !match_bool_term(pat->stuck_current, pat->stuck_negate, view->wm_view.sticky, pat->stuck,
            focus_present ? focused_view->wm_view.sticky : false, focus_present)) {
        return false;
    }
    if (pat->transient_set && !match_bool_term(pat->transient_current, pat->transient_negate, fbwl_view_is_transient(view),
            pat->transient, focus_present ? fbwl_view_is_transient(focused_view) : false, focus_present)) {
        return false;
    }
    if (pat->urgent_set && !match_bool_term(pat->urgent_current, pat->urgent_negate, fbwl_view_is_urgent(view), pat->urgent,
            focus_present ? fbwl_view_is_urgent(focused_view) : false, focus_present)) {
        return false;
    }
    if (pat->iconhidden_set && !match_bool_term(pat->iconhidden_current, pat->iconhidden_negate, fbwl_view_is_icon_hidden(view),
            pat->iconhidden, focus_present ? fbwl_view_is_icon_hidden(focused_view) : false, focus_present)) {
        return false;
    }
    if (pat->focushidden_set && !match_bool_term(pat->focushidden_current, pat->focushidden_negate, fbwl_view_is_focus_hidden(view),
            pat->focushidden, focus_present ? fbwl_view_is_focus_hidden(focused_view) : false, focus_present)) {
        return false;
    }

    if (pat->workspacename_set) {
        const char *ws_name = fbwm_core_workspace_name(env->wm, view->wm_view.workspace);
        bool ok = false;
        if (pat->workspacename_current) {
            const char *cur_name = fbwm_core_workspace_name(env->wm, current_ws);
            ok = strcmp(safe_cstr(ws_name), safe_cstr(cur_name)) == 0;
        } else {
            ok = match_regex_or_current(false, false, pat->workspacename_regex_valid, &pat->workspacename_regex, ws_name,
                NULL, false);
        }
        if (pat->workspacename_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->head_set) {
        int mouse_head = 0;
        int view_head = 0;
        int focused_head = 0;
        bool ok = false;
        if (pat->head_mouse) {
            ok = cursor_head0(env, &mouse_head) && view_head0(env, view, &view_head) && view_head == mouse_head;
        } else if (pat->head_current) {
            ok = focus_present && view_head0(env, view, &view_head) && view_head0(env, focused_view, &focused_head) &&
                view_head == focused_head;
        } else {
            ok = view_head0(env, view, &view_head) && view_head == pat->head0;
        }
        if (pat->head_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->layer_set) {
        const int view_kind = view_layer_kind(env, view);
        bool ok = false;
        if (pat->layer_current) {
            ok = focus_present && view_kind == view_layer_kind(env, focused_view);
        } else if (pat->layer_kind == FBWL_ICONBAR_LAYER_DOCK) {
            ok = view_kind == FBWL_ICONBAR_LAYER_TOP;
        } else {
            ok = view_kind == pat->layer_kind;
        }
        if (pat->layer_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (pat->screen_set) {
        bool ok = pat->screen_current ? focus_present : (pat->screen0 == 0);
        if (pat->screen_negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    for (size_t i = 0; i < pat->xprops_len; i++) {
        if (!match_xprop_term(env, view, &pat->xprops[i])) {
            return false;
        }
    }

    if (pat->title_set && !match_regex_or_current(pat->title_current, pat->title_negate, pat->title_regex_valid, &pat->title_regex,
            fbwl_view_title(view), focus_present ? fbwl_view_title(focused_view) : NULL, focus_present)) {
        return false;
    }
    if (pat->name_set && !match_regex_or_current(pat->name_current, pat->name_negate, pat->name_regex_valid, &pat->name_regex,
            fbwl_view_instance(view), focus_present ? fbwl_view_instance(focused_view) : NULL, focus_present)) {
        return false;
    }
    if (pat->role_set && !match_regex_or_current(pat->role_current, pat->role_negate, pat->role_regex_valid, &pat->role_regex,
            fbwl_view_role(view), focus_present ? fbwl_view_role(focused_view) : NULL, focus_present)) {
        return false;
    }
    if (pat->class_set && !match_regex_or_current(pat->class_current, pat->class_negate, pat->class_regex_valid, &pat->class_regex,
            fbwl_view_app_id(view), focus_present ? fbwl_view_app_id(focused_view) : NULL, focus_present)) {
        return false;
    }
    return true;
}
