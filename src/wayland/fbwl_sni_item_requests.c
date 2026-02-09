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

static void sni_item_update_render_icon(struct fbwl_sni_item *item) {
    if (item == NULL) {
        return;
    }

    struct wlr_buffer *primary = item->base_buf;
    if (item->status == FBWL_SNI_STATUS_NEEDS_ATTENTION && item->attention_buf != NULL) {
        primary = item->attention_buf;
    }

    if (item->status == FBWL_SNI_STATUS_PASSIVE) {
        if (item->render_buf != NULL) {
            wlr_buffer_drop(item->render_buf);
            item->render_buf = NULL;
        }
        item->icon_buf = NULL;
        item->render_primary_buf = NULL;
        item->render_overlay_buf = NULL;
        item->render_status = item->status;

        if (item->watcher != NULL && item->watcher->on_change != NULL) {
            item->watcher->on_change(item->watcher->on_change_userdata);
        }
        return;
    }

    const bool want_overlay = primary != NULL && item->overlay_buf != NULL;
    struct wlr_buffer *want_overlay_buf = want_overlay ? item->overlay_buf : NULL;

    struct wlr_buffer *desired = NULL;
    if (!want_overlay) {
        if (primary != NULL) {
            desired = primary;
        } else if (item->overlay_buf != NULL) {
            desired = item->overlay_buf;
        }
    }

    if (item->render_status == item->status && item->render_primary_buf == primary &&
            item->render_overlay_buf == want_overlay_buf) {
        if (want_overlay) {
            if (item->render_buf != NULL && item->icon_buf == item->render_buf) {
                return;
            }
        } else {
            if (item->render_buf == NULL && item->icon_buf == desired) {
                return;
            }
        }
    }

    struct wlr_buffer *new_render = NULL;
    struct wlr_buffer *new_icon = desired;
    if (want_overlay) {
        new_render = sni_icon_compose_overlay(primary, item->overlay_buf);
        if (new_render != NULL) {
            new_icon = new_render;
        } else {
            new_icon = primary;
        }
    }

    if (item->render_buf != NULL) {
        wlr_buffer_drop(item->render_buf);
        item->render_buf = NULL;
    }
    item->render_buf = new_render;
    item->icon_buf = new_icon;
    item->render_primary_buf = primary;
    item->render_overlay_buf = want_overlay_buf;
    item->render_status = item->status;

    if (item->icon_buf != NULL) {
        wlr_log(WLR_INFO, "SNI: icon updated id=%s status=%s", item->id != NULL ? item->id : "",
            sni_status_str(item->status));
    }

    if (item->watcher != NULL && item->watcher->on_change != NULL) {
        item->watcher->on_change(item->watcher->on_change_userdata);
    }
}

static int sni_item_get_icon_name_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_icon_name = sd_bus_slot_unref(item->slot_get_icon_name);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: IconName query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        return 0;
    }

    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
    if (r < 0) {
        return 0;
    }

    const char *icon_name = NULL;
    r = sd_bus_message_read(m, "s", &icon_name);
    (void)sd_bus_message_exit_container(m);
    if (r < 0 || icon_name == NULL || icon_name[0] == '\0') {
        return 0;
    }

    char *path = sni_icon_resolve_png_path(icon_name, item->icon_theme_path);
    if (path == NULL) {
        wlr_log(WLR_INFO, "SNI: IconName not found for id=%s name=%s", item->id != NULL ? item->id : "", icon_name);
        return 0;
    }

    struct wlr_buffer *buf = sni_icon_buffer_from_png_path(path);
    if (buf == NULL) {
        wlr_log(WLR_INFO, "SNI: IconName load failed for id=%s name=%s path=%s",
            item->id != NULL ? item->id : "", icon_name, path);
        free(path);
        return 0;
    }

    if (item->base_buf != NULL) {
        wlr_buffer_drop(item->base_buf);
        item->base_buf = NULL;
    }

    item->base_buf = buf;
    sni_item_update_render_icon(item);

    free(path);
    return 0;
}

