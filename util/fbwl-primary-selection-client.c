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

#include "primary-selection-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_offer {
    struct zwp_primary_selection_offer_v1 *offer;
    bool has_text_utf8;
    bool has_text_plain;
};

struct fbwl_primary_selection_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;

    struct wl_seat *seat;
    struct wl_keyboard *keyboard;

    struct zwp_primary_selection_device_manager_v1 *ps_mgr;
    struct zwp_primary_selection_device_v1 *ps_device;
    struct zwp_primary_selection_source_v1 *ps_source;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    bool configured;

    const char *set_text;
    bool mode_set;
    int stay_ms;

    bool mode_get;
    int timeout_ms;

    uint32_t serial;
    bool have_serial;
    bool selection_set;
    bool cancelled;

    struct fbwl_offer offer;
    struct zwp_primary_selection_offer_v1 *selection_offer;
};

static const struct wl_keyboard_listener keyboard_listener;
static const struct zwp_primary_selection_device_v1_listener ps_device_listener;
static const struct zwp_primary_selection_source_v1_listener ps_source_listener;
static const struct zwp_primary_selection_offer_v1_listener ps_offer_listener;

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-primary-selection-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-primary-selection-client-shm-XXXXXX";
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

static void try_set_primary_selection(struct fbwl_primary_selection_app *app) {
    if (app == NULL || !app->mode_set || app->selection_set) {
        return;
    }
    if (!app->have_serial || app->ps_device == NULL || app->ps_source == NULL) {
        return;
    }

    zwp_primary_selection_device_v1_set_selection(app->ps_device, app->ps_source, app->serial);
    wl_display_flush(app->display);
    app->selection_set = true;
    printf("ok primary_selection_set\n");
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
    struct fbwl_primary_selection_app *app = data;

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0) {
        if (app->keyboard == NULL) {
            app->keyboard = wl_seat_get_keyboard(seat);
            if (app->keyboard != NULL) {
                wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
            }
        }
    } else if (app->keyboard != NULL) {
        wl_keyboard_destroy(app->keyboard);
        app->keyboard = NULL;
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

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int fd, uint32_t size) {
    (void)data;
    (void)keyboard;
    (void)format;
    (void)size;
    close(fd);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        struct wl_surface *surface, struct wl_array *keys) {
    (void)keyboard;
    (void)surface;
    (void)keys;
    struct fbwl_primary_selection_app *app = data;
    app->serial = serial;
    app->have_serial = true;
    try_set_primary_selection(app);
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        struct wl_surface *surface) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        uint32_t time, uint32_t key, uint32_t state) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)time;
    (void)key;
    (void)state;
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
        uint32_t group) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)mods_depressed;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
}

static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard,
        int32_t rate, int32_t delay) {
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

static void ps_offer_handle_offer(void *data, struct zwp_primary_selection_offer_v1 *offer,
        const char *mime_type) {
    struct fbwl_primary_selection_app *app = data;
    if (app == NULL || offer == NULL || mime_type == NULL) {
        return;
    }
    if (offer != app->offer.offer) {
        return;
    }

    if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
        app->offer.has_text_utf8 = true;
    }
    if (strcmp(mime_type, "text/plain") == 0) {
        app->offer.has_text_plain = true;
    }
}

static const struct zwp_primary_selection_offer_v1_listener ps_offer_listener = {
    .offer = ps_offer_handle_offer,
};

static void ps_device_handle_data_offer(void *data, struct zwp_primary_selection_device_v1 *device,
        struct zwp_primary_selection_offer_v1 *offer) {
    (void)device;
    struct fbwl_primary_selection_app *app = data;
    if (app == NULL) {
        return;
    }

    if (app->offer.offer != NULL && app->offer.offer != offer) {
        zwp_primary_selection_offer_v1_destroy(app->offer.offer);
    }
    app->offer.offer = offer;
    app->offer.has_text_utf8 = false;
    app->offer.has_text_plain = false;
    if (offer != NULL) {
        zwp_primary_selection_offer_v1_add_listener(offer, &ps_offer_listener, app);
    }
}

static void ps_device_handle_selection(void *data, struct zwp_primary_selection_device_v1 *device,
        struct zwp_primary_selection_offer_v1 *offer) {
    (void)device;
    struct fbwl_primary_selection_app *app = data;
    if (app == NULL) {
        return;
    }

    app->selection_offer = offer;
}

static const struct zwp_primary_selection_device_v1_listener ps_device_listener = {
    .data_offer = ps_device_handle_data_offer,
    .selection = ps_device_handle_selection,
};

static void ps_source_handle_send(void *data, struct zwp_primary_selection_source_v1 *source,
        const char *mime_type, int32_t fd) {
    (void)source;
    (void)mime_type;
    struct fbwl_primary_selection_app *app = data;
    const char *text = app != NULL && app->set_text != NULL ? app->set_text : "";
    (void)write(fd, text, strlen(text));
    close(fd);
}

