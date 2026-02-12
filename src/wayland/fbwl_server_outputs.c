#include "wayland/fbwl_server_internal.h"

#include "wayland/fbwl_output.h"
#include "wayland/fbwl_output_management.h"
#include "wayland/fbwl_output_power.h"
#include "wayland/fbwl_scene_layers.h"
#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <drm_fourcc.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

static uint8_t float_to_u8_clamped(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

static uint32_t rgb24_from_rgba(const float rgba[4]) {
    if (rgba == NULL) {
        return 0;
    }
    const uint32_t r = float_to_u8_clamped(rgba[0]);
    const uint32_t g = float_to_u8_clamped(rgba[1]);
    const uint32_t b = float_to_u8_clamped(rgba[2]);
    return (r << 16) | (g << 8) | b;
}

static bool wallpaper_path_is_regular_file(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

static struct wlr_buffer *wallpaper_buffer_from_png_path(const char *path) {
    if (!wallpaper_path_is_regular_file(path)) {
        return NULL;
    }

    cairo_surface_t *loaded = cairo_image_surface_create_from_png(path);
    if (loaded == NULL || cairo_surface_status(loaded) != CAIRO_STATUS_SUCCESS) {
        if (loaded != NULL) {
            cairo_surface_destroy(loaded);
        }
        return NULL;
    }

    const int w = cairo_image_surface_get_width(loaded);
    const int h = cairo_image_surface_get_height(loaded);
    if (w < 1 || h < 1 || w > 8192 || h > 8192) {
        cairo_surface_destroy(loaded);
        return NULL;
    }

    cairo_surface_t *surface = loaded;
    if (cairo_image_surface_get_format(loaded) != CAIRO_FORMAT_ARGB32) {
        cairo_surface_t *converted = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        if (converted == NULL || cairo_surface_status(converted) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(loaded);
            if (converted != NULL) {
                cairo_surface_destroy(converted);
            }
            return NULL;
        }

        cairo_t *cr = cairo_create(converted);
        cairo_set_source_surface(cr, loaded, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);

        cairo_surface_destroy(loaded);
        surface = converted;
    }

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

static struct wlr_buffer *wallpaper_tile_buffer_create(struct wlr_buffer *wallpaper_buf, const struct wlr_box *output_box,
        const float background_color[4]) {
    if (wallpaper_buf == NULL || output_box == NULL) {
        return NULL;
    }

    if (output_box->width < 1 || output_box->height < 1 || wallpaper_buf->width < 1 || wallpaper_buf->height < 1) {
        return NULL;
    }

    void *data = NULL;
    uint32_t format = 0;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(wallpaper_buf, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
        return NULL;
    }

    if (format != DRM_FORMAT_ARGB8888 && format != DRM_FORMAT_XRGB8888) {
        wlr_buffer_end_data_ptr_access(wallpaper_buf);
        return NULL;
    }

    cairo_surface_t *src = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
        wallpaper_buf->width, wallpaper_buf->height, (int)stride);
    if (src == NULL || cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        if (src != NULL) {
            cairo_surface_destroy(src);
        }
        wlr_buffer_end_data_ptr_access(wallpaper_buf);
        return NULL;
    }

    cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, output_box->width, output_box->height);
    if (dst == NULL || cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src);
        if (dst != NULL) {
            cairo_surface_destroy(dst);
        }
        wlr_buffer_end_data_ptr_access(wallpaper_buf);
        return NULL;
    }

    cairo_t *cr = cairo_create(dst);
    if (background_color != NULL) {
        cairo_set_source_rgba(cr, background_color[0], background_color[1], background_color[2], 1.0);
    } else {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    }
    cairo_paint(cr);

    cairo_set_source_surface(cr, src, -(double)output_box->x, -(double)output_box->y);
    cairo_pattern_t *pat = cairo_get_source(cr);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(pat, CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_destroy(cr);

    cairo_surface_destroy(src);
    wlr_buffer_end_data_ptr_access(wallpaper_buf);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(dst);
    if (buf == NULL) {
        cairo_surface_destroy(dst);
        return NULL;
    }
    return buf;
}

static bool wallpaper_fill_src_box(const struct wlr_buffer *wallpaper_buf, const struct wlr_box *output_box,
        struct wlr_fbox *out_src_box) {
    if (wallpaper_buf == NULL || output_box == NULL || out_src_box == NULL) {
        return false;
    }

    if (output_box->width < 1 || output_box->height < 1 || wallpaper_buf->width < 1 || wallpaper_buf->height < 1) {
        return false;
    }

    const double buf_w = (double)wallpaper_buf->width;
    const double buf_h = (double)wallpaper_buf->height;
    const double out_aspect = (double)output_box->width / (double)output_box->height;
    const double buf_aspect = buf_w / buf_h;

    struct wlr_fbox src = {
        .x = 0.0,
        .y = 0.0,
        .width = buf_w,
        .height = buf_h,
    };

    if (buf_aspect > out_aspect) {
        const double crop_w = buf_h * out_aspect;
        if (crop_w > 0.0 && crop_w < buf_w) {
            src.width = crop_w;
            src.x = (buf_w - crop_w) / 2.0;
        }
    } else if (buf_aspect < out_aspect) {
        const double crop_h = buf_w / out_aspect;
        if (crop_h > 0.0 && crop_h < buf_h) {
            src.height = crop_h;
            src.y = (buf_h - crop_h) / 2.0;
        }
    }

    if (src.width <= 0.0 || src.height <= 0.0) {
        return false;
    }

    *out_src_box = src;
    return true;
}

static void server_background_update_output(struct fbwl_server *server, struct fbwl_output *output) {
    if (server == NULL || output == NULL || server->output_layout == NULL || server->layer_background == NULL ||
            output->wlr_output == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);

    if (box.width < 1 || box.height < 1) {
        if (output->background_image != NULL) {
            wlr_scene_node_destroy(&output->background_image->node);
            output->background_image = NULL;
        }
        if (output->background_rect != NULL) {
            wlr_scene_node_destroy(&output->background_rect->node);
            output->background_rect = NULL;
        }
        if (output->wallpaper_tile_buf != NULL) {
            wlr_buffer_drop(output->wallpaper_tile_buf);
            output->wallpaper_tile_buf = NULL;
        }
        return;
    }

    if (output->wallpaper_tile_buf != NULL &&
            (server->wallpaper_buf == NULL || server->wallpaper_mode != FBWL_WALLPAPER_MODE_TILE)) {
        wlr_buffer_drop(output->wallpaper_tile_buf);
        output->wallpaper_tile_buf = NULL;
    }

    if (server->wallpaper_buf != NULL) {
        if (output->background_image == NULL) {
            output->background_image = wlr_scene_buffer_create(server->layer_background, server->wallpaper_buf);
            if (output->background_image == NULL) {
                wlr_log(WLR_ERROR, "Background: failed to create wallpaper buffer");
            }
        }

        struct wlr_buffer *wallpaper_buf = server->wallpaper_buf;
        if (server->wallpaper_mode == FBWL_WALLPAPER_MODE_TILE) {
            struct wlr_buffer *tiled = wallpaper_tile_buffer_create(server->wallpaper_buf, &box, server->background_color);
            if (tiled != NULL) {
                if (output->wallpaper_tile_buf != NULL) {
                    wlr_buffer_drop(output->wallpaper_tile_buf);
                }
                output->wallpaper_tile_buf = tiled;
                wallpaper_buf = tiled;
            } else if (output->wallpaper_tile_buf != NULL) {
                wlr_buffer_drop(output->wallpaper_tile_buf);
                output->wallpaper_tile_buf = NULL;
            }
        }

        if (output->background_image != NULL) {
            wlr_scene_buffer_set_buffer(output->background_image, wallpaper_buf);
        }

        if (output->background_image != NULL) {
            int dest_x = box.x;
            int dest_y = box.y;
            int dest_w = box.width;
            int dest_h = box.height;

            struct wlr_fbox src = {0};
            const struct wlr_fbox *src_box = NULL;
            if (server->wallpaper_mode == FBWL_WALLPAPER_MODE_TILE) {
                src_box = NULL;
            } else if (server->wallpaper_mode == FBWL_WALLPAPER_MODE_FILL) {
                if (wallpaper_fill_src_box(server->wallpaper_buf, &box, &src)) {
                    src_box = &src;
                }
            } else if (server->wallpaper_mode == FBWL_WALLPAPER_MODE_CENTER) {
                dest_w = wallpaper_buf->width;
                dest_h = wallpaper_buf->height;
                dest_x = box.x + (box.width - dest_w) / 2;
                dest_y = box.y + (box.height - dest_h) / 2;
            }

            wlr_scene_buffer_set_source_box(output->background_image, src_box);
            wlr_scene_buffer_set_dest_size(output->background_image, dest_w, dest_h);
            wlr_scene_node_set_position(&output->background_image->node, dest_x, dest_y);

            if (server->wallpaper_mode == FBWL_WALLPAPER_MODE_CENTER) {
                if (output->background_rect == NULL) {
                    output->background_rect =
                        wlr_scene_rect_create(server->layer_background, box.width, box.height, server->background_color);
                } else {
                    wlr_scene_rect_set_size(output->background_rect, box.width, box.height);
                    wlr_scene_rect_set_color(output->background_rect, server->background_color);
                }
                if (output->background_rect != NULL) {
                    wlr_scene_node_set_position(&output->background_rect->node, box.x, box.y);
                    wlr_scene_node_lower_to_bottom(&output->background_rect->node);
                }
                wlr_scene_node_raise_to_top(&output->background_image->node);
            } else if (output->background_rect != NULL) {
                wlr_scene_node_destroy(&output->background_rect->node);
                output->background_rect = NULL;
                wlr_scene_node_lower_to_bottom(&output->background_image->node);
            } else {
                wlr_scene_node_lower_to_bottom(&output->background_image->node);
            }

            wlr_log(WLR_INFO, "Background: wallpaper output name=%s x=%d y=%d w=%d h=%d mode=%s path=%s",
                output->wlr_output->name != NULL ? output->wlr_output->name : "(unnamed)",
                box.x, box.y, box.width, box.height,
                fbwl_wallpaper_mode_str(server->wallpaper_mode),
                server->wallpaper_path != NULL ? server->wallpaper_path : "");
            return;
        }
    }

    if (output->background_image != NULL) {
        wlr_scene_node_destroy(&output->background_image->node);
        output->background_image = NULL;
    }

    if (output->background_rect == NULL) {
        output->background_rect =
            wlr_scene_rect_create(server->layer_background, box.width, box.height, server->background_color);
        if (output->background_rect == NULL) {
            wlr_log(WLR_ERROR, "Background: failed to create rect");
            return;
        }
    } else {
        wlr_scene_rect_set_size(output->background_rect, box.width, box.height);
        wlr_scene_rect_set_color(output->background_rect, server->background_color);
    }

    wlr_scene_node_set_position(&output->background_rect->node, box.x, box.y);
    wlr_scene_node_lower_to_bottom(&output->background_rect->node);

    const uint32_t rgb = rgb24_from_rgba(server->background_color);
    wlr_log(WLR_INFO, "Background: output name=%s x=%d y=%d w=%d h=%d rgb=#%06x",
        output->wlr_output->name != NULL ? output->wlr_output->name : "(unnamed)",
        box.x, box.y, box.width, box.height, rgb);
}

static void server_background_update_all(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    struct fbwl_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        server_background_update_output(server, out);
    }
}

