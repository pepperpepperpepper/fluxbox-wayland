#include "wayland/fbwl_ui_toolbar_build.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wayland/fbwl_ui_text.h"
#include "wmcore/fbwm_core.h"

static void toolbar_buttons_free(struct fbwl_toolbar_button_cfg *buttons, size_t len) {
    if (buttons == NULL) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        struct fbwl_toolbar_button_cfg *cfg = &buttons[i];
        free(cfg->name);
        cfg->name = NULL;
        free(cfg->label);
        cfg->label = NULL;
        for (size_t j = 0; j < FBWL_TOOLBAR_BUTTON_COMMANDS_MAX; j++) {
            free(cfg->commands[j]);
            cfg->commands[j] = NULL;
        }
    }
    free(buttons);
}

void fbwl_ui_toolbar_buttons_clear(struct fbwl_toolbar_ui *ui) {
    if (ui == NULL) {
        return;
    }
    toolbar_buttons_free(ui->buttons, ui->buttons_len);
    ui->buttons = NULL;
    ui->buttons_len = 0;
}

static const char *nonnull(const char *s) {
    return s != NULL ? s : "";
}

static const struct fbwl_toolbar_button_cfg *toolbar_button_cfg_find(const struct fbwl_toolbar_ui *ui, const char *name) {
    if (ui == NULL || name == NULL || *name == '\0' || ui->buttons == NULL || ui->buttons_len == 0) {
        return NULL;
    }
    for (size_t i = 0; i < ui->buttons_len; i++) {
        if (ui->buttons[i].name != NULL && strcmp(ui->buttons[i].name, name) == 0) {
            return &ui->buttons[i];
        }
    }
    return NULL;
}

static bool toolbar_buttons_eq(const struct fbwl_toolbar_button_cfg *a, size_t a_len,
        const struct fbwl_toolbar_button_cfg *b, size_t b_len) {
    if (a_len != b_len) {
        return false;
    }
    for (size_t i = 0; i < a_len; i++) {
        if (strcmp(nonnull(a[i].name), nonnull(b[i].name)) != 0) {
            return false;
        }
        if (strcmp(nonnull(a[i].label), nonnull(b[i].label)) != 0) {
            return false;
        }
        for (size_t j = 0; j < FBWL_TOOLBAR_BUTTON_COMMANDS_MAX; j++) {
            if (strcmp(nonnull(a[i].commands[j]), nonnull(b[i].commands[j])) != 0) {
                return false;
            }
        }
    }
    return true;
}

bool fbwl_ui_toolbar_buttons_replace(struct fbwl_toolbar_ui *ui, struct fbwl_toolbar_button_cfg *buttons, size_t len) {
    if (ui == NULL) {
        toolbar_buttons_free(buttons, len);
        return false;
    }

    if (toolbar_buttons_eq(ui->buttons, ui->buttons_len, buttons, len)) {
        toolbar_buttons_free(buttons, len);
        return false;
    }

    fbwl_ui_toolbar_buttons_clear(ui);
    ui->buttons = buttons;
    ui->buttons_len = len;
    return true;
}

void fbwl_ui_toolbar_build_buttons(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
        bool vertical, const float fg[4]) {
    if (ui == NULL || fg == NULL) {
        return;
    }
    if (env == NULL || env->wm == NULL) {
        return;
    }
    if (!ui->enabled || ui->tree == NULL || ui->button_count == 0 ||
            ui->button_item_lx == NULL || ui->button_item_w == NULL || ui->button_item_tokens == NULL ||
            ui->thickness < 1) {
        return;
    }
    ui->button_rects = calloc(ui->button_count, sizeof(*ui->button_rects));
    ui->button_labels = calloc(ui->button_count, sizeof(*ui->button_labels));

    if (ui->button_rects == NULL || ui->button_labels == NULL) {
        free(ui->button_rects);
        ui->button_rects = NULL;
        free(ui->button_labels);
        ui->button_labels = NULL;
        ui->button_count = 0;
        return;
    }

    float item[4] = {0.00f, 0.00f, 0.00f, 0.01f};
    const int pad = ui->thickness >= 24 ? 8 : 2;

    for (size_t i = 0; i < ui->button_count; i++) {
        const int off = ui->button_item_lx[i];
        const int tool_w = ui->button_item_w[i];

        const int w = vertical ? ui->thickness : tool_w;
        const int h = vertical ? tool_w : ui->thickness;

        ui->button_rects[i] = wlr_scene_rect_create(ui->tree, w, h, item);
        if (ui->button_rects[i] != NULL) {
            wlr_scene_node_set_position(&ui->button_rects[i]->node, vertical ? 0 : off, vertical ? off : 0);
        }

        const char *tok = ui->button_item_tokens[i];
        if (tok == NULL) {
            continue;
        }

        const char *label = "";
        char label_buf[128];
        label_buf[0] = '\0';
        if (strcmp(tok, "prevworkspace") == 0 || strcmp(tok, "prevwindow") == 0) {
            label = "<";
        } else if (strcmp(tok, "nextworkspace") == 0 || strcmp(tok, "nextwindow") == 0) {
            label = ">";
        } else if (strcmp(tok, "workspacename") == 0) {
            const size_t head = ui->on_head >= 0 ? (size_t)ui->on_head : 0;
            int cur = fbwm_core_workspace_current_for_head(env->wm, head);
            if (cur < 0) {
                cur = 0;
            }
            const char *ws_name = fbwm_core_workspace_name(env->wm, cur);
            if (ws_name != NULL && *ws_name != '\0') {
                label = ws_name;
            } else if (snprintf(label_buf, sizeof(label_buf), "%d", cur + 1) > 0) {
                label = label_buf;
            }
        } else if (strncmp(tok, "button.", 7) == 0) {
            const char *name = tok + 7;
            const struct fbwl_toolbar_button_cfg *cfg = toolbar_button_cfg_find(ui, name);
            if (cfg != NULL && cfg->label != NULL) {
                label = cfg->label;
            }
        }

        wlr_log(WLR_INFO, "Toolbar: tool tok=%s lx=%d w=%d", tok, off, tool_w);

        if (label == NULL || *label == '\0') {
            continue;
        }

        struct wlr_buffer *buf = fbwl_text_buffer_create(label, w, h, pad, fg, ui->font,
            env->decor_theme != NULL ? &env->decor_theme->toolbar_label_effect : NULL);
        if (buf == NULL) {
            continue;
        }

        ui->button_labels[i] = wlr_scene_buffer_create(ui->tree, buf);
        if (ui->button_labels[i] != NULL) {
            wlr_scene_node_set_position(&ui->button_labels[i]->node, vertical ? 0 : off, vertical ? off : 0);
        }
        wlr_buffer_drop(buf);
    }

    wlr_log(WLR_INFO, "Toolbar: toolbuttons count=%zu", ui->button_count);
}
