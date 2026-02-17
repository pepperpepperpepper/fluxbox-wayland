#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#include <xcb/xcb.h>
#include <xcb/composite.h>

static volatile sig_atomic_t g_stop = 0;
static bool g_debug = false;

static void handle_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static bool env_is_truthy(const char *name) {
    const char *v = getenv(name);
    if (v == NULL || *v == '\0') {
        return false;
    }
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "no") == 0 || strcmp(v, "off") == 0) {
        return false;
    }
    return true;
}

static uint64_t now_ms(void) {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static xcb_atom_t intern_atom(xcb_connection_t *conn, const char *name) {
    if (conn == NULL || name == NULL) {
        return XCB_ATOM_NONE;
    }
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return XCB_ATOM_NONE;
    }
    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

static xcb_visualtype_t *find_visual(xcb_screen_t *screen, xcb_visualid_t visual_id) {
    if (screen == NULL) {
        return NULL;
    }

    xcb_depth_iterator_t depth_it = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_it.rem; xcb_depth_next(&depth_it)) {
        xcb_visualtype_iterator_t vis_it = xcb_depth_visuals_iterator(depth_it.data);
        for (; vis_it.rem; xcb_visualtype_next(&vis_it)) {
            if (vis_it.data != NULL && vis_it.data->visual_id == visual_id) {
                return vis_it.data;
            }
        }
    }
    return NULL;
}

static uint32_t count_trailing_zeros_u32(uint32_t x) {
    if (x == 0) {
        return 0;
    }
    return (uint32_t)__builtin_ctz(x);
}

static uint8_t scale_to_u8(uint32_t v, uint32_t max) {
    if (max == 0) {
        return 0;
    }
    if (v > max) {
        v = max;
    }
    return (uint8_t)((v * 255U + max / 2U) / max);
}

struct xembed_item {
    struct xembed_item *next;

    xcb_window_t xwin;
    char *path;
    char *item_id;
    const char *status;

    int icon_w;
    int icon_h;
    uint8_t *icon_argb;
    size_t icon_len;

    bool dirty;
    uint64_t dirty_after_ms;

    sd_bus_slot *slot_vtable;
};

struct proxy {
    xcb_connection_t *xconn;
    xcb_screen_t *xscreen;
    int xfd;
    xcb_window_t mgr_win;

    xcb_atom_t atom_manager;
    xcb_atom_t atom_systray_sel;
    xcb_atom_t atom_systray_opcode;
    xcb_atom_t atom_xembed;

    sd_bus *bus;
    const char *watcher_name;

    struct xembed_item *items;
};

static int sni_method_noop(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;
    if (m == NULL) {
        return 0;
    }
    return sd_bus_reply_method_return(m, "");
}

static int sni_prop_id(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    const struct xembed_item *item = userdata;
    return sd_bus_message_append(reply, "s", item != NULL && item->item_id != NULL ? item->item_id : "");
}

static int sni_prop_status(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    const struct xembed_item *item = userdata;
    return sd_bus_message_append(reply, "s", item != NULL && item->status != NULL ? item->status : "Active");
}

static int sni_prop_icon_theme_path(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;
    return sd_bus_message_append(reply, "s", "");
}

static int sni_prop_empty_str(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;
    return sd_bus_message_append(reply, "s", "");
}

static int sni_prop_icon_pixmap(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    const struct xembed_item *item = userdata;

    int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) {
        return r;
    }

    if (item != NULL && item->icon_argb != NULL && item->icon_w > 0 && item->icon_h > 0 && item->icon_len > 0) {
        r = sd_bus_message_open_container(reply, 'r', "iiay");
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append(reply, "ii", item->icon_w, item->icon_h);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append_array(reply, 'y', item->icon_argb, item->icon_len);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_close_container(reply);
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int sni_prop_empty_pixmap(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) {
        return r;
    }
    return sd_bus_message_close_container(reply);
}

