#include "wayland/fbwl_tabs.h"

#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"

struct fbwl_tab_group {
    struct wl_list link;  // fbwl_server.tab_groups
    struct wl_list views; // fbwl_view.tab_link
    struct fbwl_view *active;
};

void fbwl_tabs_init_defaults(struct fbwl_tabs_config *cfg) {
    if (cfg == NULL) {
        return;
    }

    *cfg = (struct fbwl_tabs_config){
        .intitlebar = true,
        .max_over = false,
        .use_pixmap = true,
        .placement = FBWL_TOOLBAR_PLACEMENT_TOP_LEFT,
        .width_px = 64,
        .padding_px = 0,
        .focus_model = FBWL_TAB_FOCUS_CLICK,
        .attach_area = FBWL_TABS_ATTACH_WINDOW,
    };
}

static size_t tab_group_size(const struct fbwl_tab_group *group) {
    if (group == NULL) {
        return 0;
    }

    size_t n = 0;
    const struct fbwl_view *v;
    wl_list_for_each(v, &group->views, tab_link) {
        n++;
    }
    return n;
}

static struct fbwl_tab_group *tab_group_create(struct fbwl_server *server) {
    if (server == NULL) {
        return NULL;
    }

    struct fbwl_tab_group *group = calloc(1, sizeof(*group));
    if (group == NULL) {
        return NULL;
    }

    wl_list_init(&group->views);
    group->active = NULL;
    wl_list_insert(&server->tab_groups, &group->link);
    return group;
}

static void tab_group_destroy(struct fbwl_tab_group *group) {
    if (group == NULL) {
        return;
    }
    wl_list_remove(&group->link);
    free(group);
}

bool fbwl_tabs_view_is_active(const struct fbwl_view *view) {
    if (view == NULL) {
        return false;
    }
    if (view->tab_group == NULL) {
        return true;
    }
    return view->tab_group->active == view;
}

static bool view_is_mapped_not_minimized(const struct fbwl_view *view) {
    return view != NULL && view->mapped && !view->minimized;
}

static struct fbwl_view *tab_group_pick_active_fallback(struct fbwl_tab_group *group) {
    if (group == NULL) {
        return NULL;
    }

    if (view_is_mapped_not_minimized(group->active)) {
        return group->active;
    }

    struct fbwl_view *v;
    wl_list_for_each(v, &group->views, tab_link) {
        if (view_is_mapped_not_minimized(v)) {
            return v;
        }
    }

    if (!wl_list_empty(&group->views)) {
        return wl_container_of(group->views.next, v, tab_link);
    }

    return NULL;
}

static void tab_group_set_active(struct fbwl_tab_group *group, struct fbwl_view *view, const char *reason) {
    if (group == NULL || view == NULL) {
        return;
    }
    if (group->active == view) {
        return;
    }

    group->active = view;
    wlr_log(WLR_INFO, "Tabs: activate reason=%s title=%s",
        reason != NULL ? reason : "(null)",
        fbwl_view_display_title(view));
}

static void tab_group_apply_visibility(struct fbwl_tab_group *group) {
    if (group == NULL) {
        return;
    }

    struct fbwl_view *active = tab_group_pick_active_fallback(group);
    if (active != NULL && group->active != active) {
        group->active = active;
    }

    struct fbwl_view *v;
    wl_list_for_each(v, &group->views, tab_link) {
        if (v == NULL || v->scene_tree == NULL) {
            continue;
        }
        const bool visible_ws = fbwm_core_view_is_visible(&v->server->wm, &v->wm_view);
        const bool enabled = visible_ws && (group->active == v);
        wlr_scene_node_set_enabled(&v->scene_tree->node, enabled);
    }
}

static void tab_group_repair_workspace(struct fbwl_tab_group *group) {
    if (group == NULL) {
        return;
    }

    struct fbwl_view *ref = tab_group_pick_active_fallback(group);
    if (ref == NULL) {
        return;
    }

    struct fbwl_view *v;
    wl_list_for_each(v, &group->views, tab_link) {
        if (v == NULL) {
            continue;
        }
        v->wm_view.workspace = ref->wm_view.workspace;
        v->wm_view.sticky = ref->wm_view.sticky;
    }
}

