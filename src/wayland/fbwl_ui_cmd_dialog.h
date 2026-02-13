#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

struct wlr_output_layout;
struct wlr_scene;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;

struct fbwl_decor_theme;
struct fbwl_text_effect;

typedef bool (*fbwl_cmd_dialog_submit_fn)(void *userdata, const char *text);

struct fbwl_cmd_dialog_ui {
    bool open;

    int x;
    int y;
    int width;
    int height;

    char *text;
    char font[128];
    const struct fbwl_text_effect *effect;
    char prefix[64];
    fbwl_cmd_dialog_submit_fn submit;
    void *submit_userdata;

    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *bg;
    struct wlr_scene_buffer *label;
};

void fbwl_ui_cmd_dialog_close(struct fbwl_cmd_dialog_ui *ui, const char *why);
void fbwl_ui_cmd_dialog_open(struct fbwl_cmd_dialog_ui *ui,
    struct wlr_scene *scene, struct wlr_scene_tree *layer_overlay,
    const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout);
void fbwl_ui_cmd_dialog_open_prompt(struct fbwl_cmd_dialog_ui *ui,
    struct wlr_scene *scene, struct wlr_scene_tree *layer_overlay,
    const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout,
    const char *prefix, const char *initial_text, fbwl_cmd_dialog_submit_fn submit, void *submit_userdata);
void fbwl_ui_cmd_dialog_update_position(struct fbwl_cmd_dialog_ui *ui, struct wlr_output_layout *output_layout);
bool fbwl_ui_cmd_dialog_handle_key(struct fbwl_cmd_dialog_ui *ui, xkb_keysym_t sym, uint32_t modifiers);