static void sni_item_request_icon_name(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_icon_name = sd_bus_slot_unref(item->slot_get_icon_name);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_icon_name, item->service, item->path,
        "org.freedesktop.DBus.Properties", "Get", sni_item_get_icon_name_reply, item, "ss",
        "org.kde.StatusNotifierItem", "IconName");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: IconName query send failed for id=%s: %s",
            item->id != NULL ? item->id : "", strerror(-r));
    }
}

static struct wlr_buffer *sni_item_parse_icon_pixmap_get_reply(sd_bus_message *m) {
    if (m == NULL) {
        return NULL;
    }

    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a(iiay)");
    if (r < 0) {
        return NULL;
    }

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "(iiay)");
    if (r < 0) {
        (void)sd_bus_message_exit_container(m);
        return NULL;
    }

    int best_w = 0;
    int best_h = 0;
    const uint8_t *best_data = NULL;
    size_t best_len = 0;

    for (;;) {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_STRUCT, "iiay");
        if (r < 0 || r == 0) {
            break;
        }

        int32_t w = 0;
        int32_t h = 0;
        const uint8_t *data = NULL;
        size_t len = 0;

        r = sd_bus_message_read(m, "ii", &w, &h);
        if (r >= 0) {
            r = sd_bus_message_read_array(m, 'y', (const void **)&data, &len);
        }

        (void)sd_bus_message_exit_container(m);

        if (r < 0 || w < 1 || h < 1) {
            continue;
        }
        if (w > 1024 || h > 1024) {
            continue;
        }
        const size_t need = (size_t)w * (size_t)h * 4;
        if (need / 4 != (size_t)w * (size_t)h || len < need) {
            continue;
        }

        const size_t area = (size_t)w * (size_t)h;
        const size_t best_area = (size_t)best_w * (size_t)best_h;
        if (area > best_area) {
            best_w = (int)w;
            best_h = (int)h;
            best_data = data;
            best_len = len;
        }
    }

    (void)sd_bus_message_exit_container(m);
    (void)sd_bus_message_exit_container(m);

    if (best_data == NULL || best_w < 1 || best_h < 1) {
        return NULL;
    }
    return sni_icon_buffer_from_argb32(best_data, best_len, best_w, best_h);
}

static int sni_item_get_icon_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_icon = sd_bus_slot_unref(item->slot_get_icon);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: icon query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        sni_item_request_icon_name(item);
        return 0;
    }

    struct wlr_buffer *buf = sni_item_parse_icon_pixmap_get_reply(m);

    if (buf != NULL) {
        if (item->base_buf != NULL) {
            wlr_buffer_drop(item->base_buf);
            item->base_buf = NULL;
        }
        item->base_buf = buf;
        sni_item_update_render_icon(item);
    } else {
        sni_item_request_icon_name(item);
    }

    return 0;
}

static void sni_item_request_icon(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_icon = sd_bus_slot_unref(item->slot_get_icon);
    item->slot_get_icon_name = sd_bus_slot_unref(item->slot_get_icon_name);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_icon, item->service, item->path,
        "org.freedesktop.DBus.Properties", "Get", sni_item_get_icon_reply, item, "ss",
        "org.kde.StatusNotifierItem", "IconPixmap");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: icon query send failed for id=%s: %s",
            item->id != NULL ? item->id : "", strerror(-r));
    }
}

