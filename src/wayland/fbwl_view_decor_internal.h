#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wayland/fbwl_view.h"

struct fbwl_decor_theme;
struct fbwl_server;
struct fbwl_texture;

struct wlr_buffer;
struct wlr_scene_buffer;
struct wlr_scene_rect;

bool fbwl_view_decor_round_frame_geom(const struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        uint32_t *out_round_mask, int *out_frame_x, int *out_frame_y, int *out_frame_w, int *out_frame_h);

bool fbwl_view_decor_buffer_accepts_input_round_corners(struct wlr_scene_buffer *buffer, double *sx, double *sy);

struct wlr_buffer *fbwl_view_decor_solid_color_buffer_masked(int width, int height, const float rgba[static 4],
        uint32_t round_mask, int offset_x, int offset_y, int frame_w, int frame_h);

struct wlr_buffer *fbwl_view_decor_wallpaper_region_buffer_masked(struct fbwl_server *server,
        int global_x, int global_y, int width, int height,
        uint32_t round_mask, int offset_x, int offset_y, int frame_w, int frame_h);

int fbwl_view_decor_button_size(const struct fbwl_decor_theme *theme);
int fbwl_view_decor_window_bevel_px(const struct fbwl_decor_theme *theme, int title_h);
bool fbwl_view_decor_texture_can_use_flat_rect(const struct fbwl_texture *tex);
bool fbwl_view_decor_button_allowed(const struct fbwl_view *view, enum fbwl_decor_hit_kind kind);

size_t fbwl_view_decor_visible_buttons(const struct fbwl_view *view,
        const enum fbwl_decor_hit_kind *buttons, size_t len);

struct wlr_scene_rect *fbwl_view_decor_button_rect(struct fbwl_view *view, enum fbwl_decor_hit_kind kind);
struct wlr_scene_buffer *fbwl_view_decor_button_tex(struct fbwl_view *view, enum fbwl_decor_hit_kind kind);
struct wlr_scene_buffer *fbwl_view_decor_button_icon(struct fbwl_view *view, enum fbwl_decor_hit_kind kind);

void fbwl_view_decor_button_apply(struct fbwl_view *view, const struct fbwl_decor_theme *theme,
        enum fbwl_decor_hit_kind kind,
        int btn_x, int btn_y, int btn_size,
        uint32_t round_mask, int frame_x, int frame_y, int frame_w, int frame_h);
