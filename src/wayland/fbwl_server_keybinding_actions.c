#include "wayland/fbwl_server_keybinding_actions.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_deco_mask.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_foreign_toplevel.h"

static char *trim_inplace(char *s);

static xcb_atom_t intern_atom(xcb_connection_t *conn, const char *name) {
    if (conn == NULL || name == NULL || *name == '\0') {
        return XCB_ATOM_NONE;
    }
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return XCB_ATOM_NONE;
    }
    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

void server_keybindings_view_toggle_maximize_horizontal(void *userdata, struct fbwl_view *view) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->output_layout == NULL) {
        return;
    }

    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);

    if (view->fullscreen) {
        fbwl_view_set_fullscreen(view, false, server->output_layout, &server->outputs,
            server->layer_normal, server->layer_fullscreen, NULL);
    }
    if (view->maximized && (!view->maximized_h || !view->maximized_v)) {
        view->maximized_h = true;
        view->maximized_v = true;
    }

    const int cur_h = fbwl_view_current_height(view);
    const int cur_w = fbwl_view_current_width(view);
    if (cur_w < 1 || cur_h < 1) {
        return;
    }

    const bool on = !view->maximized_h;
    if (on && !view->maximized && !view->maximized_h && !view->maximized_v) {
        fbwl_view_save_geometry(view);
    }

    struct wlr_box box = {0};
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool full_max = cfg != NULL ? cfg->full_maximization : server->full_maximization;
    if (full_max) {
        fbwl_view_get_output_box(view, server->output_layout, NULL, &box);
    } else {
        fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, NULL, &box);
    }
    fbwl_view_apply_tabs_maxover_box(view, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    int x = view->x;
    int y = view->y;
    int w = cur_w;
    int h = cur_h;

    if (on) {
        x = box.x;
        w = box.width;
    } else {
        x = view->saved_x;
        w = view->saved_w > 0 ? view->saved_w : cur_w;
    }

    if (on && view->decor_enabled) {
        const int border = server->decor_theme.border_width;
        x += border;
        w -= 2 * border;
    }
    if (!on && !view->maximized_v) {
        y = view->saved_y;
        h = view->saved_h > 0 ? view->saved_h : cur_h;
    }
    if (w < 1 || h < 1) {
        return;
    }

    view->maximized_h = on;
    view->maximized = view->maximized_h && view->maximized_v;
    view->x = x;
    view->y = y;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
    fbwl_view_pseudo_bg_update(view, on ? "maximize-h-on" : "maximize-h-off");
    if (view->type == FBWL_VIEW_XDG) {
        wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, view->maximized);
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
    } else if (view->type == FBWL_VIEW_XWAYLAND) {
        wlr_xwayland_surface_set_maximized(view->xwayland_surface, view->maximized_h, view->maximized_v);
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
            (uint16_t)w, (uint16_t)h);
    }
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, view->maximized);
    }

    fbwl_tabs_sync_geometry_from_view(view, true, w, h, on ? "maximize-h-on" : "maximize-h-off");
    fbwl_view_foreign_update_output_from_position(view, server->output_layout);
    wlr_log(WLR_INFO, "MaximizeHorizontal: %s %s w=%d h=%d", fbwl_view_display_title(view),
        on ? "on" : "off", w, h);
    server_strict_mousefocus_recheck_after_restack(server, before, on ? "maximize-h-on" : "maximize-h-off");
    server_toolbar_ui_rebuild(server);
}

