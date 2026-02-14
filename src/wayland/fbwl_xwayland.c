#include "wayland/fbwl_xwayland.h"

#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_apps_rules.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_attention.h"
#include "wayland/fbwl_view_foreign_toplevel.h"

static int positive_or(int v, int fallback) {
    return v > 0 ? v : fallback;
}

static int increase_to_multiple(int val, int inc) {
    if (inc <= 1) {
        return val;
    }
    const int rem = val % inc;
    return rem == 0 ? val : (val + inc - rem);
}

static int decrease_to_multiple(int val, int inc) {
    if (inc <= 1) {
        return val;
    }
    const int rem = val % inc;
    return rem == 0 ? val : (val - rem);
}

static bool point_in_view_frame(const struct fbwl_view *view, int content_w, int content_h, double lx, double ly) {
    if (view == NULL || content_w < 1 || content_h < 1) {
        return false;
    }

    int left = 0, top = 0, right = 0, bottom = 0;
    if (view->server != NULL) {
        fbwl_view_decor_frame_extents(view, &view->server->decor_theme, &left, &top, &right, &bottom);
    }

    const double frame_x = (double)view->x - (double)left;
    const double frame_y = (double)view->y - (double)top;
    const double frame_w = (double)content_w + (double)left + (double)right;
    const double frame_h = (double)content_h + (double)top + (double)bottom;
    if (frame_w < 1.0 || frame_h < 1.0) {
        return false;
    }

    return lx >= frame_x && lx < frame_x + frame_w && ly >= frame_y && ly < frame_y + frame_h;
}

void fbwl_xwayland_apply_size_hints(const struct wlr_xwayland_surface *xsurface, int *width, int *height,
        bool make_fit) {
    if (xsurface == NULL || xsurface->size_hints == NULL || width == NULL || height == NULL) {
        return;
    }

    int w = *width;
    int h = *height;
    if (w < 1 || h < 1) {
        return;
    }

    const xcb_size_hints_t *hints = xsurface->size_hints;

    int base_w = 0;
    int base_h = 0;
    if ((hints->flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) != 0) {
        base_w = positive_or(hints->base_width, 0);
        base_h = positive_or(hints->base_height, 0);
    }

    int min_w = 1;
    int min_h = 1;
    if ((hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) != 0) {
        min_w = positive_or(hints->min_width, 1);
        min_h = positive_or(hints->min_height, 1);
    }
    if (base_w > min_w) {
        min_w = base_w;
    }
    if (base_h > min_h) {
        min_h = base_h;
    }

    int max_w = 0;
    int max_h = 0;
    if ((hints->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) != 0) {
        max_w = positive_or(hints->max_width, 0);
        max_h = positive_or(hints->max_height, 0);
    }

    int inc_w = 1;
    int inc_h = 1;
    if ((hints->flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) != 0) {
        inc_w = positive_or(hints->width_inc, 1);
        inc_h = positive_or(hints->height_inc, 1);
    }

    int max_fit_w = max_w;
    int max_fit_h = max_h;
    if (make_fit && (w < max_w || max_w == 0)) {
        max_fit_w = w;
    }
    if (make_fit && (h < max_h || max_h == 0)) {
        max_fit_h = h;
    }

    if (max_fit_w > 0 && w > max_fit_w) {
        w = max_fit_w;
    }
    if (max_fit_h > 0 && h > max_fit_h) {
        h = max_fit_h;
    }
    if (w < min_w) {
        w = min_w;
    }
    if (h < min_h) {
        h = min_h;
    }

    const int rel_w = w - base_w;
    if (rel_w >= 0) {
        w = base_w + decrease_to_multiple(rel_w, inc_w);
        if (w < min_w) {
            w = base_w + increase_to_multiple(min_w - base_w, inc_w);
        }
        if (max_fit_w > 0 && w > max_fit_w) {
            w = base_w + decrease_to_multiple(max_fit_w - base_w, inc_w);
        }
    }

    const int rel_h = h - base_h;
    if (rel_h >= 0) {
        h = base_h + decrease_to_multiple(rel_h, inc_h);
        if (h < min_h) {
            h = base_h + increase_to_multiple(min_h - base_h, inc_h);
        }
        if (max_fit_h > 0 && h > max_fit_h) {
            h = base_h + decrease_to_multiple(max_fit_h - base_h, inc_h);
        }
    }

    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }

    *width = w;
    *height = h;
}

