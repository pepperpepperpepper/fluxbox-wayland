#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "cursor-shape-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_cursor_shape_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;

    struct wl_seat *seat;
    struct wl_pointer *pointer;

    struct wp_cursor_shape_manager_v1 *cursor_shape_mgr;
    struct wp_cursor_shape_device_v1 *cursor_shape_device;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    bool configured;
    bool printed_ready;

    uint32_t enter_serial;
    bool have_enter_serial;
    bool set_shape_sent;

    uint32_t shape;
};

static const struct wl_seat_listener seat_listener;
static const struct wl_pointer_listener pointer_listener;

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-cursor-shape-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-cursor-shape-client-shm-XXXXXX";
    fd = mkstemp(template);
    if (fd < 0) {
        return -1;
    }
    unlink(template);
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static struct wl_buffer *create_shm_buffer(struct wl_shm *shm, int width, int height) {
    const int stride = width * 4;
    const size_t size = (size_t)stride * (size_t)height;

    int fd = create_shm_fd(size);
    if (fd < 0) {
        return NULL;
    }

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    memset(data, 0xff, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)size);
    close(fd);
    munmap(data, size);
    if (pool == NULL) {
        return NULL;
    }

    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
        width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return buffer;
}

static void try_setup_cursor_shape(struct fbwl_cursor_shape_app *app) {
    if (app == NULL || app->cursor_shape_mgr == NULL || app->pointer == NULL) {
        return;
    }
    if (app->cursor_shape_device != NULL) {
        return;
    }

    app->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(app->cursor_shape_mgr, app->pointer);
}

static void try_send_set_shape(struct fbwl_cursor_shape_app *app) {
    if (app == NULL || app->set_shape_sent) {
        return;
    }
    if (app->cursor_shape_device == NULL || !app->have_enter_serial) {
        return;
    }

    wp_cursor_shape_device_v1_set_shape(app->cursor_shape_device, app->enter_serial, app->shape);
    wl_display_flush(app->display);
    app->set_shape_sent = true;
    printf("ok set_shape_sent\n");
    fflush(stdout);
}

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *wm_base,
        uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct fbwl_cursor_shape_app *app = data;

    if ((caps & WL_SEAT_CAPABILITY_POINTER) != 0) {
        if (app->pointer == NULL) {
            app->pointer = wl_seat_get_pointer(seat);
            if (app->pointer != NULL) {
                wl_pointer_add_listener(app->pointer, &pointer_listener, app);
            }
            try_setup_cursor_shape(app);
        }
    } else if (app->pointer != NULL) {
        wl_pointer_destroy(app->pointer);
        app->pointer = NULL;
    }
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer;
    (void)surface;
    (void)sx;
    (void)sy;
    struct fbwl_cursor_shape_app *app = data;
    app->enter_serial = serial;
    app->have_enter_serial = true;
    try_send_set_shape(app);
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *surface) {
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
        wl_fixed_t sx, wl_fixed_t sy) {
    (void)data;
    (void)pointer;
    (void)time;
    (void)sx;
    (void)sy;
}

static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
        uint32_t time, uint32_t button, uint32_t state) {
    (void)data;
    (void)pointer;
    (void)serial;
    (void)time;
    (void)button;
    (void)state;
}