void server_keybindings_view_toggle_maximize_vertical(void *userdata, struct fbwl_view *view) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->output_layout == NULL) {
        return;
    }

    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);

    if (view->fullscreen) {
        fbwl_view_set_fullscreen(view, false, server->output_layout, &server->outputs,
            server->layer_normal, server->layer_fullscreen, NULL);
    }
    if (view->maximized && (!view->maximized_h || !view->maximized_v)) {
        view->maximized_h = true;
        view->maximized_v = true;
    }

    const int cur_h = fbwl_view_current_height(view);
    const int cur_w = fbwl_view_current_width(view);
    if (cur_w < 1 || cur_h < 1) {
        return;
    }

    const bool on = !view->maximized_v;
    if (on && !view->maximized && !view->maximized_h && !view->maximized_v) {
        fbwl_view_save_geometry(view);
    }

    struct wlr_box box = {0};
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool full_max = cfg != NULL ? cfg->full_maximization : server->full_maximization;
    if (full_max) {
        fbwl_view_get_output_box(view, server->output_layout, NULL, &box);
    } else {
        fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, NULL, &box);
    }
    fbwl_view_apply_tabs_maxover_box(view, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    int x = view->x;
    int y = view->y;
    int w = cur_w;
    int h = cur_h;

    if (on) {
        y = box.y;
        h = box.height;
    } else {
        y = view->saved_y;
        h = view->saved_h > 0 ? view->saved_h : cur_h;
    }

    if (on && view->decor_enabled) {
        const int border = server->decor_theme.border_width;
        const int title_h = server->decor_theme.title_height;
        y += title_h + border;
        h -= title_h + 2 * border;
    }
    if (!on && !view->maximized_h) {
        x = view->saved_x;
        w = view->saved_w > 0 ? view->saved_w : cur_w;
    }
    if (w < 1 || h < 1) {
        return;
    }

    view->maximized_v = on;
    view->maximized = view->maximized_h && view->maximized_v;
    view->x = x;
    view->y = y;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
    fbwl_view_pseudo_bg_update(view, on ? "maximize-v-on" : "maximize-v-off");
    if (view->type == FBWL_VIEW_XDG) {
        wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, view->maximized);
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
    } else if (view->type == FBWL_VIEW_XWAYLAND) {
        wlr_xwayland_surface_set_maximized(view->xwayland_surface, view->maximized_h, view->maximized_v);
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
            (uint16_t)w, (uint16_t)h);
    }
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_maximized(view->foreign_toplevel, view->maximized);
    }

    fbwl_tabs_sync_geometry_from_view(view, true, w, h, on ? "maximize-v-on" : "maximize-v-off");
    fbwl_view_foreign_update_output_from_position(view, server->output_layout);
    wlr_log(WLR_INFO, "MaximizeVertical: %s %s w=%d h=%d", fbwl_view_display_title(view),
        on ? "on" : "off", w, h);
    server_strict_mousefocus_recheck_after_restack(server, before, on ? "maximize-v-on" : "maximize-v-off");
    server_toolbar_ui_rebuild(server);
}

static struct wlr_scene_tree *layer_fallback(struct fbwl_server *server) {
    if (server == NULL) {
        return NULL;
    }
    if (server->layer_normal != NULL) {
        return server->layer_normal;
    }
    if (server->scene != NULL) {
        return &server->scene->tree;
    }
    return NULL;
}

static struct wlr_scene_tree *layer_tree_for_value(struct fbwl_server *server, int layer) {
    if (server == NULL) {
        return NULL;
    }

    struct wlr_scene_tree *fallback = layer_fallback(server);
    if (fallback == NULL) {
        return NULL;
    }

    if (layer <= 0) {
        return server->layer_overlay != NULL ? server->layer_overlay : fallback;
    }
    if (layer <= 6) {
        return server->layer_top != NULL ? server->layer_top : fallback;
    }
    if (layer <= 8) {
        return server->layer_normal != NULL ? server->layer_normal : fallback;
    }
    if (layer <= 10) {
        return server->layer_bottom != NULL ? server->layer_bottom : fallback;
    }
    return server->layer_background != NULL ? server->layer_background : fallback;
}

static int layer_category(const struct fbwl_server *server, const struct fbwl_view *view) {
    if (server == NULL || view == NULL) {
        return 8;
    }
    struct wlr_scene_tree *layer = view->base_layer != NULL ? view->base_layer : server->layer_normal;
    if (layer == server->layer_top) {
        return 6;
    }
    if (layer == server->layer_bottom) {
        return 10;
    }
    if (layer == server->layer_background) {
        return 12;
    }
    return 8;
}

void server_keybindings_view_set_layer(void *userdata, struct fbwl_view *view, int layer) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL || view->scene_tree == NULL) {
        return;
    }

    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);

    struct wlr_scene_tree *tree = layer_tree_for_value(server, layer);
    if (tree == NULL) {
        return;
    }

    view->base_layer = tree;
    if (!view->fullscreen) {
        wlr_scene_node_reparent(&view->scene_tree->node, tree);
    }
    wlr_log(WLR_INFO, "Layer: %s set=%d reason=keybinding", fbwl_view_display_title(view), layer);
    if (!view->fullscreen) {
        server_strict_mousefocus_recheck_after_restack(server, before, "set-layer");
    }
}

