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

#include "wlr-output-management-unstable-v1-client-protocol.h"

struct fbwl_mode {
    struct wl_list link;
    struct zwlr_output_mode_v1 *mode;
};

struct fbwl_head {
    struct wl_list link;
    struct zwlr_output_head_v1 *head;

    struct wl_list modes; // fbwl_mode.link
    struct zwlr_output_mode_v1 *current_mode;

    char *name;
    bool enabled;
    int32_t x;
    int32_t y;

    bool have_name;
    bool have_enabled;
    bool have_position;
};

struct fbwl_client {
    struct wl_display *display;
    struct wl_registry *registry;
    struct zwlr_output_manager_v1 *manager;

    struct wl_list heads; // fbwl_head.link
    size_t head_count;

    uint32_t serial;
    bool got_done;

    struct zwlr_output_configuration_v1 *config;
    bool config_done;
    bool config_success;

    bool verify_done;

    uint32_t initial_serial;
    int expect_heads;
    int target_index;
    int delta_y;

    int32_t expected_x;
    int32_t expected_y;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static void mode_free(struct fbwl_mode *m) {
    if (m == NULL) {
        return;
    }
    if (m->mode != NULL) {
        zwlr_output_mode_v1_release(m->mode);
    }
    free(m);
}

static void head_free(struct fbwl_head *h) {
    if (h == NULL) {
        return;
    }
    struct fbwl_mode *m, *tmp;
    wl_list_for_each_safe(m, tmp, &h->modes, link) {
        wl_list_remove(&m->link);
        mode_free(m);
    }
    free(h->name);
    if (h->head != NULL) {
        zwlr_output_head_v1_release(h->head);
    }
    free(h);
}

static void cleanup(struct fbwl_client *c) {
    if (c == NULL) {
        return;
    }
    struct fbwl_head *h, *tmp;
    wl_list_for_each_safe(h, tmp, &c->heads, link) {
        wl_list_remove(&h->link);
        head_free(h);
    }
    if (c->config != NULL) {
        zwlr_output_configuration_v1_destroy(c->config);
        c->config = NULL;
    }
    if (c->manager != NULL) {
        zwlr_output_manager_v1_destroy(c->manager);
    }
    if (c->registry != NULL) {
        wl_registry_destroy(c->registry);
    }
    if (c->display != NULL) {
        wl_display_disconnect(c->display);
    }
}

static struct fbwl_head *head_by_index(struct fbwl_client *c, int idx) {
    int i = 0;
    struct fbwl_head *h;
    wl_list_for_each(h, &c->heads, link) {
        if (i == idx) {
            return h;
        }
        i++;
    }
    return NULL;
}

static bool have_enough_head_info(struct fbwl_client *c) {
    if (c->head_count < (size_t)c->expect_heads) {
        return false;
    }
    struct fbwl_head *h;
    wl_list_for_each(h, &c->heads, link) {
        if (!h->have_enabled || !h->have_name) {
            return false;
        }
        if (h->enabled && !h->have_position) {
            return false;
        }
        if (h->enabled && h->current_mode == NULL) {
            return false;
        }
    }
    return true;
}

static void mode_handle_size(void *data, struct zwlr_output_mode_v1 *mode,
        int32_t width, int32_t height) {
    (void)data;
    (void)mode;
    (void)width;
    (void)height;
}

static void mode_handle_refresh(void *data, struct zwlr_output_mode_v1 *mode, int32_t refresh) {
    (void)data;
    (void)mode;
    (void)refresh;
}

static void mode_handle_preferred(void *data, struct zwlr_output_mode_v1 *mode) {
    (void)data;
    (void)mode;
}

static void mode_handle_finished(void *data, struct zwlr_output_mode_v1 *mode) {
    struct fbwl_mode *m = data;
    (void)mode;
    if (m == NULL) {
        return;
    }
    wl_list_remove(&m->link);
    mode_free(m);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
    .size = mode_handle_size,
    .refresh = mode_handle_refresh,
    .preferred = mode_handle_preferred,
    .finished = mode_handle_finished,
};

static void head_handle_name(void *data, struct zwlr_output_head_v1 *head, const char *name) {
    (void)head;
    struct fbwl_head *h = data;
    free(h->name);
    h->name = name != NULL ? strdup(name) : NULL;
    h->have_name = h->name != NULL;
}

static void head_handle_description(void *data, struct zwlr_output_head_v1 *head, const char *description) {
    (void)data;
    (void)head;
    (void)description;
}

static void head_handle_physical_size(void *data, struct zwlr_output_head_v1 *head,
        int32_t width, int32_t height) {
    (void)data;
    (void)head;
    (void)width;
    (void)height;
}

static void head_handle_mode(void *data, struct zwlr_output_head_v1 *head, struct zwlr_output_mode_v1 *mode) {
    (void)head;
    struct fbwl_head *h = data;
    struct fbwl_mode *m = calloc(1, sizeof(*m));
    if (m == NULL) {
        return;
    }
    m->mode = mode;
    wl_list_insert(&h->modes, &m->link);
    zwlr_output_mode_v1_add_listener(mode, &mode_listener, m);
}

static void head_handle_enabled(void *data, struct zwlr_output_head_v1 *head, int32_t enabled) {
    (void)head;
    struct fbwl_head *h = data;
    h->enabled = enabled != 0;
    h->have_enabled = true;
}

static void head_handle_current_mode(void *data, struct zwlr_output_head_v1 *head,
        struct zwlr_output_mode_v1 *mode) {
    (void)head;
    struct fbwl_head *h = data;
    h->current_mode = mode;
}

static void head_handle_position(void *data, struct zwlr_output_head_v1 *head, int32_t x, int32_t y) {
    (void)head;
    struct fbwl_head *h = data;
    h->x = x;
    h->y = y;
    h->have_position = true;
}

static void head_handle_transform(void *data, struct zwlr_output_head_v1 *head, int32_t transform) {
    (void)data;
    (void)head;
    (void)transform;
}

static void head_handle_scale(void *data, struct zwlr_output_head_v1 *head, wl_fixed_t scale) {
    (void)data;
    (void)head;
    (void)scale;
}

static void head_handle_finished(void *data, struct zwlr_output_head_v1 *head) {
    (void)head;
    struct fbwl_head *h = data;
    if (h == NULL) {
        return;
    }
    wl_list_remove(&h->link);
    head_free(h);
}

static void head_handle_make(void *data, struct zwlr_output_head_v1 *head, const char *make) {
    (void)data;
    (void)head;
    (void)make;
}

static void head_handle_model(void *data, struct zwlr_output_head_v1 *head, const char *model) {
    (void)data;
    (void)head;
    (void)model;
}

static void head_handle_serial_number(void *data, struct zwlr_output_head_v1 *head, const char *serial_number) {
    (void)data;
    (void)head;
    (void)serial_number;
}

static void head_handle_adaptive_sync(void *data, struct zwlr_output_head_v1 *head, uint32_t state) {
    (void)data;
    (void)head;
    (void)state;
}

static const struct zwlr_output_head_v1_listener head_listener = {
    .name = head_handle_name,
    .description = head_handle_description,
    .physical_size = head_handle_physical_size,
    .mode = head_handle_mode,
    .enabled = head_handle_enabled,
    .current_mode = head_handle_current_mode,
    .position = head_handle_position,
    .transform = head_handle_transform,
    .scale = head_handle_scale,
    .finished = head_handle_finished,
    .make = head_handle_make,
    .model = head_handle_model,
    .serial_number = head_handle_serial_number,
    .adaptive_sync = head_handle_adaptive_sync,
};

static void config_handle_succeeded(void *data, struct zwlr_output_configuration_v1 *config) {
    struct fbwl_client *c = data;
    (void)config;
    c->config_done = true;
    c->config_success = true;
    if (c->config != NULL) {
        zwlr_output_configuration_v1_destroy(c->config);
        c->config = NULL;
    }
}

static void config_handle_failed(void *data, struct zwlr_output_configuration_v1 *config) {
    struct fbwl_client *c = data;
    (void)config;
    c->config_done = true;
    c->config_success = false;
    if (c->config != NULL) {
        zwlr_output_configuration_v1_destroy(c->config);
        c->config = NULL;
    }
}

static void config_handle_cancelled(void *data, struct zwlr_output_configuration_v1 *config) {
    struct fbwl_client *c = data;
    (void)config;
    c->config_done = true;
    c->config_success = false;
    if (c->config != NULL) {
        zwlr_output_configuration_v1_destroy(c->config);
        c->config = NULL;
    }
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
    .succeeded = config_handle_succeeded,
    .failed = config_handle_failed,
    .cancelled = config_handle_cancelled,
};

static void manager_handle_head(void *data, struct zwlr_output_manager_v1 *manager,
        struct zwlr_output_head_v1 *head) {
    (void)manager;
    struct fbwl_client *c = data;
    struct fbwl_head *h = calloc(1, sizeof(*h));
    if (h == NULL) {
        return;
    }
    h->head = head;
    wl_list_init(&h->modes);
    wl_list_insert(&c->heads, &h->link);
    c->head_count++;

