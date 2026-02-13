#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "wayland/fbwl_server_keybinding_actions.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_xwayland.h"

#define FBWL_KEYMOD_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | \
    WLR_MODIFIER_MOD2 | WLR_MODIFIER_MOD3 | WLR_MODIFIER_MOD5)

static bool parse_keys_modifier(const char *token, uint32_t *mods) {
    if (token == NULL || mods == NULL) {
        return false;
    }
    if (strcasecmp(token, "none") == 0) {
        return true;
    }
    if (strcasecmp(token, "mod1") == 0 || strcasecmp(token, "alt") == 0) {
        *mods |= WLR_MODIFIER_ALT;
        return true;
    }
    if (strcasecmp(token, "mod2") == 0) {
        *mods |= WLR_MODIFIER_MOD2;
        return true;
    }
    if (strcasecmp(token, "mod3") == 0) {
        *mods |= WLR_MODIFIER_MOD3;
        return true;
    }
    if (strcasecmp(token, "mod4") == 0 || strcasecmp(token, "super") == 0 ||
            strcasecmp(token, "logo") == 0 || strcasecmp(token, "win") == 0) {
        *mods |= WLR_MODIFIER_LOGO;
        return true;
    }
    if (strcasecmp(token, "mod5") == 0) {
        *mods |= WLR_MODIFIER_MOD5;
        return true;
    }
    if (strcasecmp(token, "control") == 0 || strcasecmp(token, "ctrl") == 0) {
        *mods |= WLR_MODIFIER_CTRL;
        return true;
    }
    if (strcasecmp(token, "shift") == 0) {
        *mods |= WLR_MODIFIER_SHIFT;
        return true;
    }
    return false;
}

static bool next_token_buf(const char **inout, char *buf, size_t buf_size) {
    if (inout == NULL || *inout == NULL || buf == NULL || buf_size < 2) {
        return false;
    }
    const char *p = *inout;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        *inout = p;
        return false;
    }
    const char *start = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        p++;
    }
    size_t len = (size_t)(p - start);
    if (len + 1 > buf_size) {
        return false;
    }
    memcpy(buf, start, len);
    buf[len] = '\0';
    *inout = p;
    return true;
}

static bool parse_key_mode_return_binding(const char *s,
        enum fbwl_keybinding_key_kind *out_kind, uint32_t *out_keycode, xkb_keysym_t *out_sym,
        uint32_t *out_modifiers) {
    if (out_kind == NULL || out_keycode == NULL || out_sym == NULL || out_modifiers == NULL) {
        return false;
    }
    *out_kind = FBWL_KEYBIND_KEYSYM;
    *out_keycode = 0;
    *out_sym = XKB_KEY_NoSymbol;
    *out_modifiers = 0;
    if (s == NULL) {
        return false;
    }
    uint32_t mods = 0;
    bool have_key = false;
    enum fbwl_keybinding_key_kind kind = FBWL_KEYBIND_KEYSYM;
    uint32_t keycode = 0;
    xkb_keysym_t sym = XKB_KEY_NoSymbol;
    const char *p = s;
    char tok[64];
    while (next_token_buf(&p, tok, sizeof(tok))) {
        if (strncasecmp(tok, "on", 2) == 0) {
            continue;
        }
        if (parse_keys_modifier(tok, &mods)) {
            continue;
        }
        if (have_key) {
            return false;
        }
        if (strcasecmp(tok, "arg") == 0 || strcasecmp(tok, "changeworkspace") == 0) {
            return false;
        }
        sym = xkb_keysym_from_name(tok, XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym != XKB_KEY_NoSymbol) {
            kind = FBWL_KEYBIND_KEYSYM;
            have_key = true;
            continue;
        }
        bool is_keycode = true;
        for (const char *q = tok; *q != '\0'; q++) {
            if (!isdigit((unsigned char)*q)) {
                is_keycode = false;
                break;
            }
        }
        if (!is_keycode) {
            return false;
        }
        char *end = NULL;
        long code = strtol(tok, &end, 10);
        if (end == tok || end == NULL || *end != '\0' || code < 0 || code > 100000) {
            return false;
        }

        kind = FBWL_KEYBIND_KEYCODE;
        keycode = (uint32_t)code;
        have_key = true;
    }
    if (!have_key) {
        return false;
    }
    *out_kind = kind;
    *out_keycode = keycode;
    *out_sym = xkb_keysym_to_lower(sym);
    *out_modifiers = mods & FBWL_KEYMOD_MASK;
    return true;
}