void fbwl_xwayland_handle_ready(const char *display_name) {
    if (display_name != NULL) {
        setenv("DISPLAY", display_name, true);
    }
    wlr_log(WLR_INFO, "XWayland: ready DISPLAY=%s", display_name != NULL ? display_name : "(null)");
}

void fbwl_xwayland_handle_new_surface(struct fbwl_server *server, struct wlr_xwayland_surface *xsurface,
        const struct fbwm_view_ops *wm_view_ops,
        wl_notify_func_t destroy_fn,
        wl_notify_func_t associate_fn,
        wl_notify_func_t dissociate_fn,
        wl_notify_func_t request_configure_fn,
        wl_notify_func_t request_activate_fn,
        wl_notify_func_t request_close_fn,
        wl_notify_func_t request_demands_attention_fn,
        wl_notify_func_t set_title_fn,
        wl_notify_func_t set_class_fn,
        wl_notify_func_t set_hints_fn,
        struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_mgr,
        const struct fbwl_view_foreign_toplevel_handlers *foreign_handlers) {
    if (server == NULL || xsurface == NULL) {
        return;
    }

    wlr_log(WLR_INFO, "XWayland: new_surface id=0x%x override_redirect=%d title=%s class=%s",
        (unsigned)xsurface->window_id,
        xsurface->override_redirect ? 1 : 0,
        xsurface->title != NULL ? xsurface->title : "(no-title)",
        xsurface->class != NULL ? xsurface->class : "(no-class)");

    if (xsurface->override_redirect) {
        return;
    }

    struct fbwl_view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->create_seq = ++server->view_create_seq;
    view->type = FBWL_VIEW_XWAYLAND;
    view->xwayland_surface = xsurface;
    wl_list_init(&view->tab_link);
    xsurface->data = view;

    fbwm_view_init(&view->wm_view, wm_view_ops, view);

    view->destroy.notify = destroy_fn;
    wl_signal_add(&xsurface->events.destroy, &view->destroy);
    view->xwayland_associate.notify = associate_fn;
    wl_signal_add(&xsurface->events.associate, &view->xwayland_associate);
    view->xwayland_dissociate.notify = dissociate_fn;
    wl_signal_add(&xsurface->events.dissociate, &view->xwayland_dissociate);
    view->xwayland_request_configure.notify = request_configure_fn;
    wl_signal_add(&xsurface->events.request_configure, &view->xwayland_request_configure);
    view->xwayland_request_activate.notify = request_activate_fn;
    wl_signal_add(&xsurface->events.request_activate, &view->xwayland_request_activate);
    view->xwayland_request_close.notify = request_close_fn;
    wl_signal_add(&xsurface->events.request_close, &view->xwayland_request_close);
    view->xwayland_request_demands_attention.notify = request_demands_attention_fn;
    wl_signal_add(&xsurface->events.request_demands_attention, &view->xwayland_request_demands_attention);
    view->xwayland_set_title.notify = set_title_fn;
    wl_signal_add(&xsurface->events.set_title, &view->xwayland_set_title);
    view->xwayland_set_class.notify = set_class_fn;
    wl_signal_add(&xsurface->events.set_class, &view->xwayland_set_class);
    view->xwayland_set_hints.notify = set_hints_fn;
    wl_signal_add(&xsurface->events.set_hints, &view->xwayland_set_hints);

    if (foreign_toplevel_mgr != NULL) {
        fbwl_view_foreign_toplevel_create(view, foreign_toplevel_mgr, xsurface->title, xsurface->class, foreign_handlers);
    }
}

