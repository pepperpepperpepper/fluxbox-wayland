#include "wayland/fbwl_texture_render_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include <cairo/cairo.h>

#include "wayland/fbwl_texture.h"

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

struct fbwl_rgba8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

static inline void rgba8_brighten_8(struct fbwl_rgba8 *c) {
    if (c == NULL) {
        return;
    }
    c->r = (uint8_t)(c->r + (uint8_t)((255u - c->r) / 8u));
    c->g = (uint8_t)(c->g + (uint8_t)((255u - c->g) / 8u));
    c->b = (uint8_t)(c->b + (uint8_t)((255u - c->b) / 8u));
}

static inline void rgba8_darken_0_75(struct fbwl_rgba8 *c) {
    if (c == NULL) {
        return;
    }
    c->r = (uint8_t)(((uint32_t)c->r * 3u) / 4u);
    c->g = (uint8_t)(((uint32_t)c->g * 3u) / 4u);
    c->b = (uint8_t)(((uint32_t)c->b * 3u) / 4u);
}

static inline void pseudo_interlace(struct fbwl_rgba8 *c, bool interlaced, size_t y) {
    if (c == NULL || !interlaced) {
        return;
    }
    if ((y & 1u) == 0u) {
        rgba8_brighten_8(c);
    } else {
        rgba8_darken_0_75(c);
    }
}

static struct fbwl_rgba8 *gradient_buf = NULL;
static size_t gradient_buf_len = 0;

static struct fbwl_rgba8 *get_gradient_buf(size_t n) {
    if (n <= gradient_buf_len) {
        return gradient_buf;
    }
    struct fbwl_rgba8 *tmp = realloc(gradient_buf, n * sizeof(*tmp));
    if (tmp == NULL) {
        return NULL;
    }
    gradient_buf = tmp;
    gradient_buf_len = n;
    return gradient_buf;
}

static void invert_rgb(unsigned int width, unsigned int height, struct fbwl_rgba8 *rgba) {
    if (rgba == NULL || width == 0 || height == 0) {
        return;
    }
    struct fbwl_rgba8 *l = rgba;
    struct fbwl_rgba8 *r = rgba + (size_t)width * (size_t)height;
    if (r == l) {
        return;
    }
    for (--r; l < r; ++l, --r) {
        struct fbwl_rgba8 tmp = *l;
        *l = *r;
        *r = tmp;
    }
}

static void mirror_rgb(unsigned int width, unsigned int height, struct fbwl_rgba8 *rgba) {
    (void)height;
    if (rgba == NULL || width == 0) {
        return;
    }
    struct fbwl_rgba8 *l = rgba;
    struct fbwl_rgba8 *r = rgba + (size_t)width;
    if (r == l) {
        return;
    }
    for (--r; l < r; ++l, --r) {
        *r = *l;
    }
}

static void prepare_linear_table(size_t size, struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to, double scale) {
    if (rgba == NULL || from == NULL || to == NULL || size == 0) {
        return;
    }
    const double r = (double)from->r;
    const double g = (double)from->g;
    const double b = (double)from->b;

    const double delta_r = ((double)to->r - r) / (double)size;
    const double delta_g = ((double)to->g - g) / (double)size;
    const double delta_b = ((double)to->b - b) / (double)size;

    for (size_t i = 0; i < size; ++i) {
        rgba[i].r = (uint8_t)(scale * (r + (double)i * delta_r));
        rgba[i].g = (uint8_t)(scale * (g + (double)i * delta_g));
        rgba[i].b = (uint8_t)(scale * (b + (double)i * delta_b));
        rgba[i].a = 0;
    }
}

static void prepare_mirror_table(size_t size, struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to, double scale) {
    if (rgba == NULL || from == NULL || to == NULL || size == 0) {
        return;
    }
    const size_t half_size = (size >> 1) + (size & 1u);
    prepare_linear_table(half_size, &rgba[0], from, to, scale);
    mirror_rgb((unsigned int)size, 1u, rgba);
}