void server_keybindings_view_raise_layer(void *userdata, struct fbwl_view *view) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    const int cur = layer_category(server, view);
    const int next = cur >= 12 ? 10 : (cur >= 10 ? 8 : 6);
    server_keybindings_view_set_layer(server, view, next);
}

void server_keybindings_view_lower_layer(void *userdata, struct fbwl_view *view) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    const int cur = layer_category(server, view);
    const int next = cur <= 6 ? 8 : (cur <= 8 ? 10 : 12);
    server_keybindings_view_set_layer(server, view, next);
}

void server_keybindings_view_set_xprop(void *userdata, struct fbwl_view *view, const char *name, const char *value) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL || name == NULL || *name == '\0') {
        return;
    }
    if (server->xwayland == NULL || view->type != FBWL_VIEW_XWAYLAND || view->xwayland_surface == NULL) {
        return;
    }

    xcb_connection_t *conn = wlr_xwayland_get_xwm_connection(server->xwayland);
    if (conn == NULL) {
        return;
    }

    xcb_atom_t prop = intern_atom(conn, name);
    xcb_atom_t utf8 = intern_atom(conn, "UTF8_STRING");
    if (prop == XCB_ATOM_NONE || utf8 == XCB_ATOM_NONE) {
        return;
    }

    const char *v = value != NULL ? value : "";
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, view->xwayland_surface->window_id, prop, utf8, 8,
        (uint32_t)strlen(v), v);
    xcb_flush(conn);
    wlr_log(WLR_INFO, "SetXProp: %s prop=%s len=%zu", fbwl_view_display_title(view), name, strlen(v));
}

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static const char *trim_ws(const char *s) {
    if (s == NULL) {
        return "";
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static char *strip_braces_dup(const char *s) {
    const char *use = trim_ws(s);
    size_t len = strlen(use);
    if (len >= 2 && use[0] == '{' && use[len - 1] == '}') {
        return strndup(use + 1, len - 2);
    }
    return strdup(use);
}

static bool parse_alpha_token(const char *tok, bool *out_relative, int *out_value) {
    if (tok == NULL || out_relative == NULL || out_value == NULL) {
        return false;
    }
    if (*tok == '\0') {
        return false;
    }

    bool relative = false;
    int sign = 1;
    const char *p = tok;
    if (*p == '+' || *p == '-') {
        relative = true;
        if (*p == '-') {
            sign = -1;
        }
        p++;
    }
    if (*p == '\0') {
        return false;
    }

    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p || end == NULL || *end != '\0') {
        return false;
    }
    if (v < -100000 || v > 100000) {
        return false;
    }

    *out_relative = relative;
    *out_value = relative ? (int)(sign * v) : (int)v;
    return true;
}

void server_keybindings_view_set_alpha_cmd(void *userdata, struct fbwl_view *view, const char *args) {
    struct fbwl_server *server = userdata;
    if (view == NULL) {
        return;
    }

    uint8_t def_f = 255;
    uint8_t def_u = 255;
    if (server != NULL && server->window_alpha_defaults_configured) {
        def_f = server->window_alpha_default_focused;
        def_u = server->window_alpha_default_unfocused;
    }

    const char *s = trim_ws(args);
    if (*s == '\0') {
        fbwl_view_set_alpha(view, def_f, def_u, "setalpha-default");
        view->alpha_is_default = true;
        return;
    }

    char *copy = strdup(s);
    if (copy == NULL) {
        return;
    }

    char *saveptr = NULL;
    const char *tok1 = strtok_r(copy, " \t\r\n", &saveptr);
    const char *tok2 = strtok_r(NULL, " \t\r\n", &saveptr);
    const char *tok3 = strtok_r(NULL, " \t\r\n", &saveptr);
    if (tok1 == NULL || tok3 != NULL) {
        wlr_log(WLR_ERROR, "SetAlpha: invalid args (expected 0-2 values): %s", s);
        free(copy);
        return;
    }

    const int base_f = view->alpha_set ? (int)view->alpha_focused : (int)def_f;
    const int base_u = view->alpha_set ? (int)view->alpha_unfocused : (int)def_u;

    bool rel1 = false;
    int val1 = 0;
    if (!parse_alpha_token(tok1, &rel1, &val1)) {
        wlr_log(WLR_ERROR, "SetAlpha: invalid token: %s", tok1);
        free(copy);
        return;
    }

    int out_f = 0;
    int out_u = 0;
    if (tok2 == NULL) {
        out_f = rel1 ? base_f + val1 : val1;
        out_u = rel1 ? base_u + val1 : val1;
    } else {
        bool rel2 = false;
        int val2 = 0;
        if (!parse_alpha_token(tok2, &rel2, &val2)) {
            wlr_log(WLR_ERROR, "SetAlpha: invalid token: %s", tok2);
            free(copy);
            return;
        }
        out_f = rel1 ? base_f + val1 : val1;
        out_u = rel2 ? base_u + val2 : val2;
    }

    out_f = clamp_i(out_f, 0, 255);
    out_u = clamp_i(out_u, 0, 255);

    fbwl_view_set_alpha(view, (uint8_t)out_f, (uint8_t)out_u, "setalpha");
    view->alpha_is_default = false;
    free(copy);
}

void server_keybindings_view_toggle_decor(void *userdata, struct fbwl_view *view) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }

    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);
    const bool enable = !view->decor_enabled;
    view->decor_forced = true;
    fbwl_view_decor_set_enabled(view, enable);
    fbwl_view_decor_update(view, &server->decor_theme);

    wlr_log(WLR_INFO, "ToggleDecor: %s %s reason=keybinding", fbwl_view_display_title(view), enable ? "on" : "off");
    server_strict_mousefocus_recheck_after_restack(server, before, enable ? "decor-on" : "decor-off");
    server_toolbar_ui_rebuild(server);
}

