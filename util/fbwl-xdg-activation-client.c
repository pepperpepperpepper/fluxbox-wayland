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

#include "xdg-activation-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_xdg_activation_app;

struct fbwl_window {
    struct fbwl_xdg_activation_app *app;
    const char *title;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;
    bool configured;
};

struct fbwl_xdg_activation_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;

    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xdg_activation_v1 *activation;

    struct fbwl_window win_a;
    struct fbwl_window win_b;

    struct wl_surface *pointer_focus;
    struct xdg_activation_token_v1 *token;
    bool requested;
    bool printed_ready;
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
    fd = memfd_create("fbwl-xdg-activation-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-xdg-activation-client-shm-XXXXXX";
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

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

static void window_commit_if_needed(struct fbwl_window *win) {
    struct fbwl_xdg_activation_app *app = win->app;
    if (app == NULL || app->shm == NULL) {
        return;
    }

    int width = win->pending_width > 0 ? win->pending_width : win->current_width;
    int height = win->pending_height > 0 ? win->pending_height : win->current_height;
    if (width <= 0) {
        width = 128;
    }
    if (height <= 0) {
        height = 96;
    }

    if (win->buffer == NULL || width != win->current_width || height != win->current_height) {
        struct wl_buffer *buffer = create_shm_buffer(app->shm, width, height);
        if (buffer != NULL) {
            if (win->buffer != NULL) {
                wl_buffer_destroy(win->buffer);
            }
            win->buffer = buffer;
            win->current_width = width;
            win->current_height = height;
        }
    }

    if (win->surface != NULL && win->buffer != NULL) {
        wl_surface_attach(win->surface, win->buffer, 0, 0);
        wl_surface_damage(win->surface, 0, 0, width, height);
        wl_surface_commit(win->surface);
    }
}

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface,
        uint32_t serial) {
    struct fbwl_window *win = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    win->configured = true;
    window_commit_if_needed(win);

    struct fbwl_xdg_activation_app *app = win->app;
    if (app != NULL && app->display != NULL) {
        wl_display_flush(app->display);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
        int32_t width, int32_t height, struct wl_array *states) {
    (void)xdg_toplevel;
    (void)states;
    struct fbwl_window *win = data;
    win->pending_width = width;
    win->pending_height = height;
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    (void)xdg_toplevel;
    struct fbwl_window *win = data;
    struct fbwl_xdg_activation_app *app = win->app;
    if (app != NULL) {
        app->done = true;
        app->ok = false;
    }
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

static void token_handle_done(void *data,
        struct xdg_activation_token_v1 *xdg_activation_token_v1, const char *token) {
    (void)xdg_activation_token_v1;
    struct fbwl_xdg_activation_app *app = data;
    if (app == NULL || token == NULL) {
        return;
    }

    xdg_activation_v1_activate(app->activation, token, app->win_b.surface);
    if (app->token != NULL) {
        xdg_activation_token_v1_destroy(app->token);
        app->token = NULL;
    }
    wl_display_flush(app->display);
}

static const struct xdg_activation_token_v1_listener token_listener = {
    .done = token_handle_done,
};

static void pointer_handle_enter(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer;
    (void)serial;
    (void)sx;
    (void)sy;
    struct fbwl_xdg_activation_app *app = data;
    app->pointer_focus = surface;
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *surface) {
    (void)pointer;
    (void)serial;
    struct fbwl_xdg_activation_app *app = data;
    if (app->pointer_focus == surface) {
        app->pointer_focus = NULL;
    }
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
    (void)pointer;
    (void)time;
    (void)button;
    struct fbwl_xdg_activation_app *app = data;
    if (app == NULL || app->requested || app->activation == NULL || app->seat == NULL) {
        return;
    }
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }
    if (app->pointer_focus != app->win_a.surface) {
        return;
    }

    app->requested = true;
    app->token = xdg_activation_v1_get_activation_token(app->activation);
    if (app->token == NULL) {
        fprintf(stderr, "fbwl-xdg-activation-client: get_activation_token failed\n");
        app->done = true;
        app->ok = false;
        return;
    }
    xdg_activation_token_v1_add_listener(app->token, &token_listener, app);
    xdg_activation_token_v1_set_serial(app->token, serial, app->seat);
    xdg_activation_token_v1_set_surface(app->token, app->win_a.surface);
    xdg_activation_token_v1_commit(app->token);
    wl_display_flush(app->display);
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

static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer, uint32_t source) {
    (void)data;
    (void)pointer;
    (void)source;
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

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
        int fd, uint32_t size) {
    (void)data;
    (void)keyboard;
    if (fd < 0) {
        return;
    }
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size > 0) {
        void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            munmap(map, size);
        }
    }
    close(fd);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        struct wl_surface *surface, struct wl_array *keys) {
    (void)keyboard;
    (void)serial;
    (void)keys;
    struct fbwl_xdg_activation_app *app = data;
    if (app == NULL || !app->requested) {
        return;
    }
    if (surface == app->win_b.surface) {
        printf("ok xdg_activation\n");
        fflush(stdout);
        app->done = true;
        app->ok = true;
    }
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
        uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)mods_depressed;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
}

