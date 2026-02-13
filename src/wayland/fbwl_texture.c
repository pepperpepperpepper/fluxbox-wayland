#include "wayland/fbwl_texture.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
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

#include "wayland/fbwl_texture_render_internal.h"
#include "wayland/fbwl_ui_text.h"

struct texture_cache_entry {
    struct texture_cache_entry *prev;
    struct texture_cache_entry *next;
    uint32_t type;
    int width;
    int height;
    uint8_t c_r;
    uint8_t c_g;
    uint8_t c_b;
    uint8_t c_to_r;
    uint8_t c_to_g;
    uint8_t c_to_b;
    char *pixmap;
    time_t pixmap_mtime;
    cairo_surface_t *surface; // cache-held reference
    size_t bytes;
    time_t last_used;
};

static struct texture_cache_entry *texture_cache_head = NULL; // most recently used
static struct texture_cache_entry *texture_cache_tail = NULL; // least recently used
static size_t texture_cache_bytes = 0;

// Defaults match Fluxbox/X11.
static int texture_cache_life_minutes = 5;
static size_t texture_cache_max_bytes = 200 * 1024;

static bool texture_cache_enabled(void) {
    return texture_cache_life_minutes > 0 && texture_cache_max_bytes > 0;
}

static void texture_cache_detach(struct texture_cache_entry *e) {
    if (e == NULL) {
        return;
    }
    if (e->prev != NULL) {
        e->prev->next = e->next;
    } else {
        texture_cache_head = e->next;
    }
    if (e->next != NULL) {
        e->next->prev = e->prev;
    } else {
        texture_cache_tail = e->prev;
    }
    e->prev = NULL;
    e->next = NULL;
}

static void texture_cache_attach_front(struct texture_cache_entry *e) {
    if (e == NULL) {
        return;
    }
    e->prev = NULL;
    e->next = texture_cache_head;
    if (texture_cache_head != NULL) {
        texture_cache_head->prev = e;
    }
    texture_cache_head = e;
    if (texture_cache_tail == NULL) {
        texture_cache_tail = e;
    }
}

static void texture_cache_remove_entry(struct texture_cache_entry *e) {
    if (e == NULL) {
        return;
    }
    texture_cache_detach(e);
    if (e->bytes <= texture_cache_bytes) {
        texture_cache_bytes -= e->bytes;
    } else {
        texture_cache_bytes = 0;
    }
    if (e->surface != NULL) {
        cairo_surface_destroy(e->surface);
    }
    free(e->pixmap);
    free(e);
}

static void texture_cache_clear_all(void) {
    while (texture_cache_head != NULL) {
        texture_cache_remove_entry(texture_cache_head);
    }
    texture_cache_head = NULL;
    texture_cache_tail = NULL;
    texture_cache_bytes = 0;
}

static void texture_cache_prune_expired(time_t now) {
    if (!texture_cache_enabled()) {
        return;
    }
    const time_t ttl = (time_t)texture_cache_life_minutes * 60;
    if (ttl <= 0) {
        texture_cache_clear_all();
        return;
    }
    while (texture_cache_tail != NULL && now - texture_cache_tail->last_used > ttl) {
        texture_cache_remove_entry(texture_cache_tail);
    }
}

static void texture_cache_trim_to_max(void) {
    if (!texture_cache_enabled()) {
        return;
    }
    while (texture_cache_tail != NULL && texture_cache_bytes > texture_cache_max_bytes) {
        texture_cache_remove_entry(texture_cache_tail);
    }
}

static bool texture_cache_key_matches(const struct texture_cache_entry *e,
        uint32_t type, int width, int height,
        uint8_t c_r, uint8_t c_g, uint8_t c_b,
        uint8_t c_to_r, uint8_t c_to_g, uint8_t c_to_b,
        const char *pixmap) {
    if (e == NULL) {
        return false;
    }
    if (e->type != type || e->width != width || e->height != height) {
        return false;
    }

    const bool want_pixmap = pixmap != NULL && *pixmap != '\0';
    const bool have_pixmap = e->pixmap != NULL && *e->pixmap != '\0';
    if (want_pixmap != have_pixmap) {
        return false;
    }
    if (want_pixmap) {
        return strcmp(e->pixmap, pixmap) == 0;
    }

    return e->c_r == c_r && e->c_g == c_g && e->c_b == c_b &&
        e->c_to_r == c_to_r && e->c_to_g == c_to_g && e->c_to_b == c_to_b;
}

