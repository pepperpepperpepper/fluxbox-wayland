#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "ext-data-control-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"

enum fbwl_dc_proto {
    FBWL_DC_PROTO_EXT,
    FBWL_DC_PROTO_WLR,
};

struct fbwl_offer_info {
    bool has_text_utf8;
    bool has_text_plain;
};

struct fbwl_data_control_app {
    enum fbwl_dc_proto proto;

    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;

    struct zwlr_data_control_manager_v1 *wlr_mgr;
    struct zwlr_data_control_device_v1 *wlr_device;
    struct zwlr_data_control_source_v1 *wlr_source;
    struct zwlr_data_control_offer_v1 *wlr_offer_pending;
    struct zwlr_data_control_offer_v1 *wlr_selection_offer;
    struct zwlr_data_control_offer_v1 *wlr_primary_offer;
    struct fbwl_offer_info wlr_offer_pending_info;
    struct fbwl_offer_info wlr_selection_info;
    struct fbwl_offer_info wlr_primary_info;

    struct ext_data_control_manager_v1 *ext_mgr;
    struct ext_data_control_device_v1 *ext_device;
    struct ext_data_control_source_v1 *ext_source;
    struct ext_data_control_offer_v1 *ext_offer_pending;
    struct ext_data_control_offer_v1 *ext_selection_offer;
    struct ext_data_control_offer_v1 *ext_primary_offer;
    struct fbwl_offer_info ext_offer_pending_info;
    struct fbwl_offer_info ext_selection_info;
    struct fbwl_offer_info ext_primary_info;

    const char *set_text;
    bool mode_set;
    bool mode_get;
    bool primary;

    bool selection_set;
    bool cancelled;
    bool finished;

    int timeout_ms;
    int stay_ms;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static void wlr_offer_handle_offer(void *data, struct zwlr_data_control_offer_v1 *offer,
        const char *mime_type) {
    struct fbwl_data_control_app *app = data;
    if (app == NULL || offer == NULL || mime_type == NULL) {
        return;
    }
    if (offer != app->wlr_offer_pending) {
        return;
    }

    if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
        app->wlr_offer_pending_info.has_text_utf8 = true;
    }
    if (strcmp(mime_type, "text/plain") == 0) {
        app->wlr_offer_pending_info.has_text_plain = true;
    }
}

static const struct zwlr_data_control_offer_v1_listener wlr_offer_listener = {
    .offer = wlr_offer_handle_offer,
};

static void wlr_device_handle_data_offer(void *data, struct zwlr_data_control_device_v1 *device,
        struct zwlr_data_control_offer_v1 *offer) {
    (void)device;
    struct fbwl_data_control_app *app = data;
    if (app == NULL || offer == NULL) {
        return;
    }

    app->wlr_offer_pending = offer;
    app->wlr_offer_pending_info = (struct fbwl_offer_info){0};
    zwlr_data_control_offer_v1_add_listener(offer, &wlr_offer_listener, app);
}

static void wlr_device_handle_selection(void *data, struct zwlr_data_control_device_v1 *device,
        struct zwlr_data_control_offer_v1 *offer) {
    (void)device;
    struct fbwl_data_control_app *app = data;
    if (app == NULL) {
        return;
    }

    if (app->wlr_selection_offer != NULL && app->wlr_selection_offer != offer) {
        zwlr_data_control_offer_v1_destroy(app->wlr_selection_offer);
    }
    app->wlr_selection_offer = offer;
    if (offer != NULL && offer == app->wlr_offer_pending) {
        app->wlr_selection_info = app->wlr_offer_pending_info;
    } else if (offer == NULL) {
        app->wlr_selection_info = (struct fbwl_offer_info){0};
    }
}

static void wlr_device_handle_finished(void *data, struct zwlr_data_control_device_v1 *device) {
    (void)device;
    struct fbwl_data_control_app *app = data;
    if (app != NULL) {
        app->finished = true;
    }
}

