#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static xcb_atom_t intern_atom(xcb_connection_t *conn, const char *name) {
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

static uint32_t alloc_color_or_fallback(xcb_connection_t *conn, xcb_colormap_t cmap,
        uint16_t r, uint16_t g, uint16_t b, uint32_t fallback_pixel) {
    xcb_alloc_color_cookie_t cookie = xcb_alloc_color(conn, cmap, r, g, b);
    xcb_alloc_color_reply_t *reply = xcb_alloc_color_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return fallback_pixel;
    }
    const uint32_t pixel = reply->pixel;
    free(reply);
    return pixel;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--display DISPLAY] [--title TITLE] [--class CLASS] [--instance INSTANCE] [--stay-ms MS] [--w W] [--h H] [--width-inc N] [--height-inc N] [--xprop NAME[=VALUE]] [--xprop-cardinal NAME=U32] [--urgent-after-ms MS] [--net-wm-icon] [--dock|--desktop|--splash] [--kde-dockapp]\n",
        argv0);
}

static void draw_solid(xcb_connection_t *conn, xcb_drawable_t drawable, xcb_gcontext_t gc,
        uint16_t width, uint16_t height) {
    xcb_rectangle_t rect = {0, 0, width, height};
    xcb_poly_fill_rectangle(conn, drawable, gc, 1, &rect);
}

