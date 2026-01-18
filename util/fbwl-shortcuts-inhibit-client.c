#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct zwp_keyboard_shortcuts_inhibit_manager_v1 *inhibit_mgr;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor;

    bool configured;
    bool inhibit_active;
    bool sync_done;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-shortcuts-inhibit-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-shortcuts-inhibit-client-shm-XXXXXX";
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
    memset(data, 0x80, size);

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

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *wm_base,
        uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

static void inhibitor_handle_active(void *data,
        struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor) {
    (void)inhibitor;
    struct fbwl_app *app = data;
    app->inhibit_active = true;
    printf("ok shortcuts-inhibit active\n");
    fflush(stdout);
}

static void inhibitor_handle_inactive(void *data,
        struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor) {
    (void)data;
    (void)inhibitor;
}

static const struct zwp_keyboard_shortcuts_inhibitor_v1_listener inhibitor_listener = {
    .active = inhibitor_handle_active,
    .inactive = inhibitor_handle_inactive,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, v);
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
        uint32_t v = version < 7 ? version : 7;
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, v);
        return;
    }
    if (strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) == 0) {
        app->inhibit_mgr = wl_registry_bind(registry, name,
            &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
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
    struct fbwl_app *app = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    app->configured = true;

    int width = app->pending_width > 0 ? app->pending_width : app->current_width;
    int height = app->pending_height > 0 ? app->pending_height : app->current_height;
    if (width <= 0) {
        width = 64;
    }
    if (height <= 0) {
        height = 64;
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
    struct fbwl_app *app = data;
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

static void sync_callback_done(void *data, struct wl_callback *callback, uint32_t callback_data) {
    (void)callback_data;
    struct fbwl_app *app = data;
    app->sync_done = true;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
    .done = sync_callback_done,
};

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--stay-ms MS] [--title TITLE]\n", argv0);
}

static void cleanup(struct fbwl_app *app) {
    if (app->inhibitor != NULL) {
        zwp_keyboard_shortcuts_inhibitor_v1_destroy(app->inhibitor);
        app->inhibitor = NULL;
    }
    if (app->buffer != NULL) {
        wl_buffer_destroy(app->buffer);
        app->buffer = NULL;
    }
    if (app->xdg_toplevel != NULL) {
        xdg_toplevel_destroy(app->xdg_toplevel);
        app->xdg_toplevel = NULL;
    }
    if (app->xdg_surface != NULL) {
        xdg_surface_destroy(app->xdg_surface);
        app->xdg_surface = NULL;
    }
    if (app->surface != NULL) {
        wl_surface_destroy(app->surface);
        app->surface = NULL;
    }
    if (app->inhibit_mgr != NULL) {
        zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(app->inhibit_mgr);
        app->inhibit_mgr = NULL;
    }
    if (app->seat != NULL) {
        wl_seat_destroy(app->seat);
        app->seat = NULL;
    }
    if (app->wm_base != NULL) {
        xdg_wm_base_destroy(app->wm_base);
        app->wm_base = NULL;
    }
    if (app->shm != NULL) {
        wl_shm_destroy(app->shm);
        app->shm = NULL;
    }
    if (app->compositor != NULL) {
        wl_compositor_destroy(app->compositor);
        app->compositor = NULL;
    }
    if (app->registry != NULL) {
        wl_registry_destroy(app->registry);
        app->registry = NULL;
    }
    if (app->display != NULL) {
        wl_display_disconnect(app->display);
        app->display = NULL;
    }
}

static bool pump_until(struct fbwl_app *app, int64_t deadline_ms, bool (*done)(struct fbwl_app *app)) {
    while (now_ms() < deadline_ms) {
        wl_display_dispatch_pending(app->display);
        wl_display_flush(app->display);
        if (done(app)) {
            return true;
        }

        int remaining = (int)(deadline_ms - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {
            .fd = wl_display_get_fd(app->display),
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (ret == 0) {
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            return false;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app->display) < 0) {
                return false;
            }
        }
    }
    return done(app);
}

static void pump_for(struct fbwl_app *app, int64_t deadline_ms) {
    while (now_ms() < deadline_ms) {
        wl_display_dispatch_pending(app->display);
        wl_display_flush(app->display);

        int remaining = (int)(deadline_ms - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {
            .fd = wl_display_get_fd(app->display),
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0 && errno == EINTR) {
            continue;
        }
        if (ret <= 0) {
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            return;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app->display) < 0) {
                return;
            }
        }
    }
}

static bool want_active(struct fbwl_app *app) {
    return app->configured && app->inhibit_active;
}

static bool want_sync(struct fbwl_app *app) {
    return app->sync_done;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 4000;
    int stay_ms = 8000;
    const char *title = "shortcuts-inhibit";

    static const struct option opts[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"stay-ms", required_argument, NULL, 3},
        {"title", required_argument, NULL, 4},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
        switch (opt) {
        case 1:
            socket_name = optarg;
            break;
        case 2:
            timeout_ms = atoi(optarg);
            break;
        case 3:
            stay_ms = atoi(optarg);
            break;
        case 4:
            title = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 2;
        }
    }

    struct fbwl_app app = {0};
    app.current_width = 64;
    app.current_height = 64;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-shortcuts-inhibit-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(null)", strerror(errno));
        cleanup(&app);
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL ||
            app.seat == NULL || app.inhibit_mgr == NULL) {
        fprintf(stderr,
            "fbwl-shortcuts-inhibit-client: missing required globals (compositor=%p shm=%p wm_base=%p seat=%p inhibit_mgr=%p)\n",
            (void *)app.compositor, (void *)app.shm, (void *)app.wm_base,
            (void *)app.seat, (void *)app.inhibit_mgr);
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    if (app.surface == NULL) {
        fprintf(stderr, "fbwl-shortcuts-inhibit-client: wl_compositor_create_surface failed\n");
        cleanup(&app);
        return 1;
    }
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_set_title(app.xdg_toplevel, title);

    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);

    app.inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
        app.inhibit_mgr, app.surface, app.seat);
    if (app.inhibitor == NULL) {
        fprintf(stderr, "fbwl-shortcuts-inhibit-client: inhibit_shortcuts failed\n");
        cleanup(&app);
        return 1;
    }
    zwp_keyboard_shortcuts_inhibitor_v1_add_listener(app.inhibitor, &inhibitor_listener, &app);

    wl_surface_commit(app.surface);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    if (!pump_until(&app, deadline, want_active)) {
        fprintf(stderr, "fbwl-shortcuts-inhibit-client: timed out waiting for active (configured=%d active=%d)\n",
            (int)app.configured, (int)app.inhibit_active);
        cleanup(&app);
        return 1;
    }

    if (stay_ms > 0) {
        pump_for(&app, now_ms() + stay_ms);
    }

    struct wl_callback *cb = wl_display_sync(app.display);
    if (cb != NULL) {
        wl_callback_add_listener(cb, &sync_listener, &app);
        (void)pump_until(&app, now_ms() + timeout_ms, want_sync);
    }

    cleanup(&app);
    return 0;
}