static void wlr_device_handle_primary_selection(void *data, struct zwlr_data_control_device_v1 *device,
        struct zwlr_data_control_offer_v1 *offer) {
    (void)device;
    struct fbwl_data_control_app *app = data;
    if (app == NULL) {
        return;
    }

    if (app->wlr_primary_offer != NULL && app->wlr_primary_offer != offer) {
        zwlr_data_control_offer_v1_destroy(app->wlr_primary_offer);
    }
    app->wlr_primary_offer = offer;
    if (offer != NULL && offer == app->wlr_offer_pending) {
        app->wlr_primary_info = app->wlr_offer_pending_info;
    } else if (offer == NULL) {
        app->wlr_primary_info = (struct fbwl_offer_info){0};
    }
}

static const struct zwlr_data_control_device_v1_listener wlr_device_listener = {
    .data_offer = wlr_device_handle_data_offer,
    .selection = wlr_device_handle_selection,
    .finished = wlr_device_handle_finished,
    .primary_selection = wlr_device_handle_primary_selection,
};

static void wlr_source_handle_send(void *data, struct zwlr_data_control_source_v1 *source,
        const char *mime_type, int32_t fd) {
    (void)source;
    (void)mime_type;
    struct fbwl_data_control_app *app = data;
    if (app == NULL) {
        close(fd);
        return;
    }

    const char *text = app->set_text != NULL ? app->set_text : "";
    (void)write_all(fd, text, strlen(text));
    close(fd);
}

static void wlr_source_handle_cancelled(void *data, struct zwlr_data_control_source_v1 *source) {
    (void)source;
    struct fbwl_data_control_app *app = data;
    if (app != NULL) {
        app->cancelled = true;
    }
}

static const struct zwlr_data_control_source_v1_listener wlr_source_listener = {
    .send = wlr_source_handle_send,
    .cancelled = wlr_source_handle_cancelled,
};

static void ext_offer_handle_offer(void *data, struct ext_data_control_offer_v1 *offer,
        const char *mime_type) {
    struct fbwl_data_control_app *app = data;
    if (app == NULL || offer == NULL || mime_type == NULL) {
        return;
    }
    if (offer != app->ext_offer_pending) {
        return;
    }

    if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
        app->ext_offer_pending_info.has_text_utf8 = true;
    }
    if (strcmp(mime_type, "text/plain") == 0) {
        app->ext_offer_pending_info.has_text_plain = true;
    }
}

static const struct ext_data_control_offer_v1_listener ext_offer_listener = {
    .offer = ext_offer_handle_offer,
};

static void ext_device_handle_data_offer(void *data, struct ext_data_control_device_v1 *device,
        struct ext_data_control_offer_v1 *offer) {
    (void)device;
    struct fbwl_data_control_app *app = data;
    if (app == NULL || offer == NULL) {
        return;
    }

    app->ext_offer_pending = offer;
    app->ext_offer_pending_info = (struct fbwl_offer_info){0};
    ext_data_control_offer_v1_add_listener(offer, &ext_offer_listener, app);
}

static void ext_device_handle_selection(void *data, struct ext_data_control_device_v1 *device,
        struct ext_data_control_offer_v1 *offer) {
    (void)device;
    struct fbwl_data_control_app *app = data;
    if (app == NULL) {
        return;
    }

    if (app->ext_selection_offer != NULL && app->ext_selection_offer != offer) {
        ext_data_control_offer_v1_destroy(app->ext_selection_offer);
    }
    app->ext_selection_offer = offer;
    if (offer != NULL && offer == app->ext_offer_pending) {
        app->ext_selection_info = app->ext_offer_pending_info;
    } else if (offer == NULL) {
        app->ext_selection_info = (struct fbwl_offer_info){0};
    }
}

static void ext_device_handle_finished(void *data, struct ext_data_control_device_v1 *device) {
    (void)device;
    struct fbwl_data_control_app *app = data;
    if (app != NULL) {
        app->finished = true;
    }
}

