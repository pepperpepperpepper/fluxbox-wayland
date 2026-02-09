#pragma once

#include <stdint.h>
#include <stdbool.h>

struct fbwl_view;
struct wl_list;
struct wl_event_source;
struct wlr_cursor;
struct wlr_output_layout;
struct wlr_scene_rect;
struct wlr_scene_tree;

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
    bool tab_attach_enabled;
    double grab_x, grab_y;
    int view_x, view_y;
    int view_w, view_h;
    int last_w, last_h;

    bool last_cursor_valid;
    int last_cursor_x, last_cursor_y;

    bool pending_valid;
    int pending_x, pending_y;
    int pending_w, pending_h;

    struct wlr_scene_tree *outline_tree;
    struct wlr_scene_rect *outline_top;
    struct wlr_scene_rect *outline_bottom;
    struct wlr_scene_rect *outline_left;
    struct wlr_scene_rect *outline_right;

    struct wl_event_source *resize_timer;
};

void fbwl_grab_begin_move(struct fbwl_grab *grab, struct fbwl_view *view, struct wlr_cursor *cursor,
    uint32_t button);
void fbwl_grab_begin_tabbing(struct fbwl_grab *grab, struct fbwl_view *view, struct wlr_cursor *cursor,
    uint32_t button);
void fbwl_grab_begin_resize(struct fbwl_grab *grab, struct fbwl_view *view, struct wlr_cursor *cursor,
    uint32_t button, uint32_t edges);
void fbwl_grab_commit(struct fbwl_grab *grab, struct wlr_output_layout *output_layout, const char *why);
void fbwl_grab_end(struct fbwl_grab *grab);
void fbwl_grab_update(struct fbwl_grab *grab, struct wlr_cursor *cursor,
    struct wlr_output_layout *output_layout, struct wl_list *outputs,
    int edge_snap_threshold_px, int edge_resize_snap_threshold_px,
    bool opaque_move, bool opaque_resize, int opaque_resize_delay_ms);
