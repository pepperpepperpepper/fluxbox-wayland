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

#include "wlr-output-power-management-unstable-v1-client-protocol.h"

struct fbwl_output {
    struct wl_list link;
    struct wl_output *output;
    uint32_t global_name;
};

struct fbwl_client {
    struct wl_display *display;
    struct wl_registry *registry;
    struct zwlr_output_power_manager_v1 *power_mgr;

    struct wl_list outputs; // fbwl_output.link
    size_t output_count;

    struct zwlr_output_power_v1 *power;
    enum zwlr_output_power_v1_mode current_mode;
    bool have_mode;
    bool failed;

    int expect_outputs;
    int target_index;
};

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static void output_handle_geometry(void *data, struct wl_output *wl_output,
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

static void output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags,
        int32_t width, int32_t height, int32_t refresh) {
    (void)data;
    (void)wl_output;
    (void)flags;
    (void)width;
    (void)height;
    (void)refresh;
}

static void output_handle_done(void *data, struct wl_output *wl_output) {
    (void)data;
    (void)wl_output;
}

static void output_handle_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    (void)data;
    (void)wl_output;
    (void)factor;
}

static void output_handle_name(void *data, struct wl_output *wl_output, const char *name) {
    (void)data;
    (void)wl_output;
    (void)name;
}

static void output_handle_description(void *data, struct wl_output *wl_output, const char *description) {
    (void)data;
    (void)wl_output;
    (void)description;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
    .name = output_handle_name,
    .description = output_handle_description,
};

static void power_handle_mode(void *data, struct zwlr_output_power_v1 *power, uint32_t mode) {
    (void)power;
    struct fbwl_client *c = data;
    c->have_mode = true;
    c->current_mode = (enum zwlr_output_power_v1_mode)mode;
}

static void power_handle_failed(void *data, struct zwlr_output_power_v1 *power) {
    (void)power;
    struct fbwl_client *c = data;
    c->failed = true;
}

static const struct zwlr_output_power_v1_listener power_listener = {
    .mode = power_handle_mode,
    .failed = power_handle_failed,
};

static void output_free(struct fbwl_output *out) {
    if (out == NULL) {
        return;
    }
    if (out->output != NULL) {
        wl_output_destroy(out->output);
    }
    free(out);
}

static void cleanup(struct fbwl_client *c) {
    if (c == NULL) {
        return;
    }
    if (c->power != NULL) {
        zwlr_output_power_v1_destroy(c->power);
        c->power = NULL;
    }
    struct fbwl_output *out, *tmp;
    wl_list_for_each_safe(out, tmp, &c->outputs, link) {
        wl_list_remove(&out->link);
        output_free(out);
    }
    if (c->power_mgr != NULL) {
        zwlr_output_power_manager_v1_destroy(c->power_mgr);
        c->power_mgr = NULL;
    }
    if (c->registry != NULL) {
        wl_registry_destroy(c->registry);
        c->registry = NULL;
    }
    if (c->display != NULL) {
        wl_display_disconnect(c->display);
        c->display = NULL;
    }
}