void server_keybindings_view_set_decor(void *userdata, struct fbwl_view *view, const char *value) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }

    const char *s = trim_ws(value);
    if (*s == '\0') {
        wlr_log(WLR_ERROR, "SetDecor: missing value");
        return;
    }

    char *norm = strip_braces_dup(s);
    if (norm == NULL) {
        return;
    }

    const char *v = trim_ws(norm);

    uint32_t mask = FBWL_DECOR_NORMAL;
    if (!fbwl_deco_mask_parse(v, &mask)) {
        wlr_log(WLR_ERROR, "SetDecor: invalid value: %s", v);
        free(norm);
        return;
    }

    const bool enable = fbwl_deco_mask_has_frame(mask);
    const char *preset = fbwl_deco_mask_preset_name(mask);

    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);
    view->decor_forced = true;
    view->decor_mask = mask;
    fbwl_view_decor_set_enabled(view, enable);
    fbwl_view_decor_update(view, &server->decor_theme);

    wlr_log(WLR_INFO, "SetDecor: %s value=%s enabled=%d mask=0x%08x preset=%s reason=keybinding",
        fbwl_view_display_title(view), v, enable ? 1 : 0, mask, preset != NULL ? preset : "(custom)");
    server_strict_mousefocus_recheck_after_restack(server, before, enable ? "decor-on" : "decor-off");
    server_toolbar_ui_rebuild(server);

    free(norm);
}

static struct fbwl_view *find_view_by_create_seq(struct fbwl_server *server, uint64_t create_seq) {
    if (server == NULL || create_seq == 0) {
        return NULL;
    }
    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view != NULL && view->create_seq == create_seq) {
            return view;
        }
    }
    return NULL;
}

