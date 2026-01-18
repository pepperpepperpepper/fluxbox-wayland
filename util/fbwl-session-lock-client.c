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

#include "ext-session-lock-v1-client-protocol.h"

struct fbwl_output {
    struct wl_list link;
    struct wl_output *output;
    uint32_t global_name;
};

struct fbwl_lock_surface {
    struct wl_list link;
    struct fbwl_app *app;
    struct wl_surface *surface;
    struct ext_session_lock_surface_v1 *lock_surface;
    struct wl_buffer *buffer;
    uint32_t width;
    uint32_t height;
    bool configured;
};

struct fbwl_app {
    struct wl_display *display;
    struct wl_registry *registry;

    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct ext_session_lock_manager_v1 *lock_mgr;

    struct wl_list outputs; // fbwl_output.link
    size_t output_count;

    struct ext_session_lock_v1 *lock;
    bool locked;
    bool finished;

    struct wl_list lock_surfaces; // fbwl_lock_surface.link
    size_t configured_count;

    bool sync_done;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-session-lock-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-session-lock-client-shm-XXXXXX";
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

static struct wl_buffer *create_shm_buffer(struct wl_shm *shm, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return NULL;
    }
    const int stride = (int)width * 4;
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
    memset(data, 0x40, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)size);
    close(fd);
    munmap(data, size);
    if (pool == NULL) {
        return NULL;
    }

    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
        (int)width, (int)height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return buffer;
}

static void wl_output_handle_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel,
        const char *make, const char *model, int32_t transform) {
    (void)data;
    (void)wl_output;
    (void)x;
    (void)y;
    (void)physical_width;
    (void)physical_height;
    (void)subpixel;
    (void)make;
    (void)model;
    (void)transform;
}

static void wl_output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
        int32_t width, int32_t height, int32_t refresh) {
    (void)data;
    (void)wl_output;
    (void)flags;
    (void)width;
    (void)height;
    (void)refresh;
}

static void wl_output_handle_done(void *data, struct wl_output *wl_output) {
    (void)data;
    (void)wl_output;
}

static void wl_output_handle_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    (void)data;
    (void)wl_output;
    (void)factor;
}

static void wl_output_handle_name(void *data, struct wl_output *wl_output, const char *name) {
    (void)data;
    (void)wl_output;
    (void)name;
}

static void wl_output_handle_description(void *data, struct wl_output *wl_output, const char *description) {
    (void)data;
    (void)wl_output;
    (void)description;
}

static const struct wl_output_listener wl_output_listener = {
    .geometry = wl_output_handle_geometry,
    .mode = wl_output_handle_mode,
    .done = wl_output_handle_done,
    .scale = wl_output_handle_scale,
    .name = wl_output_handle_name,
    .description = wl_output_handle_description,
};

static void lock_surface_handle_configure(void *data,
        struct ext_session_lock_surface_v1 *lock_surface,
        uint32_t serial, uint32_t width, uint32_t height) {
    struct fbwl_lock_surface *ls = data;
    if (ls == NULL) {
        return;
    }

    ls->width = width;
    ls->height = height;

    ext_session_lock_surface_v1_ack_configure(lock_surface, serial);

    struct fbwl_app *app = ls->app;
    if (app == NULL || app->shm == NULL) {
        return;
    }

    if (ls->buffer != NULL) {
        wl_buffer_destroy(ls->buffer);
        ls->buffer = NULL;
    }
    ls->buffer = create_shm_buffer(app->shm, width, height);
    if (ls->buffer == NULL) {
        return;
    }

    wl_surface_attach(ls->surface, ls->buffer, 0, 0);
    wl_surface_damage(ls->surface, 0, 0, (int)width, (int)height);
    wl_surface_commit(ls->surface);
    wl_display_flush(app->display);

    if (!ls->configured) {
        ls->configured = true;
        app->configured_count++;
    }
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = lock_surface_handle_configure,
};

