#include "wayland/fbwl_ui_cmd_dialog.h"

#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_text.h"
#include "wayland/fbwl_util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

static void fbwl_ui_cmd_dialog_destroy_scene(struct fbwl_cmd_dialog_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (ui->tree != NULL) {
        wlr_scene_node_destroy(&ui->tree->node);
        ui->tree = NULL;
    }
    ui->bg = NULL;
    ui->label = NULL;
}

static void fbwl_ui_cmd_dialog_render(struct fbwl_cmd_dialog_ui *ui) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open || ui->tree == NULL) {
        return;
    }

    if (ui->label == NULL) {
        ui->label = wlr_scene_buffer_create(ui->tree, NULL);
        if (ui->label == NULL) {
            return;
        }
        wlr_scene_node_set_position(&ui->label->node, 0, 0);
    }

    const char *txt = ui->text != NULL ? ui->text : "";
    const char *prefix = ui->prefix[0] != '\0' ? ui->prefix : "Run: ";
    size_t need = strlen(prefix) + strlen(txt) + 1;
    char *render = malloc(need);
    if (render == NULL) {
        return;
    }
    (void)snprintf(render, need, "%s%s", prefix, txt);

    const float fg[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct wlr_buffer *buf = fbwl_text_buffer_create(render, ui->width, ui->height, 8, fg, ui->font);
    free(render);
    if (buf == NULL) {
        wlr_scene_buffer_set_buffer(ui->label, NULL);
        return;
    }

    wlr_scene_buffer_set_buffer(ui->label, buf);
    wlr_buffer_drop(buf);
}

