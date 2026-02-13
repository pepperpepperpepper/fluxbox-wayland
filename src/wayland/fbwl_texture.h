#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct wlr_buffer;

// Fluxbox-compatible texture bits, modeled after src/FbTk/Texture.hh.
enum fbwl_texture_type_bits {
    FBWL_TEXTURE_NONE = 0x00000u,

    // Bevel "level"
    FBWL_TEXTURE_FLAT   = 0x00002u,
    FBWL_TEXTURE_SUNKEN = 0x00004u,
    FBWL_TEXTURE_RAISED = 0x00008u,

    // Base texture type
    FBWL_TEXTURE_SOLID    = 0x00010u,
    FBWL_TEXTURE_GRADIENT = 0x00020u,

    // Gradient variants (only meaningful with GRADIENT)
    FBWL_TEXTURE_HORIZONTAL    = 0x00040u,
    FBWL_TEXTURE_VERTICAL      = 0x00080u,
    FBWL_TEXTURE_DIAGONAL      = 0x00100u,
    FBWL_TEXTURE_CROSSDIAGONAL = 0x00200u,
    FBWL_TEXTURE_RECTANGLE     = 0x00400u,
    FBWL_TEXTURE_PYRAMID       = 0x00800u,
    FBWL_TEXTURE_PIPECROSS     = 0x01000u,
    FBWL_TEXTURE_ELLIPTIC      = 0x02000u,

    // Extra flags
    FBWL_TEXTURE_BEVEL1         = 0x04000u,
    FBWL_TEXTURE_BEVEL2         = 0x08000u,
    FBWL_TEXTURE_INVERT         = 0x10000u,
    FBWL_TEXTURE_PARENTRELATIVE = 0x20000u,
    FBWL_TEXTURE_INTERLACED     = 0x40000u,
    FBWL_TEXTURE_TILED          = 0x80000u,
};

struct fbwl_texture {
    uint32_t type;
    float color[4];
    float color_to[4];
    float pic_color[4];
    char pixmap[256];
};

void fbwl_texture_init(struct fbwl_texture *tex);

// Parse Fluxbox-style texture strings, e.g.:
//   "Raised Gradient Vertical Bevel1 Interlaced"
//   "ParentRelative"
//
// Note: "pixmap" is handled by setting tex->pixmap separately; this parser
// intentionally mirrors Fluxbox's substring-based matching behavior.
bool fbwl_texture_parse_type(const char *s, uint32_t *out_type);

static inline bool fbwl_texture_is_parentrelative(const struct fbwl_texture *tex) {
    return tex != NULL && (tex->type & FBWL_TEXTURE_PARENTRELATIVE) != 0;
}

// Render a texture into an ARGB8888 wlr_buffer.
//
// Returns NULL for ParentRelative (caller should use wallpaper-backed underlays).
struct wlr_buffer *fbwl_texture_render_buffer(const struct fbwl_texture *tex, int width, int height);

// Configure the internal texture/pixmap surface cache.
//
// Uses Fluxbox/X11 semantics:
//   - cache_life_minutes: session.cacheLife (minutes)
//   - cache_max_kb: session.cacheMax (kB)
void fbwl_texture_cache_configure(int cache_life_minutes, int cache_max_kb);