static cairo_surface_t *texture_cache_lookup_surface(uint32_t type, int width, int height,
        uint8_t c_r, uint8_t c_g, uint8_t c_b,
        uint8_t c_to_r, uint8_t c_to_g, uint8_t c_to_b,
        const char *pixmap, time_t pixmap_mtime) {
    if (!texture_cache_enabled()) {
        return NULL;
    }
    const time_t now = time(NULL);
    texture_cache_prune_expired(now);

    for (struct texture_cache_entry *e = texture_cache_head; e != NULL; ) {
        struct texture_cache_entry *next = e->next;
        if (!texture_cache_key_matches(e, type, width, height, c_r, c_g, c_b, c_to_r, c_to_g, c_to_b, pixmap)) {
            e = next;
            continue;
        }

        if (pixmap != NULL && *pixmap != '\0' && e->pixmap_mtime != 0 && pixmap_mtime != 0 &&
                e->pixmap_mtime != pixmap_mtime) {
            texture_cache_remove_entry(e);
            e = next;
            continue;
        }

        if (e->surface == NULL) {
            e = next;
            continue;
        }

        e->last_used = now;
        if (e != texture_cache_head) {
            texture_cache_detach(e);
            texture_cache_attach_front(e);
        }

        return cairo_surface_reference(e->surface);
    }

    return NULL;
}

static void texture_cache_store_surface(uint32_t type, int width, int height,
        uint8_t c_r, uint8_t c_g, uint8_t c_b,
        uint8_t c_to_r, uint8_t c_to_g, uint8_t c_to_b,
        const char *pixmap, time_t pixmap_mtime,
        cairo_surface_t *surface) {
    if (!texture_cache_enabled()) {
        return;
    }
    if (surface == NULL || width < 1 || height < 1) {
        return;
    }

    const size_t bytes = (size_t)width * (size_t)height * 4u;
    if (bytes == 0 || bytes > texture_cache_max_bytes) {
        return;
    }

    const time_t now = time(NULL);
    texture_cache_prune_expired(now);

    for (struct texture_cache_entry *e = texture_cache_head; e != NULL; ) {
        struct texture_cache_entry *next = e->next;
        if (texture_cache_key_matches(e, type, width, height, c_r, c_g, c_b, c_to_r, c_to_g, c_to_b, pixmap)) {
            texture_cache_remove_entry(e);
            break;
        }
        e = next;
    }

    struct texture_cache_entry *e = calloc(1, sizeof(*e));
    if (e == NULL) {
        return;
    }
    if (pixmap != NULL && *pixmap != '\0') {
        e->pixmap = strdup(pixmap);
        if (e->pixmap == NULL) {
            free(e);
            return;
        }
        e->pixmap_mtime = pixmap_mtime;
    }
    e->type = type;
    e->width = width;
    e->height = height;
    e->c_r = c_r;
    e->c_g = c_g;
    e->c_b = c_b;
    e->c_to_r = c_to_r;
    e->c_to_g = c_to_g;
    e->c_to_b = c_to_b;
    e->surface = cairo_surface_reference(surface);
    e->bytes = bytes;
    e->last_used = now;
    texture_cache_attach_front(e);
    texture_cache_bytes += bytes;
    texture_cache_trim_to_max();
}

void fbwl_texture_cache_configure(int cache_life_minutes, int cache_max_kb) {
    if (cache_life_minutes < 0) {
        cache_life_minutes = 0;
    }
    if (cache_max_kb < 0) {
        cache_max_kb = 0;
    }

    texture_cache_life_minutes = cache_life_minutes;
    texture_cache_max_bytes = (size_t)cache_max_kb * 1024u;

    if (!texture_cache_enabled()) {
        texture_cache_clear_all();
        return;
    }

    const time_t now = time(NULL);
    texture_cache_prune_expired(now);
    texture_cache_trim_to_max();
}

void fbwl_texture_init(struct fbwl_texture *tex) {
    if (tex == NULL) {
        return;
    }
    memset(tex, 0, sizeof(*tex));
    tex->type = FBWL_TEXTURE_FLAT | FBWL_TEXTURE_SOLID;
    tex->color[0] = 0.0f;
    tex->color[1] = 0.0f;
    tex->color[2] = 0.0f;
    tex->color[3] = 1.0f;
    memcpy(tex->color_to, tex->color, sizeof(tex->color_to));
    tex->pic_color[0] = 1.0f;
    tex->pic_color[1] = 1.0f;
    tex->pic_color[2] = 1.0f;
    tex->pic_color[3] = 1.0f;
    tex->pixmap[0] = '\0';
}

