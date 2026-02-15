#include "wayland/fbwl_ui_toolbar_build.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "wmcore/fbwm_core.h"
#include "wayland/fbwl_icon_theme.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_menu_icon.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_ui_toolbar_iconbar_pattern.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_xwayland_icon.h"

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
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static void parse_mode_options_inplace(char *s, bool *out_groups, bool *out_static, char **out_pattern) {
    if (out_groups != NULL) {
        *out_groups = false;
    }
    if (out_static != NULL) {
        *out_static = false;
    }
    if (out_pattern != NULL) {
        *out_pattern = s;
    }
    if (s == NULL) {
        return;
    }

    char *open = strchr(s, '{');
    if (open == NULL) {
        if (out_pattern != NULL) {
            *out_pattern = trim_inplace(s);
        }
        return;
    }
    char *close = strchr(open + 1, '}');
    if (close == NULL) {
        if (out_pattern != NULL) {
            *out_pattern = trim_inplace(s);
        }
        return;
    }
    *open = '\0';
    *close = '\0';

    char *opts = trim_inplace(open + 1);
    if (opts != NULL && *opts != '\0') {
        char *save = NULL;
        for (char *tok = strtok_r(opts, " \t", &save); tok != NULL; tok = strtok_r(NULL, " \t", &save)) {
            if (out_groups != NULL && strcasecmp(tok, "groups") == 0) {
                *out_groups = true;
                continue;
            }
            if (out_static != NULL && strcasecmp(tok, "static") == 0) {
                *out_static = true;
                continue;
            }
        }
    }

    if (out_pattern != NULL) {
        *out_pattern = trim_inplace(close + 1);
    }
}

static int view_create_seq_cmp(const void *a, const void *b) {
    const struct fbwl_view *av = *(const struct fbwl_view *const *)a;
    const struct fbwl_view *bv = *(const struct fbwl_view *const *)b;
    if (av == NULL || bv == NULL) {
        return 0;
    }
    if (av->create_seq < bv->create_seq) {
        return -1;
    }
    if (av->create_seq > bv->create_seq) {
        return 1;
    }
    if (av < bv) {
        return -1;
    }
    if (av > bv) {
        return 1;
    }
    return 0;
}

static char *iconbar_label_text(const struct fbwl_toolbar_ui *ui, const struct fbwl_view *view) {
    const char *t = fbwl_view_display_title(view);
    if (t == NULL) {
        t = "";
    }
    if (ui == NULL || view == NULL || !view->minimized) {
        return strdup(t);
    }

    const char *pre = ui->iconbar_iconified_prefix;
    const char *suf = ui->iconbar_iconified_suffix;
    if (pre == NULL || suf == NULL || (pre[0] == '\0' && suf[0] == '\0')) {
        return strdup(t);
    }

    int n = snprintf(NULL, 0, "%s%s%s", pre, t, suf);
    if (n <= 0) {
        return strdup(t);
    }
    size_t len = (size_t)n + 1;
    char *out = malloc(len);
    if (out == NULL) {
        return strdup(t);
    }
    snprintf(out, len, "%s%s%s", pre, t, suf);
    return out;
}

static void iconbar_alloc_fail_cleanup(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }

    free(ui->iconbar_views);
    ui->iconbar_views = NULL;
    free(ui->iconbar_texts);
    ui->iconbar_texts = NULL;
    free(ui->iconbar_item_lx);
    ui->iconbar_item_lx = NULL;
    free(ui->iconbar_item_w);
    ui->iconbar_item_w = NULL;
    ui->iconbar_bg = NULL;
    free(ui->iconbar_bgs);
    ui->iconbar_bgs = NULL;
    free(ui->iconbar_labels);
    ui->iconbar_labels = NULL;
    free(ui->iconbar_needs_tooltip);
    ui->iconbar_needs_tooltip = NULL;
    ui->iconbar_count = 0;
}

