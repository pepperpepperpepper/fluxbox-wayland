#include "wayland/fbwl_resize_edges.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/edges.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"

static bool test_corner(int xy, int wh, int corner_size_px, int corner_size_pc) {
    if (xy < corner_size_px) {
        return true;
    }
    if (corner_size_pc <= 0) {
        return false;
    }
    const long long lhs = 100LL * (long long)xy;
    const long long rhs = (long long)corner_size_pc * (long long)wh;
    return lhs < rhs;
}

static uint32_t edges_from_edge_or_corner(const struct fbwl_view *view, int cursor_x, int cursor_y,
        int corner_size_px, int corner_size_pc) {
    if (view == NULL) {
        return WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
    }

    if (corner_size_px < 0) {
        corner_size_px = 0;
    }
    if (corner_size_pc < 0) {
        corner_size_pc = 0;
    }
    if (corner_size_pc > 100) {
        corner_size_pc = 100;
    }

    const int content_w = fbwl_view_current_width(view);
    const int content_h = fbwl_view_current_height(view);
    if (content_w < 1 || content_h < 1) {
        return WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
    }

    int ext_left = 0, ext_top = 0, ext_right = 0, ext_bottom = 0;
    const struct fbwl_server *server = view->server;
    fbwl_view_decor_frame_extents(view, server != NULL ? &server->decor_theme : NULL,
        &ext_left, &ext_top, &ext_right, &ext_bottom);

    const int frame_w = content_w + ext_left + ext_right;
    const int frame_h = content_h + ext_top + ext_bottom;
    if (frame_w < 1 || frame_h < 1) {
        return WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
    }

    const int frame_x = view->x - ext_left;
    const int frame_y = view->y - ext_top;
    const int x = cursor_x - frame_x;
    const int y = cursor_y - frame_y;

    const int cx = frame_w / 2;
    const int cy = frame_h / 2;

    if (x < cx && test_corner(x, cx, corner_size_px, corner_size_pc)) {
        if (y < cy && test_corner(y, cy, corner_size_px, corner_size_pc)) {
            return WLR_EDGE_LEFT | WLR_EDGE_TOP;
        }
        if (test_corner(frame_h - y - 1, frame_h - cy, corner_size_px, corner_size_pc)) {
            return WLR_EDGE_LEFT | WLR_EDGE_BOTTOM;
        }
    } else if (test_corner(frame_w - x - 1, frame_w - cx, corner_size_px, corner_size_pc)) {
        if (y < cy && test_corner(y, cy, corner_size_px, corner_size_pc)) {
            return WLR_EDGE_RIGHT | WLR_EDGE_TOP;
        }
        if (test_corner(frame_h - y - 1, frame_h - cy, corner_size_px, corner_size_pc)) {
            return WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
        }
    }

    // Not a corner; find the nearest edge.
    if (cy - abs(y - cy) < cx - abs(x - cx)) {
        return y > cy ? WLR_EDGE_BOTTOM : WLR_EDGE_TOP;
    }
    return x > cx ? WLR_EDGE_RIGHT : WLR_EDGE_LEFT;
}

uint32_t fbwl_resize_edges_from_startresizing_args(const struct fbwl_view *view,
        int cursor_x, int cursor_y, const char *args) {
    const uint32_t default_edges = WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
    const uint32_t center_edges = WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM;

    if (args == NULL || *args == '\0') {
        return default_edges;
    }

    char *dup = strdup(args);
    if (dup == NULL) {
        return default_edges;
    }

    char *save = NULL;
    const char *tok = strtok_r(dup, " \t", &save);
    if (tok == NULL || *tok == '\0') {
        free(dup);
        return default_edges;
    }

    if (strcasecmp(tok, "center") == 0) {
        free(dup);
        return center_edges;
    }
    if (strcasecmp(tok, "topleft") == 0) {
        free(dup);
        return WLR_EDGE_TOP | WLR_EDGE_LEFT;
    }
    if (strcasecmp(tok, "top") == 0) {
        free(dup);
        return WLR_EDGE_TOP;
    }
    if (strcasecmp(tok, "topright") == 0) {
        free(dup);
        return WLR_EDGE_TOP | WLR_EDGE_RIGHT;
    }
    if (strcasecmp(tok, "left") == 0) {
        free(dup);
        return WLR_EDGE_LEFT;
    }
    if (strcasecmp(tok, "right") == 0) {
        free(dup);
        return WLR_EDGE_RIGHT;
    }
    if (strcasecmp(tok, "bottomleft") == 0) {
        free(dup);
        return WLR_EDGE_BOTTOM | WLR_EDGE_LEFT;
    }
    if (strcasecmp(tok, "bottom") == 0) {
        free(dup);
        return WLR_EDGE_BOTTOM;
    }
    if (strcasecmp(tok, "bottomright") == 0) {
        free(dup);
        return WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
    }

    if (strcasecmp(tok, "nearestcorner") == 0) {
        free(dup);
        return edges_from_edge_or_corner(view, cursor_x, cursor_y, 0, 100);
    }
    if (strcasecmp(tok, "nearestedge") == 0) {
        free(dup);
        return edges_from_edge_or_corner(view, cursor_x, cursor_y, 0, 0);
    }
    if (strcasecmp(tok, "nearestcorneroredge") == 0) {
        int corner_size_px = 50;
        int corner_size_pc = 30;
        const char *tok2 = strtok_r(NULL, " \t", &save);
        const char *tok3 = strtok_r(NULL, " \t", &save);
        if (tok2 != NULL && *tok2 != '\0') {
            corner_size_px = 0;
            corner_size_pc = 0;
            const size_t len = strlen(tok2);
            if (len > 0 && tok2[len - 1] == '%') {
                corner_size_pc = atoi(tok2);
            } else {
                corner_size_px = atoi(tok2);
                if (tok3 != NULL && *tok3 != '\0') {
                    corner_size_pc = atoi(tok3);
                }
            }
        }
        free(dup);
        return edges_from_edge_or_corner(view, cursor_x, cursor_y, corner_size_px, corner_size_pc);
    }

    free(dup);
    return default_edges;
}