static const sd_bus_vtable sni_item_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Activate", "ii", "", sni_method_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", sni_method_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ContextMenu", "ii", "", sni_method_noop, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_PROPERTY("Id", "s", sni_prop_id, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Title", "s", sni_prop_empty_str, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status", "s", sni_prop_status, 0, 0),

    SD_BUS_PROPERTY("IconName", "s", sni_prop_empty_str, 0, 0),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", sni_prop_icon_pixmap, 0, 0),
    SD_BUS_PROPERTY("IconThemePath", "s", sni_prop_icon_theme_path, 0, 0),

    SD_BUS_PROPERTY("AttentionIconName", "s", sni_prop_empty_str, 0, 0),
    SD_BUS_PROPERTY("AttentionIconPixmap", "a(iiay)", sni_prop_empty_pixmap, 0, 0),

    SD_BUS_PROPERTY("OverlayIconName", "s", sni_prop_empty_str, 0, 0),
    SD_BUS_PROPERTY("OverlayIconPixmap", "a(iiay)", sni_prop_empty_pixmap, 0, 0),

    SD_BUS_VTABLE_END,
};

static struct xembed_item *proxy_item_find(struct proxy *p, xcb_window_t xwin) {
    if (p == NULL) {
        return NULL;
    }
    for (struct xembed_item *it = p->items; it != NULL; it = it->next) {
        if (it->xwin == xwin) {
            return it;
        }
    }
    return NULL;
}

static void proxy_item_destroy(struct proxy *p, struct xembed_item *item) {
    if (p == NULL || item == NULL) {
        return;
    }

    item->slot_vtable = sd_bus_slot_unref(item->slot_vtable);
    free(item->path);
    free(item->item_id);
    free(item->icon_argb);
    free(item);
}

