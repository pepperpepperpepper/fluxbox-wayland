#include "wayland/fbwl_sni_tray_internal.h"

#ifdef HAVE_SYSTEMD

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <systemd/sd-bus.h>

#include <wlr/util/log.h>

static void sni_watcher_notify_change(struct fbwl_sni_watcher *watcher) {
    if (watcher == NULL || watcher->on_change == NULL) {
        return;
    }
    watcher->on_change(watcher->on_change_userdata);
}

static uint32_t wl_event_mask_from_sdbus_events(uint32_t events) {
    uint32_t mask = 0;
    if ((events & POLLIN) != 0) {
        mask |= WL_EVENT_READABLE;
    }
    if ((events & POLLOUT) != 0) {
        mask |= WL_EVENT_WRITABLE;
    }
    return mask;
}

static struct fbwl_sni_item *sni_item_find(struct fbwl_sni_watcher *watcher, const char *id) {
    if (watcher == NULL || id == NULL) {
        return NULL;
    }

    struct fbwl_sni_item *item;
    wl_list_for_each(item, &watcher->items, link) {
        if (item->id != NULL && strcmp(item->id, id) == 0) {
            return item;
        }
    }
    return NULL;
}

static int sni_prop_registered_items(sd_bus *bus, const char *path, const char *interface,
        const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct fbwl_sni_watcher *watcher = userdata;
    if (watcher == NULL) {
        return -EINVAL;
    }

    int r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) {
        return r;
    }

    struct fbwl_sni_item *item;
    wl_list_for_each(item, &watcher->items, link) {
        const char *id = item->id != NULL ? item->id : "";
        r = sd_bus_message_append(reply, "s", id);
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int sni_prop_host_registered(sd_bus *bus, const char *path, const char *interface,
        const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct fbwl_sni_watcher *watcher = userdata;
    if (watcher == NULL) {
        return -EINVAL;
    }
    return sd_bus_message_append(reply, "b", watcher->host_registered);
}

static int sni_prop_protocol_version(sd_bus *bus, const char *path, const char *interface,
        const char *property, sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;
    return sd_bus_message_append(reply, "i", 0);
}

static int sni_item_activate_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: Activate reply error: %s: %s",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
    }

    return 0;
}

static int sni_item_secondary_activate_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: SecondaryActivate reply error: %s: %s",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
    }

    return 0;
}

static int sni_item_context_menu_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: ContextMenu reply error: %s: %s",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
    }

    return 0;
}

void fbwl_sni_send_item_action(struct fbwl_sni_watcher *watcher, const char *id, const char *service, const char *path,
        const char *method, const char *action, int x, int y) {
    if (watcher == NULL || watcher->bus == NULL || service == NULL || service[0] == '\0' || path == NULL || path[0] != '/') {
        return;
    }

    if (method == NULL || method[0] == '\0') {
        return;
    }

    sd_bus_message_handler_t reply = NULL;
    if (strcmp(method, "Activate") == 0) {
        reply = sni_item_activate_reply;
    } else if (strcmp(method, "SecondaryActivate") == 0) {
        reply = sni_item_secondary_activate_reply;
    } else if (strcmp(method, "ContextMenu") == 0) {
        reply = sni_item_context_menu_reply;
    }

    const char *id_safe = id != NULL ? id : "";
    const char *action_safe = action != NULL ? action : "";

    int r = sd_bus_call_method_async(watcher->bus, NULL, service, path,
        "org.kde.StatusNotifierItem", method, reply, watcher, "ii", x, y);
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: %s send failed id=%s: %s", action_safe, id_safe, strerror(-r));
    } else {
        wlr_log(WLR_INFO, "SNI: %s sent id=%s", action_safe, id_safe);
    }
}

