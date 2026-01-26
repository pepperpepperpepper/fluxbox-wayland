#include "fbwm_core.h"

#include "fbwm_output.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool view_in_list(const struct fbwm_view *view) {
    return view != NULL && view->prev != NULL && view->next != NULL;
}

static bool view_is_mapped(const struct fbwm_view *view) {
    if (view == NULL) {
        return false;
    }
    if (view->ops != NULL && view->ops->is_mapped != NULL) {
        return view->ops->is_mapped(view);
    }
    return true;
}

static bool view_is_visible(const struct fbwm_core *core, const struct fbwm_view *view) {
    if (core == NULL || view == NULL) {
        return false;
    }
    if (!view_in_list(view)) {
        return false;
    }
    if (!view_is_mapped(view)) {
        return false;
    }
    if (view->sticky) {
        return true;
    }
    return view->workspace == core->workspace_current;
}

static void list_insert_after(struct fbwm_view *pos, struct fbwm_view *view) {
    view->next = pos->next;
    view->prev = pos;
    pos->next->prev = view;
    pos->next = view;
}

static void list_remove(struct fbwm_view *view) {
    view->prev->next = view->next;
    view->next->prev = view->prev;
    view->prev = NULL;
    view->next = NULL;
}

static const char *safe_str(const char *s) {
    return s != NULL ? s : "(null)";
}

static void log_focus(const struct fbwm_view *view, const char *why) {
    if (view == NULL || view->ops == NULL) {
        return;
    }
    const char *title = view->ops->title ? view->ops->title(view) : NULL;
    const char *app_id = view->ops->app_id ? view->ops->app_id(view) : NULL;
    fprintf(stderr, "Policy: focus (%s) title=%s app_id=%s\n",
        safe_str(why), safe_str(title), safe_str(app_id));
}

void fbwm_view_init(struct fbwm_view *view, const struct fbwm_view_ops *ops, void *userdata) {
    if (view == NULL) {
        return;
    }
    view->ops = ops;
    view->userdata = userdata;
    view->prev = NULL;
    view->next = NULL;
    view->workspace = 0;
    view->sticky = false;
}

void fbwm_core_init(struct fbwm_core *core) {
    if (core == NULL) {
        return;
    }

    core->views.ops = NULL;
    core->views.userdata = NULL;
    core->views.prev = &core->views;
    core->views.next = &core->views;
    core->focused = NULL;

    core->workspace_current = 0;
    core->workspace_count = 4;
    core->workspace_names = NULL;
    core->workspace_names_len = 0;

    core->placement_strategy = FBWM_PLACE_ROW_SMART;
    core->placement_row_dir = FBWM_ROW_LEFT_TO_RIGHT;
    core->placement_col_dir = FBWM_COL_TOP_TO_BOTTOM;

    core->place_next_x = 64;
    core->place_next_y = 64;
}

void fbwm_core_finish(struct fbwm_core *core) {
    fbwm_core_clear_workspace_names(core);
}

void fbwm_core_view_map(struct fbwm_core *core, struct fbwm_view *view) {
    if (core == NULL || view == NULL) {
        return;
    }
    if (view_in_list(view)) {
        return;
    }
    if (view->workspace < 0 || view->workspace >= core->workspace_count) {
        view->workspace = core->workspace_current;
    }
    list_insert_after(&core->views, view);
}

static void refocus_if_needed(struct fbwm_core *core) {
    if (core == NULL) {
        return;
    }
    if (core->focused != NULL && view_is_visible(core, core->focused)) {
        return;
    }

    core->focused = NULL;
    for (struct fbwm_view *walk = core->views.next; walk != &core->views; walk = walk->next) {
        if (view_is_visible(core, walk)) {
            fbwm_core_focus_view(core, walk);
            return;
        }
    }
}

void fbwm_core_refocus(struct fbwm_core *core) {
    refocus_if_needed(core);
}

