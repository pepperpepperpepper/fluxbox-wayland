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

#include <wayland-client.h>

#include "wlr-export-dmabuf-unstable-v1-client-protocol.h"

struct fbwl_export_dmabuf_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_output *output;
    struct zwlr_export_dmabuf_manager_v1 *manager;
    struct zwlr_export_dmabuf_frame_v1 *frame;

    bool done;
    bool ok;
    bool allow_cancel;
    int overlay_cursor;

    bool got_frame;
    uint32_t expected_objects;
    uint32_t got_objects;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t cancel_reason;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static void frame_handle_frame(void *data,
        struct zwlr_export_dmabuf_frame_v1 *frame,
        uint32_t width, uint32_t height,
        uint32_t offset_x, uint32_t offset_y,
        uint32_t buffer_flags, uint32_t flags,
        uint32_t format, uint32_t mod_high, uint32_t mod_low,
        uint32_t num_objects) {
    (void)frame;
    (void)offset_x;
    (void)offset_y;
    (void)buffer_flags;
    (void)flags;
    (void)mod_high;
    (void)mod_low;
    struct fbwl_export_dmabuf_app *app = data;
    app->got_frame = true;
    app->width = width;
    app->height = height;
    app->format = format;
    app->expected_objects = num_objects;
}

static void frame_handle_object(void *data,
        struct zwlr_export_dmabuf_frame_v1 *frame,
        uint32_t index, int32_t fd, uint32_t size,
        uint32_t offset, uint32_t stride,
        uint32_t plane_index) {
    (void)frame;
    (void)index;
    (void)size;
    (void)offset;
    (void)stride;
    (void)plane_index;
    struct fbwl_export_dmabuf_app *app = data;
    app->got_objects++;
    if (fd >= 0) {
        close(fd);
    }
}

static void frame_handle_ready(void *data,
        struct zwlr_export_dmabuf_frame_v1 *frame,
        uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    (void)frame;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    struct fbwl_export_dmabuf_app *app = data;
    if (!app->got_frame) {
        fprintf(stderr, "fbwl-export-dmabuf-client: got ready without frame\n");
        app->done = true;
        app->ok = false;
        return;
    }
    if (app->got_objects < app->expected_objects) {
        fprintf(stderr, "fbwl-export-dmabuf-client: expected %u objects but got %u\n",
            app->expected_objects, app->got_objects);
        app->done = true;
        app->ok = false;
        return;
    }

    printf("ok export-dmabuf ready %ux%u objects=%u format=0x%x\n",
        app->width, app->height, app->got_objects, app->format);
    fflush(stdout);
    app->done = true;
    app->ok = true;
}

static void frame_handle_cancel(void *data,
        struct zwlr_export_dmabuf_frame_v1 *frame, uint32_t reason) {
    (void)frame;
    struct fbwl_export_dmabuf_app *app = data;
    app->cancel_reason = reason;

    if (app->allow_cancel) {
        printf("ok export-dmabuf cancel reason=%u\n", reason);
        fflush(stdout);
        app->done = true;
        app->ok = true;
        return;
    }

    fprintf(stderr, "fbwl-export-dmabuf-client: cancelled reason=%u\n", reason);
    app->done = true;
    app->ok = false;
}

static const struct zwlr_export_dmabuf_frame_v1_listener frame_listener = {
    .frame = frame_handle_frame,
    .object = frame_handle_object,
    .ready = frame_handle_ready,
    .cancel = frame_handle_cancel,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    struct fbwl_export_dmabuf_app *app = data;

    if (strcmp(interface, wl_output_interface.name) == 0 && app->output == NULL) {
        app->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        return;
    }
    if (strcmp(interface, zwlr_export_dmabuf_manager_v1_interface.name) == 0) {
        app->manager = wl_registry_bind(registry, name,
            &zwlr_export_dmabuf_manager_v1_interface, 1);
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

static void cleanup(struct fbwl_export_dmabuf_app *app) {
    if (app->frame != NULL) {
        zwlr_export_dmabuf_frame_v1_destroy(app->frame);
    }
    if (app->manager != NULL) {
        zwlr_export_dmabuf_manager_v1_destroy(app->manager);
    }
    if (app->output != NULL) {
        wl_output_destroy(app->output);
    }
    if (app->registry != NULL) {
        wl_registry_destroy(app->registry);
    }
    if (app->display != NULL) {
        wl_display_disconnect(app->display);
    }
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--overlay-cursor] [--allow-cancel]\n",
        argv0);
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 3000;
    bool allow_cancel = false;
    int overlay_cursor = 0;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"overlay-cursor", no_argument, NULL, 3},
        {"allow-cancel", no_argument, NULL, 4},
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
            overlay_cursor = 1;
            break;
        case 4:
            allow_cancel = true;
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

    struct fbwl_export_dmabuf_app app = {0};
    app.allow_cancel = allow_cancel;
    app.overlay_cursor = overlay_cursor;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-export-dmabuf-client: wl_display_connect failed\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.output == NULL || app.manager == NULL) {
        fprintf(stderr, "fbwl-export-dmabuf-client: missing required globals\n");
        cleanup(&app);
        return 1;
    }

    app.frame = zwlr_export_dmabuf_manager_v1_capture_output(app.manager,
        app.overlay_cursor, app.output);
    if (app.frame == NULL) {
        fprintf(stderr, "fbwl-export-dmabuf-client: capture_output failed\n");
        cleanup(&app);
        return 1;
    }
    zwlr_export_dmabuf_frame_v1_add_listener(app.frame, &frame_listener, &app);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    while (!app.done && now_ms() < deadline) {
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
            fprintf(stderr, "fbwl-export-dmabuf-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-export-dmabuf-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-export-dmabuf-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.done) {
        fprintf(stderr, "fbwl-export-dmabuf-client: timed out waiting for response\n");
        cleanup(&app);
        return 1;
    }

    bool ok = app.ok;
    cleanup(&app);
    return ok ? 0 : 1;
}
