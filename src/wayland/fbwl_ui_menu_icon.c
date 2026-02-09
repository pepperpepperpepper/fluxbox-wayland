#include "wayland/fbwl_ui_menu_icon.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <cairo/cairo.h>

#ifdef HAVE_XPM
#include <X11/xpm.h>
#endif

#include <wlr/interfaces/wlr_buffer.h>

#include "wayland/fbwl_ui_text.h"

struct icon_cache_entry {
    struct icon_cache_entry *prev;
    struct icon_cache_entry *next;
    char *path;
    int icon_px;
    cairo_surface_t *surface; // cache-held reference
    size_t bytes;
    time_t last_used;
};

static struct icon_cache_entry *icon_cache_head = NULL; // most recently used
static struct icon_cache_entry *icon_cache_tail = NULL; // least recently used
static size_t icon_cache_bytes = 0;

// Defaults match Fluxbox/X11.
static int icon_cache_life_minutes = 5;
static size_t icon_cache_max_bytes = 200 * 1024;

static bool icon_cache_enabled(void) {
    return icon_cache_life_minutes > 0 && icon_cache_max_bytes > 0;
}

static void icon_cache_detach(struct icon_cache_entry *e) {
    if (e == NULL) {
        return;
    }
    if (e->prev != NULL) {
        e->prev->next = e->next;
    } else {
        icon_cache_head = e->next;
    }
    if (e->next != NULL) {
        e->next->prev = e->prev;
    } else {
        icon_cache_tail = e->prev;
    }
    e->prev = NULL;
    e->next = NULL;
}

static void icon_cache_attach_front(struct icon_cache_entry *e) {
    if (e == NULL) {
        return;
    }
    e->prev = NULL;
    e->next = icon_cache_head;
    if (icon_cache_head != NULL) {
        icon_cache_head->prev = e;
    }
    icon_cache_head = e;
    if (icon_cache_tail == NULL) {
        icon_cache_tail = e;
    }
}

static void icon_cache_remove_entry(struct icon_cache_entry *e) {
    if (e == NULL) {
        return;
    }
    icon_cache_detach(e);
    if (e->bytes <= icon_cache_bytes) {
        icon_cache_bytes -= e->bytes;
    } else {
        icon_cache_bytes = 0;
    }
    if (e->surface != NULL) {
        cairo_surface_destroy(e->surface);
    }
    free(e->path);
    free(e);
}

static void icon_cache_clear_all(void) {
    while (icon_cache_head != NULL) {
        icon_cache_remove_entry(icon_cache_head);
    }
    icon_cache_head = NULL;
    icon_cache_tail = NULL;
    icon_cache_bytes = 0;
}

static void icon_cache_prune_expired(time_t now) {
    if (!icon_cache_enabled()) {
        return;
    }
    const time_t ttl = (time_t)icon_cache_life_minutes * 60;
    if (ttl <= 0) {
        icon_cache_clear_all();
        return;
    }
    while (icon_cache_tail != NULL && now - icon_cache_tail->last_used > ttl) {
        icon_cache_remove_entry(icon_cache_tail);
    }
}

static void icon_cache_trim_to_max(void) {
    if (!icon_cache_enabled()) {
        return;
    }
    while (icon_cache_tail != NULL && icon_cache_bytes > icon_cache_max_bytes) {
        icon_cache_remove_entry(icon_cache_tail);
    }
}

static struct icon_cache_entry *icon_cache_find(const char *path, int icon_px) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }
    for (struct icon_cache_entry *e = icon_cache_head; e != NULL; e = e->next) {
        if (e->icon_px != icon_px) {
            continue;
        }
        if (e->path != NULL && strcmp(e->path, path) == 0) {
            return e;
        }
    }
    return NULL;
}

static cairo_surface_t *icon_cache_lookup_surface(const char *path, int icon_px) {
    if (!icon_cache_enabled()) {
        return NULL;
    }
    const time_t now = time(NULL);
    icon_cache_prune_expired(now);

    struct icon_cache_entry *e = icon_cache_find(path, icon_px);
    if (e == NULL || e->surface == NULL) {
        return NULL;
    }

    e->last_used = now;
    if (e != icon_cache_head) {
        icon_cache_detach(e);
        icon_cache_attach_front(e);
    }
    return cairo_surface_reference(e->surface);
}

static void icon_cache_store_surface(const char *path, int icon_px, cairo_surface_t *surface) {
    if (!icon_cache_enabled()) {
        return;
    }
    if (path == NULL || *path == '\0' || surface == NULL || icon_px < 1) {
        return;
    }

    const size_t bytes = (size_t)icon_px * (size_t)icon_px * 4;
    if (bytes == 0 || bytes > icon_cache_max_bytes) {
        return;
    }

    const time_t now = time(NULL);
    icon_cache_prune_expired(now);

    struct icon_cache_entry *existing = icon_cache_find(path, icon_px);
    if (existing != NULL) {
        icon_cache_remove_entry(existing);
    }

    struct icon_cache_entry *e = calloc(1, sizeof(*e));
    if (e == NULL) {
        return;
    }
    e->path = strdup(path);
    if (e->path == NULL) {
        free(e);
        return;
    }
    e->icon_px = icon_px;
    e->surface = cairo_surface_reference(surface);
    e->bytes = bytes;
    e->last_used = now;
    icon_cache_attach_front(e);
    icon_cache_bytes += bytes;
    icon_cache_trim_to_max();
}