static struct fbwl_output *output_by_index(struct fbwl_client *c, int idx) {
    int i = 0;
    struct fbwl_output *out;
    wl_list_for_each(out, &c->outputs, link) {
        if (i == idx) {
            return out;
        }
        i++;
    }
    return NULL;
}

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_client *c = data;
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
        wl_output_add_listener(out->output, &output_listener, out);
        wl_list_insert(c->outputs.prev, &out->link);
        c->output_count++;
        return;
    }
    if (strcmp(interface, zwlr_output_power_manager_v1_interface.name) == 0) {
        uint32_t bind_version = version;
        if (bind_version > 1) {
            bind_version = 1;
        }
        c->power_mgr = wl_registry_bind(registry, name, &zwlr_output_power_manager_v1_interface, bind_version);
        return;
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)registry;
    struct fbwl_client *c = data;
    struct fbwl_output *out, *tmp;
    wl_list_for_each_safe(out, tmp, &c->outputs, link) {
        if (out->global_name == name) {
            wl_list_remove(&out->link);
            output_free(out);
            if (c->output_count > 0) {
                c->output_count--;
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
    fprintf(stderr, "Usage: %s [--socket NAME] [--timeout-ms MS] [--expect-outputs N] [--target-index I]\n", argv0);
}

static bool wait_for_mode(struct fbwl_client *c, enum zwlr_output_power_v1_mode mode, int timeout_ms) {
    const int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        wl_display_dispatch_pending(c->display);
        if (c->failed) {
            return false;
        }
        if (c->have_mode && c->current_mode == mode) {
            return true;
        }
        wl_display_flush(c->display);

        int remaining = (int)(deadline - now_ms());
        if (remaining < 0) {
            remaining = 0;
        }
        struct pollfd pfd = {
            .fd = wl_display_get_fd(c->display),
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
            if (wl_display_dispatch(c->display) < 0) {
                return false;
            }
        }
    }
    return c->have_mode && c->current_mode == mode && !c->failed;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    int timeout_ms = 4000;

    struct fbwl_client c = {0};
    wl_list_init(&c.outputs);
    c.expect_outputs = 2;
    c.target_index = 1;

    static const struct option opts[] = {
        {"socket", required_argument, NULL, 1},
        {"timeout-ms", required_argument, NULL, 2},
        {"expect-outputs", required_argument, NULL, 3},
        {"target-index", required_argument, NULL, 4},
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
            c.expect_outputs = atoi(optarg);
            break;
        case 4:
            c.target_index = atoi(optarg);
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 2;
        }
    }

    c.display = wl_display_connect(socket_name);
    if (c.display == NULL) {
        fprintf(stderr, "fbwl-output-power-client: wl_display_connect failed (WAYLAND_DISPLAY=%s): %s\n",
            socket_name != NULL ? socket_name : "(null)", strerror(errno));
        cleanup(&c);
        return 1;
    }
    c.registry = wl_display_get_registry(c.display);
    wl_registry_add_listener(c.registry, &registry_listener, &c);
    wl_display_roundtrip(c.display);

    if (c.power_mgr == NULL) {
        fprintf(stderr, "fbwl-output-power-client: compositor does not advertise zwlr_output_power_manager_v1\n");
        cleanup(&c);
        return 1;
    }
    if (c.output_count < (size_t)c.expect_outputs) {
        fprintf(stderr, "fbwl-output-power-client: expected >=%d wl_output globals, got %zu\n",
            c.expect_outputs, c.output_count);
        cleanup(&c);
        return 1;
    }

    struct fbwl_output *target = output_by_index(&c, c.target_index);
    if (target == NULL || target->output == NULL) {
        fprintf(stderr, "fbwl-output-power-client: target output index %d not found\n", c.target_index);
        cleanup(&c);
        return 1;
    }

    c.power = zwlr_output_power_manager_v1_get_output_power(c.power_mgr, target->output);
    if (c.power == NULL) {
        fprintf(stderr, "fbwl-output-power-client: get_output_power failed\n");
        cleanup(&c);
        return 1;
    }
    zwlr_output_power_v1_add_listener(c.power, &power_listener, &c);
    wl_display_flush(c.display);

    c.have_mode = false;
    c.failed = false;
    if (!wait_for_mode(&c, ZWLR_OUTPUT_POWER_V1_MODE_ON, timeout_ms)) {
        fprintf(stderr, "fbwl-output-power-client: timed out waiting for initial mode=on (failed=%d have_mode=%d mode=%u)\n",
            (int)c.failed, (int)c.have_mode, (unsigned)c.current_mode);
        cleanup(&c);
        return 1;
    }

    c.have_mode = false;
    zwlr_output_power_v1_set_mode(c.power, ZWLR_OUTPUT_POWER_V1_MODE_OFF);
    wl_display_flush(c.display);
    if (!wait_for_mode(&c, ZWLR_OUTPUT_POWER_V1_MODE_OFF, timeout_ms)) {
        fprintf(stderr, "fbwl-output-power-client: timed out waiting for mode=off\n");
        cleanup(&c);
        return 1;
    }

    c.have_mode = false;
    zwlr_output_power_v1_set_mode(c.power, ZWLR_OUTPUT_POWER_V1_MODE_ON);
    wl_display_flush(c.display);
    if (!wait_for_mode(&c, ZWLR_OUTPUT_POWER_V1_MODE_ON, timeout_ms)) {
        fprintf(stderr, "fbwl-output-power-client: timed out waiting for mode=on\n");
        cleanup(&c);
        return 1;
    }

    printf("ok output-power toggled index=%d off->on\n", c.target_index);
    cleanup(&c);
    return 0;
}

