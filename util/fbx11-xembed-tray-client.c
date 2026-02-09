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

static bool parse_rgb24(const char *s, uint16_t *r, uint16_t *g, uint16_t *b) {
    if (s == NULL || r == NULL || g == NULL || b == NULL) {
        return false;
    }
    if (strlen(s) != 7 || s[0] != '#') {
        return false;
    }

    char tmp[3] = {0};
    tmp[0] = s[1];
    tmp[1] = s[2];
    long rr = strtol(tmp, NULL, 16);
    tmp[0] = s[3];
    tmp[1] = s[4];
    long gg = strtol(tmp, NULL, 16);
    tmp[0] = s[5];
    tmp[1] = s[6];
    long bb = strtol(tmp, NULL, 16);

    if (rr < 0 || rr > 255 || gg < 0 || gg > 255 || bb < 0 || bb > 255) {
        return false;
    }

    *r = (uint16_t)(rr * 257);
    *g = (uint16_t)(gg * 257);
    *b = (uint16_t)(bb * 257);
    return true;
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
    fprintf(stderr, "Usage: %s [--display DISPLAY] [--stay-ms MS] [--title TITLE] [--rgb #RRGGBB] [--w W] [--h H]\n",
        argv0);
}

static xcb_window_t wait_for_systray_owner(xcb_connection_t *conn, xcb_atom_t sel, int timeout_ms) {
    if (conn == NULL || sel == XCB_ATOM_NONE) {
        return XCB_WINDOW_NONE;
    }

    const uint64_t end = now_ms() + (timeout_ms > 0 ? (uint64_t)timeout_ms : 0);
    while (timeout_ms <= 0 || now_ms() <= end) {
        xcb_get_selection_owner_cookie_t cookie = xcb_get_selection_owner(conn, sel);
        xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(conn, cookie, NULL);
        if (reply != NULL) {
            xcb_window_t owner = reply->owner;
            free(reply);
            if (owner != XCB_WINDOW_NONE) {
                return owner;
            }
        }
        usleep(50 * 1000);
    }

    return XCB_WINDOW_NONE;
}

static void draw_solid(xcb_connection_t *conn, xcb_drawable_t drawable, xcb_gcontext_t gc,
        uint16_t width, uint16_t height) {
    xcb_rectangle_t rect = {0, 0, width, height};
    xcb_poly_fill_rectangle(conn, drawable, gc, 1, &rect);
}