void fbwm_core_view_unmap(struct fbwm_core *core, struct fbwm_view *view) {
    if (core == NULL || view == NULL) {
        return;
    }
    if (!view_in_list(view)) {
        return;
    }

    const bool was_focused = core->focused == view;
    list_remove(view);
    if (was_focused) {
        refocus_if_needed(core);
    }
}

void fbwm_core_view_destroy(struct fbwm_core *core, struct fbwm_view *view) {
    fbwm_core_view_unmap(core, view);
}

void fbwm_core_focus_view(struct fbwm_core *core, struct fbwm_view *view) {
    if (core == NULL || view == NULL) {
        return;
    }

    if (!view_is_visible(core, view)) {
        return;
    }

    list_remove(view);
    list_insert_after(&core->views, view);
    core->focused = view;

    log_focus(view, "direct");

    if (view->ops != NULL && view->ops->focus != NULL) {
        view->ops->focus(view);
    }
}

void fbwm_core_focus_next(struct fbwm_core *core) {
    if (core == NULL) {
        return;
    }

    struct fbwm_view *candidate = NULL;
    for (struct fbwm_view *walk = core->views.prev; walk != &core->views; walk = walk->prev) {
        if (view_is_visible(core, walk)) {
            candidate = walk;
            break;
        }
    }
    if (candidate == NULL || candidate == core->focused) {
        return;
    }

    log_focus(candidate, "cycle");
    fbwm_core_focus_view(core, candidate);
}

void fbwm_core_set_workspace_count(struct fbwm_core *core, int count) {
    if (core == NULL) {
        return;
    }
    if (count < 1) {
        count = 1;
    }
    core->workspace_count = count;
    if (core->workspace_current >= core->workspace_count) {
        core->workspace_current = core->workspace_count - 1;
    }
    refocus_if_needed(core);
}

int fbwm_core_workspace_count(const struct fbwm_core *core) {
    return core != NULL ? core->workspace_count : 0;
}

int fbwm_core_workspace_current(const struct fbwm_core *core) {
    return core != NULL ? core->workspace_current : 0;
}

void fbwm_core_clear_workspace_names(struct fbwm_core *core) {
    if (core == NULL) {
        return;
    }
    if (core->workspace_names != NULL) {
        for (size_t i = 0; i < core->workspace_names_len; i++) {
            free(core->workspace_names[i]);
        }
        free(core->workspace_names);
    }
    core->workspace_names = NULL;
    core->workspace_names_len = 0;
}

bool fbwm_core_set_workspace_name(struct fbwm_core *core, int workspace, const char *name) {
    if (core == NULL) {
        return false;
    }
    if (workspace < 0) {
        return false;
    }

    size_t needed = (size_t)workspace + 1;
    if (needed > core->workspace_names_len) {
        void *p = realloc(core->workspace_names, needed * sizeof(core->workspace_names[0]));
        if (p == NULL) {
            return false;
        }
        core->workspace_names = p;
        for (size_t i = core->workspace_names_len; i < needed; i++) {
            core->workspace_names[i] = NULL;
        }
        core->workspace_names_len = needed;
    }

    char *dup = NULL;
    if (name != NULL && *name != '\0') {
        dup = strdup(name);
        if (dup == NULL) {
            return false;
        }
    }

    free(core->workspace_names[workspace]);
    core->workspace_names[workspace] = dup;
    return true;
}

const char *fbwm_core_workspace_name(const struct fbwm_core *core, int workspace) {
    if (core == NULL) {
        return NULL;
    }
    if (workspace < 0) {
        return NULL;
    }
    if (core->workspace_names == NULL || (size_t)workspace >= core->workspace_names_len) {
        return NULL;
    }
    return core->workspace_names[workspace];
}

enum fbwm_window_placement_strategy fbwm_core_window_placement(const struct fbwm_core *core) {
    return core != NULL ? core->placement_strategy : FBWM_PLACE_ROW_SMART;
}

void fbwm_core_set_window_placement(struct fbwm_core *core, enum fbwm_window_placement_strategy strategy) {
    if (core == NULL) {
        return;
    }
    core->placement_strategy = strategy;
}