static void lock_handle_locked(void *data, struct ext_session_lock_v1 *lock) {
    (void)lock;
    struct fbwl_app *app = data;
    app->locked = true;
    printf("ok session-lock locked\n");
    fflush(stdout);
}

static void lock_handle_finished(void *data, struct ext_session_lock_v1 *lock) {
    (void)lock;
    struct fbwl_app *app = data;
    app->finished = true;
}

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked = lock_handle_locked,
    .finished = lock_handle_finished,
};

static void sync_callback_done(void *data, struct wl_callback *callback, uint32_t callback_data) {
    (void)callback_data;
    struct fbwl_app *app = data;
    app->sync_done = true;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
    .done = sync_callback_done,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_app *app = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t bind_version = version;
        if (bind_version > 4) {
            bind_version = 4;
        }
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, bind_version);
        return;
    }
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        return;
    }
    if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        app->lock_mgr = wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1);
        return;
    }
    if (strcmp(interface, wl_output_interface.name) == 0) {
        struct fbwl_output *out = calloc(1, sizeof(*out));
        if (out == NULL) {
            return;
        }
        out->global_name = name;

        uint32_t bind_version = version;
        if (bind_version > 4) {
            bind_version = 4;
        }
        out->output = wl_registry_bind(registry, name, &wl_output_interface, bind_version);
        if (out->output == NULL) {
            free(out);
            return;
        }
        wl_output_add_listener(out->output, &wl_output_listener, out);
        wl_list_insert(app->outputs.prev, &out->link);
        app->output_count++;
        return;
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)registry;
    struct fbwl_app *app = data;
    struct fbwl_output *out, *tmp;
    wl_list_for_each_safe(out, tmp, &app->outputs, link) {
        if (out->global_name == name) {
            wl_list_remove(&out->link);
            if (out->output != NULL) {
                wl_output_destroy(out->output);
            }
            free(out);
            if (app->output_count > 0) {
                app->output_count--;
            }
            break;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--locked-ms MS]\n", argv0);
}

static void lock_surface_cleanup(struct fbwl_lock_surface *ls) {
    if (ls == NULL) {
        return;
    }
    if (ls->lock_surface != NULL) {
        ext_session_lock_surface_v1_destroy(ls->lock_surface);
        ls->lock_surface = NULL;
    }
    if (ls->surface != NULL) {
        wl_surface_destroy(ls->surface);
        ls->surface = NULL;
    }
    if (ls->buffer != NULL) {
        wl_buffer_destroy(ls->buffer);
        ls->buffer = NULL;
    }
    free(ls);
}

