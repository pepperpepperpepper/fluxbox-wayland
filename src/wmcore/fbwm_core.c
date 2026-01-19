#include "fbwm_core.h"

#include "fbwm_output.h"

#include <stddef.h>
#include <stdio.h>

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

    core->place_next_x = 64;
    core->place_next_y = 64;
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

void fbwm_core_place_next(struct fbwm_core *core, const struct fbwm_output *output, int *x, int *y) {
    if (core == NULL || x == NULL || y == NULL) {
        return;
    }

    struct fbwm_box box = {0};
    if (output != NULL) {
        if (!fbwm_output_get_usable_box(output, &box) || box.width < 1 || box.height < 1) {
            (void)fbwm_output_get_full_box(output, &box);
        }
    }

    int px = core->place_next_x;
    int py = core->place_next_y;
    core->place_next_x = (core->place_next_x + 32) % 256;
    core->place_next_y = (core->place_next_y + 32) % 256;

    if (box.width > 0 && box.height > 0) {
        px += box.x;
        py += box.y;

        if (px < box.x || px >= box.x + box.width) {
            px = box.x;
        }
        if (py < box.y || py >= box.y + box.height) {
            py = box.y;
        }
    }

    *x = px;
    *y = py;
}
