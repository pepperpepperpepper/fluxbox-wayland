#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <wayland-server-core.h>

struct fbwl_apps_rule;
struct fbwl_decor_theme;
struct fbwl_server;
struct fbwl_view;
struct fbwl_view_foreign_toplevel_handlers;
struct fbwm_core;
struct fbwm_view_ops;
struct wlr_foreign_toplevel_manager_v1;
struct wlr_output_layout;
struct wlr_scene_tree;
struct wlr_xwayland_surface;
struct wlr_xwayland_surface_configure_event;

void fbwl_xwayland_apply_size_hints(const struct wlr_xwayland_surface *xsurface, int *width, int *height,
        bool make_fit);

struct fbwl_xwayland_hooks {
    void *userdata;
    void (*apply_workspace_visibility)(void *userdata, const char *why);
    void (*toolbar_rebuild)(void *userdata);
    void (*clear_keyboard_focus)(void *userdata);
    void (*clear_focused_view_if_matches)(void *userdata, struct fbwl_view *view);
    void (*apps_rules_apply_pre_map)(struct fbwl_view *view, const struct fbwl_apps_rule *rule);
    void (*apps_rules_apply_post_map)(struct fbwl_view *view, const struct fbwl_apps_rule *rule);
    void (*view_set_minimized)(struct fbwl_view *view, bool minimized, const char *why);
};

void fbwl_xwayland_handle_ready(const char *display_name);

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
        const struct fbwl_view_foreign_toplevel_handlers *foreign_handlers);

void fbwl_xwayland_handle_surface_map(struct fbwl_view *view,
        struct fbwm_core *wm,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        double cursor_x,
        double cursor_y,
        const struct fbwl_apps_rule *apps_rules,
        size_t apps_rule_count,
        const struct fbwl_xwayland_hooks *hooks);

void fbwl_xwayland_handle_surface_unmap(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xwayland_hooks *hooks);

void fbwl_xwayland_handle_surface_commit(struct fbwl_view *view,
        const struct fbwl_decor_theme *decor_theme);

void fbwl_xwayland_handle_surface_associate(struct fbwl_view *view,
        struct wlr_scene_tree *parent,
        const struct fbwl_decor_theme *decor_theme,
        struct wlr_output_layout *output_layout,
        wl_notify_func_t map_fn,
        wl_notify_func_t unmap_fn,
        wl_notify_func_t commit_fn);

void fbwl_xwayland_handle_surface_dissociate(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xwayland_hooks *hooks);

void fbwl_xwayland_handle_surface_request_configure(struct fbwl_view *view,
        struct wlr_xwayland_surface_configure_event *event,
        const struct fbwl_decor_theme *decor_theme,
        struct wlr_output_layout *output_layout);

void fbwl_xwayland_handle_surface_request_activate(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xwayland_hooks *hooks);

void fbwl_xwayland_handle_surface_request_close(struct fbwl_view *view);

void fbwl_xwayland_handle_surface_set_title(struct fbwl_view *view,
        const struct fbwl_decor_theme *decor_theme,
        const struct fbwl_xwayland_hooks *hooks);

void fbwl_xwayland_handle_surface_set_class(struct fbwl_view *view,
        const struct fbwl_xwayland_hooks *hooks);

void fbwl_xwayland_handle_surface_destroy(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xwayland_hooks *hooks);
