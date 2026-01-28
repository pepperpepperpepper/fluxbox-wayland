#include "wayland/fbwl_apps_remember.h"

#include <stdbool.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_apps_rules.h"
#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"

static const char *anchor_name(enum fbwl_apps_rule_anchor anchor) {
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

static bool anchor_is_right(enum fbwl_apps_rule_anchor anchor) {
    switch (anchor) {
    case FBWL_APPS_ANCHOR_TOPRIGHT:
    case FBWL_APPS_ANCHOR_RIGHT:
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        return true;
    default:
        return false;
    }
}

static bool anchor_is_bottom(enum fbwl_apps_rule_anchor anchor) {
    switch (anchor) {
    case FBWL_APPS_ANCHOR_BOTTOMLEFT:
    case FBWL_APPS_ANCHOR_BOTTOM:
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        return true;
    default:
        return false;
    }
}

static void anchor_screen_ref(const struct wlr_box *box, enum fbwl_apps_rule_anchor anchor,
        int *out_x, int *out_y) {
    if (out_x != NULL) {
        *out_x = 0;
    }
    if (out_y != NULL) {
        *out_y = 0;
    }
    if (box == NULL || out_x == NULL || out_y == NULL) {
        return;
    }

    const int left = box->x;
    const int right = box->x + box->width;
    const int top = box->y;
    const int bottom = box->y + box->height;
    const int cx = box->x + box->width / 2;
    const int cy = box->y + box->height / 2;

    switch (anchor) {
    case FBWL_APPS_ANCHOR_TOPLEFT:
        *out_x = left;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_LEFT:
        *out_x = left;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOMLEFT:
        *out_x = left;
        *out_y = bottom;
        return;
    case FBWL_APPS_ANCHOR_TOP:
        *out_x = cx;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_CENTER:
        *out_x = cx;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOM:
        *out_x = cx;
        *out_y = bottom;
        return;
    case FBWL_APPS_ANCHOR_TOPRIGHT:
        *out_x = right;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_RIGHT:
        *out_x = right;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        *out_x = right;
        *out_y = bottom;
        return;
    default:
        *out_x = left;
        *out_y = top;
        return;
    }
}

static void anchor_window_ref(int w, int h, enum fbwl_apps_rule_anchor anchor, int *out_x, int *out_y) {
    if (out_x != NULL) {
        *out_x = 0;
    }
    if (out_y != NULL) {
        *out_y = 0;
    }
    if (out_x == NULL || out_y == NULL) {
        return;
    }

    const int left = 0;
    const int right = w;
    const int top = 0;
    const int bottom = h;
    const int cx = w / 2;
    const int cy = h / 2;

    switch (anchor) {
    case FBWL_APPS_ANCHOR_TOPLEFT:
        *out_x = left;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_LEFT:
        *out_x = left;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOMLEFT:
        *out_x = left;
        *out_y = bottom;
        return;
    case FBWL_APPS_ANCHOR_TOP:
        *out_x = cx;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_CENTER:
        *out_x = cx;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOM:
        *out_x = cx;
        *out_y = bottom;
        return;
    case FBWL_APPS_ANCHOR_TOPRIGHT:
        *out_x = right;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_RIGHT:
        *out_x = right;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        *out_x = right;
        *out_y = bottom;
        return;
    default:
        *out_x = left;
        *out_y = top;
        return;
    }
}

static struct wlr_output *output_at_cursor(const struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return NULL;
    }

    double x = 0.0;
    double y = 0.0;
    if (server->cursor != NULL) {
        x = server->cursor->x;
        y = server->cursor->y;
    }

    struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, x, y);
    if (out != NULL) {
        return out;
    }
    return wlr_output_layout_get_center_output(server->output_layout);
}

static struct wlr_output *output_for_head(const struct fbwl_server *server, int head) {
    if (server == NULL || server->output_layout == NULL) {
        return NULL;
    }
    if (head < 0) {
        return NULL;
    }

    const size_t n = fbwl_screen_map_count(server->output_layout, &server->outputs);
    if (n == 0) {
        return NULL;
    }
    if ((size_t)head >= n) {
        return NULL;
    }

    return fbwl_screen_map_output_for_screen(server->output_layout, &server->outputs, (size_t)head);
}

static int scale_percent(int value, int base) {
    return (int)((long)value * (long)base / 100L);
}