void fbwl_server_key_mode_set(struct fbwl_server *server, const char *mode) {
    if (server == NULL) {
        return;
    }

    char *new_mode = NULL;
    const char *rest = NULL;
    if (mode != NULL && *mode != '\0') {
        const char *p = mode;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        const char *start = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        rest = p;
        if (len > 0 && start[len - 1] == ':') {
            len--;
        }
        if (len > 0) {
            char *tmp = malloc(len + 1);
            if (tmp != NULL) {
                memcpy(tmp, start, len);
                tmp[len] = '\0';
                if (strcasecmp(tmp, "default") != 0) {
                    new_mode = tmp;
                } else {
                    free(tmp);
                }
            }
        }
    }

    free(server->key_mode);
    server->key_mode = new_mode;

    server->key_mode_return_active = false;
    server->key_mode_return_kind = FBWL_KEYBIND_KEYSYM;
    server->key_mode_return_keycode = 0;
    server->key_mode_return_sym = xkb_keysym_to_lower(XKB_KEY_Escape);
    server->key_mode_return_modifiers = 0;

    if (server->key_mode != NULL) {
        server->key_mode_return_active = true;

        enum fbwl_keybinding_key_kind kind = FBWL_KEYBIND_KEYSYM;
        uint32_t keycode = 0;
        xkb_keysym_t sym = XKB_KEY_NoSymbol;
        uint32_t mods = 0;

        bool parsed = false;
        if (rest != NULL) {
            const char *t = rest;
            while (*t != '\0' && isspace((unsigned char)*t)) {
                t++;
            }
            if (*t != '\0') {
                parsed = parse_key_mode_return_binding(t, &kind, &keycode, &sym, &mods);
                if (!parsed) {
                    wlr_log(WLR_ERROR, "KeyMode: invalid return-keybinding: %s", t);
                }
            }
        }

        if (parsed) {
            server->key_mode_return_kind = kind;
            server->key_mode_return_keycode = keycode;
            server->key_mode_return_sym = sym;
            server->key_mode_return_modifiers = mods;
        }
    }

    wlr_log(WLR_INFO, "KeyMode: set to %s", server->key_mode != NULL ? server->key_mode : "default");
}

static bool view_frame_metrics(const struct fbwl_server *server, const struct fbwl_view *view,
        int *out_frame_x, int *out_frame_y, int *out_frame_w, int *out_frame_h,
        int *out_left, int *out_top, int *out_border) {
    if (out_frame_x != NULL) {
        *out_frame_x = 0;
    }
    if (out_frame_y != NULL) {
        *out_frame_y = 0;
    }
    if (out_frame_w != NULL) {
        *out_frame_w = 0;
    }
    if (out_frame_h != NULL) {
        *out_frame_h = 0;
    }
    if (out_left != NULL) {
        *out_left = 0;
    }
    if (out_top != NULL) {
        *out_top = 0;
    }
    if (out_border != NULL) {
        *out_border = 0;
    }

    if (server == NULL || view == NULL || out_frame_x == NULL || out_frame_y == NULL || out_frame_w == NULL || out_frame_h == NULL) {
        return false;
    }

    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    if (w < 1 || h < 1) {
        return false;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    fbwl_view_decor_frame_extents(view, &server->decor_theme, &left, &top, &right, &bottom);

    const int border = left > right ? left : right;
    const int frame_w = w + left + right;
    const int frame_h = h + top + bottom;

    *out_frame_x = view->x - left;
    *out_frame_y = view->y - top;
    *out_frame_w = frame_w;
    *out_frame_h = frame_h;
    if (out_left != NULL) {
        *out_left = left;
    }
    if (out_top != NULL) {
        *out_top = top;
    }
    if (out_border != NULL) {
        *out_border = border;
    }
    return true;
}

struct cmd_token {
    const char *s;
    size_t len;
};

static bool token_next(const char **inout, struct cmd_token *out) {
    if (inout == NULL || *inout == NULL || out == NULL) {
        return false;
    }
    const char *p = *inout;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        *inout = p;
        return false;
    }
    const char *start = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        p++;
    }
    *out = (struct cmd_token){
        .s = start,
        .len = (size_t)(p - start),
    };
    *inout = p;
    return true;
}

