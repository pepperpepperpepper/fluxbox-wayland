#include "wayland/fbwl_ui_toolbar_layout.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>

#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_ui_toolbar.h"
#include "wmcore/fbwm_core.h"

#ifdef HAVE_SYSTEMD
#include "wayland/fbwl_sni_tray.h"
#endif

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

static int toolbar_tool_button_w(const struct fbwl_toolbar_ui *ui, const char *tok) {
    if (ui == NULL || tok == NULL || *tok == '\0' || ui->thickness < 1 || ui->cell_w < 1) {
        return 0;
    }
    if (strcmp(tok, "prevworkspace") == 0 ||
            strcmp(tok, "nextworkspace") == 0 ||
            strcmp(tok, "prevwindow") == 0 ||
            strcmp(tok, "nextwindow") == 0) {
        return ui->thickness;
    }
    if (strcmp(tok, "workspacename") == 0) {
        int w = ui->cell_w * 2;
        if (w > 256) {
            w = 256;
        }
        return w;
    }
    if (strncmp(tok, "button.", 7) == 0) {
        return ui->cell_w;
    }
    return 0;
}

void fbwl_ui_toolbar_layout_apply(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env, bool vertical) {
    if (ui == NULL || env == NULL || env->wm == NULL) {
        return;
    }

    int border_w = ui->border_w;
    if (border_w < 0) {
        border_w = 0;
    }
    if (border_w > 20) {
        border_w = 20;
    }

    int bevel_w = ui->bevel_w;
    if (bevel_w < 0) {
        bevel_w = 0;
    }
    if (bevel_w > 20) {
        bevel_w = 20;
    }

    const int offset = border_w + bevel_w;

    int workspaces = fbwm_core_workspace_count(env->wm);
    if (workspaces < 1) {
        workspaces = 1;
    }
    ui->cell_count = (size_t)workspaces;
    ui->ws_width = 0;

    int output_main = 0;
    if (env->output_layout != NULL) {
        const size_t on_head = ui->on_head >= 0 ? (size_t)ui->on_head : 0;
        struct wlr_output *out = fbwl_screen_map_output_for_screen(env->output_layout, env->outputs, on_head);
        if (out != NULL) {
            struct wlr_box box = {0};
            wlr_output_layout_get_box(env->output_layout, out, &box);
            output_main = vertical ? box.height : box.width;
        }
    }

    int percent = ui->width_percent;
    if (percent < 1 || percent > 100) {
        percent = 100;
    }

    int avail = output_main > 0 ? output_main - 2 * border_w : 0;
    if (avail < 1) {
        avail = 1;
    }

    int inner_main = (avail * percent) / 100;
    if (inner_main < 1) {
        inner_main = 1;
    }

    const int outer_main = inner_main + 2 * border_w;

    int payload_main = inner_main - 2 * bevel_w;
    if (payload_main < 1) {
        payload_main = 1;
    }

    int outer_cross = ui->thickness + 2 * bevel_w + 2 * border_w;
    if (outer_cross < 1) {
        outer_cross = 1;
    }

    if (vertical) {
        ui->width = outer_cross;
        ui->height = outer_main;
    } else {
        ui->height = outer_cross;
        ui->width = outer_main;
    }

    const int main_total = payload_main;

    bool want_iconbar = false;
    bool want_tray = false;
    bool want_clock = false;
    if (ui->tools_order != NULL) {
        for (size_t i = 0; i < ui->tools_order_len; i++) {
            const char *tok = ui->tools_order[i];
            if (tok == NULL) {
                continue;
            }
            if (!want_iconbar && strcmp(tok, "iconbar") == 0) {
                want_iconbar = true;
            } else if (!want_tray && strcmp(tok, "systemtray") == 0) {
                want_tray = true;
            } else if (!want_clock && strcmp(tok, "clock") == 0) {
                want_clock = true;
            }
        }
    }

    const int clock_w_desired = want_clock ? 120 : 0;

    int tray_icon_w = ui->thickness;
    if (tray_icon_w < 1) {
        tray_icon_w = 1;
    }
    size_t tray_count_full = 0;
#ifdef HAVE_SYSTEMD
    if (want_tray && env->sni != NULL && env->sni->items.prev != NULL && env->sni->items.next != NULL) {
        struct fbwl_sni_item *item;
        wl_list_for_each(item, &env->sni->items, link) {
            if (item->status != FBWL_SNI_STATUS_PASSIVE) {
                tray_count_full++;
            }
        }
    }
#endif

    size_t toolbtn_cap = 0;
    int toolbtn_w_total = 0;
    if (ui->tools_order != NULL) {
        for (size_t i = 0; i < ui->tools_order_len; i++) {
            const char *tok = ui->tools_order[i];
            if (tok == NULL) {
                continue;
            }
            int bw = toolbar_tool_button_w(ui, tok);
            if (bw < 1) {
                continue;
            }
            if (strncmp(tok, "button.", 7) == 0) {
                const char *name = tok + 7;
                const struct fbwl_toolbar_button_cfg *cfg = toolbar_button_cfg_find(ui, name);
                if (cfg == NULL || cfg->label == NULL || *cfg->label == '\0') {
                    continue;
                }
            }
            toolbtn_cap++;
            toolbtn_w_total += bw;
        }
    }

    const int fixed_no_iconbar_no_tray = toolbtn_w_total + clock_w_desired;
    int tray_w_target = 0;
    size_t tray_count_target = 0;
    if (want_tray && tray_count_full > 0 && tray_icon_w > 0) {
        int avail_for_tray = main_total - fixed_no_iconbar_no_tray;
        if (avail_for_tray < 0) {
            avail_for_tray = 0;
        }
        const int full_w = (int)(tray_count_full * (size_t)tray_icon_w);
        tray_w_target = full_w < avail_for_tray ? full_w : avail_for_tray;
        if (tray_w_target < 0) {
            tray_w_target = 0;
        }
        tray_count_target = (size_t)(tray_w_target / tray_icon_w);
        if (tray_count_target > tray_count_full) {
            tray_count_target = tray_count_full;
        }
        tray_w_target = (int)tray_count_target * tray_icon_w;
    }

    int iconbar_w_target = 0;
    if (want_iconbar) {
        iconbar_w_target = main_total - fixed_no_iconbar_no_tray - tray_w_target;
        if (iconbar_w_target < 0) {
            iconbar_w_target = 0;
        }
    }

    ui->buttons_x = 0;
    ui->buttons_w = 0;
    ui->button_count = 0;
    if (toolbtn_cap > 0) {
        ui->button_item_lx = calloc(toolbtn_cap, sizeof(*ui->button_item_lx));
        ui->button_item_w = calloc(toolbtn_cap, sizeof(*ui->button_item_w));
        ui->button_item_tokens = calloc(toolbtn_cap, sizeof(*ui->button_item_tokens));
        if (ui->button_item_lx == NULL || ui->button_item_w == NULL || ui->button_item_tokens == NULL) {
            free(ui->button_item_lx);
            ui->button_item_lx = NULL;
            free(ui->button_item_w);
            ui->button_item_w = NULL;
            free(ui->button_item_tokens);
            ui->button_item_tokens = NULL;
            toolbtn_cap = 0;
        }
    }

    int cursor = 0;
    size_t toolbtn_idx = 0;
    bool iconbar_placed = false;
    bool tray_placed = false;
    bool clock_placed = false;

    ui->clock_x = 0;
    ui->clock_w = 0;
    ui->tray_x = 0;
    ui->tray_w = 0;
    ui->tray_icon_w = 0;
    ui->tray_count = 0;
    ui->iconbar_w = 0;
    ui->iconbar_x = 0;

    if (ui->tools_order != NULL) {
        for (size_t i = 0; i < ui->tools_order_len; i++) {
            if (cursor >= main_total) {
                break;
            }
            const char *tok = ui->tools_order[i];
            if (tok == NULL) {
                continue;
            }

            if (strcmp(tok, "iconbar") == 0) {
                if (iconbar_placed) {
                    continue;
                }
                int w = iconbar_w_target;
                if (w > main_total - cursor) {
                    w = main_total - cursor;
                }
                if (w < 0) {
                    w = 0;
                }
                ui->iconbar_x = offset + cursor;
                ui->iconbar_w = w;
                cursor += w;
                iconbar_placed = true;
                continue;
            }

            if (strcmp(tok, "systemtray") == 0) {
                if (tray_placed) {
                    continue;
                }
                int w = tray_w_target;
                if (w > main_total - cursor) {
                    w = main_total - cursor;
                }
                if (w < 0) {
                    w = 0;
                }
                int fit = tray_icon_w > 0 ? (w / tray_icon_w) : 0;
                if (fit < 0) {
                    fit = 0;
                }
                if ((size_t)fit > tray_count_full) {
                    fit = (int)tray_count_full;
                }
                ui->tray_x = offset + cursor;
                ui->tray_icon_w = tray_icon_w;
                ui->tray_count = (size_t)fit;
                ui->tray_w = fit * tray_icon_w;
                cursor += ui->tray_w;
                tray_placed = true;
                continue;
            }

            if (strcmp(tok, "clock") == 0) {
                if (clock_placed) {
                    continue;
                }
                int w = clock_w_desired;
                if (w > main_total - cursor) {
                    w = main_total - cursor;
                }
                if (w < 0) {
                    w = 0;
                }
                ui->clock_x = offset + cursor;
                ui->clock_w = w;
                cursor += w;
                clock_placed = true;
                continue;
            }

            int bw = toolbar_tool_button_w(ui, tok);
            if (bw < 1) {
                continue;
            }
            if (strncmp(tok, "button.", 7) == 0) {
                const char *name = tok + 7;
                const struct fbwl_toolbar_button_cfg *cfg = toolbar_button_cfg_find(ui, name);
                if (cfg == NULL || cfg->label == NULL || *cfg->label == '\0') {
                    continue;
                }
            }
            if (bw > main_total - cursor) {
                bw = main_total - cursor;
            }
            if (bw < 1) {
                break;
            }
            if (toolbtn_idx < toolbtn_cap && ui->button_item_lx != NULL && ui->button_item_w != NULL && ui->button_item_tokens != NULL) {
                ui->button_item_lx[toolbtn_idx] = offset + cursor;
                ui->button_item_w[toolbtn_idx] = bw;
                ui->button_item_tokens[toolbtn_idx] = tok;
                toolbtn_idx++;
            }
            cursor += bw;
        }
    }

    ui->button_count = toolbtn_idx;
    (void)tray_count_target;
}