static void view_set_title_override(struct fbwl_server *server, struct fbwl_view *view, const char *text,
        const char *why) {
    if (server == NULL || view == NULL) {
        return;
    }

    const char *use = text != NULL ? text : "";
    use = trim_ws(use);

    char *dup = strip_braces_dup(use);
    if (dup == NULL) {
        wlr_log(WLR_ERROR, "Title: OOM");
        return;
    }

    const char *final = trim_ws(dup);
    if (*final == '\0') {
        free(view->title_override);
        view->title_override = NULL;
        fbwl_view_foreign_toplevel_set_title(view, fbwl_view_title(view));
        fbwl_view_decor_update_title_text(view, &server->decor_theme);
        server_toolbar_ui_rebuild(server);
        wlr_log(WLR_INFO, "Title: cleared title override create_seq=%llu reason=%s",
            (unsigned long long)view->create_seq,
            why != NULL ? why : "(null)");
        free(dup);
        return;
    }

    char *keep = strdup(final);
    free(dup);
    if (keep == NULL) {
        wlr_log(WLR_ERROR, "Title: OOM");
        return;
    }
    free(view->title_override);
    view->title_override = keep;
    fbwl_view_foreign_toplevel_set_title(view, fbwl_view_title(view));
    fbwl_view_decor_update_title_text(view, &server->decor_theme);
    server_toolbar_ui_rebuild(server);
    wlr_log(WLR_INFO, "Title: set title override create_seq=%llu title=%s reason=%s",
        (unsigned long long)view->create_seq,
        fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(null)",
        why != NULL ? why : "(null)");
}

void server_keybindings_view_set_title(void *userdata, struct fbwl_view *view, const char *title) {
    struct fbwl_server *server = userdata;
    view_set_title_override(server, view, title, "keybinding");
}

static bool cmd_dialog_submit_set_title(void *userdata, const char *text) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return true;
    }

    const uint64_t seq = server->cmd_dialog_target_create_seq;
    if (seq == 0) {
        wlr_log(WLR_ERROR, "Title: set-title-dialog missing target create_seq");
        return true;
    }

    struct fbwl_view *view = find_view_by_create_seq(server, seq);
    if (view == NULL) {
        wlr_log(WLR_ERROR, "Title: set-title-dialog no match create_seq=%llu",
            (unsigned long long)seq);
        server->cmd_dialog_target_create_seq = 0;
        return true;
    }

    view_set_title_override(server, view, text, "set-title-dialog");
    server->cmd_dialog_target_create_seq = 0;
    return true;
}

void server_keybindings_view_set_title_dialog(void *userdata, struct fbwl_view *view) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->scene == NULL || server->output_layout == NULL) {
        return;
    }

    server->cmd_dialog_target_create_seq = view->create_seq;
    server_menu_ui_close(server, "set-title-dialog");
    fbwl_ui_cmd_dialog_open_prompt(&server->cmd_dialog_ui, server->scene, server->layer_overlay,
        &server->decor_theme, server->output_layout, "Set Title: ", "",
        cmd_dialog_submit_set_title, server);
}

void server_keybindings_workspace_toggle_prev(void *userdata, int cursor_x, int cursor_y, const char *why) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    const size_t head = fbwl_server_screen_index_at(server, cursor_x, cursor_y);
    const int cur = fbwm_core_workspace_current_for_head(&server->wm, head);
    const int prev = fbwm_core_workspace_prev_for_head(&server->wm, head);
    if (prev == cur) {
        return;
    }
    server_workspace_switch_on_head(server, head, prev, why != NULL ? why : "toggle-prev");
}

static void workspace_names_ensure_defaults(struct fbwm_core *wm, int count) {
    if (wm == NULL) {
        return;
    }
    if (count < 1) {
        count = 1;
    }
    if (count > 1000) {
        count = 1000;
    }
    for (int i = 0; i < count; i++) {
        const char *cur = fbwm_core_workspace_name(wm, i);
        if (cur != NULL && *cur != '\0') {
            continue;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "Workspace %d", i + 1);
        (void)fbwm_core_set_workspace_name(wm, i, buf);
    }
}

void server_keybindings_add_workspace(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    const int cur = fbwm_core_workspace_count(&server->wm);
    if (cur >= 1000) {
        wlr_log(WLR_ERROR, "AddWorkspace: workspace limit reached (count=%d)", cur);
        return;
    }

    const int next = cur + 1;
    fbwm_core_set_workspace_count(&server->wm, next);

    if (fbwm_core_workspace_names_len(&server->wm) > 0) {
        workspace_names_ensure_defaults(&server->wm, next);
    }
    server_toolbar_ui_rebuild(server);

    wlr_log(WLR_INFO, "Workspace: add count=%d", next);
    server_keybindings_save_rc(server);
}