static bool token_equals_ci(const struct cmd_token *tok, const char *s) {
    if (tok == NULL || s == NULL) {
        return false;
    }
    const size_t slen = strlen(s);
    if (tok->len != slen) {
        return false;
    }
    return strncasecmp(tok->s, s, tok->len) == 0;
}

static bool parse_size_token(const struct cmd_token *tok, int *out_value, bool *out_percent, bool *out_ignore) {
    if (out_value != NULL) {
        *out_value = 0;
    }
    if (out_percent != NULL) {
        *out_percent = false;
    }
    if (out_ignore != NULL) {
        *out_ignore = false;
    }
    if (tok == NULL || tok->s == NULL || tok->len == 0 || out_value == NULL || out_percent == NULL || out_ignore == NULL) {
        return false;
    }

    if (tok->len == 1 && tok->s[0] == '*') {
        *out_ignore = true;
        return true;
    }

    bool pct = false;
    size_t len = tok->len;
    if (tok->s[len - 1] == '%') {
        pct = true;
        len--;
    }
    if (len == 0) {
        return false;
    }

    char buf[64];
    if (len >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, tok->s, len);
    buf[len] = '\0';

    errno = 0;
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (errno != 0 || end == buf || *end != '\0') {
        return false;
    }
    if (v < -1000000 || v > 1000000) {
        return false;
    }
    *out_value = (int)v;
    *out_percent = pct;
    return true;
}

static int64_t floor_div_i64(int64_t a, int64_t b) {
    if (b == 0) {
        return 0;
    }
    if (a >= 0) {
        return a / b;
    }
    int64_t q = a / b;
    int64_t r = a % b;
    return r == 0 ? q : (q - 1);
}

static int cal_percentage_value_of(int percent, int base) {
    if (base <= 0) {
        return 0;
    }
    const int64_t num = (int64_t)percent * (int64_t)base;
    return (int)floor_div_i64(num + 50, 100);
}

enum fbwl_geom_anchor {
    FBWL_GEOM_ANCHOR_TOPLEFT = 0,
    FBWL_GEOM_ANCHOR_TOP,
    FBWL_GEOM_ANCHOR_TOPRIGHT,
    FBWL_GEOM_ANCHOR_LEFT,
    FBWL_GEOM_ANCHOR_CENTER,
    FBWL_GEOM_ANCHOR_RIGHT,
    FBWL_GEOM_ANCHOR_BOTTOMLEFT,
    FBWL_GEOM_ANCHOR_BOTTOM,
    FBWL_GEOM_ANCHOR_BOTTOMRIGHT,
};

static const char *geom_anchor_name(enum fbwl_geom_anchor a) {
    switch (a) {
    case FBWL_GEOM_ANCHOR_TOPLEFT:
        return "topleft";
    case FBWL_GEOM_ANCHOR_TOP:
        return "top";
    case FBWL_GEOM_ANCHOR_TOPRIGHT:
        return "topright";
    case FBWL_GEOM_ANCHOR_LEFT:
        return "left";
    case FBWL_GEOM_ANCHOR_CENTER:
        return "center";
    case FBWL_GEOM_ANCHOR_RIGHT:
        return "right";
    case FBWL_GEOM_ANCHOR_BOTTOMLEFT:
        return "bottomleft";
    case FBWL_GEOM_ANCHOR_BOTTOM:
        return "bottom";
    case FBWL_GEOM_ANCHOR_BOTTOMRIGHT:
        return "bottomright";
    default:
        return "topleft";
    }
}

