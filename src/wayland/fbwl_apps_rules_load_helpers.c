#include "wayland/fbwl_apps_rules.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

char *trim_inplace(char *s) {
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
    char *anchored = malloc(pat_len + 3);
    if (anchored == NULL) {
        free(match->pattern);
        match->pattern = NULL;
        match->set = false;
        return false;
    }
    snprintf(anchored, pat_len + 3, "^%s$", pattern);

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

bool parse_layer(const char *s, int *out) {
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

bool parse_two_ints_with_percent(const char *s, int *out_a, bool *out_a_pct, int *out_b, bool *out_b_pct) {
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

enum fbwl_apps_rule_anchor parse_anchor(const char *s, bool *out_ok) {
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

void apps_rule_apply_attrs(struct fbwl_apps_rule *rule, const struct fbwl_apps_rule *attrs) {
    if (rule == NULL || attrs == NULL) {
        return;
    }

    if (attrs->set_focus_hidden) {
        rule->set_focus_hidden = true;
        rule->focus_hidden = attrs->focus_hidden;
    }
    if (attrs->set_icon_hidden) {
        rule->set_icon_hidden = true;
        rule->icon_hidden = attrs->icon_hidden;
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
    if (attrs->set_ignore_size_hints) {
        rule->set_ignore_size_hints = true;
        rule->ignore_size_hints = attrs->ignore_size_hints;
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
        rule->maximized_h = attrs->maximized_h;
        rule->maximized_v = attrs->maximized_v;
    }
    if (attrs->set_fullscreen) {
        rule->set_fullscreen = true;
        rule->fullscreen = attrs->fullscreen;
    }
    if (attrs->set_shaded) {
        rule->set_shaded = true;
        rule->shaded = attrs->shaded;
    }
    if (attrs->set_tab) {
        rule->set_tab = true;
        rule->tab = attrs->tab;
    }
    if (attrs->set_alpha) {
        rule->set_alpha = true;
        rule->alpha_focused = attrs->alpha_focused;
        rule->alpha_unfocused = attrs->alpha_unfocused;
    }
    if (attrs->set_focus_protection) {
        rule->set_focus_protection = true;
        rule->focus_protection = attrs->focus_protection;
    }
    if (attrs->set_decor) {
        rule->set_decor = true;
        rule->decor_mask = attrs->decor_mask;
    }
    if (attrs->set_layer) {
        rule->set_layer = true;
        rule->layer = attrs->layer;
    }
    if (attrs->set_save_on_close) {
        rule->set_save_on_close = true;
        rule->save_on_close = attrs->save_on_close;
    }
}

bool parse_yes_no(const char *s, bool *out) {
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

void apps_rule_parse_match_term(struct fbwl_apps_rule *rule, const char *term) {
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

    // Fluxbox client patterns default to Name (instance) when no key is given.
    const char *keyname = (key != NULL && *key != '\0') ? key : "name";
    if (strcasecmp(keyname, "title") == 0) {
        (void)apps_rule_match_set_regex(&rule->title, pat, negate);
    } else if (strcasecmp(keyname, "app_id") == 0 || strcasecmp(keyname, "appid") == 0 ||
            strcasecmp(keyname, "class") == 0) {
        (void)apps_rule_match_set_regex(&rule->app_id, pat, negate);
    } else if (strcasecmp(keyname, "name") == 0 || strcasecmp(keyname, "instance") == 0) {
        (void)apps_rule_match_set_regex(&rule->instance, pat, negate);
    } else if (strcasecmp(keyname, "role") == 0) {
        (void)apps_rule_match_set_regex(&rule->role, pat, negate);
    }

    free(tmp);
}

bool apps_rules_add(struct fbwl_apps_rule **rules, size_t *rule_count, struct fbwl_apps_rule *rule) {
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
