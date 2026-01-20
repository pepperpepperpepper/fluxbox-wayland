#include "wayland/fbwl_sni_tray_internal.h"

#ifdef HAVE_SYSTEMD

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <drm_fourcc.h>

#include <cairo/cairo.h>

#include <wlr/interfaces/wlr_buffer.h>

#include "wayland/fbwl_ui_text.h"

struct wlr_buffer *sni_icon_buffer_from_argb32(const uint8_t *argb, size_t len, int width, int height) {
    if (argb == NULL || len < 4 || width < 1 || height < 1) {
        return NULL;
    }

    const size_t need = (size_t)width * (size_t)height * 4;
    if (need / 4 != (size_t)width * (size_t)height || len < need) {
        return NULL;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    uint8_t *dst = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    if (dst == NULL || stride < width * 4) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        uint8_t *row = dst + (ptrdiff_t)y * stride;
        const size_t src_row_off = (size_t)y * (size_t)width * 4;
        for (int x = 0; x < width; x++) {
            const size_t off = src_row_off + (size_t)x * 4;
            const uint8_t a = argb[off + 0];
            const uint8_t r = argb[off + 1];
            const uint8_t g = argb[off + 2];
            const uint8_t b = argb[off + 3];

            const uint16_t ar = (uint16_t)r * (uint16_t)a;
            const uint16_t ag = (uint16_t)g * (uint16_t)a;
            const uint16_t ab = (uint16_t)b * (uint16_t)a;

            const uint8_t r_p = (uint8_t)((ar + 127) / 255);
            const uint8_t g_p = (uint8_t)((ag + 127) / 255);
            const uint8_t b_p = (uint8_t)((ab + 127) / 255);

            row[(size_t)x * 4 + 0] = b_p;
            row[(size_t)x * 4 + 1] = g_p;
            row[(size_t)x * 4 + 2] = r_p;
            row[(size_t)x * 4 + 3] = a;
        }
    }

