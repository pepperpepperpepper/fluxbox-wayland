#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/input-event-codes.h>
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

#include "xdg-shell-client-protocol.h"

struct fbwl_dnd_app;

struct fbwl_window {
    struct fbwl_dnd_app *app;
    const char *name;

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

enum fbwl_dnd_role {
    FBWL_DND_SOURCE = 1,
    FBWL_DND_TARGET = 2,
};

struct fbwl_dnd_app {
    enum fbwl_dnd_role role;

    struct wl_display *display;
    struct wl_registry *registry;

    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;

    struct wl_seat *seat;
    struct wl_pointer *pointer;

    struct wl_data_device_manager *data_device_mgr;
    uint32_t data_device_mgr_version;
    struct wl_data_device *data_device;

    struct fbwl_window window;

    struct wl_surface *pointer_surface;

    bool drag_started;
    struct wl_data_source *data_source;

    struct wl_data_offer *dnd_offer;
    uint32_t dnd_enter_serial;
    char *dnd_mime;

    char *text;
    bool printed_ready;
    bool done;
    bool ok;
};

static const char *fbwl_dnd_mime = "text/plain;charset=utf-8";

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-dnd-client", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-dnd-client-shm-XXXXXX";
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

static void maybe_print_ready(struct fbwl_dnd_app *app) {
    if (app->printed_ready) {
        return;
    }
    if (app->window.configured && app->data_device != NULL) {
        fprintf(stdout, "fbwl-dnd-client: ready\n");
        fflush(stdout);
        app->printed_ready = true;
    }
}

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

static void window_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
        uint32_t serial) {
    struct fbwl_window *win = data;
    struct fbwl_dnd_app *app = win->app;

    xdg_surface_ack_configure(xdg_surface, serial);
    win->configured = true;

    int width = win->pending_width > 0 ? win->pending_width : win->current_width;
    int height = win->pending_height > 0 ? win->pending_height : win->current_height;
    if (width <= 0) {
        width = 32;
    }
    if (height <= 0) {
        height = 32;
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

            wl_surface_attach(win->surface, win->buffer, 0, 0);
            wl_surface_damage(win->surface, 0, 0, width, height);
            wl_surface_commit(win->surface);
            wl_display_flush(app->display);
        }
    }

    maybe_print_ready(app);
}

static const struct xdg_surface_listener window_xdg_surface_listener = {
    .configure = window_xdg_surface_configure,
};

static void window_xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
        int32_t width, int32_t height, struct wl_array *states) {
    (void)xdg_toplevel;
    (void)states;
    struct fbwl_window *win = data;
    win->pending_width = width;
    win->pending_height = height;
}

static void window_xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    (void)data;
    (void)xdg_toplevel;
}

static const struct xdg_toplevel_listener window_xdg_toplevel_listener = {
    .configure = window_xdg_toplevel_configure,
    .close = window_xdg_toplevel_close,
};

static int window_init(struct fbwl_window *win, struct fbwl_dnd_app *app,
        const char *name, const char *title, const char *app_id) {
    memset(win, 0, sizeof(*win));
    win->app = app;
    win->name = name;
    win->current_width = 32;
    win->current_height = 32;

    win->surface = wl_compositor_create_surface(app->compositor);
    if (win->surface == NULL) {
        fprintf(stderr, "fbwl-dnd-client: %s: wl_compositor_create_surface failed\n", name);
        return 1;
    }
    win->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, win->surface);
    if (win->xdg_surface == NULL) {
        fprintf(stderr, "fbwl-dnd-client: %s: xdg_wm_base_get_xdg_surface failed\n", name);
        return 1;
    }
    xdg_surface_add_listener(win->xdg_surface, &window_xdg_surface_listener, win);
    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    xdg_toplevel_add_listener(win->xdg_toplevel, &window_xdg_toplevel_listener, win);

    if (title != NULL) {
        xdg_toplevel_set_title(win->xdg_toplevel, title);
    }
    if (app_id != NULL) {
        xdg_toplevel_set_app_id(win->xdg_toplevel, app_id);
    }

    wl_surface_commit(win->surface);
    wl_display_flush(app->display);
    return 0;
}

static void data_source_handle_target(void *data, struct wl_data_source *wl_data_source,
        const char *mime_type) {
    (void)wl_data_source;
    struct fbwl_dnd_app *app = data;
    if (app->role == FBWL_DND_SOURCE) {
        fprintf(stdout, "fbwl-dnd-client: source_target mime=%s\n",
            mime_type != NULL ? mime_type : "(null)");
        fflush(stdout);
    }
}

