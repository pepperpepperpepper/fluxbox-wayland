#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>

#include <systemd/sd-bus.h>

static void usage(const char *argv0) {
    printf("Usage: %s [--watcher NAME] [--item-path PATH] [--stay-ms MS]\n", argv0);
    printf("          [--id ID]\n");
    printf("          [--activate-mark FILE] [--secondary-activate-mark FILE] [--context-menu-mark FILE]\n");
    printf("          [--icon-rgba #RRGGBB[AA]] [--icon-size N] [--icon-name NAME]\n");
    printf("          [--icon-theme-path PATH]\n");
    printf("          [--status Active|Passive|NeedsAttention]\n");
    printf("          [--attention-icon-rgba #RRGGBB[AA]] [--attention-icon-size N] [--attention-icon-name NAME]\n");
    printf("          [--overlay-icon-rgba #RRGGBB[AA]] [--overlay-icon-size N] [--overlay-icon-name NAME]\n");
    printf("          [--update-icon-rgba #RRGGBB[AA]] [--update-icon-on-activate]\n");
    printf("\n");
    printf("Defaults:\n");
    printf("  --watcher   org.kde.StatusNotifierWatcher\n");
    printf("  --item-path /fbwl/TestItem\n");
}

struct item_service {
    const char *item_path;
    const char *id;
    const char *activate_mark;
    const char *secondary_activate_mark;
    const char *context_menu_mark;
    const char *status;
    const char *icon_name;
    const char *icon_theme_path;
    const char *attention_icon_name;
    const char *overlay_icon_name;
    int icon_w;
    int icon_h;
    size_t icon_len;
    uint8_t *icon_argb;
    int attention_icon_w;
    int attention_icon_h;
    size_t attention_icon_len;
    uint8_t *attention_icon_argb;
    int overlay_icon_w;
    int overlay_icon_h;
    size_t overlay_icon_len;
    uint8_t *overlay_icon_argb;
    bool update_on_activate;
    bool update_done;
    bool update_has_color;
    uint8_t update_r;
    uint8_t update_g;
    uint8_t update_b;
    uint8_t update_a;
};

