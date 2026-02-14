#pragma once

#include <stdbool.h>

#include "wayland/fbwl_view.h"

struct wlr_buffer;

struct wlr_buffer *fbwl_decor_icon_render_builtin(enum fbwl_decor_hit_kind kind, bool toggled, int size_px,
    const float rgba[static 4]);

struct wlr_buffer *fbwl_decor_icon_render_pixmap(const char *pixmap_path, int size_px);