void fbwl_tabs_repair(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    struct fbwl_tab_group *group;
    wl_list_for_each(group, &server->tab_groups, link) {
        tab_group_repair_workspace(group);
        tab_group_apply_visibility(group);
    }
}

static void tabs_maybe_destroy_group(struct fbwl_tab_group *group) {
    if (group == NULL) {
        return;
    }

    const size_t n = tab_group_size(group);
    if (n >= 2) {
        return;
    }

    if (n == 1) {
        struct fbwl_view *v = wl_container_of(group->views.next, v, tab_link);
        wl_list_remove(&v->tab_link);
        wl_list_init(&v->tab_link);
        v->tab_group = NULL;
        if (v->scene_tree != NULL) {
            const bool visible_ws = fbwm_core_view_is_visible(&v->server->wm, &v->wm_view);
            wlr_scene_node_set_enabled(&v->scene_tree->node, visible_ws);
        }
    }

    tab_group_destroy(group);
}

static void tabs_add_to_group(struct fbwl_tab_group *group, struct fbwl_view *view) {
    if (group == NULL || view == NULL) {
        return;
    }
    view->tab_group = group;
    wl_list_insert(group->views.prev, &view->tab_link);
    if (group->active == NULL) {
        group->active = view;
    }
}

static void sync_view_to_anchor_geometry(struct fbwl_view *view, const struct fbwl_view *anchor) {
    if (view == NULL || anchor == NULL) {
        return;
    }

    view->x = anchor->x;
    view->y = anchor->y;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }

    const int w = fbwl_view_current_width(anchor);
    const int h = fbwl_view_current_height(anchor);
    if (w < 1 || h < 1) {
        return;
    }

    if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
    } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y, (uint16_t)w, (uint16_t)h);
    }
}

bool fbwl_tabs_attach(struct fbwl_view *view, struct fbwl_view *anchor, const char *reason) {
    if (view == NULL || anchor == NULL || view == anchor) {
        return false;
    }
    if (view->server == NULL || anchor->server != view->server) {
        return false;
    }
    if (view->tab_group != NULL) {
        return false;
    }

    if (!view_is_mapped_not_minimized(anchor)) {
        return false;
    }

    struct fbwl_tab_group *group = anchor->tab_group;
    if (group == NULL) {
        group = tab_group_create(view->server);
        if (group == NULL) {
            return false;
        }
        tabs_add_to_group(group, anchor);
        group->active = anchor;
    }

    tabs_add_to_group(group, view);
    sync_view_to_anchor_geometry(view, anchor);
    view->wm_view.workspace = anchor->wm_view.workspace;
    view->wm_view.sticky = anchor->wm_view.sticky;
    view->placed = true;

    wlr_log(WLR_INFO, "Tabs: attach reason=%s anchor=%s view=%s tabs=%zu",
        reason != NULL ? reason : "(null)",
        fbwl_view_display_title(anchor),
        fbwl_view_display_title(view),
        tab_group_size(group));
    return true;
}

void fbwl_tabs_detach(struct fbwl_view *view, const char *reason) {
    if (view == NULL || view->tab_group == NULL) {
        return;
    }

    struct fbwl_tab_group *group = view->tab_group;
    const bool was_active = group->active == view;

    wl_list_remove(&view->tab_link);
    wl_list_init(&view->tab_link);
    view->tab_group = NULL;

    if (was_active) {
        group->active = tab_group_pick_active_fallback(group);
    }

    wlr_log(WLR_INFO, "Tabs: detach reason=%s title=%s remaining=%zu",
        reason != NULL ? reason : "(null)",
        fbwl_view_display_title(view),
        tab_group_size(group));

    tabs_maybe_destroy_group(group);
    if (view->server != NULL) {
        fbwl_tabs_repair(view->server);
    }
}

void fbwl_tabs_activate(struct fbwl_view *view, const char *reason) {
    if (view == NULL || view->tab_group == NULL) {
        return;
    }
    tab_group_set_active(view->tab_group, view, reason);
    tab_group_apply_visibility(view->tab_group);
}