void server_keybindings_remove_last_workspace(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    const int cur = fbwm_core_workspace_count(&server->wm);
    if (cur <= 1) {
        wlr_log(WLR_INFO, "RemoveLastWorkspace: ignored (count=%d)", cur);
        return;
    }

    const int next = cur - 1;
    const int target_ws = next - 1;
    for (struct fbwm_view *walk = server->wm.views.next; walk != &server->wm.views; walk = walk->next) {
        if (!walk->sticky && walk->workspace >= next) {
            walk->workspace = target_ws;
        }
    }
    fbwm_core_set_workspace_count(&server->wm, next);
    apply_workspace_visibility(server, "remove-last-workspace");

    wlr_log(WLR_INFO, "Workspace: remove-last count=%d", next);
    server_keybindings_save_rc(server);
}

void server_keybindings_set_workspace_name(void *userdata, const char *args, int cursor_x, int cursor_y) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    const size_t head = fbwl_server_screen_index_at(server, cursor_x, cursor_y);
    int ws = fbwm_core_workspace_current_for_head(&server->wm, head);
    if (ws < 0) {
        ws = 0;
    }

    const int count = fbwm_core_workspace_count(&server->wm);
    workspace_names_ensure_defaults(&server->wm, count);

    const char *name = "empty";
    char *tmp = NULL;
    if (args != NULL) {
        tmp = strdup(args);
        if (tmp == NULL) {
            wlr_log(WLR_ERROR, "SetWorkspaceName: OOM");
            return;
        }
        char *trimmed = trim_inplace(tmp);
        if (trimmed != NULL && *trimmed != '\0') {
            name = trimmed;
        }
    }

    if (!fbwm_core_set_workspace_name(&server->wm, ws, name)) {
        wlr_log(WLR_ERROR, "SetWorkspaceName: failed ws=%d", ws + 1);
        free(tmp);
        return;
    }
    free(tmp);

    wlr_log(WLR_INFO, "WorkspaceName: set ws=%d", ws + 1);
    server_toolbar_ui_rebuild(server);
    server_keybindings_save_rc(server);
}

static bool cmd_dialog_submit_set_workspace_name(void *userdata, const char *text) {
    struct fbwl_server *server = userdata;
    if (server != NULL && server->cursor != NULL) {
        server_keybindings_set_workspace_name(userdata, text, (int)server->cursor->x, (int)server->cursor->y);
    } else {
        server_keybindings_set_workspace_name(userdata, text, 0, 0);
    }
    return true;
}

void server_keybindings_set_workspace_name_dialog(void *userdata, int cursor_x, int cursor_y) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->scene == NULL || server->output_layout == NULL) {
        return;
    }

    const size_t head = fbwl_server_screen_index_at(server, cursor_x, cursor_y);
    int ws = fbwm_core_workspace_current_for_head(&server->wm, head);
    if (ws < 0) {
        ws = 0;
    }

    const char *name = fbwm_core_workspace_name(&server->wm, ws);
    char buf[32];
    const char *initial = "";
    if (name != NULL && *name != '\0') {
        initial = name;
    } else if (snprintf(buf, sizeof(buf), "%d", ws + 1) > 0) {
        initial = buf;
    }

    server_menu_ui_close(server, "set-workspace-name-dialog");
    fbwl_ui_cmd_dialog_open_prompt(&server->cmd_dialog_ui, server->scene, server->layer_overlay,
        &server->decor_theme, server->output_layout, "SetWorkspaceName ", initial,
        cmd_dialog_submit_set_workspace_name, server);
}

static bool env_name_is_valid(const char *name) {
    if (name == NULL || *name == '\0') {
        return false;
    }
    const unsigned char first = (unsigned char)name[0];
    if (!(isalpha(first) || first == '_')) {
        return false;
    }
    for (const char *p = name + 1; *p != '\0'; p++) {
        const unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '_')) {
            return false;
        }
    }
    return true;
}

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

