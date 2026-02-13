#pragma once

#include <cairo/cairo.h>
#include <stdint.h>

cairo_surface_t *fbwl_texture_render_solid(uint32_t type,
    const float color[4],
    const float color_to[4],
    int width,
    int height);

cairo_surface_t *fbwl_texture_render_gradient(uint32_t type,
    const float color[4],
    const float color_to[4],
    int width,
    int height);
