#pragma once

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_idle_inhibit_manager_v1;
struct wlr_idle_notifier_v1;
struct wlr_seat;

struct fbwl_idle_state {
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
    struct wl_listener new_idle_inhibitor;
    int idle_inhibitor_count;

    struct wlr_idle_notifier_v1 *idle_notifier;
    bool idle_inhibited;

    struct wlr_seat **seat;
};

bool fbwl_idle_init(struct fbwl_idle_state *state, struct wl_display *display, struct wlr_seat **seat);
void fbwl_idle_finish(struct fbwl_idle_state *state);

void fbwl_idle_notify_activity(struct fbwl_idle_state *state);

