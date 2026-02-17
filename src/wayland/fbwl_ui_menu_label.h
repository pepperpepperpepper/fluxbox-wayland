#pragma once

#include <stddef.h>

struct fbwl_menu_ui;
struct wlr_scene_buffer;

void fbwl_ui_menu_update_item_label(struct fbwl_menu_ui *ui, size_t idx);
void fbwl_ui_menu_update_all_item_labels(struct fbwl_menu_ui *ui);
void fbwl_ui_menu_render_item_label(struct fbwl_menu_ui *ui, struct wlr_scene_buffer *sb, size_t idx);
