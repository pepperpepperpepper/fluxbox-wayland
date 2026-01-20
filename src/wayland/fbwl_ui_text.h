#pragma once

#include <cairo/cairo.h>

struct wlr_buffer;

struct wlr_buffer *fbwl_cairo_buffer_create(cairo_surface_t *surface);
struct wlr_buffer *fbwl_text_buffer_create(const char *text, int width, int height,
    int pad_x, const float rgba[static 4]);