static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate,
        int32_t delay) {
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

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct fbwl_xdg_activation_app *app = data;
    if (app == NULL) {
        return;
    }

    const bool has_pointer = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
    const bool has_keyboard = (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

    if (has_pointer && app->pointer == NULL) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    } else if (!has_pointer && app->pointer != NULL) {
        wl_pointer_destroy(app->pointer);
        app->pointer = NULL;
    }

    if (has_keyboard && app->keyboard == NULL) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
    } else if (!has_keyboard && app->keyboard != NULL) {
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

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_xdg_activation_app *app = data;
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
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
        wl_seat_add_listener(app->seat, &seat_listener, app);
        return;
    }
    if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
        app->activation = wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1);
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

static void cleanup_window(struct fbwl_window *win) {
    if (win->buffer != NULL) {
        wl_buffer_destroy(win->buffer);
        win->buffer = NULL;
    }
    if (win->xdg_toplevel != NULL) {
        xdg_toplevel_destroy(win->xdg_toplevel);
        win->xdg_toplevel = NULL;
    }
    if (win->xdg_surface != NULL) {
        xdg_surface_destroy(win->xdg_surface);
        win->xdg_surface = NULL;
    }
    if (win->surface != NULL) {
        wl_surface_destroy(win->surface);
        win->surface = NULL;
    }
}

static void cleanup(struct fbwl_xdg_activation_app *app) {
    if (app->token != NULL) {
        xdg_activation_token_v1_destroy(app->token);
        app->token = NULL;
    }
    if (app->keyboard != NULL) {
        wl_keyboard_destroy(app->keyboard);
        app->keyboard = NULL;
    }
    if (app->pointer != NULL) {
        wl_pointer_destroy(app->pointer);
        app->pointer = NULL;
    }
    cleanup_window(&app->win_a);
    cleanup_window(&app->win_b);
    if (app->activation != NULL) {
        xdg_activation_v1_destroy(app->activation);
        app->activation = NULL;
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

static bool create_window(struct fbwl_xdg_activation_app *app, struct fbwl_window *win,
        const char *title) {
    win->app = app;
    win->title = title;
    win->current_width = 128;
    win->current_height = 96;

    win->surface = wl_compositor_create_surface(app->compositor);
    if (win->surface == NULL) {
        return false;
    }

    win->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, win->surface);
    if (win->xdg_surface == NULL) {
        return false;
    }
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);

    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    if (win->xdg_toplevel == NULL) {
        return false;
    }
    xdg_toplevel_add_listener(win->xdg_toplevel, &xdg_toplevel_listener, win);
    xdg_toplevel_set_title(win->xdg_toplevel, title);
    wl_surface_commit(win->surface);
    return true;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 8000;

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

    struct fbwl_xdg_activation_app app = {0};
    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-xdg-activation-client: wl_display_connect failed\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL || app.seat == NULL ||
            app.activation == NULL) {
        fprintf(stderr, "fbwl-xdg-activation-client: missing required globals\n");
        cleanup(&app);
        return 1;
    }

    if (!create_window(&app, &app.win_a, "fbwl-activate-a") ||
            !create_window(&app, &app.win_b, "fbwl-activate-b")) {
        fprintf(stderr, "fbwl-xdg-activation-client: failed to create windows\n");
        cleanup(&app);
        return 1;
    }
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    while (!app.done && now_ms() < deadline) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        if (!app.printed_ready && app.win_a.configured && app.win_b.configured) {
            printf("fbwl-xdg-activation-client: ready\n");
            fflush(stdout);
            app.printed_ready = true;
        }

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
            fprintf(stderr, "fbwl-xdg-activation-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-xdg-activation-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-xdg-activation-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.ok) {
        fprintf(stderr, "fbwl-xdg-activation-client: failed or timed out\n");
        cleanup(&app);
        return 1;
    }

    cleanup(&app);
    return 0;
}