static void ext_device_handle_primary_selection(void *data, struct ext_data_control_device_v1 *device,
        struct ext_data_control_offer_v1 *offer) {
    (void)device;
    struct fbwl_data_control_app *app = data;
    if (app == NULL) {
        return;
    }

    if (app->ext_primary_offer != NULL && app->ext_primary_offer != offer) {
        ext_data_control_offer_v1_destroy(app->ext_primary_offer);
    }
    app->ext_primary_offer = offer;
    if (offer != NULL && offer == app->ext_offer_pending) {
        app->ext_primary_info = app->ext_offer_pending_info;
    } else if (offer == NULL) {
        app->ext_primary_info = (struct fbwl_offer_info){0};
    }
}

static const struct ext_data_control_device_v1_listener ext_device_listener = {
    .data_offer = ext_device_handle_data_offer,
    .selection = ext_device_handle_selection,
    .finished = ext_device_handle_finished,
    .primary_selection = ext_device_handle_primary_selection,
};

static void ext_source_handle_send(void *data, struct ext_data_control_source_v1 *source,
        const char *mime_type, int32_t fd) {
    (void)source;
    (void)mime_type;
    struct fbwl_data_control_app *app = data;
    if (app == NULL) {
        close(fd);
        return;
    }

    const char *text = app->set_text != NULL ? app->set_text : "";
    (void)write_all(fd, text, strlen(text));
    close(fd);
}

static void ext_source_handle_cancelled(void *data, struct ext_data_control_source_v1 *source) {
    (void)source;
    struct fbwl_data_control_app *app = data;
    if (app != NULL) {
        app->cancelled = true;
    }
}

static const struct ext_data_control_source_v1_listener ext_source_listener = {
    .send = ext_source_handle_send,
    .cancelled = ext_source_handle_cancelled,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_data_control_app *app = data;

    if (strcmp(interface, wl_seat_interface.name) == 0 && app->seat == NULL) {
        uint32_t v = version < 5 ? version : 5;
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, v);
        return;
    }
    if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0 && app->wlr_mgr == NULL) {
        uint32_t v = version < 2 ? version : 2;
        app->wlr_mgr = wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, v);
        return;
    }
    if (strcmp(interface, ext_data_control_manager_v1_interface.name) == 0 && app->ext_mgr == NULL) {
        uint32_t v = version < 1 ? version : 1;
        app->ext_mgr = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, v);
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

static void cleanup(struct fbwl_data_control_app *app) {
    if (app == NULL) {
        return;
    }

    if (app->wlr_offer_pending != NULL) {
        zwlr_data_control_offer_v1_destroy(app->wlr_offer_pending);
    }
    if (app->wlr_selection_offer != NULL && app->wlr_selection_offer != app->wlr_offer_pending) {
        zwlr_data_control_offer_v1_destroy(app->wlr_selection_offer);
    }
    if (app->wlr_primary_offer != NULL &&
            app->wlr_primary_offer != app->wlr_offer_pending &&
            app->wlr_primary_offer != app->wlr_selection_offer) {
        zwlr_data_control_offer_v1_destroy(app->wlr_primary_offer);
    }
    if (app->wlr_source != NULL) {
        zwlr_data_control_source_v1_destroy(app->wlr_source);
    }
    if (app->wlr_device != NULL) {
        zwlr_data_control_device_v1_destroy(app->wlr_device);
    }
    if (app->wlr_mgr != NULL) {
        zwlr_data_control_manager_v1_destroy(app->wlr_mgr);
    }

    if (app->ext_offer_pending != NULL) {
        ext_data_control_offer_v1_destroy(app->ext_offer_pending);
    }
    if (app->ext_selection_offer != NULL && app->ext_selection_offer != app->ext_offer_pending) {
        ext_data_control_offer_v1_destroy(app->ext_selection_offer);
    }
    if (app->ext_primary_offer != NULL &&
            app->ext_primary_offer != app->ext_offer_pending &&
            app->ext_primary_offer != app->ext_selection_offer) {
        ext_data_control_offer_v1_destroy(app->ext_primary_offer);
    }
    if (app->ext_source != NULL) {
        ext_data_control_source_v1_destroy(app->ext_source);
    }
    if (app->ext_device != NULL) {
        ext_data_control_device_v1_destroy(app->ext_device);
    }
    if (app->ext_mgr != NULL) {
        ext_data_control_manager_v1_destroy(app->ext_mgr);
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

static int receive_text_from_fd(int fd, int timeout_ms, char **out_text) {
    if (out_text == NULL) {
        return 1;
    }

    char *buf = NULL;
    size_t len = 0;

    const int64_t deadline = now_ms() + timeout_ms;
    for (;;) {
        int remaining = (int)(deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }

        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int prc = poll(&pfd, 1, remaining);
        if (prc == 0) {
            fprintf(stderr, "fbwl-data-control-client: timed out waiting for data\n");
            free(buf);
            return 1;
        }
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-data-control-client: poll failed: %s\n", strerror(errno));
            free(buf);
            return 1;
        }

        char tmp[512];
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-data-control-client: read failed: %s\n", strerror(errno));
            free(buf);
            return 1;
        }
        if (n == 0) {
            break;
        }

        char *new_buf = realloc(buf, len + (size_t)n + 1);
        if (new_buf == NULL) {
            fprintf(stderr, "fbwl-data-control-client: OOM\n");
            free(buf);
            return 1;
        }
        buf = new_buf;
        memcpy(buf + len, tmp, (size_t)n);
        len += (size_t)n;
        buf[len] = '\0';
    }

    if (buf == NULL) {
        buf = strdup("");
    }
    *out_text = buf;
    return 0;
}