void server_pseudo_transparency_refresh(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL) {
            continue;
        }
        fbwl_view_pseudo_bg_update(view, why != NULL ? why : "server-refresh");
    }
    server_toolbar_ui_update_position(server);
    server_slit_ui_update_position(server);
}

bool server_wallpaper_set(struct fbwl_server *server, const char *path, enum fbwl_wallpaper_mode mode) {
    if (server == NULL) {
        return false;
    }

    const char *p = path != NULL ? path : "";
    if (p[0] == '\0' || strcasecmp(p, "none") == 0 || strcasecmp(p, "clear") == 0) {
        server->wallpaper_mode = mode;
        free(server->wallpaper_path);
        server->wallpaper_path = NULL;
        if (server->wallpaper_buf != NULL) {
            wlr_buffer_drop(server->wallpaper_buf);
            server->wallpaper_buf = NULL;
        }
        server_background_update_all(server);
        server_pseudo_transparency_refresh(server, "wallpaper-clear");
        wlr_log(WLR_INFO, "Background: wallpaper cleared");
        return true;
    }

    struct wlr_buffer *buf = wallpaper_buffer_from_png_path(p);
    if (buf == NULL) {
        wlr_log(WLR_ERROR, "Background: failed to load wallpaper path=%s", p);
        return false;
    }

    char *dup = strdup(p);
    if (dup == NULL) {
        wlr_buffer_drop(buf);
        return false;
    }

    free(server->wallpaper_path);
    server->wallpaper_path = dup;
    server->wallpaper_mode = mode;

    if (server->wallpaper_buf != NULL) {
        wlr_buffer_drop(server->wallpaper_buf);
    }
    server->wallpaper_buf = buf;

    server_background_update_all(server);
    server_pseudo_transparency_refresh(server, "wallpaper-set");
    wlr_log(WLR_INFO, "Background: wallpaper set path=%s mode=%s",
        server->wallpaper_path, fbwl_wallpaper_mode_str(server->wallpaper_mode));
    return true;
}

