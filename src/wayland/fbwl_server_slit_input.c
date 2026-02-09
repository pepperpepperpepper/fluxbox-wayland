#include <linux/input-event-codes.h>
#include <stdbool.h>

#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"

static bool slit_is_topmost_at(const struct fbwl_slit_ui *ui, struct wlr_scene *scene, int lx, int ly) {
    if (ui == NULL || scene == NULL || ui->tree == NULL) {
        return false;
    }

    double sx = 0, sy = 0;
    struct wlr_scene_node *node =
        wlr_scene_node_at(&scene->tree.node, (double)lx, (double)ly, &sx, &sy);
    for (struct wlr_scene_node *walk = node; walk != NULL; walk = walk->parent != NULL ? &walk->parent->node : NULL) {
        if (walk == &ui->tree->node) {
            return true;
        }
    }
    return false;
}

bool server_slit_ui_handle_button(struct fbwl_server *server, const struct wlr_pointer_button_event *event) {
    if (server == NULL || event == NULL || server->cursor == NULL || server->seat == NULL) {
        return false;
    }
    if (!server->slit_ui.enabled || server->slit_ui.tree == NULL) {
        return false;
    }
    if (server->slit_ui.width < 1 || server->slit_ui.height < 1) {
        return false;
    }

    const int lx = (int)server->cursor->x;
    const int ly = (int)server->cursor->y;

    if (lx < server->slit_ui.x || lx >= server->slit_ui.x + server->slit_ui.width ||
            ly < server->slit_ui.y || ly >= server->slit_ui.y + server->slit_ui.height) {
        return false;
    }

    if (!slit_is_topmost_at(&server->slit_ui, server->scene, lx, ly)) {
        return false;
    }

    if (event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return false;
    }

    if (event->button == BTN_RIGHT) {
        wlr_seat_pointer_clear_focus(server->seat);
        server_menu_ui_open_slit(server, lx, ly);
        return true;
    }

    if (event->button == BTN_LEFT) {
        wlr_scene_node_raise_to_top(&server->slit_ui.tree->node);
        wlr_log(WLR_INFO, "Slit: raise reason=click");
    }

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
    return true;
}
