#include "wayland/fbwl_server_window_remember.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_apps_rules.h"
#include "wayland/fbwl_deco_mask.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"

static bool apps_rule_match_is_set(const struct fbwl_apps_rule_match *match) {
    return match != NULL && match->set && match->pattern != NULL && match->regex_valid;
}

static bool apps_rule_has_any_settings(const struct fbwl_apps_rule *rule) {
    if (rule == NULL) {
        return false;
    }
    return rule->set_focus_hidden || rule->set_icon_hidden ||
        rule->set_workspace || rule->set_sticky || rule->set_jump || rule->set_head ||
        rule->set_dimensions || rule->set_ignore_size_hints || rule->set_position ||
        rule->set_minimized || rule->set_maximized || rule->set_fullscreen ||
        rule->set_shaded || rule->set_tab || rule->set_alpha || rule->set_focus_protection ||
        rule->set_decor || rule->set_layer || rule->set_save_on_close;
}

static bool ensure_apps_file_exists(struct fbwl_server *server) {
    if (server == NULL || server->apps_file == NULL || *server->apps_file == '\0') {
        return false;
    }
    if (fbwl_file_exists(server->apps_file)) {
        return true;
    }

    FILE *f = fopen(server->apps_file, "w");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Remember: failed to create apps file %s: %s", server->apps_file, strerror(errno));
        return false;
    }
    fclose(f);
    server->apps_rules_rewrite_safe = true;
    wlr_log(WLR_INFO, "Remember: created %s", server->apps_file);
    return true;
}

static char *regex_escape_ere(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    const size_t len = strlen(s);
    if (len > (SIZE_MAX - 1) / 2) {
        return NULL;
    }
    char *out = malloc(len * 2 + 1);
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == '.' || c == '^' || c == '$' || c == '*' || c == '+' || c == '?' ||
                c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '|') {
            out[j++] = '\\';
        }
        out[j++] = (char)c;
    }
    out[j] = '\0';
    return out;
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
        wlr_log(WLR_ERROR, "Remember: invalid regex '%s': %s", match->pattern, errbuf);
        free(match->pattern);
        match->pattern = NULL;
        match->regex_valid = false;
        return false;
    }
    match->regex_valid = true;
    return true;
}

static void apps_rule_free(struct fbwl_apps_rule *rule) {
    if (rule == NULL) {
        return;
    }
    apps_rule_match_free(&rule->app_id);
    apps_rule_match_free(&rule->instance);
    apps_rule_match_free(&rule->role);
    apps_rule_match_free(&rule->title);
    memset(rule, 0, sizeof(*rule));
}

static struct fbwl_apps_rule *apps_rules_match_for_view(struct fbwl_server *server, const struct fbwl_view *view,
        size_t *rule_index_out) {
    if (rule_index_out != NULL) {
        *rule_index_out = 0;
    }
    if (server == NULL || view == NULL || server->apps_rules == NULL || server->apps_rule_count == 0) {
        return NULL;
    }
    const char *app_id = fbwl_view_app_id(view);
    const char *instance = fbwl_view_instance(view);
    const char *title = fbwl_view_title(view);
    const char *role = fbwl_view_role(view);
    size_t idx = 0;
    const struct fbwl_apps_rule *matched = fbwl_apps_rules_match(server->apps_rules, server->apps_rule_count,
        app_id, instance, title, role, &idx);
    if (matched == NULL || idx >= server->apps_rule_count) {
        return NULL;
    }
    if (rule_index_out != NULL) {
        *rule_index_out = idx;
    }
    return &server->apps_rules[idx];
}

static bool apps_rules_append(struct fbwl_server *server, struct fbwl_apps_rule *rule, size_t *out_idx) {
    if (out_idx != NULL) {
        *out_idx = 0;
    }
    if (server == NULL || rule == NULL) {
        return false;
    }
    struct fbwl_apps_rule *new_rules = realloc(server->apps_rules, (server->apps_rule_count + 1) * sizeof(*new_rules));
    if (new_rules == NULL) {
        return false;
    }
    server->apps_rules = new_rules;
    const size_t idx = server->apps_rule_count++;
    server->apps_rules[idx] = *rule;
    memset(rule, 0, sizeof(*rule));
    if (out_idx != NULL) {
        *out_idx = idx;
    }
    return true;
}