void fbwl_ui_cmd_dialog_update_position(struct fbwl_cmd_dialog_ui *ui, struct wlr_output_layout *output_layout) {
    if (ui == NULL || output_layout == NULL) {
        return;
    }
    if (!ui->open || ui->tree == NULL) {
        return;
    }

    struct wlr_output *out = wlr_output_layout_get_center_output(output_layout);
    if (out == NULL) {
        return;
    }

    struct wlr_box box = {0};
    wlr_output_layout_get_box(output_layout, out, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    int x = box.x + (box.width - ui->width) / 2;
    int y = box.y + box.height / 4;
    if (x < box.x) {
        x = box.x;
    }
    if (y < box.y) {
        y = box.y;
    }

    if (x == ui->x && y == ui->y) {
        return;
    }

    ui->x = x;
    ui->y = y;
    wlr_scene_node_set_position(&ui->tree->node, ui->x, ui->y);
    wlr_log(WLR_INFO, "CmdDialog: position x=%d y=%d w=%d h=%d", ui->x, ui->y, ui->width, ui->height);
}

void fbwl_ui_cmd_dialog_close(struct fbwl_cmd_dialog_ui *ui, const char *why) {
    if (ui == NULL) {
        return;
    }
    if (!ui->open) {
        return;
    }

    fbwl_ui_cmd_dialog_destroy_scene(ui);
    ui->open = false;
    free(ui->text);
    ui->text = NULL;
    ui->font[0] = '\0';
    ui->prefix[0] = '\0';
    ui->submit = NULL;
    ui->submit_userdata = NULL;

    wlr_log(WLR_INFO, "CmdDialog: close reason=%s", why != NULL ? why : "(null)");
}

void fbwl_ui_cmd_dialog_open_prompt(struct fbwl_cmd_dialog_ui *ui,
        struct wlr_scene *scene, struct wlr_scene_tree *layer_overlay,
        const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout,
        const char *prefix, const char *initial_text,
        fbwl_cmd_dialog_submit_fn submit, void *submit_userdata) {
    if (ui == NULL || scene == NULL || decor_theme == NULL) {
        return;
    }

    fbwl_ui_cmd_dialog_close(ui, "reopen");

    ui->open = true;

    ui->height = decor_theme->title_height > 0 ? decor_theme->title_height : 24;
    ui->width = 600;
    if (ui->width < 200) {
        ui->width = 200;
    }
    (void)snprintf(ui->font, sizeof(ui->font), "%s", decor_theme->window_font);
    if (prefix != NULL && *prefix != '\0') {
        (void)snprintf(ui->prefix, sizeof(ui->prefix), "%s", prefix);
    } else {
        (void)snprintf(ui->prefix, sizeof(ui->prefix), "%s", "Run: ");
    }
    ui->submit = submit;
    ui->submit_userdata = submit_userdata;

    free(ui->text);
    ui->text = strdup(initial_text != NULL ? initial_text : "");
    if (ui->text == NULL) {
        ui->open = false;
        return;
    }

    struct wlr_scene_tree *parent = layer_overlay != NULL ? layer_overlay : &scene->tree;
    ui->tree = wlr_scene_tree_create(parent);
    if (ui->tree == NULL) {
        ui->open = false;
        free(ui->text);
        ui->text = NULL;
        return;
    }

    float bg[4] = {decor_theme->titlebar_inactive[0], decor_theme->titlebar_inactive[1],
        decor_theme->titlebar_inactive[2], 0.95f};
    ui->bg = wlr_scene_rect_create(ui->tree, ui->width, ui->height, bg);
    if (ui->bg != NULL) {
        wlr_scene_node_set_position(&ui->bg->node, 0, 0);
    }

    fbwl_ui_cmd_dialog_render(ui);
    wlr_scene_node_raise_to_top(&ui->tree->node);
    fbwl_ui_cmd_dialog_update_position(ui, output_layout);

    wlr_log(WLR_INFO, "CmdDialog: open");
}

void fbwl_ui_cmd_dialog_open(struct fbwl_cmd_dialog_ui *ui,
        struct wlr_scene *scene, struct wlr_scene_tree *layer_overlay,
        const struct fbwl_decor_theme *decor_theme, struct wlr_output_layout *output_layout) {
    fbwl_ui_cmd_dialog_open_prompt(ui, scene, layer_overlay, decor_theme, output_layout,
        "Run: ", "", NULL, NULL);
}

static bool fbwl_ui_cmd_dialog_text_backspace(struct fbwl_cmd_dialog_ui *ui) {
    if (ui == NULL || ui->text == NULL) {
        return false;
    }
    size_t len = strlen(ui->text);
    if (len == 0) {
        return false;
    }

    size_t i = len - 1;
    while (i > 0 && ((unsigned char)ui->text[i] & 0xC0) == 0x80) {
        i--;
    }
    ui->text[i] = '\0';
    return true;
}

bool fbwl_ui_cmd_dialog_handle_key(struct fbwl_cmd_dialog_ui *ui, xkb_keysym_t sym, uint32_t modifiers) {
    if (ui == NULL) {
        return false;
    }
    if (!ui->open) {
        return false;
    }

    if (sym == XKB_KEY_Escape) {
        fbwl_ui_cmd_dialog_close(ui, "escape");
        return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        const char *cmd = ui->text != NULL ? ui->text : "";
        if (ui->submit != NULL) {
            bool ok = ui->submit(ui->submit_userdata, cmd);
            if (ok) {
                fbwl_ui_cmd_dialog_close(ui, "submit");
            }
        } else if (*cmd != '\0') {
            wlr_log(WLR_INFO, "CmdDialog: execute cmd=%s", cmd);
            fbwl_spawn(cmd);
            fbwl_ui_cmd_dialog_close(ui, "execute");
        } else {
            fbwl_ui_cmd_dialog_close(ui, "empty-enter");
        }
        return true;
    }
    if (sym == XKB_KEY_BackSpace) {
        if (fbwl_ui_cmd_dialog_text_backspace(ui)) {
            fbwl_ui_cmd_dialog_render(ui);
        }
        return true;
    }

    if ((modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO)) != 0) {
        return true;
    }

    char utf8[16];
    int n = xkb_keysym_to_utf8(sym, utf8, sizeof(utf8));
    if (n <= 0 || (size_t)n >= sizeof(utf8)) {
        return true;
    }
    utf8[n] = '\0';

    size_t cur = ui->text != NULL ? strlen(ui->text) : 0;
    if (cur > 4096) {
        return true;
    }

    char *next = realloc(ui->text, cur + (size_t)n + 1);
    if (next == NULL) {
        return true;
    }
    ui->text = next;
    memcpy(ui->text + cur, utf8, (size_t)n + 1);
    fbwl_ui_cmd_dialog_render(ui);
    return true;
}