static int sni_method_register_item(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct fbwl_sni_watcher *watcher = userdata;
    if (watcher == NULL || watcher->bus == NULL) {
        return -EINVAL;
    }

    const char *arg = NULL;
    int r = sd_bus_message_read(m, "s", &arg);
    if (r < 0) {
        return r;
    }

    const char *sender = sd_bus_message_get_sender(m);
    if (sender == NULL) {
        return -EINVAL;
    }

    const char *service = NULL;
    const char *path = NULL;
    char *service_dup = NULL;

    if (arg != NULL && arg[0] == '/') {
        service = sender;
        path = arg;
    } else if (arg != NULL) {
        const char *slash = strchr(arg, '/');
        if (slash != NULL) {
            service_dup = strndup(arg, (size_t)(slash - arg));
            service = service_dup;
            path = slash;
        } else {
            service = arg;
            path = "/StatusNotifierItem";
        }
    }

    if (service == NULL || *service == '\0' || path == NULL || path[0] != '/') {
        free(service_dup);
        return -EINVAL;
    }

    size_t id_len = strlen(service) + strlen(path) + 1;
    char *id = malloc(id_len);
    if (id == NULL) {
        free(service_dup);
        return -ENOMEM;
    }
    snprintf(id, id_len, "%s%s", service, path);

    struct fbwl_sni_item *existing = sni_item_find(watcher, id);
    if (existing == NULL) {
        struct fbwl_sni_item *item = calloc(1, sizeof(*item));
        if (item == NULL) {
            free(id);
            free(service_dup);
            return -ENOMEM;
        }
        item->watcher = watcher;
        item->id = id;
        item->service = strdup(service);
        item->owner = strdup(sender);
        item->path = strdup(path);
        if (item->service == NULL || item->owner == NULL || item->path == NULL) {
            sni_item_destroy(item);
            free(service_dup);
            return -ENOMEM;
        }
        wl_list_insert(&watcher->items, &item->link);

        wlr_log(WLR_INFO, "SNI: item registered id=%s", item->id);
        (void)sd_bus_emit_signal(watcher->bus, "/StatusNotifierWatcher",
            "org.kde.StatusNotifierWatcher", "StatusNotifierItemRegistered", "s", item->id);

        sni_watcher_notify_change(watcher);

        sni_item_subscribe(item);
        sni_item_request_all(item);
    } else {
        free(id);
    }

    free(service_dup);
    return sd_bus_reply_method_return(m, "");
}