static bool apps_rules_remove(struct fbwl_server *server, size_t idx) {
    if (server == NULL || server->apps_rules == NULL || idx >= server->apps_rule_count) {
        return false;
    }

    apps_rule_free(&server->apps_rules[idx]);

    if (idx + 1 < server->apps_rule_count) {
        memmove(&server->apps_rules[idx], &server->apps_rules[idx + 1],
            (server->apps_rule_count - idx - 1) * sizeof(server->apps_rules[0]));
    }
    server->apps_rule_count--;

    if (server->apps_rule_count == 0) {
        free(server->apps_rules);
        server->apps_rules = NULL;
        return true;
    }

    struct fbwl_apps_rule *shrunk = realloc(server->apps_rules, server->apps_rule_count * sizeof(*shrunk));
    if (shrunk != NULL) {
        server->apps_rules = shrunk;
    }
    return true;
}

static struct fbwl_apps_rule *apps_rules_find_or_create_for_view(struct fbwl_server *server, const struct fbwl_view *view,
        size_t *rule_idx_out) {
    if (rule_idx_out != NULL) {
        *rule_idx_out = 0;
    }
    if (server == NULL || view == NULL) {
        return NULL;
    }

    if (!ensure_apps_file_exists(server)) {
        return NULL;
    }
    if (!server->apps_rules_rewrite_safe) {
        wlr_log(WLR_INFO, "Remember: apps file not rewrite-safe; refusing to modify: %s",
            server->apps_file != NULL ? server->apps_file : "(null)");
        return NULL;
    }

    size_t idx = 0;
    struct fbwl_apps_rule *matched = apps_rules_match_for_view(server, view, &idx);
    if (matched != NULL) {
        if (rule_idx_out != NULL) {
            *rule_idx_out = idx;
        }
        return matched;
    }

    const char *app_id_raw = fbwl_view_app_id(view);
    const char *instance_raw = fbwl_view_instance(view);
    if (app_id_raw == NULL || *app_id_raw == '\0') {
        wlr_log(WLR_ERROR, "Remember: cannot create apps rule: missing app_id");
        return NULL;
    }

    char *app_id_pat = regex_escape_ere(app_id_raw);
    char *instance_pat = instance_raw != NULL ? regex_escape_ere(instance_raw) : NULL;
    if (app_id_pat == NULL || (instance_raw != NULL && *instance_raw != '\0' && instance_pat == NULL)) {
        free(app_id_pat);
        free(instance_pat);
        wlr_log(WLR_ERROR, "Remember: OOM creating match patterns");
        return NULL;
    }

    struct fbwl_apps_rule rule = {0};
    bool ok = apps_rule_match_set_regex(&rule.app_id, app_id_pat, false);
    if (ok && instance_pat != NULL && *instance_pat != '\0' &&
            (instance_raw == NULL || strcmp(instance_raw, app_id_raw) != 0)) {
        ok = apps_rule_match_set_regex(&rule.instance, instance_pat, false);
    }
    free(app_id_pat);
    free(instance_pat);
    if (!ok || !apps_rule_match_is_set(&rule.app_id)) {
        apps_rule_free(&rule);
        return NULL;
    }

    if (!apps_rules_append(server, &rule, &idx)) {
        apps_rule_free(&rule);
        wlr_log(WLR_ERROR, "Remember: OOM adding apps rule");
        return NULL;
    }

    server->apps_rules_generation++;
    wlr_log(WLR_INFO, "Remember: added apps rule idx=%zu app_id=%s", idx, app_id_raw);
    if (rule_idx_out != NULL) {
        *rule_idx_out = idx;
    }
    return &server->apps_rules[idx];
}

static void remember_snapshot_dimensions(struct fbwl_apps_rule *rule, const struct fbwl_view *view) {
    if (rule == NULL || view == NULL) {
        return;
    }
    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    if (w < 1 || h < 1) {
        return;
    }
    rule->width = w;
    rule->height = h;
    rule->width_percent = false;
    rule->height_percent = false;
}