static bool str_to_lower_owned(const char *s, char **out_owned) {
    if (out_owned != NULL) {
        *out_owned = NULL;
    }
    if (s == NULL || out_owned == NULL) {
        return false;
    }
    const size_t len = strlen(s);
    char *buf = malloc(len + 1);
    if (buf == NULL) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)s[i]);
    }
    buf[len] = '\0';
    *out_owned = buf;
    return true;
}

bool fbwl_texture_parse_type(const char *s, uint32_t *out_type) {
    if (out_type != NULL) {
        *out_type = 0;
    }
    if (s == NULL || out_type == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    char *t_owned = NULL;
    if (!str_to_lower_owned(s, &t_owned) || t_owned == NULL) {
        return false;
    }
    const char *ts = t_owned;

    if (strstr(ts, "parentrelative") != NULL) {
        *out_type = FBWL_TEXTURE_PARENTRELATIVE;
        free(t_owned);
        return true;
    }

    uint32_t type = FBWL_TEXTURE_NONE;

    if (strstr(ts, "gradient") != NULL) {
        type |= FBWL_TEXTURE_GRADIENT;
        if (strstr(ts, "crossdiagonal") != NULL) {
            type |= FBWL_TEXTURE_CROSSDIAGONAL;
        } else if (strstr(ts, "rectangle") != NULL) {
            type |= FBWL_TEXTURE_RECTANGLE;
        } else if (strstr(ts, "pyramid") != NULL) {
            type |= FBWL_TEXTURE_PYRAMID;
        } else if (strstr(ts, "pipecross") != NULL) {
            type |= FBWL_TEXTURE_PIPECROSS;
        } else if (strstr(ts, "elliptic") != NULL) {
            type |= FBWL_TEXTURE_ELLIPTIC;
        } else if (strstr(ts, "diagonal") != NULL) {
            type |= FBWL_TEXTURE_DIAGONAL;
        } else if (strstr(ts, "horizontal") != NULL) {
            type |= FBWL_TEXTURE_HORIZONTAL;
        } else if (strstr(ts, "vertical") != NULL) {
            type |= FBWL_TEXTURE_VERTICAL;
        } else {
            type |= FBWL_TEXTURE_DIAGONAL;
        }
    } else if (strstr(ts, "solid") != NULL) {
        type |= FBWL_TEXTURE_SOLID;
    } else {
        type |= FBWL_TEXTURE_SOLID;
    }

    if (strstr(ts, "raised") != NULL) {
        type |= FBWL_TEXTURE_RAISED;
    } else if (strstr(ts, "sunken") != NULL) {
        type |= FBWL_TEXTURE_SUNKEN;
    } else if (strstr(ts, "flat") != NULL) {
        type |= FBWL_TEXTURE_FLAT;
    } else {
        type |= FBWL_TEXTURE_FLAT;
    }

    if ((type & FBWL_TEXTURE_FLAT) == 0) {
        if (strstr(ts, "bevel2") != NULL) {
            type |= FBWL_TEXTURE_BEVEL2;
        } else {
            type |= FBWL_TEXTURE_BEVEL1;
        }
    }

    if (strstr(ts, "invert") != NULL) {
        type |= FBWL_TEXTURE_INVERT;
    }
    if (strstr(ts, "interlaced") != NULL) {
        type |= FBWL_TEXTURE_INTERLACED;
    }
    if (strstr(ts, "tiled") != NULL) {
        type |= FBWL_TEXTURE_TILED;
    }

    free(t_owned);
    *out_type = type;
    return true;
}

static bool pixmap_is_regular_file_limited(const char *path) {
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
    const off_t max_size = 32 * 1024 * 1024;
    return st.st_size >= 0 && st.st_size <= max_size;
}

static bool path_has_suffix(const char *path, const char *suffix) {
    if (path == NULL || suffix == NULL) {
        return false;
    }
    const size_t plen = strlen(path);
    const size_t slen = strlen(suffix);
    if (plen < slen) {
        return false;
    }
    return strcasecmp(path + (plen - slen), suffix) == 0;
}

static cairo_surface_t *pixmap_surface_from_png_path(const char *path) {
    if (!pixmap_is_regular_file_limited(path)) {
        return NULL;
    }
    cairo_surface_t *loaded = cairo_image_surface_create_from_png(path);
    if (loaded == NULL || cairo_surface_status(loaded) != CAIRO_STATUS_SUCCESS) {
        if (loaded != NULL) {
            cairo_surface_destroy(loaded);
        }
        return NULL;
    }
    if (cairo_image_surface_get_format(loaded) == CAIRO_FORMAT_ARGB32) {
        return loaded;
    }
    const int w = cairo_image_surface_get_width(loaded);
    const int h = cairo_image_surface_get_height(loaded);
    cairo_surface_t *converted = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (converted == NULL || cairo_surface_status(converted) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(loaded);
        if (converted != NULL) {
            cairo_surface_destroy(converted);
        }
        return NULL;
    }
    cairo_t *cr = cairo_create(converted);
    if (cr == NULL || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(loaded);
        cairo_surface_destroy(converted);
        if (cr != NULL) {
            cairo_destroy(cr);
        }
        return NULL;
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, loaded, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(loaded);
    cairo_surface_flush(converted);
    return converted;
}

#ifdef HAVE_XPM

static bool xpm_parse_hex_nibble(char c, uint8_t *out) {
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

static bool xpm_parse_hex_byte(const char *s, uint8_t *out) {
    if (s == NULL || out == NULL) {
        return false;
    }
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (!xpm_parse_hex_nibble(s[0], &hi) || !xpm_parse_hex_nibble(s[1], &lo)) {
        return false;
    }
    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

static cairo_surface_t *pixmap_surface_from_xpm_path(const char *path) {
    if (!pixmap_is_regular_file_limited(path)) {
        return NULL;
    }

    XpmImage xpm = {0};
    XpmInfo info = {0};
    int rc = XpmReadFileToXpmImage((char *)path, &xpm, &info);
    if (rc != XpmSuccess) {
        return NULL;
    }

    const unsigned int w = xpm.width;
    const unsigned int h = xpm.height;
    if (w == 0 || h == 0) {
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
        uint32_t px = 0xFFFFFFFFu;
        const char *c = xpm.colorTable[i].c_color;
        if (c != NULL) {
            while (*c != '\0' && isspace((unsigned char)*c)) {
                c++;
            }
        }
        if (c != NULL && (strcasecmp(c, "none") == 0 || strcasecmp(c, "transparent") == 0)) {
            px = 0x00000000u;
        } else if (c != NULL && c[0] == '#' && strlen(c) >= 7) {
            uint8_t r = 0, g = 0, b = 0;
            if (xpm_parse_hex_byte(c + 1, &r) && xpm_parse_hex_byte(c + 3, &g) && xpm_parse_hex_byte(c + 5, &b)) {
                px = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        colors[i] = px;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)w, (int)h);
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
    for (int y = 0; y < (int)h; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * (size_t)stride);
        for (int x = 0; x < (int)w; x++) {
            const unsigned int idx = xpm.data[(size_t)y * (size_t)w + (size_t)x];
            row[x] = idx < xpm.ncolors ? colors[idx] : 0xFFFFFFFFu;
        }
    }
    cairo_surface_mark_dirty(surface);

    free(colors);
    XpmFreeXpmInfo(&info);
    XpmFreeXpmImage(&xpm);
    return surface;
}

#endif

static cairo_surface_t *pixmap_surface_load(const char *path) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }
    if (path_has_suffix(path, ".png")) {
        return pixmap_surface_from_png_path(path);
    }
#ifdef HAVE_XPM
    if (path_has_suffix(path, ".xpm")) {
        return pixmap_surface_from_xpm_path(path);
    }
#endif
    return NULL;
}

static cairo_surface_t *surface_scale_to_fit(cairo_surface_t *src, int w, int h) {
    if (src == NULL || w < 1 || h < 1) {
        return NULL;
    }
    if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        return NULL;
    }

    cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
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

    const int sw = cairo_image_surface_get_width(src);
    const int sh = cairo_image_surface_get_height(src);
    if (sw < 1 || sh < 1) {
        cairo_destroy(cr);
        cairo_surface_destroy(src);
        cairo_surface_destroy(dst);
        return NULL;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_scale(cr, (double)w / (double)sw, (double)h / (double)sh);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);
    cairo_surface_flush(dst);
    return dst;
}

static cairo_surface_t *surface_tile(cairo_surface_t *src, int w, int h) {
    if (src == NULL || w < 1 || h < 1) {
        return NULL;
    }
    if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        return NULL;
    }

    cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
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

    cairo_pattern_t *pat = cairo_pattern_create_for_surface(src);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
    cairo_set_source(cr, pat);
    cairo_pattern_destroy(pat);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);
    cairo_surface_flush(dst);
    return dst;
}

