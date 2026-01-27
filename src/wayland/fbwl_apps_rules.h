#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <regex.h>

struct fbwl_apps_rule_match {
    bool set;
    bool negate;
    char *pattern;
    regex_t regex;
    bool regex_valid;
};

struct fbwl_apps_rule {
    struct fbwl_apps_rule_match app_id;
    struct fbwl_apps_rule_match instance;
    struct fbwl_apps_rule_match title;

    int group_id;

    bool set_workspace;
    int workspace;

    bool set_sticky;
    bool sticky;

    bool set_jump;
    bool jump;

    bool set_minimized;
    bool minimized;

    bool set_maximized;
    bool maximized;

    bool set_fullscreen;
    bool fullscreen;

    bool set_decor;
    bool decor_enabled;

    bool set_layer;
    int layer;
};

void fbwl_apps_rules_free(struct fbwl_apps_rule **rules, size_t *rule_count);

bool fbwl_apps_rules_load_file(struct fbwl_apps_rule **rules, size_t *rule_count, const char *path);

const struct fbwl_apps_rule *fbwl_apps_rules_match(const struct fbwl_apps_rule *rules, size_t rule_count,
    const char *app_id, const char *instance, const char *title, size_t *rule_index_out);
