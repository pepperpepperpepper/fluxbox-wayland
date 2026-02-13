#pragma once

#include <stdbool.h>
#include <stdint.h>

// Decoration mask bits and presets, matching Fluxbox/X11 WindowState.
enum fbwl_deco_mask_bits {
    FBWL_DECORM_TITLEBAR = (1u << 0),
    FBWL_DECORM_HANDLE   = (1u << 1),
    FBWL_DECORM_BORDER   = (1u << 2),
    FBWL_DECORM_ICONIFY  = (1u << 3),
    FBWL_DECORM_MAXIMIZE = (1u << 4),
    FBWL_DECORM_CLOSE    = (1u << 5),
    FBWL_DECORM_MENU     = (1u << 6),
    FBWL_DECORM_STICKY   = (1u << 7),
    FBWL_DECORM_SHADE    = (1u << 8),
    FBWL_DECORM_TAB      = (1u << 9),
    FBWL_DECORM_ENABLED  = (1u << 10),
    FBWL_DECORM_LAST     = (1u << 11),
};

enum fbwl_deco_mask_preset {
    FBWL_DECOR_NONE   = 0u,
    FBWL_DECOR_NORMAL = FBWL_DECORM_LAST - 1u,
    FBWL_DECOR_TINY   = FBWL_DECORM_TITLEBAR | FBWL_DECORM_ICONIFY,
    FBWL_DECOR_TOOL   = FBWL_DECORM_TITLEBAR,
    FBWL_DECOR_BORDER = FBWL_DECORM_BORDER,
    FBWL_DECOR_TAB    = FBWL_DECORM_BORDER | FBWL_DECORM_TAB,
};

static inline bool fbwl_deco_mask_has_frame(uint32_t mask) {
    return (mask & (FBWL_DECORM_TITLEBAR | FBWL_DECORM_HANDLE | FBWL_DECORM_BORDER | FBWL_DECORM_TAB)) != 0;
}

bool fbwl_deco_mask_parse(const char *s, uint32_t *out_mask);

// Returns the canonical preset name for a known mask (e.g. "NORMAL"), or NULL.
const char *fbwl_deco_mask_preset_name(uint32_t mask);