void fbwl_xwayland_handle_surface_map(struct fbwl_view *view,
        struct fbwm_core *wm,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        double cursor_x,
        double cursor_y,
        const struct fbwl_apps_rule *apps_rules,
        size_t apps_rule_count,
        const struct fbwl_xwayland_hooks *hooks) {
    if (view == NULL || view->xwayland_surface == NULL || wm == NULL) {
        return;
    }

    const struct fbwl_apps_rule *apps_rule = NULL;
    size_t apps_rule_idx = 0;
    if (!view->apps_rules_applied) {
        const char *app_id = fbwl_view_app_id(view);
        const char *instance = NULL;
        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            instance = view->xwayland_surface->instance;
        } else {
            instance = app_id;
        }
        const char *title = fbwl_view_title(view);
        apps_rule = fbwl_apps_rules_match(apps_rules, apps_rule_count,
            app_id, instance, title, &apps_rule_idx);
        if (apps_rule != NULL && apps_rule->set_tab) {
            view->tabs_enabled_override_set = true;
            view->tabs_enabled_override = apps_rule->tab;
        }
    }

    struct fbwl_view *autotab_anchor = NULL;
    if (!view->placed && fbwm_core_window_placement(wm) == FBWM_PLACE_AUTOTAB) {
        struct fbwm_view *focused = wm->focused;
        struct fbwl_view *anchor = focused != NULL ? focused->userdata : NULL;
        if (anchor != NULL && anchor->mapped && !anchor->minimized) {
            if (fbwl_tabs_attach(view, anchor, "autotab")) {
                autotab_anchor = anchor;
            }
        }
    }

    if (autotab_anchor != NULL) {
        view->wm_view.workspace = autotab_anchor->wm_view.workspace;
        view->wm_view.sticky = autotab_anchor->wm_view.sticky;
    } else {
        const size_t head = view->server != NULL ? fbwl_server_screen_index_at(view->server, cursor_x, cursor_y) : 0;
        view->wm_view.workspace = fbwm_core_workspace_current_for_head(wm, head);
    }

    if (!view->apps_rules_applied && apps_rule != NULL) {
        view->apps_rule_index_valid = true;
        view->apps_rule_index = apps_rule_idx;
        view->apps_rules_generation = view->server != NULL ? view->server->apps_rules_generation : 0;

        wlr_log(WLR_INFO, "Apps: match rule=%zu title=%s app_id=%s",
            apps_rule_idx,
            fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
            fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)");

        if (hooks != NULL && hooks->apps_rules_apply_pre_map != NULL) {
            hooks->apps_rules_apply_pre_map(view, apps_rule);
        }
        view->apps_rules_applied = true;

        wlr_log(WLR_INFO,
            "Apps: applied title=%s app_id=%s workspace_id=%d sticky=%d jump=%d minimized=%d maximized=%d fullscreen=%d shaded=%d alpha=%d,%d focus_protect=0x%x group_id=%d deco=0x%x layer=%d head=%d dims=%d%sx%d%s pos=%d%s,%d%s anchor=%d",
            fbwl_view_display_title(view),
            fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)",
            apps_rule->set_workspace ? apps_rule->workspace : -1,
            apps_rule->set_sticky ? (apps_rule->sticky ? 1 : 0) : -1,
            apps_rule->set_jump ? (apps_rule->jump ? 1 : 0) : -1,
            apps_rule->set_minimized ? (apps_rule->minimized ? 1 : 0) : -1,
            apps_rule->set_maximized ? ((apps_rule->maximized_h || apps_rule->maximized_v) ? 1 : 0) : -1,
            apps_rule->set_fullscreen ? (apps_rule->fullscreen ? 1 : 0) : -1,
            apps_rule->set_shaded ? (apps_rule->shaded ? 1 : 0) : -1,
            apps_rule->set_alpha ? apps_rule->alpha_focused : -1,
            apps_rule->set_alpha ? apps_rule->alpha_unfocused : -1,
            apps_rule->set_focus_protection ? (unsigned int)apps_rule->focus_protection : 0u,
            apps_rule->group_id,
            apps_rule->set_decor ? (unsigned int)apps_rule->decor_mask : 0u,
            apps_rule->set_layer ? apps_rule->layer : -1,
            apps_rule->set_head ? apps_rule->head : -1,
            apps_rule->set_dimensions ? apps_rule->width : -1,
            apps_rule->set_dimensions && apps_rule->width_percent ? "%" : "",
            apps_rule->set_dimensions ? apps_rule->height : -1,
            apps_rule->set_dimensions && apps_rule->height_percent ? "%" : "",
            apps_rule->set_position ? apps_rule->x : -1,
            apps_rule->set_position && apps_rule->x_percent ? "%" : "",
            apps_rule->set_position ? apps_rule->y : -1,
            apps_rule->set_position && apps_rule->y_percent ? "%" : "",
            apps_rule->set_position ? (int)apps_rule->position_anchor : -1);
    }

    if (autotab_anchor == NULL && apps_rule != NULL && apps_rule->set_head && !apps_rule->set_workspace) {
        const size_t head = apps_rule->head >= 0 ? (size_t)apps_rule->head : 0;
        view->wm_view.workspace = fbwm_core_workspace_current_for_head(wm, head);
    }

    if (autotab_anchor != NULL) {
        view->wm_view.workspace = autotab_anchor->wm_view.workspace;
        view->wm_view.sticky = autotab_anchor->wm_view.sticky;
    }

    if (view->server != NULL &&
            view->server->window_alpha_defaults_configured &&
            (view->alpha_is_default || !view->alpha_set)) {
        fbwl_view_set_alpha(view, view->server->window_alpha_default_focused, view->server->window_alpha_default_unfocused,
            "init-default");
        view->alpha_is_default = true;
    }

    if (!view->placed) {
        fbwl_view_place_initial(view, wm, output_layout, outputs, cursor_x, cursor_y);
        view->placed = true;
    }

    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    if (w > 0 && h > 0) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
            (uint16_t)w, (uint16_t)h);
    }

    view->mapped = true;
    view->minimized = false;
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, false);
    }

    fbwm_core_view_map(wm, &view->wm_view);
    if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
        hooks->apply_workspace_visibility(hooks->userdata, "xwayland-map");
    }
    if (apps_rule != NULL && hooks != NULL && hooks->apps_rules_apply_post_map != NULL) {
        hooks->apps_rules_apply_post_map(view, apps_rule);
    }
    wlr_log(WLR_INFO, "XWayland: map title=%s class=%s",
        fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
        fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-class)");
}