static inline uint8_t float_to_u8(float f) {
    if (f <= 0.0f) {
        return 0;
    }
    if (f >= 1.0f) {
        return 255;
    }
    const float scaled = f * 255.0f + 0.5f;
    if (scaled <= 0.0f) {
        return 0;
    }
    if (scaled >= 255.0f) {
        return 255;
    }
    return (uint8_t)scaled;
}

struct wlr_buffer *fbwl_texture_render_buffer(const struct fbwl_texture *tex, int width, int height) {
    if (tex == NULL) {
        return NULL;
    }
    if (width < 1 || height < 1) {
        return NULL;
    }
    if (width > 8192) {
        width = 8192;
    }
    if (height > 8192) {
        height = 8192;
    }

    if ((tex->type & FBWL_TEXTURE_PARENTRELATIVE) != 0) {
        return NULL;
    }

    const uint32_t type = tex->type;

    const char *pixmap = tex->pixmap[0] != '\0' ? tex->pixmap : NULL;
    time_t pixmap_mtime = 0;

    if (pixmap != NULL) {
        struct stat st;
        if (stat(pixmap, &st) == 0) {
            pixmap_mtime = st.st_mtime;
        }

        cairo_surface_t *cached = texture_cache_lookup_surface(type, width, height,
            0, 0, 0,
            0, 0, 0,
            pixmap, pixmap_mtime);
        if (cached != NULL) {
            struct wlr_buffer *buf = fbwl_cairo_buffer_create(cached);
            if (buf != NULL) {
                return buf;
            }
            cairo_surface_destroy(cached);
        }

        cairo_surface_t *src = pixmap_surface_load(pixmap);
        if (src != NULL) {
            cairo_surface_t *scaled = NULL;
            if ((type & FBWL_TEXTURE_TILED) != 0) {
                scaled = surface_tile(src, width, height);
            } else {
                scaled = surface_scale_to_fit(src, width, height);
            }
            if (scaled != NULL) {
                struct wlr_buffer *buf = fbwl_cairo_buffer_create(scaled);
                if (buf != NULL) {
                    texture_cache_store_surface(type, width, height,
                        0, 0, 0,
                        0, 0, 0,
                        pixmap, pixmap_mtime,
                        scaled);
                    return buf;
                }
                cairo_surface_destroy(scaled);
            }
        }

        pixmap = NULL;
        pixmap_mtime = 0;
    }

