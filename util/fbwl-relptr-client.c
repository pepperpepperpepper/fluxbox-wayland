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

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_relptr_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;

    struct wl_pointer *pointer;
    struct zwp_relative_pointer_manager_v1 *rel_mgr;
    struct zwp_relative_pointer_v1 *rel_pointer;
    struct zwp_pointer_constraints_v1 *constraints;
    struct zwp_locked_pointer_v1 *locked_pointer;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    int current_width;
    int current_height;
    int32_t pending_width;
    int32_t pending_height;

    bool configured;
    bool pointer_inside;
    bool lock_requested;
    bool lock_active;
    bool got_relative_motion;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-relptr-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-relptr-client-shm-XXXXXX";
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

static void relptr_handle_relative_motion(void *data,
        struct zwp_relative_pointer_v1 *rel_pointer,
        uint32_t utime_hi, uint32_t utime_lo,
        wl_fixed_t dx, wl_fixed_t dy,
        wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
    (void)rel_pointer;
    (void)utime_hi;
    (void)utime_lo;
    (void)dx_unaccel;
    (void)dy_unaccel;
    struct fbwl_relptr_app *app = data;
    if (!app->lock_active) {
        return;
    }

    const double ddx = wl_fixed_to_double(dx);
    const double ddy = wl_fixed_to_double(dy);
    if (ddx == 0.0 && ddy == 0.0) {
        return;
    }

    printf("ok relative\n");
    fflush(stdout);
    app->got_relative_motion = true;
}

static const struct zwp_relative_pointer_v1_listener relptr_listener = {
    .relative_motion = relptr_handle_relative_motion,
};

static void locked_pointer_handle_locked(void *data,
        struct zwp_locked_pointer_v1 *locked_pointer) {
    (void)locked_pointer;
    struct fbwl_relptr_app *app = data;
    app->lock_active = true;
    printf("ok locked\n");
    fflush(stdout);
}

static void locked_pointer_handle_unlocked(void *data,
        struct zwp_locked_pointer_v1 *locked_pointer) {
    (void)locked_pointer;
    struct fbwl_relptr_app *app = data;
    app->lock_active = false;
}

static const struct zwp_locked_pointer_v1_listener locked_pointer_listener = {
    .locked = locked_pointer_handle_locked,
    .unlocked = locked_pointer_handle_unlocked,
};

static void maybe_setup_relative_pointer(struct fbwl_relptr_app *app) {
    if (app->rel_pointer != NULL || app->rel_mgr == NULL || app->pointer == NULL) {
        return;
    }

    app->rel_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(app->rel_mgr,
        app->pointer);
    if (app->rel_pointer == NULL) {
        fprintf(stderr, "fbwl-relptr-client: failed to create relative pointer\n");
        return;
    }
    zwp_relative_pointer_v1_add_listener(app->rel_pointer, &relptr_listener, app);
}

static void maybe_request_lock(struct fbwl_relptr_app *app) {
    if (app->lock_requested || app->constraints == NULL || app->pointer == NULL ||
            app->surface == NULL) {
        return;
    }

    app->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(app->constraints,
        app->surface, app->pointer, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    if (app->locked_pointer == NULL) {
        fprintf(stderr, "fbwl-relptr-client: lock_pointer failed\n");
        return;
    }
    zwp_locked_pointer_v1_add_listener(app->locked_pointer, &locked_pointer_listener, app);
    app->lock_requested = true;
    wl_display_flush(app->display);
}

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer;
    (void)serial;
    (void)sx;
    (void)sy;
    struct fbwl_relptr_app *app = data;
    app->pointer_inside = (surface == app->surface);
    if (app->pointer_inside) {
        maybe_setup_relative_pointer(app);
        maybe_request_lock(app);
    }
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *surface) {
    (void)pointer;
    (void)serial;
    struct fbwl_relptr_app *app = data;
    if (surface == app->surface) {
        app->pointer_inside = false;
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
    (void)data;
    (void)pointer;
    (void)serial;
    (void)time;
    (void)button;
    (void)state;
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

static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static void pointer_handle_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value120) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)value120;
}