static int sni_item_get_attention_icon_name_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_attention_icon_name = sd_bus_slot_unref(item->slot_get_attention_icon_name);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: AttentionIconName query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        return 0;
    }

    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
    if (r < 0) {
        return 0;
    }

    const char *icon_name = NULL;
    r = sd_bus_message_read(m, "s", &icon_name);
    (void)sd_bus_message_exit_container(m);
    if (r < 0 || icon_name == NULL || icon_name[0] == '\0') {
        return 0;
    }

    char *path = sni_icon_resolve_png_path(icon_name, item->icon_theme_path);
    if (path == NULL) {
        wlr_log(WLR_INFO, "SNI: AttentionIconName not found for id=%s name=%s", item->id != NULL ? item->id : "",
            icon_name);
        return 0;
    }

    struct wlr_buffer *buf = sni_icon_buffer_from_png_path(path);
    if (buf == NULL) {
        wlr_log(WLR_INFO, "SNI: AttentionIconName load failed for id=%s name=%s path=%s",
            item->id != NULL ? item->id : "", icon_name, path);
        free(path);
        return 0;
    }

    if (item->attention_buf != NULL) {
        wlr_buffer_drop(item->attention_buf);
        item->attention_buf = NULL;
    }

    item->attention_buf = buf;
    sni_item_update_render_icon(item);

    free(path);
    return 0;
}

static void sni_item_request_attention_icon_name(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_attention_icon_name = sd_bus_slot_unref(item->slot_get_attention_icon_name);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_attention_icon_name, item->service,
        item->path, "org.freedesktop.DBus.Properties", "Get", sni_item_get_attention_icon_name_reply, item, "ss",
        "org.kde.StatusNotifierItem", "AttentionIconName");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: AttentionIconName query send failed for id=%s: %s",
            item->id != NULL ? item->id : "", strerror(-r));
    }
}

static int sni_item_get_attention_icon_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_attention_icon = sd_bus_slot_unref(item->slot_get_attention_icon);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: AttentionIconPixmap query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        sni_item_request_attention_icon_name(item);
        return 0;
    }

    struct wlr_buffer *buf = sni_item_parse_icon_pixmap_get_reply(m);
    if (buf != NULL) {
        if (item->attention_buf != NULL) {
            wlr_buffer_drop(item->attention_buf);
            item->attention_buf = NULL;
        }
        item->attention_buf = buf;
        sni_item_update_render_icon(item);
    } else {
        sni_item_request_attention_icon_name(item);
    }

    return 0;
}

static void sni_item_request_attention_icon(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_attention_icon = sd_bus_slot_unref(item->slot_get_attention_icon);
    item->slot_get_attention_icon_name = sd_bus_slot_unref(item->slot_get_attention_icon_name);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_attention_icon, item->service, item->path,
        "org.freedesktop.DBus.Properties", "Get", sni_item_get_attention_icon_reply, item, "ss",
        "org.kde.StatusNotifierItem", "AttentionIconPixmap");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: AttentionIconPixmap query send failed for id=%s: %s",
            item->id != NULL ? item->id : "", strerror(-r));
    }
}

static int sni_item_get_overlay_icon_name_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_overlay_icon_name = sd_bus_slot_unref(item->slot_get_overlay_icon_name);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: OverlayIconName query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        return 0;
    }

    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
    if (r < 0) {
        return 0;
    }

    const char *icon_name = NULL;
    r = sd_bus_message_read(m, "s", &icon_name);
    (void)sd_bus_message_exit_container(m);
    if (r < 0 || icon_name == NULL || icon_name[0] == '\0') {
        return 0;
    }

    char *path = sni_icon_resolve_png_path(icon_name, item->icon_theme_path);
    if (path == NULL) {
        wlr_log(WLR_INFO, "SNI: OverlayIconName not found for id=%s name=%s", item->id != NULL ? item->id : "",
            icon_name);
        return 0;
    }

    struct wlr_buffer *buf = sni_icon_buffer_from_png_path(path);
    if (buf == NULL) {
        wlr_log(WLR_INFO, "SNI: OverlayIconName load failed for id=%s name=%s path=%s",
            item->id != NULL ? item->id : "", icon_name, path);
        free(path);
        return 0;
    }

    if (item->overlay_buf != NULL) {
        wlr_buffer_drop(item->overlay_buf);
        item->overlay_buf = NULL;
    }

    item->overlay_buf = buf;
    sni_item_update_render_icon(item);

    free(path);
    return 0;
}

