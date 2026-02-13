#pragma once

#include <stdint.h>

struct fbwl_view;

void server_keybindings_view_toggle_maximize_horizontal(void *userdata, struct fbwl_view *view);
void server_keybindings_view_toggle_maximize_vertical(void *userdata, struct fbwl_view *view);
void server_keybindings_view_raise_layer(void *userdata, struct fbwl_view *view);
void server_keybindings_view_lower_layer(void *userdata, struct fbwl_view *view);
void server_keybindings_view_set_layer(void *userdata, struct fbwl_view *view, int layer);
void server_keybindings_view_set_xprop(void *userdata, struct fbwl_view *view, const char *name, const char *value);

void server_keybindings_view_move_to_cmd(void *userdata, struct fbwl_view *view, const char *args);
void server_keybindings_view_move_rel_cmd(void *userdata, struct fbwl_view *view, int kind, const char *args);
void server_keybindings_view_resize_to_cmd(void *userdata, struct fbwl_view *view, const char *args);
void server_keybindings_view_resize_rel_cmd(void *userdata, struct fbwl_view *view, int kind, const char *args);

void server_keybindings_view_set_alpha_cmd(void *userdata, struct fbwl_view *view, const char *args);
void server_keybindings_view_toggle_decor(void *userdata, struct fbwl_view *view);
void server_keybindings_view_set_decor(void *userdata, struct fbwl_view *view, const char *value);
void server_keybindings_view_set_title(void *userdata, struct fbwl_view *view, const char *title);
void server_keybindings_view_set_title_dialog(void *userdata, struct fbwl_view *view);

void server_keybindings_reload_style(void *userdata);
void server_keybindings_set_style(void *userdata, const char *path);
void server_keybindings_save_rc(void *userdata);
void server_keybindings_set_resource_value(void *userdata, const char *args);
void server_keybindings_set_resource_value_dialog(void *userdata);
void server_keybindings_set_env(void *userdata, const char *args);
void server_keybindings_bind_key(void *userdata, const char *binding);

void server_keybindings_toggle_toolbar_hidden(void *userdata, int cursor_x, int cursor_y);
void server_keybindings_toggle_toolbar_above(void *userdata, int cursor_x, int cursor_y);
void server_keybindings_toggle_slit_hidden(void *userdata, int cursor_x, int cursor_y);
void server_keybindings_toggle_slit_above(void *userdata, int cursor_x, int cursor_y);

void server_keybindings_views_attach_pattern(void *userdata, const char *pattern, int cursor_x, int cursor_y);
void server_keybindings_show_desktop(void *userdata, int cursor_x, int cursor_y);
void server_keybindings_arrange_windows(void *userdata, int method, const char *pattern, int cursor_x, int cursor_y);
void server_keybindings_unclutter(void *userdata, const char *pattern, int cursor_x, int cursor_y);

void server_keybindings_deiconify(void *userdata, const char *args, int cursor_x, int cursor_y);
void server_keybindings_close_all_windows(void *userdata);

void server_keybindings_workspace_toggle_prev(void *userdata, int cursor_x, int cursor_y, const char *why);
void server_keybindings_add_workspace(void *userdata);
void server_keybindings_remove_last_workspace(void *userdata);
void server_keybindings_set_workspace_name(void *userdata, const char *args, int cursor_x, int cursor_y);
void server_keybindings_set_workspace_name_dialog(void *userdata, int cursor_x, int cursor_y);

void server_keybindings_view_set_head(void *userdata, struct fbwl_view *view, int head);
void server_keybindings_view_send_to_rel_head(void *userdata, struct fbwl_view *view, int delta);

void server_keybindings_mark_window(void *userdata, struct fbwl_view *view, uint32_t keycode);
void server_keybindings_goto_marked_window(void *userdata, uint32_t keycode);