void fbwl_ui_menu_icon_cache_configure(int cache_life_minutes, int cache_max_kb) {
    if (cache_life_minutes < 0) {
        cache_life_minutes = 0;
    }
    if (cache_max_kb < 0) {
        cache_max_kb = 0;
    }

    icon_cache_life_minutes = cache_life_minutes;
    icon_cache_max_bytes = (size_t)cache_max_kb * 1024;

    if (!icon_cache_enabled()) {
        icon_cache_clear_all();
        return;
    }

    const time_t now = time(NULL);
    icon_cache_prune_expired(now);
    icon_cache_trim_to_max();
}

static bool icon_is_regular_file_limited(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        return false;
    }
    if (st.st_size <= 0 || st.st_size > (off_t)(8 * 1024 * 1024)) {
        return false;
    }
    return true;
}

static bool icon_has_suffix(const char *s, const char *suffix) {
    if (s == NULL || suffix == NULL) {
        return false;
    }
    const size_t s_len = strlen(s);
    const size_t suf_len = strlen(suffix);
    if (s_len < suf_len) {
        return false;
    }
    return strcasecmp(s + (s_len - suf_len), suffix) == 0;
}

static cairo_surface_t *icon_surface_scale_to_square(cairo_surface_t *src, int icon_px) {
    if (src == NULL || icon_px < 1) {
        return NULL;
    }
    if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        return NULL;
    }

    const int src_w = cairo_image_surface_get_width(src);
    const int src_h = cairo_image_surface_get_height(src);
    if (src_w < 1 || src_h < 1 || src_w > 2048 || src_h > 2048) {
        cairo_surface_destroy(src);
        return NULL;
    }

    cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, icon_px, icon_px);
    if (dst == NULL || cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        if (dst != NULL) {
            cairo_surface_destroy(dst);
        }
        return NULL;
    }

    cairo_t *cr = cairo_create(dst);
    if (cr == NULL || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        cairo_surface_destroy(dst);
        if (cr != NULL) {
            cairo_destroy(cr);
        }
        return NULL;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const double sx = (double)icon_px / (double)src_w;
    const double sy = (double)icon_px / (double)src_h;
    const double scale = sx < sy ? sx : sy;
    const double draw_w = (double)src_w * scale;
    const double draw_h = (double)src_h * scale;
    const double dx = ((double)icon_px - draw_w) * 0.5;
    const double dy = ((double)icon_px - draw_h) * 0.5;

    cairo_translate(cr, dx, dy);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(src);
    cairo_surface_flush(dst);
    return dst;
}

static cairo_surface_t *icon_surface_from_png_path(const char *path, int icon_px) {
    if (!icon_is_regular_file_limited(path)) {
        return NULL;
    }

    cairo_surface_t *loaded = cairo_image_surface_create_from_png(path);
    if (loaded == NULL || cairo_surface_status(loaded) != CAIRO_STATUS_SUCCESS) {
        if (loaded != NULL) {
            cairo_surface_destroy(loaded);
        }
        return NULL;
    }

    return icon_surface_scale_to_square(loaded, icon_px);
}

#ifdef HAVE_XPM

static bool icon_parse_hex_nibble(char c, uint8_t *out) {
    if (out == NULL) {
        return false;
    }
    if (c >= '0' && c <= '9') {
        *out = (uint8_t)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = (uint8_t)(10 + (c - 'a'));
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (uint8_t)(10 + (c - 'A'));
        return true;
    }
    return false;
}

