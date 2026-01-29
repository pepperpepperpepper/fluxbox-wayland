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

enum fbwl_apps_rule_anchor {
    FBWL_APPS_ANCHOR_TOPLEFT = 0,
    FBWL_APPS_ANCHOR_LEFT,
    FBWL_APPS_ANCHOR_BOTTOMLEFT,
    FBWL_APPS_ANCHOR_TOP,
    FBWL_APPS_ANCHOR_CENTER,
    FBWL_APPS_ANCHOR_BOTTOM,
    FBWL_APPS_ANCHOR_TOPRIGHT,
    FBWL_APPS_ANCHOR_RIGHT,
    FBWL_APPS_ANCHOR_BOTTOMRIGHT,
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

    bool set_head;
    int head;

    bool set_dimensions;
    int width;
    bool width_percent;
    int height;
    bool height_percent;

    bool set_position;
    enum fbwl_apps_rule_anchor position_anchor;
    int x;
    bool x_percent;
    int y;
    bool y_percent;

    bool set_minimized;
    bool minimized;

    bool set_maximized;
    bool maximized;

    bool set_fullscreen;
    bool fullscreen;

    bool set_shaded;
    bool shaded;

    bool set_alpha;
    int alpha_focused;
    int alpha_unfocused;

    bool set_decor;
    bool decor_enabled;

    bool set_layer;
    int layer;
};

void fbwl_apps_rules_free(struct fbwl_apps_rule **rules, size_t *rule_count);

bool fbwl_apps_rules_load_file(struct fbwl_apps_rule **rules, size_t *rule_count, const char *path);

const struct fbwl_apps_rule *fbwl_apps_rules_match(const struct fbwl_apps_rule *rules, size_t rule_count,
    const char *app_id, const char *instance, const char *title, size_t *rule_index_out);