    cairo_surface_mark_dirty(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

const char *sni_status_str(enum fbwl_sni_status status) {
    switch (status) {
    case FBWL_SNI_STATUS_PASSIVE:
        return "Passive";
    case FBWL_SNI_STATUS_NEEDS_ATTENTION:
        return "NeedsAttention";
    case FBWL_SNI_STATUS_ACTIVE:
    default:
        return "Active";
    }
}

enum fbwl_sni_status sni_status_parse(const char *s) {
    if (s == NULL || s[0] == '\0') {
        return FBWL_SNI_STATUS_ACTIVE;
    }
    if (strcmp(s, "Passive") == 0) {
        return FBWL_SNI_STATUS_PASSIVE;
    }
    if (strcmp(s, "NeedsAttention") == 0) {
        return FBWL_SNI_STATUS_NEEDS_ATTENTION;
    }
    return FBWL_SNI_STATUS_ACTIVE;
}

struct wlr_buffer *sni_icon_compose_overlay(struct wlr_buffer *base, struct wlr_buffer *overlay) {
    if (base == NULL || overlay == NULL) {
        return NULL;
    }

    const int bw = base->width;
    const int bh = base->height;
    if (bw < 1 || bh < 1 || bw > 1024 || bh > 1024) {
        return NULL;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    cairo_t *cr = cairo_create(surface);

    void *base_data = NULL;
    uint32_t base_format = 0;
    size_t base_stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(base, WLR_BUFFER_DATA_PTR_ACCESS_READ, &base_data, &base_format,
            &base_stride)) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return NULL;
    }
    if (base_format != DRM_FORMAT_ARGB8888 || base_stride > INT_MAX) {
        wlr_buffer_end_data_ptr_access(base);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_surface_t *base_surface =
        cairo_image_surface_create_for_data(base_data, CAIRO_FORMAT_ARGB32, bw, bh, (int)base_stride);
    cairo_set_source_surface(cr, base_surface, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(base_surface);
    wlr_buffer_end_data_ptr_access(base);

    const int ow = overlay->width;
    const int oh = overlay->height;
    if (ow > 0 && oh > 0) {
        void *ov_data = NULL;
        uint32_t ov_format = 0;
        size_t ov_stride = 0;
        const bool ov_ok =
            wlr_buffer_begin_data_ptr_access(overlay, WLR_BUFFER_DATA_PTR_ACCESS_READ, &ov_data, &ov_format, &ov_stride);
        if (ov_ok && ov_format == DRM_FORMAT_ARGB8888 && ov_stride <= INT_MAX) {
            cairo_surface_t *ov_surface = cairo_image_surface_create_for_data(ov_data, CAIRO_FORMAT_ARGB32, ow, oh,
                (int)ov_stride);

            const int max_w = bw >= 2 ? bw / 2 : bw;
            const int max_h = bh >= 2 ? bh / 2 : bh;

            double scale = 1.0;
            if (ow > max_w || oh > max_h) {
                const double sx = max_w > 0 ? (double)max_w / (double)ow : 1.0;
                const double sy = max_h > 0 ? (double)max_h / (double)oh : 1.0;
                scale = sx < sy ? sx : sy;
                if (scale > 1.0) {
                    scale = 1.0;
                }
                if (scale < 0.01) {
                    scale = 0.01;
                }
            }

            int tw = (int)((double)ow * scale + 0.5);
            int th = (int)((double)oh * scale + 0.5);
            if (tw < 1) {
                tw = 1;
            }
            if (th < 1) {
                th = 1;
            }
            int x = bw - tw;
            int y = bh - th;
            if (x < 0) {
                x = 0;
            }
            if (y < 0) {
                y = 0;
            }

            cairo_save(cr);
            cairo_translate(cr, x, y);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, ov_surface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);

            cairo_surface_destroy(ov_surface);
        }
        if (ov_ok) {
            wlr_buffer_end_data_ptr_access(overlay);
        }
    }

    cairo_destroy(cr);
    cairo_surface_mark_dirty(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

static bool sni_str_has_suffix(const char *s, const char *suffix) {
    if (s == NULL || suffix == NULL) {
        return false;
    }

    const size_t slen = strlen(s);
    const size_t sufflen = strlen(suffix);
    if (slen < sufflen) {
        return false;
    }

    return memcmp(s + slen - sufflen, suffix, sufflen) == 0;
}

static bool sni_path_is_regular_file_limited(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    struct stat st = {0};
    if (stat(path, &st) != 0) {
        return false;
    }

    if (!S_ISREG(st.st_mode)) {
        return false;
    }

    if (st.st_size < 1 || st.st_size > 5 * 1024 * 1024) {
        return false;
    }

    return access(path, R_OK) == 0;
}

static char *sni_icon_find_png_in_data_dir(const char *base, const char *icon_name, bool has_png_ext,
        const char *const *themes, size_t theme_count) {
    if (base == NULL || base[0] == '\0' || icon_name == NULL || icon_name[0] == '\0') {
        return NULL;
    }

    static const int sizes[] = {16, 22, 24, 32, 48, 64, 128};
    static const char *contexts[] = {"status", "apps", "panel", "actions"};

    for (size_t ti = 0; ti < theme_count; ti++) {
        const char *theme = themes[ti];
        if (theme == NULL || theme[0] == '\0') {
            continue;
        }

        for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
            const int sz = sizes[si];
            for (size_t ci = 0; ci < sizeof(contexts) / sizeof(contexts[0]); ci++) {
                int n = snprintf(NULL, 0, "%s/icons/%s/%dx%d/%s/%s%s", base, theme, sz, sz, contexts[ci],
                    icon_name, has_png_ext ? "" : ".png");
                if (n <= 0) {
                    continue;
                }
                size_t len = (size_t)n + 1;
                char *path = malloc(len);
                if (path == NULL) {
                    continue;
                }
                snprintf(path, len, "%s/icons/%s/%dx%d/%s/%s%s", base, theme, sz, sz, contexts[ci], icon_name,
                    has_png_ext ? "" : ".png");

                if (sni_path_is_regular_file_limited(path)) {
                    return path;
                }
                free(path);
            }
        }
    }

    return NULL;
}

static char *sni_icon_find_png_in_icon_dir(const char *base, const char *icon_name, bool has_png_ext,
        const char *const *themes, size_t theme_count) {
    if (base == NULL || base[0] == '\0' || icon_name == NULL || icon_name[0] == '\0') {
        return NULL;
    }

    static const int sizes[] = {16, 22, 24, 32, 48, 64, 128};
    static const char *contexts[] = {"status", "apps", "panel", "actions"};

    for (size_t ti = 0; ti < theme_count; ti++) {
        const char *theme = themes[ti];
        if (theme == NULL || theme[0] == '\0') {
            continue;
        }

        for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
            const int sz = sizes[si];
            for (size_t ci = 0; ci < sizeof(contexts) / sizeof(contexts[0]); ci++) {
                int n = snprintf(NULL, 0, "%s/%s/%dx%d/%s/%s%s", base, theme, sz, sz, contexts[ci], icon_name,
                    has_png_ext ? "" : ".png");
                if (n <= 0) {
                    continue;
                }
                size_t len = (size_t)n + 1;
                char *path = malloc(len);
                if (path == NULL) {
                    continue;
                }
                snprintf(path, len, "%s/%s/%dx%d/%s/%s%s", base, theme, sz, sz, contexts[ci], icon_name,
                    has_png_ext ? "" : ".png");

                if (sni_path_is_regular_file_limited(path)) {
                    return path;
                }
                free(path);
            }
        }
    }

    return NULL;
}

char *sni_icon_resolve_png_path(const char *icon_name, const char *icon_theme_path) {
    if (icon_name == NULL || icon_name[0] == '\0') {
        return NULL;
    }

    const bool has_png_ext = sni_str_has_suffix(icon_name, ".png");

    if (strchr(icon_name, '/') != NULL) {
        if (sni_path_is_regular_file_limited(icon_name)) {
            return strdup(icon_name);
        }

        if (!has_png_ext) {
            int n = snprintf(NULL, 0, "%s.png", icon_name);
            if (n > 0) {
                size_t len = (size_t)n + 1;
                char *path = malloc(len);
                if (path != NULL) {
                    snprintf(path, len, "%s.png", icon_name);
                    if (sni_path_is_regular_file_limited(path)) {
                        return path;
                    }
                    free(path);
                }
            }
        }

        return NULL;
    }

    const char *themes[3] = {0};
    size_t theme_count = 0;
    const char *theme_env = getenv("FBWL_ICON_THEME");
    if (theme_env != NULL && theme_env[0] != '\0') {
        themes[theme_count++] = theme_env;
    }
    themes[theme_count++] = "hicolor";
    themes[theme_count++] = "Adwaita";

    if (icon_theme_path != NULL && icon_theme_path[0] != '\0') {
        char *path = sni_icon_find_png_in_data_dir(icon_theme_path, icon_name, has_png_ext, themes, theme_count);
        if (path != NULL) {
            return path;
        }
        path = sni_icon_find_png_in_icon_dir(icon_theme_path, icon_name, has_png_ext, themes, theme_count);
        if (path != NULL) {
            return path;
        }
    }

    char *data_home_tmp = NULL;
    const char *data_home = getenv("XDG_DATA_HOME");
    if (data_home == NULL || data_home[0] == '\0') {
        const char *home = getenv("HOME");
        if (home != NULL && home[0] != '\0') {
            size_t len = strlen(home) + strlen("/.local/share") + 1;
            data_home_tmp = malloc(len);
            if (data_home_tmp != NULL) {
                snprintf(data_home_tmp, len, "%s/.local/share", home);
                data_home = data_home_tmp;
            }
        }
    }

    if (data_home != NULL && data_home[0] != '\0') {
        char *path = sni_icon_find_png_in_data_dir(data_home, icon_name, has_png_ext, themes, theme_count);
        if (path != NULL) {
            free(data_home_tmp);
            return path;
        }
    }

    const char *data_dirs = getenv("XDG_DATA_DIRS");
    if (data_dirs == NULL || data_dirs[0] == '\0') {
        data_dirs = "/usr/local/share:/usr/share";
    }

    char *found = NULL;
    const char *p = data_dirs;
    while (p != NULL && *p != '\0' && found == NULL) {
        const char *colon = strchr(p, ':');
        const size_t n = colon != NULL ? (size_t)(colon - p) : strlen(p);
        if (n > 0) {
            char *base = strndup(p, n);
            if (base != NULL) {
                found = sni_icon_find_png_in_data_dir(base, icon_name, has_png_ext, themes, theme_count);
                free(base);
            }
        }
        if (colon == NULL) {
            break;
        }
        p = colon + 1;
    }

    free(data_home_tmp);
    return found;
}

struct wlr_buffer *sni_icon_buffer_from_png_path(const char *path) {
    if (!sni_path_is_regular_file_limited(path)) {
        return NULL;
    }

    cairo_surface_t *loaded = cairo_image_surface_create_from_png(path);
    if (loaded == NULL || cairo_surface_status(loaded) != CAIRO_STATUS_SUCCESS) {
        if (loaded != NULL) {
            cairo_surface_destroy(loaded);
        }
        return NULL;
    }

    const int w = cairo_image_surface_get_width(loaded);
    const int h = cairo_image_surface_get_height(loaded);
    if (w < 1 || h < 1 || w > 1024 || h > 1024) {
        cairo_surface_destroy(loaded);
        return NULL;
    }

    cairo_surface_t *surface = loaded;
    if (cairo_image_surface_get_format(loaded) != CAIRO_FORMAT_ARGB32) {
        cairo_surface_t *converted = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        if (converted == NULL || cairo_surface_status(converted) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(loaded);
            if (converted != NULL) {
                cairo_surface_destroy(converted);
            }
            return NULL;
        }

        cairo_t *cr = cairo_create(converted);
        cairo_set_source_surface(cr, loaded, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);

        cairo_surface_destroy(loaded);
        surface = converted;
    }

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

#endif