int main(int argc, char **argv) {
    const char *display_name = NULL;
    const char *title = "fbx11-smoke";
    const char *class_name = "fbx11-smoke";
    const char *instance_name = "fbx11-smoke";
    const char *xprop_utf8 = NULL;
    const char *xprop_cardinal = NULL;
    int stay_ms = 2000;
    uint16_t win_w = 128;
    uint16_t win_h = 96;
    int32_t width_inc = 0;
    int32_t height_inc = 0;
    int urgent_after_ms = -1;
    bool set_net_wm_icon = false;
    bool set_dock = false;
    bool set_desktop = false;
    bool set_splash = false;
    bool set_kde_dockapp = false;

    static const struct option options[] = {
        {"display", required_argument, NULL, 1},
        {"title", required_argument, NULL, 2},
        {"class", required_argument, NULL, 3},
        {"instance", required_argument, NULL, 4},
        {"stay-ms", required_argument, NULL, 5},
        {"w", required_argument, NULL, 6},
        {"h", required_argument, NULL, 7},
        {"width-inc", required_argument, NULL, 8},
        {"height-inc", required_argument, NULL, 9},
        {"net-wm-icon", no_argument, NULL, 10},
        {"dock", no_argument, NULL, 11},
        {"kde-dockapp", no_argument, NULL, 12},
        {"desktop", no_argument, NULL, 13},
        {"splash", no_argument, NULL, 14},
        {"xprop", required_argument, NULL, 15},
        {"xprop-cardinal", required_argument, NULL, 16},
        {"urgent-after-ms", required_argument, NULL, 17},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "h", options, NULL)) != -1) {
        switch (c) {
        case 1:
            display_name = optarg;
            break;
        case 2:
            title = optarg;
            break;
        case 3:
            class_name = optarg;
            break;
        case 4:
            instance_name = optarg;
            break;
        case 5:
            stay_ms = atoi(optarg);
            if (stay_ms < 0) {
                stay_ms = 0;
            }
            break;
        case 6:
            win_w = (uint16_t)atoi(optarg);
            if (win_w < 1) {
                win_w = 1;
            }
            break;
        case 7:
            win_h = (uint16_t)atoi(optarg);
            if (win_h < 1) {
                win_h = 1;
            }
            break;
        case 8:
            width_inc = atoi(optarg);
            if (width_inc < 0) {
                width_inc = 0;
            }
            break;
        case 9:
            height_inc = atoi(optarg);
            if (height_inc < 0) {
                height_inc = 0;
            }
            break;
        case 10:
            set_net_wm_icon = true;
            break;
        case 11:
            set_dock = true;
            break;
        case 12:
            set_kde_dockapp = true;
            break;
        case 13:
            set_desktop = true;
            break;
        case 14:
            set_splash = true;
            break;
        case 15:
            xprop_utf8 = optarg;
            break;
        case 16:
            xprop_cardinal = optarg;
            break;
        case 17: {
            char *end = NULL;
            long v = strtol(optarg, &end, 10);
            if (end == optarg || end == NULL || *end != '\0' || v < 0 || v > 600000) {
                fprintf(stderr, "fbx11-smoke-client: invalid --urgent-after-ms: %s\n", optarg);
                return 2;
            }
            urgent_after_ms = (int)v;
            break;
        }
        case 'h':
        default:
            usage(argv[0]);
            return (c == 'h') ? 0 : 2;
        }
    }

    const int win_type_count = (set_dock ? 1 : 0) + (set_desktop ? 1 : 0) + (set_splash ? 1 : 0);
    if (win_type_count > 1) {
        fprintf(stderr, "fbx11-smoke-client: --dock/--desktop/--splash are mutually exclusive\n");
        return 2;
    }

    int screen_nbr = 0;
    xcb_connection_t *conn = xcb_connect(display_name, &screen_nbr);
    if (conn == NULL || xcb_connection_has_error(conn)) {
        fprintf(stderr, "fbx11-smoke-client: xcb_connect failed (DISPLAY=%s)\n",
            display_name != NULL ? display_name : "(env)");
        if (conn != NULL) {
            xcb_disconnect(conn);
        }
        return 1;
    }

    const xcb_setup_t *setup = xcb_get_setup(conn);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_nbr && it.rem > 0; i++) {
        xcb_screen_next(&it);
    }
    xcb_screen_t *screen = it.data;
    if (screen == NULL) {
        fprintf(stderr, "fbx11-smoke-client: failed to get X11 screen\n");
        xcb_disconnect(conn);
        return 1;
    }

    xcb_window_t win = xcb_generate_id(conn);

    uint32_t values[] = {
        screen->black_pixel,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
    };
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;

    xcb_create_window(conn,
        XCB_COPY_FROM_PARENT,
        win,
        screen->root,
        0, 0,
        win_w, win_h,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        mask, values);

    xcb_atom_t atom_wm_name = XCB_ATOM_WM_NAME;
    xcb_atom_t atom_wm_class = XCB_ATOM_WM_CLASS;
    xcb_atom_t atom_utf8 = intern_atom(conn, "UTF8_STRING");
    xcb_atom_t atom_net_wm_name = intern_atom(conn, "_NET_WM_NAME");

    /* WM_NAME (STRING) */
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom_wm_name, XCB_ATOM_STRING, 8,
        (uint32_t)strlen(title), title);

    /* _NET_WM_NAME (UTF8_STRING) */
    if (atom_net_wm_name != XCB_ATOM_NONE && atom_utf8 != XCB_ATOM_NONE) {
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom_net_wm_name, atom_utf8, 8,
            (uint32_t)strlen(title), title);
    }

    /* WM_CLASS: instance\0class\0 */
    const size_t inst_len = strlen(instance_name);
    const size_t class_len = strlen(class_name);
    const size_t class_data_len = inst_len + 1 + class_len + 1;
    char *class_data = calloc(1, class_data_len);
    if (class_data == NULL) {
        fprintf(stderr, "fbx11-smoke-client: calloc failed\n");
        xcb_disconnect(conn);
        return 1;
    }
    memcpy(class_data, instance_name, inst_len);
    memcpy(class_data + inst_len + 1, class_name, class_len);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom_wm_class, XCB_ATOM_STRING, 8,
        (uint32_t)class_data_len, class_data);
    free(class_data);

    if (width_inc > 0 || height_inc > 0) {
        xcb_size_hints_t hints = {0};
        xcb_icccm_size_hints_set_resize_inc(&hints,
            width_inc > 0 ? width_inc : 1,
            height_inc > 0 ? height_inc : 1);
        xcb_icccm_set_wm_normal_hints(conn, win, &hints);
    }

    if (set_dock || set_desktop || set_splash) {
        xcb_atom_t atom_net_wm_window_type = intern_atom(conn, "_NET_WM_WINDOW_TYPE");
        xcb_atom_t atom_type = XCB_ATOM_NONE;
        if (set_dock) {
            atom_type = intern_atom(conn, "_NET_WM_WINDOW_TYPE_DOCK");
        } else if (set_desktop) {
            atom_type = intern_atom(conn, "_NET_WM_WINDOW_TYPE_DESKTOP");
        } else if (set_splash) {
            atom_type = intern_atom(conn, "_NET_WM_WINDOW_TYPE_SPLASH");
        }
        if (atom_net_wm_window_type != XCB_ATOM_NONE && atom_type != XCB_ATOM_NONE) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom_net_wm_window_type,
                XCB_ATOM_ATOM, 32, 1, &atom_type);
        }
    }

    if (set_kde_dockapp) {
        xcb_atom_t atom_kwm_dockwindow = intern_atom(conn, "KWM_DOCKWINDOW");
        if (atom_kwm_dockwindow != XCB_ATOM_NONE) {
            uint32_t val = 1;
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom_kwm_dockwindow, atom_kwm_dockwindow, 32, 1, &val);
        }

        xcb_atom_t atom_kde_systray = intern_atom(conn, "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR");
        if (atom_kde_systray != XCB_ATOM_NONE) {
            xcb_window_t root = screen->root;
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom_kde_systray, XCB_ATOM_WINDOW, 32, 1, &root);
        }
    }

    if (set_net_wm_icon) {
        xcb_atom_t atom_net_wm_icon = intern_atom(conn, "_NET_WM_ICON");
        if (atom_net_wm_icon != XCB_ATOM_NONE) {
            enum { ICON_W = 16, ICON_H = 16 };
            uint32_t icon[2 + ICON_W * ICON_H];
            icon[0] = ICON_W;
            icon[1] = ICON_H;
            for (int y = 0; y < ICON_H; y++) {
                for (int x = 0; x < ICON_W; x++) {
                    const uint8_t r = (uint8_t)((x * 255) / (ICON_W - 1));
                    const uint8_t g = (uint8_t)((y * 255) / (ICON_H - 1));
                    const uint8_t b = 0x00;
                    icon[2 + y * ICON_W + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                }
            }
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom_net_wm_icon, XCB_ATOM_CARDINAL, 32,
                (uint32_t)(2 + ICON_W * ICON_H), icon);
        }
    }

    if (xprop_utf8 != NULL && atom_utf8 != XCB_ATOM_NONE) {
        const char *spec = xprop_utf8;
        const char *eq = strchr(spec, '=');
        const size_t name_len = eq != NULL ? (size_t)(eq - spec) : strlen(spec);
        const char *val = eq != NULL ? eq + 1 : "";
        if (name_len == 0) {
            fprintf(stderr, "fbx11-smoke-client: --xprop requires NAME[=VALUE]\n");
            xcb_disconnect(conn);
            return 2;
        }
        char *name = strndup(spec, name_len);
        if (name == NULL) {
            fprintf(stderr, "fbx11-smoke-client: strndup failed\n");
            xcb_disconnect(conn);
            return 1;
        }
        xcb_atom_t atom = intern_atom(conn, name);
        free(name);
        if (atom != XCB_ATOM_NONE) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom, atom_utf8, 8, (uint32_t)strlen(val), val);
        }
    }

    if (xprop_cardinal != NULL) {
        const char *spec = xprop_cardinal;
        const char *eq = strchr(spec, '=');
        if (eq == NULL || eq == spec || eq[1] == '\0') {
            fprintf(stderr, "fbx11-smoke-client: --xprop-cardinal requires NAME=U32\n");
            xcb_disconnect(conn);
            return 2;
        }
        char *name = strndup(spec, (size_t)(eq - spec));
        if (name == NULL) {
            fprintf(stderr, "fbx11-smoke-client: strndup failed\n");
            xcb_disconnect(conn);
            return 1;
        }
        errno = 0;
        char *end = NULL;
        unsigned long v = strtoul(eq + 1, &end, 0);
        if (errno != 0 || end == eq + 1 || (end != NULL && *end != '\0') || v > UINT32_MAX) {
            fprintf(stderr, "fbx11-smoke-client: invalid --xprop-cardinal value: %s\n", eq + 1);
            free(name);
            xcb_disconnect(conn);
            return 2;
        }
        const uint32_t u32 = (uint32_t)v;
        xcb_atom_t atom = intern_atom(conn, name);
        free(name);
        if (atom != XCB_ATOM_NONE) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom, XCB_ATOM_CARDINAL, 32, 1, &u32);
        }
    }

    xcb_map_window(conn, win);

    xcb_gcontext_t gc = xcb_generate_id(conn);
    // This client is used for XWayland smoke coverage. Avoid solid-white windows
    // so debug screenshots don't look like compositor glitches.
    const uint32_t fill_pixel = alloc_color_or_fallback(conn, screen->default_colormap,
        0x2222, 0x2222, 0x2222, screen->black_pixel);
    uint32_t gc_values[] = {fill_pixel};
    xcb_create_gc(conn, gc, win, XCB_GC_FOREGROUND, gc_values);

    draw_solid(conn, win, gc, win_w, win_h);
    xcb_flush(conn);

    bool urgent_set = false;
    const uint64_t map_time = now_ms();

    const int xfd = xcb_get_file_descriptor(conn);
    const uint64_t deadline = now_ms() + (uint64_t)stay_ms;

    while (now_ms() < deadline) {
        if (!urgent_set && urgent_after_ms >= 0 && now_ms() - map_time >= (uint64_t)urgent_after_ms) {
            xcb_icccm_wm_hints_t hints = {0};
            hints.flags = XCB_ICCCM_WM_HINT_X_URGENCY;
            xcb_icccm_set_wm_hints(conn, win, &hints);
            xcb_flush(conn);
            urgent_set = true;
        }

        int timeout = 10;
        const uint64_t remaining = deadline - now_ms();
        if (remaining < (uint64_t)timeout) {
            timeout = (int)remaining;
        }

        struct pollfd pfd = {.fd = xfd, .events = POLLIN};
        int pr = poll(&pfd, 1, timeout);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbx11-smoke-client: poll failed: %s\n", strerror(errno));
            break;
        }

        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(conn)) != NULL) {
            const uint8_t type = event->response_type & ~0x80;
            if (type == XCB_EXPOSE) {
                draw_solid(conn, win, gc, win_w, win_h);
                xcb_flush(conn);
            } else if (type == XCB_CONFIGURE_NOTIFY) {
                const xcb_configure_notify_event_t *cev = (const xcb_configure_notify_event_t *)event;
                if (cev->width != win_w || cev->height != win_h) {
                    win_w = cev->width;
                    win_h = cev->height;
                    draw_solid(conn, win, gc, win_w, win_h);
                    xcb_flush(conn);
                }
            }
            free(event);
        }
    }

    xcb_free_gc(conn, gc);
    xcb_destroy_window(conn, win);
    xcb_flush(conn);
    xcb_disconnect(conn);
    return 0;
}
