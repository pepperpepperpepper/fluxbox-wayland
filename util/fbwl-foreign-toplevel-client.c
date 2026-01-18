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

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

enum fbwl_cmd {
    FBWL_CMD_LIST,
    FBWL_CMD_MINIMIZE,
    FBWL_CMD_UNMINIMIZE,
    FBWL_CMD_CLOSE,
};

struct fbwl_client;

struct fbwl_toplevel {
    struct wl_list link;
    struct fbwl_client *client;
    struct zwlr_foreign_toplevel_handle_v1 *handle;

    char *title;
    char *app_id;

    bool maximized;
    bool minimized;
    bool activated;
    bool fullscreen;

    bool dirty;
};

struct fbwl_client {
    struct wl_display *display;
    struct wl_registry *registry;

    struct zwlr_foreign_toplevel_manager_v1 *manager;
    struct wl_list toplevels; // fbwl_toplevel.link

    enum fbwl_cmd cmd;
    const char *target_title;
    bool action_sent;

    bool want_minimized;

    bool done;
    bool success;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static void toplevel_free(struct fbwl_toplevel *tl) {
    if (tl == NULL) {
        return;
    }
    if (tl->handle != NULL) {
        zwlr_foreign_toplevel_handle_v1_destroy(tl->handle);
    }
    free(tl->title);
    free(tl->app_id);
    free(tl);
}

static void cleanup(struct fbwl_client *c) {
    struct fbwl_toplevel *tl, *tmp;
    wl_list_for_each_safe(tl, tmp, &c->toplevels, link) {
        wl_list_remove(&tl->link);
        toplevel_free(tl);
    }

    if (c->manager != NULL) {
        zwlr_foreign_toplevel_manager_v1_destroy(c->manager);
    }
    if (c->registry != NULL) {
        wl_registry_destroy(c->registry);
    }
    if (c->display != NULL) {
        wl_display_disconnect(c->display);
    }
}

static void maybe_act_on_toplevel(struct fbwl_client *c, struct fbwl_toplevel *tl) {
    if (c == NULL || tl == NULL) {
        return;
    }
    if (c->done) {
        return;
    }

    if (c->cmd == FBWL_CMD_LIST) {
        const char *title = tl->title != NULL ? tl->title : "(no-title)";
        const char *app_id = tl->app_id != NULL ? tl->app_id : "(no-app-id)";
        fprintf(stdout, "toplevel title=%s app_id=%s state=%s%s%s%s\n",
            title, app_id,
            tl->activated ? "activated " : "",
            tl->maximized ? "maximized " : "",
            tl->minimized ? "minimized " : "",
            tl->fullscreen ? "fullscreen " : "");
        return;
    }

    if (c->target_title == NULL || tl->title == NULL) {
        return;
    }
    if (strcmp(tl->title, c->target_title) != 0) {
        return;
    }

    if (c->cmd == FBWL_CMD_MINIMIZE || c->cmd == FBWL_CMD_UNMINIMIZE) {
        const bool want = c->want_minimized;
        if (tl->minimized == want) {
            c->success = true;
            c->done = true;
            return;
        }

        if (!c->action_sent) {
            if (want) {
                zwlr_foreign_toplevel_handle_v1_set_minimized(tl->handle);
            } else {
                zwlr_foreign_toplevel_handle_v1_unset_minimized(tl->handle);
            }
            c->action_sent = true;
            wl_display_flush(c->display);
            return;
        }

        if (c->action_sent && tl->dirty && tl->minimized == want) {
            c->success = true;
            c->done = true;
            return;
        }
        return;
    }

    if (c->cmd == FBWL_CMD_CLOSE) {
        if (!c->action_sent) {
            zwlr_foreign_toplevel_handle_v1_close(tl->handle);
            c->action_sent = true;
            wl_display_flush(c->display);
        }
    }
}

static void handle_toplevel_title(void *data,
        struct zwlr_foreign_toplevel_handle_v1 *handle, const char *title) {
    (void)handle;
    struct fbwl_toplevel *tl = data;
    free(tl->title);
    tl->title = title != NULL ? strdup(title) : NULL;
}

static void handle_toplevel_app_id(void *data,
        struct zwlr_foreign_toplevel_handle_v1 *handle, const char *app_id) {
    (void)handle;
    struct fbwl_toplevel *tl = data;
    free(tl->app_id);
    tl->app_id = app_id != NULL ? strdup(app_id) : NULL;
}

static void handle_toplevel_output_enter(void *data,
        struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output) {
    (void)data;
    (void)handle;
    (void)output;
}

static void handle_toplevel_output_leave(void *data,
        struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output) {
    (void)data;
    (void)handle;
    (void)output;
}

static void handle_toplevel_state(void *data,
        struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *state) {
    (void)handle;
    struct fbwl_toplevel *tl = data;

    tl->maximized = false;
    tl->minimized = false;
    tl->activated = false;
    tl->fullscreen = false;

    uint32_t *s;
    wl_array_for_each(s, state) {
        switch (*s) {
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
            tl->maximized = true;
            break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
            tl->minimized = true;
            break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
            tl->activated = true;
            break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
            tl->fullscreen = true;
            break;
        default:
            break;
        }
    }

    tl->dirty = true;
}

static void handle_toplevel_done(void *data,
        struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)handle;
    struct fbwl_toplevel *tl = data;
    if (tl->client == NULL) {
        return;
    }

    maybe_act_on_toplevel(tl->client, tl);
    tl->dirty = false;
}

static void handle_toplevel_closed(void *data,
        struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)handle;
    struct fbwl_toplevel *tl = data;
    struct fbwl_client *c = tl->client;
    if (c == NULL || c->done || c->cmd != FBWL_CMD_CLOSE || !c->action_sent) {
        return;
    }
    if (c->target_title == NULL || tl->title == NULL) {
        return;
    }
    if (strcmp(tl->title, c->target_title) != 0) {
        return;
    }
    c->success = true;
    c->done = true;
}