void fbwl_xwayland_handle_surface_unmap(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xwayland_hooks *hooks) {
    if (view == NULL || wm == NULL) {
        return;
    }
    view->mapped = false;
    view->minimized = false;
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, false);
    }
    fbwm_core_view_unmap(wm, &view->wm_view);
    if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
        hooks->apply_workspace_visibility(hooks->userdata, "xwayland-unmap");
    }
    wlr_log(WLR_INFO, "XWayland: unmap title=%s", fbwl_view_display_title(view));
}

void fbwl_xwayland_handle_surface_commit(struct fbwl_view *view,
        const struct fbwl_decor_theme *decor_theme) {
    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    if (surface == NULL) {
        return;
    }

    const int old_w = view->committed_width;
    const int old_h = view->committed_height;
    const int w = surface->current.width;
    const int h = surface->current.height;
    const bool size_changed = w > 0 && h > 0 && (w != old_w || h != old_h);
    if (size_changed) {
        view->committed_width = w;
        view->committed_height = h;
        view->width = w;
        view->height = h;
        wlr_log(WLR_INFO, "Surface size: %s %dx%d", fbwl_view_display_title(view), w, h);
    }
    fbwl_view_decor_update(view, view->server != NULL ? decor_theme : NULL);
    fbwl_view_pseudo_bg_update(view, size_changed ? "commit-size" : "commit");

    if (size_changed && view->server != NULL && view->server->cursor != NULL) {
        const double cx = view->server->cursor->x;
        const double cy = view->server->cursor->y;
        const bool was_inside = point_in_view_frame(view, old_w, old_h, cx, cy);
        const bool now_inside = point_in_view_frame(view, w, h, cx, cy);
        if (was_inside != now_inside) {
            server_strict_mousefocus_recheck(view->server, "commit-size");
        }
    }
}

void fbwl_xwayland_handle_surface_associate(struct fbwl_view *view,
        struct wlr_scene_tree *parent,
        const struct fbwl_decor_theme *decor_theme,
        struct wlr_output_layout *output_layout,
        wl_notify_func_t map_fn,
        wl_notify_func_t unmap_fn,
        wl_notify_func_t commit_fn) {
    struct wlr_xwayland_surface *xsurface = view != NULL ? view->xwayland_surface : NULL;
    if (view == NULL || view->server == NULL || xsurface == NULL || xsurface->surface == NULL || parent == NULL) {
        return;
    }

    view->scene_tree = wlr_scene_tree_create(parent);
    if (view->scene_tree == NULL) {
        return;
    }
    view->content_tree = wlr_scene_tree_create(view->scene_tree);
    view->base_layer = parent;
    view->scene_tree->node.data = view;
    struct wlr_scene_tree *content_parent = view->content_tree != NULL ? view->content_tree : view->scene_tree;
    (void)wlr_scene_surface_create(content_parent, xsurface->surface);
    fbwl_view_decor_create(view, view->server != NULL ? decor_theme : NULL);
    fbwl_view_decor_set_enabled(view, view->server != NULL && view->server->default_deco_enabled);

    view->x = xsurface->x;
    view->y = xsurface->y;
    wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    fbwl_view_pseudo_bg_update(view, "xwayland-associate");
    fbwl_view_foreign_update_output_from_position(view, output_layout);

    view->map.notify = map_fn;
    wl_signal_add(&xsurface->surface->events.map, &view->map);
    view->unmap.notify = unmap_fn;
    wl_signal_add(&xsurface->surface->events.unmap, &view->unmap);
    view->commit.notify = commit_fn;
    wl_signal_add(&xsurface->surface->events.commit, &view->commit);

    wlr_log(WLR_INFO, "XWayland: associate title=%s class=%s",
        fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
        fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-class)");
}

