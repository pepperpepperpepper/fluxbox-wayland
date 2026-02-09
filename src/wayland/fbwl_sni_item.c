#include "wayland/fbwl_sni_tray_internal.h"

#ifdef HAVE_SYSTEMD

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <systemd/sd-bus.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>

void sni_item_destroy(struct fbwl_sni_item *item) {
    if (item == NULL) {
        return;
    }
    item->slot_get_icon = sd_bus_slot_unref(item->slot_get_icon);
    item->slot_get_icon_name = sd_bus_slot_unref(item->slot_get_icon_name);
    item->slot_get_icon_theme_path = sd_bus_slot_unref(item->slot_get_icon_theme_path);
    item->slot_get_attention_icon = sd_bus_slot_unref(item->slot_get_attention_icon);
    item->slot_get_attention_icon_name = sd_bus_slot_unref(item->slot_get_attention_icon_name);
    item->slot_get_overlay_icon = sd_bus_slot_unref(item->slot_get_overlay_icon);
    item->slot_get_overlay_icon_name = sd_bus_slot_unref(item->slot_get_overlay_icon_name);
    item->slot_get_status = sd_bus_slot_unref(item->slot_get_status);
    item->slot_get_item_id = sd_bus_slot_unref(item->slot_get_item_id);
    item->slot_props_changed = sd_bus_slot_unref(item->slot_props_changed);
    item->slot_new_icon = sd_bus_slot_unref(item->slot_new_icon);
    item->slot_new_attention_icon = sd_bus_slot_unref(item->slot_new_attention_icon);
    item->slot_new_overlay_icon = sd_bus_slot_unref(item->slot_new_overlay_icon);
    item->slot_new_status = sd_bus_slot_unref(item->slot_new_status);
    item->slot_new_icon_theme_path = sd_bus_slot_unref(item->slot_new_icon_theme_path);
    item->icon_buf = NULL;
    if (item->render_buf != NULL) {
        wlr_buffer_drop(item->render_buf);
        item->render_buf = NULL;
    }
    if (item->base_buf != NULL) {
        wlr_buffer_drop(item->base_buf);
        item->base_buf = NULL;
    }
    if (item->attention_buf != NULL) {
        wlr_buffer_drop(item->attention_buf);
        item->attention_buf = NULL;
    }
    if (item->overlay_buf != NULL) {
        wlr_buffer_drop(item->overlay_buf);
        item->overlay_buf = NULL;
    }
    if (item->link.prev != NULL && item->link.next != NULL) {
        wl_list_remove(&item->link);
        item->link.prev = NULL;
        item->link.next = NULL;
    }
    free(item->id);
    free(item->item_id);
    free(item->service);
    free(item->owner);
    free(item->path);
    free(item->icon_theme_path);
    free(item);
}

static int sni_item_icon_signal(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)m;
    (void)ret_error;
    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }
    sni_item_request_all(item);
    return 0;
}

static int sni_item_properties_changed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        return 0;
    }

    sni_item_request_all(item);
    return 0;
}

void sni_item_subscribe(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_props_changed = sd_bus_slot_unref(item->slot_props_changed);
    item->slot_new_icon = sd_bus_slot_unref(item->slot_new_icon);
    item->slot_new_attention_icon = sd_bus_slot_unref(item->slot_new_attention_icon);
    item->slot_new_overlay_icon = sd_bus_slot_unref(item->slot_new_overlay_icon);
    item->slot_new_status = sd_bus_slot_unref(item->slot_new_status);
    item->slot_new_icon_theme_path = sd_bus_slot_unref(item->slot_new_icon_theme_path);

    if (item->owner == NULL || item->owner[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    {
        const char *tmpl =
            "type='signal',sender='%s',path='%s',interface='org.freedesktop.DBus.Properties',"
            "member='PropertiesChanged',arg0='org.kde.StatusNotifierItem'";
        int n = snprintf(NULL, 0, tmpl, item->owner, item->path);
        if (n > 0) {
            size_t len = (size_t)n + 1;
            char *match = malloc(len);
            if (match != NULL) {
                snprintf(match, len, tmpl, item->owner, item->path);
                int r = sd_bus_add_match(item->watcher->bus, &item->slot_props_changed, match,
                    sni_item_properties_changed, item);
                if (r < 0) {
                    wlr_log(WLR_INFO, "SNI: failed to add PropertiesChanged match id=%s: %s",
                        item->id != NULL ? item->id : "", strerror(-r));
                }
                free(match);
            }
        }
    }

    {
        const char *tmpl = "type='signal',sender='%s',path='%s',interface='org.kde.StatusNotifierItem',member='%s'";
        const char *members[] = {"NewIcon", "NewAttentionIcon", "NewOverlayIcon", "NewStatus", "NewIconThemePath"};
        sd_bus_slot **slots[] = {&item->slot_new_icon, &item->slot_new_attention_icon, &item->slot_new_overlay_icon,
            &item->slot_new_status, &item->slot_new_icon_theme_path};
        for (size_t i = 0; i < sizeof(members) / sizeof(members[0]); i++) {
            int n = snprintf(NULL, 0, tmpl, item->owner, item->path, members[i]);
            if (n <= 0) {
                continue;
            }
            size_t len = (size_t)n + 1;
            char *match = malloc(len);
            if (match == NULL) {
                continue;
            }
            snprintf(match, len, tmpl, item->owner, item->path, members[i]);

            int r = sd_bus_add_match(item->watcher->bus, slots[i], match, sni_item_icon_signal, item);
            if (r < 0) {
                wlr_log(WLR_INFO, "SNI: failed to add %s match id=%s: %s", members[i],
                    item->id != NULL ? item->id : "", strerror(-r));
            }
            free(match);
        }
    }
}

#endif
