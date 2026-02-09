#include "wayland/fbwl_ui_toolbar_build.h"

#include <stdlib.h>
#include <string.h>

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

    int xoff = ui->tray_x;
    const int pad = ui->thickness >= 8 ? 2 : 0;
    int size = ui->thickness - 2 * pad;
    if (size < 1) {
        size = 1;
    }

    float item[4] = {
        env->decor_theme->toolbar_iconbar_focused[0],
        env->decor_theme->toolbar_iconbar_focused[1],
        env->decor_theme->toolbar_iconbar_focused[2],
        env->decor_theme->toolbar_iconbar_focused[3] * alpha * 0.65f,
    };

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
            ui->tray_rects[idx] = wlr_scene_rect_create(ui->tree, size, size, item);
            if (ui->tray_rects[idx] != NULL) {
                wlr_scene_node_set_position(&ui->tray_rects[idx]->node,
                    vertical ? pad : xoff + pad,
                    vertical ? xoff + pad : pad);
            }
            ui->tray_icons[idx] = wlr_scene_buffer_create(ui->tree, sni->icon_buf);
            if (ui->tray_icons[idx] != NULL) {
                wlr_scene_node_set_position(&ui->tray_icons[idx]->node,
                    vertical ? pad : xoff + pad,
                    vertical ? xoff + pad : pad);
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