static void ps_source_handle_cancelled(void *data, struct zwp_primary_selection_source_v1 *source) {
    (void)source;
    struct fbwl_primary_selection_app *app = data;
    if (app != NULL) {
        app->cancelled = true;
    }
}

static const struct zwp_primary_selection_source_v1_listener ps_source_listener = {
    .send = ps_source_handle_send,
    .cancelled = ps_source_handle_cancelled,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_primary_selection_app *app = data;

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
        uint32_t v = version < 5 ? version : 5;
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, v);
        wl_seat_add_listener(app->seat, &seat_listener, app);
        return;
    }
    if (strcmp(interface, zwp_primary_selection_device_manager_v1_interface.name) == 0) {
        uint32_t v = version < 1 ? version : 1;
        app->ps_mgr = wl_registry_bind(registry, name,
            &zwp_primary_selection_device_manager_v1_interface, v);
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
    struct fbwl_primary_selection_app *app = data;
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
    struct fbwl_primary_selection_app *app = data;
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

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--socket NAME] (--set TEXT [--stay-ms MS] | --get) [--timeout-ms MS]\n",
        argv0);
}

static void cleanup(struct fbwl_primary_selection_app *app) {
    if (app->offer.offer != NULL) {
        zwp_primary_selection_offer_v1_destroy(app->offer.offer);
    }
    if (app->ps_source != NULL) {
        zwp_primary_selection_source_v1_destroy(app->ps_source);
    }
    if (app->ps_device != NULL) {
        zwp_primary_selection_device_v1_destroy(app->ps_device);
    }
    if (app->ps_mgr != NULL) {
        zwp_primary_selection_device_manager_v1_destroy(app->ps_mgr);
    }
    if (app->keyboard != NULL) {
        wl_keyboard_destroy(app->keyboard);
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

static int receive_selection_text(struct fbwl_primary_selection_app *app, char **out_text) {
    if (app == NULL || out_text == NULL) {
        return 1;
    }

    struct zwp_primary_selection_offer_v1 *offer = app->selection_offer;
    if (offer == NULL) {
        return 1;
    }

    const char *mime = "text/plain;charset=utf-8";
    if (app->offer.has_text_utf8) {
        mime = "text/plain;charset=utf-8";
    } else if (app->offer.has_text_plain) {
        mime = "text/plain";
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        fprintf(stderr, "fbwl-primary-selection-client: pipe failed: %s\n", strerror(errno));
        return 1;
    }

    zwp_primary_selection_offer_v1_receive(offer, mime, pipefd[1]);
    wl_display_flush(app->display);
    close(pipefd[1]);

    char *buf = NULL;
    size_t len = 0;

    const int64_t deadline = now_ms() + app->timeout_ms;
    for (;;) {
        int remaining = (int)(deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }

        struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN};
        int prc = poll(&pfd, 1, remaining);
        if (prc == 0) {
            fprintf(stderr, "fbwl-primary-selection-client: timed out waiting for selection data\n");
            free(buf);
            close(pipefd[0]);
            return 1;
        }
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-primary-selection-client: poll failed: %s\n", strerror(errno));
            free(buf);
            close(pipefd[0]);
            return 1;
        }

        char tmp[512];
        ssize_t n = read(pipefd[0], tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-primary-selection-client: read failed: %s\n", strerror(errno));
            free(buf);
            close(pipefd[0]);
            return 1;
        }
        if (n == 0) {
            break;
        }

        char *new_buf = realloc(buf, len + (size_t)n + 1);
        if (new_buf == NULL) {
            fprintf(stderr, "fbwl-primary-selection-client: OOM\n");
            free(buf);
            close(pipefd[0]);
            return 1;
        }
        buf = new_buf;
        memcpy(buf + len, tmp, (size_t)n);
        len += (size_t)n;
        buf[len] = '\0';
    }

    close(pipefd[0]);

    if (buf == NULL) {
        buf = strdup("");
    }

    *out_text = buf;
    return 0;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    const char *set_text = NULL;
    bool mode_get = false;
    int timeout_ms = 5000;
    int stay_ms = 0;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"set", required_argument, NULL, 2},
        {"get", no_argument, NULL, 3},
        {"timeout-ms", required_argument, NULL, 4},
        {"stay-ms", required_argument, NULL, 5},
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
            set_text = optarg;
            break;
        case 3:
            mode_get = true;
            break;
        case 4:
            timeout_ms = atoi(optarg);
            break;
        case 5:
            stay_ms = atoi(optarg);
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

    if ((set_text == NULL && !mode_get) || (set_text != NULL && mode_get)) {
        usage(argv[0]);
        return 1;
    }

    struct fbwl_primary_selection_app app = {0};
    app.set_text = set_text;
    app.mode_set = (set_text != NULL);
    app.mode_get = mode_get;
    app.timeout_ms = timeout_ms;
    app.stay_ms = stay_ms;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-primary-selection-client: wl_display_connect failed\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL || app.seat == NULL || app.ps_mgr == NULL) {
        fprintf(stderr, "fbwl-primary-selection-client: missing required globals\n");
        cleanup(&app);
        return 1;
    }

    app.ps_device = zwp_primary_selection_device_manager_v1_get_device(app.ps_mgr, app.seat);
    if (app.ps_device == NULL) {
        fprintf(stderr, "fbwl-primary-selection-client: get_device failed\n");
        cleanup(&app);
        return 1;
    }
    zwp_primary_selection_device_v1_add_listener(app.ps_device, &ps_device_listener, &app);

    if (app.mode_set) {
        app.ps_source = zwp_primary_selection_device_manager_v1_create_source(app.ps_mgr);
        if (app.ps_source == NULL) {
            fprintf(stderr, "fbwl-primary-selection-client: create_source failed\n");
            cleanup(&app);
            return 1;
        }
        zwp_primary_selection_source_v1_add_listener(app.ps_source, &ps_source_listener, &app);
        zwp_primary_selection_source_v1_offer(app.ps_source, "text/plain;charset=utf-8");
        zwp_primary_selection_source_v1_offer(app.ps_source, "text/plain");
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    if (app.surface == NULL) {
        fprintf(stderr, "fbwl-primary-selection-client: wl_compositor_create_surface failed\n");
        cleanup(&app);
        return 1;
    }
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    if (app.xdg_surface == NULL) {
        fprintf(stderr, "fbwl-primary-selection-client: xdg_wm_base_get_xdg_surface failed\n");
        cleanup(&app);
        return 1;
    }
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.xdg_toplevel, app.mode_set ? "fbwl-primary-selection-set" : "fbwl-primary-selection-get");
    app.current_width = 32;
    app.current_height = 32;

    wl_surface_commit(app.surface);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + app.timeout_ms;
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
            fprintf(stderr, "fbwl-primary-selection-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-primary-selection-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-primary-selection-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.configured) {
        fprintf(stderr, "fbwl-primary-selection-client: timed out waiting for xdg_surface.configure\n");
        cleanup(&app);
        return 1;
    }

    if (app.mode_set) {
        const int64_t set_deadline = now_ms() + app.timeout_ms;
        while (!app.selection_set && now_ms() < set_deadline) {
            wl_display_dispatch_pending(app.display);
            wl_display_flush(app.display);
            try_set_primary_selection(&app);

            int remaining = (int)(set_deadline - now_ms());
            if (remaining < 0) {
                remaining = 0;
            }
            struct pollfd pfd = {.fd = wl_display_get_fd(app.display), .events = POLLIN};
            int ret = poll(&pfd, 1, remaining);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "fbwl-primary-selection-client: poll failed: %s\n", strerror(errno));
                cleanup(&app);
                return 1;
            }
            if (ret == 0) {
                break;
            }
            if (pfd.revents & POLLIN) {
                (void)wl_display_dispatch(app.display);
            }
        }

        if (!app.selection_set) {
            fprintf(stderr, "fbwl-primary-selection-client: timed out waiting to set primary selection\n");
            cleanup(&app);
            return 1;
        }

        if (app.stay_ms > 0) {
            const int64_t stay_deadline = now_ms() + app.stay_ms;
            while (!app.cancelled && now_ms() < stay_deadline) {
                wl_display_dispatch_pending(app.display);
                wl_display_flush(app.display);

                int remaining = (int)(stay_deadline - now_ms());
                if (remaining < 0) {
                    remaining = 0;
                }
                struct pollfd pfd = {.fd = wl_display_get_fd(app.display), .events = POLLIN};
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
                    (void)wl_display_dispatch(app.display);
                }
            }
        }

        cleanup(&app);
        return 0;
    }

    /* get mode */
    const int64_t get_deadline = now_ms() + app.timeout_ms;
    while (app.selection_offer == NULL && now_ms() < get_deadline) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        int remaining = (int)(get_deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }

        struct pollfd pfd = {.fd = wl_display_get_fd(app.display), .events = POLLIN};
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-primary-selection-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (ret == 0) {
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-primary-selection-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-primary-selection-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (app.selection_offer == NULL) {
        fprintf(stderr, "fbwl-primary-selection-client: timed out waiting for selection offer\n");
        cleanup(&app);
        return 1;
    }

    char *text = NULL;
    if (receive_selection_text(&app, &text) != 0) {
        cleanup(&app);
        return 1;
    }

    if (text == NULL) {
        text = strdup("");
    }
    printf("%s\n", text);
    free(text);

    cleanup(&app);
    return 0;
}