static uint64_t now_usec(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void touch_file(const char *path) {
    if (path == NULL || *path == '\0') {
        return;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        close(fd);
    }
}

static int item_method_activate(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct item_service *svc = userdata;
    int x = 0;
    int y = 0;
    int r = sd_bus_message_read(m, "ii", &x, &y);
    if (r < 0) {
        return r;
    }
    (void)x;
    (void)y;
    if (svc != NULL) {
        touch_file(svc->activate_mark);

        if (svc->update_on_activate && svc->update_has_color && !svc->update_done &&
                svc->icon_argb != NULL && svc->icon_w > 0 && svc->icon_h > 0 && svc->icon_len > 0) {
            svc->update_done = true;
            for (size_t i = 0; i + 3 < svc->icon_len; i += 4) {
                svc->icon_argb[i + 0] = svc->update_a;
                svc->icon_argb[i + 1] = svc->update_r;
                svc->icon_argb[i + 2] = svc->update_g;
                svc->icon_argb[i + 3] = svc->update_b;
            }

            sd_bus *bus = sd_bus_message_get_bus(m);
            if (bus != NULL && svc->item_path != NULL) {
                (void)sd_bus_emit_properties_changed(bus, svc->item_path, "org.kde.StatusNotifierItem", "IconPixmap",
                    NULL);
                (void)sd_bus_emit_signal(bus, svc->item_path, "org.kde.StatusNotifierItem", "NewIcon", "");
            }
        }
    }
    return sd_bus_reply_method_return(m, "");
}

static int item_method_secondary_activate(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct item_service *svc = userdata;
    int x = 0;
    int y = 0;
    int r = sd_bus_message_read(m, "ii", &x, &y);
    if (r < 0) {
        return r;
    }
    (void)x;
    (void)y;
    if (svc != NULL) {
        touch_file(svc->secondary_activate_mark);
    }
    return sd_bus_reply_method_return(m, "");
}

static int item_method_context_menu(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct item_service *svc = userdata;
    int x = 0;
    int y = 0;
    int r = sd_bus_message_read(m, "ii", &x, &y);
    if (r < 0) {
        return r;
    }
    (void)x;
    (void)y;
    if (svc != NULL) {
        touch_file(svc->context_menu_mark);
    }
    return sd_bus_reply_method_return(m, "");
}

static int item_prop_icon_pixmap(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;

    int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) {
        return r;
    }

    if (svc != NULL && svc->icon_argb != NULL && svc->icon_w > 0 && svc->icon_h > 0 && svc->icon_len > 0) {
        r = sd_bus_message_open_container(reply, 'r', "iiay");
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append(reply, "ii", svc->icon_w, svc->icon_h);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append_array(reply, 'y', svc->icon_argb, svc->icon_len);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_close_container(reply);
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int item_prop_icon_name(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;
    return sd_bus_message_append(reply, "s", svc != NULL && svc->icon_name != NULL ? svc->icon_name : "");
}

static int item_prop_icon_theme_path(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;
    return sd_bus_message_append(reply, "s", svc != NULL && svc->icon_theme_path != NULL ? svc->icon_theme_path : "");
}

static int item_prop_attention_icon_pixmap(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;

    int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) {
        return r;
    }

    if (svc != NULL && svc->attention_icon_argb != NULL && svc->attention_icon_w > 0 && svc->attention_icon_h > 0 &&
            svc->attention_icon_len > 0) {
        r = sd_bus_message_open_container(reply, 'r', "iiay");
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append(reply, "ii", svc->attention_icon_w, svc->attention_icon_h);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append_array(reply, 'y', svc->attention_icon_argb, svc->attention_icon_len);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_close_container(reply);
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int item_prop_attention_icon_name(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;
    return sd_bus_message_append(reply, "s",
        svc != NULL && svc->attention_icon_name != NULL ? svc->attention_icon_name : "");
}

static int item_prop_overlay_icon_pixmap(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;

    int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) {
        return r;
    }

    if (svc != NULL && svc->overlay_icon_argb != NULL && svc->overlay_icon_w > 0 && svc->overlay_icon_h > 0 &&
            svc->overlay_icon_len > 0) {
        r = sd_bus_message_open_container(reply, 'r', "iiay");
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append(reply, "ii", svc->overlay_icon_w, svc->overlay_icon_h);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_append_array(reply, 'y', svc->overlay_icon_argb, svc->overlay_icon_len);
        if (r < 0) {
            return r;
        }

        r = sd_bus_message_close_container(reply);
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int item_prop_overlay_icon_name(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;
    return sd_bus_message_append(reply, "s", svc != NULL && svc->overlay_icon_name != NULL ? svc->overlay_icon_name : "");
}

static int item_prop_status(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;
    return sd_bus_message_append(reply, "s", svc != NULL && svc->status != NULL ? svc->status : "Active");
}

static int item_prop_id(sd_bus *bus, const char *path, const char *interface, const char *property,
        sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;
    struct item_service *svc = userdata;
    return sd_bus_message_append(reply, "s", svc != NULL && svc->id != NULL ? svc->id : "");
}

static const sd_bus_vtable item_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Activate", "ii", "", item_method_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", item_method_secondary_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ContextMenu", "ii", "", item_method_context_menu, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Status", "s", item_prop_status, 0, 0),
    SD_BUS_PROPERTY("Id", "s", item_prop_id, 0, 0),
    SD_BUS_PROPERTY("IconName", "s", item_prop_icon_name, 0, 0),
    SD_BUS_PROPERTY("IconThemePath", "s", item_prop_icon_theme_path, 0, 0),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", item_prop_icon_pixmap, 0, 0),
    SD_BUS_PROPERTY("AttentionIconName", "s", item_prop_attention_icon_name, 0, 0),
    SD_BUS_PROPERTY("AttentionIconPixmap", "a(iiay)", item_prop_attention_icon_pixmap, 0, 0),
    SD_BUS_PROPERTY("OverlayIconName", "s", item_prop_overlay_icon_name, 0, 0),
    SD_BUS_PROPERTY("OverlayIconPixmap", "a(iiay)", item_prop_overlay_icon_pixmap, 0, 0),
    SD_BUS_VTABLE_END,
};

static bool strv_contains(char **items, const char *needle) {
    if (items == NULL || needle == NULL) {
        return false;
    }
    for (size_t i = 0; items[i] != NULL; i++) {
        if (strcmp(items[i], needle) == 0) {
            return true;
        }
    }
    return false;
}

static void free_strv(char **items) {
    if (items == NULL) {
        return;
    }
    for (size_t i = 0; items[i] != NULL; i++) {
        free(items[i]);
    }
    free(items);
}

static int parse_hex_u8(const char *s, uint8_t *out) {
    if (s == NULL || out == NULL) {
        return -1;
    }

    char tmp[3] = {0};
    tmp[0] = s[0];
    tmp[1] = s[1];

    char *end = NULL;
    unsigned long v = strtoul(tmp, &end, 16);
    if (end == NULL || *end != '\0' || v > 255) {
        return -1;
    }

    *out = (uint8_t)v;
    return 0;
}

static int parse_rgba(const char *s, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    if (s == NULL || r == NULL || g == NULL || b == NULL || a == NULL) {
        return -1;
    }

    if (s[0] != '#') {
        return -1;
    }

    const size_t n = strlen(s);
    if (n != 7 && n != 9) {
        return -1;
    }

    if (parse_hex_u8(s + 1, r) != 0 || parse_hex_u8(s + 3, g) != 0 || parse_hex_u8(s + 5, b) != 0) {
        return -1;
    }

    uint8_t alpha = 255;
    if (n == 9) {
        if (parse_hex_u8(s + 7, &alpha) != 0) {
            return -1;
        }
    }

    *a = alpha;
    return 0;
}

static int alloc_solid_argb(uint8_t **out_argb, size_t *out_len, int *out_w, int *out_h, int size, uint8_t r,
        uint8_t g, uint8_t b, uint8_t a) {
    if (out_argb == NULL || out_len == NULL || out_w == NULL || out_h == NULL || size < 1) {
        return -1;
    }

    const size_t len = (size_t)size * (size_t)size * 4;
    if (len / 4 != (size_t)size * (size_t)size) {
        return -1;
    }

    uint8_t *buf = malloc(len);
    if (buf == NULL) {
        return -1;
    }

    for (size_t i = 0; i + 3 < len; i += 4) {
        buf[i + 0] = a;
        buf[i + 1] = r;
        buf[i + 2] = g;
        buf[i + 3] = b;
    }

    *out_argb = buf;
    *out_len = len;
    *out_w = size;
    *out_h = size;
    return 0;
}

int main(int argc, char **argv) {
    const char *watcher_name = "org.kde.StatusNotifierWatcher";
    const char *item_path = "/fbwl/TestItem";
    const char *item_id = "";
    int stay_ms = 0;
    const char *activate_mark = NULL;
    const char *secondary_activate_mark = NULL;
    const char *context_menu_mark = NULL;
    const char *icon_rgba = NULL;
    int icon_size = 16;
    const char *icon_name = NULL;
    const char *icon_theme_path = NULL;
    const char *status = "Active";
    const char *attention_icon_rgba = NULL;
    int attention_icon_size = 16;
    const char *attention_icon_name = NULL;
    const char *overlay_icon_rgba = NULL;
    int overlay_icon_size = 16;
    const char *overlay_icon_name = NULL;
    const char *update_icon_rgba = NULL;
    bool update_icon_on_activate = false;

    static const struct option options[] = {
        {"watcher", required_argument, NULL, 1},
        {"item-path", required_argument, NULL, 2},
        {"stay-ms", required_argument, NULL, 3},
        {"activate-mark", required_argument, NULL, 4},
        {"secondary-activate-mark", required_argument, NULL, 5},
        {"context-menu-mark", required_argument, NULL, 6},
        {"icon-rgba", required_argument, NULL, 7},
        {"icon-size", required_argument, NULL, 8},
        {"update-icon-rgba", required_argument, NULL, 9},
        {"update-icon-on-activate", no_argument, NULL, 10},
        {"icon-name", required_argument, NULL, 11},
        {"icon-theme-path", required_argument, NULL, 12},
        {"status", required_argument, NULL, 13},
        {"attention-icon-rgba", required_argument, NULL, 14},
        {"attention-icon-size", required_argument, NULL, 15},
        {"attention-icon-name", required_argument, NULL, 16},
        {"overlay-icon-rgba", required_argument, NULL, 17},
        {"overlay-icon-size", required_argument, NULL, 18},
        {"overlay-icon-name", required_argument, NULL, 19},
        {"id", required_argument, NULL, 20},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "h", options, NULL)) != -1) {
        switch (c) {
        case 1:
            watcher_name = optarg;
            break;
        case 2:
            item_path = optarg;
            break;
        case 3:
            stay_ms = atoi(optarg);
            if (stay_ms < 0) {
                stay_ms = 0;
            }
            break;
        case 4:
            activate_mark = optarg;
            break;
        case 5:
            secondary_activate_mark = optarg;
            break;
        case 6:
            context_menu_mark = optarg;
            break;
        case 7:
            icon_rgba = optarg;
            break;
        case 8:
            icon_size = atoi(optarg);
            if (icon_size < 1) {
                icon_size = 1;
            }
            if (icon_size > 1024) {
                icon_size = 1024;
            }
            break;
        case 9:
            update_icon_rgba = optarg;
            break;
        case 10:
            update_icon_on_activate = true;
            break;
        case 11:
            icon_name = optarg;
            break;
        case 12:
            icon_theme_path = optarg;
            break;
        case 13:
            status = optarg;
            break;
        case 14:
            attention_icon_rgba = optarg;
            break;
        case 15:
            attention_icon_size = atoi(optarg);
            if (attention_icon_size < 1) {
                attention_icon_size = 1;
            }
            if (attention_icon_size > 1024) {
                attention_icon_size = 1024;
            }
            break;
        case 16:
            attention_icon_name = optarg;
            break;
        case 17:
            overlay_icon_rgba = optarg;
            break;
        case 18:
            overlay_icon_size = atoi(optarg);
            if (overlay_icon_size < 1) {
                overlay_icon_size = 1;
            }
            if (overlay_icon_size > 1024) {
                overlay_icon_size = 1024;
            }
            break;
        case 19:
            overlay_icon_name = optarg;
            break;
        case 20:
            item_id = optarg;
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

    if (item_path == NULL || item_path[0] != '/') {
        fprintf(stderr, "fbwl-sni-item-client: --item-path must start with '/'\n");
        return 1;
    }

    struct item_service svc = {
        .item_path = item_path,
        .id = item_id,
        .activate_mark = activate_mark,
        .secondary_activate_mark = secondary_activate_mark,
        .context_menu_mark = context_menu_mark,
        .status = status,
        .icon_name = icon_name,
        .icon_theme_path = icon_theme_path,
        .attention_icon_name = attention_icon_name,
        .overlay_icon_name = overlay_icon_name,
    };

    if (icon_rgba != NULL) {
        uint8_t r = 0, g = 0, b = 0, a = 0;
        if (parse_rgba(icon_rgba, &r, &g, &b, &a) != 0) {
            fprintf(stderr, "fbwl-sni-item-client: --icon-rgba must be #RRGGBB or #RRGGBBAA\n");
            return 1;
        }
        if (alloc_solid_argb(&svc.icon_argb, &svc.icon_len, &svc.icon_w, &svc.icon_h, icon_size, r, g, b, a) != 0) {
            fprintf(stderr, "fbwl-sni-item-client: icon alloc failed\n");
            return 1;
        }
    }

    if (attention_icon_rgba != NULL) {
        uint8_t r = 0, g = 0, b = 0, a = 0;
        if (parse_rgba(attention_icon_rgba, &r, &g, &b, &a) != 0) {
            fprintf(stderr, "fbwl-sni-item-client: --attention-icon-rgba must be #RRGGBB or #RRGGBBAA\n");
            free(svc.icon_argb);
            return 1;
        }
        if (alloc_solid_argb(&svc.attention_icon_argb, &svc.attention_icon_len, &svc.attention_icon_w,
                &svc.attention_icon_h, attention_icon_size, r, g, b, a) != 0) {
            fprintf(stderr, "fbwl-sni-item-client: attention icon alloc failed\n");
            free(svc.icon_argb);
            return 1;
        }
    }

    if (overlay_icon_rgba != NULL) {
        uint8_t r = 0, g = 0, b = 0, a = 0;
        if (parse_rgba(overlay_icon_rgba, &r, &g, &b, &a) != 0) {
            fprintf(stderr, "fbwl-sni-item-client: --overlay-icon-rgba must be #RRGGBB or #RRGGBBAA\n");
            free(svc.attention_icon_argb);
            free(svc.icon_argb);
            return 1;
        }
        if (alloc_solid_argb(&svc.overlay_icon_argb, &svc.overlay_icon_len, &svc.overlay_icon_w, &svc.overlay_icon_h,
                overlay_icon_size, r, g, b, a) != 0) {
            fprintf(stderr, "fbwl-sni-item-client: overlay icon alloc failed\n");
            free(svc.attention_icon_argb);
            free(svc.icon_argb);
            return 1;
        }
    }

    if (update_icon_rgba != NULL) {
        uint8_t r = 0, g = 0, b = 0, a = 0;
        if (parse_rgba(update_icon_rgba, &r, &g, &b, &a) != 0) {
            fprintf(stderr, "fbwl-sni-item-client: --update-icon-rgba must be #RRGGBB or #RRGGBBAA\n");
            free(svc.overlay_icon_argb);
            free(svc.attention_icon_argb);
            free(svc.icon_argb);
            return 1;
        }
        svc.update_has_color = true;
        svc.update_r = r;
        svc.update_g = g;
        svc.update_b = b;
        svc.update_a = a;
    }
    svc.update_on_activate = update_icon_on_activate;

    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    sd_bus_slot *slot_item = NULL;

    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "fbwl-sni-item-client: sd_bus_open_user failed: %s\n", strerror(-r));
        free(svc.overlay_icon_argb);
        free(svc.attention_icon_argb);
        free(svc.icon_argb);
        return 1;
    }

    const char *unique = NULL;
    r = sd_bus_get_unique_name(bus, &unique);
    if (r < 0 || unique == NULL) {
        fprintf(stderr, "fbwl-sni-item-client: sd_bus_get_unique_name failed: %s\n", strerror(-r));
        sd_bus_unref(bus);
        free(svc.overlay_icon_argb);
        free(svc.attention_icon_argb);
        free(svc.icon_argb);
        return 1;
    }

    r = sd_bus_add_object_vtable(bus, &slot_item, item_path, "org.kde.StatusNotifierItem", item_vtable, &svc);
    if (r < 0) {
        fprintf(stderr, "fbwl-sni-item-client: add item vtable failed: %s\n", strerror(-r));
        sd_bus_unref(bus);
        free(svc.overlay_icon_argb);
        free(svc.attention_icon_argb);
        free(svc.icon_argb);
        return 1;
    }

    r = sd_bus_call_method(bus, watcher_name, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem", &error, &reply, "s", item_path);
    sd_bus_message_unref(reply);
    reply = NULL;
    if (r < 0) {
        fprintf(stderr, "fbwl-sni-item-client: RegisterStatusNotifierItem failed: %s: %s\n",
            error.name != NULL ? error.name : "err", error.message != NULL ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        free(svc.overlay_icon_argb);
        free(svc.attention_icon_argb);
        free(svc.icon_argb);
        return 1;
    }
    sd_bus_error_free(&error);

    char expected[256];
    int n = snprintf(expected, sizeof(expected), "%s%s", unique, item_path);
    if (n < 0 || (size_t)n >= sizeof(expected)) {
        fprintf(stderr, "fbwl-sni-item-client: expected id too long\n");
        sd_bus_unref(bus);
        free(svc.overlay_icon_argb);
        free(svc.attention_icon_argb);
        free(svc.icon_argb);
        return 1;
    }

    char **items = NULL;
    r = sd_bus_get_property_strv(bus, watcher_name, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
        "RegisteredStatusNotifierItems", &error, &items);
    if (r < 0) {
        fprintf(stderr, "fbwl-sni-item-client: get RegisteredStatusNotifierItems failed: %s: %s\n",
            error.name != NULL ? error.name : "err", error.message != NULL ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        free(svc.overlay_icon_argb);
        free(svc.attention_icon_argb);
        free(svc.icon_argb);
        return 1;
    }
    sd_bus_error_free(&error);

    if (!strv_contains(items, expected)) {
        fprintf(stderr, "fbwl-sni-item-client: expected id not registered: %s\n", expected);
        for (size_t i = 0; items[i] != NULL; i++) {
            fprintf(stderr, "  registered: %s\n", items[i]);
        }
        sd_bus_message_unref(reply);
        free_strv(items);
        sd_bus_unref(bus);
        free(svc.overlay_icon_argb);
        free(svc.attention_icon_argb);
        free(svc.icon_argb);
        return 1;
    }
    free_strv(items);

    printf("ok sni registered id=%s\n", expected);
    fflush(stdout);

    if (stay_ms > 0) {
        uint64_t deadline = now_usec() + (uint64_t)stay_ms * 1000ULL;
        for (;;) {
            for (;;) {
                r = sd_bus_process(bus, NULL);
                if (r < 0) {
                    fprintf(stderr, "fbwl-sni-item-client: sd_bus_process failed: %s\n", strerror(-r));
                    deadline = 0;
                    break;
                }
                if (r == 0) {
                    break;
                }
            }

            uint64_t now = now_usec();
            if (deadline == 0 || now >= deadline) {
                break;
            }

            uint64_t remaining = deadline - now;
            r = sd_bus_wait(bus, remaining);
            if (r < 0) {
                fprintf(stderr, "fbwl-sni-item-client: sd_bus_wait failed: %s\n", strerror(-r));
                break;
            }
        }
    }

    sd_bus_slot_unref(slot_item);
    sd_bus_unref(bus);
    free(svc.overlay_icon_argb);
    free(svc.attention_icon_argb);
    free(svc.icon_argb);
    return 0;
}
