#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"

#define FBWL_MOUSEBIND_DRAG_THRESHOLD_PX 4

static struct fbwl_view *find_view_by_create_seq(struct fbwl_server *server, uint64_t create_seq) {
    if (server == NULL || create_seq == 0) {
        return NULL;
    }
    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view != NULL && view->create_seq == create_seq) {
            return view;
        }
    }
    return NULL;
}

static void mousebind_capture_clear(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    server->mousebind_capture_active = false;
    server->mousebind_capture_has_click = false;
    server->mousebind_capture_has_move = false;
    server->mousebind_capture_moved = false;
    server->mousebind_capture_button = 0;
    server->mousebind_capture_fb_button = 0;
    server->mousebind_capture_modifiers = 0;
    server->mousebind_capture_context = FBWL_MOUSEBIND_ANY;
    server->mousebind_capture_press_x = 0;
    server->mousebind_capture_press_y = 0;
    server->mousebind_capture_target_create_seq = 0;
}

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

int server_fluxbox_mouse_button_from_event(uint32_t button) {
    switch (button) {
    case BTN_LEFT:
        return 1;
    case BTN_MIDDLE:
        return 2;
    case BTN_RIGHT:
        return 3;
    default:
        return 0;
    }
}

int server_fluxbox_mouse_button_from_axis(const struct wlr_pointer_axis_event *event) {
    if (event == NULL) {
        return 0;
    }
    if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL) {
        return 0;
    }

    double delta = event->delta;
    if (event->delta_discrete != 0) {
        delta = (double)event->delta_discrete;
    }

    if (delta < 0) {
        return 4;
    }
    if (delta > 0) {
        return 5;
    }
    return 0;
}

enum fbwl_mousebinding_context server_mousebinding_context_at(struct fbwl_server *server,
        struct fbwl_view *view, struct wlr_surface *surface) {
    if (server != NULL && server->toolbar_ui.enabled && !server->toolbar_ui.hidden) {
        const int x = server->toolbar_ui.x;
        const int y = server->toolbar_ui.y;
        const int w = server->toolbar_ui.width;
        const int h = server->toolbar_ui.height;
        const int cx = (int)server->cursor->x;
        const int cy = (int)server->cursor->y;
        if (cx >= x && cy >= y && cx < x + w && cy < y + h) {
            return FBWL_MOUSEBIND_TOOLBAR;
        }
    }
    if (server != NULL && server->cursor != NULL && server->scene != NULL &&
            server->slit_ui.enabled && server->slit_ui.tree != NULL &&
            server->slit_ui.width > 0 && server->slit_ui.height > 0) {
        const int lx = (int)server->cursor->x;
        const int ly = (int)server->cursor->y;
        if (lx >= server->slit_ui.x && lx < server->slit_ui.x + server->slit_ui.width &&
                ly >= server->slit_ui.y && ly < server->slit_ui.y + server->slit_ui.height) {
            if (slit_is_topmost_at(&server->slit_ui, server->scene, lx, ly)) {
                return FBWL_MOUSEBIND_SLIT;
            }
        }
    }
    if (view == NULL && surface == NULL) {
        return FBWL_MOUSEBIND_DESKTOP;
    }
    if (view == NULL) {
        return FBWL_MOUSEBIND_ANY;
    }
    if (view->in_slit) {
        return FBWL_MOUSEBIND_SLIT;
    }
    if (surface != NULL) {
        return FBWL_MOUSEBIND_WINDOW;
    }

    const struct fbwl_decor_hit hit =
        fbwl_view_decor_hit_test(view, server != NULL ? &server->decor_theme : NULL, server->cursor->x, server->cursor->y);
    if (server != NULL && fbwl_view_tabs_bar_contains(view, server->cursor->x, server->cursor->y)) {
        return FBWL_MOUSEBIND_TAB;
    }
    if (hit.kind == FBWL_DECOR_HIT_TITLEBAR) {
        return FBWL_MOUSEBIND_TITLEBAR;
    }
    if (hit.kind == FBWL_DECOR_HIT_RESIZE) {
        if ((hit.edges & (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) {
            return FBWL_MOUSEBIND_LEFT_GRIP;
        }
        if ((hit.edges & (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)) == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)) {
            return FBWL_MOUSEBIND_RIGHT_GRIP;
        }
        return FBWL_MOUSEBIND_WINDOW_BORDER;
    }
    return FBWL_MOUSEBIND_WINDOW;
}

bool server_mousebind_capture_handle_press(struct fbwl_server *server, struct fbwl_view *view, struct wlr_surface *surface,
        const struct wlr_pointer_button_event *event, uint32_t modifiers) {
    if (server == NULL || event == NULL || server->cursor == NULL) {
        return false;
    }

    const int fb_button = server_fluxbox_mouse_button_from_event(event->button);
    if (fb_button == 0) {
        return false;
    }

    const bool is_double = server->double_click_interval_ms > 0 &&
        event->time_msec >= server->last_button_time_msec &&
        fb_button == server->last_button &&
        event->time_msec - server->last_button_time_msec < (uint32_t)server->double_click_interval_ms;

    struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
    hooks.button = event->button;
    hooks.cursor_x = (int)server->cursor->x;
    hooks.cursor_y = (int)server->cursor->y;

    const enum fbwl_mousebinding_context ctx = server_mousebinding_context_at(server, view, surface);

    struct fbwl_view *target = NULL;
    uint64_t target_seq = 0;
    if (ctx != FBWL_MOUSEBIND_DESKTOP && ctx != FBWL_MOUSEBIND_TOOLBAR && ctx != FBWL_MOUSEBIND_SLIT) {
        target = view;
        if (target != NULL) {
            target_seq = target->create_seq;
        }
    }

    const bool has_click = fbwl_mousebindings_has(server->mousebindings, server->mousebinding_count, ctx,
        FBWL_MOUSEBIND_EVENT_CLICK, fb_button, modifiers, &hooks);
    const bool has_move = fbwl_mousebindings_has(server->mousebindings, server->mousebinding_count, ctx,
        FBWL_MOUSEBIND_EVENT_MOVE, fb_button, modifiers, &hooks);

    const bool handled_press = fbwl_mousebindings_handle(server->mousebindings, server->mousebinding_count, ctx,
        FBWL_MOUSEBIND_EVENT_PRESS, fb_button, modifiers, is_double, target, &hooks);

    server->last_button_time_msec = event->time_msec;
    server->last_button = fb_button;

    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        mousebind_capture_clear(server);
        return handled_press || has_click || has_move;
    }

    if (handled_press || has_click || has_move) {
        server->mousebind_capture_active = true;
        server->mousebind_capture_has_click = has_click;
        server->mousebind_capture_has_move = has_move;
        server->mousebind_capture_moved = false;
        server->mousebind_capture_button = event->button;
        server->mousebind_capture_fb_button = fb_button;
        server->mousebind_capture_modifiers = modifiers;
        server->mousebind_capture_context = ctx;
        server->mousebind_capture_press_x = hooks.cursor_x;
        server->mousebind_capture_press_y = hooks.cursor_y;
        server->mousebind_capture_target_create_seq = target_seq;
        return true;
    }

    return false;
}

