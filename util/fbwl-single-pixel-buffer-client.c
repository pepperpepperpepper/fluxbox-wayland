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

#include "single-pixel-buffer-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct fbwl_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct wp_single_pixel_buffer_manager_v1 *single_pixel_mgr;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_buffer *buffer;

    bool configured;
    bool committed;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
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
    struct fbwl_app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, v);
        return;
    }
    if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, v);
        xdg_wm_base_add_listener(app->wm_base, &xdg_wm_base_listener, app);
        return;
    }
    if (strcmp(interface, wp_single_pixel_buffer_manager_v1_interface.name) == 0) {
        app->single_pixel_mgr = wl_registry_bind(registry, name,
            &wp_single_pixel_buffer_manager_v1_interface, 1);
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
    struct fbwl_app *app = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    app->configured = true;

    if (app->buffer == NULL) {
        app->buffer = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
            app->single_pixel_mgr, 0xffffffffu, 0x00000000u, 0x00000000u, 0xffffffffu);
        if (app->buffer == NULL) {
            return;
        }
    }

    if (!app->committed) {
        wl_surface_attach(app->surface, app->buffer, 0, 0);
        wl_surface_damage(app->surface, 0, 0, 1, 1);
        wl_surface_commit(app->surface);
        wl_display_flush(app->display);
        app->committed = true;
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
        int32_t width, int32_t height, struct wl_array *states) {
    (void)data;
    (void)xdg_toplevel;
    (void)width;
    (void)height;
    (void)states;
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
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--title TITLE]\n", argv0);
}

static void cleanup(struct fbwl_app *app) {
    if (app->buffer != NULL) {
        wl_buffer_destroy(app->buffer);
        app->buffer = NULL;
    }
    if (app->xdg_toplevel != NULL) {
        xdg_toplevel_destroy(app->xdg_toplevel);
        app->xdg_toplevel = NULL;
    }
    if (app->xdg_surface != NULL) {
        xdg_surface_destroy(app->xdg_surface);
        app->xdg_surface = NULL;
    }
    if (app->surface != NULL) {
        wl_surface_destroy(app->surface);
        app->surface = NULL;
    }
    if (app->single_pixel_mgr != NULL) {
        wp_single_pixel_buffer_manager_v1_destroy(app->single_pixel_mgr);
        app->single_pixel_mgr = NULL;
    }
    if (app->wm_base != NULL) {
        xdg_wm_base_destroy(app->wm_base);
        app->wm_base = NULL;
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

static bool pump_until(struct fbwl_app *app, int64_t deadline_ms) {
    while (now_ms() < deadline_ms) {
        wl_display_dispatch_pending(app->display);
        wl_display_flush(app->display);
        if (app->configured && app->committed) {
            return true;
        }

        int remaining = (int)(deadline_ms - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {
            .fd = wl_display_get_fd(app->display),
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (ret == 0) {
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            return false;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app->display) < 0) {
                return false;
            }
        }
    }
    return app->configured && app->committed;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 3000;
    const char *title = "single-pixel-buffer";

    static const struct option opts[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"title", required_argument, NULL, 3},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
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
        case 'h':
        default:
            usage(argv[0]);
            return 2;
        }
    }

    struct fbwl_app app = {0};
    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-single-pixel-buffer-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(null)", strerror(errno));
        cleanup(&app);
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.wm_base == NULL || app.single_pixel_mgr == NULL) {
        fprintf(stderr,
            "fbwl-single-pixel-buffer-client: missing required globals (compositor=%p wm_base=%p single_pixel_mgr=%p)\n",
            (void *)app.compositor, (void *)app.wm_base, (void *)app.single_pixel_mgr);
        cleanup(&app);
        return 1;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_set_title(app.xdg_toplevel, title);

    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);

    wl_surface_commit(app.surface);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    if (!pump_until(&app, deadline)) {
        fprintf(stderr, "fbwl-single-pixel-buffer-client: timed out waiting for configure+commit\n");
        cleanup(&app);
        return 1;
    }

    cleanup(&app);
    return 0;
}