static int wlr_receive_offer_text(struct fbwl_data_control_app *app,
        struct zwlr_data_control_offer_v1 *offer, struct fbwl_offer_info info, char **out_text) {
    if (app == NULL || offer == NULL || out_text == NULL) {
        return 1;
    }

    const char *mime = "text/plain;charset=utf-8";
    if (info.has_text_utf8) {
        mime = "text/plain;charset=utf-8";
    } else if (info.has_text_plain) {
        mime = "text/plain";
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        fprintf(stderr, "fbwl-data-control-client: pipe failed: %s\n", strerror(errno));
        return 1;
    }

    zwlr_data_control_offer_v1_receive(offer, mime, pipefd[1]);
    wl_display_flush(app->display);
    close(pipefd[1]);

    int rc = receive_text_from_fd(pipefd[0], app->timeout_ms, out_text);
    close(pipefd[0]);
    return rc;
}

static int ext_receive_offer_text(struct fbwl_data_control_app *app,
        struct ext_data_control_offer_v1 *offer, struct fbwl_offer_info info, char **out_text) {
    if (app == NULL || offer == NULL || out_text == NULL) {
        return 1;
    }

    const char *mime = "text/plain;charset=utf-8";
    if (info.has_text_utf8) {
        mime = "text/plain;charset=utf-8";
    } else if (info.has_text_plain) {
        mime = "text/plain";
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        fprintf(stderr, "fbwl-data-control-client: pipe failed: %s\n", strerror(errno));
        return 1;
    }

    ext_data_control_offer_v1_receive(offer, mime, pipefd[1]);
    wl_display_flush(app->display);
    close(pipefd[1]);

    int rc = receive_text_from_fd(pipefd[0], app->timeout_ms, out_text);
    close(pipefd[0]);
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--socket NAME] [--protocol ext|wlr] [--primary] (--set TEXT [--stay-ms MS] | --get) [--timeout-ms MS]\n",
        argv0);
}

