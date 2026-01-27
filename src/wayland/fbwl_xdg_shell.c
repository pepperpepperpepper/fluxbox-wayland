#include "wayland/fbwl_xdg_shell.h"

#include <stdlib.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_apps_rules.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_foreign_toplevel.h"

struct fbwl_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_popup *popup = wl_container_of(listener, popup, commit);
    if (popup->xdg_popup->base->initial_commit) {
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_popup *popup = wl_container_of(listener, popup, destroy);
    fbwl_cleanup_listener(&popup->commit);
    fbwl_cleanup_listener(&popup->destroy);
    free(popup);
}

void fbwl_xdg_shell_handle_new_popup(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_popup *xdg_popup = data;

    struct fbwl_popup *popup = calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;

    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (parent != NULL) {
        struct wlr_scene_tree *parent_tree = parent->data;
        xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
    }

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
    popup->destroy.notify = xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void fbwl_xdg_shell_handle_new_toplevel(struct fbwl_server *server, struct wlr_xdg_toplevel *xdg_toplevel,
        struct wlr_scene_tree *toplevel_parent,
        const struct fbwl_decor_theme *decor_theme,
        const struct fbwm_view_ops *wm_view_ops,
        wl_notify_func_t map_fn,
        wl_notify_func_t unmap_fn,
        wl_notify_func_t commit_fn,
        wl_notify_func_t destroy_fn,
        wl_notify_func_t request_maximize_fn,
        wl_notify_func_t request_fullscreen_fn,
        wl_notify_func_t request_minimize_fn,
        wl_notify_func_t set_title_fn,
        wl_notify_func_t set_app_id_fn,
        struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_mgr,
        const struct fbwl_view_foreign_toplevel_handlers *foreign_handlers) {
    if (server == NULL || xdg_toplevel == NULL || xdg_toplevel->base == NULL || toplevel_parent == NULL) {
        return;
    }

    struct fbwl_view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->type = FBWL_VIEW_XDG;
    view->xdg_toplevel = xdg_toplevel;
    wl_list_init(&view->tab_link);
    view->scene_tree = wlr_scene_xdg_surface_create(toplevel_parent, xdg_toplevel->base);
    view->scene_tree->node.data = view;
    xdg_toplevel->base->data = view->scene_tree;
    view->base_layer = toplevel_parent;
    fbwl_view_decor_create(view, decor_theme);

    fbwm_view_init(&view->wm_view, wm_view_ops, view);

    view->map.notify = map_fn;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &view->map);
    view->unmap.notify = unmap_fn;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &view->unmap);
    view->commit.notify = commit_fn;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &view->commit);

    view->destroy.notify = destroy_fn;
    wl_signal_add(&xdg_toplevel->events.destroy, &view->destroy);

    view->request_maximize.notify = request_maximize_fn;
    wl_signal_add(&xdg_toplevel->events.request_maximize, &view->request_maximize);
    view->request_fullscreen.notify = request_fullscreen_fn;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen, &view->request_fullscreen);
    view->request_minimize.notify = request_minimize_fn;
    wl_signal_add(&xdg_toplevel->events.request_minimize, &view->request_minimize);

    view->set_title.notify = set_title_fn;
    wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);
    view->set_app_id.notify = set_app_id_fn;
    wl_signal_add(&xdg_toplevel->events.set_app_id, &view->set_app_id);

    if (foreign_toplevel_mgr != NULL) {
        fbwl_view_foreign_toplevel_create(view, foreign_toplevel_mgr, xdg_toplevel->title, xdg_toplevel->app_id,
            foreign_handlers);
    }
}

void fbwl_xdg_shell_handle_toplevel_map(struct fbwl_view *view,
        struct fbwm_core *wm,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        double cursor_x,
        double cursor_y,
        const struct fbwl_apps_rule *apps_rules,
        size_t apps_rule_count,
        const struct fbwl_xdg_shell_hooks *hooks) {
    if (view == NULL || wm == NULL) {
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
                "Apps: applied title=%s app_id=%s workspace_id=%d sticky=%d jump=%d minimized=%d maximized=%d fullscreen=%d group_id=%d deco=%d layer=%d",
                fbwl_view_display_title(view),
                fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)",
                apps_rule->set_workspace ? apps_rule->workspace : -1,
                apps_rule->set_sticky ? (apps_rule->sticky ? 1 : 0) : -1,
                apps_rule->set_jump ? (apps_rule->jump ? 1 : 0) : -1,
                apps_rule->set_minimized ? (apps_rule->minimized ? 1 : 0) : -1,
                apps_rule->set_maximized ? (apps_rule->maximized ? 1 : 0) : -1,
                apps_rule->set_fullscreen ? (apps_rule->fullscreen ? 1 : 0) : -1,
                apps_rule->group_id,
                apps_rule->set_decor ? (apps_rule->decor_enabled ? 1 : 0) : -1,
                apps_rule->set_layer ? apps_rule->layer : -1);
        }
    }

    if (autotab_anchor != NULL) {
        view->wm_view.workspace = autotab_anchor->wm_view.workspace;
        view->wm_view.sticky = autotab_anchor->wm_view.sticky;
    }

    if (!view->placed) {
        fbwl_view_place_initial(view, wm, output_layout, outputs, cursor_x, cursor_y);
        view->placed = true;
    }
    view->mapped = true;
    view->minimized = false;
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, false);
    }

    fbwm_core_view_map(wm, &view->wm_view);
    if (hooks != NULL && hooks->apply_workspace_visibility != NULL) {
        hooks->apply_workspace_visibility(hooks->userdata, "map");
    }
    if (apps_rule != NULL && hooks != NULL && hooks->apps_rules_apply_post_map != NULL) {
        hooks->apps_rules_apply_post_map(view, apps_rule);
    }
}