static int sni_method_register_host(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct fbwl_sni_watcher *watcher = userdata;
    if (watcher == NULL || watcher->bus == NULL) {
        return -EINVAL;
    }

    const char *service = NULL;
    int r = sd_bus_message_read(m, "s", &service);
    if (r < 0) {
        return r;
    }

    if (!watcher->host_registered) {
        watcher->host_registered = true;
        wlr_log(WLR_INFO, "SNI: host registered service=%s", service != NULL ? service : "");
        (void)sd_bus_emit_signal(watcher->bus, "/StatusNotifierWatcher",
            "org.kde.StatusNotifierWatcher", "StatusNotifierHostRegistered", "");
    }

    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable sni_watcher_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", sni_method_register_item, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "", sni_method_register_host, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
    SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", sni_prop_registered_items, 0, 0),
    SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", sni_prop_host_registered, 0, 0),
    SD_BUS_PROPERTY("ProtocolVersion", "i", sni_prop_protocol_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

static void sni_watcher_update_sources(struct fbwl_sni_watcher *watcher) {
    if (watcher == NULL || watcher->bus == NULL) {
        return;
    }

    if (watcher->event_source_fd != NULL) {
        const uint32_t events = (uint32_t)sd_bus_get_events(watcher->bus);
        const uint32_t mask = wl_event_mask_from_sdbus_events(events);
        wl_event_source_fd_update(watcher->event_source_fd, mask);
    }

    if (watcher->event_source_timer != NULL) {
        uint64_t usec = UINT64_MAX;
        int r = sd_bus_get_timeout(watcher->bus, &usec);
        if (r < 0) {
            usec = UINT64_MAX;
        }

        int ms = 60000;
        if (usec != UINT64_MAX) {
            ms = (int)((usec + 999ULL) / 1000ULL);
            if (ms < 1) {
                ms = 1;
            }
        }
        wl_event_source_timer_update(watcher->event_source_timer, ms);
    }
}

static void sni_watcher_process(struct fbwl_sni_watcher *watcher) {
    if (watcher == NULL || watcher->bus == NULL) {
        return;
    }

    for (;;) {
        int r = sd_bus_process(watcher->bus, NULL);
        if (r < 0) {
            wlr_log(WLR_ERROR, "SNI: sd_bus_process failed: %s", strerror(-r));
            break;
        }
        if (r == 0) {
            break;
        }
    }

    sni_watcher_update_sources(watcher);
}

static int sni_watcher_handle_fd(int fd, uint32_t mask, void *data) {
    (void)fd;
    if ((mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) != 0) {
        return 0;
    }
    struct fbwl_sni_watcher *watcher = data;
    sni_watcher_process(watcher);
    return 0;
}

static int sni_watcher_handle_timer(void *data) {
    struct fbwl_sni_watcher *watcher = data;
    sni_watcher_process(watcher);
    return 0;
}

static int sni_name_owner_changed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct fbwl_sni_watcher *watcher = userdata;
    if (watcher == NULL || watcher->bus == NULL) {
        return 0;
    }

    const char *name = NULL;
    const char *old_owner = NULL;
    const char *new_owner = NULL;
    int r = sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
    if (r < 0) {
        return 0;
    }

    if (name == NULL || *name == '\0') {
        return 0;
    }

    if (new_owner != NULL && *new_owner != '\0') {
        return 0;
    }

    bool changed = false;
    struct fbwl_sni_item *item, *tmp;
    wl_list_for_each_safe(item, tmp, &watcher->items, link) {
        if (item->service != NULL && strcmp(item->service, name) == 0) {
            wlr_log(WLR_INFO, "SNI: item unregistered id=%s", item->id != NULL ? item->id : "");
            (void)sd_bus_emit_signal(watcher->bus, "/StatusNotifierWatcher",
                "org.kde.StatusNotifierWatcher", "StatusNotifierItemUnregistered", "s",
                item->id != NULL ? item->id : "");
            sni_item_destroy(item);
            changed = true;
        }
    }

    if (changed) {
        sni_watcher_notify_change(watcher);
    }

    return 0;
}

bool fbwl_sni_start(struct fbwl_sni_watcher *watcher, struct wl_event_loop *loop, fbwl_sni_on_change_fn on_change,
        void *userdata) {
    if (watcher == NULL || loop == NULL) {
        return false;
    }

    fbwl_sni_on_change_fn on_change_local = on_change;
    void *userdata_local = userdata;

    memset(watcher, 0, sizeof(*watcher));
    watcher->host_registered = true;
    wl_list_init(&watcher->items);
    watcher->on_change = on_change_local;
    watcher->on_change_userdata = userdata_local;

    int r = sd_bus_open_user(&watcher->bus);
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: disabled (no session bus): %s", strerror(-r));
        watcher->bus = NULL;
        return false;
    }

    r = sd_bus_request_name(watcher->bus, "org.kde.StatusNotifierWatcher", 0);
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: disabled (name busy): %s", strerror(-r));
        fbwl_sni_finish(watcher);
        return false;
    }

    r = sd_bus_add_object_vtable(watcher->bus, &watcher->slot_vtable, "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", sni_watcher_vtable, watcher);
    if (r < 0) {
        wlr_log(WLR_ERROR, "SNI: failed to add object vtable: %s", strerror(-r));
        fbwl_sni_finish(watcher);
        return false;
    }

    r = sd_bus_add_match(watcher->bus, &watcher->slot_name_owner_changed,
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
        sni_name_owner_changed, watcher);
    if (r < 0) {
        wlr_log(WLR_ERROR, "SNI: failed to add NameOwnerChanged match: %s", strerror(-r));
        fbwl_sni_finish(watcher);
        return false;
    }

    int fd = sd_bus_get_fd(watcher->bus);
    if (fd < 0) {
        wlr_log(WLR_ERROR, "SNI: sd_bus_get_fd failed: %s", strerror(-fd));
        fbwl_sni_finish(watcher);
        return false;
    }

    const uint32_t events = (uint32_t)sd_bus_get_events(watcher->bus);
    const uint32_t mask = wl_event_mask_from_sdbus_events(events);

    watcher->event_source_fd = wl_event_loop_add_fd(loop, fd, mask, sni_watcher_handle_fd, watcher);
    watcher->event_source_timer = wl_event_loop_add_timer(loop, sni_watcher_handle_timer, watcher);
    if (watcher->event_source_fd == NULL) {
        wlr_log(WLR_ERROR, "SNI: failed to add fd to wl_event_loop");
        fbwl_sni_finish(watcher);
        return false;
    }
    sni_watcher_update_sources(watcher);

    wlr_log(WLR_INFO, "SNI: watcher enabled (org.kde.StatusNotifierWatcher)");
    return true;
}

void fbwl_sni_finish(struct fbwl_sni_watcher *watcher) {
    if (watcher == NULL) {
        return;
    }

    if (watcher->items.prev != NULL && watcher->items.next != NULL) {
        struct fbwl_sni_item *item, *tmp;
        wl_list_for_each_safe(item, tmp, &watcher->items, link) {
            sni_item_destroy(item);
        }
    } else {
        wl_list_init(&watcher->items);
    }

    if (watcher->event_source_fd != NULL) {
        wl_event_source_remove(watcher->event_source_fd);
        watcher->event_source_fd = NULL;
    }
    if (watcher->event_source_timer != NULL) {
        wl_event_source_remove(watcher->event_source_timer);
        watcher->event_source_timer = NULL;
    }

    watcher->slot_name_owner_changed = sd_bus_slot_unref(watcher->slot_name_owner_changed);
    watcher->slot_vtable = sd_bus_slot_unref(watcher->slot_vtable);
    watcher->bus = sd_bus_unref(watcher->bus);

    watcher->on_change = NULL;
    watcher->on_change_userdata = NULL;
}

#endif