static void server_output_management_arrange_layers_on_output(void *userdata, struct wlr_output *wlr_output) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_scene_layers_arrange_layer_surfaces_on_output(server->output_layout, &server->outputs, &server->layer_surfaces,
        wlr_output);
}

static void server_update_head_count(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    if (server->output_layout == NULL) {
        fbwm_core_set_head_count(&server->wm, 1);
        return;
    }
    size_t heads = fbwl_screen_map_count(server->output_layout, &server->outputs);
    if (heads < 1) {
        heads = 1;
    }
    fbwm_core_set_head_count(&server->wm, heads);
    wlr_log(WLR_INFO, "Workspace: heads=%zu reason=%s", heads, why != NULL ? why : "(null)");
}

static void server_output_manager_test(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_manager_test);
    struct wlr_output_configuration_v1 *config = data;

    wlr_log(WLR_INFO, "OutputMgmt: test serial=%u", config->serial);
    const bool ok = fbwl_output_management_apply_config(server->backend, server->output_layout, &server->outputs,
        config, true, server_output_management_arrange_layers_on_output, server);
    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

static void server_output_manager_apply(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_manager_apply);
    struct wlr_output_configuration_v1 *config = data;

    wlr_log(WLR_INFO, "OutputMgmt: apply serial=%u", config->serial);
    const bool ok = fbwl_output_management_apply_config(server->backend, server->output_layout, &server->outputs,
        config, false, server_output_management_arrange_layers_on_output, server);
    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
        fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
        server_background_update_all(server);
        fbwl_screen_map_log(server->output_layout, &server->outputs, "output-management-apply");
        server_update_head_count(server, "output-management-apply");
        server_toolbar_ui_update_position(server);
        server_slit_ui_update_position(server);
        server_cmd_dialog_ui_update_position(server);
        server_osd_ui_update_position(server);
        server_pseudo_transparency_refresh(server, "output-management-apply");
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