static void cleanup(struct fbwl_app *app) {
    if (app == NULL) {
        return;
    }
    struct fbwl_lock_surface *ls, *lstmp;
    wl_list_for_each_safe(ls, lstmp, &app->lock_surfaces, link) {
        wl_list_remove(&ls->link);
        lock_surface_cleanup(ls);
    }
    if (app->lock != NULL) {
        // If we didn't get locked, destroy is legal; otherwise unlock_and_destroy must be used.
        if (!app->locked) {
            ext_session_lock_v1_destroy(app->lock);
        }
        app->lock = NULL;
    }
    if (app->lock_mgr != NULL) {
        ext_session_lock_manager_v1_destroy(app->lock_mgr);
        app->lock_mgr = NULL;
    }
    struct fbwl_output *out, *tmp;
    wl_list_for_each_safe(out, tmp, &app->outputs, link) {
        wl_list_remove(&out->link);
        if (out->output != NULL) {
            wl_output_destroy(out->output);
        }
        free(out);
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

static bool pump_until(struct fbwl_app *app, int64_t deadline_ms, bool (*done)(struct fbwl_app *app)) {
    while (now_ms() < deadline_ms) {
        wl_display_dispatch_pending(app->display);
        wl_display_flush(app->display);
        if (done(app)) {
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
    return done(app);
}

static void pump_for(struct fbwl_app *app, int64_t deadline_ms) {
    while (now_ms() < deadline_ms) {
        wl_display_dispatch_pending(app->display);
        wl_display_flush(app->display);

        int remaining = (int)(deadline_ms - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {
            .fd = wl_display_get_fd(app->display),
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0 && errno == EINTR) {
            continue;
        }
        if (ret <= 0) {
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            return;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app->display) < 0) {
                return;
            }
        }
    }
}

static bool want_locked(struct fbwl_app *app) {
    return app->locked && !app->finished && app->configured_count >= 1;
}

static bool want_sync(struct fbwl_app *app) {
    return app->sync_done;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 6000;
    int locked_ms = 800;

    static const struct option opts[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"locked-ms", required_argument, NULL, 3},
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
            locked_ms = atoi(optarg);
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 2;
        }
    }

    struct fbwl_app app = {0};
    wl_list_init(&app.outputs);
    wl_list_init(&app.lock_surfaces);

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-session-lock-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(null)", strerror(errno));
        cleanup(&app);
        return 1;
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.lock_mgr == NULL) {
        fprintf(stderr, "fbwl-session-lock-client: missing required globals (compositor=%p shm=%p lock_mgr=%p)\n",
            (void *)app.compositor, (void *)app.shm, (void *)app.lock_mgr);
        cleanup(&app);
        return 1;
    }
    if (app.output_count < 1) {
        fprintf(stderr, "fbwl-session-lock-client: compositor has no wl_output\n");
        cleanup(&app);
        return 1;
    }

    app.lock = ext_session_lock_manager_v1_lock(app.lock_mgr);
    if (app.lock == NULL) {
        fprintf(stderr, "fbwl-session-lock-client: lock request failed\n");
        cleanup(&app);
        return 1;
    }
    ext_session_lock_v1_add_listener(app.lock, &lock_listener, &app);

    struct fbwl_output *out;
    wl_list_for_each(out, &app.outputs, link) {
        struct fbwl_lock_surface *ls = calloc(1, sizeof(*ls));
        if (ls == NULL) {
            continue;
        }
        ls->surface = wl_compositor_create_surface(app.compositor);
        if (ls->surface == NULL) {
            free(ls);
            continue;
        }
        ls->app = &app;
        ls->lock_surface = ext_session_lock_v1_get_lock_surface(app.lock, ls->surface, out->output);
        if (ls->lock_surface == NULL) {
            wl_surface_destroy(ls->surface);
            free(ls);
            continue;
        }
        ext_session_lock_surface_v1_add_listener(ls->lock_surface, &lock_surface_listener, ls);
        wl_list_insert(&app.lock_surfaces, &ls->link);
    }

    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    if (!pump_until(&app, deadline, want_locked)) {
        fprintf(stderr, "fbwl-session-lock-client: timed out waiting for locked (locked=%d finished=%d configured=%zu)\n",
            (int)app.locked, (int)app.finished, app.configured_count);
        cleanup(&app);
        return 1;
    }

    const int64_t locked_deadline = now_ms() + locked_ms;
    pump_for(&app, locked_deadline);

    ext_session_lock_v1_unlock_and_destroy(app.lock);
    app.lock = NULL;
    wl_display_flush(app.display);

    struct wl_callback *cb = wl_display_sync(app.display);
    if (cb == NULL) {
        fprintf(stderr, "fbwl-session-lock-client: wl_display_sync failed\n");
        cleanup(&app);
        return 1;
    }
    wl_callback_add_listener(cb, &sync_listener, &app);

    const int64_t unlock_deadline = now_ms() + timeout_ms;
    if (!pump_until(&app, unlock_deadline, want_sync)) {
        fprintf(stderr, "fbwl-session-lock-client: timed out waiting for unlock sync\n");
        cleanup(&app);
        return 1;
    }

    printf("ok session-lock unlocked\n");
    cleanup(&app);
    return 0;
}