    zwlr_output_head_v1_add_listener(head, &head_listener, h);
}

static void manager_handle_done(void *data, struct zwlr_output_manager_v1 *manager, uint32_t serial) {
    (void)manager;
    struct fbwl_client *c = data;
    c->serial = serial;
    c->got_done = true;
}

static void manager_handle_finished(void *data, struct zwlr_output_manager_v1 *manager) {
    (void)manager;
    struct fbwl_client *c = data;
    c->config_done = true;
    c->config_success = false;
}

static const struct zwlr_output_manager_v1_listener manager_listener = {
    .head = manager_handle_head,
    .done = manager_handle_done,
    .finished = manager_handle_finished,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_client *c = data;
    (void)version;

    if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        c->manager = wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, v);
        zwlr_output_manager_v1_add_listener(c->manager, &manager_listener, c);
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
    fprintf(stderr,
        "Usage: %s [--socket NAME] [--timeout-ms MS] [--expect-heads N] [--target-index I] [--delta-y DY]\n",
        argv0);
}

static bool send_apply(struct fbwl_client *c) {
    if (c->manager == NULL || !c->got_done || !have_enough_head_info(c)) {
        return false;
    }

    struct fbwl_head *target = head_by_index(c, c->target_index);
    if (target == NULL || !target->enabled) {
        fprintf(stderr, "fbwl-output-management-client: target head index=%d unavailable or disabled\n",
            c->target_index);
        return false;
    }

    c->initial_serial = c->serial;
    c->expected_x = target->x;
    c->expected_y = target->y + c->delta_y;

    c->config = zwlr_output_manager_v1_create_configuration(c->manager, c->serial);
    if (c->config == NULL) {
        fprintf(stderr, "fbwl-output-management-client: failed to create configuration\n");
        return false;
    }
    zwlr_output_configuration_v1_add_listener(c->config, &config_listener, c);

    int idx = 0;
    struct fbwl_head *h;
    wl_list_for_each(h, &c->heads, link) {
        if (!h->enabled) {
            zwlr_output_configuration_v1_disable_head(c->config, h->head);
            idx++;
            continue;
        }

        struct zwlr_output_configuration_head_v1 *ch =
            zwlr_output_configuration_v1_enable_head(c->config, h->head);
        if (ch == NULL) {
            fprintf(stderr, "fbwl-output-management-client: enable_head failed\n");
            return false;
        }
        if (h->current_mode != NULL) {
            zwlr_output_configuration_head_v1_set_mode(ch, h->current_mode);
        }

        int32_t x = h->x;
        int32_t y = h->y;
        if (idx == c->target_index) {
            x = c->expected_x;
            y = c->expected_y;
        }
        zwlr_output_configuration_head_v1_set_position(ch, x, y);
        idx++;
    }

    zwlr_output_configuration_v1_apply(c->config);
    wl_display_flush(c->display);
    return true;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 4000;

    struct fbwl_client c = {0};
    wl_list_init(&c.heads);
    c.expect_heads = 2;
    c.target_index = 1;
    c.delta_y = 120;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"expect-heads", required_argument, NULL, 3},
        {"target-index", required_argument, NULL, 4},
        {"delta-y", required_argument, NULL, 5},
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
            c.expect_heads = atoi(optarg);
            break;
        case 4:
            c.target_index = atoi(optarg);
            break;
        case 5:
            c.delta_y = atoi(optarg);
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 2;
        }
    }

    c.display = wl_display_connect(socket_name);
    if (c.display == NULL) {
        fprintf(stderr, "fbwl-output-management-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(null)", strerror(errno));
        return 1;
    }
    c.registry = wl_display_get_registry(c.display);
    wl_registry_add_listener(c.registry, &registry_listener, &c);
    wl_display_roundtrip(c.display);

    const int64_t deadline = now_ms() + timeout_ms;
    bool apply_sent = false;

    while (now_ms() < deadline) {
        wl_display_dispatch_pending(c.display);
        wl_display_flush(c.display);

        if (c.manager == NULL) {
            // keep waiting for globals
        } else if (!apply_sent && c.got_done && have_enough_head_info(&c)) {
            apply_sent = send_apply(&c);
            if (!apply_sent) {
                cleanup(&c);
                return 1;
            }
        } else if (apply_sent && c.config_done && !c.config_success) {
            cleanup(&c);
            fprintf(stderr, "fbwl-output-management-client: apply failed/cancelled\n");
            return 1;
        } else if (apply_sent && c.config_done && c.config_success && c.got_done && c.serial != c.initial_serial) {
            struct fbwl_head *target = head_by_index(&c, c.target_index);
            if (target != NULL && target->enabled && target->have_position &&
                    target->x == c.expected_x && target->y == c.expected_y) {
                printf("ok output-management moved index=%d name=%s x=%d y=%d\n",
                    c.target_index,
                    target->name != NULL ? target->name : "(no-name)",
                    target->x, target->y);
                cleanup(&c);
                return 0;
            }
        }

        struct pollfd pfd = {
            .fd = wl_display_get_fd(c.display),
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
        if (ret < 0) {
            fprintf(stderr, "fbwl-output-management-client: poll failed: %s\n", strerror(errno));
            break;
        }
        if (ret == 0) {
            break;
        }
        if (wl_display_dispatch(c.display) < 0) {
            fprintf(stderr, "fbwl-output-management-client: wl_display_dispatch failed\n");
            break;
        }
    }

    cleanup(&c);
    fprintf(stderr, "fbwl-output-management-client: timed out\n");
    return 1;
}
