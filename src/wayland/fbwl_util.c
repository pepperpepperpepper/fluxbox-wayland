#include "wayland/fbwl_util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>

void fbwl_cleanup_listener(struct wl_listener *listener) {
    if (listener->link.prev != NULL && listener->link.next != NULL) {
        wl_list_remove(&listener->link);
        listener->link.prev = NULL;
        listener->link.next = NULL;
    }
}

void fbwl_cleanup_fd(int *fd) {
    if (fd != NULL && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

bool fbwl_parse_hex_color(const char *s, float rgba[static 4]) {
    if (s == NULL || rgba == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '#') {
        s++;
    }

    const size_t len = strlen(s);
    if (len != 6 && len != 8) {
        return false;
    }

    uint32_t comps[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i])) {
            return false;
        }
    }

    char buf[3] = {0};
    for (size_t i = 0; i < len / 2; i++) {
        buf[0] = s[i * 2];
        buf[1] = s[i * 2 + 1];
        char *end = NULL;
        unsigned long v = strtoul(buf, &end, 16);
        if (end == NULL || *end != '\0' || v > 255) {
            return false;
        }
        comps[i] = (uint32_t)v;
    }

    rgba[0] = (float)comps[0] / 255.0f;
    rgba[1] = (float)comps[1] / 255.0f;
    rgba[2] = (float)comps[2] / 255.0f;
    rgba[3] = (float)comps[3] / 255.0f;
    return true;
}
