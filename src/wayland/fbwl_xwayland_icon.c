#include "wayland/fbwl_xwayland_icon.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_ui_menu_icon.h"

static xcb_atom_t intern_atom(xcb_connection_t *conn, const char *name) {
    if (conn == NULL || name == NULL) {
        return XCB_ATOM_NONE;
    }
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return XCB_ATOM_NONE;
    }
    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

struct wlr_buffer *fbwl_xwayland_icon_buffer_create(struct wlr_xwayland *xwayland,
        const struct wlr_xwayland_surface *xsurface, int icon_px) {
    if (xwayland == NULL || xsurface == NULL || icon_px < 1) {
        return NULL;
    }

    xcb_connection_t *conn = wlr_xwayland_get_xwm_connection(xwayland);
    if (conn == NULL) {
        return NULL;
    }

    xcb_atom_t atom_net_wm_icon = intern_atom(conn, "_NET_WM_ICON");
    if (atom_net_wm_icon == XCB_ATOM_NONE) {
        return NULL;
    }

    // Cap property payload to 4 MiB to avoid pathological allocations.
    // xcb_get_property's long_length is in 32-bit units.
    const uint32_t max_u32 = 1024u * 1024u;

    xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, xsurface->window_id,
        atom_net_wm_icon, XCB_ATOM_CARDINAL, 0, max_u32);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(conn, cookie, NULL);
    if (reply == NULL) {
        return NULL;
    }

    struct wlr_buffer *buf = NULL;

    if (reply->format != 32) {
        goto out;
    }

    const size_t n_u32 = (size_t)(xcb_get_property_value_length(reply) / 4);
    if (n_u32 < 2) {
        goto out;
    }

    const uint32_t *vals = (const uint32_t *)xcb_get_property_value(reply);
    if (vals == NULL) {
        goto out;
    }

    int best_w = 0;
    int best_h = 0;
    const uint32_t *best_pixels = NULL;
    bool best_is_ge = false;
    int best_dim = 0;

    size_t pos = 0;
    while (pos + 2 <= n_u32) {
        const uint32_t w_u32 = vals[pos++];
        const uint32_t h_u32 = vals[pos++];

        if (w_u32 < 1 || h_u32 < 1 || w_u32 > 2048 || h_u32 > 2048) {
            break;
        }

        const size_t pixels = (size_t)w_u32 * (size_t)h_u32;
        if (pixels > n_u32 - pos) {
            break;
        }

        const uint32_t *pixels_ptr = vals + pos;
        pos += pixels;

        const int w = (int)w_u32;
        const int h = (int)h_u32;
        const int dim = w > h ? w : h;
        const bool is_ge = dim >= icon_px;

        if (best_pixels == NULL) {
            best_w = w;
            best_h = h;
            best_dim = dim;
            best_pixels = pixels_ptr;
            best_is_ge = is_ge;
            continue;
        }

        if (is_ge) {
            if (!best_is_ge || dim < best_dim) {
                best_w = w;
                best_h = h;
                best_dim = dim;
                best_pixels = pixels_ptr;
                best_is_ge = true;
            }
        } else if (!best_is_ge && dim > best_dim) {
            best_w = w;
            best_h = h;
            best_dim = dim;
            best_pixels = pixels_ptr;
        }
    }

    if (best_pixels == NULL) {
        goto out;
    }

    buf = fbwl_ui_menu_icon_buffer_create_argb32(best_pixels, best_w, best_h, icon_px);
    if (buf == NULL) {
        wlr_log(WLR_DEBUG, "XWayland: failed to build _NET_WM_ICON buffer (win=0x%x)", (unsigned)xsurface->window_id);
        goto out;
    }

out:
    free(reply);
    return buf;
}