void fbwl_xwayland_handle_surface_dissociate(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xwayland_hooks *hooks) {
    if (view == NULL || wm == NULL) {
        return;
    }
    if (view->mapped) {
        view->mapped = false;
        fbwm_core_view_unmap(wm, &view->wm_view);
        if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "xwayland-dissociate");
        }
    }

    fbwl_cleanup_listener(&view->map);
    fbwl_cleanup_listener(&view->unmap);
    fbwl_cleanup_listener(&view->commit);

    if (view->scene_tree != NULL) {
        fbwl_pseudo_bg_destroy(&view->pseudo_bg);
        fbwl_pseudo_bg_destroy(&view->decor_titlebar_pseudo_bg);
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree = NULL;
        view->content_tree = NULL;
        view->base_layer = NULL;
        view->decor_tree = NULL;
        view->decor_titlebar_pseudo_bg = (struct fbwl_pseudo_bg){0};
        view->decor_titlebar_tex = NULL;
        view->decor_titlebar = NULL;
        view->decor_title_text = NULL;
        view->decor_border_top = NULL;
        view->decor_border_bottom = NULL;
        view->decor_border_left = NULL;
        view->decor_border_right = NULL;
        view->decor_btn_menu = NULL;
        view->decor_btn_shade = NULL;
        view->decor_btn_stick = NULL;
        view->decor_btn_close = NULL;
        view->decor_btn_max = NULL;
        view->decor_btn_min = NULL;
        view->decor_btn_lhalf = NULL;
        view->decor_btn_rhalf = NULL;
        view->decor_tabs_tree = NULL;
        view->decor_title_text_cache_w = 0;
        view->decor_title_text_cache_active = false;
    }
}

void fbwl_xwayland_handle_surface_request_configure(struct fbwl_view *view,
        struct wlr_xwayland_surface_configure_event *event,
        const struct fbwl_decor_theme *decor_theme,
        struct wlr_output_layout *output_layout) {
    if (view == NULL || view->xwayland_surface == NULL || event == NULL) {
        return;
    }

    view->x = event->x;
    view->y = event->y;
    view->width = event->width;
    view->height = event->height;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
    }
    fbwl_view_pseudo_bg_update(view, "xwayland-request-configure");
    fbwl_view_decor_update(view, view->server != NULL ? decor_theme : NULL);

    wlr_xwayland_surface_configure(view->xwayland_surface, event->x, event->y,
        event->width, event->height);
    fbwl_view_foreign_update_output_from_position(view, output_layout);
    wlr_log(WLR_INFO, "XWayland: request_configure title=%s x=%d y=%d w=%u h=%u mask=0x%x",
        fbwl_view_display_title(view), event->x, event->y, event->width, event->height, event->mask);
}

void fbwl_xwayland_handle_surface_request_activate(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xwayland_hooks *hooks) {
    if (view == NULL || wm == NULL) {
        return;
    }

    if (view->minimized && hooks != NULL && hooks->view_set_minimized != NULL) {
        hooks->view_set_minimized(view, false, "xwayland-request-activate");
    }
    if (!view->wm_view.sticky) {
        const size_t head = view->server != NULL ? fbwl_server_screen_index_for_view(view->server, view) : 0;
        const int cur_ws = fbwm_core_workspace_current_for_head(wm, head);
        if (view->wm_view.workspace != cur_ws) {
            if (view->server != NULL) {
                server_workspace_switch_on_head(view->server, head, view->wm_view.workspace,
                    "xwayland-request-activate-switch");
            } else {
                fbwm_core_workspace_switch_on_head(wm, head, view->wm_view.workspace);
                if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
                    hooks->apply_workspace_visibility(hooks->userdata, "xwayland-request-activate-switch");
                }
            }
        }
    }
    struct fbwl_server *server = view->server;
    const enum fbwl_focus_reason prev_reason = server != NULL ? server->focus_reason : FBWL_FOCUS_REASON_NONE;
    if (server != NULL) {
        server->focus_reason = FBWL_FOCUS_REASON_ACTIVATE;
    }
    if (server == NULL || server_focus_request_allowed(server, view, FBWL_FOCUS_REASON_ACTIVATE, "activate")) {
        fbwm_core_focus_view(wm, &view->wm_view);
    }
    if (server != NULL) {
        server->focus_reason = prev_reason;
    }
}