void fbwl_xdg_shell_handle_toplevel_unmap(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xdg_shell_hooks *hooks) {
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
        hooks->apply_workspace_visibility(hooks->userdata, "unmap");
    }
}

void fbwl_xdg_shell_handle_toplevel_commit(struct fbwl_view *view,
        const struct fbwl_decor_theme *decor_theme) {
    if (view == NULL || view->xdg_toplevel == NULL) {
        return;
    }
    if (view->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
    }

    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    const int w = surface->current.width;
    const int h = surface->current.height;
    if (w > 0 && h > 0 && (w != view->width || h != view->height)) {
        view->width = w;
        view->height = h;
        wlr_log(WLR_INFO, "Surface size: %s %dx%d",
            fbwl_view_display_title(view),
            w, h);
    }
    fbwl_view_decor_update(view, view->server != NULL ? decor_theme : NULL);
}

void fbwl_xdg_shell_handle_toplevel_request_maximize(struct fbwl_view *view,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs) {
    if (view == NULL || view->xdg_toplevel == NULL) {
        return;
    }
    fbwl_view_set_maximized(view, view->xdg_toplevel->requested.maximized, output_layout, outputs);
}

void fbwl_xdg_shell_handle_toplevel_request_fullscreen(struct fbwl_view *view,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        struct wlr_scene_tree *layer_normal,
        struct wlr_scene_tree *layer_fullscreen) {
    if (view == NULL || view->xdg_toplevel == NULL) {
        return;
    }
    fbwl_view_set_fullscreen(view, view->xdg_toplevel->requested.fullscreen, output_layout, outputs,
        layer_normal, layer_fullscreen, view->xdg_toplevel->requested.fullscreen_output);
}

void fbwl_xdg_shell_handle_toplevel_request_minimize(struct fbwl_view *view,
        const struct fbwl_xdg_shell_hooks *hooks) {
    if (view == NULL || view->xdg_toplevel == NULL) {
        return;
    }
    if (hooks != NULL && hooks->view_set_minimized != NULL) {
        hooks->view_set_minimized(view, view->xdg_toplevel->requested.minimized, "xdg-request");
    }
}

void fbwl_xdg_shell_handle_toplevel_set_title(struct fbwl_view *view,
        const struct fbwl_decor_theme *decor_theme,
        const struct fbwl_xdg_shell_hooks *hooks) {
    if (view == NULL || view->xdg_toplevel == NULL) {
        return;
    }
    fbwl_view_foreign_toplevel_set_title(view, view->xdg_toplevel->title);
    fbwl_view_decor_update_title_text(view, view->server != NULL ? decor_theme : NULL);
    if (view->server != NULL && hooks != NULL && hooks->toolbar_rebuild != NULL) {
        hooks->toolbar_rebuild(hooks->userdata);
    }
}

void fbwl_xdg_shell_handle_toplevel_set_app_id(struct fbwl_view *view,
        const struct fbwl_xdg_shell_hooks *hooks) {
    if (view == NULL || view->xdg_toplevel == NULL) {
        return;
    }
    fbwl_view_foreign_toplevel_set_app_id(view, view->xdg_toplevel->app_id);
    if (view->server != NULL && hooks != NULL && hooks->toolbar_rebuild != NULL) {
        hooks->toolbar_rebuild(hooks->userdata);
    }
}

void fbwl_xdg_shell_handle_toplevel_destroy(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xdg_shell_hooks *hooks) {
    if (view == NULL || wm == NULL) {
        return;
    }

    fbwl_tabs_detach(view, "destroy");

    fbwl_cleanup_listener(&view->map);
    fbwl_cleanup_listener(&view->unmap);
    fbwl_cleanup_listener(&view->commit);
    fbwl_cleanup_listener(&view->destroy);
    fbwl_cleanup_listener(&view->request_maximize);
    fbwl_cleanup_listener(&view->request_fullscreen);
    fbwl_cleanup_listener(&view->request_minimize);
    fbwl_cleanup_listener(&view->set_title);
    fbwl_cleanup_listener(&view->set_app_id);
    fbwl_cleanup_listener(&view->foreign_request_maximize);
    fbwl_cleanup_listener(&view->foreign_request_minimize);
    fbwl_cleanup_listener(&view->foreign_request_activate);
    fbwl_cleanup_listener(&view->foreign_request_fullscreen);
    fbwl_cleanup_listener(&view->foreign_request_close);

    if (hooks != NULL && hooks->clear_focused_view_if_matches != NULL) {
        hooks->clear_focused_view_if_matches(hooks->userdata, view);
    }

    fbwl_view_foreign_toplevel_destroy(view);

    fbwm_core_view_destroy(wm, &view->wm_view);
    if (wm->focused == NULL && hooks != NULL && hooks->clear_keyboard_focus != NULL) {
        hooks->clear_keyboard_focus(hooks->userdata);
    }
    free(view);
}
