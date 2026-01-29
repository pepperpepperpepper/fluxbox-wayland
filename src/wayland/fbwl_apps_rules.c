#include "wayland/fbwl_apps_rules.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

static void apps_rule_match_free(struct fbwl_apps_rule_match *match) {
    if (match == NULL) {
        return;
    }

    if (match->regex_valid) {
        regfree(&match->regex);
        match->regex_valid = false;
    }

    free(match->pattern);
    match->pattern = NULL;
    match->set = false;
    match->negate = false;
}

static bool apps_rule_match_set_regex(struct fbwl_apps_rule_match *match, const char *pattern, bool negate) {
    if (match == NULL || pattern == NULL) {
        return false;
    }

    apps_rule_match_free(match);
    match->set = true;
    match->negate = negate;
    match->pattern = strdup(pattern);
    if (match->pattern == NULL) {
        match->set = false;
        return false;
    }

    const size_t pat_len = strlen(pattern);
    char *anchored = malloc(pat_len + 5);
    if (anchored == NULL) {
        free(match->pattern);
        match->pattern = NULL;
        match->set = false;
        return false;
    }
    snprintf(anchored, pat_len + 5, "^(%s)$", pattern);

    int rc = regcomp(&match->regex, anchored, REG_EXTENDED | REG_NOSUB);
    free(anchored);
    if (rc != 0) {
        char errbuf[256];
        errbuf[0] = '\0';
        regerror(rc, &match->regex, errbuf, sizeof(errbuf));
        wlr_log(WLR_ERROR, "Apps: invalid regex '%s': %s", match->pattern, errbuf);
        free(match->pattern);
        match->pattern = NULL;
        match->regex_valid = false;
        return false;
    }

    match->regex_valid = true;
    return true;
}

static bool parse_layer(const char *s, int *out) {
    if (s == NULL || out == NULL) {
        return false;
    }

    char *endptr = NULL;
    long n = strtol(s, &endptr, 10);
    if (endptr != s && endptr != NULL && *endptr == '\0') {
        *out = (int)n;
        return true;
    }

    const char *v = s;
    if (strcasecmp(v, "menu") == 0) {
        *out = 0;
        return true;
    }
    if (strcasecmp(v, "abovedock") == 0) {
        *out = 2;
        return true;
    }
    if (strcasecmp(v, "dock") == 0) {
        *out = 4;
        return true;
    }
    if (strcasecmp(v, "top") == 0) {
        *out = 6;
        return true;
    }
    if (strcasecmp(v, "normal") == 0) {
        *out = 8;
        return true;
    }
    if (strcasecmp(v, "bottom") == 0) {
        *out = 10;
        return true;
    }
    if (strcasecmp(v, "desktop") == 0) {
        *out = 12;
        return true;
    }
    if (strcasecmp(v, "overlay") == 0) {
        *out = 0;
        return true;
    }
    if (strcasecmp(v, "background") == 0) {
        *out = 12;
        return true;
    }
    return false;
}

static bool parse_int_with_percent(const char *token, int *out_value, bool *out_percent) {
    if (token == NULL || out_value == NULL || out_percent == NULL) {
        return false;
    }

    *out_value = 0;
    *out_percent = false;

    char buf[64];
    size_t n = strlen(token);
    if (n == 0) {
        return false;
    }
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, token, n);
    buf[n] = '\0';

    if (buf[n - 1] == '%') {
        buf[n - 1] = '\0';
        *out_percent = true;
    }

    char *endptr = NULL;
    long v = strtol(buf, &endptr, 10);
    if (endptr == buf || endptr == NULL || *endptr != '\0') {
        return false;
    }

    *out_value = (int)v;
    return true;
}

static bool parse_two_ints_with_percent(const char *s, int *out_a, bool *out_a_pct, int *out_b, bool *out_b_pct) {
    if (s == NULL || out_a == NULL || out_a_pct == NULL || out_b == NULL || out_b_pct == NULL) {
        return false;
    }

    char tmp[128];
    size_t n = strlen(s);
    if (n == 0) {
        return false;
    }
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, s, n);
    tmp[n] = '\0';

    char *saveptr = NULL;
    char *a = strtok_r(tmp, " \t", &saveptr);
    char *b = strtok_r(NULL, " \t", &saveptr);
    if (a == NULL || b == NULL) {
        return false;
    }
    if (!parse_int_with_percent(a, out_a, out_a_pct)) {
        return false;
    }
    if (!parse_int_with_percent(b, out_b, out_b_pct)) {
        return false;
    }
    return true;
}

