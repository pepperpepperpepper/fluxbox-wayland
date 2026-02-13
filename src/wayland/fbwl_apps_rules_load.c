#include "wayland/fbwl_apps_rules.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_deco_mask.h"

char *trim_inplace(char *s);
bool parse_layer(const char *s, int *out);
bool parse_two_ints_with_percent(const char *s, int *out_a, bool *out_a_pct, int *out_b, bool *out_b_pct);
enum fbwl_apps_rule_anchor parse_anchor(const char *s, bool *out_ok);
void apps_rule_apply_attrs(struct fbwl_apps_rule *rule, const struct fbwl_apps_rule *attrs);
bool parse_yes_no(const char *s, bool *out);
void apps_rule_parse_match_term(struct fbwl_apps_rule *rule, const char *term);
bool apps_rules_add(struct fbwl_apps_rule **rules, size_t *rule_count, struct fbwl_apps_rule *rule);

static bool parse_match_limit(const char *s, int *out_limit) {
    if (out_limit != NULL) {
        *out_limit = 0;
    }
    if (s == NULL) {
        return true;
    }
    const char *open = strchr(s, '{');
    if (open == NULL) {
        return true;
    }
    const char *close = strchr(open + 1, '}');
    if (close == NULL) {
        return false;
    }
    size_t len = (size_t)(close - (open + 1));
    char *tmp = strndup(open + 1, len);
    if (tmp == NULL) {
        return false;
    }
    char *t = trim_inplace(tmp);
    if (t == NULL || *t == '\0') {
        free(tmp);
        return false;
    }
    char *end = NULL;
    long v = strtol(t, &end, 10);
    if (end == t || *end != '\0' || v < 0 || v > 100000) {
        free(tmp);
        return false;
    }
    if (out_limit != NULL) {
        *out_limit = (int)v;
    }
    free(tmp);
    return true;
}