static enum fbwl_geom_anchor parse_geom_anchor(const struct cmd_token *tok) {
    if (tok == NULL || tok->s == NULL || tok->len == 0) {
        return FBWL_GEOM_ANCHOR_TOPLEFT;
    }

    if (token_equals_ci(tok, "lefttop") || token_equals_ci(tok, "topleft") || token_equals_ci(tok, "upperleft")) {
        return FBWL_GEOM_ANCHOR_TOPLEFT;
    }
    if (token_equals_ci(tok, "top") || token_equals_ci(tok, "upper") || token_equals_ci(tok, "topcenter")) {
        return FBWL_GEOM_ANCHOR_TOP;
    }
    if (token_equals_ci(tok, "righttop") || token_equals_ci(tok, "topright") || token_equals_ci(tok, "upperright")) {
        return FBWL_GEOM_ANCHOR_TOPRIGHT;
    }
    if (token_equals_ci(tok, "left") || token_equals_ci(tok, "leftcenter")) {
        return FBWL_GEOM_ANCHOR_LEFT;
    }
    if (token_equals_ci(tok, "center") || token_equals_ci(tok, "wincen") || token_equals_ci(tok, "wincenter")) {
        return FBWL_GEOM_ANCHOR_CENTER;
    }
    if (token_equals_ci(tok, "right") || token_equals_ci(tok, "rightcenter")) {
        return FBWL_GEOM_ANCHOR_RIGHT;
    }
    if (token_equals_ci(tok, "leftbottom") || token_equals_ci(tok, "bottomleft") || token_equals_ci(tok, "lowerleft")) {
        return FBWL_GEOM_ANCHOR_BOTTOMLEFT;
    }
    if (token_equals_ci(tok, "bottom") || token_equals_ci(tok, "lower") || token_equals_ci(tok, "bottomcenter")) {
        return FBWL_GEOM_ANCHOR_BOTTOM;
    }
    if (token_equals_ci(tok, "rightbottom") || token_equals_ci(tok, "bottomright") || token_equals_ci(tok, "lowerright")) {
        return FBWL_GEOM_ANCHOR_BOTTOMRIGHT;
    }

    return FBWL_GEOM_ANCHOR_TOPLEFT;
}

static bool anchor_is_left(enum fbwl_geom_anchor a) {
    return a == FBWL_GEOM_ANCHOR_TOPLEFT || a == FBWL_GEOM_ANCHOR_LEFT || a == FBWL_GEOM_ANCHOR_BOTTOMLEFT;
}

static bool anchor_is_right(enum fbwl_geom_anchor a) {
    return a == FBWL_GEOM_ANCHOR_TOPRIGHT || a == FBWL_GEOM_ANCHOR_RIGHT || a == FBWL_GEOM_ANCHOR_BOTTOMRIGHT;
}

static bool anchor_is_top(enum fbwl_geom_anchor a) {
    return a == FBWL_GEOM_ANCHOR_TOPLEFT || a == FBWL_GEOM_ANCHOR_TOP || a == FBWL_GEOM_ANCHOR_TOPRIGHT;
}

static bool anchor_is_bottom(enum fbwl_geom_anchor a) {
    return a == FBWL_GEOM_ANCHOR_BOTTOMLEFT || a == FBWL_GEOM_ANCHOR_BOTTOM || a == FBWL_GEOM_ANCHOR_BOTTOMRIGHT;
}

static int translate_x(int x, enum fbwl_geom_anchor anchor, int left, int right, int w, int bw) {
    if (anchor_is_left(anchor)) {
        return left + x;
    }
    if (anchor_is_right(anchor)) {
        return right - w - bw - x;
    }
    return x + (left + right - w - bw) / 2;
}