static void sni_item_request_overlay_icon(struct fbwl_sni_item *item);

static int sni_item_get_icon_theme_path_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_icon_theme_path = sd_bus_slot_unref(item->slot_get_icon_theme_path);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: IconThemePath query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        return 0;
    }

    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
    if (r < 0) {
        return 0;
    }

    const char *path = NULL;
    r = sd_bus_message_read(m, "s", &path);
    (void)sd_bus_message_exit_container(m);
    if (r < 0) {
        return 0;
    }

    const char *new_path = path != NULL && path[0] != '\0' ? path : NULL;
    const bool same = (new_path == NULL && item->icon_theme_path == NULL) ||
        (new_path != NULL && item->icon_theme_path != NULL && strcmp(new_path, item->icon_theme_path) == 0);
    if (same) {
        return 0;
    }

    free(item->icon_theme_path);
    item->icon_theme_path = new_path != NULL ? strdup(new_path) : NULL;
    wlr_log(WLR_INFO, "SNI: icon theme path updated id=%s path=%s",
        item->id != NULL ? item->id : "", item->icon_theme_path != NULL ? item->icon_theme_path : "");

    sni_item_request_icon(item);
    sni_item_request_attention_icon(item);
    sni_item_request_overlay_icon(item);

    return 0;
}

static void sni_item_request_icon_theme_path(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_icon_theme_path = sd_bus_slot_unref(item->slot_get_icon_theme_path);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_icon_theme_path, item->service, item->path,
        "org.freedesktop.DBus.Properties", "Get", sni_item_get_icon_theme_path_reply, item, "ss",
        "org.kde.StatusNotifierItem", "IconThemePath");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: IconThemePath query send failed for id=%s: %s",
            item->id != NULL ? item->id : "", strerror(-r));
    }
}

static void sni_item_request_overlay_icon_name(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_overlay_icon_name = sd_bus_slot_unref(item->slot_get_overlay_icon_name);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_overlay_icon_name, item->service, item->path,
        "org.freedesktop.DBus.Properties", "Get", sni_item_get_overlay_icon_name_reply, item, "ss",
        "org.kde.StatusNotifierItem", "OverlayIconName");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: OverlayIconName query send failed for id=%s: %s",
            item->id != NULL ? item->id : "", strerror(-r));
    }
}

static int sni_item_get_overlay_icon_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_overlay_icon = sd_bus_slot_unref(item->slot_get_overlay_icon);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: OverlayIconPixmap query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        sni_item_request_overlay_icon_name(item);
        return 0;
    }

    struct wlr_buffer *buf = sni_item_parse_icon_pixmap_get_reply(m);
    if (buf != NULL) {
        if (item->overlay_buf != NULL) {
            wlr_buffer_drop(item->overlay_buf);
            item->overlay_buf = NULL;
        }
        item->overlay_buf = buf;
        sni_item_update_render_icon(item);
    } else {
        sni_item_request_overlay_icon_name(item);
    }

    return 0;
}

static void sni_item_request_overlay_icon(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_overlay_icon = sd_bus_slot_unref(item->slot_get_overlay_icon);
    item->slot_get_overlay_icon_name = sd_bus_slot_unref(item->slot_get_overlay_icon_name);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_overlay_icon, item->service, item->path,
        "org.freedesktop.DBus.Properties", "Get", sni_item_get_overlay_icon_reply, item, "ss",
        "org.kde.StatusNotifierItem", "OverlayIconPixmap");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: OverlayIconPixmap query send failed for id=%s: %s", item->id != NULL ? item->id : "",
            strerror(-r));
    }
}

