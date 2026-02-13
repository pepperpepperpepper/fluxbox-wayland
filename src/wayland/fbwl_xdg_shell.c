#include "wayland/fbwl_xdg_shell.h"

#include <stdlib.h>

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_apps_rules.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_attention.h"
#include "wayland/fbwl_view_foreign_toplevel.h"

struct fbwl_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct fbwl_view *view;
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
    if (popup != NULL && popup->view != NULL) {
        fbwl_view_pseudo_bg_update(popup->view, "popup-destroy");
    }
    fbwl_cleanup_listener(&popup->commit);
    fbwl_cleanup_listener(&popup->destroy);
    free(popup);
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

void fbwl_xdg_shell_handle_new_popup(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_popup *xdg_popup = data;

    struct fbwl_popup *popup = calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;

    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (parent != NULL) {
        struct wlr_scene_tree *parent_tree = parent->data;
        struct wlr_scene_tree *popup_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
        xdg_popup->base->data = popup_tree;

        struct wlr_scene_node *walk = parent_tree != NULL ? &parent_tree->node : NULL;
        while (walk != NULL && walk->data == NULL) {
            walk = walk->parent != NULL ? &walk->parent->node : NULL;
        }
        struct fbwl_view *view = walk != NULL ? walk->data : NULL;
        if (view != NULL && popup_tree != NULL) {
            popup->view = view;
            popup_tree->node.data = view;
            fbwl_view_alpha_apply(view);
            fbwl_view_pseudo_bg_update(view, "popup-create");
        }
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
    view->create_seq = ++server->view_create_seq;
    view->type = FBWL_VIEW_XDG;
    view->xdg_toplevel = xdg_toplevel;
    wl_list_init(&view->tab_link);
    view->scene_tree = wlr_scene_tree_create(toplevel_parent);
    if (view->scene_tree == NULL) {
        free(view);
        return;
    }
    view->scene_tree->node.data = view;

    view->content_tree = wlr_scene_xdg_surface_create(view->scene_tree, xdg_toplevel->base);
    if (view->content_tree == NULL) {
        wlr_scene_node_destroy(&view->scene_tree->node);
        free(view);
        return;
    }
    view->content_tree->node.data = view;
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

    if (!view->decor_forced) {
        const bool enable = view->xdg_decoration_server_side && view->server != NULL && view->server->default_deco_enabled;
        fbwl_view_decor_set_enabled(view, enable);
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
        wlr_log(WLR_INFO, "Surface size: %s %dx%d",
            fbwl_view_display_title(view),
            w, h);
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
    free(view->title_override);
    view->title_override = NULL;
    fbwl_view_foreign_toplevel_set_title(view, fbwl_view_title(view));
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

    fbwl_view_attention_finish(view);

    fbwl_view_foreign_toplevel_destroy(view);

    fbwl_view_cleanup(view);

    if (view->scene_tree != NULL) {
        fbwl_pseudo_bg_destroy(&view->pseudo_bg);
        fbwl_pseudo_bg_destroy(&view->decor_titlebar_pseudo_bg);
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree = NULL;
        view->content_tree = NULL;
    }

    fbwm_core_view_destroy(wm, &view->wm_view);
    if (wm->focused == NULL && hooks != NULL && hooks->clear_keyboard_focus != NULL) {
        hooks->clear_keyboard_focus(hooks->userdata);
    }
    free(view);
}
