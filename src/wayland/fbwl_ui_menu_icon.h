#pragma once

#include <stdint.h>

struct wlr_buffer;

// Configure the internal icon buffer cache (best-effort).
// Values match Fluxbox resources:
//   - session.cacheLife (minutes)
//   - session.cacheMax (kB)
// Setting either value to 0 disables caching and clears any existing entries.
void fbwl_ui_menu_icon_cache_configure(int cache_life_minutes, int cache_max_kb);

struct wlr_buffer *fbwl_ui_menu_icon_buffer_create(const char *path, int icon_px);
struct wlr_buffer *fbwl_ui_menu_icon_buffer_create_argb32(const uint32_t *argb, int w, int h, int icon_px);
