#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

enum fbwl_apps_focus_protection {
    FBWL_APPS_FOCUS_PROTECT_NONE = 0,
    FBWL_APPS_FOCUS_PROTECT_GAIN = 1u << 0,
    FBWL_APPS_FOCUS_PROTECT_REFUSE = 1u << 1,
    FBWL_APPS_FOCUS_PROTECT_LOCK = 1u << 2,
    FBWL_APPS_FOCUS_PROTECT_DENY = 1u << 3,
};

struct fbwl_apps_rule {
    struct fbwl_apps_rule_match app_id;
    struct fbwl_apps_rule_match instance;
    struct fbwl_apps_rule_match role;
    struct fbwl_apps_rule_match title;

    int match_limit;
    int match_count;

    int group_id;

    bool set_focus_hidden;
    bool focus_hidden;

    bool set_icon_hidden;
    bool icon_hidden;

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

    bool set_ignore_size_hints;
    bool ignore_size_hints;

    bool set_position;
    enum fbwl_apps_rule_anchor position_anchor;
    int x;
    bool x_percent;
    int y;
    bool y_percent;

    bool set_minimized;
    bool minimized;

    bool set_maximized;
    bool maximized_h;
    bool maximized_v;

    bool set_fullscreen;
    bool fullscreen;

    bool set_shaded;
    bool shaded;

    bool set_tab;
    bool tab;

    bool set_alpha;
    int alpha_focused;
    int alpha_unfocused;

    bool set_focus_protection;
    uint32_t focus_protection;

    bool set_decor;
    uint32_t decor_mask;

    bool set_layer;
    int layer;

    bool set_save_on_close;
    bool save_on_close;
};

void fbwl_apps_rules_free(struct fbwl_apps_rule **rules, size_t *rule_count);

bool fbwl_apps_rules_load_file(struct fbwl_apps_rule **rules, size_t *rule_count, const char *path,
    bool *rewrite_safe_out);

bool fbwl_apps_rules_save_file(const struct fbwl_apps_rule *rules, size_t rule_count, const char *path);

const struct fbwl_apps_rule *fbwl_apps_rules_match(const struct fbwl_apps_rule *rules, size_t rule_count,
    const char *app_id, const char *instance, const char *title, const char *role, size_t *rule_index_out);