int main(int argc, char **argv) {
    const char *display_name = NULL;
    int stay_ms = 8000;
    const char *title = "fbx11-xembed-tray";
    const char *rgb = "#00ff00";
    uint16_t win_w = 16;
    uint16_t win_h = 16;

    static const struct option options[] = {
        {"display", required_argument, NULL, 1},
        {"stay-ms", required_argument, NULL, 2},
        {"title", required_argument, NULL, 3},
        {"rgb", required_argument, NULL, 4},
        {"w", required_argument, NULL, 5},
        {"h", required_argument, NULL, 6},
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
            stay_ms = atoi(optarg);
            if (stay_ms < 0) {
                stay_ms = 0;
            }
            break;
        case 3:
            title = optarg;
            break;
        case 4:
            rgb = optarg;
            break;
        case 5:
            win_w = (uint16_t)atoi(optarg);
            if (win_w < 1) {
                win_w = 1;
            }
            break;
        case 6:
            win_h = (uint16_t)atoi(optarg);
            if (win_h < 1) {
                win_h = 1;
            }
            break;
        case 'h':
        default:
            usage(argv[0]);
            return (c == 'h') ? 0 : 2;
        }
    }

    int screen_nbr = 0;
    xcb_connection_t *conn = xcb_connect(display_name, &screen_nbr);
    if (conn == NULL || xcb_connection_has_error(conn)) {
        fprintf(stderr, "fbx11-xembed-tray-client: xcb_connect failed (DISPLAY=%s)\n",
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
        fprintf(stderr, "fbx11-xembed-tray-client: failed to get X11 screen\n");
        xcb_disconnect(conn);
        return 1;
    }

    char sel_name[64];
    snprintf(sel_name, sizeof(sel_name), "_NET_SYSTEM_TRAY_S%d", screen_nbr);
    xcb_atom_t atom_systray_sel = intern_atom(conn, sel_name);
    xcb_atom_t atom_systray_opcode = intern_atom(conn, "_NET_SYSTEM_TRAY_OPCODE");
    xcb_atom_t atom_xembed_info = intern_atom(conn, "_XEMBED_INFO");

    if (atom_systray_sel == XCB_ATOM_NONE || atom_systray_opcode == XCB_ATOM_NONE || atom_xembed_info == XCB_ATOM_NONE) {
        fprintf(stderr, "fbx11-xembed-tray-client: missing required atoms\n");
        xcb_disconnect(conn);
        return 1;
    }

    xcb_window_t owner = wait_for_systray_owner(conn, atom_systray_sel, 5000);
    if (owner == XCB_WINDOW_NONE) {
        fprintf(stderr, "fbx11-xembed-tray-client: system tray selection owner not found (%s)\n", sel_name);
        xcb_disconnect(conn);
        return 1;
    }

    uint16_t r16 = 0, g16 = 0, b16 = 0;
    if (!parse_rgb24(rgb, &r16, &g16, &b16)) {
        fprintf(stderr, "fbx11-xembed-tray-client: invalid --rgb (expected #RRGGBB): %s\n", rgb);
        xcb_disconnect(conn);
        return 1;
    }

    const uint32_t fg = alloc_color_or_fallback(conn, screen->default_colormap, r16, g16, b16, screen->white_pixel);
    const uint32_t bg = screen->black_pixel;

    xcb_window_t win = xcb_generate_id(conn);
    uint32_t values[] = {
        bg,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS,
        1, // override_redirect
    };
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_OVERRIDE_REDIRECT;

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

    xcb_icccm_set_wm_name(conn, win, XCB_ATOM_STRING, 8, (uint32_t)strlen(title), title);

    // WM_CLASS: instance\0class\0
    const size_t title_len = strlen(title);
    const size_t class_len = title_len + 1 + title_len + 1;
    char *class_data = calloc(1, class_len);
    if (class_data != NULL) {
        memcpy(class_data, title, title_len);
        memcpy(class_data + title_len + 1, title, title_len);
        xcb_icccm_set_wm_class(conn, win, (uint32_t)class_len, class_data);
        free(class_data);
    }

    // XEMBED_INFO: [version, flags]. Set mapped flag so tray managers don't ignore it.
    const uint32_t xembed_info[2] = {0, 1};
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, atom_xembed_info, atom_xembed_info, 32, 2, xembed_info);

    xcb_map_window(conn, win);
    xcb_flush(conn);

    // Request docking: send _NET_SYSTEM_TRAY_OPCODE to the manager window.
    xcb_client_message_event_t dock = {0};
    dock.response_type = XCB_CLIENT_MESSAGE;
    dock.format = 32;
    dock.window = owner;
    dock.type = atom_systray_opcode;
    dock.data.data32[0] = XCB_CURRENT_TIME;
    dock.data.data32[1] = 0; // SYSTEM_TRAY_REQUEST_DOCK
    dock.data.data32[2] = win;
    dock.data.data32[3] = 0;
    dock.data.data32[4] = 0;
    xcb_send_event(conn, false, owner, XCB_EVENT_MASK_NO_EVENT, (const char *)&dock);
    xcb_flush(conn);

    xcb_gcontext_t gc = xcb_generate_id(conn);
    uint32_t gcv[] = {fg};
    xcb_create_gc(conn, gc, win, XCB_GC_FOREGROUND, gcv);

    uint16_t cur_w = win_w;
    uint16_t cur_h = win_h;
    draw_solid(conn, win, gc, cur_w, cur_h);
    xcb_flush(conn);

    const uint64_t end = now_ms() + (stay_ms > 0 ? (uint64_t)stay_ms : 0);
    int fd = xcb_get_file_descriptor(conn);
    while (stay_ms <= 0 || now_ms() <= end) {
        int timeout_ms = 200;
        if (stay_ms > 0) {
            const uint64_t now = now_ms();
            if (now >= end) {
                break;
            }
            const uint64_t rem = end - now;
            timeout_ms = rem > 200 ? 200 : (int)rem;
        }

        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbx11-xembed-tray-client: poll failed: %s\n", strerror(errno));
            break;
        }

        xcb_generic_event_t *ev;
        while ((ev = xcb_poll_for_event(conn)) != NULL) {
            const uint8_t rt = ev->response_type & ~0x80;
            switch (rt) {
            case XCB_EXPOSE:
                draw_solid(conn, win, gc, cur_w, cur_h);
                xcb_flush(conn);
                break;
            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t *ce = (xcb_configure_notify_event_t *)ev;
                if (ce->width > 0 && ce->height > 0) {
                    cur_w = ce->width;
                    cur_h = ce->height;
                    draw_solid(conn, win, gc, cur_w, cur_h);
                    xcb_flush(conn);
                }
            } break;
            case XCB_BUTTON_PRESS:
                // No-op: this is a tray icon; interactions are compositor/proxy-specific.
                break;
            default:
                break;
            }
            free(ev);
        }
    }

    xcb_disconnect(conn);
    return 0;
}