static int translate_y(int y, enum fbwl_geom_anchor anchor, int top, int bottom, int h, int bw) {
    if (anchor_is_top(anchor)) {
        return top + y;
    }
    if (anchor_is_bottom(anchor)) {
        return bottom - h - bw - y;
    }
    return y + (top + bottom - h - bw) / 2;
}

static bool view_ignores_size_hints(const struct fbwl_view *view) {
    return view != NULL && view->ignore_size_hints_override_set && view->ignore_size_hints_override;
}

static int positive_or(int v, int fallback) {
    return v > 0 ? v : fallback;
}

static void view_get_resize_incs(const struct fbwl_view *view, int *out_inc_w, int *out_inc_h) {
    if (out_inc_w != NULL) {
        *out_inc_w = 1;
    }
    if (out_inc_h != NULL) {
        *out_inc_h = 1;
    }
    if (view == NULL || view->type != FBWL_VIEW_XWAYLAND || view->xwayland_surface == NULL || view_ignores_size_hints(view)) {
        return;
    }
    const xcb_size_hints_t *hints = view->xwayland_surface->size_hints;
    if (hints == NULL) {
        return;
    }
    if ((hints->flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) != 0) {
        if (out_inc_w != NULL) {
            *out_inc_w = positive_or(hints->width_inc, 1);
        }
        if (out_inc_h != NULL) {
            *out_inc_h = positive_or(hints->height_inc, 1);
        }
    }
}

static void view_disable_maximization_if_needed(struct fbwl_server *server, struct fbwl_view *view) {
    if (server == NULL || view == NULL || server->output_layout == NULL) {
        return;
    }

    if (view->fullscreen) {
        fbwl_view_set_fullscreen(view, false, server->output_layout, &server->outputs,
            server->layer_normal, server->layer_fullscreen, NULL);
    }
    if (view->maximized || view->maximized_h || view->maximized_v) {
        fbwl_view_set_maximized(view, false, server->output_layout, &server->outputs);
    }
}

static void view_apply_move(struct fbwl_server *server, struct fbwl_view *view, int x, int y, const char *why) {
    if (server == NULL || view == NULL) {
        return;
    }

    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    if (w < 1 || h < 1) {
        return;
    }

    view->x = x;
    view->y = y;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }
    fbwl_view_pseudo_bg_update(view, why);
    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)w, (uint16_t)h);
    }

    view->placed = true;
    fbwl_tabs_sync_geometry_from_view(view, false, 0, 0, why);
    fbwl_view_foreign_update_output_from_position(view, server->output_layout);
}

static void view_apply_resize(struct fbwl_server *server, struct fbwl_view *view, int w, int h, const char *why) {
    if (server == NULL || view == NULL) {
        return;
    }
    if (w < 1 || h < 1) {
        return;
    }

    if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
    } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)w, (uint16_t)h);
    }
    view->width = w;
    view->height = h;
    view->placed = true;
    fbwl_view_pseudo_bg_update(view, why);
    fbwl_tabs_sync_geometry_from_view(view, true, w, h, why);
    fbwl_view_foreign_update_output_from_position(view, server->output_layout);
}

