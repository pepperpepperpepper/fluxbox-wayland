#pragma once

#include <cairo/cairo.h>

struct wlr_buffer;
struct fbwl_text_effect;

struct wlr_buffer *fbwl_cairo_buffer_create(cairo_surface_t *surface);
struct wlr_buffer *fbwl_text_buffer_create(const char *text, int width, int height,
    int pad_x, const float rgba[static 4], const char *font, const struct fbwl_text_effect *effect, int justify);
struct wlr_buffer *fbwl_text_buffer_create_underlined(const char *text, int width, int height,
    int pad_x, const float rgba[static 4], const char *font, const struct fbwl_text_effect *effect, int justify,
    const float underline_rgba[static 4], const char *underline_font, int underline_justify,
    int underline_start_byte, int underline_len_bytes);
bool fbwl_text_measure(const char *text, int height, const char *font, int *out_w, int *out_h);
bool fbwl_text_fits(const char *text, int width, int height, int pad_x, const char *font);