static enum fbwl_dc_proto parse_proto(const char *s) {
    if (s != NULL && strcasecmp(s, "wlr") == 0) {
        return FBWL_DC_PROTO_WLR;
    }
    return FBWL_DC_PROTO_EXT;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    enum fbwl_dc_proto proto = FBWL_DC_PROTO_EXT;
    const char *set_text = NULL;
    bool mode_get = false;
    bool primary = false;
    int timeout_ms = 3000;
    int stay_ms = 0;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"protocol", required_argument, NULL, 2},
        {"primary", no_argument, NULL, 3},
        {"set", required_argument, NULL, 4},
        {"get", no_argument, NULL, 5},
        {"timeout-ms", required_argument, NULL, 6},
        {"stay-ms", required_argument, NULL, 7},
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
            proto = parse_proto(optarg);
            break;
        case 3:
            primary = true;
            break;
        case 4:
            set_text = optarg;
            break;
        case 5:
            mode_get = true;
            break;
        case 6:
            timeout_ms = atoi(optarg);
            break;
        case 7:
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

    struct fbwl_data_control_app app = {0};
    app.proto = proto;
    app.set_text = set_text;
    app.mode_set = (set_text != NULL);
    app.mode_get = mode_get;
    app.primary = primary;
    app.timeout_ms = timeout_ms;
    app.stay_ms = stay_ms;

    app.display = wl_display_connect(socket_name);
    if (app.display == NULL) {
        fprintf(stderr, "fbwl-data-control-client: wl_display_connect failed\n");
        cleanup(&app);
        return 1;
    }

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.seat == NULL) {
        fprintf(stderr, "fbwl-data-control-client: missing wl_seat\n");
        cleanup(&app);
        return 1;
    }

    if (app.proto == FBWL_DC_PROTO_WLR) {
        if (app.wlr_mgr == NULL) {
            fprintf(stderr, "fbwl-data-control-client: missing zwlr_data_control_manager_v1\n");
            cleanup(&app);
            return 1;
        }

        app.wlr_device = zwlr_data_control_manager_v1_get_data_device(app.wlr_mgr, app.seat);
        if (app.wlr_device == NULL) {
            fprintf(stderr, "fbwl-data-control-client: get_data_device failed\n");
            cleanup(&app);
            return 1;
        }
        zwlr_data_control_device_v1_add_listener(app.wlr_device, &wlr_device_listener, &app);
    } else {
        if (app.ext_mgr == NULL) {
            fprintf(stderr, "fbwl-data-control-client: missing ext_data_control_manager_v1\n");
            cleanup(&app);
            return 1;
        }

        app.ext_device = ext_data_control_manager_v1_get_data_device(app.ext_mgr, app.seat);
        if (app.ext_device == NULL) {
            fprintf(stderr, "fbwl-data-control-client: get_data_device failed\n");
            cleanup(&app);
            return 1;
        }
        ext_data_control_device_v1_add_listener(app.ext_device, &ext_device_listener, &app);
    }

    wl_display_roundtrip(app.display);

    if (app.mode_set) {
        if (app.proto == FBWL_DC_PROTO_WLR) {
            const int v = wl_proxy_get_version((struct wl_proxy *)app.wlr_device);
            if (app.primary && v < 2) {
                fprintf(stderr, "fbwl-data-control-client: wlr-data-control primary selection requires v2\n");
                cleanup(&app);
                return 1;
            }

            app.wlr_source = zwlr_data_control_manager_v1_create_data_source(app.wlr_mgr);
            if (app.wlr_source == NULL) {
                fprintf(stderr, "fbwl-data-control-client: create_data_source failed\n");
                cleanup(&app);
                return 1;
            }
            zwlr_data_control_source_v1_add_listener(app.wlr_source, &wlr_source_listener, &app);
            zwlr_data_control_source_v1_offer(app.wlr_source, "text/plain;charset=utf-8");
            zwlr_data_control_source_v1_offer(app.wlr_source, "text/plain");

            if (app.primary) {
                zwlr_data_control_device_v1_set_primary_selection(app.wlr_device, app.wlr_source);
            } else {
                zwlr_data_control_device_v1_set_selection(app.wlr_device, app.wlr_source);
            }
        } else {
            app.ext_source = ext_data_control_manager_v1_create_data_source(app.ext_mgr);
            if (app.ext_source == NULL) {
                fprintf(stderr, "fbwl-data-control-client: create_data_source failed\n");
                cleanup(&app);
                return 1;
            }
            ext_data_control_source_v1_add_listener(app.ext_source, &ext_source_listener, &app);
            ext_data_control_source_v1_offer(app.ext_source, "text/plain;charset=utf-8");
            ext_data_control_source_v1_offer(app.ext_source, "text/plain");

            if (app.primary) {
                ext_data_control_device_v1_set_primary_selection(app.ext_device, app.ext_source);
            } else {
                ext_data_control_device_v1_set_selection(app.ext_device, app.ext_source);
            }
        }

        wl_display_flush(app.display);
        app.selection_set = true;
        printf("ok %s_set\n", app.primary ? "primary_selection" : "selection");
        fflush(stdout);

        if (app.stay_ms > 0) {
            const int64_t stay_deadline = now_ms() + app.stay_ms;
            while (!app.cancelled && !app.finished && now_ms() < stay_deadline) {
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
    const int64_t deadline = now_ms() + app.timeout_ms;
    while (!app.finished && now_ms() < deadline) {
        if (app.proto == FBWL_DC_PROTO_WLR) {
            if (!app.primary && app.wlr_selection_offer != NULL &&
                    (app.wlr_selection_info.has_text_utf8 || app.wlr_selection_info.has_text_plain)) {
                break;
            }
            if (app.primary && app.wlr_primary_offer != NULL &&
                    (app.wlr_primary_info.has_text_utf8 || app.wlr_primary_info.has_text_plain)) {
                break;
            }
        } else {
            if (!app.primary && app.ext_selection_offer != NULL &&
                    (app.ext_selection_info.has_text_utf8 || app.ext_selection_info.has_text_plain)) {
                break;
            }
            if (app.primary && app.ext_primary_offer != NULL &&
                    (app.ext_primary_info.has_text_utf8 || app.ext_primary_info.has_text_plain)) {
                break;
            }
        }

        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        int remaining = (int)(deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {.fd = wl_display_get_fd(app.display), .events = POLLIN};
        int ret = poll(&pfd, 1, remaining);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-data-control-client: poll failed: %s\n", strerror(errno));
            cleanup(&app);
            return 1;
        }
        if (ret == 0) {
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-data-control-client: compositor hung up\n");
            cleanup(&app);
            return 1;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(app.display) < 0) {
                fprintf(stderr, "fbwl-data-control-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                cleanup(&app);
                return 1;
            }
        }
    }

    if (app.finished) {
        fprintf(stderr, "fbwl-data-control-client: device finished\n");
        cleanup(&app);
        return 1;
    }

    char *text = NULL;
    int rc = 0;
    if (app.proto == FBWL_DC_PROTO_WLR) {
        const int v = wl_proxy_get_version((struct wl_proxy *)app.wlr_device);
        if (app.primary && v < 2) {
            fprintf(stderr, "fbwl-data-control-client: wlr-data-control primary selection requires v2\n");
            cleanup(&app);
            return 1;
        }

        struct zwlr_data_control_offer_v1 *offer =
            app.primary ? app.wlr_primary_offer : app.wlr_selection_offer;
        struct fbwl_offer_info info =
            app.primary ? app.wlr_primary_info : app.wlr_selection_info;
        if (offer == NULL) {
            fprintf(stderr, "fbwl-data-control-client: timed out waiting for offer\n");
            cleanup(&app);
            return 1;
        }
        rc = wlr_receive_offer_text(&app, offer, info, &text);
    } else {
        struct ext_data_control_offer_v1 *offer =
            app.primary ? app.ext_primary_offer : app.ext_selection_offer;
        struct fbwl_offer_info info =
            app.primary ? app.ext_primary_info : app.ext_selection_info;
        if (offer == NULL) {
            fprintf(stderr, "fbwl-data-control-client: timed out waiting for offer\n");
            cleanup(&app);
            return 1;
        }
        rc = ext_receive_offer_text(&app, offer, info, &text);
    }

    if (rc != 0) {
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