void server_keybindings_view_move_to_cmd(void *userdata, struct fbwl_view *view, const char *args) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (view->in_slit) {
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool disable_move = cfg != NULL ? cfg->max_disable_move : server->max_disable_move;
    if (disable_move && (view->fullscreen || view->maximized || view->maximized_h || view->maximized_v)) {
        wlr_log(WLR_INFO, "MoveTo: %s ignored (max_disable_move)", fbwl_view_display_title(view));
        return;
    }

    view_disable_maximization_if_needed(server, view);

    int frame_x = 0;
    int frame_y = 0;
    int frame_w = 0;
    int frame_h = 0;
    int left = 0;
    int top = 0;
    int border = 0;
    if (!view_frame_metrics(server, view, &frame_x, &frame_y, &frame_w, &frame_h, &left, &top, &border)) {
        return;
    }

    const int inner_w = frame_w - 2 * border;
    const int inner_h = frame_h - 2 * border;
    const int bw = 2 * border;

    const char *p = args != NULL ? args : "";
    struct cmd_token tok_x = {0};
    struct cmd_token tok_y = {0};
    if (!token_next(&p, &tok_x) || !token_next(&p, &tok_y)) {
        wlr_log(WLR_ERROR, "MoveTo: missing args title=%s args=%s", fbwl_view_display_title(view), args != NULL ? args : "");
        return;
    }

    int x = 0;
    bool x_pct = false;
    bool x_ign = false;
    if (!parse_size_token(&tok_x, &x, &x_pct, &x_ign)) {
        wlr_log(WLR_ERROR, "MoveTo: bad x token title=%s", fbwl_view_display_title(view));
        return;
    }
    int y = 0;
    bool y_pct = false;
    bool y_ign = false;
    if (!parse_size_token(&tok_y, &y, &y_pct, &y_ign)) {
        wlr_log(WLR_ERROR, "MoveTo: bad y token title=%s", fbwl_view_display_title(view));
        return;
    }

    enum fbwl_geom_anchor anchor = FBWL_GEOM_ANCHOR_TOPLEFT;
    struct cmd_token tok_anchor = {0};
    if (token_next(&p, &tok_anchor)) {
        anchor = parse_geom_anchor(&tok_anchor);
    }

    struct wlr_box usable = {0};
    fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, NULL, &usable);
    if (usable.width < 1 || usable.height < 1) {
        return;
    }

    int new_x = view->x;
    int new_y = view->y;

    if (!x_ign) {
        const int x_px = x_pct ? cal_percentage_value_of(x, usable.width) : x;
        const int frame_x_new = translate_x(x_px, anchor, usable.x, usable.x + usable.width, inner_w, bw);
        new_x = frame_x_new + border;
    }
    if (!y_ign) {
        const int y_px = y_pct ? cal_percentage_value_of(y, usable.height) : y;
        const int frame_y_new = translate_y(y_px, anchor, usable.y, usable.y + usable.height, inner_h, bw);
        new_y = frame_y_new + top;
    }

    view_apply_move(server, view, new_x, new_y, "moveto");
    wlr_log(WLR_INFO, "MoveTo: %s x=%d y=%d anchor=%s args=%s",
        fbwl_view_display_title(view), view->x, view->y, geom_anchor_name(anchor),
        args != NULL ? args : "");
    server_strict_mousefocus_recheck(server, "moveto");
}

void server_keybindings_view_move_rel_cmd(void *userdata, struct fbwl_view *view, int kind, const char *args) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (view->in_slit) {
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool disable_move = cfg != NULL ? cfg->max_disable_move : server->max_disable_move;
    if (disable_move && (view->fullscreen || view->maximized || view->maximized_h || view->maximized_v)) {
        wlr_log(WLR_INFO, "Move: %s ignored (max_disable_move)", fbwl_view_display_title(view));
        return;
    }

    int a = 0;
    int b = 0;
    if (args != NULL) {
        errno = 0;
        char *end = NULL;
        long v = strtol(args, &end, 10);
        if (end != args && errno == 0) {
            a = (int)v;
            if (end != NULL) {
                while (*end != '\0' && isspace((unsigned char)*end)) {
                    end++;
                }
                if (end != NULL && *end != '\0') {
                    char *end2 = NULL;
                    long v2 = strtol(end, &end2, 10);
                    if (end2 != end) {
                        b = (int)v2;
                    }
                }
            }
        }
    }

    int dx = 0;
    int dy = 0;
    switch (kind) {
    case 1: // MoveRight
        dx = a;
        dy = 0;
        break;
    case 2: // MoveLeft
        dx = -a;
        dy = 0;
        break;
    case 3: // MoveUp
        dx = 0;
        dy = -a;
        break;
    case 4: // MoveDown
        dx = 0;
        dy = a;
        break;
    default: // Move
        dx = a;
        dy = b;
        break;
    }

    view_disable_maximization_if_needed(server, view);
    view_apply_move(server, view, view->x + dx, view->y + dy, "move");
    wlr_log(WLR_INFO, "Move: %s x=%d y=%d dx=%d dy=%d args=%s",
        fbwl_view_display_title(view), view->x, view->y, dx, dy,
        args != NULL ? args : "");
    server_strict_mousefocus_recheck(server, "move");
}