static void data_source_handle_send(void *data, struct wl_data_source *wl_data_source,
        const char *mime_type, int32_t fd) {
    struct fbwl_dnd_app *app = data;
    (void)wl_data_source;

    if (mime_type == NULL || app->text == NULL) {
        close(fd);
        return;
    }

    const size_t len = strlen(app->text);
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, app->text + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        off += (size_t)n;
    }
    close(fd);
}

static void data_source_handle_cancelled(void *data, struct wl_data_source *wl_data_source) {
    struct fbwl_dnd_app *app = data;
    if (app->data_source == wl_data_source) {
        app->data_source = NULL;
    }
    wl_data_source_destroy(wl_data_source);
    if (app->role == FBWL_DND_SOURCE) {
        fprintf(stderr, "fbwl-dnd-client: cancelled\n");
        app->done = true;
        app->ok = false;
    }
}

static void data_source_handle_dnd_drop_performed(void *data, struct wl_data_source *wl_data_source) {
    (void)wl_data_source;
    struct fbwl_dnd_app *app = data;
    if (app->role == FBWL_DND_SOURCE) {
        fprintf(stdout, "fbwl-dnd-client: source_drop_performed\n");
        fflush(stdout);
    }
}

static void data_source_handle_dnd_finished(void *data, struct wl_data_source *wl_data_source) {
    struct fbwl_dnd_app *app = data;
    if (app->data_source == wl_data_source) {
        app->data_source = NULL;
    }
    wl_data_source_destroy(wl_data_source);
    if (app->role == FBWL_DND_SOURCE) {
        fprintf(stdout, "fbwl-dnd-client: source_finished\n");
        fprintf(stdout, "ok dnd source\n");
        fflush(stdout);
        app->done = true;
        app->ok = true;
    }
}

static void data_source_handle_action(void *data, struct wl_data_source *wl_data_source,
        uint32_t dnd_action) {
    (void)wl_data_source;
    struct fbwl_dnd_app *app = data;
    if (app->role == FBWL_DND_SOURCE) {
        fprintf(stdout, "fbwl-dnd-client: source_action=%u\n", dnd_action);
        fflush(stdout);
    }
}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_handle_target,
    .send = data_source_handle_send,
    .cancelled = data_source_handle_cancelled,
    .dnd_drop_performed = data_source_handle_dnd_drop_performed,
    .dnd_finished = data_source_handle_dnd_finished,
    .action = data_source_handle_action,
};

static void dnd_start(struct fbwl_dnd_app *app, uint32_t serial) {
    if (app->drag_started || app->data_device_mgr == NULL || app->data_device == NULL) {
        return;
    }

    struct wl_data_source *src = wl_data_device_manager_create_data_source(app->data_device_mgr);
    if (src == NULL) {
        fprintf(stderr, "fbwl-dnd-client: create_data_source failed\n");
        app->done = true;
        app->ok = false;
        return;
    }
    wl_data_source_add_listener(src, &data_source_listener, app);
    wl_data_source_offer(src, fbwl_dnd_mime);
    if (app->data_device_mgr_version >= 3) {
        wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    }

    wl_data_device_start_drag(app->data_device, src, app->window.surface, NULL, serial);
    wl_display_flush(app->display);

    app->data_source = src;
    app->drag_started = true;
    fprintf(stdout, "fbwl-dnd-client: drag_started\n");
    fflush(stdout);
}