static void remember_snapshot_position(struct fbwl_apps_rule *rule, struct fbwl_server *server, const struct fbwl_view *view) {
    if (rule == NULL || server == NULL || view == NULL || server->output_layout == NULL) {
        return;
    }

    int gx = view->x;
    int gy = view->y;
    int gw = fbwl_view_current_width(view);
    int gh = fbwl_view_current_height(view);
    if (gw < 1 || gh < 1) {
        return;
    }

    if ((view->fullscreen || view->maximized || view->maximized_h || view->maximized_v) &&
            view->saved_w > 0 && view->saved_h > 0) {
        gx = view->saved_x;
        gy = view->saved_y;
        gw = view->saved_w;
        gh = view->saved_h;
    }
    if (gw < 1 || gh < 1) {
        return;
    }

    struct wlr_box screen = {0};
    fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, NULL, &screen);
    if (screen.width < 1 || screen.height < 1) {
        fbwl_view_get_output_box(view, server->output_layout, NULL, &screen);
    }
    if (screen.width < 1 || screen.height < 1) {
        return;
    }

    int frame_left = 0;
    int frame_top = 0;
    int frame_right = 0;
    int frame_bottom = 0;
    fbwl_view_decor_frame_extents(view, &server->decor_theme, &frame_left, &frame_top, &frame_right, &frame_bottom);

    const int frame_x = gx - frame_left;
    const int frame_y = gy - frame_top;

    rule->position_anchor = FBWL_APPS_ANCHOR_TOPLEFT;
    rule->x = frame_x - screen.x;
    rule->y = frame_y - screen.y;
    rule->x_percent = false;
    rule->y_percent = false;
}

static int remember_snapshot_layer(const struct fbwl_server *server, const struct fbwl_view *view) {
    if (server == NULL || view == NULL) {
        return 8;
    }
    if (view->base_layer == server->layer_background) {
        return 12;
    }
    if (view->base_layer == server->layer_bottom) {
        return 10;
    }
    if (view->base_layer == server->layer_normal) {
        return 8;
    }
    if (view->base_layer == server->layer_top) {
        return 6;
    }
    if (view->base_layer == server->layer_overlay) {
        return 0;
    }
    return 8;
}

static bool save_apps_file(struct fbwl_server *server) {
    if (server == NULL || server->apps_file == NULL || *server->apps_file == '\0') {
        return false;
    }
    if (!ensure_apps_file_exists(server)) {
        return false;
    }
    if (server->apps_rule_count == 0 || server->apps_rules == NULL) {
        FILE *f = fopen(server->apps_file, "w");
        if (f == NULL) {
            return false;
        }
        fclose(f);
        return true;
    }
    return fbwl_apps_rules_save_file(server->apps_rules, server->apps_rule_count, server->apps_file);
}