void server_keybindings_view_resize_to_cmd(void *userdata, struct fbwl_view *view, const char *args) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (view->in_slit) {
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool disable_resize = cfg != NULL ? cfg->max_disable_resize : server->max_disable_resize;
    if (disable_resize && (view->fullscreen || view->maximized || view->maximized_h || view->maximized_v)) {
        wlr_log(WLR_INFO, "ResizeTo: %s ignored (max_disable_resize)", fbwl_view_display_title(view));
        return;
    }

    view_disable_maximization_if_needed(server, view);

    int frame_x = 0;
    int frame_y = 0;
    int frame_w = 0;
    int frame_h = 0;
    int left = 0;
    int top = 0;
    int border = 0;
    if (!view_frame_metrics(server, view, &frame_x, &frame_y, &frame_w, &frame_h, &left, &top, &border)) {
        return;
    }

    const int title_h = top - border;
    const int inner_w0 = frame_w - 2 * border;
    const int inner_h0 = frame_h - 2 * border;

    const char *p = args != NULL ? args : "";
    struct cmd_token tok_w = {0};
    struct cmd_token tok_h = {0};
    if (!token_next(&p, &tok_w) || !token_next(&p, &tok_h)) {
        wlr_log(WLR_ERROR, "ResizeTo: missing args title=%s args=%s", fbwl_view_display_title(view), args != NULL ? args : "");
        return;
    }

    int w = 0;
    bool w_pct = false;
    bool w_ign = false;
    if (!parse_size_token(&tok_w, &w, &w_pct, &w_ign) || w_ign) {
        wlr_log(WLR_ERROR, "ResizeTo: bad width token title=%s", fbwl_view_display_title(view));
        return;
    }
    int h = 0;
    bool h_pct = false;
    bool h_ign = false;
    if (!parse_size_token(&tok_h, &h, &h_pct, &h_ign) || h_ign) {
        wlr_log(WLR_ERROR, "ResizeTo: bad height token title=%s", fbwl_view_display_title(view));
        return;
    }

    int inner_w = w;
    int inner_h = h;

    if (w_pct || h_pct) {
        struct wlr_box usable = {0};
        fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, NULL, &usable);
        if (usable.width < 1 || usable.height < 1) {
            return;
        }
        if (w_pct) {
            inner_w = cal_percentage_value_of(w, usable.width) - 2 * border;
            if (inner_w <= 0) {
                inner_w = inner_w0;
            }
        }
        if (h_pct) {
            inner_h = cal_percentage_value_of(h, usable.height) - 2 * border;
            if (inner_h <= 0) {
                inner_h = inner_h0;
            }
        }
    }

    if (!w_pct && inner_w == 0) {
        inner_w = inner_w0;
    }
    if (!h_pct && inner_h == 0) {
        inner_h = inner_h0;
    }
    if (inner_w < 1) {
        inner_w = 1;
    }
    if (inner_h < 1) {
        inner_h = 1;
    }

    int content_w = inner_w;
    int content_h = inner_h - title_h;
    if (content_h < 1) {
        content_h = 1;
    }

    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL && !view_ignores_size_hints(view)) {
        fbwl_xwayland_apply_size_hints(view->xwayland_surface, &content_w, &content_h, false);
    }

    view_apply_resize(server, view, content_w, content_h, "resizeto");
    wlr_log(WLR_INFO, "ResizeTo: %s w=%d h=%d args=%s",
        fbwl_view_display_title(view), content_w, content_h,
        args != NULL ? args : "");
    server_strict_mousefocus_recheck(server, "resizeto");
}

