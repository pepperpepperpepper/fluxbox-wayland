#include "wayland/fbwl_apps_rules.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wayland/fbwl_deco_mask.h"

static void apps_rules_write_match_terms(FILE *f, const struct fbwl_apps_rule *rule) {
    if (f == NULL || rule == NULL) {
        return;
    }

    if (rule->app_id.set && rule->app_id.pattern != NULL) {
        fprintf(f, " (app_id%s=%s)", rule->app_id.negate ? "!" : "", rule->app_id.pattern);
    }
    if (rule->instance.set && rule->instance.pattern != NULL) {
        fprintf(f, " (instance%s=%s)", rule->instance.negate ? "!" : "", rule->instance.pattern);
    }
    if (rule->role.set && rule->role.pattern != NULL) {
        fprintf(f, " (role%s=%s)", rule->role.negate ? "!" : "", rule->role.pattern);
    }
    if (rule->title.set && rule->title.pattern != NULL) {
        fprintf(f, " (title%s=%s)", rule->title.negate ? "!" : "", rule->title.pattern);
    }
}

static void apps_rules_write_int_with_percent(FILE *f, int v, bool percent) {
    if (f == NULL) {
        return;
    }
    if (percent) {
        fprintf(f, "%d%%", v);
    } else {
        fprintf(f, "%d", v);
    }
}

static void apps_rules_write_focus_protection(FILE *f, uint32_t prot) {
    if (f == NULL) {
        return;
    }
    if (prot == 0) {
        fprintf(f, "none");
        return;
    }

    bool first = true;
    if (prot & FBWL_APPS_FOCUS_PROTECT_LOCK) {
        fprintf(f, "%slock", first ? "" : " ");
        first = false;
    }
    if (prot & FBWL_APPS_FOCUS_PROTECT_DENY) {
        fprintf(f, "%sdeny", first ? "" : " ");
        first = false;
    }
    if (prot & FBWL_APPS_FOCUS_PROTECT_GAIN) {
        fprintf(f, "%sgain", first ? "" : " ");
        first = false;
    }
    if (prot & FBWL_APPS_FOCUS_PROTECT_REFUSE) {
        fprintf(f, "%srefuse", first ? "" : " ");
        first = false;
    }
}

static const char *apps_rules_anchor_name(enum fbwl_apps_rule_anchor anchor) {
    switch (anchor) {
    case FBWL_APPS_ANCHOR_TOPLEFT:
        return "TopLeft";
    case FBWL_APPS_ANCHOR_LEFT:
        return "Left";
    case FBWL_APPS_ANCHOR_BOTTOMLEFT:
        return "BottomLeft";
    case FBWL_APPS_ANCHOR_TOP:
        return "Top";
    case FBWL_APPS_ANCHOR_CENTER:
        return "Center";
    case FBWL_APPS_ANCHOR_BOTTOM:
        return "Bottom";
    case FBWL_APPS_ANCHOR_TOPRIGHT:
        return "TopRight";
    case FBWL_APPS_ANCHOR_RIGHT:
        return "Right";
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        return "BottomRight";
    default:
        return "TopLeft";
    }
}

static const char *apps_rules_maximized_name(const struct fbwl_apps_rule *rule) {
    if (rule == NULL) {
        return "no";
    }
    if (rule->maximized_h && rule->maximized_v) {
        return "yes";
    }
    if (rule->maximized_h) {
        return "horz";
    }
    if (rule->maximized_v) {
        return "vert";
    }
    return "no";
}