static struct fbwl_view *tab_group_pick_next_mapped(struct fbwl_tab_group *group, struct fbwl_view *start) {
    if (group == NULL || start == NULL) {
        return NULL;
    }

    struct wl_list *pos = start->tab_link.next;
    for (;;) {
        if (pos == &group->views) {
            pos = group->views.next;
            continue;
        }
        if (pos == &start->tab_link) {
            break;
        }
        struct fbwl_view *v = wl_container_of(pos, v, tab_link);
        if (view_is_mapped_not_minimized(v)) {
            return v;
        }
        pos = pos->next;
    }
    return NULL;
}

static struct fbwl_view *tab_group_pick_prev_mapped(struct fbwl_tab_group *group, struct fbwl_view *start) {
    if (group == NULL || start == NULL) {
        return NULL;
    }

    struct wl_list *pos = start->tab_link.prev;
    for (;;) {
        if (pos == &group->views) {
            pos = group->views.prev;
            continue;
        }
        if (pos == &start->tab_link) {
            break;
        }
        struct fbwl_view *v = wl_container_of(pos, v, tab_link);
        if (view_is_mapped_not_minimized(v)) {
            return v;
        }
        pos = pos->prev;
    }
    return NULL;
}

struct fbwl_view *fbwl_tabs_pick_next(const struct fbwl_view *view) {
    struct fbwl_tab_group *group = view != NULL ? view->tab_group : NULL;
    if (group == NULL) {
        return NULL;
    }

    struct fbwl_view *start = tab_group_pick_active_fallback(group);
    if (start == NULL) {
        return NULL;
    }

    return tab_group_pick_next_mapped(group, start);
}

struct fbwl_view *fbwl_tabs_pick_prev(const struct fbwl_view *view) {
    struct fbwl_tab_group *group = view != NULL ? view->tab_group : NULL;
    if (group == NULL) {
        return NULL;
    }

    struct fbwl_view *start = tab_group_pick_active_fallback(group);
    if (start == NULL) {
        return NULL;
    }

    return tab_group_pick_prev_mapped(group, start);
}

struct fbwl_view *fbwl_tabs_pick_index0(const struct fbwl_view *view, int tab_index0) {
    struct fbwl_tab_group *group = view != NULL ? view->tab_group : NULL;
    if (group == NULL || tab_index0 < 0) {
        return NULL;
    }

    int idx = 0;
    struct fbwl_view *v;
    wl_list_for_each(v, &group->views, tab_link) {
        if (!view_is_mapped_not_minimized(v)) {
            continue;
        }
        if (idx == tab_index0) {
            return v;
        }
        idx++;
    }
    return NULL;
}

void fbwl_tabs_sync_geometry_from_view(struct fbwl_view *view, bool include_size, int width, int height,
        const char *reason) {
    if (view == NULL || view->tab_group == NULL) {
        return;
    }

    struct fbwl_tab_group *group = view->tab_group;
    struct fbwl_view *v;
    wl_list_for_each(v, &group->views, tab_link) {
        if (v == NULL || v == view) {
            continue;
        }

        v->x = view->x;
        v->y = view->y;
        if (v->scene_tree != NULL) {
            wlr_scene_node_set_position(&v->scene_tree->node, v->x, v->y);
        }

        if (include_size) {
            if (width < 1 || height < 1) {
                continue;
            }
            if (v->type == FBWL_VIEW_XDG && v->xdg_toplevel != NULL) {
                wlr_xdg_toplevel_set_size(v->xdg_toplevel, width, height);
            } else if (v->type == FBWL_VIEW_XWAYLAND && v->xwayland_surface != NULL) {
                wlr_xwayland_surface_configure(v->xwayland_surface, v->x, v->y, (uint16_t)width, (uint16_t)height);
            }
        }
    }

    if (include_size) {
        wlr_log(WLR_INFO, "Tabs: sync-geometry reason=%s title=%s w=%d h=%d",
            reason != NULL ? reason : "(null)",
            fbwl_view_display_title(view),
            width, height);
    } else {
        wlr_log(WLR_INFO, "Tabs: sync-geometry reason=%s title=%s",
            reason != NULL ? reason : "(null)",
            fbwl_view_display_title(view));
    }
}
