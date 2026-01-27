#include "wayland/fbwl_xwayland.h"

#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_apps_rules.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_foreign_toplevel.h"

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
        wl_notify_func_t set_title_fn,
        wl_notify_func_t set_class_fn,
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
    view->xwayland_set_title.notify = set_title_fn;
    wl_signal_add(&xsurface->events.set_title, &view->xwayland_set_title);
    view->xwayland_set_class.notify = set_class_fn;
    wl_signal_add(&xsurface->events.set_class, &view->xwayland_set_class);

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
    if (autotab_anchor != NULL) {
        view->wm_view.workspace = autotab_anchor->wm_view.workspace;
        view->wm_view.sticky = autotab_anchor->wm_view.sticky;
    } else {
        view->wm_view.workspace = fbwm_core_workspace_current(wm);
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
        if (apps_rule != NULL) {
            wlr_log(WLR_INFO, "Apps: match rule=%zu title=%s app_id=%s",
                apps_rule_idx,
                fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
                fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)");

            if (hooks != NULL && hooks->apps_rules_apply_pre_map != NULL) {
                hooks->apps_rules_apply_pre_map(view, apps_rule);
            }
            view->apps_rules_applied = true;

            wlr_log(WLR_INFO,
                "Apps: applied title=%s app_id=%s workspace_id=%d sticky=%d jump=%d minimized=%d maximized=%d fullscreen=%d",
                fbwl_view_display_title(view),
                fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)",
                apps_rule->set_workspace ? apps_rule->workspace : -1,
                apps_rule->set_sticky ? (apps_rule->sticky ? 1 : 0) : -1,
                apps_rule->set_jump ? (apps_rule->jump ? 1 : 0) : -1,
                apps_rule->set_minimized ? (apps_rule->minimized ? 1 : 0) : -1,
                apps_rule->set_maximized ? (apps_rule->maximized ? 1 : 0) : -1,
                apps_rule->set_fullscreen ? (apps_rule->fullscreen ? 1 : 0) : -1);
        }
    }

    if (autotab_anchor != NULL) {
        view->wm_view.workspace = autotab_anchor->wm_view.workspace;
        view->wm_view.sticky = autotab_anchor->wm_view.sticky;
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

    const int w = surface->current.width;
    const int h = surface->current.height;
    if (w > 0 && h > 0 && (w != view->width || h != view->height)) {
        view->width = w;
        view->height = h;
        wlr_log(WLR_INFO, "Surface size: %s %dx%d", fbwl_view_display_title(view), w, h);
    }
    fbwl_view_decor_update(view, view->server != NULL ? decor_theme : NULL);
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
    (void)wlr_scene_surface_create(view->scene_tree, xsurface->surface);
    view->scene_tree->node.data = view;
    fbwl_view_decor_create(view, view->server != NULL ? decor_theme : NULL);
    fbwl_view_decor_set_enabled(view, true);

    view->x = xsurface->x;
    view->y = xsurface->y;
    wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
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
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree = NULL;
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
    if (!view->wm_view.sticky &&
            view->wm_view.workspace != fbwm_core_workspace_current(wm)) {
        fbwm_core_workspace_switch(wm, view->wm_view.workspace);
        if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
            hooks->apply_workspace_visibility(hooks->userdata, "xwayland-request-activate-switch");
        }
    }
    fbwm_core_focus_view(wm, &view->wm_view);
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

    fbwl_cleanup_listener(&view->map);
    fbwl_cleanup_listener(&view->unmap);
    fbwl_cleanup_listener(&view->commit);
    fbwl_cleanup_listener(&view->destroy);
    fbwl_cleanup_listener(&view->xwayland_associate);
    fbwl_cleanup_listener(&view->xwayland_dissociate);
    fbwl_cleanup_listener(&view->xwayland_request_configure);
    fbwl_cleanup_listener(&view->xwayland_request_activate);
    fbwl_cleanup_listener(&view->xwayland_request_close);
    fbwl_cleanup_listener(&view->xwayland_set_title);
    fbwl_cleanup_listener(&view->xwayland_set_class);

    fbwl_cleanup_listener(&view->foreign_request_maximize);
    fbwl_cleanup_listener(&view->foreign_request_minimize);
    fbwl_cleanup_listener(&view->foreign_request_activate);
    fbwl_cleanup_listener(&view->foreign_request_fullscreen);
    fbwl_cleanup_listener(&view->foreign_request_close);

    if (hooks != NULL && hooks->clear_focused_view_if_matches != NULL) {
        hooks->clear_focused_view_if_matches(hooks->userdata, view);
    }

    free(view->decor_title_text_cache);
    view->decor_title_text_cache = NULL;

    if (view->scene_tree != NULL) {
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree = NULL;
    }

    fbwl_view_foreign_toplevel_destroy(view);

    free(view->decor_title_text_cache);
    view->decor_title_text_cache = NULL;

    fbwm_core_view_destroy(wm, &view->wm_view);
    if (wm->focused == NULL && hooks != NULL && hooks->clear_keyboard_focus != NULL) {
        hooks->clear_keyboard_focus(hooks->userdata);
    }
    free(view);
}
