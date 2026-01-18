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

#include "xdg-output-unstable-v1-client-protocol.h"

struct fbwl_xdg_output_info {
    uint32_t registry_name;
    struct wl_output *wl_output;
    struct zxdg_output_v1 *xdg_output;

    char *name;
    char *description;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;

    bool have_name;
    bool have_position;
    bool have_size;
    bool done;
};

struct fbwl_xdg_output_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct zxdg_output_manager_v1 *manager;
    uint32_t manager_version;

    struct fbwl_xdg_output_info *outputs;
    size_t outputs_len;
    size_t outputs_cap;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static void xdg_output_handle_logical_position(void *data, struct zxdg_output_v1 *xdg_output,
        int32_t x, int32_t y) {
    (void)xdg_output;
    struct fbwl_xdg_output_info *out = data;
    out->x = x;
    out->y = y;
    out->have_position = true;
}

static void xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
        int32_t width, int32_t height) {
    (void)xdg_output;
    struct fbwl_xdg_output_info *out = data;
    out->width = width;
    out->height = height;
    out->have_size = true;
}

static void xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output) {
    (void)xdg_output;
    struct fbwl_xdg_output_info *out = data;
    out->done = true;
}

static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output,
        const char *name) {
    (void)xdg_output;
    struct fbwl_xdg_output_info *out = data;
    free(out->name);
    out->name = name != NULL ? strdup(name) : NULL;
    out->have_name = out->name != NULL;
}

static void xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output,
        const char *description) {
    (void)xdg_output;
    struct fbwl_xdg_output_info *out = data;
    free(out->description);
    out->description = description != NULL ? strdup(description) : NULL;
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
};

static bool ensure_outputs_capacity(struct fbwl_xdg_output_app *app) {
    if (app->outputs_len < app->outputs_cap) {
        return true;
    }

    size_t new_cap = app->outputs_cap == 0 ? 8 : app->outputs_cap * 2;
    struct fbwl_xdg_output_info *new_outputs = realloc(app->outputs, new_cap * sizeof(*app->outputs));
    if (new_outputs == NULL) {
        return false;
    }
    memset(&new_outputs[app->outputs_cap], 0, (new_cap - app->outputs_cap) * sizeof(*app->outputs));
    app->outputs = new_outputs;
    app->outputs_cap = new_cap;
    return true;
}

static struct fbwl_xdg_output_info *find_output_by_registry_name(struct fbwl_xdg_output_app *app,
        uint32_t registry_name) {
    for (size_t i = 0; i < app->outputs_len; i++) {
        if (app->outputs[i].registry_name == registry_name) {
            return &app->outputs[i];
        }
    }
    return NULL;
}

static bool create_xdg_output_for(struct fbwl_xdg_output_app *app, struct fbwl_xdg_output_info *out) {
    if (app->manager == NULL || out->wl_output == NULL || out->xdg_output != NULL) {
        return true;
    }
    out->xdg_output = zxdg_output_manager_v1_get_xdg_output(app->manager, out->wl_output);
    if (out->xdg_output == NULL) {
        return false;
    }
    zxdg_output_v1_add_listener(out->xdg_output, &xdg_output_listener, out);
    return true;
}

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_xdg_output_app *app = data;
    (void)version;

    if (strcmp(interface, wl_output_interface.name) == 0) {
        if (!ensure_outputs_capacity(app)) {
            fprintf(stderr, "fbwl-xdg-output-client: out of memory\n");
            return;
        }
        struct fbwl_xdg_output_info *out = find_output_by_registry_name(app, name);
        if (out == NULL) {
            out = &app->outputs[app->outputs_len++];
            memset(out, 0, sizeof(*out));
            out->registry_name = name;
        }
        if (out->wl_output == NULL) {
            out->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 2);
        }
        if (out->wl_output != NULL) {
            (void)create_xdg_output_for(app, out);
        }
        return;
    }

    if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        app->manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, v);
        if (app->manager == NULL) {
            fprintf(stderr, "fbwl-xdg-output-client: failed to bind xdg_output_manager\n");
            return;
        }
        app->manager_version = zxdg_output_manager_v1_get_version(app->manager);
        for (size_t i = 0; i < app->outputs_len; i++) {
            (void)create_xdg_output_for(app, &app->outputs[i]);
        }
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
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--expect-outputs N]\n", argv0);
}

static void cleanup(struct fbwl_xdg_output_app *app) {
    for (size_t i = 0; i < app->outputs_len; i++) {
        struct fbwl_xdg_output_info *out = &app->outputs[i];
        free(out->name);
        free(out->description);
        if (out->xdg_output != NULL) {
            zxdg_output_v1_destroy(out->xdg_output);
        }
        if (out->wl_output != NULL) {
            wl_output_destroy(out->wl_output);
        }
    }
    free(app->outputs);
    if (app->manager != NULL) {
        zxdg_output_manager_v1_destroy(app->manager);
    }
    if (app->registry != NULL) {
        wl_registry_destroy(app->registry);
    }
    if (app->display != NULL) {
        wl_display_disconnect(app->display);
    }
}

static size_t count_complete_outputs(const struct fbwl_xdg_output_app *app) {
    size_t count = 0;
    for (size_t i = 0; i < app->outputs_len; i++) {
        const struct fbwl_xdg_output_info *out = &app->outputs[i];
        if (out->xdg_output == NULL || !out->have_position || !out->have_size) {
            continue;
        }
        if (app->manager_version >= 2 && !out->have_name) {
            continue;
        }
        if (app->manager_version < 3 && !out->done) {
            continue;
        }
        count++;
    }
    return count;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 3000;
    int expect_outputs = 1;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"expect-outputs", required_argument, NULL, 3},
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
            expect_outputs = atoi(optarg);
            if (expect_outputs < 1) {
                expect_outputs = 1;
            }
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

    struct fbwl_xdg_output_app app = {0};
    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-xdg-output-client: wl_display_connect failed\n");
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (app.manager == NULL) {
        fprintf(stderr, "fbwl-xdg-output-client: missing zxdg_output_manager_v1\n");
        cleanup(&app);
        return 1;
    }

    const int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        if ((int)count_complete_outputs(&app) >= expect_outputs) {
            break;
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
            fprintf(stderr, "fbwl-xdg-output-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-xdg-output-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-xdg-output-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    const size_t ok_count = count_complete_outputs(&app);
    if ((int)ok_count < expect_outputs) {
        fprintf(stderr, "fbwl-xdg-output-client: timed out (have %zu/%d complete xdg-outputs)\n",
            ok_count, expect_outputs);
        cleanup(&app);
        return 1;
    }

    printf("ok xdg_output outputs=%zu\n", ok_count);
    fflush(stdout);

    cleanup(&app);
    return 0;
}