enum fbwm_row_placement_direction fbwm_core_row_placement_direction(const struct fbwm_core *core) {
    return core != NULL ? core->placement_row_dir : FBWM_ROW_LEFT_TO_RIGHT;
}

void fbwm_core_set_row_placement_direction(struct fbwm_core *core, enum fbwm_row_placement_direction dir) {
    if (core == NULL) {
        return;
    }
    core->placement_row_dir = dir;
}

enum fbwm_col_placement_direction fbwm_core_col_placement_direction(const struct fbwm_core *core) {
    return core != NULL ? core->placement_col_dir : FBWM_COL_TOP_TO_BOTTOM;
}

void fbwm_core_set_col_placement_direction(struct fbwm_core *core, enum fbwm_col_placement_direction dir) {
    if (core == NULL) {
        return;
    }
    core->placement_col_dir = dir;
}

bool fbwm_core_view_is_visible(const struct fbwm_core *core, const struct fbwm_view *view) {
    return view_is_visible(core, view);
}

void fbwm_core_workspace_switch(struct fbwm_core *core, int workspace) {
    if (core == NULL) {
        return;
    }
    if (workspace < 0 || workspace >= core->workspace_count) {
        return;
    }
    if (workspace == core->workspace_current) {
        return;
    }

    core->workspace_current = workspace;
    fprintf(stderr, "Policy: workspace switch to %d\n", workspace + 1);

    refocus_if_needed(core);
}

void fbwm_core_move_focused_to_workspace(struct fbwm_core *core, int workspace) {
    if (core == NULL) {
        return;
    }
    if (workspace < 0 || workspace >= core->workspace_count) {
        return;
    }
    if (core->focused == NULL) {
        return;
    }

    struct fbwm_view *view = core->focused;
    view->workspace = workspace;
    const char *title = view->ops && view->ops->title ? view->ops->title(view) : NULL;
    const char *app_id = view->ops && view->ops->app_id ? view->ops->app_id(view) : NULL;
    fprintf(stderr, "Policy: move focused to workspace %d title=%s app_id=%s\n",
        workspace + 1, safe_str(title), safe_str(app_id));

    refocus_if_needed(core);
}

static int clamp_i32(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static bool boxes_intersect(const struct fbwm_box *a, const struct fbwm_box *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    if (a->width < 1 || a->height < 1 || b->width < 1 || b->height < 1) {
        return false;
    }
    return a->x < b->x + b->width &&
        a->x + a->width > b->x &&
        a->y < b->y + b->height &&
        a->y + a->height > b->y;
}

static int64_t overlap_area(const struct fbwm_box *a, const struct fbwm_box *b) {
    if (!boxes_intersect(a, b)) {
        return 0;
    }
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = (a->x + a->width) < (b->x + b->width) ? (a->x + a->width) : (b->x + b->width);
    int y1 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);
    int w = x1 - x0;
    int h = y1 - y0;
    if (w < 1 || h < 1) {
        return 0;
    }
    return (int64_t)w * (int64_t)h;
}

static size_t gather_visible_view_boxes(const struct fbwm_core *core, struct fbwm_box *out, size_t cap) {
    if (core == NULL || out == NULL || cap == 0) {
        return 0;
    }

    size_t len = 0;
    for (struct fbwm_view *walk = core->views.next; walk != &core->views; walk = walk->next) {
        if (!view_is_visible(core, walk)) {
            continue;
        }
        if (walk->ops == NULL || walk->ops->get_box == NULL) {
            continue;
        }
        struct fbwm_box box = {0};
        if (!walk->ops->get_box(walk, &box) || box.width < 1 || box.height < 1) {
            continue;
        }
        if (len >= cap) {
            break;
        }
        out[len++] = box;
    }
    return len;
}

static void sort_ints(int *vals, size_t len) {
    if (vals == NULL || len < 2) {
        return;
    }
    for (size_t i = 1; i < len; i++) {
        int v = vals[i];
        size_t j = i;
        while (j > 0 && vals[j - 1] > v) {
            vals[j] = vals[j - 1];
            j--;
        }
        vals[j] = v;
    }
}