static void handle_toplevel_parent(void *data,
        struct zwlr_foreign_toplevel_handle_v1 *handle,
        struct zwlr_foreign_toplevel_handle_v1 *parent) {
    (void)data;
    (void)handle;
    (void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_listener = {
    .title = handle_toplevel_title,
    .app_id = handle_toplevel_app_id,
    .output_enter = handle_toplevel_output_enter,
    .output_leave = handle_toplevel_output_leave,
    .state = handle_toplevel_state,
    .done = handle_toplevel_done,
    .closed = handle_toplevel_closed,
    .parent = handle_toplevel_parent,
};

static void handle_manager_toplevel(void *data,
        struct zwlr_foreign_toplevel_manager_v1 *manager,
        struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)manager;
    struct fbwl_client *c = data;

    struct fbwl_toplevel *tl = calloc(1, sizeof(*tl));
    if (tl == NULL) {
        return;
    }
    tl->client = c;
    tl->handle = handle;
    wl_list_insert(&c->toplevels, &tl->link);

    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_listener, tl);
}

static void handle_manager_finished(void *data,
        struct zwlr_foreign_toplevel_manager_v1 *manager) {
    (void)data;
    (void)manager;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = handle_manager_toplevel,
    .finished = handle_manager_finished,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_client *c = data;

    if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        c->manager = wl_registry_bind(registry, name,
            &zwlr_foreign_toplevel_manager_v1_interface, v);
        zwlr_foreign_toplevel_manager_v1_add_listener(c->manager, &manager_listener, c);
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
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [--socket NAME] [--timeout-ms MS] list\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] [--timeout-ms MS] minimize TITLE\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] [--timeout-ms MS] unminimize TITLE\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] [--timeout-ms MS] close TITLE\n", argv0);
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 2000;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int copt;
    while ((copt = getopt_long(argc, argv, "h", options, NULL)) != -1) {
        switch (copt) {
        case 1:
            socket_name = optarg;
            break;
        case 2:
            timeout_ms = atoi(optarg);
            if (timeout_ms < 1) {
                timeout_ms = 1;
            }
            break;
        case 'h':
        default:
            usage(argv[0]);
            return copt == 'h' ? 0 : 1;
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[optind++];
    enum fbwl_cmd parsed = FBWL_CMD_LIST;
    const char *title = NULL;
    if (strcmp(cmd, "list") == 0) {
        parsed = FBWL_CMD_LIST;
    } else if (strcmp(cmd, "minimize") == 0) {
        parsed = FBWL_CMD_MINIMIZE;
    } else if (strcmp(cmd, "unminimize") == 0) {
        parsed = FBWL_CMD_UNMINIMIZE;
    } else if (strcmp(cmd, "close") == 0) {
        parsed = FBWL_CMD_CLOSE;
    } else {
        fprintf(stderr, "fbwl-foreign-toplevel-client: unknown command: %s\n", cmd);
        usage(argv[0]);
        return 1;
    }

    if (parsed != FBWL_CMD_LIST) {
        if (optind >= argc) {
            fprintf(stderr, "fbwl-foreign-toplevel-client: missing TITLE\n");
            usage(argv[0]);
            return 1;
        }
        title = argv[optind++];
    }
    if (optind < argc) {
        usage(argv[0]);
        return 1;
    }

    struct fbwl_client client = {0};
    client.cmd = parsed;
    client.target_title = title;
    client.action_sent = false;
    client.want_minimized = (parsed == FBWL_CMD_MINIMIZE);
    wl_list_init(&client.toplevels);

    client.display = wl_display_connect(socket_name);
    if (client.display == NULL) {
        fprintf(stderr, "fbwl-foreign-toplevel-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(env)", strerror(errno));
        cleanup(&client);
        return 1;
    }

    client.registry = wl_display_get_registry(client.display);
    wl_registry_add_listener(client.registry, &registry_listener, &client);
    if (wl_display_roundtrip(client.display) < 0) {
        fprintf(stderr, "fbwl-foreign-toplevel-client: wl_display_roundtrip failed: %s\n",
            strerror(errno));
        cleanup(&client);
        return 1;
    }

    if (client.manager == NULL) {
        fprintf(stderr, "fbwl-foreign-toplevel-client: missing zwlr_foreign_toplevel_manager_v1\n");
        cleanup(&client);
        return 1;
    }

    if (client.cmd == FBWL_CMD_LIST) {
        // Allow some extra events to arrive before printing.
        wl_display_roundtrip(client.display);
        struct fbwl_toplevel *tl;
        wl_list_for_each(tl, &client.toplevels, link) {
            maybe_act_on_toplevel(&client, tl);
        }
        cleanup(&client);
        return 0;
    }

    const int64_t deadline = now_ms() + timeout_ms;
    while (!client.done && now_ms() < deadline) {
        wl_display_dispatch_pending(client.display);
        wl_display_flush(client.display);

        int remaining = (int)(deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }

        struct pollfd pfd = {
            .fd = wl_display_get_fd(client.display),
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
            fprintf(stderr, "fbwl-foreign-toplevel-client: poll failed: %s\n", strerror(errno));
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "fbwl-foreign-toplevel-client: compositor hung up\n");
            break;
        }
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(client.display) < 0) {
                fprintf(stderr, "fbwl-foreign-toplevel-client: wl_display_dispatch failed: %s\n",
                    strerror(errno));
                break;
            }
        }
    }

    const int rc = client.success ? 0 : 1;
    cleanup(&client);
    return rc;
}
