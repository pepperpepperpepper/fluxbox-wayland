#pragma once

#include <stdbool.h>

struct fbwl_decor_theme;

bool fbwl_style_parse_toolbar_slit(struct fbwl_decor_theme *theme, const char *key, char *val);

// Apply Fluxbox/X11 fallback semantics for toolbar/slit theme keys.
//
// This is intentionally separate from parsing to preserve fallback behavior
// across multiple style loads (e.g. style + overlay) by consulting the
// `*_explicit` flags stored in struct fbwl_decor_theme.
void fbwl_style_apply_toolbar_slit_fallbacks(struct fbwl_decor_theme *theme);