void fbwl_apps_remember_apply_pre_map(struct fbwl_view *view, const struct fbwl_apps_rule *rule) {
    if (view == NULL || view->server == NULL || rule == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;
    if (server->output_layout == NULL) {
        return;
    }

    struct wlr_output *preferred_out = NULL;
    if (rule->set_head) {
        preferred_out = output_for_head(server, rule->head);
        if (preferred_out == NULL) {
            wlr_log(WLR_ERROR, "Apps: ignoring out-of-range head=%d for %s",
                rule->head, fbwl_view_display_title(view));
        }
    }
    if (preferred_out == NULL) {
        preferred_out = output_at_cursor(server);
    }

    struct wlr_box screen = {0};
    fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, preferred_out, &screen);
    if (screen.width < 1 || screen.height < 1) {
        fbwl_view_get_output_box(view, server->output_layout, preferred_out, &screen);
    }
    if (screen.width < 1 || screen.height < 1) {
        return;
    }

    int desired_w = fbwl_view_current_width(view);
    int desired_h = fbwl_view_current_height(view);

    if (rule->set_dimensions) {
        int w = rule->width_percent ? scale_percent(rule->width, screen.width) : rule->width;
        int h = rule->height_percent ? scale_percent(rule->height, screen.height) : rule->height;
        if (w > 0) {
            desired_w = w;
        }
        if (h > 0) {
            desired_h = h;
        }

        if (desired_w > 0 && desired_h > 0) {
            if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
                wlr_xdg_toplevel_set_size(view->xdg_toplevel, desired_w, desired_h);
            } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
                view->width = desired_w;
                view->height = desired_h;
            }
        }
    }

    int frame_left = 0;
    int frame_top = 0;
    int frame_w = desired_w;
    int frame_h = desired_h;
    if (view->decor_enabled && !view->fullscreen) {
        const struct fbwl_decor_theme *theme = &server->decor_theme;
        const int border = theme->border_width;
        const int title_h = theme->title_height;
        frame_left = border;
        frame_top = title_h + border;
        frame_w += 2 * border;
        frame_h += title_h + 2 * border;
    }

    if (rule->set_position && desired_w > 0 && desired_h > 0) {
        int x_off = rule->x_percent ? scale_percent(rule->x, screen.width) : rule->x;
        int y_off = rule->y_percent ? scale_percent(rule->y, screen.height) : rule->y;

        const enum fbwl_apps_rule_anchor anchor = rule->position_anchor;
        if (anchor_is_right(anchor)) {
            x_off = -x_off;
        }
        if (anchor_is_bottom(anchor)) {
            y_off = -y_off;
        }

        int sx = 0;
        int sy = 0;
        anchor_screen_ref(&screen, anchor, &sx, &sy);

        int wx = 0;
        int wy = 0;
        anchor_window_ref(frame_w, frame_h, anchor, &wx, &wy);

        const int frame_x = sx + x_off - wx;
        const int frame_y = sy + y_off - wy;
        view->x = frame_x + frame_left;
        view->y = frame_y + frame_top;
        if (view->scene_tree != NULL) {
            wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        }
        view->placed = true;
        fbwl_view_foreign_update_output_from_position(view, server->output_layout);

        wlr_log(WLR_INFO, "Apps: remember position title=%s app_id=%s anchor=%s frame=%d,%d content=%d,%d size=%dx%d out=%s",
            fbwl_view_display_title(view),
            fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)",
            anchor_name(anchor),
            frame_x, frame_y,
            view->x, view->y,
            desired_w, desired_h,
            preferred_out != NULL && preferred_out->name != NULL ? preferred_out->name : "(none)");
        return;
    }

    if (rule->set_head && !view->placed && preferred_out != NULL) {
        const double px = (double)screen.x + (double)screen.width / 2.0;
        const double py = (double)screen.y + (double)screen.height / 2.0;
        fbwl_view_place_initial(view, &server->wm, server->output_layout, &server->outputs, px, py);
        view->placed = true;
        wlr_log(WLR_INFO, "Apps: remember head=%d placed title=%s out=%s",
            rule->head,
            fbwl_view_display_title(view),
            preferred_out->name != NULL ? preferred_out->name : "(unnamed)");
        return;
    }

    if (rule->set_dimensions) {
        wlr_log(WLR_INFO, "Apps: remember dimensions title=%s app_id=%s w=%d%s h=%d%s",
            fbwl_view_display_title(view),
            fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)",
            rule->width,
            rule->width_percent ? "%" : "",
            rule->height,
            rule->height_percent ? "%" : "");
    }
}

