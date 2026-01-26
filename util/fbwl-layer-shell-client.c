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
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct fbwl_layer_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    bool configured;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-layer-shell-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-layer-shell-client-shm-XXXXXX";
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
    // Default to a panel-like dark color (with a subtle border) so smoke
    // screenshots are readable and don't look like a compositor glitch.
    uint32_t *px = data;
    const int stride_px = stride / 4;
    const uint32_t bg = 0xFF2B2B2B;     // ARGB
    const uint32_t border = 0xFF444444; // ARGB
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const bool is_border = (x == 0 || y == 0 || x == width - 1 || y == height - 1);
            px[y * stride_px + x] = is_border ? border : bg;
        }
    }

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

static void layer_surface_handle_configure(void *data,
        struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
        uint32_t width, uint32_t height) {
    (void)layer_surface;
    struct fbwl_layer_app *app = data;

    zwlr_layer_surface_v1_ack_configure(app->layer_surface, serial);
    app->configured = true;

    app->pending_width = (int32_t)width;
    app->pending_height = (int32_t)height;

    int w = app->pending_width > 0 ? app->pending_width : app->current_width;
    int h = app->pending_height > 0 ? app->pending_height : app->current_height;
    if (w <= 0) {
        w = 32;
    }
    if (h <= 0) {
        h = 32;
    }

    if (app->buffer == NULL || w != app->current_width || h != app->current_height) {
        struct wl_buffer *buffer = create_shm_buffer(app->shm, w, h);
        if (buffer != NULL) {
            if (app->buffer != NULL) {
                wl_buffer_destroy(app->buffer);
            }
            app->buffer = buffer;
            app->current_width = w;
            app->current_height = h;

            wl_surface_attach(app->surface, app->buffer, 0, 0);
            wl_surface_damage(app->surface, 0, 0, w, h);
            wl_surface_commit(app->surface);
            wl_display_flush(app->display);
        }
    }
}

static void layer_surface_handle_closed(void *data,
        struct zwlr_layer_surface_v1 *layer_surface) {
    (void)data;
    (void)layer_surface;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_layer_app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, v);
        return;
    }
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        return;
    }
    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        app->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, v);
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

static void cleanup(struct fbwl_layer_app *app) {
    if (app->buffer != NULL) {
        wl_buffer_destroy(app->buffer);
    }
    if (app->layer_surface != NULL) {
        zwlr_layer_surface_v1_destroy(app->layer_surface);
    }
    if (app->surface != NULL) {
        wl_surface_destroy(app->surface);
    }
    if (app->layer_shell != NULL) {
        zwlr_layer_shell_v1_destroy(app->layer_shell);
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

static enum zwlr_layer_shell_v1_layer parse_layer(const char *s) {
    if (s == NULL) {
        return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    }
    if (strcmp(s, "background") == 0) {
        return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    }
    if (strcmp(s, "bottom") == 0) {
        return ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    }
    if (strcmp(s, "top") == 0) {
        return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    }
    if (strcmp(s, "overlay") == 0) {
        return ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    }
    return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
}

static uint32_t parse_anchor(const char *s) {
    if (s == NULL) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    }
    if (strcmp(s, "top") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    }
    if (strcmp(s, "bottom") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    }
    if (strcmp(s, "left") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    }
    if (strcmp(s, "right") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    }
    if (strcmp(s, "all") == 0) {
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    }
    return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--stay-ms MS]\n", argv0);
    fprintf(stderr, "  [--namespace NS] [--layer background|bottom|top|overlay]\n");
    fprintf(stderr, "  [--anchor top|bottom|left|right|all] [--exclusive-zone N]\n");
    fprintf(stderr, "  [--width W] [--height H]\n");
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 2000;
    int stay_ms = 0;
    const char *ns = "fbwl-layer";
    enum zwlr_layer_shell_v1_layer layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    uint32_t anchor = parse_anchor(NULL);
    int exclusive_zone = 0;
    int width = 0;
    int height = 32;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"stay-ms", required_argument, NULL, 3},
        {"namespace", required_argument, NULL, 4},
        {"layer", required_argument, NULL, 5},
        {"anchor", required_argument, NULL, 6},
        {"exclusive-zone", required_argument, NULL, 7},
        {"width", required_argument, NULL, 8},
        {"height", required_argument, NULL, 9},
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
            stay_ms = atoi(optarg);
            break;
        case 4:
            ns = optarg;
            break;
        case 5:
            layer = parse_layer(optarg);
            break;
        case 6:
            anchor = parse_anchor(optarg);
            break;
        case 7:
            exclusive_zone = atoi(optarg);
            break;
        case 8:
            width = atoi(optarg);
            break;
        case 9:
            height = atoi(optarg);
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
    if (timeout_ms < 1) {
        timeout_ms = 1;
    }

    struct fbwl_layer_app app = {0};
    app.current_width = width > 0 ? width : 32;
    app.current_height = height > 0 ? height : 32;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-layer-shell-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(env)", strerror(errno));
        cleanup(&app);
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) {
        fprintf(stderr, "fbwl-layer-shell-client: wl_display_roundtrip failed: %s\n", strerror(errno));
        cleanup(&app);
        return 1;
    }

    if (app.compositor == NULL || app.shm == NULL || app.layer_shell == NULL) {
        fprintf(stderr, "fbwl-layer-shell-client: missing globals compositor=%p shm=%p layer_shell=%p\n",
            (void *)app.compositor, (void *)app.shm, (void *)app.layer_shell);
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    if (app.surface == NULL) {
        fprintf(stderr, "fbwl-layer-shell-client: wl_compositor_create_surface failed\n");
        cleanup(&app);
        return 1;
    }

    app.layer_surface = zwlr_layer_shell_v1_get_layer_surface(app.layer_shell,
        app.surface, NULL, layer, ns);
    if (app.layer_surface == NULL) {
        fprintf(stderr, "fbwl-layer-shell-client: get_layer_surface failed\n");
        cleanup(&app);
        return 1;
    }
    zwlr_layer_surface_v1_add_listener(app.layer_surface, &layer_surface_listener, &app);
    zwlr_layer_surface_v1_set_anchor(app.layer_surface, anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(app.layer_surface, exclusive_zone);
    zwlr_layer_surface_v1_set_size(app.layer_surface,
        width > 0 ? (uint32_t)width : 0,
        height > 0 ? (uint32_t)height : 0);

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
            fprintf(stderr, "fbwl-layer-shell-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-layer-shell-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-layer-shell-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.configured) {
        fprintf(stderr, "fbwl-layer-shell-client: timed out waiting for layer_surface.configure\n");
        cleanup(&app);
        return 1;
    }

    if (stay_ms != 0) {
        const int64_t stay_deadline = now_ms() + stay_ms;
        while (now_ms() < stay_deadline) {
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
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (ret == 0) {
                break;
            }
            if (pfd.revents & POLLIN) {
                if (wl_display_dispatch(app.display) < 0) {
                    break;
                }
            }
        }
    }

    cleanup(&app);
    return 0;
}
