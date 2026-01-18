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

#include "input-method-unstable-v2-client-protocol.h"

struct fbwl_im_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct zwp_input_method_manager_v2 *manager;
    struct zwp_input_method_v2 *input_method;
    struct wl_callback *sync;

    const char *commit_text;
    uint32_t done_count;

    bool activated;
    bool sent_commit;
    bool committed;
    bool success;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] --commit TEXT\n", argv0);
}

static void cleanup(struct fbwl_im_app *app) {
    if (app->sync != NULL) {
        wl_callback_destroy(app->sync);
    }
    if (app->input_method != NULL) {
        zwp_input_method_v2_destroy(app->input_method);
    }
    if (app->manager != NULL) {
        zwp_input_method_manager_v2_destroy(app->manager);
    }
    if (app->seat != NULL) {
        wl_seat_destroy(app->seat);
    }
    if (app->registry != NULL) {
        wl_registry_destroy(app->registry);
    }
    if (app->display != NULL) {
        wl_display_disconnect(app->display);
    }
}

static void sync_handle_done(void *data, struct wl_callback *callback, uint32_t serial) {
    (void)serial;
    struct fbwl_im_app *app = data;
    if (callback != NULL) {
        wl_callback_destroy(callback);
    }
    app->sync = NULL;
    app->committed = true;
    app->success = true;
    printf("ok input-method committed\n");
}

static const struct wl_callback_listener sync_listener = {
    .done = sync_handle_done,
};

static void input_method_handle_activate(void *data, struct zwp_input_method_v2 *input_method) {
    (void)input_method;
    struct fbwl_im_app *app = data;
    app->activated = true;
}

static void input_method_handle_deactivate(void *data, struct zwp_input_method_v2 *input_method) {
    (void)data;
    (void)input_method;
}

static void input_method_handle_surrounding_text(void *data, struct zwp_input_method_v2 *input_method,
        const char *text, uint32_t cursor, uint32_t anchor) {
    (void)data;
    (void)input_method;
    (void)text;
    (void)cursor;
    (void)anchor;
}

static void input_method_handle_text_change_cause(void *data, struct zwp_input_method_v2 *input_method,
        uint32_t cause) {
    (void)data;
    (void)input_method;
    (void)cause;
}

static void input_method_handle_content_type(void *data, struct zwp_input_method_v2 *input_method,
        uint32_t hint, uint32_t purpose) {
    (void)data;
    (void)input_method;
    (void)hint;
    (void)purpose;
}

static void input_method_handle_done(void *data, struct zwp_input_method_v2 *input_method) {
    struct fbwl_im_app *app = data;
    app->done_count++;

    if (!app->activated || app->sent_commit) {
        return;
    }

    zwp_input_method_v2_commit_string(input_method, app->commit_text);
    zwp_input_method_v2_commit(input_method, app->done_count);
    app->sent_commit = true;

    app->sync = wl_display_sync(app->display);
    if (app->sync != NULL) {
        wl_callback_add_listener(app->sync, &sync_listener, app);
    }
    wl_display_flush(app->display);
}

static void input_method_handle_unavailable(void *data, struct zwp_input_method_v2 *input_method) {
    (void)input_method;
    struct fbwl_im_app *app = data;
    fprintf(stderr, "fbwl-input-method-client: input method unavailable\n");
    app->committed = true;
    app->success = false;
}

static const struct zwp_input_method_v2_listener input_method_listener = {
    .activate = input_method_handle_activate,
    .deactivate = input_method_handle_deactivate,
    .surrounding_text = input_method_handle_surrounding_text,
    .text_change_cause = input_method_handle_text_change_cause,
    .content_type = input_method_handle_content_type,
    .done = input_method_handle_done,
    .unavailable = input_method_handle_unavailable,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_im_app *app = data;
    (void)version;

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t v = version < 5 ? version : 5;
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, v);
        return;
    }
    if (strcmp(interface, zwp_input_method_manager_v2_interface.name) == 0) {
        app->manager =
            wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1);
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

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    const char *commit_text = NULL;
    int timeout_ms = 8000;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"commit", required_argument, NULL, 3},
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
            commit_text = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 2;
        }
    }
    if (commit_text == NULL) {
        usage(argv[0]);
        return 2;
    }

    struct fbwl_im_app app = {0};
    app.commit_text = commit_text;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-input-method-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(null)", strerror(errno));
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.seat == NULL || app.manager == NULL) {
        fprintf(stderr, "fbwl-input-method-client: missing globals seat=%p manager=%p\n",
            (void *)app.seat, (void *)app.manager);
        cleanup(&app);
        return 1;
    }

    app.input_method = zwp_input_method_manager_v2_get_input_method(app.manager, app.seat);
    if (app.input_method == NULL) {
        fprintf(stderr, "fbwl-input-method-client: failed to create input method object\n");
        cleanup(&app);
        return 1;
    }
    zwp_input_method_v2_add_listener(app.input_method, &input_method_listener, &app);
    wl_display_flush(app.display);

    const int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        if (app.committed) {
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
            fprintf(stderr, "fbwl-input-method-client: wl_display_dispatch failed\n");
            break;
        }
    }

    cleanup(&app);
    fprintf(stderr, "fbwl-input-method-client: timed out waiting for activate\n");
    return 1;
}