void server_mousebind_capture_handle_motion(struct fbwl_server *server) {
    if (server == NULL || !server->mousebind_capture_active || server->cursor == NULL) {
        return;
    }
    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        mousebind_capture_clear(server);
        return;
    }

    const int cx = (int)server->cursor->x;
    const int cy = (int)server->cursor->y;

    const int dx = abs(cx - server->mousebind_capture_press_x);
    const int dy = abs(cy - server->mousebind_capture_press_y);
    if (!server->mousebind_capture_moved &&
            dx + dy >= FBWL_MOUSEBIND_DRAG_THRESHOLD_PX) {
        server->mousebind_capture_moved = true;
    }
    if (!server->mousebind_capture_moved || !server->mousebind_capture_has_move) {
        return;
    }

    struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
    hooks.button = server->mousebind_capture_button;
    hooks.cursor_x = cx;
    hooks.cursor_y = cy;

    struct fbwl_view *target = NULL;
    if (server->mousebind_capture_context != FBWL_MOUSEBIND_DESKTOP &&
            server->mousebind_capture_context != FBWL_MOUSEBIND_TOOLBAR &&
            server->mousebind_capture_context != FBWL_MOUSEBIND_SLIT) {
        target = find_view_by_create_seq(server, server->mousebind_capture_target_create_seq);
    }

    (void)fbwl_mousebindings_handle(server->mousebindings, server->mousebinding_count,
        server->mousebind_capture_context, FBWL_MOUSEBIND_EVENT_MOVE,
        server->mousebind_capture_fb_button, server->mousebind_capture_modifiers, false, target, &hooks);

    server->mousebind_capture_has_move = false;
    server->mousebind_capture_has_click = false;
    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        if (server->grab.button == server->mousebind_capture_button) {
            server->grab.grab_x = (double)server->mousebind_capture_press_x;
            server->grab.grab_y = (double)server->mousebind_capture_press_y;
        }
        server_grab_update(server);
        mousebind_capture_clear(server);
    }
}

bool server_mousebind_capture_handle_release(struct fbwl_server *server, const struct wlr_pointer_button_event *event) {
    if (server == NULL || event == NULL || server->cursor == NULL) {
        return false;
    }
    if (!server->mousebind_capture_active) {
        return false;
    }
    if (event->button != server->mousebind_capture_button) {
        return false;
    }

    const int cx = (int)server->cursor->x;
    const int cy = (int)server->cursor->y;

    const int dx = abs(cx - server->mousebind_capture_press_x);
    const int dy = abs(cy - server->mousebind_capture_press_y);
    if (dx + dy >= FBWL_MOUSEBIND_DRAG_THRESHOLD_PX) {
        server->mousebind_capture_moved = true;
    }

    if (server->mousebind_capture_has_click && !server->mousebind_capture_moved) {
        struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
        hooks.button = server->mousebind_capture_button;
        hooks.cursor_x = cx;
        hooks.cursor_y = cy;

        struct fbwl_view *target = NULL;
        if (server->mousebind_capture_context != FBWL_MOUSEBIND_DESKTOP &&
                server->mousebind_capture_context != FBWL_MOUSEBIND_TOOLBAR &&
                server->mousebind_capture_context != FBWL_MOUSEBIND_SLIT) {
            target = find_view_by_create_seq(server, server->mousebind_capture_target_create_seq);
        }

        (void)fbwl_mousebindings_handle(server->mousebindings, server->mousebinding_count,
            server->mousebind_capture_context, FBWL_MOUSEBIND_EVENT_CLICK,
            server->mousebind_capture_fb_button, server->mousebind_capture_modifiers, false, target, &hooks);
    }

    mousebind_capture_clear(server);
    return true;
}