static void pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time,
        uint32_t axis, wl_fixed_t value) {
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static void pointer_handle_frame(void *data, struct wl_pointer *pointer) {
    (void)data;
    (void)pointer;
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
        uint32_t axis) {
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
        int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_cursor_shape_app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        app->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, v);
        return;
    }
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        return;
    }
    if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, v);
        xdg_wm_base_add_listener(app->wm_base, &xdg_wm_base_listener, app);
        return;
    }
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t v = version < 5 ? version : 5;
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, v);
        wl_seat_add_listener(app->seat, &seat_listener, app);
        return;
    }
    if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        uint32_t v = version < 2 ? version : 2;
        app->cursor_shape_mgr = wl_registry_bind(registry, name,
            &wp_cursor_shape_manager_v1_interface, v);
        try_setup_cursor_shape(app);
        return;
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
        uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
        uint32_t serial) {
    struct fbwl_cursor_shape_app *app = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    app->configured = true;

    int width = app->pending_width > 0 ? app->pending_width : app->current_width;
    int height = app->pending_height > 0 ? app->pending_height : app->current_height;
    if (width <= 0) {
        width = 32;
    }
    if (height <= 0) {
        height = 32;
    }

    if (app->buffer == NULL || width != app->current_width || height != app->current_height) {
        struct wl_buffer *buffer = create_shm_buffer(app->shm, width, height);
        if (buffer != NULL) {
            if (app->buffer != NULL) {
                wl_buffer_destroy(app->buffer);
            }
            app->buffer = buffer;
            app->current_width = width;
            app->current_height = height;

            wl_surface_attach(app->surface, app->buffer, 0, 0);
            wl_surface_damage(app->surface, 0, 0, width, height);
            wl_surface_commit(app->surface);
            wl_display_flush(app->display);
        }
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
        int32_t width, int32_t height, struct wl_array *states) {
    struct fbwl_cursor_shape_app *app = data;
    (void)xdg_toplevel;
    (void)states;
    app->pending_width = width;
    app->pending_height = height;
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    (void)data;
    (void)xdg_toplevel;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

static void cleanup(struct fbwl_cursor_shape_app *app) {
    if (app->cursor_shape_device != NULL) {
        wp_cursor_shape_device_v1_destroy(app->cursor_shape_device);
    }
    if (app->cursor_shape_mgr != NULL) {
        wp_cursor_shape_manager_v1_destroy(app->cursor_shape_mgr);
    }
    if (app->pointer != NULL) {
        wl_pointer_destroy(app->pointer);
    }
    if (app->seat != NULL) {
        wl_seat_destroy(app->seat);
    }
    if (app->buffer != NULL) {
        wl_buffer_destroy(app->buffer);
    }
    if (app->xdg_toplevel != NULL) {
        xdg_toplevel_destroy(app->xdg_toplevel);
    }
    if (app->xdg_surface != NULL) {
        xdg_surface_destroy(app->xdg_surface);
    }
    if (app->surface != NULL) {
        wl_surface_destroy(app->surface);
    }
    if (app->wm_base != NULL) {
        xdg_wm_base_destroy(app->wm_base);
    }
    if (app->shm != NULL) {
        wl_shm_destroy(app->shm);
    }
    if (app->compositor != NULL) {
        wl_compositor_destroy(app->compositor);
    }
    if (app->registry != NULL) {
        wl_registry_destroy(app->registry);
    }
    if (app->display != NULL) {
        wl_display_disconnect(app->display);
    }
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--socket NAME] [--timeout-ms MS] [--shape NAME]\n",
        argv0);
    fprintf(stderr, "  shape: default|text (default: text)\n");
}

static uint32_t parse_shape(const char *s) {
    if (s != NULL && strcmp(s, "default") == 0) {
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    }
    return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 5000;
    const char *shape_name = "text";

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"shape", required_argument, NULL, 3},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "h", options, NULL)) != -1) {
        switch (c) {
        case 1:
            socket_name = optarg;
            break;
        case 2:
            timeout_ms = atoi(optarg);
            break;
        case 3:
            shape_name = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }
    if (optind < argc) {
        usage(argv[0]);
        return 1;
    }

    struct fbwl_cursor_shape_app app = {0};
    app.shape = parse_shape(shape_name);
    app.current_width = 32;
    app.current_height = 32;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-cursor-shape-client: wl_display_connect failed\n");
        cleanup(&app);
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL ||
            app.seat == NULL || app.cursor_shape_mgr == NULL) {
        fprintf(stderr, "fbwl-cursor-shape-client: missing required globals\n");
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    if (app.surface == NULL) {
        fprintf(stderr, "fbwl-cursor-shape-client: wl_compositor_create_surface failed\n");
        cleanup(&app);
        return 1;
    }

    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    if (app.xdg_surface == NULL) {
        fprintf(stderr, "fbwl-cursor-shape-client: xdg_wm_base_get_xdg_surface failed\n");
        cleanup(&app);
        return 1;
    }
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.xdg_toplevel, "fbwl-cursor-shape");

    wl_surface_commit(app.surface);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    while (!app.set_shape_sent && now_ms() < deadline) {
        try_setup_cursor_shape(&app);
        if (app.configured && !app.printed_ready && app.cursor_shape_device != NULL && app.pointer != NULL) {
            printf("ok ready\n");
            fflush(stdout);
            app.printed_ready = true;
        }
        try_send_set_shape(&app);

        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        int remaining = (int)(deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {.fd = wl_display_get_fd(app.display), .events = POLLIN};
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-cursor-shape-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (ret == 0) {
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-cursor-shape-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-cursor-shape-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.set_shape_sent) {
        fprintf(stderr, "fbwl-cursor-shape-client: timed out waiting to set cursor shape\n");
        cleanup(&app);
        return 1;
    }

    if (wl_display_roundtrip(app.display) < 0) {
        fprintf(stderr, "fbwl-cursor-shape-client: wl_display_roundtrip failed: %s\n",
            strerror(errno));
        cleanup(&app);
        return 1;
    }

    cleanup(&app);
    return 0;
}
