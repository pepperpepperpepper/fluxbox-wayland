#include "wayland/fbwl_ui_toolbar_build.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_ui_decor_theme.h"
#include "wayland/fbwl_ui_toolbar.h"

#ifdef HAVE_SYSTEMD
#include "wayland/fbwl_sni_pin.h"
#include "wayland/fbwl_sni_tray.h"
#endif

void fbwl_ui_toolbar_build_tray(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
        bool vertical, float alpha) {
    if (ui == NULL || env == NULL || env->decor_theme == NULL || ui->tree == NULL) {
        return;
    }
    if (ui->tray_count == 0 || ui->tray_w < 1 || ui->tray_icon_w < 1) {
        return;
    }

#ifndef HAVE_SYSTEMD
    (void)vertical;
    (void)alpha;
    ui->tray_count = 0;
    return;
#else
    ui->tray_ids = calloc(ui->tray_count, sizeof(*ui->tray_ids));
    ui->tray_services = calloc(ui->tray_count, sizeof(*ui->tray_services));
    ui->tray_paths = calloc(ui->tray_count, sizeof(*ui->tray_paths));
    ui->tray_item_lx = calloc(ui->tray_count, sizeof(*ui->tray_item_lx));
    ui->tray_item_w = calloc(ui->tray_count, sizeof(*ui->tray_item_w));
    ui->tray_rects = calloc(ui->tray_count, sizeof(*ui->tray_rects));
    ui->tray_icons = calloc(ui->tray_count, sizeof(*ui->tray_icons));
    if (ui->tray_ids == NULL || ui->tray_services == NULL || ui->tray_paths == NULL ||
            ui->tray_item_lx == NULL || ui->tray_item_w == NULL || ui->tray_rects == NULL ||
            ui->tray_icons == NULL) {
        if (ui->tray_ids != NULL) {
            for (size_t i = 0; i < ui->tray_count; i++) {
                free(ui->tray_ids[i]);
            }
        }
        free(ui->tray_ids);
        ui->tray_ids = NULL;
        if (ui->tray_services != NULL) {
            for (size_t i = 0; i < ui->tray_count; i++) {
                free(ui->tray_services[i]);
            }
        }
        free(ui->tray_services);
        ui->tray_services = NULL;
        if (ui->tray_paths != NULL) {
            for (size_t i = 0; i < ui->tray_count; i++) {
                free(ui->tray_paths[i]);
            }
        }
        free(ui->tray_paths);
        ui->tray_paths = NULL;
        free(ui->tray_item_lx);
        ui->tray_item_lx = NULL;
        free(ui->tray_item_w);
        ui->tray_item_w = NULL;
        free(ui->tray_rects);
        ui->tray_rects = NULL;
        free(ui->tray_icons);
        ui->tray_icons = NULL;
        ui->tray_count = 0;
        return;
    }

    const int cross = ui->border_w + ui->bevel_w;
    const int bg_w = vertical ? ui->thickness : ui->tray_w;
    const int bg_h = vertical ? ui->tray_w : ui->thickness;
    ui->tray_bg = wlr_scene_buffer_create(ui->tree, NULL);
    if (ui->tray_bg != NULL) {
        wlr_scene_node_set_position(&ui->tray_bg->node, vertical ? cross : ui->tray_x, vertical ? ui->tray_x : cross);
        const struct fbwl_texture *tex = &env->decor_theme->toolbar_systray_tex;
        const bool parentrel = fbwl_texture_is_parentrelative(tex);
        if (!parentrel) {
            struct wlr_buffer *buf = fbwl_texture_render_buffer(tex, bg_w > 0 ? bg_w : 1, bg_h > 0 ? bg_h : 1);
            wlr_scene_buffer_set_buffer(ui->tray_bg, buf);
            if (buf != NULL) {
                wlr_buffer_drop(buf);
            }
            wlr_scene_buffer_set_dest_size(ui->tray_bg, bg_w > 0 ? bg_w : 1, bg_h > 0 ? bg_h : 1);
            wlr_scene_buffer_set_opacity(ui->tray_bg, alpha);
        } else {
            wlr_scene_node_set_enabled(&ui->tray_bg->node, false);
        }
    }

    int xoff = ui->tray_x;
    const int pad = ui->thickness >= 8 ? 2 : 0;
    int size = ui->thickness - 2 * pad;
    if (size < 1) {
        size = 1;
    }

    size_t idx = 0;
    if (env->sni != NULL && env->sni->items.prev != NULL && env->sni->items.next != NULL) {
        struct fbwl_sni_item **items = calloc(ui->tray_count, sizeof(*items));
        const size_t ordered = items != NULL ?
            fbwl_sni_pin_order_items(env->sni, items, ui->tray_count, ui->systray_pin_left,
                ui->systray_pin_left_len, ui->systray_pin_right, ui->systray_pin_right_len) :
            0;
        for (size_t i = 0; i < ordered; i++) {
            struct fbwl_sni_item *sni = items[i];
            if (sni->status == FBWL_SNI_STATUS_PASSIVE) {
                continue;
            }
            if (idx >= ui->tray_count) {
                break;
            }
            ui->tray_item_lx[idx] = xoff;
            ui->tray_item_w[idx] = ui->tray_icon_w;
            ui->tray_ids[idx] = strdup(sni->id != NULL ? sni->id : "");
            ui->tray_services[idx] = strdup(sni->service != NULL ? sni->service : "");
            ui->tray_paths[idx] = strdup(sni->path != NULL ? sni->path : "");
            ui->tray_icons[idx] = wlr_scene_buffer_create(ui->tree, sni->icon_buf);
            if (ui->tray_icons[idx] != NULL) {
                wlr_scene_node_set_position(&ui->tray_icons[idx]->node,
                    vertical ? cross + pad : xoff + pad,
                    vertical ? xoff + pad : cross + pad);
                wlr_scene_buffer_set_dest_size(ui->tray_icons[idx], size, size);
            }
            wlr_log(WLR_INFO, "Toolbar: tray item idx=%zu lx=%d w=%d id=%s item_id=%s",
                idx, xoff, ui->tray_icon_w, sni->id != NULL ? sni->id : "",
                sni->item_id != NULL ? sni->item_id : "");
            xoff += ui->tray_icon_w;
            idx++;
        }
        free(items);
    }
    ui->tray_count = idx;
#endif
}
