#include "wayland/fbwl_scene_layers.h"

#include <stdlib.h>

#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_output.h"
#include "wayland/fbwl_util.h"

struct fbwl_layer_surface {
    struct wl_list link;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;
    enum zwlr_layer_shell_v1_layer layer;

    struct wlr_output_layout *output_layout;
    struct wl_list *outputs;
    struct wl_list *layer_surfaces;

    struct wlr_scene_tree *layer_background;
    struct wlr_scene_tree *layer_bottom;
    struct wlr_scene_tree *layer_top;
    struct wlr_scene_tree *layer_overlay;
    struct wlr_scene_tree *scene_root;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static struct wlr_scene_tree *scene_tree_for_layer(struct wlr_scene_tree *layer_background,
        struct wlr_scene_tree *layer_bottom,
        struct wlr_scene_tree *layer_top,
        struct wlr_scene_tree *layer_overlay,
        enum zwlr_layer_shell_v1_layer layer) {
    switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return layer_background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return layer_bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return layer_top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return layer_overlay;
    default:
        return layer_top;
    }
}

void fbwl_scene_layers_arrange_layer_surfaces_on_output(struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        struct wl_list *layer_surfaces,
        struct wlr_output *wlr_output) {
    if (output_layout == NULL || outputs == NULL || layer_surfaces == NULL || wlr_output == NULL) {
        return;
    }

    struct wlr_box full = {0};
    wlr_output_layout_get_box(output_layout, wlr_output, &full);
    if (full.width < 1 || full.height < 1) {
        return;
    }

    struct wlr_box usable = full;
    const enum zwlr_layer_shell_v1_layer layers[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    };

    for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); i++) {
        const enum zwlr_layer_shell_v1_layer layer = layers[i];
        struct fbwl_layer_surface *ls;
        wl_list_for_each(ls, layer_surfaces, link) {
            if (ls->layer_surface == NULL || ls->scene_layer_surface == NULL) {
                continue;
            }
            if (ls->layer_surface->output != wlr_output) {
                continue;
            }
            if (ls->layer != layer) {
                continue;
            }
            if (!ls->layer_surface->initialized) {
                continue;
            }
            wlr_scene_layer_surface_v1_configure(ls->scene_layer_surface, &full, &usable);

            int lx = 0, ly = 0;
            (void)wlr_scene_node_coords(&ls->scene_layer_surface->tree->node, &lx, &ly);
            const char *ns = ls->layer_surface->namespace != NULL ? ls->layer_surface->namespace : "(no-namespace)";
            const struct wlr_layer_surface_v1_state *st = &ls->layer_surface->current;
            wlr_log(WLR_INFO, "LayerShell: surface ns=%s layer=%d pos=%d,%d size=%ux%u excl=%d",
                ns, (int)ls->layer, lx, ly, st->actual_width, st->actual_height, st->exclusive_zone);
        }
    }

    struct fbwl_output *out = fbwl_output_find(outputs, wlr_output);
    if (out != NULL) {
        out->usable_area = usable;
    }
    wlr_log(WLR_INFO, "LayerShell: output=%s usable=%d,%d %dx%d",
        wlr_output->name != NULL ? wlr_output->name : "(unnamed)",
        usable.x, usable.y, usable.width, usable.height);
}

