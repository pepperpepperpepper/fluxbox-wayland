#include "wayland/fbwl_apps_rules.h"

static bool apps_rule_matches(const struct fbwl_apps_rule *rule, const char *app_id, const char *instance,
        const char *title, const char *role) {
    if (rule == NULL) {
        return false;
    }

    if (rule->match_limit > 0 && rule->match_count >= rule->match_limit) {
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

    if (rule->role.set) {
        if (!rule->role.regex_valid) {
            return false;
        }
        bool ok = regexec(&rule->role.regex, role != NULL ? role : "", 0, NULL, 0) == 0;
        if (rule->role.negate) {
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
        const char *app_id, const char *instance, const char *title, const char *role, size_t *rule_index_out) {
    if (rules == NULL || rule_count == 0) {
        return NULL;
    }

    for (size_t i = 0; i < rule_count; i++) {
        const struct fbwl_apps_rule *rule = &rules[i];
        if (apps_rule_matches(rule, app_id, instance, title, role)) {
            if (rule_index_out != NULL) {
                *rule_index_out = i;
            }
            return rule;
        }
    }

    return NULL;
}
