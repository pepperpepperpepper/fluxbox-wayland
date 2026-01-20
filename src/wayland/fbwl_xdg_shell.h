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
struct wlr_xdg_toplevel;

struct fbwl_xdg_shell_hooks {
    void *userdata;
    void (*apply_workspace_visibility)(void *userdata, const char *why);
    void (*toolbar_rebuild)(void *userdata);
    void (*clear_keyboard_focus)(void *userdata);
    void (*clear_focused_view_if_matches)(void *userdata, struct fbwl_view *view);
    void (*apps_rules_apply_pre_map)(struct fbwl_view *view, const struct fbwl_apps_rule *rule);
    void (*apps_rules_apply_post_map)(struct fbwl_view *view, const struct fbwl_apps_rule *rule);
    void (*view_set_minimized)(struct fbwl_view *view, bool minimized, const char *why);
};

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
        const struct fbwl_view_foreign_toplevel_handlers *foreign_handlers);

void fbwl_xdg_shell_handle_new_popup(struct wl_listener *listener, void *data);

void fbwl_xdg_shell_handle_toplevel_map(struct fbwl_view *view,
        struct fbwm_core *wm,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        double cursor_x,
        double cursor_y,
        const struct fbwl_apps_rule *apps_rules,
        size_t apps_rule_count,
        const struct fbwl_xdg_shell_hooks *hooks);

void fbwl_xdg_shell_handle_toplevel_unmap(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xdg_shell_hooks *hooks);

void fbwl_xdg_shell_handle_toplevel_commit(struct fbwl_view *view,
        const struct fbwl_decor_theme *decor_theme);

void fbwl_xdg_shell_handle_toplevel_request_maximize(struct fbwl_view *view,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs);

void fbwl_xdg_shell_handle_toplevel_request_fullscreen(struct fbwl_view *view,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        struct wlr_scene_tree *layer_normal,
        struct wlr_scene_tree *layer_fullscreen);

void fbwl_xdg_shell_handle_toplevel_request_minimize(struct fbwl_view *view,
        const struct fbwl_xdg_shell_hooks *hooks);

void fbwl_xdg_shell_handle_toplevel_set_title(struct fbwl_view *view,
        const struct fbwl_decor_theme *decor_theme,
        const struct fbwl_xdg_shell_hooks *hooks);

void fbwl_xdg_shell_handle_toplevel_set_app_id(struct fbwl_view *view,
        const struct fbwl_xdg_shell_hooks *hooks);

void fbwl_xdg_shell_handle_toplevel_destroy(struct fbwl_view *view,
        struct fbwm_core *wm,
        const struct fbwl_xdg_shell_hooks *hooks);