static void data_offer_handle_offer(void *data, struct wl_data_offer *wl_data_offer,
        const char *mime_type) {
    struct fbwl_dnd_app *app = data;
    if (app->role != FBWL_DND_TARGET || mime_type == NULL) {
        return;
    }
    fprintf(stdout, "fbwl-dnd-client: target_offer mime=%s\n", mime_type);
    fflush(stdout);
    if (app->dnd_mime != NULL) {
        return;
    }

    if (strcmp(mime_type, fbwl_dnd_mime) != 0) {
        return;
    }

    app->dnd_mime = strdup(mime_type);
    if (app->dnd_mime == NULL) {
        fprintf(stderr, "fbwl-dnd-client: strdup failed\n");
        app->done = true;
        app->ok = false;
        return;
    }

    if (app->dnd_offer == wl_data_offer && app->dnd_enter_serial != 0) {
        wl_data_offer_accept(wl_data_offer, app->dnd_enter_serial, mime_type);
        if (app->data_device_mgr_version >= 3) {
            wl_data_offer_set_actions(wl_data_offer,
                WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        }
        wl_display_flush(app->display);
    }
}

static void data_offer_handle_source_actions(void *data, struct wl_data_offer *wl_data_offer,
        uint32_t source_actions) {
    (void)data;
    (void)wl_data_offer;
    (void)source_actions;
}

static void data_offer_handle_action(void *data, struct wl_data_offer *wl_data_offer,
        uint32_t dnd_action) {
    (void)data;
    (void)wl_data_offer;
    (void)dnd_action;
}

static const struct wl_data_offer_listener data_offer_listener = {
    .offer = data_offer_handle_offer,
    .source_actions = data_offer_handle_source_actions,
    .action = data_offer_handle_action,
};

static void data_device_handle_data_offer(void *data, struct wl_data_device *wl_data_device,
        struct wl_data_offer *id) {
    (void)wl_data_device;
    struct fbwl_dnd_app *app = data;
    if (app->role != FBWL_DND_TARGET) {
        return;
    }
    if (id != NULL) {
        fprintf(stdout, "fbwl-dnd-client: target_data_offer\n");
        fflush(stdout);
        wl_data_offer_add_listener(id, &data_offer_listener, app);
    }
}

static void data_device_handle_enter(void *data, struct wl_data_device *wl_data_device,
        uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
        struct wl_data_offer *id) {
    (void)wl_data_device;
    (void)surface;
    (void)x;
    (void)y;

    struct fbwl_dnd_app *app = data;
    if (app->role != FBWL_DND_TARGET) {
        return;
    }

    if (app->dnd_offer != NULL && app->dnd_offer != id) {
        wl_data_offer_destroy(app->dnd_offer);
        app->dnd_offer = NULL;
        free(app->dnd_mime);
        app->dnd_mime = NULL;
    }

    app->dnd_offer = id;
    app->dnd_enter_serial = serial;

    if (app->dnd_offer != NULL) {
        if (app->dnd_mime == NULL) {
            app->dnd_mime = strdup(fbwl_dnd_mime);
            if (app->dnd_mime == NULL) {
                fprintf(stderr, "fbwl-dnd-client: strdup failed\n");
                app->done = true;
                app->ok = false;
                return;
            }
        }

        wl_data_offer_accept(app->dnd_offer, app->dnd_enter_serial, app->dnd_mime);
        if (app->data_device_mgr_version >= 3) {
            wl_data_offer_set_actions(app->dnd_offer,
                WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        }
        wl_display_flush(app->display);
        fprintf(stdout, "fbwl-dnd-client: target_enter accept\n");
        fflush(stdout);
    }
}

static void data_device_handle_leave(void *data, struct wl_data_device *wl_data_device) {
    (void)wl_data_device;
    struct fbwl_dnd_app *app = data;
    if (app->role != FBWL_DND_TARGET) {
        return;
    }
    if (app->dnd_offer != NULL) {
        wl_data_offer_destroy(app->dnd_offer);
        app->dnd_offer = NULL;
    }
    free(app->dnd_mime);
    app->dnd_mime = NULL;
}

static void data_device_handle_motion(void *data, struct wl_data_device *wl_data_device,
        uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)data;
    (void)wl_data_device;
    (void)time;
    (void)x;
    (void)y;
}

static int receive_offer_to_string(struct fbwl_dnd_app *app, struct wl_data_offer *offer,
        const char *mime, char *out, size_t out_cap) {
    if (out_cap < 1) {
        return 1;
    }
    out[0] = '\0';

    int fds[2];
    if (pipe(fds) != 0) {
        fprintf(stderr, "fbwl-dnd-client: pipe failed: %s\n", strerror(errno));
        return 1;
    }

    wl_data_offer_receive(offer, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(app->display);

    int flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    const int64_t deadline = now_ms() + 2000;
    ssize_t total = 0;
    bool eof = false;
    while (!eof && total < (ssize_t)out_cap - 1) {
        int remaining = (int)(deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {
            .fd = fds[0],
            .events = POLLIN,
        };
        int ret = poll(&pfd, 1, remaining);
        if (ret == 0) {
            fprintf(stderr, "fbwl-dnd-client: timed out reading DnD payload\n");
            close(fds[0]);
            return 1;
        }
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-dnd-client: poll failed while reading DnD payload: %s\n",
                strerror(errno));
            close(fds[0]);
            return 1;
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLIN)) {
            for (;;) {
                ssize_t n = read(fds[0], out + total, out_cap - 1 - (size_t)total);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    fprintf(stderr, "fbwl-dnd-client: read failed for DnD payload: %s\n",
                        strerror(errno));
                    close(fds[0]);
                    return 1;
                }
                if (n == 0) {
                    eof = true;
                    break;
                }
                total += n;
                if (total >= (ssize_t)out_cap - 1) {
                    break;
                }
            }
        }
    }
    close(fds[0]);
    out[total] = '\0';
    return 0;
}

