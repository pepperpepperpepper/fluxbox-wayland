#pragma once

// Best-effort icon lookup for usePixmap features (iconbar, menus, etc).
// Returns a malloc()'d path on success, NULL on failure.

char *fbwl_icon_theme_resolve_path(const char *icon_name);