void fbwl_ui_toolbar_build_iconbar(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
        bool vertical, const float fg[4]) {
    if (ui == NULL || env == NULL || env->wm == NULL || ui->tree == NULL) {
        return;
    }
    if ((ui->tools & FBWL_TOOLBAR_TOOL_ICONBAR) == 0 || ui->iconbar_w < 1) {
        return;
    }

    char mode_buf[sizeof(ui->iconbar_mode)];
    strncpy(mode_buf, ui->iconbar_mode, sizeof(mode_buf));
    mode_buf[sizeof(mode_buf) - 1] = '\0';
    char *mode = trim_inplace(mode_buf);
    if (mode == NULL || *mode == '\0' || strcasecmp(mode, "none") == 0) {
        return;
    }

    bool groups = false;
    bool static_order = false;
    char *pattern = mode;
    parse_mode_options_inplace(mode, &groups, &static_order, &pattern);

    struct fbwl_iconbar_pattern pat = {0};
    fbwl_iconbar_pattern_parse_inplace(&pat, pattern);

    const int toolbar_head = ui->on_head >= 0 ? ui->on_head : 0;
    const int cur_ws = fbwm_core_workspace_current_for_head(env->wm, (size_t)toolbar_head);

    size_t icon_count = 0;
    for (struct fbwm_view *wm_view = env->wm->views.next; wm_view != &env->wm->views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped) {
            continue;
        }
        const int view_head = wm_view->ops != NULL && wm_view->ops->head != NULL ? wm_view->ops->head(wm_view) : 0;
        if (view_head != toolbar_head) {
            continue;
        }
        if (groups && !fbwl_tabs_view_is_active(view)) {
            continue;
        }
        if (!fbwl_iconbar_pattern_matches(&pat, env, view, cur_ws)) {
            continue;
        }
        icon_count++;
    }
    if (icon_count == 0) {
        fbwl_iconbar_pattern_free(&pat);
        return;
    }

    struct fbwl_view **selected = calloc(icon_count, sizeof(*selected));
    if (selected == NULL) {
        fbwl_iconbar_pattern_free(&pat);
        return;
    }

    size_t idx = 0;
    for (struct fbwm_view *wm_view = env->wm->views.next; wm_view != &env->wm->views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || !view->mapped) {
            continue;
        }
        const int view_head = wm_view->ops != NULL && wm_view->ops->head != NULL ? wm_view->ops->head(wm_view) : 0;
        if (view_head != toolbar_head) {
            continue;
        }
        if (groups && !fbwl_tabs_view_is_active(view)) {
            continue;
        }
        if (!fbwl_iconbar_pattern_matches(&pat, env, view, cur_ws)) {
            continue;
        }
        if (idx >= icon_count) {
            break;
        }
        selected[idx++] = view;
    }
    icon_count = idx;
    if (icon_count == 0) {
        fbwl_iconbar_pattern_free(&pat);
        free(selected);
        return;
    }
    if (static_order) {
        qsort(selected, icon_count, sizeof(*selected), view_create_seq_cmp);
    }

    ui->iconbar_views = calloc(icon_count, sizeof(*ui->iconbar_views));
    ui->iconbar_texts = calloc(icon_count, sizeof(*ui->iconbar_texts));
    ui->iconbar_item_lx = calloc(icon_count, sizeof(*ui->iconbar_item_lx));
    ui->iconbar_item_w = calloc(icon_count, sizeof(*ui->iconbar_item_w));
    ui->iconbar_bgs = calloc(icon_count, sizeof(*ui->iconbar_bgs));
    ui->iconbar_labels = calloc(icon_count, sizeof(*ui->iconbar_labels));
    ui->iconbar_needs_tooltip = calloc(icon_count, sizeof(*ui->iconbar_needs_tooltip));
    if (ui->iconbar_views == NULL || ui->iconbar_texts == NULL || ui->iconbar_item_lx == NULL || ui->iconbar_item_w == NULL ||
            ui->iconbar_bgs == NULL || ui->iconbar_labels == NULL || ui->iconbar_needs_tooltip == NULL) {
        iconbar_alloc_fail_cleanup(ui);
        fbwl_iconbar_pattern_free(&pat);
        free(selected);
        return;
    }

    ui->iconbar_count = icon_count;
    const float alpha = (float)ui->alpha / 255.0f;

    int pad = ui->iconbar_icon_text_padding_px;
    if (pad < 0) {
        pad = 0;
    }

    int icon_px = ui->thickness >= 18 ? ui->thickness - 8 : ui->thickness;
    if (icon_px < 8) {
        icon_px = 8;
    }
    if (icon_px > ui->thickness) {
        icon_px = ui->thickness;
    }
    if (icon_px > 64) {
        icon_px = 64;
    }

    enum fbwl_iconbar_alignment align = ui->iconbar_alignment;

    const int cross = ui->border_w + ui->bevel_w;
    const int bg_w = vertical ? ui->thickness : ui->iconbar_w;
    const int bg_h = vertical ? ui->iconbar_w : ui->thickness;
    ui->iconbar_bg = wlr_scene_buffer_create(ui->tree, NULL);
    if (ui->iconbar_bg != NULL) {
        wlr_scene_node_set_position(&ui->iconbar_bg->node, vertical ? cross : ui->iconbar_x, vertical ? ui->iconbar_x : cross);
        const struct fbwl_texture *tex = env->decor_theme != NULL ? &env->decor_theme->toolbar_iconbar_empty_tex : NULL;
        const bool parentrel = fbwl_texture_is_parentrelative(tex);
        if (tex != NULL && !parentrel) {
            struct wlr_buffer *buf = fbwl_texture_render_buffer(tex, bg_w > 0 ? bg_w : 1, bg_h > 0 ? bg_h : 1);
            wlr_scene_buffer_set_buffer(ui->iconbar_bg, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(ui->iconbar_bg, bg_w > 0 ? bg_w : 1, bg_h > 0 ? bg_h : 1);
            wlr_scene_buffer_set_opacity(ui->iconbar_bg, alpha);
        } else {
            wlr_scene_node_set_enabled(&ui->iconbar_bg->node, false);
        }
    }
    int xoff = ui->iconbar_x;
    int base_w = 0;
    int rem = 0;
    unsigned int *smart_demands = NULL;
    unsigned int smart_total = 0;
    int smart_rounding_error = 0;
    if (align == FBWL_ICONBAR_ALIGN_RELATIVE) {
        base_w = ui->iconbar_w / (int)icon_count;
        rem = ui->iconbar_w % (int)icon_count;
    } else if (align == FBWL_ICONBAR_ALIGN_RELATIVE_SMART) {
        smart_demands = calloc(icon_count, sizeof(*smart_demands));
        if (smart_demands != NULL) {
            for (size_t i = 0; i < icon_count; i++) {
                struct fbwl_view *view = selected[i];
                char *label_text = view != NULL ? iconbar_label_text(ui, view) : NULL;

                int text_w = 0;
                (void)fbwl_text_measure(label_text != NULL ? label_text : "", ui->thickness, ui->font, &text_w, NULL);
                free(label_text);

                unsigned int demand = (unsigned int)(text_w > 0 ? text_w : 0);
                demand += (unsigned int)(2 * pad);
                if (ui->iconbar_use_pixmap) {
                    demand += (unsigned int)(icon_px + pad);
                }
                if (demand < 1) {
                    demand = 1;
                }
                smart_demands[i] = demand;
                smart_total += demand;
            }

            const unsigned int total_width = (unsigned int)ui->iconbar_w;
            if (smart_total > 0 && total_width > 0) {
                long overhead = (long)smart_total - (long)total_width;
                if (overhead > (long)icon_count) {
                    overhead += (long)icon_count; // compensate for rounding errors
                    const unsigned int mean = smart_total / (unsigned int)icon_count;
                    const unsigned int thresh = 3u * mean / 2u;
                    unsigned long greed = 0;
                    for (size_t i = 0; i < icon_count; i++) {
                        if (smart_demands[i] > thresh) {
                            greed += smart_demands[i];
                        }
                    }
                    if (greed > 0) {
                        for (size_t i = 0; i < icon_count; i++) {
                            if (smart_demands[i] <= thresh) {
                                continue;
                            }
                            unsigned long d = (unsigned long)smart_demands[i] * (unsigned long)overhead / greed;
                            if (smart_demands[i] > mean + d) {
                                smart_demands[i] -= (unsigned int)d;
                            } else {
                                d = smart_demands[i] > mean ? (unsigned long)(smart_demands[i] - mean) : 0ul;
                                smart_demands[i] = mean;
                            }
                            if (d > 0 && d <= smart_total) {
                                smart_total -= (unsigned int)d;
                            }
                        }
                    }
                }

                if (smart_total > 0) {
                    smart_rounding_error = (int)total_width;
                    for (size_t i = 0; i < icon_count; i++) {
                        smart_rounding_error -= (int)((unsigned long long)smart_demands[i] * (unsigned long long)total_width /
                            (unsigned long long)smart_total);
                    }
                    if (smart_rounding_error < 0) {
                        smart_rounding_error = 0;
                    }
                }
            }
        }
        if (smart_demands == NULL || smart_total == 0) {
            align = FBWL_ICONBAR_ALIGN_RELATIVE;
            base_w = ui->iconbar_w / (int)icon_count;
            rem = ui->iconbar_w % (int)icon_count;
        }
    } else if (align == FBWL_ICONBAR_ALIGN_LEFT || align == FBWL_ICONBAR_ALIGN_RIGHT) {
        int iw = ui->iconbar_icon_width_px > 0 ? ui->iconbar_icon_width_px : 128;
        if (iw < 1) {
            iw = 1;
        }
        int share = ui->iconbar_w / (int)icon_count;
        if (share < 1) {
            share = 1;
        }
        if (iw > share) {
            iw = share;
        }
        const int total = iw * (int)icon_count;
        const int leftover = ui->iconbar_w - total;
        if (align == FBWL_ICONBAR_ALIGN_RIGHT && leftover > 0) {
            xoff += leftover;
        }
        base_w = iw;
        rem = 0;
    }

    for (size_t i = 0; i < icon_count; i++) {
        struct fbwl_view *view = selected[i];
        if (view == NULL) {
            continue;
        }

        int extra = 0;
        if (align == FBWL_ICONBAR_ALIGN_RELATIVE_SMART && smart_rounding_error > 0) {
            smart_rounding_error--;
            extra = 1;
        }

        int iw = base_w + ((int)i < rem ? 1 : 0);
        if (align == FBWL_ICONBAR_ALIGN_RELATIVE_SMART && smart_total > 0 && smart_demands != NULL) {
            const unsigned int total_width = (unsigned int)ui->iconbar_w;
            iw = (int)((unsigned long long)smart_demands[i] * (unsigned long long)total_width /
                (unsigned long long)smart_total);
            iw += extra;
        }
        if (iw < 1) {
            iw = 1;
        }

        ui->iconbar_views[i] = view;
        ui->iconbar_item_lx[i] = xoff;
        ui->iconbar_item_w[i] = iw;

        const int w = vertical ? ui->thickness : iw;
        const int h = vertical ? iw : ui->thickness;
        const int base_x = vertical ? cross : xoff;
        const int base_y = vertical ? xoff : cross;

        ui->iconbar_bgs[i] = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->iconbar_bgs[i] != NULL) {
            wlr_scene_node_set_position(&ui->iconbar_bgs[i]->node, base_x, base_y);
            const bool urgent = fbwl_view_is_urgent(view);
            const bool focused_or_urgent = view == env->focused_view || urgent;
            const struct fbwl_texture *tex = env->decor_theme != NULL
                ? (focused_or_urgent ? &env->decor_theme->toolbar_iconbar_focused_tex : &env->decor_theme->toolbar_iconbar_unfocused_tex)
                : NULL;
            const bool parentrel = fbwl_texture_is_parentrelative(tex);
            if (tex != NULL && !parentrel) {
                struct wlr_buffer *buf = fbwl_texture_render_buffer(tex, w > 0 ? w : 1, h > 0 ? h : 1);
                wlr_scene_buffer_set_buffer(ui->iconbar_bgs[i], buf);
                if (buf != NULL) {
                    wlr_buffer_drop(buf);
                }
                wlr_scene_buffer_set_dest_size(ui->iconbar_bgs[i], w > 0 ? w : 1, h > 0 ? h : 1);
                wlr_scene_buffer_set_opacity(ui->iconbar_bgs[i], alpha);
            } else {
                wlr_scene_node_set_enabled(&ui->iconbar_bgs[i]->node, false);
            }
        }

        char *label_text = iconbar_label_text(ui, view);
        if (label_text == NULL) {
            label_text = strdup(fbwl_view_display_title(view));
        }
        ui->iconbar_texts[i] = label_text;

        bool icon_loaded = false;
        int text_x = base_x;
        int text_w = w;

        if (ui->iconbar_use_pixmap) {
            struct wlr_buffer *icon_buf = NULL;

            if (view->type == FBWL_VIEW_XWAYLAND && env != NULL && env->xwayland != NULL) {
                icon_buf = fbwl_xwayland_icon_buffer_create(env->xwayland, view->xwayland_surface, icon_px);
            }

            if (icon_buf == NULL) {
                const char *icon_name = fbwl_view_app_id(view);
                char *icon_path = fbwl_icon_theme_resolve_path(icon_name);
                if (icon_path != NULL) {
                    icon_buf = fbwl_ui_menu_icon_buffer_create(icon_path, icon_px);
                    free(icon_path);
                }
            }

            if (icon_buf != NULL) {
                struct wlr_scene_buffer *sb_icon = wlr_scene_buffer_create(ui->tree, icon_buf);
                if (sb_icon != NULL) {
                    const int ix = base_x + pad;
                    const int iy = base_y + (h > icon_px ? (h - icon_px) / 2 : 0);
                    wlr_scene_node_set_position(&sb_icon->node, ix, iy);
                    icon_loaded = true;
                }
                wlr_buffer_drop(icon_buf);
            }
        }

        if (icon_loaded) {
            text_x = base_x + pad + icon_px;
            text_w = w - (pad + icon_px);
            if (text_w < 1) {
                text_w = 1;
            }
        }

        const struct fbwl_text_effect *effect = NULL;
        int justify = 0;
        if (env != NULL && env->decor_theme != NULL) {
            const bool urgent = view != NULL && fbwl_view_is_urgent(view);
            const bool focused_or_urgent = view != NULL && (view == env->focused_view || urgent);
            effect = focused_or_urgent ? &env->decor_theme->toolbar_iconbar_focused_effect : &env->decor_theme->toolbar_iconbar_unfocused_effect;
            justify = focused_or_urgent ? env->decor_theme->toolbar_iconbar_focused_justify : env->decor_theme->toolbar_iconbar_unfocused_justify;
        }
        struct wlr_buffer *buf = fbwl_text_buffer_create(label_text != NULL ? label_text : "", text_w, h, pad, fg, ui->font, effect, justify);
        if (buf != NULL) {
            struct wlr_scene_buffer *sb = wlr_scene_buffer_create(ui->tree, buf);
            if (sb != NULL) {
                wlr_scene_node_set_position(&sb->node, text_x, base_y);
                ui->iconbar_labels[i] = sb;
            }
            wlr_buffer_drop(buf);
        }

        ui->iconbar_needs_tooltip[i] = !fbwl_text_fits(label_text, text_w, h, pad, ui->font);

        wlr_log(WLR_INFO, "Toolbar: iconbar item idx=%zu lx=%d w=%d title=%s minimized=%d label=%s icon=%d",
            i, xoff, iw, fbwl_view_display_title(view), view->minimized ? 1 : 0,
            label_text != NULL ? label_text : "", icon_loaded ? 1 : 0);

        xoff += iw;
    }

    fbwl_iconbar_pattern_free(&pat);
    free(smart_demands);
    free(selected);
}