static void data_device_handle_drop(void *data, struct wl_data_device *wl_data_device) {
    (void)wl_data_device;
    struct fbwl_dnd_app *app = data;
    if (app->role != FBWL_DND_TARGET) {
        return;
    }
    fprintf(stdout, "fbwl-dnd-client: target_drop\n");
    fflush(stdout);

    if (app->dnd_offer == NULL || app->dnd_mime == NULL) {
        fprintf(stderr, "fbwl-dnd-client: drop without a valid offer/mime\n");
        app->done = true;
        app->ok = false;
        return;
    }

    char buf[4096];
    if (receive_offer_to_string(app, app->dnd_offer, app->dnd_mime, buf, sizeof(buf)) != 0) {
        app->done = true;
        app->ok = false;
        return;
    }

    if (strcmp(buf, app->text != NULL ? app->text : "") != 0) {
        fprintf(stderr, "fbwl-dnd-client: drop mismatch: expected '%s' got '%s'\n",
            app->text != NULL ? app->text : "", buf);
        app->done = true;
        app->ok = false;
        return;
    }

    if (app->data_device_mgr_version >= 3) {
        wl_data_offer_finish(app->dnd_offer);
        wl_display_flush(app->display);
    }

    fprintf(stdout, "ok dnd\n");
    fflush(stdout);

    app->done = true;
    app->ok = true;
}

static void data_device_handle_selection(void *data, struct wl_data_device *wl_data_device,
        struct wl_data_offer *id) {
    (void)data;
    (void)wl_data_device;
    (void)id;
}