void fbwl_xwayland_handle_surface_request_close(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }
    if (view->xwayland_surface != NULL) {
        wlr_xwayland_surface_close(view->xwayland_surface);
    }
}

void fbwl_xwayland_handle_surface_set_title(struct fbwl_view *view,
        const struct fbwl_decor_theme *decor_theme,
        const struct fbwl_xwayland_hooks *hooks) {
    if (view == NULL) {
        return;
    }
    free(view->title_override);
    view->title_override = NULL;
    fbwl_view_foreign_toplevel_set_title(view, fbwl_view_title(view));
    fbwl_view_decor_update_title_text(view, view->server != NULL ? decor_theme : NULL);
    if (view->server != NULL && hooks != NULL && hooks->toolbar_rebuild != NULL) {
        hooks->toolbar_rebuild(hooks->userdata);
    }
}

void fbwl_xwayland_handle_surface_set_class(struct fbwl_view *view,
        const struct fbwl_xwayland_hooks *hooks) {
    if (view == NULL) {
        return;
    }
    fbwl_view_foreign_toplevel_set_app_id(view, fbwl_view_app_id(view));
    if (view->server != NULL && hooks != NULL && hooks->toolbar_rebuild != NULL) {
        hooks->toolbar_rebuild(hooks->userdata);
    }
}

void fbwl_xwayland_handle_surface_destroy(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xwayland_hooks *hooks) {
    if (view == NULL || wm == NULL) {
        return;
    }

    fbwl_tabs_detach(view, "destroy");

    if (view->server != NULL && view->server->decor_button_pressed_view == view) {
        view->server->decor_button_pressed_view = NULL;
        view->server->decor_button_pressed_kind = FBWL_DECOR_HIT_NONE;
        view->server->decor_button_pressed_button = 0;
    }

    fbwl_cleanup_listener(&view->map);
    fbwl_cleanup_listener(&view->unmap);
    fbwl_cleanup_listener(&view->commit);
    fbwl_cleanup_listener(&view->destroy);
    fbwl_cleanup_listener(&view->xwayland_associate);
    fbwl_cleanup_listener(&view->xwayland_dissociate);
    fbwl_cleanup_listener(&view->xwayland_request_configure);
    fbwl_cleanup_listener(&view->xwayland_request_activate);
    fbwl_cleanup_listener(&view->xwayland_request_close);
    fbwl_cleanup_listener(&view->xwayland_request_demands_attention);
    fbwl_cleanup_listener(&view->xwayland_set_title);
    fbwl_cleanup_listener(&view->xwayland_set_class);
    fbwl_cleanup_listener(&view->xwayland_set_hints);

    fbwl_cleanup_listener(&view->foreign_request_maximize);
    fbwl_cleanup_listener(&view->foreign_request_minimize);
    fbwl_cleanup_listener(&view->foreign_request_activate);
    fbwl_cleanup_listener(&view->foreign_request_fullscreen);
    fbwl_cleanup_listener(&view->foreign_request_close);

    if (hooks != NULL && hooks->clear_focused_view_if_matches != NULL) {
        hooks->clear_focused_view_if_matches(hooks->userdata, view);
    }

    fbwl_view_cleanup(view);

    if (view->scene_tree != NULL) {
        fbwl_pseudo_bg_destroy(&view->pseudo_bg);
        fbwl_pseudo_bg_destroy(&view->decor_titlebar_pseudo_bg);
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree = NULL;
        view->content_tree = NULL;
    }

    fbwl_view_attention_finish(view);

    fbwl_view_foreign_toplevel_destroy(view);

    fbwm_core_view_destroy(wm, &view->wm_view);
    if (wm->focused == NULL && hooks != NULL && hooks->clear_keyboard_focus != NULL) {
        hooks->clear_keyboard_focus(hooks->userdata);
    }
    free(view);
}
