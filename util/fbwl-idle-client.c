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

#include "ext-idle-notify-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_idle_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;

    struct ext_idle_notifier_v1 *idle_notifier;
    struct ext_idle_notification_v1 *idle_notification;
    struct zwp_idle_inhibit_manager_v1 *idle_inhibit_mgr;
    struct zwp_idle_inhibitor_v1 *idle_inhibitor;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    bool configured;
    bool got_idled;
    bool got_resumed;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-idle-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-idle-client-shm-XXXXXX";
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
    memset(data, 0x90, size);

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

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
        uint32_t serial) {
    struct fbwl_idle_app *app = data;
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
        }
    }

    if (app->surface != NULL && app->buffer != NULL) {
        wl_surface_attach(app->surface, app->buffer, 0, 0);
        wl_surface_damage(app->surface, 0, 0, width, height);
        wl_surface_commit(app->surface);
    }
    wl_display_flush(app->display);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
        int32_t width, int32_t height, struct wl_array *states) {
    (void)xdg_toplevel;
    (void)states;
    struct fbwl_idle_app *app = data;
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

static void idle_notification_handle_idled(void *data,
        struct ext_idle_notification_v1 *ext_idle_notification_v1) {
    (void)ext_idle_notification_v1;
    struct fbwl_idle_app *app = data;
    app->got_idled = true;
}

static void idle_notification_handle_resumed(void *data,
        struct ext_idle_notification_v1 *ext_idle_notification_v1) {
    (void)ext_idle_notification_v1;
    struct fbwl_idle_app *app = data;
    app->got_resumed = true;
}

static const struct ext_idle_notification_v1_listener idle_notification_listener = {
    .idled = idle_notification_handle_idled,
    .resumed = idle_notification_handle_resumed,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_idle_app *app = data;
    (void)version;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
        return;
    }
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        return;
    }
    if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 2);
        xdg_wm_base_add_listener(app->wm_base, &xdg_wm_base_listener, app);
        return;
    }
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        return;
    }
    if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        app->idle_notifier = wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1);
        return;
    }
    if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
        app->idle_inhibit_mgr = wl_registry_bind(registry, name,
            &zwp_idle_inhibit_manager_v1_interface, 1);
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

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS]\n", argv0);
}

static void cleanup(struct fbwl_idle_app *app) {
    if (app->idle_inhibitor != NULL) {
        zwp_idle_inhibitor_v1_destroy(app->idle_inhibitor);
    }
    if (app->idle_notification != NULL) {
        ext_idle_notification_v1_destroy(app->idle_notification);
    }
    if (app->idle_inhibit_mgr != NULL) {
        zwp_idle_inhibit_manager_v1_destroy(app->idle_inhibit_mgr);
    }
    if (app->idle_notifier != NULL) {
        ext_idle_notifier_v1_destroy(app->idle_notifier);
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

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 4000;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
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

    struct fbwl_idle_app app = {0};
    app.current_width = 64;
    app.current_height = 64;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-idle-client: wl_display_connect failed\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL || app.seat == NULL ||
            app.idle_notifier == NULL || app.idle_inhibit_mgr == NULL) {
        fprintf(stderr, "fbwl-idle-client: missing required globals\n");
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    if (app.surface == NULL) {
        fprintf(stderr, "fbwl-idle-client: wl_compositor_create_surface failed\n");
        cleanup(&app);
        return 1;
    }
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    if (app.xdg_surface == NULL) {
        fprintf(stderr, "fbwl-idle-client: xdg_wm_base_get_xdg_surface failed\n");
        cleanup(&app);
        return 1;
    }
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.xdg_toplevel, "fbwl-idle-client");

    wl_surface_commit(app.surface);
    wl_display_flush(app.display);

    const int64_t configure_deadline = now_ms() + timeout_ms;
    while (!app.configured && now_ms() < configure_deadline) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        int remaining = (int)(configure_deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }

        struct pollfd pfd = {
            .fd = wl_display_get_fd(app.display),
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, remaining);
        if (ret == 0) {
            break;
        }
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-idle-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-idle-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-idle-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }
    if (!app.configured) {
        fprintf(stderr, "fbwl-idle-client: timed out waiting for xdg_surface.configure\n");
        cleanup(&app);
        return 1;
    }

    app.idle_notification = ext_idle_notifier_v1_get_idle_notification(app.idle_notifier,
        200, app.seat);
    if (app.idle_notification == NULL) {
        fprintf(stderr, "fbwl-idle-client: get_idle_notification failed\n");
        cleanup(&app);
        return 1;
    }
    ext_idle_notification_v1_add_listener(app.idle_notification, &idle_notification_listener, &app);

    app.idle_inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(app.idle_inhibit_mgr, app.surface);
    if (app.idle_inhibitor == NULL) {
        fprintf(stderr, "fbwl-idle-client: create_inhibitor failed\n");
        cleanup(&app);
        return 1;
    }
    wl_display_flush(app.display);

    const int64_t inhibited_until = now_ms() + 500;
    while (now_ms() < inhibited_until) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);
        if (app.got_idled) {
            fprintf(stderr, "fbwl-idle-client: unexpected idled event while inhibited\n");
            cleanup(&app);
            return 1;
        }

        struct pollfd pfd = {
            .fd = wl_display_get_fd(app.display),
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, (int)(inhibited_until - now_ms()));
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-idle-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (ret == 0) {
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-idle-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-idle-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    zwp_idle_inhibitor_v1_destroy(app.idle_inhibitor);
    app.idle_inhibitor = NULL;
    wl_display_flush(app.display);

    const int64_t idle_deadline = now_ms() + 1500;
    while (!app.got_idled && now_ms() < idle_deadline) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        int remaining = (int)(idle_deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {
            .fd = wl_display_get_fd(app.display),
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, remaining);
        if (ret == 0) {
            break;
        }
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-idle-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-idle-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-idle-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.got_idled) {
        fprintf(stderr, "fbwl-idle-client: timed out waiting for idled event after uninhibit\n");
        cleanup(&app);
        return 1;
    }

    printf("ok idle_notify idle_inhibit\n");
    fflush(stdout);

    cleanup(&app);
    return 0;
}