static bool icon_parse_hex_byte(const char *s, uint8_t *out) {
    if (s == NULL || out == NULL) {
        return false;
    }
    uint8_t hi = 0, lo = 0;
    if (!icon_parse_hex_nibble(s[0], &hi) || !icon_parse_hex_nibble(s[1], &lo)) {
        return false;
    }
    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

static uint32_t icon_parse_xpm_color_argb(const char *c) {
    if (c == NULL || *c == '\0') {
        return 0xFFFFFFFFu;
    }
    if (strcasecmp(c, "none") == 0) {
        return 0x00000000u;
    }
    if (c[0] != '#') {
        return 0xFFFFFFFFu;
    }

    const size_t len = strlen(c + 1);
    const char *hex = c + 1;
    uint8_t r = 0, g = 0, b = 0;

    if (len == 3) {
        uint8_t rn = 0, gn = 0, bn = 0;
        if (!icon_parse_hex_nibble(hex[0], &rn) ||
                !icon_parse_hex_nibble(hex[1], &gn) ||
                !icon_parse_hex_nibble(hex[2], &bn)) {
            return 0xFFFFFFFFu;
        }
        r = (uint8_t)(rn * 17);
        g = (uint8_t)(gn * 17);
        b = (uint8_t)(bn * 17);
    } else if (len == 6) {
        if (!icon_parse_hex_byte(hex + 0, &r) ||
                !icon_parse_hex_byte(hex + 2, &g) ||
                !icon_parse_hex_byte(hex + 4, &b)) {
            return 0xFFFFFFFFu;
        }
    } else if (len == 12) {
        uint8_t r_hi = 0, g_hi = 0, b_hi = 0;
        if (!icon_parse_hex_byte(hex + 0, &r_hi) ||
                !icon_parse_hex_byte(hex + 4, &g_hi) ||
                !icon_parse_hex_byte(hex + 8, &b_hi)) {
            return 0xFFFFFFFFu;
        }
        r = r_hi;
        g = g_hi;
        b = b_hi;
    } else {
        return 0xFFFFFFFFu;
    }

    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static cairo_surface_t *icon_surface_from_xpm_path(const char *path, int icon_px) {
    if (!icon_is_regular_file_limited(path)) {
        return NULL;
    }

    XpmImage xpm = {0};
    XpmInfo info = {0};
    int rc = XpmReadFileToXpmImage(path, &xpm, &info);
    if (rc != XpmSuccess) {
        XpmFreeXpmInfo(&info);
        XpmFreeXpmImage(&xpm);
        return NULL;
    }

    const int w = (int)xpm.width;
    const int h = (int)xpm.height;
    if (w < 1 || h < 1 || w > 2048 || h > 2048 || xpm.data == NULL || xpm.colorTable == NULL || xpm.ncolors < 1) {
        XpmFreeXpmInfo(&info);
        XpmFreeXpmImage(&xpm);
        return NULL;
    }

    uint32_t *colors = calloc(xpm.ncolors, sizeof(*colors));
    if (colors == NULL) {
        XpmFreeXpmInfo(&info);
        XpmFreeXpmImage(&xpm);
        return NULL;
    }

    for (unsigned int i = 0; i < xpm.ncolors; i++) {
        const XpmColor *ct = &xpm.colorTable[i];
        const char *c = ct->c_color != NULL ? ct->c_color :
            (ct->g_color != NULL ? ct->g_color : (ct->m_color != NULL ? ct->m_color : NULL));
        colors[i] = icon_parse_xpm_color_argb(c);
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        free(colors);
        XpmFreeXpmInfo(&info);
        XpmFreeXpmImage(&xpm);
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    uint8_t *data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * (size_t)stride);
        for (int x = 0; x < w; x++) {
            const unsigned int idx = xpm.data[(size_t)y * (size_t)w + (size_t)x];
            row[x] = idx < xpm.ncolors ? colors[idx] : 0xFFFFFFFFu;
        }
    }
    cairo_surface_mark_dirty(surface);

    free(colors);
    XpmFreeXpmInfo(&info);
    XpmFreeXpmImage(&xpm);

    return icon_surface_scale_to_square(surface, icon_px);
}

#endif

struct wlr_buffer *fbwl_ui_menu_icon_buffer_create(const char *path, int icon_px) {
    if (path == NULL || *path == '\0' || icon_px < 1) {
        return NULL;
    }

    cairo_surface_t *cached = icon_cache_lookup_surface(path, icon_px);
    if (cached != NULL) {
        struct wlr_buffer *buf = fbwl_cairo_buffer_create(cached);
        if (buf != NULL) {
            return buf;
        }
        cairo_surface_destroy(cached);
    }

    cairo_surface_t *surface = NULL;
    if (icon_has_suffix(path, ".png")) {
        surface = icon_surface_from_png_path(path, icon_px);
    }
#ifdef HAVE_XPM
    else if (icon_has_suffix(path, ".xpm")) {
        surface = icon_surface_from_xpm_path(path, icon_px);
    }
#endif

    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    icon_cache_store_surface(path, icon_px, surface);
    return buf;
}

static uint32_t icon_premultiply_argb(uint32_t argb) {
    const uint32_t a = (argb >> 24) & 0xFFu;
    uint32_t r = (argb >> 16) & 0xFFu;
    uint32_t g = (argb >> 8) & 0xFFu;
    uint32_t b = argb & 0xFFu;

    if (a < 255) {
        r = (r * a + 127) / 255;
        g = (g * a + 127) / 255;
        b = (b * a + 127) / 255;
    }

    return (a << 24) | (r << 16) | (g << 8) | b;
}

struct wlr_buffer *fbwl_ui_menu_icon_buffer_create_argb32(const uint32_t *argb, int w, int h, int icon_px) {
    if (argb == NULL || w < 1 || h < 1 || icon_px < 1 || w > 2048 || h > 2048) {
        return NULL;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    uint8_t *data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * (size_t)stride);
        for (int x = 0; x < w; x++) {
            const size_t idx = (size_t)y * (size_t)w + (size_t)x;
            row[x] = icon_premultiply_argb(argb[idx]);
        }
    }
    cairo_surface_mark_dirty(surface);

    surface = icon_surface_scale_to_square(surface, icon_px);
    if (surface == NULL) {
        return NULL;
    }

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}
