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
    fprintf(stderr, "Usage: %s [--display DISPLAY] [--title TITLE] [--class CLASS] [--instance INSTANCE] [--stay-ms MS] [--w W] [--h H]\n",
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
    int stay_ms = 2000;
    uint16_t win_w = 128;
    uint16_t win_h = 96;

    static const struct option options[] = {
        {"display", required_argument, NULL, 1},
        {"title", required_argument, NULL, 2},
        {"class", required_argument, NULL, 3},
        {"instance", required_argument, NULL, 4},
        {"stay-ms", required_argument, NULL, 5},
        {"w", required_argument, NULL, 6},
        {"h", required_argument, NULL, 7},
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
        case 'h':
        default:
            usage(argv[0]);
            return (c == 'h') ? 0 : 2;
        }
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

    const int xfd = xcb_get_file_descriptor(conn);
    const uint64_t deadline = now_ms() + (uint64_t)stay_ms;

    while (now_ms() < deadline) {
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
