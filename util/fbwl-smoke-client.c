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

#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_smoke_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *decoration_mgr;
    struct zxdg_toplevel_decoration_v1 *decoration;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    bool configured;
    bool closed;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-smoke-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-smoke-client-shm-XXXXXX";
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

static void fill_buffer_argb8888(void *data, int width, int height) {
    if (data == NULL || width < 1 || height < 1) {
        return;
    }

    // This client is used in screenshots; avoid solid-white buffers (they look like
    // compositor glitches in galleries).
    const uint32_t bg0 = 0xFF1E1E1E;
    const uint32_t bg1 = 0xFF242424;
    const uint32_t border = 0xFF444444;
    const int border_w = 2;

    uint32_t *px = data;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const bool is_border =
                (x < border_w) || (x >= width - border_w) ||
                (y < border_w) || (y >= height - border_w);
            if (is_border) {
                px[y * width + x] = border;
            } else {
                const bool alt = (((x >> 4) ^ (y >> 4)) & 1) != 0;
                px[y * width + x] = alt ? bg1 : bg0;
            }
        }
    }
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
    fill_buffer_argb8888(data, width, height);

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

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_smoke_app *app = data;

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
    if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        uint32_t v = version < 1 ? version : 1;
        app->decoration_mgr = wl_registry_bind(registry, name,
            &zxdg_decoration_manager_v1_interface, v);
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
    struct fbwl_smoke_app *app = data;
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
    struct fbwl_smoke_app *app = data;
    (void)xdg_toplevel;
    (void)states;
    app->pending_width = width;
    app->pending_height = height;
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct fbwl_smoke_app *app = data;
    (void)xdg_toplevel;
    if (app != NULL) {
        app->closed = true;
    }
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--socket NAME] [--timeout-ms MS] [--title TITLE] [--app-id APPID] [--stay-ms MS] [--xdg-decoration]\n",
        argv0);
}

static void cleanup(struct fbwl_smoke_app *app) {
    if (app->decoration != NULL) {
        zxdg_toplevel_decoration_v1_destroy(app->decoration);
    }
    if (app->decoration_mgr != NULL) {
        zxdg_decoration_manager_v1_destroy(app->decoration_mgr);
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
    int timeout_ms = 2000;
    const char *title = "fbwl-smoke-client";
    const char *app_id = NULL;
    int stay_ms = 0;
    bool want_xdg_decoration = false;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"title", required_argument, NULL, 3},
        {"app-id", required_argument, NULL, 4},
        {"stay-ms", required_argument, NULL, 5},
        {"xdg-decoration", no_argument, NULL, 6},
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
            title = optarg;
            break;
        case 4:
            app_id = optarg;
            break;
        case 5:
            stay_ms = atoi(optarg);
            break;
        case 6:
            want_xdg_decoration = true;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return c == 'h' ? 0 : 1;
        }
    }
    if (optind < argc) {
        usage(argv[0]);
        return 1;
    }

    struct fbwl_smoke_app app = {0};
    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-smoke-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(env)", strerror(errno));
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) {
        fprintf(stderr, "fbwl-smoke-client: wl_display_roundtrip failed: %s\n", strerror(errno));
        cleanup(&app);
        return 1;
    }

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL) {
        fprintf(stderr, "fbwl-smoke-client: missing globals: compositor=%p shm=%p xdg_wm_base=%p\n",
            (void *)app.compositor, (void *)app.shm, (void *)app.wm_base);
        cleanup(&app);
        return 1;
    }
    if (want_xdg_decoration && app.decoration_mgr == NULL) {
        fprintf(stderr,
            "fbwl-smoke-client: --xdg-decoration requested but compositor did not offer zxdg_decoration_manager_v1\n");
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    if (app.surface == NULL) {
        fprintf(stderr, "fbwl-smoke-client: wl_compositor_create_surface failed\n");
        cleanup(&app);
        return 1;
    }
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    if (app.xdg_surface == NULL) {
        fprintf(stderr, "fbwl-smoke-client: xdg_wm_base_get_xdg_surface failed\n");
        cleanup(&app);
        return 1;
    }
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
    if (title != NULL) {
        xdg_toplevel_set_title(app.xdg_toplevel, title);
    }
    if (app_id != NULL) {
        xdg_toplevel_set_app_id(app.xdg_toplevel, app_id);
    }
    if (want_xdg_decoration && app.decoration_mgr != NULL) {
        app.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(app.decoration_mgr,
            app.xdg_toplevel);
        if (app.decoration == NULL) {
            fprintf(stderr, "fbwl-smoke-client: get_toplevel_decoration failed\n");
            cleanup(&app);
            return 1;
        }
        zxdg_toplevel_decoration_v1_set_mode(app.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    app.current_width = 32;
    app.current_height = 32;

    wl_surface_commit(app.surface);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    while (!app.configured && now_ms() < deadline) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        int remaining = (int)(deadline - now_ms());
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
            fprintf(stderr, "fbwl-smoke-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-smoke-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-smoke-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.configured) {
        fprintf(stderr, "fbwl-smoke-client: timed out waiting for xdg_surface.configure\n");
        cleanup(&app);
        return 1;
    }

    if (stay_ms != 0) {
        const int64_t stay_deadline = now_ms() + stay_ms;
        while (!app.closed && now_ms() < stay_deadline) {
            wl_display_dispatch_pending(app.display);
            wl_display_flush(app.display);

            int remaining = (int)(stay_deadline - now_ms());
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
                fprintf(stderr, "fbwl-smoke-client: poll failed: %s\n", strerror(errno));
                cleanup(&app);
                return 1;
            }
            if (pfd.revents & (POLLERR | POLLHUP)) {
                fprintf(stderr, "fbwl-smoke-client: compositor hung up\n");
                cleanup(&app);
                return 1;
            }
            if (pfd.revents & POLLIN) {
                if (wl_display_dispatch(app.display) < 0) {
                    fprintf(stderr, "fbwl-smoke-client: wl_display_dispatch failed: %s\n",
                        strerror(errno));
                    cleanup(&app);
                    return 1;
                }
            }
        }
    }

    cleanup(&app);
    return 0;
}