static int sni_item_get_status_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_status = sd_bus_slot_unref(item->slot_get_status);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: Status query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        return 0;
    }

    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
    if (r < 0) {
        return 0;
    }

    const char *status = NULL;
    r = sd_bus_message_read(m, "s", &status);
    (void)sd_bus_message_exit_container(m);
    if (r < 0 || status == NULL || status[0] == '\0') {
        return 0;
    }

    enum fbwl_sni_status parsed = sni_status_parse(status);
    if (parsed == item->status) {
        return 0;
    }

    item->status = parsed;
    wlr_log(WLR_INFO, "SNI: status updated id=%s status=%s", item->id != NULL ? item->id : "",
        sni_status_str(item->status));
    sni_item_update_render_icon(item);
    return 0;
}

static int sni_item_get_item_id_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;

    struct fbwl_sni_item *item = userdata;
    if (item == NULL) {
        return 0;
    }

    item->slot_get_item_id = sd_bus_slot_unref(item->slot_get_item_id);

    if (m == NULL) {
        return 0;
    }

    if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        wlr_log(WLR_INFO, "SNI: Id query error for id=%s: %s: %s",
            item->id != NULL ? item->id : "",
            err != NULL && err->name != NULL ? err->name : "",
            err != NULL && err->message != NULL ? err->message : "");
        return 0;
    }

    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
    if (r < 0) {
        return 0;
    }

    const char *item_id = NULL;
    r = sd_bus_message_read(m, "s", &item_id);
    (void)sd_bus_message_exit_container(m);
    if (r < 0 || item_id == NULL) {
        return 0;
    }

    if (item_id[0] == '\0') {
        if (item->item_id == NULL) {
            return 0;
        }
        free(item->item_id);
        item->item_id = NULL;
        if (item->watcher != NULL && item->watcher->on_change != NULL) {
            item->watcher->on_change(item->watcher->on_change_userdata);
        }
        return 0;
    }

    if (item->item_id != NULL && strcmp(item->item_id, item_id) == 0) {
        return 0;
    }

    char *dup = strdup(item_id);
    if (dup == NULL) {
        return 0;
    }

    free(item->item_id);
    item->item_id = dup;
    wlr_log(WLR_INFO, "SNI: item id updated id=%s item_id=%s", item->id != NULL ? item->id : "",
        item->item_id != NULL ? item->item_id : "");

    if (item->watcher != NULL && item->watcher->on_change != NULL) {
        item->watcher->on_change(item->watcher->on_change_userdata);
    }
    return 0;
}

static void sni_item_request_item_id(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_item_id = sd_bus_slot_unref(item->slot_get_item_id);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_item_id, item->service, item->path,
        "org.freedesktop.DBus.Properties", "Get", sni_item_get_item_id_reply, item, "ss",
        "org.kde.StatusNotifierItem", "Id");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: Id query send failed for id=%s: %s",
            item->id != NULL ? item->id : "", strerror(-r));
    }
}

static void sni_item_request_status(struct fbwl_sni_item *item) {
    if (item == NULL || item->watcher == NULL || item->watcher->bus == NULL) {
        return;
    }

    item->slot_get_status = sd_bus_slot_unref(item->slot_get_status);

    if (item->service == NULL || item->service[0] == '\0' || item->path == NULL || item->path[0] != '/') {
        return;
    }

    int r = sd_bus_call_method_async(item->watcher->bus, &item->slot_get_status, item->service, item->path,
        "org.freedesktop.DBus.Properties", "Get", sni_item_get_status_reply, item, "ss",
        "org.kde.StatusNotifierItem", "Status");
    if (r < 0) {
        wlr_log(WLR_INFO, "SNI: Status query send failed for id=%s: %s", item->id != NULL ? item->id : "",
            strerror(-r));
    }
}

void sni_item_request_all(struct fbwl_sni_item *item) {
    if (item == NULL) {
        return;
    }

    sni_item_request_status(item);
    sni_item_request_item_id(item);
    sni_item_request_icon_theme_path(item);
    sni_item_request_icon(item);
    sni_item_request_attention_icon(item);
    sni_item_request_overlay_icon(item);
}

#endif

