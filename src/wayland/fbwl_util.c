#include "wayland/fbwl_util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <time.h>
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

static bool parse_x_hex_component_16(const char *s, size_t len, uint16_t *out) {
    if (s == NULL || out == NULL || len < 1 || len > 4) {
        return false;
    }

    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (!isxdigit(c)) {
            return false;
        }
        v *= 16;
        if (c >= '0' && c <= '9') {
            v += (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v += 10u + (uint32_t)(c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            v += 10u + (uint32_t)(c - 'A');
        } else {
            return false;
        }
    }

    const uint32_t max = (1u << (len * 4)) - 1u;
    const uint32_t scaled = max > 0 ? (v * 65535u + max / 2u) / max : 0u;
    if (scaled > 65535u) {
        return false;
    }

    *out = (uint16_t)scaled;
    return true;
}

static bool parse_rgb_hex(const char *s, float rgba[static 4]) {
    if (s == NULL || rgba == NULL) {
        return false;
    }

    const char *p = s;
    uint16_t comps16[3] = {0, 0, 0};

    for (size_t i = 0; i < 3; i++) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        const char *start = p;
        while (*p != '\0' && isxdigit((unsigned char)*p)) {
            p++;
        }
        const char *end = p;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (start == end || (size_t)(end - start) > 4) {
            return false;
        }
        if (!parse_x_hex_component_16(start, (size_t)(end - start), &comps16[i])) {
            return false;
        }
        if (i < 2) {
            if (*p != '/') {
                return false;
            }
            p++;
        } else {
            if (*p != '\0') {
                return false;
            }
        }
    }

    rgba[0] = (float)comps16[0] / 65535.0f;
    rgba[1] = (float)comps16[1] / 65535.0f;
    rgba[2] = (float)comps16[2] / 65535.0f;
    rgba[3] = 1.0f;
    return true;
}

static bool parse_rgb_float(const char *s, float rgba[static 4]) {
    if (s == NULL || rgba == NULL) {
        return false;
    }

    const char *p = s;
    float comps[3] = {0.0f, 0.0f, 0.0f};

    for (size_t i = 0; i < 3; i++) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            return false;
        }
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p || end == NULL) {
            return false;
        }
        p = end;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (i < 2) {
            if (*p != '/') {
                return false;
            }
            p++;
        } else {
            if (*p != '\0') {
                return false;
            }
        }

        if (v < 0.0) {
            v = 0.0;
        }
        if (v > 1.0) {
            v = 1.0;
        }
        comps[i] = (float)v;
    }

    rgba[0] = comps[0];
    rgba[1] = comps[1];
    rgba[2] = comps[2];
    rgba[3] = 1.0f;
    return true;
}

bool fbwl_parse_color(const char *s, float rgba[static 4]) {
    if (fbwl_parse_hex_color(s, rgba)) {
        return true;
    }

    if (s == NULL || rgba == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    if (strncasecmp(s, "rgb:", 4) == 0) {
        return parse_rgb_hex(s + 4, rgba);
    }
    if (strncasecmp(s, "rgbi:", 5) == 0) {
        return parse_rgb_float(s + 5, rgba);
    }

    if (strcasecmp(s, "transparent") == 0 || strcasecmp(s, "none") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 0.0f;
        rgba[2] = 0.0f;
        rgba[3] = 0.0f;
        return true;
    }

    if (strncasecmp(s, "gray", 4) == 0 || strncasecmp(s, "grey", 4) == 0) {
        const char *p = s + 4;
        if (strncasecmp(s, "grey", 4) == 0) {
            p = s + 4;
        }
        if (*p == '\0') {
            rgba[0] = 0.5f;
            rgba[1] = 0.5f;
            rgba[2] = 0.5f;
            rgba[3] = 1.0f;
            return true;
        }
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end != p && end != NULL && *end == '\0' && v >= 0 && v <= 100) {
            float f = (float)v / 100.0f;
            rgba[0] = f;
            rgba[1] = f;
            rgba[2] = f;
            rgba[3] = 1.0f;
            return true;
        }
        return false;
    }

    if (strcasecmp(s, "black") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 0.0f;
        rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "white") == 0) {
        rgba[0] = 1.0f;
        rgba[1] = 1.0f;
        rgba[2] = 1.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "red") == 0) {
        rgba[0] = 1.0f;
        rgba[1] = 0.0f;
        rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "green") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 1.0f;
        rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "blue") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 0.0f;
        rgba[2] = 1.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "yellow") == 0) {
        rgba[0] = 1.0f;
        rgba[1] = 1.0f;
        rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "cyan") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 1.0f;
        rgba[2] = 1.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "magenta") == 0) {
        rgba[0] = 1.0f;
        rgba[1] = 0.0f;
        rgba[2] = 1.0f;
        rgba[3] = 1.0f;
        return true;
    }

    return false;
}

void fbwl_spawn(const char *cmd) {
    if (cmd == NULL || *cmd == '\0') {
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
        _exit(127);
    }
}

uint64_t fbwl_now_msec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
