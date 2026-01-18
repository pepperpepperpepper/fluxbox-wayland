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

#include "wlr-screencopy-unstable-v1-client-protocol.h"

struct fbwl_screencopy_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_output *output;

    struct zwlr_screencopy_manager_v1 *manager;
    struct zwlr_screencopy_frame_v1 *frame;

    struct wl_buffer *buffer;
    void *shm_data;
    size_t shm_size;
    uint32_t shm_format;
    int width;
    int height;
    int stride;

    bool expect_rgb_set;
    uint32_t expect_rgb24;
    int sample_x;
    int sample_y;

    bool got_buffer_info;
    bool done;
    bool ok;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-screencopy-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-screencopy-client-shm-XXXXXX";
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

static struct wl_buffer *create_shm_buffer(struct wl_shm *shm,
        uint32_t format, int width, int height, int stride, void **out_data, size_t *out_size) {
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
    memset(data, 0x00, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)size);
    close(fd);
    if (pool == NULL) {
        munmap(data, size);
        return NULL;
    }

    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
        width, height, stride, format);
    wl_shm_pool_destroy(pool);
    if (buffer == NULL) {
        munmap(data, size);
        return NULL;
    }

    if (out_data != NULL) {
        *out_data = data;
    }
    if (out_size != NULL) {
        *out_size = size;
    }
    return buffer;
}

static void frame_handle_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame,
        uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    (void)frame;
    struct fbwl_screencopy_app *app = data;
    app->shm_format = format;
    app->width = (int)width;
    app->height = (int)height;
    app->stride = (int)stride;
    app->got_buffer_info = true;

    if (app->buffer == NULL) {
        app->buffer = create_shm_buffer(app->shm, app->shm_format,
            app->width, app->height, app->stride, &app->shm_data, &app->shm_size);
        if (app->buffer == NULL) {
            fprintf(stderr, "fbwl-screencopy-client: failed to create shm buffer\n");
            app->done = true;
            app->ok = false;
            return;
        }
        zwlr_screencopy_frame_v1_copy(app->frame, app->buffer);
        wl_display_flush(app->display);
    }
}

static void frame_handle_flags(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) {
    (void)data;
    (void)frame;
    (void)flags;
}