bool fbwl_apps_rules_load_file(struct fbwl_apps_rule **rules, size_t *rule_count, const char *path,
        bool *rewrite_safe_out) {
    if (rewrite_safe_out != NULL) {
        *rewrite_safe_out = true;
    }
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
    bool rewrite_safe = true;
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
            rewrite_safe = false;
            continue;
        }

        char *close = strchr(s, ']');
        if (close == NULL) {
            rewrite_safe = false;
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
            const char *after_terms = rest;
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
                after_terms = close_paren + 1;
                p = after_terms;
            }
            int limit = 0;
            if (!parse_match_limit(after_terms, &limit)) {
                rewrite_safe = false;
            }
            cur.match_limit = limit;
            continue;
        }

        if (in_group) {
            if (strcasecmp(key, "app") == 0 || strcasecmp(key, "transient") == 0) {
                struct fbwl_apps_rule pat = {0};
                pat.group_id = group_id;

                const char *rest = close + 1;
                const char *p = rest;
                const char *after_terms = rest;
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
                    after_terms = close_paren + 1;
                    p = after_terms;
                }
                int limit = 0;
                if (!parse_match_limit(after_terms, &limit)) {
                    rewrite_safe = false;
                }
                pat.match_limit = limit;
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
            rewrite_safe = false;
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

        if (strcasecmp(key, "hidden") == 0) {
            bool v = false;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_focus_hidden = true;
                target->focus_hidden = v;
                target->set_icon_hidden = true;
                target->icon_hidden = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "focushidden") == 0 || strcasecmp(key, "focushiddenstate") == 0) {
            bool v = false;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_focus_hidden = true;
                target->focus_hidden = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "iconhidden") == 0 || strcasecmp(key, "iconhiddenstate") == 0) {
            bool v = false;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_icon_hidden = true;
                target->icon_hidden = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "workspace") == 0) {
            if (label == NULL || *label == '\0') {
                rewrite_safe = false;
                continue;
            }
            char *endptr = NULL;
            long ws = strtol(label, &endptr, 10);
            if (endptr == label) {
                rewrite_safe = false;
                continue;
            }
            target->set_workspace = true;
            target->workspace = (int)ws;
            continue;
        }

        if (strcasecmp(key, "head") == 0) {
            if (label == NULL || *label == '\0') {
                rewrite_safe = false;
                continue;
            }
            char *endptr = NULL;
            long head = strtol(label, &endptr, 10);
            if (endptr == label) {
                rewrite_safe = false;
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
            if (label == NULL || !parse_two_ints_with_percent(label, &w, &wp, &h, &hp)) {
                rewrite_safe = false;
            } else {
                target->set_dimensions = true;
                target->width = w;
                target->width_percent = wp;
                target->height = h;
                target->height_percent = hp;
            }
            continue;
        }

        if (strcasecmp(key, "ignoresizehints") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_ignore_size_hints = true;
                target->ignore_size_hints = v;
            } else {
                rewrite_safe = false;
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
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "sticky") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_sticky = true;
                target->sticky = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "jump") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_jump = true;
                target->jump = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "minimized") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_minimized = true;
                target->minimized = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "maximized") == 0) {
            if (label == NULL) {
                rewrite_safe = false;
                continue;
            }
            bool h = false;
            bool v = false;
            if (strcasecmp(label, "yes") == 0) {
                h = true;
                v = true;
            } else if (strcasecmp(label, "horz") == 0) {
                h = true;
                v = false;
            } else if (strcasecmp(label, "vert") == 0) {
                h = false;
                v = true;
            } else if (strcasecmp(label, "no") == 0) {
                h = false;
                v = false;
            } else {
                rewrite_safe = false;
                continue;
            }
            target->set_maximized = true;
            target->maximized_h = h;
            target->maximized_v = v;
            continue;
        }

        if (strcasecmp(key, "fullscreen") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_fullscreen = true;
                target->fullscreen = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "shaded") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_shaded = true;
                target->shaded = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "tab") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_tab = true;
                target->tab = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "alpha") == 0) {
            if (label == NULL || *label == '\0') {
                rewrite_safe = false;
                continue;
            }
            int focused = 0;
            int unfocused = 0;
            int matched = sscanf(label, "%d %d", &focused, &unfocused);
            if (matched == 1) {
                unfocused = focused;
            } else if (matched != 2) {
                rewrite_safe = false;
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

        if (strcasecmp(key, "focusnewwindow") == 0) {
            bool v = false;
            if (label == NULL || !parse_yes_no(label, &v)) {
                rewrite_safe = false;
                continue;
            }

            uint32_t prot = target->set_focus_protection ? target->focus_protection : 0;
            if ((prot & (FBWL_APPS_FOCUS_PROTECT_GAIN | FBWL_APPS_FOCUS_PROTECT_REFUSE)) == 0) {
                prot |= v ? FBWL_APPS_FOCUS_PROTECT_GAIN : FBWL_APPS_FOCUS_PROTECT_REFUSE;
                prot &= v ? ~FBWL_APPS_FOCUS_PROTECT_REFUSE : ~FBWL_APPS_FOCUS_PROTECT_GAIN;
            }

            target->set_focus_protection = true;
            target->focus_protection = prot;
            continue;
        }

        if (strcasecmp(key, "focusprotection") == 0) {
            if (label == NULL || *label == '\0') {
                rewrite_safe = false;
                continue;
            }

            char tmp[256];
            size_t len = strlen(label);
            if (len >= sizeof(tmp)) {
                len = sizeof(tmp) - 1;
            }
            memcpy(tmp, label, len);
            tmp[len] = '\0';

            uint32_t prot = 0;
            char *saveptr = NULL;
            for (char *tok = strtok_r(tmp, ", \t", &saveptr); tok != NULL; tok = strtok_r(NULL, ", \t", &saveptr)) {
                if (*tok == '\0') {
                    continue;
                }
                if (strcasecmp(tok, "none") == 0) {
                    prot = FBWL_APPS_FOCUS_PROTECT_NONE;
                } else if (strcasecmp(tok, "lock") == 0) {
                    prot = (prot & ~FBWL_APPS_FOCUS_PROTECT_DENY) | FBWL_APPS_FOCUS_PROTECT_LOCK;
                } else if (strcasecmp(tok, "deny") == 0) {
                    prot = (prot & ~FBWL_APPS_FOCUS_PROTECT_LOCK) | FBWL_APPS_FOCUS_PROTECT_DENY;
                } else if (strcasecmp(tok, "gain") == 0) {
                    prot = (prot & ~FBWL_APPS_FOCUS_PROTECT_REFUSE) | FBWL_APPS_FOCUS_PROTECT_GAIN;
                } else if (strcasecmp(tok, "refuse") == 0) {
                    prot = (prot & ~FBWL_APPS_FOCUS_PROTECT_GAIN) | FBWL_APPS_FOCUS_PROTECT_REFUSE;
                }
            }

            target->set_focus_protection = true;
            target->focus_protection = prot;
            continue;
        }

        if (strcasecmp(key, "deco") == 0) {
            target->set_decor = true;
            uint32_t mask = 0u;
            if (label == NULL || *label == '\0' || !fbwl_deco_mask_parse(label, &mask)) {
                rewrite_safe = false;
                continue;
            }
            target->decor_mask = mask;
            continue;
        }

        if (strcasecmp(key, "layer") == 0) {
            int layer = 0;
            if (label != NULL && parse_layer(label, &layer)) {
                target->set_layer = true;
                target->layer = layer;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        if (strcasecmp(key, "close") == 0 || strcasecmp(key, "saveonclose") == 0) {
            bool v;
            if (label != NULL && parse_yes_no(label, &v)) {
                target->set_save_on_close = true;
                target->save_on_close = v;
            } else {
                rewrite_safe = false;
            }
            continue;
        }

        rewrite_safe = false;
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
    if (rewrite_safe_out != NULL) {
        *rewrite_safe_out = rewrite_safe;
    }
    return true;
}
