#pragma once

struct wlr_buffer;
struct wlr_xwayland;
struct wlr_xwayland_surface;

struct wlr_buffer *fbwl_xwayland_icon_buffer_create(struct wlr_xwayland *xwayland,
    const struct wlr_xwayland_surface *xsurface, int icon_px);

