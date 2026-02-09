#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <regex.h>

struct fbwl_ui_toolbar_env;
struct fbwl_view;

struct fbwl_iconbar_xprop_term {
    char *name;
    bool negate;
    regex_t regex;
    bool regex_valid;
};

struct fbwl_iconbar_pattern {
    bool workspace_set;
    bool workspace_current;
    int workspace0;
    bool workspace_negate;
    bool minimized_set;
    bool minimized;
    bool minimized_current;
    bool minimized_negate;
    bool maximized_set;
    bool maximized;
    bool maximized_current;
    bool maximized_negate;
    bool maximized_h_set;
    bool maximized_h;
    bool maximized_h_current;
    bool maximized_h_negate;
    bool maximized_v_set;
    bool maximized_v;
    bool maximized_v_current;
    bool maximized_v_negate;
    bool fullscreen_set;
    bool fullscreen;
    bool fullscreen_current;
    bool fullscreen_negate;
    bool shaded_set;
    bool shaded;
    bool shaded_current;
    bool shaded_negate;
    bool stuck_set;
    bool stuck;
    bool stuck_current;
    bool stuck_negate;
    bool transient_set;
    bool transient;
    bool transient_current;
    bool transient_negate;
    bool urgent_set;
    bool urgent;
    bool urgent_current;
    bool urgent_negate;
    bool iconhidden_set;
    bool iconhidden;
    bool iconhidden_current;
    bool iconhidden_negate;
    bool focushidden_set;
    bool focushidden;
    bool focushidden_current;
    bool focushidden_negate;
    bool workspacename_set;
    bool workspacename_negate;
    char *workspacename;
    bool workspacename_current;
    regex_t workspacename_regex;
    bool workspacename_regex_valid;
    bool head_set;
    bool head_mouse;
    bool head_current;
    int head0;
    bool head_negate;
    bool layer_set;
    int layer_kind;
    bool layer_current;
    bool layer_negate;
    bool screen_set;
    int screen0;
    bool screen_current;
    bool screen_negate;
    bool title_set;
    bool title_negate;
    char *title;
    bool title_current;
    regex_t title_regex;
    bool title_regex_valid;
    bool name_set;
    bool name_negate;
    char *name;
    bool name_current;
    regex_t name_regex;
    bool name_regex_valid;
    bool role_set;
    bool role_negate;
    char *role;
    bool role_current;
    regex_t role_regex;
    bool role_regex_valid;
    bool class_set;
    bool class_negate;
    char *class;
    bool class_current;
    regex_t class_regex;
    bool class_regex_valid;

    struct fbwl_iconbar_xprop_term *xprops;
    size_t xprops_len;
    size_t xprops_cap;
};

void fbwl_iconbar_pattern_parse_inplace(struct fbwl_iconbar_pattern *pat, char *pattern);

void fbwl_iconbar_pattern_free(struct fbwl_iconbar_pattern *pat);

bool fbwl_iconbar_pattern_matches(const struct fbwl_iconbar_pattern *pat, const struct fbwl_ui_toolbar_env *env,
        const struct fbwl_view *view, int current_ws);

// Generic Fluxbox-style ClientPattern matcher (no iconbar-specific behavior).
bool fbwl_client_pattern_matches(const struct fbwl_iconbar_pattern *pat, const struct fbwl_ui_toolbar_env *env,
        const struct fbwl_view *view, int current_ws);