static size_t dedup_ints(int *vals, size_t len) {
    if (vals == NULL || len == 0) {
        return 0;
    }
    size_t out = 1;
    for (size_t i = 1; i < len; i++) {
        if (vals[i] != vals[out - 1]) {
            vals[out++] = vals[i];
        }
    }
    return out;
}

static void place_cascade(struct fbwm_core *core, const struct fbwm_box *usable, int view_w, int view_h, int *x, int *y) {
    int px = core->place_next_x;
    int py = core->place_next_y;

    const int step = 32;
    core->place_next_x += step;
    core->place_next_y += step;

    if (usable != NULL && usable->width > 0 && usable->height > 0) {
        int min_x = usable->x;
        int min_y = usable->y;
        int max_x = usable->x + usable->width - (view_w > 0 ? view_w : 0);
        int max_y = usable->y + usable->height - (view_h > 0 ? view_h : 0);
        if (max_x < min_x) {
            max_x = min_x;
        }
        if (max_y < min_y) {
            max_y = min_y;
        }

        px += usable->x;
        py += usable->y;

        if (px < min_x || px > max_x || py < min_y || py > max_y) {
            px = min_x;
            py = min_y;
            core->place_next_x = step;
            core->place_next_y = step;
        }
    }

    *x = px;
    *y = py;
}

static void place_under_mouse(const struct fbwm_box *usable, int view_w, int view_h, int cursor_x, int cursor_y, int *x, int *y) {
    int px = cursor_x;
    int py = cursor_y;

    if (usable != NULL && usable->width > 0 && usable->height > 0) {
        const int min_x = usable->x;
        const int min_y = usable->y;
        int max_x = usable->x + usable->width - view_w;
        int max_y = usable->y + usable->height - view_h;
        if (max_x < min_x) {
            max_x = min_x;
        }
        if (max_y < min_y) {
            max_y = min_y;
        }
        px = clamp_i32(px, min_x, max_x);
        py = clamp_i32(py, min_y, max_y);
    }

    *x = px;
    *y = py;
}