static void pointer_handle_axis_relative_direction(void *data, struct wl_pointer *pointer,
        uint32_t axis, uint32_t direction) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)direction;
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
    .axis_value120 = pointer_handle_axis_value120,
    .axis_relative_direction = pointer_handle_axis_relative_direction,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct fbwl_relptr_app *app = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && app->pointer == NULL) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
        maybe_setup_relative_pointer(app);
        return;
    }

    if (!(caps & WL_SEAT_CAPABILITY_POINTER) && app->pointer != NULL) {
        wl_pointer_destroy(app->pointer);
        app->pointer = NULL;
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
    struct fbwl_relptr_app *app = data;

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
        wl_seat_add_listener(app->seat, &seat_listener, app);
        return;
    }
    if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        app->rel_mgr = wl_registry_bind(registry, name,
            &zwp_relative_pointer_manager_v1_interface, 1);
        maybe_setup_relative_pointer(app);
        return;
    }
    if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        app->constraints = wl_registry_bind(registry, name,
            &zwp_pointer_constraints_v1_interface, 1);
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
    struct fbwl_relptr_app *app = data;
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
    struct fbwl_relptr_app *app = data;
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
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS]\n", argv0);
}

static void cleanup(struct fbwl_relptr_app *app) {
    if (app->locked_pointer != NULL) {
        zwp_locked_pointer_v1_destroy(app->locked_pointer);
    }
    if (app->rel_pointer != NULL) {
        zwp_relative_pointer_v1_destroy(app->rel_pointer);
    }
    if (app->pointer != NULL) {
        wl_pointer_destroy(app->pointer);
    }
    if (app->constraints != NULL) {
        zwp_pointer_constraints_v1_destroy(app->constraints);
    }
    if (app->rel_mgr != NULL) {
        zwp_relative_pointer_manager_v1_destroy(app->rel_mgr);
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

    struct fbwl_relptr_app app = {0};
    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-relptr-client: wl_display_connect failed\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL || app.seat == NULL) {
        fprintf(stderr, "fbwl-relptr-client: missing required globals\n");
        cleanup(&app);
        return 1;
    }
    if (app.rel_mgr == NULL || app.constraints == NULL) {
        fprintf(stderr, "fbwl-relptr-client: compositor missing relative-pointer or pointer-constraints\n");
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    if (app.surface == NULL) {
        fprintf(stderr, "fbwl-relptr-client: wl_compositor_create_surface failed\n");
        cleanup(&app);
        return 1;
    }

    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    if (app.xdg_surface == NULL) {
        fprintf(stderr, "fbwl-relptr-client: xdg_wm_base_get_xdg_surface failed\n");
        cleanup(&app);
        return 1;
    }
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.xdg_toplevel, "fbwl-relptr-client");
    app.current_width = 64;
    app.current_height = 64;

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
            fprintf(stderr, "fbwl-relptr-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-relptr-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-relptr-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.configured) {
        fprintf(stderr, "fbwl-relptr-client: timed out waiting for xdg_surface.configure\n");
        cleanup(&app);
        return 1;
    }

    printf("fbwl-relptr-client: ready\n");
    fflush(stdout);

    const int64_t deadline = now_ms() + timeout_ms;
    while (!app.got_relative_motion && now_ms() < deadline) {
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
            fprintf(stderr, "fbwl-relptr-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-relptr-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-relptr-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.lock_active) {
        fprintf(stderr, "fbwl-relptr-client: never got locked\n");
        cleanup(&app);
        return 1;
    }
    if (!app.got_relative_motion) {
        fprintf(stderr, "fbwl-relptr-client: timed out waiting for relative motion\n");
        cleanup(&app);
        return 1;
    }

    cleanup(&app);
    return 0;
}