static const struct wl_data_device_listener data_device_listener = {
    .data_offer = data_device_handle_data_offer,
    .enter = data_device_handle_enter,
    .leave = data_device_handle_leave,
    .motion = data_device_handle_motion,
    .drop = data_device_handle_drop,
    .selection = data_device_handle_selection,
};

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
        struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)wl_pointer;
    (void)serial;
    (void)sx;
    (void)sy;

    struct fbwl_dnd_app *app = data;
    app->pointer_surface = surface;
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
        struct wl_surface *surface) {
    (void)wl_pointer;
    (void)serial;
    (void)surface;

    struct fbwl_dnd_app *app = data;
    if (app->pointer_surface == surface) {
        app->pointer_surface = NULL;
    }
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
        wl_fixed_t sx, wl_fixed_t sy) {
    (void)data;
    (void)wl_pointer;
    (void)time;
    (void)sx;
    (void)sy;
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
        uint32_t time, uint32_t button, uint32_t state) {
    (void)wl_pointer;
    (void)time;

    struct fbwl_dnd_app *app = data;
    if (app->role != FBWL_DND_SOURCE) {
        return;
    }
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }
    if (button != BTN_LEFT) {
        return;
    }
    if (app->pointer_surface != app->window.surface) {
        return;
    }
    dnd_start(app, serial);
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
        uint32_t axis, wl_fixed_t value) {
    (void)data;
    (void)wl_pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {
    (void)data;
    (void)wl_pointer;
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer,
        uint32_t axis_source) {
    (void)data;
    (void)wl_pointer;
    (void)axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time,
        uint32_t axis) {
    (void)data;
    (void)wl_pointer;
    (void)time;
    (void)axis;
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis,
        int32_t discrete) {
    (void)data;
    (void)wl_pointer;
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

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
    struct fbwl_dnd_app *app = data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && app->pointer == NULL) {
        app->pointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
        wl_display_flush(app->display);
        maybe_print_ready(app);
    }
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
    (void)data;
    (void)wl_seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_dnd_app *app = data;

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
    if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        app->data_device_mgr_version = v;
        app->data_device_mgr =
            wl_registry_bind(registry, name, &wl_data_device_manager_interface, v);
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

static void cleanup(struct fbwl_dnd_app *app) {
    cleanup_window(&app->window);

    if (app->dnd_offer != NULL) {
        wl_data_offer_destroy(app->dnd_offer);
        app->dnd_offer = NULL;
    }
    free(app->dnd_mime);
    app->dnd_mime = NULL;

    if (app->data_source != NULL) {
        wl_data_source_destroy(app->data_source);
        app->data_source = NULL;
    }

    if (app->data_device != NULL) {
        wl_data_device_destroy(app->data_device);
        app->data_device = NULL;
    }
    if (app->data_device_mgr != NULL) {
        wl_data_device_manager_destroy(app->data_device_mgr);
        app->data_device_mgr = NULL;
    }
    if (app->pointer != NULL) {
        wl_pointer_destroy(app->pointer);
        app->pointer = NULL;
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
    free(app->text);
    app->text = NULL;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s --role src|dst [--socket NAME] [--timeout-ms MS] [--text TEXT]\n",
        argv0);
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 5000;
    const char *text = "fbwl-dnd-smoke";
    const char *role_str = NULL;

    static const struct option options[] = {
        {"role", required_argument, NULL, 0},
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"text", required_argument, NULL, 3},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "h", options, NULL)) != -1) {
        switch (c) {
        case 0:
            role_str = optarg;
            break;
        case 1:
            socket_name = optarg;
            break;
        case 2:
            timeout_ms = atoi(optarg);
            break;
        case 3:
            text = optarg;
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
    if (role_str == NULL) {
        usage(argv[0]);
        return 1;
    }

    struct fbwl_dnd_app app = {0};
    if (strcmp(role_str, "src") == 0 || strcmp(role_str, "source") == 0) {
        app.role = FBWL_DND_SOURCE;
    } else if (strcmp(role_str, "dst") == 0 || strcmp(role_str, "target") == 0) {
        app.role = FBWL_DND_TARGET;
    } else {
        fprintf(stderr, "fbwl-dnd-client: invalid --role '%s' (expected src|dst)\n", role_str);
        return 1;
    }
    app.text = strdup(text != NULL ? text : "");
    if (app.text == NULL) {
        fprintf(stderr, "fbwl-dnd-client: strdup failed\n");
        return 1;
    }

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-dnd-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(env)", strerror(errno));
        cleanup(&app);
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) {
        fprintf(stderr, "fbwl-dnd-client: wl_display_roundtrip failed: %s\n", strerror(errno));
        cleanup(&app);
        return 1;
    }

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL ||
            app.seat == NULL || app.data_device_mgr == NULL) {
        fprintf(stderr, "fbwl-dnd-client: missing globals: compositor=%p shm=%p xdg_wm_base=%p seat=%p ddm=%p\n",
            (void *)app.compositor, (void *)app.shm, (void *)app.wm_base,
            (void *)app.seat, (void *)app.data_device_mgr);
        cleanup(&app);
        return 1;
    }

    app.data_device = wl_data_device_manager_get_data_device(app.data_device_mgr, app.seat);
    if (app.data_device == NULL) {
        fprintf(stderr, "fbwl-dnd-client: get_data_device failed\n");
        cleanup(&app);
        return 1;
    }
    wl_data_device_add_listener(app.data_device, &data_device_listener, &app);

    if (app.role == FBWL_DND_SOURCE) {
        if (window_init(&app.window, &app, "src", "fbwl-dnd-src", "fbwl-dnd-src") != 0) {
            cleanup(&app);
            return 1;
        }
    } else {
        if (window_init(&app.window, &app, "dst", "fbwl-dnd-dst", "fbwl-dnd-dst") != 0) {
            cleanup(&app);
            return 1;
        }
    }

    if (app.window.surface != NULL) {
        /* Needed for pointer-based tests to identify focus surfaces. */
        app.pointer_surface = NULL;
    }

    if (app.role == FBWL_DND_TARGET) {
        /* Be explicit about receiving text from a different client. */
        fprintf(stdout, "fbwl-dnd-client: target_waiting\n");
        fflush(stdout);
    }

    if (app.role == FBWL_DND_SOURCE) {
        fprintf(stdout, "fbwl-dnd-client: source_waiting\n");
        fflush(stdout);
    }

    maybe_print_ready(&app);

    /* Targets should terminate after a successful drop; sources after dnd_finished. */
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
            fprintf(stderr, "fbwl-dnd-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-dnd-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-dnd-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (!app.done) {
        fprintf(stderr, "fbwl-dnd-client: timeout\n");
        cleanup(&app);
        return 1;
    }

    if (app.role == FBWL_DND_TARGET && app.ok) {
        /* Ensure wl_data_offer.finish is processed before exit. */
        (void)wl_display_roundtrip(app.display);
    }

    const int rc = app.ok ? 0 : 1;
    cleanup(&app);
    return rc;
}