static void place_rowcol_smart(struct fbwm_core *core, const struct fbwm_box *usable,
        int view_w, int view_h, bool scan_rows, bool allow_overlap, int *x, int *y) {
    if (core == NULL || x == NULL || y == NULL) {
        return;
    }

    struct fbwm_box area = {0};
    if (usable != NULL) {
        area = *usable;
    }

    const int w = view_w > 0 ? view_w : 256;
    const int h = view_h > 0 ? view_h : 256;

    int min_x = area.x;
    int min_y = area.y;
    int max_x = area.x + area.width - w;
    int max_y = area.y + area.height - h;
    if (area.width < 1 || area.height < 1) {
        min_x = 0;
        min_y = 0;
        max_x = 0;
        max_y = 0;
    } else {
        if (max_x < min_x) {
            max_x = min_x;
        }
        if (max_y < min_y) {
            max_y = min_y;
        }
    }

    struct fbwm_box view_boxes[256];
    size_t view_boxes_len = gather_visible_view_boxes(core, view_boxes, sizeof(view_boxes) / sizeof(view_boxes[0]));

    int xs[1024];
    int ys[1024];
    size_t xs_len = 0;
    size_t ys_len = 0;

    xs[xs_len++] = min_x;
    xs[xs_len++] = max_x;
    ys[ys_len++] = min_y;
    ys[ys_len++] = max_y;

    for (size_t i = 0; i < view_boxes_len; i++) {
        const struct fbwm_box *r = &view_boxes[i];
        const int cand_x[] = {r->x, r->x + r->width, r->x - w, r->x + r->width - w};
        const int cand_y[] = {r->y, r->y + r->height, r->y - h, r->y + r->height - h};

        for (size_t j = 0; j < sizeof(cand_x) / sizeof(cand_x[0]) && xs_len < sizeof(xs) / sizeof(xs[0]); j++) {
            xs[xs_len++] = clamp_i32(cand_x[j], min_x, max_x);
        }
        for (size_t j = 0; j < sizeof(cand_y) / sizeof(cand_y[0]) && ys_len < sizeof(ys) / sizeof(ys[0]); j++) {
            ys[ys_len++] = clamp_i32(cand_y[j], min_y, max_y);
        }
    }

    sort_ints(xs, xs_len);
    sort_ints(ys, ys_len);
    xs_len = dedup_ints(xs, xs_len);
    ys_len = dedup_ints(ys, ys_len);

    const bool reverse_x = core->placement_row_dir == FBWM_ROW_RIGHT_TO_LEFT;
    const bool reverse_y = core->placement_col_dir == FBWM_COL_BOTTOM_TO_TOP;

    struct fbwm_box candidate = {0};
    candidate.width = w;
    candidate.height = h;

    int best_x = min_x;
    int best_y = min_y;
    int64_t best_overlap = -1;

    size_t outer_len = scan_rows ? ys_len : xs_len;
    size_t inner_len = scan_rows ? xs_len : ys_len;

    for (size_t outer = 0; outer < outer_len; outer++) {
        size_t outer_idx = outer;
        if ((scan_rows && reverse_y) || (!scan_rows && reverse_x)) {
            outer_idx = outer_len - 1 - outer;
        }

        for (size_t inner = 0; inner < inner_len; inner++) {
            size_t inner_idx = inner;
            if ((scan_rows && reverse_x) || (!scan_rows && reverse_y)) {
                inner_idx = inner_len - 1 - inner;
            }

            int px = scan_rows ? xs[inner_idx] : xs[outer_idx];
            int py = scan_rows ? ys[outer_idx] : ys[inner_idx];

            candidate.x = px;
            candidate.y = py;

            int64_t total_overlap = 0;
            for (size_t i = 0; i < view_boxes_len; i++) {
                total_overlap += overlap_area(&candidate, &view_boxes[i]);
                if (!allow_overlap && total_overlap > 0) {
                    break;
                }
            }

            if (!allow_overlap && total_overlap == 0) {
                *x = px;
                *y = py;
                return;
            }

            if (allow_overlap) {
                if (best_overlap < 0 || total_overlap < best_overlap) {
                    best_overlap = total_overlap;
                    best_x = px;
                    best_y = py;
                    if (best_overlap == 0) {
                        *x = best_x;
                        *y = best_y;
                        return;
                    }
                }
            }
        }
    }

    *x = best_x;
    *y = best_y;
}

void fbwm_core_place_next(struct fbwm_core *core, const struct fbwm_output *output,
        int view_width, int view_height, int cursor_x, int cursor_y, int *x, int *y) {
    if (core == NULL || x == NULL || y == NULL) {
        return;
    }

    struct fbwm_box usable = {0};
    if (output != NULL) {
        if (!fbwm_output_get_usable_box(output, &usable) || usable.width < 1 || usable.height < 1) {
            (void)fbwm_output_get_full_box(output, &usable);
        }
    }

    switch (core->placement_strategy) {
    case FBWM_PLACE_ROW_SMART:
        place_rowcol_smart(core, &usable, view_width, view_height, true, false, x, y);
        return;
    case FBWM_PLACE_COL_SMART:
        place_rowcol_smart(core, &usable, view_width, view_height, false, false, x, y);
        return;
    case FBWM_PLACE_ROW_MIN_OVERLAP:
        place_rowcol_smart(core, &usable, view_width, view_height, true, true, x, y);
        return;
    case FBWM_PLACE_COL_MIN_OVERLAP:
        place_rowcol_smart(core, &usable, view_width, view_height, false, true, x, y);
        return;
    case FBWM_PLACE_UNDER_MOUSE:
        place_under_mouse(&usable, view_width > 0 ? view_width : 256, view_height > 0 ? view_height : 256,
            cursor_x, cursor_y, x, y);
        return;
    case FBWM_PLACE_AUTOTAB:
        place_rowcol_smart(core, &usable, view_width, view_height, true, false, x, y);
        return;
    case FBWM_PLACE_CASCADE:
    default:
        place_cascade(core, &usable, view_width, view_height, x, y);
        return;
    }
}