static void render_bevel1(unsigned int width, unsigned int height, struct fbwl_rgba8 *rgba) {
    if (!(width > 2u && height > 2u) || rgba == NULL) {
        return;
    }
    const size_t s = (size_t)width * (size_t)height;

    for (size_t i = 0; i < (size_t)width + 1u; ++i) {
        rgba8_brighten_8(&rgba[i]);
    }

    for (size_t i = 2u * (size_t)width - 1u; i < s - (size_t)width; i += (size_t)width) {
        rgba8_darken_0_75(&rgba[i]);
        rgba8_brighten_8(&rgba[i + 1u]);
    }

    size_t i = s - (size_t)width + 1u;
    for (; i < s; ++i) {
        rgba8_darken_0_75(&rgba[i]);
    }

    rgba8_darken_0_75(&rgba[i - 1u]);
    rgba8_darken_0_75(&rgba[i - (size_t)width]);
}

static void render_bevel2(unsigned int width, unsigned int height, struct fbwl_rgba8 *rgba) {
    if (!(width > 4u && height > 4u) || rgba == NULL) {
        return;
    }
    const size_t s = (size_t)width * (size_t)height;
    size_t i = (size_t)width + 1u;
    for (; i < 2u * (size_t)width - 2u; i++) {
        rgba8_brighten_8(&rgba[i]);
    }
    for (; i < (s - 2u * (size_t)width - 1u); i += (size_t)width) {
        rgba8_darken_0_75(&rgba[i]);
        rgba8_brighten_8(&rgba[i + 3u]);
    }
    for (i = (s - 2u * (size_t)width) + 2u; i < (s - (size_t)width - 1u); ++i) {
        rgba8_darken_0_75(&rgba[i]);
    }
}

static void render_horizontal_gradient(bool interlaced,
        unsigned int width, unsigned int height,
        struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to) {
    if (rgba == NULL || from == NULL || to == NULL) {
        return;
    }
    struct fbwl_rgba8 *gradient = get_gradient_buf((size_t)width);
    if (gradient == NULL) {
        return;
    }
    prepare_linear_table(width, gradient, from, to, 1.0);

    size_t i = 0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x, ++i) {
            rgba[i] = gradient[x];
            pseudo_interlace(&rgba[i], interlaced, y);
        }
    }
}

static void render_vertical_gradient(bool interlaced,
        unsigned int width, unsigned int height,
        struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to) {
    if (rgba == NULL || from == NULL || to == NULL) {
        return;
    }
    struct fbwl_rgba8 *gradient = get_gradient_buf((size_t)height);
    if (gradient == NULL) {
        return;
    }
    prepare_linear_table(height, gradient, from, to, 1.0);

    size_t i = 0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x, ++i) {
            rgba[i] = gradient[y];
            pseudo_interlace(&rgba[i], interlaced, y);
        }
    }
}

static void render_pyramid_gradient(bool interlaced,
        unsigned int width, unsigned int height,
        struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to) {
    if (rgba == NULL || from == NULL || to == NULL) {
        return;
    }
    const size_t s = (size_t)width + (size_t)height;
    struct fbwl_rgba8 *buf = get_gradient_buf(s);
    if (buf == NULL) {
        return;
    }
    struct fbwl_rgba8 *x_gradient = &buf[0];
    struct fbwl_rgba8 *y_gradient = &buf[width];

    prepare_mirror_table(width, x_gradient, from, to, 0.5);
    prepare_mirror_table(height, y_gradient, from, to, 0.5);

    size_t i = 0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x, ++i) {
            rgba[i].r = (uint8_t)(x_gradient[x].r + y_gradient[y].r);
            rgba[i].g = (uint8_t)(x_gradient[x].g + y_gradient[y].g);
            rgba[i].b = (uint8_t)(x_gradient[x].b + y_gradient[y].b);
            rgba[i].a = 0;
            pseudo_interlace(&rgba[i], interlaced, y);
        }
    }
}

static void render_rectangle_gradient(bool interlaced,
        unsigned int width, unsigned int height,
        struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to) {
    if (rgba == NULL || from == NULL || to == NULL) {
        return;
    }
    const size_t s = (size_t)width + (size_t)height;
    struct fbwl_rgba8 *buf = get_gradient_buf(s);
    if (buf == NULL) {
        return;
    }
    struct fbwl_rgba8 *x_gradient = &buf[0];
    struct fbwl_rgba8 *y_gradient = &buf[width];

    prepare_linear_table(width, x_gradient, from, to, 0.5);
    prepare_linear_table(height, y_gradient, from, to, 0.5);

    size_t i = 0;
    for (size_t y = 0; y < height; ++y) {
        const size_t y_idx = y < height / 2 ? y : (height - y - 1u);
        for (size_t x = 0; x < width; ++x, ++i) {
            const size_t x_idx = x < width / 2 ? x : (width - x - 1u);
            rgba[i].r = (uint8_t)(x_gradient[x_idx].r + y_gradient[y_idx].r);
            rgba[i].g = (uint8_t)(x_gradient[x_idx].g + y_gradient[y_idx].g);
            rgba[i].b = (uint8_t)(x_gradient[x_idx].b + y_gradient[y_idx].b);
            rgba[i].a = 0;
            pseudo_interlace(&rgba[i], interlaced, y);
        }
    }
}

