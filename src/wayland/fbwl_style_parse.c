#include "wayland/fbwl_style_parse.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_util.h"

static char *trim_inplace(char *s) {
    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

bool fbwl_style_load_file(struct fbwl_decor_theme *theme, const char *path) {
    if (theme == NULL || path == NULL || *path == '\0') {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Style: failed to open %s: %s", path, strerror(errno));
        return false;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    while ((nread = getline(&line, &cap, f)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        char *s = trim_inplace(line);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (*s == '#' || *s == '!') {
            continue;
        }

        char *sep = strchr(s, ':');
        if (sep == NULL) {
            continue;
        }
        *sep = '\0';
        char *key = trim_inplace(s);
        char *val = trim_inplace(sep + 1);
        if (key == NULL || *key == '\0' || val == NULL || *val == '\0') {
            continue;
        }

        if (strcasecmp(key, "window.borderWidth") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end != val && (end == NULL || *trim_inplace(end) == '\0') && v > 0 && v < 1000) {
                theme->border_width = (int)v;
            }
            continue;
        }

        if (strcasecmp(key, "window.title.height") == 0) {
            char *end = NULL;
            long v = strtol(val, &end, 10);
            if (end != val && (end == NULL || *trim_inplace(end) == '\0') && v > 0 && v < 1000) {
                theme->title_height = (int)v;
            }
            continue;
        }

        if (strcasecmp(key, "window.borderColor") == 0) {
            (void)fbwl_parse_hex_color(val, theme->border_color);
            continue;
        }

        if (strcasecmp(key, "window.title.focus.color") == 0) {
            (void)fbwl_parse_hex_color(val, theme->titlebar_active);
            continue;
        }

        if (strcasecmp(key, "window.title.unfocus.color") == 0) {
            (void)fbwl_parse_hex_color(val, theme->titlebar_inactive);
            continue;
        }
    }

    free(line);
    fclose(f);

    wlr_log(WLR_INFO, "Style: loaded %s (border=%d title_h=%d)",
        path, theme->border_width, theme->title_height);
    return true;
}
