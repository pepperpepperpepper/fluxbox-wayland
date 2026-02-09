#include "wayland/fbwl_apps_rules.h"

#include <stdlib.h>

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