static void layer_surface_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_layer_surface *ls = wl_container_of(listener, ls, map);
    const char *ns = ls->layer_surface->namespace != NULL ? ls->layer_surface->namespace : "(no-namespace)";
    wlr_log(WLR_INFO, "LayerShell: map ns=%s layer=%d", ns, (int)ls->layer);
    fbwl_scene_layers_arrange_layer_surfaces_on_output(ls->output_layout, ls->outputs, ls->layer_surfaces,
        ls->layer_surface->output);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_layer_surface *ls = wl_container_of(listener, ls, unmap);
    const char *ns = ls->layer_surface->namespace != NULL ? ls->layer_surface->namespace : "(no-namespace)";
    wlr_log(WLR_INFO, "LayerShell: unmap ns=%s layer=%d", ns, (int)ls->layer);
    fbwl_scene_layers_arrange_layer_surfaces_on_output(ls->output_layout, ls->outputs, ls->layer_surfaces,
        ls->layer_surface->output);
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_layer_surface *ls = wl_container_of(listener, ls, commit);

    enum zwlr_layer_shell_v1_layer new_layer = ls->layer_surface->current.layer;
    if (new_layer != ls->layer) {
        ls->layer = new_layer;
        struct wlr_scene_tree *parent = scene_tree_for_layer(ls->layer_background, ls->layer_bottom, ls->layer_top,
            ls->layer_overlay, ls->layer);
        if (parent != NULL && ls->scene_layer_surface != NULL) {
            wlr_scene_node_reparent(&ls->scene_layer_surface->tree->node, parent);
        }
    }

    fbwl_scene_layers_arrange_layer_surfaces_on_output(ls->output_layout, ls->outputs, ls->layer_surfaces,
        ls->layer_surface->output);
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_layer_surface *ls = wl_container_of(listener, ls, destroy);
    struct wlr_output_layout *output_layout = ls->output_layout;
    struct wl_list *outputs = ls->outputs;
    struct wl_list *layer_surfaces = ls->layer_surfaces;
    struct wlr_output *output = ls->layer_surface->output;
    const char *ns = ls->layer_surface->namespace != NULL ? ls->layer_surface->namespace : "(no-namespace)";
    wlr_log(WLR_INFO, "LayerShell: destroy ns=%s layer=%d", ns, (int)ls->layer);

    fbwl_cleanup_listener(&ls->map);
    fbwl_cleanup_listener(&ls->unmap);
    fbwl_cleanup_listener(&ls->commit);
    fbwl_cleanup_listener(&ls->destroy);
    wl_list_remove(&ls->link);
    free(ls);

    fbwl_scene_layers_arrange_layer_surfaces_on_output(output_layout, outputs, layer_surfaces, output);
}

void fbwl_scene_layers_handle_new_layer_surface(struct wlr_layer_surface_v1 *layer_surface,
        struct wlr_output_layout *output_layout,
        struct wl_list *outputs,
        struct wl_list *layer_surfaces,
        struct wlr_scene_tree *layer_background,
        struct wlr_scene_tree *layer_bottom,
        struct wlr_scene_tree *layer_top,
        struct wlr_scene_tree *layer_overlay,
        struct wlr_scene_tree *scene_root) {
    if (layer_surface == NULL || output_layout == NULL || outputs == NULL || layer_surfaces == NULL ||
            scene_root == NULL) {
        return;
    }

    if (layer_surface->output == NULL) {
        layer_surface->output = wlr_output_layout_get_center_output(output_layout);
        if (layer_surface->output == NULL && !wl_list_empty(outputs)) {
            struct fbwl_output *fallback = wl_container_of(outputs->next, fallback, link);
            layer_surface->output = fallback->wlr_output;
        }
        if (layer_surface->output == NULL) {
            wlr_log(WLR_ERROR, "LayerShell: new_surface without any outputs, closing");
            wlr_layer_surface_v1_destroy(layer_surface);
            return;
        }
    }

    struct fbwl_layer_surface *ls = calloc(1, sizeof(*ls));
    ls->layer_surface = layer_surface;
    ls->layer = layer_surface->pending.layer;
    ls->output_layout = output_layout;
    ls->outputs = outputs;
    ls->layer_surfaces = layer_surfaces;
    ls->layer_background = layer_background;
    ls->layer_bottom = layer_bottom;
    ls->layer_top = layer_top;
    ls->layer_overlay = layer_overlay;
    ls->scene_root = scene_root;

    struct wlr_scene_tree *parent = scene_tree_for_layer(layer_background, layer_bottom, layer_top, layer_overlay,
        ls->layer);
    if (parent == NULL) {
        parent = scene_root;
    }
    ls->scene_layer_surface = wlr_scene_layer_surface_v1_create(parent, layer_surface);

    wl_list_insert(layer_surfaces->prev, &ls->link);

    ls->map.notify = layer_surface_map;
    wl_signal_add(&layer_surface->surface->events.map, &ls->map);
    ls->unmap.notify = layer_surface_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);
    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);

    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &ls->destroy);

    layer_surface->data = ls;

    const char *ns = layer_surface->namespace != NULL ? layer_surface->namespace : "(no-namespace)";
    wlr_log(WLR_INFO, "LayerShell: new_surface ns=%s layer=%d output=%s",
        ns, (int)ls->layer,
        layer_surface->output->name != NULL ? layer_surface->output->name : "(unnamed)");

    fbwl_scene_layers_arrange_layer_surfaces_on_output(output_layout, outputs, layer_surfaces, layer_surface->output);
}

