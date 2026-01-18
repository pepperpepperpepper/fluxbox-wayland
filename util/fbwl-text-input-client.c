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

#include "text-input-unstable-v3-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_ti_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;

    struct zwp_text_input_manager_v3 *text_input_manager;
    struct zwp_text_input_v3 *text_input;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    bool configured;
    bool entered;

    const char *expect_commit;
    bool got_commit;
    bool success;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-text-input-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-text-input-client-shm-XXXXXX";
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

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_ti_app *app = data;

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
        return;
    }
    if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
        app->text_input_manager =
            wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1);
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
    struct fbwl_ti_app *app = data;
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
    struct fbwl_ti_app *app = data;
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

static void text_input_handle_enter(void *data, struct zwp_text_input_v3 *text_input,
        struct wl_surface *surface) {
    struct fbwl_ti_app *app = data;
    (void)surface;
    if (app->entered) {
        return;
    }
    app->entered = true;

    zwp_text_input_v3_enable(text_input);
    zwp_text_input_v3_set_surrounding_text(text_input, "hello", 5, 5);
    zwp_text_input_v3_set_text_change_cause(text_input, ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
    zwp_text_input_v3_set_content_type(text_input,
        ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
        ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    zwp_text_input_v3_commit(text_input);
    wl_display_flush(app->display);
}

static void text_input_handle_leave(void *data, struct zwp_text_input_v3 *text_input,
        struct wl_surface *surface) {
    (void)data;
    (void)text_input;
    (void)surface;
}

static void text_input_handle_preedit_string(void *data, struct zwp_text_input_v3 *text_input,
        const char *text, int32_t cursor_begin, int32_t cursor_end) {
    (void)data;
    (void)text_input;
    (void)text;
    (void)cursor_begin;
    (void)cursor_end;
}

static void text_input_handle_commit_string(void *data, struct zwp_text_input_v3 *text_input,
        const char *text) {
    (void)text_input;
    struct fbwl_ti_app *app = data;

    const char *got = text != NULL ? text : "";
    if (app->expect_commit != NULL && strcmp(got, app->expect_commit) != 0) {
        fprintf(stderr, "fbwl-text-input-client: commit mismatch got='%s' expect='%s'\n",
            got, app->expect_commit);
        app->got_commit = true;
        app->success = false;
        return;
    }

    app->got_commit = true;
    app->success = true;
    printf("ok text-input commit=%s\n", got);
}

static void text_input_handle_delete_surrounding_text(void *data, struct zwp_text_input_v3 *text_input,
        uint32_t before_length, uint32_t after_length) {
    (void)data;
    (void)text_input;
    (void)before_length;
    (void)after_length;
}

static void text_input_handle_done(void *data, struct zwp_text_input_v3 *text_input,
        uint32_t serial) {
    (void)data;
    (void)text_input;
    (void)serial;
}

static const struct zwp_text_input_v3_listener text_input_listener = {
    .enter = text_input_handle_enter,
    .leave = text_input_handle_leave,
    .preedit_string = text_input_handle_preedit_string,
    .commit_string = text_input_handle_commit_string,
    .delete_surrounding_text = text_input_handle_delete_surrounding_text,
    .done = text_input_handle_done,
};

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--title TITLE] --expect-commit TEXT\n",
        argv0);
}

static void cleanup(struct fbwl_ti_app *app) {
    if (app->text_input != NULL) {
        zwp_text_input_v3_destroy(app->text_input);
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
    if (app->buffer != NULL) {
        wl_buffer_destroy(app->buffer);
    }
    if (app->text_input_manager != NULL) {
        zwp_text_input_manager_v3_destroy(app->text_input_manager);
    }
    if (app->seat != NULL) {
        wl_seat_destroy(app->seat);
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
    const char *title = "text-input";
    const char *expect_commit = NULL;
    int timeout_ms = 6000;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"title", required_argument, NULL, 3},
        {"expect-commit", required_argument, NULL, 4},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", options, NULL)) != -1) {
        switch (opt) {
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
            expect_commit = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 2;
        }
    }
    if (expect_commit == NULL) {
        usage(argv[0]);
        return 2;
    }

    struct fbwl_ti_app app = {0};
    app.expect_commit = expect_commit;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-text-input-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(null)", strerror(errno));
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL ||
            app.seat == NULL || app.text_input_manager == NULL) {
        fprintf(stderr, "fbwl-text-input-client: missing globals compositor=%p shm=%p wm_base=%p seat=%p text_input_mgr=%p\n",
            (void *)app.compositor, (void *)app.shm, (void *)app.wm_base,
            (void *)app.seat, (void *)app.text_input_manager);
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.xdg_toplevel, title);

    app.text_input = zwp_text_input_manager_v3_get_text_input(app.text_input_manager, app.seat);
    if (app.text_input == NULL) {
        fprintf(stderr, "fbwl-text-input-client: failed to create text_input object\n");
        cleanup(&app);
        return 1;
    }
    zwp_text_input_v3_add_listener(app.text_input, &text_input_listener, &app);

    wl_surface_commit(app.surface);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        if (app.got_commit) {
            int rc = app.success ? 0 : 1;
            cleanup(&app);
            return rc;
        }

        struct pollfd pfd = {
            .fd = wl_display_get_fd(app.display),
            .events = POLLIN,
        };
        int remaining = (int)(deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0 && errno == EINTR) {
            continue;
        }
        if (ret <= 0) {
            break;
        }
        if (wl_display_dispatch(app.display) < 0) {
            fprintf(stderr, "fbwl-text-input-client: wl_display_dispatch failed\n");
            break;
        }
    }

    cleanup(&app);
    fprintf(stderr, "fbwl-text-input-client: timed out\n");
    return 1;
}