static void proxy_item_remove(struct proxy *p, xcb_window_t xwin) {
    if (p == NULL) {
        return;
    }

    struct xembed_item *prev = NULL;
    struct xembed_item *cur = p->items;
    while (cur != NULL) {
        if (cur->xwin == xwin) {
            if (prev != NULL) {
                prev->next = cur->next;
            } else {
                p->items = cur->next;
            }
            proxy_item_destroy(p, cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static bool proxy_register_item(struct proxy *p, struct xembed_item *item) {
    if (p == NULL || p->bus == NULL || p->watcher_name == NULL || item == NULL || item->path == NULL) {
        return false;
    }

    int r = sd_bus_add_object_vtable(p->bus, &item->slot_vtable, item->path,
        "org.kde.StatusNotifierItem", sni_item_vtable, item);
    if (r < 0) {
        fprintf(stderr, "xembed-sni-proxy: failed to add SNI item vtable for %s: %s\n", item->path, strerror(-r));
        return false;
    }

    r = sd_bus_call_method(p->bus, p->watcher_name, "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
        NULL, NULL, "s", item->path);
    if (r < 0) {
        fprintf(stderr, "xembed-sni-proxy: RegisterStatusNotifierItem failed for %s: %s\n",
            item->path, strerror(-r));
        return false;
    }

    return true;
}

static void proxy_item_emit_icon_changed(struct proxy *p, struct xembed_item *item) {
    if (p == NULL || p->bus == NULL || item == NULL || item->path == NULL) {
        return;
    }
    (void)sd_bus_emit_properties_changed(p->bus, item->path, "org.kde.StatusNotifierItem", "IconPixmap", NULL);
    (void)sd_bus_emit_signal(p->bus, item->path, "org.kde.StatusNotifierItem", "NewIcon", "");
}

static bool proxy_capture_icon(struct proxy *p, struct xembed_item *item) {
    if (p == NULL || p->xconn == NULL || p->xscreen == NULL || item == NULL) {
        return false;
    }

    xcb_get_geometry_cookie_t geom_cookie = xcb_get_geometry(p->xconn, item->xwin);
    xcb_generic_error_t *geom_err = NULL;
    xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(p->xconn, geom_cookie, &geom_err);
    if (geom == NULL) {
        if (g_debug && geom_err != NULL) {
            fprintf(stderr, "xembed-sni-proxy: get_geometry failed win=0x%08x err=%u\n",
                (unsigned)item->xwin, (unsigned)geom_err->error_code);
            fflush(stderr);
        }
        free(geom_err);
        return false;
    }
    free(geom_err);

    const uint16_t w = geom->width;
    const uint16_t h = geom->height;
    if (w < 1 || h < 1 || w > 256 || h > 256) {
        if (g_debug) {
            fprintf(stderr, "xembed-sni-proxy: get_geometry bad size win=0x%08x %ux%u\n",
                (unsigned)item->xwin, (unsigned)w, (unsigned)h);
            fflush(stderr);
        }
        free(geom);
        return false;
    }

    xcb_get_window_attributes_cookie_t attr_cookie = xcb_get_window_attributes(p->xconn, item->xwin);
    xcb_generic_error_t *attr_err = NULL;
    xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(p->xconn, attr_cookie, &attr_err);
    if (attr == NULL) {
        if (g_debug && attr_err != NULL) {
            fprintf(stderr, "xembed-sni-proxy: get_window_attributes failed win=0x%08x err=%u\n",
                (unsigned)item->xwin, (unsigned)attr_err->error_code);
            fflush(stderr);
        }
        free(attr_err);
        free(geom);
        return false;
    }
    free(attr_err);

    xcb_visualtype_t *vis = find_visual(p->xscreen, attr->visual);
    uint32_t red_mask = vis != NULL ? vis->red_mask : 0;
    uint32_t green_mask = vis != NULL ? vis->green_mask : 0;
    uint32_t blue_mask = vis != NULL ? vis->blue_mask : 0;

    // Fallback: many servers effectively behave like XRGB8888 even if we can't resolve the visual.
    if (red_mask == 0 || green_mask == 0 || blue_mask == 0) {
        red_mask = 0x00ff0000U;
        green_mask = 0x0000ff00U;
        blue_mask = 0x000000ffU;
    }

    const uint32_t r_shift = count_trailing_zeros_u32(red_mask);
    const uint32_t g_shift = count_trailing_zeros_u32(green_mask);
    const uint32_t b_shift = count_trailing_zeros_u32(blue_mask);

    const uint32_t r_max = red_mask >> r_shift;
    const uint32_t g_max = green_mask >> g_shift;
    const uint32_t b_max = blue_mask >> b_shift;

    free(attr);

    // Use XComposite to name a pixmap for the window contents; direct GetImage on the window can
    // fail on XWayland (BadMatch).
    xcb_pixmap_t pix = xcb_generate_id(p->xconn);
    xcb_void_cookie_t name_cookie = xcb_composite_name_window_pixmap_checked(p->xconn, item->xwin, pix);
    xcb_generic_error_t *name_err = xcb_request_check(p->xconn, name_cookie);
    if (name_err != NULL) {
        if (g_debug) {
            fprintf(stderr, "xembed-sni-proxy: name_window_pixmap failed win=0x%08x err=%u\n",
                (unsigned)item->xwin, (unsigned)name_err->error_code);
            fflush(stderr);
        }
        free(name_err);
        free(geom);
        return false;
    }

    xcb_get_image_cookie_t img_cookie =
        xcb_get_image(p->xconn, XCB_IMAGE_FORMAT_Z_PIXMAP, pix, 0, 0, w, h, UINT32_MAX);
    xcb_generic_error_t *img_err = NULL;
    xcb_get_image_reply_t *img = xcb_get_image_reply(p->xconn, img_cookie, &img_err);
    if (img == NULL) {
        if (g_debug && img_err != NULL) {
            fprintf(stderr, "xembed-sni-proxy: get_image failed win=0x%08x err=%u\n",
                (unsigned)item->xwin, (unsigned)img_err->error_code);
            fflush(stderr);
        }
        free(img_err);
        xcb_free_pixmap(p->xconn, pix);
        free(geom);
        return false;
    }
    free(img_err);
    xcb_free_pixmap(p->xconn, pix);

    const uint8_t *data = xcb_get_image_data(img);
    const size_t len = (size_t)xcb_get_image_data_length(img);

    size_t stride = 0;
    if (h > 0 && len % h == 0) {
        stride = len / h;
    }

    int bits_per_pixel = 0;
    const xcb_setup_t *setup = xcb_get_setup(p->xconn);
    if (setup != NULL) {
        xcb_format_iterator_t fmt_it = xcb_setup_pixmap_formats_iterator(setup);
        for (; fmt_it.rem; xcb_format_next(&fmt_it)) {
            if (fmt_it.data != NULL && fmt_it.data->depth == img->depth) {
                bits_per_pixel = (int)fmt_it.data->bits_per_pixel;
                break;
            }
        }
    }

    size_t bytes_per_pixel = 4;
    if (bits_per_pixel > 0) {
        bytes_per_pixel = (size_t)((bits_per_pixel + 7) / 8);
    }
    if (bytes_per_pixel < 1 || bytes_per_pixel > 4) {
        bytes_per_pixel = 4;
    }

    if (stride < (size_t)w * bytes_per_pixel) {
        if (g_debug) {
            fprintf(stderr, "xembed-sni-proxy: get_image unexpected stride win=0x%08x depth=%u bpp=%d stride=%zu len=%zu wh=%ux%u\n",
                (unsigned)item->xwin, (unsigned)img->depth, bits_per_pixel, stride, len, (unsigned)w, (unsigned)h);
            fflush(stderr);
        }
        free(img);
        free(geom);
        return false;
    }

    const size_t need = (size_t)w * (size_t)h * 4;
    uint8_t *argb = malloc(need);
    if (argb == NULL) {
        if (g_debug) {
            fprintf(stderr, "xembed-sni-proxy: malloc failed need=%zu\n", need);
            fflush(stderr);
        }
        free(img);
        free(geom);
        return false;
    }

    bool any = false;
    for (uint16_t y = 0; y < h; y++) {
        const uint8_t *row = data + (size_t)y * stride;
        for (uint16_t x = 0; x < w; x++) {
            uint32_t pix = 0;
            const uint8_t *src = row + (size_t)x * bytes_per_pixel;
            if (bytes_per_pixel >= 4) {
                memcpy(&pix, src, sizeof(pix));
            } else {
                // LSB-first.
                for (size_t i = 0; i < bytes_per_pixel; i++) {
                    pix |= (uint32_t)src[i] << (8 * i);
                }
            }
            const uint32_t rr = (pix & red_mask) >> r_shift;
            const uint32_t gg = (pix & green_mask) >> g_shift;
            const uint32_t bb = (pix & blue_mask) >> b_shift;
            const uint8_t r8 = scale_to_u8(rr, r_max);
            const uint8_t g8 = scale_to_u8(gg, g_max);
            const uint8_t b8 = scale_to_u8(bb, b_max);

            const size_t off = ((size_t)y * (size_t)w + (size_t)x) * 4;
            argb[off + 0] = 0xff;
            argb[off + 1] = r8;
            argb[off + 2] = g8;
            argb[off + 3] = b8;
            any = true;
        }
    }

    free(img);
    free(geom);

    if (!any) {
        free(argb);
        return false;
    }

    // Only update + emit if changed.
    if (item->icon_len == need && item->icon_argb != NULL && memcmp(item->icon_argb, argb, need) == 0) {
        free(argb);
        return false;
    }

    free(item->icon_argb);
    item->icon_argb = argb;
    item->icon_len = need;
    item->icon_w = (int)w;
    item->icon_h = (int)h;
    return true;
}

static void proxy_mark_dirty(struct xembed_item *item, uint64_t now, uint64_t delay_ms) {
    if (item == NULL) {
        return;
    }
    item->dirty = true;
    item->dirty_after_ms = now + delay_ms;
}

static void proxy_handle_dock(struct proxy *p, xcb_window_t win) {
    if (p == NULL || p->xconn == NULL || p->bus == NULL) {
        return;
    }

    if (proxy_item_find(p, win) != NULL) {
        return;
    }

    struct xembed_item *item = calloc(1, sizeof(*item));
    if (item == NULL) {
        return;
    }
    item->xwin = win;
    item->status = "Active";

    char path_buf[128];
    snprintf(path_buf, sizeof(path_buf), "/StatusNotifierItem/xembed0x%08x", (unsigned)win);
    item->path = strdup(path_buf);

    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "xembed-0x%08x", (unsigned)win);
    item->item_id = strdup(id_buf);

    if (item->path == NULL || item->item_id == NULL) {
        proxy_item_destroy(p, item);
        return;
    }

    // Best-effort: make the icon behave like a proper XEmbed tray client.
    const uint32_t icon_size = 16;
    const uint32_t cfg_vals[] = {icon_size, icon_size};
    xcb_configure_window(p->xconn, win,
        XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, cfg_vals);

    const uint32_t evmask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(p->xconn, win, XCB_CW_EVENT_MASK, &evmask);

    xcb_reparent_window(p->xconn, win, p->mgr_win, 0, 0);
    xcb_map_window(p->xconn, win);

    // Redirect so that we can capture the window contents via XComposite.
    {
        xcb_void_cookie_t red_cookie =
            xcb_composite_redirect_window_checked(p->xconn, win, XCB_COMPOSITE_REDIRECT_AUTOMATIC);
        xcb_generic_error_t *red_err = xcb_request_check(p->xconn, red_cookie);
        if (red_err != NULL) {
            if (g_debug) {
                fprintf(stderr, "xembed-sni-proxy: redirect_window failed win=0x%08x err=%u\n",
                    (unsigned)win, (unsigned)red_err->error_code);
                fflush(stderr);
            }
            free(red_err);
        }
    }

    // XEMBED_EMBEDDED_NOTIFY
    enum { XEMBED_EMBEDDED_NOTIFY = 0, XEMBED_VERSION = 0 };
    xcb_client_message_event_t msg = {0};
    msg.response_type = XCB_CLIENT_MESSAGE;
    msg.format = 32;
    msg.window = win;
    msg.type = p->atom_xembed;
    msg.data.data32[0] = XCB_CURRENT_TIME;
    msg.data.data32[1] = XEMBED_EMBEDDED_NOTIFY;
    msg.data.data32[2] = 0;
    msg.data.data32[3] = p->mgr_win;
    msg.data.data32[4] = XEMBED_VERSION;
    xcb_send_event(p->xconn, false, win, XCB_EVENT_MASK_NO_EVENT, (const char *)&msg);

    xcb_flush(p->xconn);

    bool captured = false;
    for (int attempt = 0; attempt < 10 && !g_stop; attempt++) {
        if (proxy_capture_icon(p, item)) {
            captured = true;
            break;
        }
        usleep(20 * 1000);
    }

    if (!proxy_register_item(p, item)) {
        proxy_item_destroy(p, item);
        return;
    }

    item->next = p->items;
    p->items = item;

    if (captured) {
        if (g_debug && item->icon_argb != NULL && item->icon_len >= 4) {
            fprintf(stderr, "xembed-sni-proxy: captured win=0x%08x %dx%d argb0=%02x%02x%02x%02x\n",
                (unsigned)win, item->icon_w, item->icon_h,
                item->icon_argb[0], item->icon_argb[1], item->icon_argb[2], item->icon_argb[3]);
            fflush(stderr);
        }
        proxy_item_emit_icon_changed(p, item);
    } else {
        if (g_debug) {
            fprintf(stderr, "xembed-sni-proxy: capture failed win=0x%08x\n", (unsigned)win);
            fflush(stderr);
        }
        proxy_mark_dirty(item, now_ms(), 120);
    }
}

static void proxy_handle_x_event(struct proxy *p, xcb_generic_event_t *ev) {
    if (p == NULL || ev == NULL) {
        return;
    }

    const uint8_t rt = ev->response_type & ~0x80;
    switch (rt) {
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *cm = (xcb_client_message_event_t *)ev;
        if (cm->window == p->mgr_win && cm->type == p->atom_systray_opcode) {
            const uint32_t opcode = cm->data.data32[1];
            if (opcode == 0) { // SYSTEM_TRAY_REQUEST_DOCK
                const xcb_window_t win = (xcb_window_t)cm->data.data32[2];
                proxy_handle_dock(p, win);
            }
        }
    } break;
    case XCB_EXPOSE: {
        xcb_expose_event_t *ex = (xcb_expose_event_t *)ev;
        struct xembed_item *item = proxy_item_find(p, ex->window);
        if (item != NULL) {
            proxy_mark_dirty(item, now_ms(), 40);
        }
    } break;
    case XCB_CONFIGURE_NOTIFY: {
        xcb_configure_notify_event_t *ce = (xcb_configure_notify_event_t *)ev;
        struct xembed_item *item = proxy_item_find(p, ce->window);
        if (item != NULL) {
            proxy_mark_dirty(item, now_ms(), 40);
        }
    } break;
    case XCB_DESTROY_NOTIFY: {
        xcb_destroy_notify_event_t *de = (xcb_destroy_notify_event_t *)ev;
        proxy_item_remove(p, de->window);
    } break;
    case XCB_SELECTION_CLEAR:
        // Lost tray manager selection - exit.
        g_stop = 1;
        break;
    default:
        break;
    }
}

static bool proxy_start_xembed(struct proxy *p) {
    if (p == NULL || p->xconn == NULL || p->xscreen == NULL) {
        return false;
    }

    char sel_name[64];
    snprintf(sel_name, sizeof(sel_name), "_NET_SYSTEM_TRAY_S%d", 0);
    p->atom_systray_sel = intern_atom(p->xconn, sel_name);
    p->atom_systray_opcode = intern_atom(p->xconn, "_NET_SYSTEM_TRAY_OPCODE");
    p->atom_manager = intern_atom(p->xconn, "MANAGER");
    p->atom_xembed = intern_atom(p->xconn, "_XEMBED");

    if (p->atom_systray_sel == XCB_ATOM_NONE || p->atom_systray_opcode == XCB_ATOM_NONE ||
            p->atom_manager == XCB_ATOM_NONE || p->atom_xembed == XCB_ATOM_NONE) {
        fprintf(stderr, "xembed-sni-proxy: missing required X11 atoms\n");
        return false;
    }

    p->mgr_win = xcb_generate_id(p->xconn);
    const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY};
    xcb_create_window(p->xconn,
        XCB_COPY_FROM_PARENT,
        p->mgr_win,
        p->xscreen->root,
        0,
        0,
        1,
        1,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        p->xscreen->root_visual,
        XCB_CW_EVENT_MASK,
        values);

    xcb_map_window(p->xconn, p->mgr_win);
    xcb_set_selection_owner(p->xconn, p->mgr_win, p->atom_systray_sel, XCB_CURRENT_TIME);
    xcb_flush(p->xconn);

    xcb_get_selection_owner_cookie_t owner_cookie = xcb_get_selection_owner(p->xconn, p->atom_systray_sel);
    xcb_get_selection_owner_reply_t *owner_reply = xcb_get_selection_owner_reply(p->xconn, owner_cookie, NULL);
    const xcb_window_t owner = owner_reply != NULL ? owner_reply->owner : XCB_WINDOW_NONE;
    free(owner_reply);
    if (owner != p->mgr_win) {
        fprintf(stderr, "xembed-sni-proxy: failed to become tray selection owner (%s)\n", sel_name);
        return false;
    }

    // Notify clients that we are the tray manager.
    xcb_client_message_event_t man = {0};
    man.response_type = XCB_CLIENT_MESSAGE;
    man.format = 32;
    man.window = p->xscreen->root;
    man.type = p->atom_manager;
    man.data.data32[0] = XCB_CURRENT_TIME;
    man.data.data32[1] = p->atom_systray_sel;
    man.data.data32[2] = p->mgr_win;
    man.data.data32[3] = 0;
    man.data.data32[4] = 0;
    xcb_send_event(p->xconn, false, p->xscreen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&man);
    xcb_flush(p->xconn);

    return true;
}

int main(void) {
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    g_debug = env_is_truthy("FBWL_XEMBED_SNI_PROXY_DEBUG");

    struct proxy p = {0};
    p.watcher_name = getenv("SNI_WATCHER");
    if (p.watcher_name == NULL || *p.watcher_name == '\0') {
        p.watcher_name = "org.kde.StatusNotifierWatcher";
    }

    int screen_nbr = 0;
    p.xconn = xcb_connect(NULL, &screen_nbr);
    if (p.xconn == NULL || xcb_connection_has_error(p.xconn)) {
        fprintf(stderr, "xembed-sni-proxy: xcb_connect failed (DISPLAY=%s)\n",
            getenv("DISPLAY") != NULL ? getenv("DISPLAY") : "(unset)");
        if (p.xconn != NULL) {
            xcb_disconnect(p.xconn);
        }
        return 1;
    }
    p.xfd = xcb_get_file_descriptor(p.xconn);

    const xcb_setup_t *setup = xcb_get_setup(p.xconn);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_nbr && it.rem > 0; i++) {
        xcb_screen_next(&it);
    }
    p.xscreen = it.data;
    if (p.xscreen == NULL) {
        fprintf(stderr, "xembed-sni-proxy: failed to get X11 screen\n");
        xcb_disconnect(p.xconn);
        return 1;
    }

    int r = sd_bus_open_user(&p.bus);
    if (r < 0) {
        fprintf(stderr, "xembed-sni-proxy: sd_bus_open_user failed: %s\n", strerror(-r));
        xcb_disconnect(p.xconn);
        return 1;
    }

    if (!proxy_start_xembed(&p)) {
        sd_bus_unref(p.bus);
        xcb_disconnect(p.xconn);
        return 1;
    }

    while (!g_stop) {
        // Drain X events.
        for (;;) {
            xcb_generic_event_t *ev = xcb_poll_for_event(p.xconn);
            if (ev == NULL) {
                break;
            }
            proxy_handle_x_event(&p, ev);
            free(ev);
        }

        // Drain DBus messages.
        for (;;) {
            r = sd_bus_process(p.bus, NULL);
            if (r < 0 || r == 0) {
                break;
            }
        }

        const uint64_t now = now_ms();
        uint64_t next_dirty_ms = UINT64_MAX;
        for (struct xembed_item *item = p.items; item != NULL; item = item->next) {
            if (!item->dirty) {
                continue;
            }
            if (now < item->dirty_after_ms) {
                if (item->dirty_after_ms < next_dirty_ms) {
                    next_dirty_ms = item->dirty_after_ms;
                }
                continue;
            }

            item->dirty = false;
            item->dirty_after_ms = 0;
            if (proxy_capture_icon(&p, item)) {
                proxy_item_emit_icon_changed(&p, item);
            }
        }

        int timeout_ms = 250;
        if (next_dirty_ms != UINT64_MAX && next_dirty_ms > now) {
            const uint64_t wait_ms = next_dirty_ms - now;
            timeout_ms = wait_ms > 250 ? 250 : (int)wait_ms;
        }

        const int bus_fd = sd_bus_get_fd(p.bus);
        const int bus_events = sd_bus_get_events(p.bus);

        struct pollfd pfds[2];
        nfds_t nfds = 0;
        if (bus_fd >= 0) {
            pfds[nfds++] = (struct pollfd){.fd = bus_fd, .events = (short)bus_events};
        }
        if (p.xfd >= 0) {
            pfds[nfds++] = (struct pollfd){.fd = p.xfd, .events = POLLIN};
        }
        if (nfds > 0) {
            (void)poll(pfds, nfds, timeout_ms);
        } else {
            usleep((useconds_t)timeout_ms * 1000U);
        }

        if (xcb_connection_has_error(p.xconn)) {
            break;
        }
    }

    while (p.items != NULL) {
        struct xembed_item *next = p.items->next;
        proxy_item_destroy(&p, p.items);
        p.items = next;
    }

    sd_bus_flush_close_unref(p.bus);
    xcb_disconnect(p.xconn);
    return 0;
}