void server_keybindings_view_resize_rel_cmd(void *userdata, struct fbwl_view *view, int kind, const char *args) {
    struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return;
    }
    if (view->in_slit) {
        return;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool disable_resize = cfg != NULL ? cfg->max_disable_resize : server->max_disable_resize;
    if (disable_resize && (view->fullscreen || view->maximized || view->maximized_h || view->maximized_v)) {
        wlr_log(WLR_INFO, "Resize: %s ignored (max_disable_resize)", fbwl_view_display_title(view));
        return;
    }

    view_disable_maximization_if_needed(server, view);

    int frame_x = 0;
    int frame_y = 0;
    int frame_w = 0;
    int frame_h = 0;
    int left = 0;
    int top = 0;
    int border = 0;
    if (!view_frame_metrics(server, view, &frame_x, &frame_y, &frame_w, &frame_h, &left, &top, &border)) {
        return;
    }

    const int title_h = top - border;
    const int inner_w0 = frame_w - 2 * border;
    const int inner_h0 = frame_h - 2 * border;

    const char *p = args != NULL ? args : "";
    struct cmd_token tok_a = {0};
    struct cmd_token tok_b = {0};
    if (!token_next(&p, &tok_a)) {
        wlr_log(WLR_ERROR, "Resize: missing args title=%s args=%s", fbwl_view_display_title(view), args != NULL ? args : "");
        return;
    }
    if (kind == 0 && !token_next(&p, &tok_b)) {
        wlr_log(WLR_ERROR, "Resize: missing second arg title=%s args=%s", fbwl_view_display_title(view), args != NULL ? args : "");
        return;
    }

    int dx = 0;
    bool dx_pct = false;
    bool dx_ign = false;
    if (!parse_size_token(&tok_a, &dx, &dx_pct, &dx_ign) || dx_ign) {
        wlr_log(WLR_ERROR, "Resize: bad width token title=%s", fbwl_view_display_title(view));
        return;
    }
    int dy = 0;
    bool dy_pct = false;
    bool dy_ign = false;
    if (kind == 0) {
        if (!parse_size_token(&tok_b, &dy, &dy_pct, &dy_ign) || dy_ign) {
            wlr_log(WLR_ERROR, "Resize: bad height token title=%s", fbwl_view_display_title(view));
            return;
        }
    } else if (kind == 2) {
        if (!parse_size_token(&tok_a, &dy, &dy_pct, &dy_ign) || dy_ign) {
            wlr_log(WLR_ERROR, "ResizeVertical: bad height token title=%s", fbwl_view_display_title(view));
            return;
        }
        dx = 0;
        dx_pct = false;
    }

    int inc_w = 1;
    int inc_h = 1;
    view_get_resize_incs(view, &inc_w, &inc_h);
    if (inc_w < 1) {
        inc_w = 1;
    }
    if (inc_h < 1) {
        inc_h = 1;
    }

    int steps_w = 0;
    int steps_h = 0;
    if (dx_pct) {
        steps_w = cal_percentage_value_of(dx, inner_w0) / inc_w;
    } else {
        steps_w = dx;
    }
    if (dy_pct) {
        steps_h = cal_percentage_value_of(dy, inner_h0) / inc_h;
    } else {
        steps_h = dy;
    }

    const int inner_w = inner_w0 + steps_w * inc_w;
    const int inner_h = inner_h0 + steps_h * inc_h;

    int content_w = inner_w;
    int content_h = inner_h - title_h;
    if (content_w < 1) {
        content_w = 1;
    }
    if (content_h < 1) {
        content_h = 1;
    }

    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL && !view_ignores_size_hints(view)) {
        fbwl_xwayland_apply_size_hints(view->xwayland_surface, &content_w, &content_h, false);
    }

    view_apply_resize(server, view, content_w, content_h, kind == 0 ? "resize" : (kind == 2 ? "resizevertical" : "resizehorizontal"));
    wlr_log(WLR_INFO, "Resize: %s w=%d h=%d steps=%d,%d inc=%d,%d args=%s",
        fbwl_view_display_title(view),
        content_w, content_h,
        steps_w, steps_h,
        inc_w, inc_h,
        args != NULL ? args : "");
    server_strict_mousefocus_recheck(server, "resize");
}