static bool parse_rgb24(const char *s, uint32_t *out_rgb24) {
    if (s == NULL || out_rgb24 == NULL) {
        return false;
    }
    while (*s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) {
        s++;
    }
    if (*s == '#') {
        s++;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    if (strlen(s) != 6) {
        return false;
    }
    for (size_t i = 0; i < 6; i++) {
        const char c = s[i];
        const bool is_hex =
            (c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F');
        if (!is_hex) {
            return false;
        }
    }

    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (end == NULL || *end != '\0' || v > 0xFFFFFFUL) {
        return false;
    }
    *out_rgb24 = (uint32_t)v;
    return true;
}

static bool sample_rgb24(const struct fbwl_screencopy_app *app, uint32_t *out_rgb24) {
    if (app == NULL || out_rgb24 == NULL || app->shm_data == NULL || app->width < 1 || app->height < 1 ||
            app->stride < app->width * 4 || app->sample_x < 0 || app->sample_y < 0 ||
            app->sample_x >= app->width || app->sample_y >= app->height) {
        return false;
    }

    if (app->shm_format != WL_SHM_FORMAT_XRGB8888 && app->shm_format != WL_SHM_FORMAT_ARGB8888) {
        return false;
    }

    const uint8_t *row = (const uint8_t *)app->shm_data + (size_t)app->sample_y * (size_t)app->stride;
    uint32_t px = 0;
    memcpy(&px, row + (size_t)app->sample_x * 4, sizeof(px));

    const uint32_t r = (px >> 16) & 0xFF;
    const uint32_t g = (px >> 8) & 0xFF;
    const uint32_t b = px & 0xFF;
    *out_rgb24 = (r << 16) | (g << 8) | b;
    return true;
}

static void frame_handle_ready(void *data, struct zwlr_screencopy_frame_v1 *frame,
        uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    (void)frame;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    struct fbwl_screencopy_app *app = data;

    if (app->expect_rgb_set) {
        uint32_t got_rgb24 = 0;
        if (!sample_rgb24(app, &got_rgb24)) {
            fprintf(stderr, "fbwl-screencopy-client: failed to sample pixel (format=%u size=%dx%d stride=%d xy=%d,%d)\n",
                app->shm_format, app->width, app->height, app->stride, app->sample_x, app->sample_y);
            app->done = true;
            app->ok = false;
            return;
        }
        if (got_rgb24 != app->expect_rgb24) {
            fprintf(stderr, "fbwl-screencopy-client: pixel mismatch at %d,%d got=#%06x want=#%06x\n",
                app->sample_x, app->sample_y, got_rgb24, app->expect_rgb24);
            app->done = true;
            app->ok = false;
            return;
        }
    }

    printf("ok screencopy\n");
    fflush(stdout);
    app->done = true;
    app->ok = true;
}

static void frame_handle_failed(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    (void)frame;
    struct fbwl_screencopy_app *app = data;
    fprintf(stderr, "fbwl-screencopy-client: capture failed\n");
    app->done = true;
    app->ok = false;
}

static void frame_handle_damage(void *data, struct zwlr_screencopy_frame_v1 *frame,
        uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    (void)data;
    (void)frame;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void frame_handle_linux_dmabuf(void *data, struct zwlr_screencopy_frame_v1 *frame,
        uint32_t format, uint32_t width, uint32_t height) {
    (void)data;
    (void)frame;
    (void)format;
    (void)width;
    (void)height;
}

static void frame_handle_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    (void)data;
    (void)frame;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_handle_buffer,
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
    .failed = frame_handle_failed,
    .damage = frame_handle_damage,
    .linux_dmabuf = frame_handle_linux_dmabuf,
    .buffer_done = frame_handle_buffer_done,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_screencopy_app *app = data;
    (void)version;

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        return;
    }
    if (strcmp(interface, wl_output_interface.name) == 0 && app->output == NULL) {
        app->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        return;
    }
    if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        app->manager = wl_registry_bind(registry, name,
            &zwlr_screencopy_manager_v1_interface, 1);
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
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--expect-rgb #RRGGBB] [--sample-x N] [--sample-y N]\n", argv0);
}

static void cleanup(struct fbwl_screencopy_app *app) {
    if (app->frame != NULL) {
        zwlr_screencopy_frame_v1_destroy(app->frame);
    }
    if (app->manager != NULL) {
        zwlr_screencopy_manager_v1_destroy(app->manager);
    }
    if (app->buffer != NULL) {
        wl_buffer_destroy(app->buffer);
    }
    if (app->shm_data != NULL && app->shm_size > 0) {
        munmap(app->shm_data, app->shm_size);
        app->shm_data = NULL;
        app->shm_size = 0;
    }
    if (app->output != NULL) {
        wl_output_destroy(app->output);
    }
    if (app->shm != NULL) {
        wl_shm_destroy(app->shm);
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
    int timeout_ms = 3000;
    const char *expect_rgb = NULL;
    int sample_x = 0;
    int sample_y = 0;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"expect-rgb", required_argument, NULL, 3},
        {"sample-x", required_argument, NULL, 4},
        {"sample-y", required_argument, NULL, 5},
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
            expect_rgb = optarg;
            break;
        case 4:
            sample_x = atoi(optarg);
            break;
        case 5:
            sample_y = atoi(optarg);
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

    struct fbwl_screencopy_app app = {0};
    app.sample_x = sample_x;
    app.sample_y = sample_y;
    if (expect_rgb != NULL) {
        if (!parse_rgb24(expect_rgb, &app.expect_rgb24)) {
            fprintf(stderr, "fbwl-screencopy-client: invalid --expect-rgb (expected #RRGGBB): %s\n", expect_rgb);
            return 1;
        }
        app.expect_rgb_set = true;
    }
    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-screencopy-client: wl_display_connect failed\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.shm == NULL || app.output == NULL || app.manager == NULL) {
        fprintf(stderr, "fbwl-screencopy-client: missing required globals\n");
        cleanup(&app);
        return 1;
    }

    app.frame = zwlr_screencopy_manager_v1_capture_output(app.manager, 0, app.output);
    if (app.frame == NULL) {
        fprintf(stderr, "fbwl-screencopy-client: capture_output failed\n");
        cleanup(&app);
        return 1;
    }
    zwlr_screencopy_frame_v1_add_listener(app.frame, &frame_listener, &app);
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
            fprintf(stderr, "fbwl-screencopy-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-screencopy-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-screencopy-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.done) {
        fprintf(stderr, "fbwl-screencopy-client: timed out waiting for screencopy\n");
        cleanup(&app);
        return 1;
    }

    bool ok = app.ok;
    cleanup(&app);
    return ok ? 0 : 1;
}
