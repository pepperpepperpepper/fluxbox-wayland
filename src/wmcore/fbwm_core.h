#pragma once

#include <stdbool.h>
#include <stddef.h>

struct fbwm_box;
struct fbwm_view;
struct fbwm_output;

struct fbwm_view_ops {
    void (*focus)(struct fbwm_view *view);
    bool (*is_mapped)(const struct fbwm_view *view);
    bool (*get_box)(const struct fbwm_view *view, struct fbwm_box *out);
    int (*head)(const struct fbwm_view *view);
    const char *(*title)(const struct fbwm_view *view);
    const char *(*app_id)(const struct fbwm_view *view);
};

struct fbwm_view {
    const struct fbwm_view_ops *ops;
    void *userdata;

    struct fbwm_view *prev;
    struct fbwm_view *next;

    int workspace;
    bool sticky;
};

typedef bool (*fbwm_core_refocus_filter_fn)(void *userdata, const struct fbwm_view *candidate,
        const struct fbwm_view *reference);

enum fbwm_window_placement_strategy {
    FBWM_PLACE_ROW_SMART = 0,
    FBWM_PLACE_COL_SMART,
    FBWM_PLACE_CASCADE,
    FBWM_PLACE_UNDER_MOUSE,
    FBWM_PLACE_ROW_MIN_OVERLAP,
    FBWM_PLACE_COL_MIN_OVERLAP,
    FBWM_PLACE_AUTOTAB,
};

enum fbwm_row_placement_direction {
    FBWM_ROW_LEFT_TO_RIGHT = 0,
    FBWM_ROW_RIGHT_TO_LEFT,
};

enum fbwm_col_placement_direction {
    FBWM_COL_TOP_TO_BOTTOM = 0,
    FBWM_COL_BOTTOM_TO_TOP,
};

struct fbwm_core {
    struct fbwm_view views;
    struct fbwm_view *focused;

    fbwm_core_refocus_filter_fn refocus_filter;
    void *refocus_filter_userdata;

    int workspace_current;
    int workspace_prev;
    int *workspace_current_by_head;
    int *workspace_prev_by_head;
    size_t workspace_current_by_head_len;
    int workspace_count;
    char **workspace_names;
    size_t workspace_names_len;

    enum fbwm_window_placement_strategy placement_strategy;
    enum fbwm_row_placement_direction placement_row_dir;
    enum fbwm_col_placement_direction placement_col_dir;

    int place_next_x;
    int place_next_y;
};

void fbwm_view_init(struct fbwm_view *view, const struct fbwm_view_ops *ops, void *userdata);

void fbwm_core_init(struct fbwm_core *core);
void fbwm_core_finish(struct fbwm_core *core);

void fbwm_core_view_map(struct fbwm_core *core, struct fbwm_view *view);
void fbwm_core_view_unmap(struct fbwm_core *core, struct fbwm_view *view);
void fbwm_core_view_destroy(struct fbwm_core *core, struct fbwm_view *view);

void fbwm_core_set_refocus_filter(struct fbwm_core *core, fbwm_core_refocus_filter_fn filter, void *userdata);

void fbwm_core_focus_view(struct fbwm_core *core, struct fbwm_view *view);
void fbwm_core_focus_view_with_reason(struct fbwm_core *core, struct fbwm_view *view, const char *why);
void fbwm_core_focus_next(struct fbwm_core *core);
void fbwm_core_focus_prev(struct fbwm_core *core);
void fbwm_core_refocus(struct fbwm_core *core);

void fbwm_core_set_workspace_count(struct fbwm_core *core, int count);
int fbwm_core_workspace_count(const struct fbwm_core *core);
int fbwm_core_workspace_current(const struct fbwm_core *core);
void fbwm_core_set_head_count(struct fbwm_core *core, size_t head_count);
size_t fbwm_core_head_count(const struct fbwm_core *core);
int fbwm_core_workspace_current_for_head(const struct fbwm_core *core, size_t head);
int fbwm_core_workspace_prev_for_head(const struct fbwm_core *core, size_t head);
void fbwm_core_clear_workspace_names(struct fbwm_core *core);
bool fbwm_core_set_workspace_name(struct fbwm_core *core, int workspace, const char *name);
const char *fbwm_core_workspace_name(const struct fbwm_core *core, int workspace);
size_t fbwm_core_workspace_names_len(const struct fbwm_core *core);

enum fbwm_window_placement_strategy fbwm_core_window_placement(const struct fbwm_core *core);
void fbwm_core_set_window_placement(struct fbwm_core *core, enum fbwm_window_placement_strategy strategy);
enum fbwm_row_placement_direction fbwm_core_row_placement_direction(const struct fbwm_core *core);
void fbwm_core_set_row_placement_direction(struct fbwm_core *core, enum fbwm_row_placement_direction dir);
enum fbwm_col_placement_direction fbwm_core_col_placement_direction(const struct fbwm_core *core);
void fbwm_core_set_col_placement_direction(struct fbwm_core *core, enum fbwm_col_placement_direction dir);

bool fbwm_core_view_is_visible(const struct fbwm_core *core, const struct fbwm_view *view);
void fbwm_core_workspace_switch(struct fbwm_core *core, int workspace);
void fbwm_core_workspace_switch_on_head(struct fbwm_core *core, size_t head, int workspace);
void fbwm_core_move_focused_to_workspace(struct fbwm_core *core, int workspace);

void fbwm_core_place_next(struct fbwm_core *core, const struct fbwm_output *output,
        int view_width, int view_height, int cursor_x, int cursor_y, int *x, int *y);
