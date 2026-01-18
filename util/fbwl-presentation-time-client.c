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

#include "presentation-time-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_presentation_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wp_presentation *presentation;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    struct wp_presentation_feedback *feedback;
    bool feedback_done;
    bool feedback_presented;
    bool feedback_discarded;

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
    fd = memfd_create("fbwl-presentation-time-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-presentation-time-client-shm-XXXXXX";
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

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *wm_base,
        uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

static void wp_presentation_handle_clock_id(void *data, struct wp_presentation *presentation,
        uint32_t clk_id) {
    (void)data;
    (void)presentation;
    (void)clk_id;
}

static const struct wp_presentation_listener presentation_listener = {
    .clock_id = wp_presentation_handle_clock_id,
};

static void feedback_handle_sync_output(void *data, struct wp_presentation_feedback *feedback,
        struct wl_output *output) {
    (void)data;
    (void)feedback;
    (void)output;
}

static void feedback_handle_presented(void *data, struct wp_presentation_feedback *feedback,
        uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec, uint32_t refresh,
        uint32_t seq_hi, uint32_t seq_lo, uint32_t flags) {
    (void)feedback;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    (void)refresh;
    (void)seq_hi;
    (void)seq_lo;
    (void)flags;
    struct fbwl_presentation_app *app = data;
    app->feedback_done = true;
    app->feedback_presented = true;
    app->feedback = NULL;
}

static void feedback_handle_discarded(void *data, struct wp_presentation_feedback *feedback) {
    (void)feedback;
    struct fbwl_presentation_app *app = data;
    app->feedback_done = true;
    app->feedback_discarded = true;
    app->feedback = NULL;
}

static const struct wp_presentation_feedback_listener feedback_listener = {
    .sync_output = feedback_handle_sync_output,
    .presented = feedback_handle_presented,
    .discarded = feedback_handle_discarded,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_presentation_app *app = data;

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
    if (strcmp(interface, wp_presentation_interface.name) == 0) {
        uint32_t v = version < 2 ? version : 2;
        app->presentation = wl_registry_bind(registry, name, &wp_presentation_interface, v);
        wp_presentation_add_listener(app->presentation, &presentation_listener, app);
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
    struct fbwl_presentation_app *app = data;
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
            if (app->presentation != NULL && app->feedback == NULL && !app->feedback_done) {
                app->feedback = wp_presentation_feedback(app->presentation, app->surface);
                wp_presentation_feedback_add_listener(app->feedback, &feedback_listener, app);
            }
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
    struct fbwl_presentation_app *app = data;
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

static void cleanup(struct fbwl_presentation_app *app) {
    if (app->feedback != NULL) {
        wp_presentation_feedback_destroy(app->feedback);
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
    if (app->presentation != NULL) {
        wp_presentation_destroy(app->presentation);
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
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS]\n", argv0);
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 3000;

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

    struct fbwl_presentation_app app = {0};
    app.current_width = 32;
    app.current_height = 32;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-presentation-time-client: wl_display_connect failed\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL || app.presentation == NULL) {
        fprintf(stderr, "fbwl-presentation-time-client: missing required globals\n");
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    if (app.surface == NULL) {
        fprintf(stderr, "fbwl-presentation-time-client: wl_compositor_create_surface failed\n");
        cleanup(&app);
        return 1;
    }

    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    if (app.xdg_surface == NULL) {
        fprintf(stderr, "fbwl-presentation-time-client: xdg_wm_base_get_xdg_surface failed\n");
        cleanup(&app);
        return 1;
    }
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.xdg_toplevel, "fbwl-presentation-time");

    wl_surface_commit(app.surface);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    while (!app.feedback_done && now_ms() < deadline) {
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
            fprintf(stderr, "fbwl-presentation-time-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (ret == 0) {
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-presentation-time-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-presentation-time-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.feedback_done) {
        fprintf(stderr, "fbwl-presentation-time-client: timed out waiting for feedback\n");
        cleanup(&app);
        return 1;
    }

    if (app.feedback_presented) {
        printf("ok presented\n");
    } else if (app.feedback_discarded) {
        printf("ok discarded\n");
    } else {
        printf("ok\n");
    }

    cleanup(&app);
    return 0;
}

