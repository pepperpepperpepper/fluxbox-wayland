#pragma once

#include <stdbool.h>
#include <stdint.h>

struct fbwl_menu_ui;
struct wlr_buffer;

bool fbwl_ui_menu_contains_point(const struct fbwl_menu_ui *ui, int lx, int ly);

uint32_t fbwl_ui_menu_round_mask(const struct fbwl_menu_ui *ui);
void fbwl_ui_menu_outer_size(const struct fbwl_menu_ui *ui, int *out_w, int *out_h);

struct wlr_buffer *fbwl_ui_menu_solid_color_buffer_masked(int width, int height, const float rgba[static 4],
    int offset_x, int offset_y, int frame_w, int frame_h, uint32_t round_mask);

void fbwl_ui_menu_update_highlight(struct fbwl_menu_ui *ui);