    const uint8_t c_r = float_to_u8(tex->color[0]);
    const uint8_t c_g = float_to_u8(tex->color[1]);
    const uint8_t c_b = float_to_u8(tex->color[2]);
    const uint8_t c_to_r = float_to_u8(tex->color_to[0]);
    const uint8_t c_to_g = float_to_u8(tex->color_to[1]);
    const uint8_t c_to_b = float_to_u8(tex->color_to[2]);

    cairo_surface_t *cached = texture_cache_lookup_surface(type, width, height,
        c_r, c_g, c_b,
        c_to_r, c_to_g, c_to_b,
        NULL, 0);
    if (cached != NULL) {
        struct wlr_buffer *buf = fbwl_cairo_buffer_create(cached);
        if (buf != NULL) {
            return buf;
        }
        cairo_surface_destroy(cached);
    }

    cairo_surface_t *surface = NULL;
    if ((type & FBWL_TEXTURE_GRADIENT) != 0) {
        surface = fbwl_texture_render_gradient(type, tex->color, tex->color_to, width, height);
    } else {
        surface = fbwl_texture_render_solid(type, tex->color, tex->color_to, width, height);
    }
    if (surface == NULL) {
        return NULL;
    }

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    texture_cache_store_surface(type, width, height,
        c_r, c_g, c_b,
        c_to_r, c_to_g, c_to_b,
        NULL, 0,
        surface);

    return buf;
}
