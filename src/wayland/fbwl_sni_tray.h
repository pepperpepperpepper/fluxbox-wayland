#pragma once

#ifdef HAVE_SYSTEMD

#include <stdbool.h>

#include <wayland-server-core.h>

struct wlr_buffer;

typedef struct sd_bus sd_bus;
typedef struct sd_bus_slot sd_bus_slot;

enum fbwl_sni_status {
    FBWL_SNI_STATUS_ACTIVE = 0,
    FBWL_SNI_STATUS_PASSIVE,
    FBWL_SNI_STATUS_NEEDS_ATTENTION,
};

struct fbwl_sni_watcher;

struct fbwl_sni_item {
    struct wl_list link;
    struct fbwl_sni_watcher *watcher;
    char *id;
    char *service;
    char *owner;
    char *path;
    char *icon_theme_path;
    struct wlr_buffer *icon_buf;
    struct wlr_buffer *base_buf;
    struct wlr_buffer *attention_buf;
    struct wlr_buffer *overlay_buf;
    struct wlr_buffer *render_buf;
    enum fbwl_sni_status status;
    struct wlr_buffer *render_primary_buf;
    struct wlr_buffer *render_overlay_buf;
    enum fbwl_sni_status render_status;
    sd_bus_slot *slot_get_icon;
    sd_bus_slot *slot_get_icon_name;
    sd_bus_slot *slot_get_icon_theme_path;
    sd_bus_slot *slot_get_attention_icon;
    sd_bus_slot *slot_get_attention_icon_name;
    sd_bus_slot *slot_get_overlay_icon;
    sd_bus_slot *slot_get_overlay_icon_name;
    sd_bus_slot *slot_get_status;
    sd_bus_slot *slot_props_changed;
    sd_bus_slot *slot_new_icon;
    sd_bus_slot *slot_new_attention_icon;
    sd_bus_slot *slot_new_overlay_icon;
    sd_bus_slot *slot_new_status;
    sd_bus_slot *slot_new_icon_theme_path;
};

typedef void (*fbwl_sni_on_change_fn)(void *userdata);

struct fbwl_sni_watcher {
    sd_bus *bus;
    sd_bus_slot *slot_vtable;
    sd_bus_slot *slot_name_owner_changed;
    struct wl_event_source *event_source_fd;
    struct wl_event_source *event_source_timer;
    bool host_registered;
    struct wl_list items; // fbwl_sni_item

    fbwl_sni_on_change_fn on_change;
    void *on_change_userdata;
};

bool fbwl_sni_start(struct fbwl_sni_watcher *watcher, struct wl_event_loop *loop, fbwl_sni_on_change_fn on_change,
    void *userdata);
void fbwl_sni_finish(struct fbwl_sni_watcher *watcher);

void fbwl_sni_send_item_action(struct fbwl_sni_watcher *watcher, const char *id, const char *service, const char *path,
    const char *method, const char *action, int x, int y);

#endif