void server_keybindings_set_env(void *userdata, const char *args) {
    (void)userdata;
    if (args == NULL || *args == '\0') {
        return;
    }

    char *dup = strdup(args);
    if (dup == NULL) {
        wlr_log(WLR_ERROR, "SetEnv: OOM");
        return;
    }

    char *s = trim_inplace(dup);
    if (s == NULL || *s == '\0') {
        free(dup);
        return;
    }

    char *name = NULL;
    char *value = NULL;

    char *first_ws = s;
    while (*first_ws != '\0' && !isspace((unsigned char)*first_ws)) {
        first_ws++;
    }

    char *eq = strchr(s, '=');
    if (eq != NULL && eq < first_ws) {
        *eq = '\0';
        name = trim_inplace(s);
        value = trim_inplace(eq + 1);
    } else if (*first_ws != '\0') {
        *first_ws = '\0';
        name = trim_inplace(s);
        value = trim_inplace(first_ws + 1);
    } else {
        wlr_log(WLR_ERROR, "SetEnv: expected 'NAME VALUE' or 'NAME=VALUE': %s", s);
        free(dup);
        return;
    }

    if (!env_name_is_valid(name)) {
        wlr_log(WLR_ERROR, "SetEnv: invalid variable name: %s", name != NULL ? name : "(null)");
        free(dup);
        return;
    }

    if (value == NULL) {
        value = "";
    }

    if (setenv(name, value, 1) != 0) {
        wlr_log(WLR_ERROR, "SetEnv: %s failed: %s", name, strerror(errno));
        free(dup);
        return;
    }

    wlr_log(WLR_INFO, "SetEnv: set %s", name);
    free(dup);
}

void server_keybindings_toggle_toolbar_hidden(void *userdata, int cursor_x, int cursor_y) {
    (void)cursor_x;
    (void)cursor_y;
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    server->toolbar_ui.hidden = !server->toolbar_ui.hidden;
    server_toolbar_ui_update_position(server);
    if (server->toolbar_ui.tree != NULL) {
        wlr_scene_node_raise_to_top(&server->toolbar_ui.tree->node);
    }
    wlr_log(WLR_INFO, "Toolbar: toggleHidden hidden=%d", server->toolbar_ui.hidden ? 1 : 0);
}

void server_keybindings_toggle_toolbar_above(void *userdata, int cursor_x, int cursor_y) {
    (void)cursor_x;
    (void)cursor_y;
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    const size_t screen = server->toolbar_ui.on_head >= 0 ? (size_t)server->toolbar_ui.on_head : 0;
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config(server, screen);
    const int rc_layer = cfg != NULL ? cfg->toolbar.layer_num : server->toolbar_ui.layer_num;
    server->toolbar_ui.layer_num = server->toolbar_ui.layer_num == rc_layer ? 2 : rc_layer;
    server_toolbar_ui_rebuild(server);
    if (server->toolbar_ui.tree != NULL) {
        wlr_scene_node_raise_to_top(&server->toolbar_ui.tree->node);
    }
    wlr_log(WLR_INFO, "Toolbar: toggleAboveDock layer=%d rc_layer=%d", server->toolbar_ui.layer_num, rc_layer);
}

void server_keybindings_toggle_slit_hidden(void *userdata, int cursor_x, int cursor_y) {
    (void)cursor_x;
    (void)cursor_y;
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    server->slit_ui.hidden = !server->slit_ui.hidden;
    server_slit_ui_update_position(server);
    if (server->slit_ui.tree != NULL) {
        wlr_scene_node_raise_to_top(&server->slit_ui.tree->node);
    }
    wlr_log(WLR_INFO, "Slit: toggleHidden hidden=%d", server->slit_ui.hidden ? 1 : 0);
}

void server_keybindings_toggle_slit_above(void *userdata, int cursor_x, int cursor_y) {
    (void)cursor_x;
    (void)cursor_y;
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    const size_t screen = server->slit_ui.on_head >= 0 ? (size_t)server->slit_ui.on_head : 0;
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config(server, screen);
    const int rc_layer = cfg != NULL ? cfg->slit.layer_num : server->slit_ui.layer_num;
    server->slit_ui.layer_num = server->slit_ui.layer_num == rc_layer ? 2 : rc_layer;
    server_slit_ui_rebuild(server);
    if (server->slit_ui.tree != NULL) {
        wlr_scene_node_raise_to_top(&server->slit_ui.tree->node);
    }
    wlr_log(WLR_INFO, "Slit: toggleAboveDock layer=%d rc_layer=%d", server->slit_ui.layer_num, rc_layer);
}