static void apps_rules_write_attrs(FILE *f, const struct fbwl_apps_rule *rule, const char *indent) {
    if (f == NULL || rule == NULL) {
        return;
    }
    if (indent == NULL) {
        indent = "";
    }

    if (rule->set_focus_hidden && rule->set_icon_hidden && rule->focus_hidden == rule->icon_hidden) {
        fprintf(f, "%s[Hidden] {%s}\n", indent, rule->focus_hidden ? "yes" : "no");
    } else {
        if (rule->set_focus_hidden) {
            fprintf(f, "%s[FocusHidden] {%s}\n", indent, rule->focus_hidden ? "yes" : "no");
        }
        if (rule->set_icon_hidden) {
            fprintf(f, "%s[IconHidden] {%s}\n", indent, rule->icon_hidden ? "yes" : "no");
        }
    }

    if (rule->set_workspace) {
        fprintf(f, "%s[Workspace] {%d}\n", indent, rule->workspace);
    }
    if (rule->set_sticky) {
        fprintf(f, "%s[Sticky] {%s}\n", indent, rule->sticky ? "yes" : "no");
    }
    if (rule->set_jump) {
        fprintf(f, "%s[Jump] {%s}\n", indent, rule->jump ? "yes" : "no");
    }
    if (rule->set_head) {
        fprintf(f, "%s[Head] {%d}\n", indent, rule->head);
    }
    if (rule->set_dimensions) {
        fprintf(f, "%s[Dimensions] {", indent);
        apps_rules_write_int_with_percent(f, rule->width, rule->width_percent);
        fputc(' ', f);
        apps_rules_write_int_with_percent(f, rule->height, rule->height_percent);
        fprintf(f, "}\n");
    }
    if (rule->set_ignore_size_hints) {
        fprintf(f, "%s[IgnoreSizeHints] {%s}\n", indent, rule->ignore_size_hints ? "yes" : "no");
    }
    if (rule->set_position) {
        fprintf(f, "%s[Position] (%s) {", indent, apps_rules_anchor_name(rule->position_anchor));
        apps_rules_write_int_with_percent(f, rule->x, rule->x_percent);
        fputc(' ', f);
        apps_rules_write_int_with_percent(f, rule->y, rule->y_percent);
        fprintf(f, "}\n");
    }
    if (rule->set_minimized) {
        fprintf(f, "%s[Minimized] {%s}\n", indent, rule->minimized ? "yes" : "no");
    }
    if (rule->set_maximized) {
        fprintf(f, "%s[Maximized] {%s}\n", indent, apps_rules_maximized_name(rule));
    }
    if (rule->set_fullscreen) {
        fprintf(f, "%s[Fullscreen] {%s}\n", indent, rule->fullscreen ? "yes" : "no");
    }
    if (rule->set_shaded) {
        fprintf(f, "%s[Shaded] {%s}\n", indent, rule->shaded ? "yes" : "no");
    }
    if (rule->set_tab) {
        fprintf(f, "%s[Tab] {%s}\n", indent, rule->tab ? "yes" : "no");
    }
    if (rule->set_alpha) {
        fprintf(f, "%s[Alpha] {%d %d}\n", indent, rule->alpha_focused, rule->alpha_unfocused);
    }
    if (rule->set_focus_protection) {
        fprintf(f, "%s[FocusProtection] {", indent);
        apps_rules_write_focus_protection(f, rule->focus_protection);
        fprintf(f, "}\n");
    }
    if (rule->set_decor) {
        const char *preset = fbwl_deco_mask_preset_name(rule->decor_mask);
        if (preset != NULL) {
            fprintf(f, "%s[Deco] {%s}\n", indent, preset);
        } else {
            fprintf(f, "%s[Deco] {0x%x}\n", indent, rule->decor_mask);
        }
    }
    if (rule->set_layer) {
        fprintf(f, "%s[Layer] {%d}\n", indent, rule->layer);
    }
    if (rule->set_save_on_close) {
        fprintf(f, "%s[Close] {%s}\n", indent, rule->save_on_close ? "yes" : "no");
    }
}

bool fbwl_apps_rules_save_file(const struct fbwl_apps_rule *rules, size_t rule_count, const char *path) {
    if (rules == NULL || rule_count == 0 || path == NULL || *path == '\0') {
        return false;
    }

    char *tmp_path = malloc(strlen(path) + sizeof(".tmp.XXXXXX"));
    if (tmp_path == NULL) {
        return false;
    }
    snprintf(tmp_path, strlen(path) + sizeof(".tmp.XXXXXX"), "%s.tmp.XXXXXX", path);

    mode_t mode = 0644;
    struct stat st = {0};
    if (stat(path, &st) == 0) {
        mode = st.st_mode & 0777;
    }

    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        free(tmp_path);
        return false;
    }

    (void)fchmod(fd, mode);

    FILE *f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        unlink(tmp_path);
        free(tmp_path);
        return false;
    }

    for (size_t i = 0; i < rule_count; i++) {
        const struct fbwl_apps_rule *rule = &rules[i];

        if (rule->group_id > 0) {
            const int group_id = rule->group_id;
            fprintf(f, "[group]\n");
            while (i < rule_count && rules[i].group_id == group_id) {
                fprintf(f, "  [app]");
                apps_rules_write_match_terms(f, &rules[i]);
                if (rules[i].match_limit > 0) {
                    fprintf(f, " {%d}", rules[i].match_limit);
                }
                fputc('\n', f);
                i++;
            }
            apps_rules_write_attrs(f, rule, "  ");
            fprintf(f, "[end]\n\n");
            i--;
            continue;
        }

        fprintf(f, "[app]");
        apps_rules_write_match_terms(f, rule);
        if (rule->match_limit > 0) {
            fprintf(f, " {%d}", rule->match_limit);
        }
        fputc('\n', f);
        apps_rules_write_attrs(f, rule, "  ");
        fprintf(f, "[end]\n\n");
    }

    bool ok = fflush(f) == 0;
    if (ok) {
        ok = fsync(fd) == 0;
    }
    ok = fclose(f) == 0 && ok;

    if (!ok) {
        unlink(tmp_path);
        free(tmp_path);
        return false;
    }

    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        free(tmp_path);
        return false;
    }

    free(tmp_path);
    return true;
}
