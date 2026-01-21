#include "wayland/fbwl_xdg_decoration.h"

#include <stdlib.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"

struct fbwl_xdg_decoration {
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    enum wlr_xdg_toplevel_decoration_v1_mode desired_mode;
    const struct fbwl_decor_theme *decor_theme;
    struct wl_listener surface_map;
    struct wl_listener request_mode;
    struct wl_listener destroy;
};

static void xdg_decoration_apply(struct fbwl_xdg_decoration *xd) {
    if (xd == NULL || xd->decoration == NULL || xd->decoration->toplevel == NULL ||
            xd->decoration->toplevel->base == NULL) {
        return;
    }
    if (!xd->decoration->toplevel->base->initialized) {
        return;
    }

    (void)wlr_xdg_toplevel_decoration_v1_set_mode(xd->decoration, xd->desired_mode);

    struct fbwl_view *view = NULL;
    if (xd->decoration->toplevel->base->data != NULL) {
        struct wlr_scene_tree *tree = xd->decoration->toplevel->base->data;
        view = tree->node.data;
    }
    if (view != NULL) {
        fbwl_view_decor_create(view, xd->decor_theme);
        fbwl_view_decor_set_enabled(view, xd->desired_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        fbwl_view_decor_update(view, xd->decor_theme);
    }
}

static void xdg_decoration_surface_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_xdg_decoration *xd = wl_container_of(listener, xd, surface_map);
    xdg_decoration_apply(xd);
    fbwl_cleanup_listener(&xd->surface_map);
}

static void xdg_decoration_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_xdg_decoration *xd = wl_container_of(listener, xd, destroy);
    fbwl_cleanup_listener(&xd->surface_map);
    fbwl_cleanup_listener(&xd->request_mode);
    fbwl_cleanup_listener(&xd->destroy);
    free(xd);
}

static void xdg_decoration_request_mode(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_xdg_decoration *xd = wl_container_of(listener, xd, request_mode);
    xd->desired_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    xdg_decoration_apply(xd);
}

static void handle_new_xdg_decoration(struct wl_listener *listener, void *data) {
    struct fbwl_xdg_decoration_state *state = wl_container_of(listener, state, new_toplevel_decoration);
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

    struct fbwl_xdg_decoration *xd = calloc(1, sizeof(*xd));
    xd->decoration = decoration;
    xd->desired_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    xd->decor_theme = state->decor_theme;

    xd->request_mode.notify = xdg_decoration_request_mode;
    wl_signal_add(&decoration->events.request_mode, &xd->request_mode);
    xd->destroy.notify = xdg_decoration_destroy;
    wl_signal_add(&decoration->events.destroy, &xd->destroy);

    if (decoration->toplevel != NULL && decoration->toplevel->base != NULL &&
            decoration->toplevel->base->initialized) {
        xdg_decoration_apply(xd);
    } else if (decoration->toplevel != NULL && decoration->toplevel->base != NULL &&
            decoration->toplevel->base->surface != NULL) {
        xd->surface_map.notify = xdg_decoration_surface_map;
        wl_signal_add(&decoration->toplevel->base->surface->events.map, &xd->surface_map);
    }
}

bool fbwl_xdg_decoration_init(struct fbwl_xdg_decoration_state *state, struct wl_display *display,
        const struct fbwl_decor_theme *decor_theme) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->manager = wlr_xdg_decoration_manager_v1_create(display);
    if (state->manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create xdg decoration manager");
        return false;
    }

    state->decor_theme = decor_theme;
    state->new_toplevel_decoration.notify = handle_new_xdg_decoration;
    wl_signal_add(&state->manager->events.new_toplevel_decoration, &state->new_toplevel_decoration);
    return true;
}

void fbwl_xdg_decoration_finish(struct fbwl_xdg_decoration_state *state) {
    if (state == NULL) {
        return;
    }

    fbwl_cleanup_listener(&state->new_toplevel_decoration);
    state->manager = NULL;
}