static void server_output_power_set_mode(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, output_power_set_mode);
    if (server == NULL) {
        return;
    }
    fbwl_output_power_handle_set_mode(data, server->output_manager, &server->outputs, server->output_layout);
}

static void server_output_destroyed(void *userdata, struct wlr_output *wlr_output) {
    struct fbwl_server *server = userdata;
    if (server == NULL || wlr_output == NULL) {
        return;
    }
    fbwl_session_lock_on_output_destroyed(&server->session_lock);

    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view != NULL && view->foreign_output == wlr_output) {
            view->foreign_output = NULL;
        }
    }

    fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
    fbwl_screen_map_log(server->output_layout, &server->outputs, "output-destroy");
    server_update_head_count(server, "output-destroy");
    server_toolbar_ui_update_position(server);
    server_slit_ui_update_position(server);
    server_cmd_dialog_ui_update_position(server);
    server_osd_ui_update_position(server);
    server_pseudo_transparency_refresh(server, "output-destroy");
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    struct fbwl_output *output = fbwl_output_create(&server->outputs, wlr_output,
        server->allocator, server->renderer,
        server->output_layout, server->scene, server->scene_layout,
        server_output_destroyed, server);
    if (output == NULL) {
        wlr_log(WLR_ERROR, "Output: failed to create output");
        return;
    }

    server_background_update_output(server, output);
    fbwl_scene_layers_arrange_layer_surfaces_on_output(server->output_layout, &server->outputs, &server->layer_surfaces,
        wlr_output);
    fbwl_output_manager_update(server->output_manager, &server->outputs, server->output_layout);
    fbwl_screen_map_log(server->output_layout, &server->outputs, "new-output");
    server_update_head_count(server, "new-output");
    server_toolbar_ui_update_position(server);
    server_slit_ui_update_position(server);
    server_cmd_dialog_ui_update_position(server);
    server_osd_ui_update_position(server);
    server_pseudo_transparency_refresh(server, "new-output");
}

