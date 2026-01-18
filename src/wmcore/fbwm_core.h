#pragma once

#include <stdbool.h>

struct fbwm_view;

struct fbwm_view_ops {
    void (*focus)(struct fbwm_view *view);
    bool (*is_mapped)(const struct fbwm_view *view);
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

struct fbwm_core {
    struct fbwm_view views;
    struct fbwm_view *focused;

    int workspace_current;
    int workspace_count;
};

void fbwm_view_init(struct fbwm_view *view, const struct fbwm_view_ops *ops, void *userdata);

void fbwm_core_init(struct fbwm_core *core);

void fbwm_core_view_map(struct fbwm_core *core, struct fbwm_view *view);
void fbwm_core_view_unmap(struct fbwm_core *core, struct fbwm_view *view);
void fbwm_core_view_destroy(struct fbwm_core *core, struct fbwm_view *view);

void fbwm_core_focus_view(struct fbwm_core *core, struct fbwm_view *view);
void fbwm_core_focus_next(struct fbwm_core *core);
void fbwm_core_refocus(struct fbwm_core *core);

void fbwm_core_set_workspace_count(struct fbwm_core *core, int count);
int fbwm_core_workspace_count(const struct fbwm_core *core);
int fbwm_core_workspace_current(const struct fbwm_core *core);

bool fbwm_core_view_is_visible(const struct fbwm_core *core, const struct fbwm_view *view);
void fbwm_core_workspace_switch(struct fbwm_core *core, int workspace);
void fbwm_core_move_focused_to_workspace(struct fbwm_core *core, int workspace);