void server_window_remember_toggle(struct fbwl_server *server, struct fbwl_view *view, enum fbwl_menu_remember_attr attr) {
    if (server == NULL || view == NULL) {
        return;
    }

    size_t rule_idx = 0;
    struct fbwl_apps_rule *rule = apps_rules_find_or_create_for_view(server, view, &rule_idx);
    if (rule == NULL) {
        return;
    }

    bool changed = false;
    switch (attr) {
    case FBWL_MENU_REMEMBER_WORKSPACE:
        rule->set_workspace = !rule->set_workspace;
        if (rule->set_workspace) {
            rule->workspace = view->wm_view.workspace;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_STICKY:
        rule->set_sticky = !rule->set_sticky;
        if (rule->set_sticky) {
            rule->sticky = view->wm_view.sticky;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_JUMP:
        rule->set_jump = !rule->set_jump;
        if (rule->set_jump) {
            rule->jump = true;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_HEAD:
        rule->set_head = !rule->set_head;
        if (rule->set_head) {
            rule->head = (int)fbwl_server_screen_index_for_view(server, view);
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_DIMENSIONS:
        rule->set_dimensions = !rule->set_dimensions;
        if (rule->set_dimensions) {
            remember_snapshot_dimensions(rule, view);
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_IGNORE_SIZE_HINTS:
        rule->set_ignore_size_hints = !rule->set_ignore_size_hints;
        if (rule->set_ignore_size_hints) {
            rule->ignore_size_hints = view->ignore_size_hints_override_set ? view->ignore_size_hints_override : false;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_POSITION:
        rule->set_position = !rule->set_position;
        if (rule->set_position) {
            remember_snapshot_position(rule, server, view);
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_MINIMIZED:
        rule->set_minimized = !rule->set_minimized;
        if (rule->set_minimized) {
            rule->minimized = view->minimized;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_MAXIMIZED:
        rule->set_maximized = !rule->set_maximized;
        if (rule->set_maximized) {
            rule->maximized_h = view->maximized_h;
            rule->maximized_v = view->maximized_v;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_FULLSCREEN:
        rule->set_fullscreen = !rule->set_fullscreen;
        if (rule->set_fullscreen) {
            rule->fullscreen = view->fullscreen;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_SHADED:
        rule->set_shaded = !rule->set_shaded;
        if (rule->set_shaded) {
            rule->shaded = view->shaded;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_TAB:
        rule->set_tab = !rule->set_tab;
        if (rule->set_tab) {
            rule->tab = view->tabs_enabled_override_set ? view->tabs_enabled_override : false;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_ALPHA:
        rule->set_alpha = !rule->set_alpha;
        if (rule->set_alpha) {
            rule->alpha_focused = view->alpha_focused;
            rule->alpha_unfocused = view->alpha_unfocused;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_FOCUS_PROTECTION:
        rule->set_focus_protection = !rule->set_focus_protection;
        if (rule->set_focus_protection) {
            rule->focus_protection = view->focus_protection;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_DECOR:
        rule->set_decor = !rule->set_decor;
        if (rule->set_decor) {
            rule->decor_mask = view->decor_enabled ? view->decor_mask : FBWL_DECOR_NONE;
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_LAYER:
        rule->set_layer = !rule->set_layer;
        if (rule->set_layer) {
            rule->layer = remember_snapshot_layer(server, view);
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_FOCUS_HIDDEN:
        rule->set_focus_hidden = !rule->set_focus_hidden;
        if (rule->set_focus_hidden) {
            rule->focus_hidden = fbwl_view_is_focus_hidden(view);
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_ICON_HIDDEN:
        rule->set_icon_hidden = !rule->set_icon_hidden;
        if (rule->set_icon_hidden) {
            rule->icon_hidden = fbwl_view_is_icon_hidden(view);
        }
        changed = true;
        break;
    case FBWL_MENU_REMEMBER_SAVE_ON_CLOSE:
        if (rule->set_save_on_close && rule->save_on_close) {
            rule->set_save_on_close = false;
            rule->save_on_close = false;
        } else {
            rule->set_save_on_close = true;
            rule->save_on_close = true;
        }
        changed = true;
        break;
    default:
        return;
    }

    if (!changed) {
        return;
    }

    server->apps_rules_generation++;
    server->apps_rules_rewrite_safe = true;

    if (!apps_rule_has_any_settings(rule)) {
        wlr_log(WLR_INFO, "Remember: keeping empty rule idx=%zu (no settings)", rule_idx);
    }

    if (save_apps_file(server)) {
        wlr_log(WLR_INFO, "Remember: wrote %s rule=%zu", server->apps_file, rule_idx);
    } else {
        wlr_log(WLR_ERROR, "Remember: failed to write %s", server->apps_file != NULL ? server->apps_file : "(null)");
    }
}

void server_window_remember_forget(struct fbwl_server *server, struct fbwl_view *view) {
    if (server == NULL || view == NULL) {
        return;
    }

    if (!ensure_apps_file_exists(server)) {
        return;
    }
    if (!server->apps_rules_rewrite_safe) {
        wlr_log(WLR_INFO, "Remember: apps file not rewrite-safe; refusing to modify: %s",
            server->apps_file != NULL ? server->apps_file : "(null)");
        return;
    }

    size_t idx = 0;
    if (apps_rules_match_for_view(server, view, &idx) == NULL) {
        return;
    }

    apps_rules_remove(server, idx);
    server->apps_rules_generation++;
    server->apps_rules_rewrite_safe = true;

    if (save_apps_file(server)) {
        wlr_log(WLR_INFO, "Remember: forgot rule idx=%zu wrote %s", idx, server->apps_file);
    } else {
        wlr_log(WLR_ERROR, "Remember: failed to write %s", server->apps_file != NULL ? server->apps_file : "(null)");
    }
}
