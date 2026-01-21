#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <wayland-server-core.h>

struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_tree;
struct wlr_seat;
struct wlr_session_lock_manager_v1;
struct wlr_session_lock_v1;
struct wlr_surface;

struct fbwl_session_lock_hooks {
    void (*clear_keyboard_focus)(void *userdata);
    void (*text_input_update_focus)(void *userdata, struct wlr_surface *surface);
    void (*update_shortcuts_inhibitor)(void *userdata);
    void *userdata;
};

struct fbwl_session_lock_state {
    struct wlr_session_lock_manager_v1 *session_lock_mgr;
    struct wl_listener new_session_lock;

    struct wlr_session_lock_v1 *session_lock;
    struct wl_listener session_lock_new_surface;
    struct wl_listener session_lock_unlock;
    struct wl_listener session_lock_destroy;
    struct wl_list session_lock_surfaces;

    bool session_locked;
    bool session_lock_sent_locked;
    size_t session_lock_expected_surfaces;

    struct wlr_scene **scene;
    struct wlr_scene_tree **layer_overlay;
    struct wlr_output_layout **output_layout;
    struct wlr_seat **seat;
    struct wl_list *outputs;

    struct fbwl_session_lock_hooks hooks;
};

bool fbwl_session_lock_init(struct fbwl_session_lock_state *state, struct wl_display *display,
        struct wlr_scene **scene, struct wlr_scene_tree **layer_overlay, struct wlr_output_layout **output_layout,
        struct wlr_seat **seat, struct wl_list *outputs, const struct fbwl_session_lock_hooks *hooks);
void fbwl_session_lock_finish(struct fbwl_session_lock_state *state);

bool fbwl_session_lock_is_locked(const struct fbwl_session_lock_state *state);
void fbwl_session_lock_on_output_destroyed(struct fbwl_session_lock_state *state);