static enum fbwl_apps_rule_anchor parse_anchor(const char *s, bool *out_ok) {
    if (out_ok != NULL) {
        *out_ok = false;
    }
    if (s == NULL || *s == '\0') {
        return FBWL_APPS_ANCHOR_TOPLEFT;
    }

    if (strcasecmp(s, "topleft") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_TOPLEFT;
    }
    if (strcasecmp(s, "left") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_LEFT;
    }
    if (strcasecmp(s, "bottomleft") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_BOTTOMLEFT;
    }
    if (strcasecmp(s, "top") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_TOP;
    }
    if (strcasecmp(s, "center") == 0 || strcasecmp(s, "wincen") == 0 ||
            strcasecmp(s, "wincenter") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_CENTER;
    }
    if (strcasecmp(s, "bottom") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_BOTTOM;
    }
    if (strcasecmp(s, "topright") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_TOPRIGHT;
    }
    if (strcasecmp(s, "right") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_RIGHT;
    }
    if (strcasecmp(s, "bottomright") == 0) {
        if (out_ok != NULL) {
            *out_ok = true;
        }
        return FBWL_APPS_ANCHOR_BOTTOMRIGHT;
    }

    return FBWL_APPS_ANCHOR_TOPLEFT;
}

static void apps_rule_apply_attrs(struct fbwl_apps_rule *rule, const struct fbwl_apps_rule *attrs) {
    if (rule == NULL || attrs == NULL) {
        return;
    }

    if (attrs->set_workspace) {
        rule->set_workspace = true;
        rule->workspace = attrs->workspace;
    }
    if (attrs->set_sticky) {
        rule->set_sticky = true;
        rule->sticky = attrs->sticky;
    }
    if (attrs->set_jump) {
        rule->set_jump = true;
        rule->jump = attrs->jump;
    }
    if (attrs->set_head) {
        rule->set_head = true;
        rule->head = attrs->head;
    }
    if (attrs->set_dimensions) {
        rule->set_dimensions = true;
        rule->width = attrs->width;
        rule->width_percent = attrs->width_percent;
        rule->height = attrs->height;
        rule->height_percent = attrs->height_percent;
    }
    if (attrs->set_position) {
        rule->set_position = true;
        rule->position_anchor = attrs->position_anchor;
        rule->x = attrs->x;
        rule->x_percent = attrs->x_percent;
        rule->y = attrs->y;
        rule->y_percent = attrs->y_percent;
    }
    if (attrs->set_minimized) {
        rule->set_minimized = true;
        rule->minimized = attrs->minimized;
    }
    if (attrs->set_maximized) {
        rule->set_maximized = true;
        rule->maximized = attrs->maximized;
    }
    if (attrs->set_fullscreen) {
        rule->set_fullscreen = true;
        rule->fullscreen = attrs->fullscreen;
    }
    if (attrs->set_shaded) {
        rule->set_shaded = true;
        rule->shaded = attrs->shaded;
    }
    if (attrs->set_alpha) {
        rule->set_alpha = true;
        rule->alpha_focused = attrs->alpha_focused;
        rule->alpha_unfocused = attrs->alpha_unfocused;
    }
    if (attrs->set_decor) {
        rule->set_decor = true;
        rule->decor_enabled = attrs->decor_enabled;
    }
    if (attrs->set_layer) {
        rule->set_layer = true;
        rule->layer = attrs->layer;
    }
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

static void apps_rule_parse_match_term(struct fbwl_apps_rule *rule, const char *term) {
    if (rule == NULL || term == NULL) {
        return;
    }

    char *tmp = strdup(term);
    if (tmp == NULL) {
        return;
    }

    char *s = trim_inplace(tmp);
    if (s == NULL || *s == '\0') {
        free(tmp);
        return;
    }

    bool negate = false;
    char *key = NULL;
    char *pat = NULL;

    char *op = strstr(s, "!=");
    if (op != NULL) {
        negate = true;
        *op = '\0';
        key = trim_inplace(s);
        pat = trim_inplace(op + 2);
    } else {
        op = strchr(s, '=');
        if (op != NULL) {
            *op = '\0';
            key = trim_inplace(s);
            pat = trim_inplace(op + 1);
        } else {
            key = NULL;
            pat = s;
        }
    }

    if (pat == NULL || *pat == '\0') {
        free(tmp);
        return;
    }

    const char *keyname = (key != NULL && *key != '\0') ? key : "app_id";
    if (strcasecmp(keyname, "title") == 0) {
        (void)apps_rule_match_set_regex(&rule->title, pat, negate);
    } else if (strcasecmp(keyname, "app_id") == 0 || strcasecmp(keyname, "appid") == 0 ||
            strcasecmp(keyname, "class") == 0) {
        (void)apps_rule_match_set_regex(&rule->app_id, pat, negate);
    } else if (strcasecmp(keyname, "name") == 0 || strcasecmp(keyname, "instance") == 0) {
        (void)apps_rule_match_set_regex(&rule->instance, pat, negate);
    }

    free(tmp);
}

static bool apps_rule_matches(const struct fbwl_apps_rule *rule, const char *app_id, const char *instance,
        const char *title) {
    if (rule == NULL) {
        return false;
    }

    if (rule->app_id.set) {
        if (!rule->app_id.regex_valid) {
            return false;
        }
        bool ok = regexec(&rule->app_id.regex, app_id != NULL ? app_id : "", 0, NULL, 0) == 0;
        if (rule->app_id.negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (rule->instance.set) {
        if (!rule->instance.regex_valid) {
            return false;
        }
        bool ok = regexec(&rule->instance.regex, instance != NULL ? instance : "", 0, NULL, 0) == 0;
        if (rule->instance.negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    if (rule->title.set) {
        if (!rule->title.regex_valid) {
            return false;
        }
        bool ok = regexec(&rule->title.regex, title != NULL ? title : "", 0, NULL, 0) == 0;
        if (rule->title.negate) {
            ok = !ok;
        }
        if (!ok) {
            return false;
        }
    }

    return true;
}

const struct fbwl_apps_rule *fbwl_apps_rules_match(const struct fbwl_apps_rule *rules, size_t rule_count,
        const char *app_id, const char *instance, const char *title, size_t *rule_index_out) {
    if (rules == NULL || rule_count == 0) {
        return NULL;
    }

    for (size_t i = 0; i < rule_count; i++) {
        const struct fbwl_apps_rule *rule = &rules[i];
        if (apps_rule_matches(rule, app_id, instance, title)) {
            if (rule_index_out != NULL) {
                *rule_index_out = i;
            }
            return rule;
        }
    }

    return NULL;
}

void fbwl_apps_rules_free(struct fbwl_apps_rule **rules, size_t *rule_count) {
    if (rules == NULL || rule_count == NULL || *rules == NULL) {
        return;
    }

    for (size_t i = 0; i < *rule_count; i++) {
        apps_rule_match_free(&(*rules)[i].app_id);
        apps_rule_match_free(&(*rules)[i].instance);
        apps_rule_match_free(&(*rules)[i].title);
    }

    free(*rules);
    *rules = NULL;
    *rule_count = 0;
}

static bool apps_rules_add(struct fbwl_apps_rule **rules, size_t *rule_count, struct fbwl_apps_rule *rule) {
    if (rules == NULL || rule_count == NULL || rule == NULL) {
        return false;
    }

    struct fbwl_apps_rule *new_rules = realloc(*rules, (*rule_count + 1) * sizeof(*new_rules));
    if (new_rules == NULL) {
        return false;
    }
    *rules = new_rules;
    (*rules)[(*rule_count)++] = *rule;
    memset(rule, 0, sizeof(*rule));
    return true;
}

bool fbwl_apps_rules_load_file(struct fbwl_apps_rule **rules, size_t *rule_count, const char *path) {
    if (rules == NULL || rule_count == NULL || path == NULL || *path == '\0') {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Apps: failed to open %s: %s", path, strerror(errno));
        return false;
    }

    size_t before = *rule_count;
    bool in_app = false;
    bool in_group = false;
    struct fbwl_apps_rule cur = {0};
    struct fbwl_apps_rule group_attrs = {0};
    size_t group_start = 0;
    int group_id = 0;
    int next_group_id = 1;

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
        if (*s == '#') {
            continue;
        }

        if (*s != '[') {
            continue;
        }

        char *close = strchr(s, ']');
        if (close == NULL) {
            continue;
        }

        char keybuf[64];
        size_t keylen = (size_t)(close - (s + 1));
        if (keylen >= sizeof(keybuf)) {
            keylen = sizeof(keybuf) - 1;
        }
        memcpy(keybuf, s + 1, keylen);
        keybuf[keylen] = '\0';

        char *key = trim_inplace(keybuf);
        if (key == NULL || *key == '\0') {
            continue;
        }

        if (!in_app && !in_group && strcasecmp(key, "group") == 0) {
            in_group = true;
            memset(&group_attrs, 0, sizeof(group_attrs));
            group_start = *rule_count;
            group_id = next_group_id++;
            continue;
        }

        if (!in_app && !in_group && (strcasecmp(key, "app") == 0 || strcasecmp(key, "transient") == 0)) {
            memset(&cur, 0, sizeof(cur));
            in_app = true;

            const char *rest = close + 1;
            const char *p = rest;
            while (p != NULL) {
                const char *open_paren = strchr(p, '(');
                if (open_paren == NULL) {
                    break;
                }
                const char *close_paren = strchr(open_paren + 1, ')');
                if (close_paren == NULL) {
                    break;
                }
                size_t tlen = (size_t)(close_paren - (open_paren + 1));
                char *term = malloc(tlen + 1);
                if (term != NULL) {
                    memcpy(term, open_paren + 1, tlen);
                    term[tlen] = '\0';
                    apps_rule_parse_match_term(&cur, term);
                    free(term);
                }
                p = close_paren + 1;
            }
            continue;
        }

        if (in_group) {
            if (strcasecmp(key, "app") == 0 || strcasecmp(key, "transient") == 0) {
                struct fbwl_apps_rule pat = {0};
                pat.group_id = group_id;

                const char *rest = close + 1;
                const char *p = rest;
                while (p != NULL) {
                    const char *open_paren = strchr(p, '(');
                    if (open_paren == NULL) {
                        break;
                    }
                    const char *close_paren = strchr(open_paren + 1, ')');
                    if (close_paren == NULL) {
                        break;
                    }
                    size_t tlen = (size_t)(close_paren - (open_paren + 1));
                    char *term = malloc(tlen + 1);
                    if (term != NULL) {
                        memcpy(term, open_paren + 1, tlen);
                        term[tlen] = '\0';
                        apps_rule_parse_match_term(&pat, term);
                        free(term);
                    }
                    p = close_paren + 1;
                }
                (void)apps_rules_add(rules, rule_count, &pat);
                continue;
            }

            if (strcasecmp(key, "end") == 0) {
                for (size_t i = group_start; i < *rule_count; i++) {
                    if ((*rules)[i].group_id == group_id) {
                        apps_rule_apply_attrs(&(*rules)[i], &group_attrs);
                    }
                }
                in_group = false;
                continue;
            }
        }

        if (!in_app && !in_group) {
            continue;
        }

        if (in_app && strcasecmp(key, "end") == 0) {
            (void)apps_rules_add(rules, rule_count, &cur);
            in_app = false;
            continue;
        }

        char *brace_open = strchr(close + 1, '{');
        char *brace_close = strrchr(close + 1, '}');
        char *paren_open = strchr(close + 1, '(');
        char *paren_close = NULL;
        char *anchor = NULL;
        if (paren_open != NULL) {
            paren_close = strchr(paren_open + 1, ')');
        }
        if (paren_open != NULL && paren_close != NULL && (brace_open == NULL || paren_close < brace_open)) {
            *paren_close = '\0';
            anchor = trim_inplace(paren_open + 1);
        }
        char *label = NULL;
        if (brace_open != NULL && brace_close != NULL && brace_close > brace_open) {
            *brace_close = '\0';
            label = trim_inplace(brace_open + 1);
        }

        struct fbwl_apps_rule *target = in_group ? &group_attrs : &cur;

        if (strcasecmp(key, "workspace") == 0) {
            if (label == NULL || *label == '\0') {
                continue;
            }
            char *endptr = NULL;
            long ws = strtol(label, &endptr, 10);
            if (endptr == label) {
                continue;
            }
            target->set_workspace = true;
            target->workspace = (int)ws;
            continue;
        }

        if (strcasecmp(key, "head") == 0) {
            if (label == NULL || *label == '\0') {
                continue;
            }
            char *endptr = NULL;
            long head = strtol(label, &endptr, 10);
            if (endptr == label) {
                continue;
            }
            target->set_head = true;
            target->head = (int)head;
            continue;
        }

        if (strcasecmp(key, "dimensions") == 0) {
            int w = 0;
            int h = 0;
            bool wp = false;
            bool hp = false;
            if (label != NULL && parse_two_ints_with_percent(label, &w, &wp, &h, &hp)) {
                target->set_dimensions = true;
                target->width = w;
                target->width_percent = wp;
                target->height = h;
                target->height_percent = hp;
            }
            continue;
        }

        if (strcasecmp(key, "position") == 0) {
            int x = 0;
            int y = 0;
            bool xp = false;
            bool yp = false;
            if (label != NULL && parse_two_ints_with_percent(label, &x, &xp, &y, &yp)) {
                bool ok = false;
                enum fbwl_apps_rule_anchor a = FBWL_APPS_ANCHOR_TOPLEFT;
                if (anchor != NULL && *anchor != '\0') {
                    a = parse_anchor(anchor, &ok);
                    (void)ok;
                }
                target->set_position = true;
                target->position_anchor = a;
                target->x = x;
                target->x_percent = xp;
                target->y = y;
                target->y_percent = yp;
            }
            continue;
        }

        if (strcasecmp(key, "sticky") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_sticky = true;
                target->sticky = v;
            }
            continue;
        }

        if (strcasecmp(key, "jump") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_jump = true;
                target->jump = v;
            }
            continue;
        }

        if (strcasecmp(key, "minimized") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_minimized = true;
                target->minimized = v;
            }
            continue;
        }

        if (strcasecmp(key, "maximized") == 0) {
            if (label == NULL) {
                continue;
            }
            bool v = false;
            if (strcasecmp(label, "yes") == 0 || strcasecmp(label, "horz") == 0 ||
                    strcasecmp(label, "vert") == 0) {
                v = true;
            } else if (strcasecmp(label, "no") == 0) {
                v = false;
            } else {
                continue;
            }
            target->set_maximized = true;
            target->maximized = v;
            continue;
        }

        if (strcasecmp(key, "fullscreen") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_fullscreen = true;
                target->fullscreen = v;
            }
            continue;
        }

        if (strcasecmp(key, "shaded") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_shaded = true;
                target->shaded = v;
            }
            continue;
        }

        if (strcasecmp(key, "alpha") == 0) {
            if (label == NULL || *label == '\0') {
                continue;
            }
            int focused = 0;
            int unfocused = 0;
            int matched = sscanf(label, "%d %d", &focused, &unfocused);
            if (matched == 1) {
                unfocused = focused;
            } else if (matched != 2) {
                continue;
            }
            if (focused < 0) {
                focused = 0;
            } else if (focused > 255) {
                focused = 255;
            }
            if (unfocused < 0) {
                unfocused = 0;
            } else if (unfocused > 255) {
                unfocused = 255;
            }

            target->set_alpha = true;
            target->alpha_focused = focused;
            target->alpha_unfocused = unfocused;
            continue;
        }

        if (strcasecmp(key, "deco") == 0) {
            if (label == NULL || *label == '\0') {
                continue;
            }
            target->set_decor = true;
            if (strcasecmp(label, "none") == 0) {
                target->decor_enabled = false;
            } else {
                target->decor_enabled = true;
            }
            continue;
        }

        if (strcasecmp(key, "layer") == 0) {
            int layer = 0;
            if (label != NULL && parse_layer(label, &layer)) {
                target->set_layer = true;
                target->layer = layer;
            }
            continue;
        }
    }

    if (in_app) {
        (void)apps_rules_add(rules, rule_count, &cur);
    }

    if (in_group) {
        for (size_t i = group_start; i < *rule_count; i++) {
            if ((*rules)[i].group_id == group_id) {
                apps_rule_apply_attrs(&(*rules)[i], &group_attrs);
            }
        }
    }

    free(line);
    fclose(f);

    wlr_log(WLR_INFO, "Apps: loaded %zu rules from %s", *rule_count - before, path);
    return true;
}
