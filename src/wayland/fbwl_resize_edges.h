#pragma once

#include <stdint.h>

struct fbwl_view;

// Returns a WLR_EDGE_* bitmask matching Fluxbox/X11 StartResizing semantics.
uint32_t fbwl_resize_edges_from_startresizing_args(const struct fbwl_view *view,
    int cursor_x, int cursor_y, const char *args);

