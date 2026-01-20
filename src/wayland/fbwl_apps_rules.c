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

    int rc = regcomp(&match->regex, match->pattern, REG_EXTENDED | REG_NOSUB);
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
    struct fbwl_apps_rule cur = {0};

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

        if (!in_app && (strcasecmp(key, "app") == 0 || strcasecmp(key, "transient") == 0)) {
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

        if (!in_app) {
            continue;
        }

        if (strcasecmp(key, "end") == 0) {
            (void)apps_rules_add(rules, rule_count, &cur);
            in_app = false;
            continue;
        }

        char *brace_open = strchr(close + 1, '{');
        char *brace_close = strrchr(close + 1, '}');
        char *label = NULL;
        if (brace_open != NULL && brace_close != NULL && brace_close > brace_open) {
            *brace_close = '\0';
            label = trim_inplace(brace_open + 1);
        }

        if (strcasecmp(key, "workspace") == 0) {
            if (label == NULL || *label == '\0') {
                continue;
            }
            char *endptr = NULL;
            long ws = strtol(label, &endptr, 10);
            if (endptr == label) {
                continue;
            }
            cur.set_workspace = true;
            cur.workspace = (int)ws;
            continue;
        }

        if (strcasecmp(key, "sticky") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                cur.set_sticky = true;
                cur.sticky = v;
            }
            continue;
        }

        if (strcasecmp(key, "jump") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                cur.set_jump = true;
                cur.jump = v;
            }
            continue;
        }

        if (strcasecmp(key, "minimized") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                cur.set_minimized = true;
                cur.minimized = v;
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
            cur.set_maximized = true;
            cur.maximized = v;
            continue;
        }

        if (strcasecmp(key, "fullscreen") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                cur.set_fullscreen = true;
                cur.fullscreen = v;
            }
            continue;
        }
    }

    if (in_app) {
        (void)apps_rules_add(rules, rule_count, &cur);
    }

    free(line);
    fclose(f);

    wlr_log(WLR_INFO, "Apps: loaded %zu rules from %s", *rule_count - before, path);
    return true;
}

