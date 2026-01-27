#pragma once

#include <stdint.h>

struct fbwl_view;
struct wlr_cursor;

enum fbwl_cursor_mode {
    FBWL_CURSOR_PASSTHROUGH,
    FBWL_CURSOR_MOVE,
    FBWL_CURSOR_RESIZE,
};

struct fbwl_grab {
    enum fbwl_cursor_mode mode;
    struct fbwl_view *view;
    uint32_t button;
    uint32_t resize_edges;
    double grab_x, grab_y;
    int view_x, view_y;
    int view_w, view_h;
    int last_w, last_h;
};

void fbwl_grab_begin_move(struct fbwl_grab *grab, struct fbwl_view *view, struct wlr_cursor *cursor,
    uint32_t button);
void fbwl_grab_begin_resize(struct fbwl_grab *grab, struct fbwl_view *view, struct wlr_cursor *cursor,
    uint32_t button, uint32_t edges);
void fbwl_grab_end(struct fbwl_grab *grab);
void fbwl_grab_update(struct fbwl_grab *grab, struct wlr_cursor *cursor);