static void render_pipecross_gradient(bool interlaced,
        unsigned int width, unsigned int height,
        struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to) {
    if (rgba == NULL || from == NULL || to == NULL) {
        return;
    }
    const size_t s = (size_t)width + (size_t)height;
    struct fbwl_rgba8 *buf = get_gradient_buf(s);
    if (buf == NULL) {
        return;
    }
    struct fbwl_rgba8 *x_gradient = &buf[0];
    struct fbwl_rgba8 *y_gradient = &buf[width];

    prepare_linear_table(width, x_gradient, from, to, 0.5);
    prepare_linear_table(height, y_gradient, from, to, 0.5);

    size_t i = 0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x, ++i) {
            rgba[i].r = (uint8_t)(x_gradient[x].r + y_gradient[y].r);
            rgba[i].g = (uint8_t)(x_gradient[x].g + y_gradient[y].g);
            rgba[i].b = (uint8_t)(x_gradient[x].b + y_gradient[y].b);
            rgba[i].a = 0;
            pseudo_interlace(&rgba[i], interlaced, y);
        }
    }
    mirror_rgb(width, height, rgba);
}

static void render_elliptic_gradient(bool interlaced,
        unsigned int width, unsigned int height,
        struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to) {
    if (rgba == NULL || from == NULL || to == NULL) {
        return;
    }
    const int c_x = (int)width / 2;
    const int c_y = (int)height / 2;
    const double r = (double)(c_x > c_y ? c_x : c_y);
    if (r <= 0.0) {
        return;
    }

    const double dr = ((double)to->r - (double)from->r) / r;
    const double dg = ((double)to->g - (double)from->g) / r;
    const double db = ((double)to->b - (double)from->b) / r;

    size_t i = 0;
    for (int y = 0; y < (int)height; ++y) {
        for (int x = 0; x < (int)width; ++x, ++i) {
            const double dx = (double)(x - c_x);
            const double dy = (double)(y - c_y);
            const double d = sqrt(dx * dx + dy * dy);

            rgba[i].r = (uint8_t)((double)from->r + (d * dr));
            rgba[i].g = (uint8_t)((double)from->g + (d * dg));
            rgba[i].b = (uint8_t)((double)from->b + (d * db));
            rgba[i].a = 0;

            pseudo_interlace(&rgba[i], interlaced, (size_t)y);
        }
    }
}

static void render_diagonal_gradient(bool interlaced,
        unsigned int width, unsigned int height,
        struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to) {
    if (rgba == NULL || from == NULL || to == NULL) {
        return;
    }
    const size_t s = (size_t)width + (size_t)height;
    struct fbwl_rgba8 *gradient = get_gradient_buf(s);
    if (gradient == NULL) {
        return;
    }

    prepare_linear_table(s, gradient, from, to, 1.0);

    size_t i = 0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x, ++i) {
            rgba[i] = gradient[x + y];
            pseudo_interlace(&rgba[i], interlaced, y);
        }
    }
}

static void render_crossdiagonal_gradient(bool interlaced,
        unsigned int width, unsigned int height,
        struct fbwl_rgba8 *rgba,
        const struct fbwl_rgba8 *from, const struct fbwl_rgba8 *to) {
    if (rgba == NULL || from == NULL || to == NULL) {
        return;
    }
    const size_t s = (size_t)width + (size_t)height;
    struct fbwl_rgba8 *buf = get_gradient_buf(s);
    if (buf == NULL) {
        return;
    }
    struct fbwl_rgba8 *x_gradient = &buf[0];
    struct fbwl_rgba8 *y_gradient = &buf[width];

    prepare_linear_table(width, x_gradient, to, from, 0.5);
    prepare_linear_table(height, y_gradient, from, to, 0.5);

    size_t i = 0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x, ++i) {
            rgba[i].r = (uint8_t)(x_gradient[x].r + y_gradient[y].r);
            rgba[i].g = (uint8_t)(x_gradient[x].g + y_gradient[y].g);
            rgba[i].b = (uint8_t)(x_gradient[x].b + y_gradient[y].b);
            rgba[i].a = 0;
            pseudo_interlace(&rgba[i], interlaced, y);
        }
    }
}