bool fbwl_server_outputs_init(struct fbwl_server *server) {
    if (server == NULL) {
        return false;
    }

    server->output_layout = wlr_output_layout_create(server->wl_display);
    if (server->output_layout == NULL) {
        wlr_log(WLR_ERROR, "failed to create output layout");
        return false;
    }

    server->output_manager = wlr_output_manager_v1_create(server->wl_display);
    if (server->output_manager == NULL) {
        wlr_log(WLR_ERROR, "failed to create output manager");
        return false;
    }
    server->output_manager_apply.notify = server_output_manager_apply;
    wl_signal_add(&server->output_manager->events.apply, &server->output_manager_apply);
    server->output_manager_test.notify = server_output_manager_test;
    wl_signal_add(&server->output_manager->events.test, &server->output_manager_test);

    server->output_power_mgr = wlr_output_power_manager_v1_create(server->wl_display);
    if (server->output_power_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create output power manager");
        return false;
    }
    server->output_power_set_mode.notify = server_output_power_set_mode;
    wl_signal_add(&server->output_power_mgr->events.set_mode, &server->output_power_set_mode);

    if (!fbwl_xdg_output_init(&server->xdg_output, server->wl_display, server->output_layout)) {
        return false;
    }
    wl_list_init(&server->outputs);
    wl_list_init(&server->layer_surfaces);
    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    server->scene = wlr_scene_create();
    server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);
    server->layer_background = wlr_scene_tree_create(&server->scene->tree);
    server->layer_bottom = wlr_scene_tree_create(&server->scene->tree);
    server->layer_normal = wlr_scene_tree_create(&server->scene->tree);
    server->layer_fullscreen = wlr_scene_tree_create(&server->scene->tree);
    server->layer_top = wlr_scene_tree_create(&server->scene->tree);
    server->layer_overlay = wlr_scene_tree_create(&server->scene->tree);
    return true;
}
