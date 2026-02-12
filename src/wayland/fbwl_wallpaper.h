#pragma once

enum fbwl_wallpaper_mode {
    FBWL_WALLPAPER_MODE_STRETCH = 0,
    FBWL_WALLPAPER_MODE_FILL,
    FBWL_WALLPAPER_MODE_CENTER,
    FBWL_WALLPAPER_MODE_TILE,
};

enum fbwl_wallpaper_mode fbwl_wallpaper_mode_parse(const char *s);
const char *fbwl_wallpaper_mode_str(enum fbwl_wallpaper_mode mode);