static void draw_hline(uint32_t *pixels, int w, int h, int x1, int x2, int y, uint32_t argb) {
    if (pixels == NULL || w < 1 || h < 1) {
        return;
    }
    if (y < 0 || y >= h) {
        return;
    }
    if (x1 > x2) {
        const int tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (x1 < 0) {
        x1 = 0;
    }
    if (x2 >= w) {
        x2 = w - 1;
    }
    if (x2 < x1) {
        return;
    }
    uint32_t *row = pixels + (size_t)y * (size_t)w;
    for (int x = x1; x <= x2; x++) {
        row[x] = argb;
    }
}

static void draw_vline(uint32_t *pixels, int w, int h, int x, int y1, int y2, uint32_t argb) {
    if (pixels == NULL || w < 1 || h < 1) {
        return;
    }
    if (x < 0 || x >= w) {
        return;
    }
    if (y1 > y2) {
        const int tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (y2 >= h) {
        y2 = h - 1;
    }
    if (y2 < y1) {
        return;
    }
    for (int y = y1; y <= y2; y++) {
        pixels[(size_t)y * (size_t)w + (size_t)x] = argb;
    }
}

static void draw_bevel_rectangle(uint32_t *pixels, int w, int h, int x1, int y1, int x2, int y2,
        uint32_t gc1, uint32_t gc2) {
    draw_hline(pixels, w, h, x1, x2, y1, gc1);
    draw_vline(pixels, w, h, x2, y1, y2, gc1);
    draw_hline(pixels, w, h, x1, x2, y2, gc2);
    draw_vline(pixels, w, h, x1, y1, y2, gc2);
}

static uint32_t rgba8_to_argb(const struct fbwl_rgba8 *c) {
    if (c == NULL) {
        return 0;
    }
    return 0xFF000000u | ((uint32_t)c->r << 16) | ((uint32_t)c->g << 8) | (uint32_t)c->b;
}

static cairo_surface_t *render_solid_texture(uint32_t type, const struct fbwl_rgba8 *color,
        const struct fbwl_rgba8 *color_to, int width, int height) {
    if (color == NULL || width < 1 || height < 1) {
        return NULL;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    uint8_t *data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);

    const uint32_t argb = rgba8_to_argb(color);
    const uint32_t argb_alt = rgba8_to_argb(color_to);
    for (int y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * (size_t)stride);
        for (int x = 0; x < width; x++) {
            row[x] = argb;
        }
        if ((type & FBWL_TEXTURE_INTERLACED) != 0 && (y % 2) == 0) {
            for (int x = 0; x < width; x++) {
                row[x] = argb_alt;
            }
        }
    }

    if (width > 1 && height > 1) {
        struct fbwl_rgba8 hi = *color;
        struct fbwl_rgba8 lo = *color;
        rgba8_brighten_8(&hi);
        rgba8_darken_0_75(&lo);
        const uint32_t hi_px = rgba8_to_argb(&hi);
        const uint32_t lo_px = rgba8_to_argb(&lo);

        if (height > 1 && width > 1 && (type & FBWL_TEXTURE_BEVEL1) != 0) {
            if ((type & FBWL_TEXTURE_RAISED) != 0) {
                draw_bevel_rectangle((uint32_t *)data, width, height, 0, height - 1, width - 1, 0, lo_px, hi_px);
            } else if ((type & FBWL_TEXTURE_SUNKEN) != 0) {
                draw_bevel_rectangle((uint32_t *)data, width, height, 0, height - 1, width - 1, 0, hi_px, lo_px);
            }
        } else if (width > 2 && height > 2 && (type & FBWL_TEXTURE_BEVEL2) != 0) {
            if ((type & FBWL_TEXTURE_RAISED) != 0) {
                draw_bevel_rectangle((uint32_t *)data, width, height, 1, height - 2, width - 2, 1, lo_px, hi_px);
            } else if ((type & FBWL_TEXTURE_SUNKEN) != 0) {
                draw_bevel_rectangle((uint32_t *)data, width, height, 1, height - 2, width - 2, 1, hi_px, lo_px);
            }
        }
    }

    cairo_surface_mark_dirty(surface);
    cairo_surface_flush(surface);
    return surface;
}

static cairo_surface_t *render_gradient_texture(uint32_t type, const struct fbwl_rgba8 *color,
        const struct fbwl_rgba8 *color_to, int width, int height) {
    if (color == NULL || color_to == NULL || width < 1 || height < 1) {
        return NULL;
    }

    const unsigned int w = (unsigned int)width;
    const unsigned int h = (unsigned int)height;
    const size_t s = (size_t)w * (size_t)h;
    if (s == 0 || s > (size_t)w * (size_t)h) {
        return NULL;
    }

    struct fbwl_rgba8 *rgba = calloc(s, sizeof(*rgba));
    if (rgba == NULL) {
        return NULL;
    }

    const struct fbwl_rgba8 *from = color;
    const struct fbwl_rgba8 *to = color_to;
    bool interlaced = (type & FBWL_TEXTURE_INTERLACED) != 0;
    bool inverted = (type & FBWL_TEXTURE_INVERT) != 0;

    if ((type & FBWL_TEXTURE_SUNKEN) != 0) {
        from = color_to;
        to = color;
        inverted = !inverted;
    }

    if ((type & FBWL_TEXTURE_HORIZONTAL) != 0) {
        render_horizontal_gradient(interlaced, w, h, rgba, from, to);
    } else if ((type & FBWL_TEXTURE_VERTICAL) != 0) {
        render_vertical_gradient(interlaced, w, h, rgba, from, to);
    } else if ((type & FBWL_TEXTURE_PYRAMID) != 0) {
        render_pyramid_gradient(interlaced, w, h, rgba, from, to);
    } else if ((type & FBWL_TEXTURE_RECTANGLE) != 0) {
        render_rectangle_gradient(interlaced, w, h, rgba, from, to);
    } else if ((type & FBWL_TEXTURE_PIPECROSS) != 0) {
        render_pipecross_gradient(interlaced, w, h, rgba, from, to);
    } else if ((type & FBWL_TEXTURE_ELLIPTIC) != 0) {
        render_elliptic_gradient(interlaced, w, h, rgba, from, to);
    } else if ((type & FBWL_TEXTURE_CROSSDIAGONAL) != 0) {
        render_crossdiagonal_gradient(interlaced, w, h, rgba, from, to);
    } else {
        render_diagonal_gradient(interlaced, w, h, rgba, from, to);
    }

    if ((type & FBWL_TEXTURE_BEVEL1) != 0) {
        render_bevel1(w, h, rgba);
    } else if ((type & FBWL_TEXTURE_BEVEL2) != 0) {
        render_bevel2(w, h, rgba);
    }

    if (inverted) {
        invert_rgb(w, h, rgba);
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        free(rgba);
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    uint8_t *data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    size_t i = 0;
    for (int y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * (size_t)stride);
        for (int x = 0; x < width; x++, i++) {
            row[x] = rgba8_to_argb(&rgba[i]);
        }
    }
    cairo_surface_mark_dirty(surface);
    free(rgba);
    cairo_surface_flush(surface);
    return surface;
}

cairo_surface_t *fbwl_texture_render_solid(uint32_t type,
        const float color[4],
        const float color_to[4],
        int width,
        int height) {
    if (color == NULL || color_to == NULL) {
        return NULL;
    }
    struct fbwl_rgba8 c = {
        .r = float_to_u8(color[0]),
        .g = float_to_u8(color[1]),
        .b = float_to_u8(color[2]),
        .a = 0,
    };
    struct fbwl_rgba8 c_to = {
        .r = float_to_u8(color_to[0]),
        .g = float_to_u8(color_to[1]),
        .b = float_to_u8(color_to[2]),
        .a = 0,
    };
    return render_solid_texture(type, &c, &c_to, width, height);
}

cairo_surface_t *fbwl_texture_render_gradient(uint32_t type,
        const float color[4],
        const float color_to[4],
        int width,
        int height) {
    if (color == NULL || color_to == NULL) {
        return NULL;
    }
    struct fbwl_rgba8 c = {
        .r = float_to_u8(color[0]),
        .g = float_to_u8(color[1]),
        .b = float_to_u8(color[2]),
        .a = 0,
    };
    struct fbwl_rgba8 c_to = {
        .r = float_to_u8(color_to[0]),
        .g = float_to_u8(color_to[1]),
        .b = float_to_u8(color_to[2]),
        .a = 0,
    };
    return render_gradient_texture(type, &c, &c_to, width, height);
}
