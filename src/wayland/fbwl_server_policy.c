#include <linux/input-event-codes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_cursor.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"

static struct fbwl_ui_toolbar_env toolbar_ui_env(struct fbwl_server *server);

static void session_lock_clear_keyboard_focus(void *userdata) {
    clear_keyboard_focus(userdata);
}

static void session_lock_text_input_update_focus(void *userdata, struct wlr_surface *surface) {
    server_text_input_update_focus(userdata, surface);
}

static void session_lock_update_shortcuts_inhibitor(void *userdata) {
    server_update_shortcuts_inhibitor(userdata);
}

struct fbwl_session_lock_hooks session_lock_hooks(struct fbwl_server *server) {
    return (struct fbwl_session_lock_hooks){
        .clear_keyboard_focus = session_lock_clear_keyboard_focus,
        .text_input_update_focus = session_lock_text_input_update_focus,
        .update_shortcuts_inhibitor = session_lock_update_shortcuts_inhibitor,
        .userdata = server,
    };
}

void server_text_input_update_focus(struct fbwl_server *server, struct wlr_surface *surface) {
    if (server == NULL) {
        return;
    }

    fbwl_text_input_update_focus(&server->text_input, surface);
}

bool server_keyboard_shortcuts_inhibited(struct fbwl_server *server) {
    if (server == NULL) {
        return false;
    }
    return fbwl_shortcuts_inhibit_is_inhibited(&server->shortcuts_inhibit);
}

void server_update_shortcuts_inhibitor(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    fbwl_shortcuts_inhibit_update(&server->shortcuts_inhibit);
}

static void raise_view(struct fbwl_view *view, const char *why) {
    if (view == NULL || view->scene_tree == NULL) {
        return;
    }
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wlr_log(WLR_INFO, "Raise: %s reason=%s",
        fbwl_view_display_title(view),
        why != NULL ? why : "(null)");
}

void focus_view(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;
    if (server != NULL && fbwl_session_lock_is_locked(&server->session_lock)) {
        return;
    }
    struct wlr_seat *seat = server->seat;

    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    if (prev_surface == surface) {
        return;
    }

    wlr_log(WLR_INFO, "Focus: %s (%s)",
        fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
        fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)");

    struct fbwl_view *prev_view = server->focused_view;
    if (prev_view != NULL && prev_view != view && prev_view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_activated(prev_view->foreign_toplevel, false);
    }
    if (prev_view != NULL && prev_view != view) {
        fbwl_view_decor_set_active(prev_view, &server->decor_theme, false);
    }

    if (prev_surface != NULL) {
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }

        struct wlr_xwayland_surface *prev_xsurface =
            wlr_xwayland_surface_try_from_wlr_surface(prev_surface);
        if (prev_xsurface != NULL) {
            wlr_xwayland_surface_activate(prev_xsurface, false);
        }
    }

    if (!server->focus.auto_raise) {
        server->auto_raise_pending_view = NULL;
    } else if (server->focus_reason == FBWL_FOCUS_REASON_POINTER_MOTION && server->focus.auto_raise_delay_ms > 0) {
        server->auto_raise_pending_view = view;
        if (server->auto_raise_timer != NULL) {
            wl_event_source_timer_update(server->auto_raise_timer, server->focus.auto_raise_delay_ms);
        }
    } else {
        server->auto_raise_pending_view = NULL;
        raise_view(view, "focus");
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);

    fbwl_view_set_activated(view, true);
    fbwl_view_decor_set_active(view, &server->decor_theme, true);
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel, true);
    }
    server->focused_view = view;
    server_toolbar_ui_update_iconbar_focus(server);
    if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
    server_text_input_update_focus(server, surface);
    server_update_shortcuts_inhibitor(server);
}

int server_auto_raise_timer(void *data) {
    struct fbwl_server *server = data;
    if (server == NULL) {
        return 0;
    }

    struct fbwl_view *view = server->auto_raise_pending_view;
    if (view != NULL && server->focus.auto_raise && server->focused_view == view) {
        raise_view(view, "autoRaiseDelay");
    }
    server->auto_raise_pending_view = NULL;
    return 0;
}

void clear_keyboard_focus(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    struct wlr_surface *prev_surface = server->seat->keyboard_state.focused_surface;
    if (prev_surface != NULL) {
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }

        struct wlr_xwayland_surface *prev_xsurface =
            wlr_xwayland_surface_try_from_wlr_surface(prev_surface);
        if (prev_xsurface != NULL) {
            wlr_xwayland_surface_activate(prev_xsurface, false);
        }
    }

    if (server->focused_view != NULL && server->focused_view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_activated(server->focused_view->foreign_toplevel, false);
    }
    if (server->focused_view != NULL) {
        fbwl_view_decor_set_active(server->focused_view, &server->decor_theme, false);
    }
    server->focused_view = NULL;

    wlr_seat_keyboard_clear_focus(server->seat);
    server_text_input_update_focus(server, NULL);
    server_update_shortcuts_inhibitor(server);
}

static void server_osd_ui_show_workspace(struct fbwl_server *server, int workspace);

void apply_workspace_visibility(struct fbwl_server *server, const char *why) {
    const int cur = fbwm_core_workspace_current(&server->wm);
    wlr_log(WLR_INFO, "Workspace: apply current=%d reason=%s", cur + 1, why != NULL ? why : "(null)");

    if (server->osd_ui.enabled) {
        if (server->osd_ui.last_workspace != cur) {
            server->osd_ui.last_workspace = cur;
            server_osd_ui_show_workspace(server, cur);
        }
    } else {
        server->osd_ui.last_workspace = cur;
    }

    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || view->scene_tree == NULL) {
            continue;
        }

        const bool visible = fbwm_core_view_is_visible(&server->wm, wm_view);
        wlr_scene_node_set_enabled(&view->scene_tree->node, visible);

        const char *title = NULL;
        if (wm_view->ops != NULL && wm_view->ops->title != NULL) {
            title = wm_view->ops->title(wm_view);
        }
        wlr_log(WLR_INFO, "Workspace: view=%s ws=%d visible=%d",
            title != NULL ? title : "(no-title)", wm_view->workspace + 1, visible ? 1 : 0);
    }

    server_toolbar_ui_rebuild(server);

    if (server->wm.focused == NULL) {
        clear_keyboard_focus(server);
    }
}

void view_set_minimized(struct fbwl_view *view, bool minimized, const char *why) {
    if (view == NULL || view->server == NULL) {
        return;
    }

    if (minimized == view->minimized) {
        if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
            wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
        }
        return;
    }

    view->minimized = minimized;
    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_set_minimized(view->xwayland_surface, minimized);
    }
    if (view->foreign_toplevel != NULL) {
        wlr_foreign_toplevel_handle_v1_set_minimized(view->foreign_toplevel, minimized);
    }
    wlr_log(WLR_INFO, "Minimize: %s %s reason=%s",
        fbwl_view_display_title(view),
        minimized ? "on" : "off",
        why != NULL ? why : "(null)");

    if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
    }

    apply_workspace_visibility(view->server, minimized ? "minimize-on" : "minimize-off");

    if (minimized) {
        fbwm_core_refocus(&view->server->wm);
        if (view->server->wm.focused == NULL) {
            clear_keyboard_focus(view->server);
        }
        return;
    }

    if (fbwm_core_view_is_visible(&view->server->wm, &view->wm_view)) {
        fbwm_core_focus_view(&view->server->wm, &view->wm_view);
        return;
    }

    fbwm_core_refocus(&view->server->wm);
    if (view->server->wm.focused == NULL) {
        clear_keyboard_focus(view->server);
    }
}

static uint32_t server_keyboard_modifiers(struct fbwl_server *server) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    return keyboard != NULL ? wlr_keyboard_get_modifiers(keyboard) : 0;
}

static void cursor_grab_update(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_grab_update(&server->grab, server->cursor);
}

static bool cursor_menu_is_open(void *userdata) {
    const struct fbwl_server *server = userdata;
    return server != NULL && server->menu_ui.open;
}

static ssize_t cursor_menu_index_at(void *userdata, int lx, int ly) {
    const struct fbwl_server *server = userdata;
    if (server == NULL) {
        return -1;
    }
    return server_menu_ui_index_at(&server->menu_ui, lx, ly);
}

static void cursor_menu_set_selected(void *userdata, size_t idx) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    server_menu_ui_set_selected(server, idx);
}

struct fbwl_cursor_menu_hooks server_cursor_menu_hooks(struct fbwl_server *server) {
    return (struct fbwl_cursor_menu_hooks){
        .userdata = server,
        .is_open = cursor_menu_is_open,
        .index_at = cursor_menu_index_at,
        .set_selected = cursor_menu_set_selected,
    };
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    fbwl_idle_notify_activity(&server->idle);
    const struct fbwl_cursor_menu_hooks hooks = server_cursor_menu_hooks(server);
    fbwl_cursor_handle_motion(server->cursor, server->cursor_mgr, server->scene, server->seat,
        server->pointer_constraints.relative_pointer_mgr,
        server->pointer_constraints.constraints,
        &server->pointer_constraints.active,
        &server->pointer_constraints.phys_valid, &server->pointer_constraints.phys_x, &server->pointer_constraints.phys_y,
        server->grab.mode, cursor_grab_update, server,
        &hooks, event);

    if (server->focus.model != FBWL_FOCUS_MODEL_CLICK_TO_FOCUS &&
            server->grab.mode == FBWL_CURSOR_PASSTHROUGH &&
            !server->cmd_dialog_ui.open &&
            !server->menu_ui.open) {
        double sx = 0, sy = 0;
        struct wlr_surface *surface = NULL;
        struct fbwl_view *view = fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        if (view != NULL && view != server->focused_view) {
            const enum fbwl_focus_reason prev_reason = server->focus_reason;
            server->focus_reason = FBWL_FOCUS_REASON_POINTER_MOTION;
            fbwm_core_focus_view(&server->wm, &view->wm_view);
            server->focus_reason = prev_reason;
        }
    }

    {
        const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
        fbwl_ui_toolbar_handle_motion(&server->toolbar_ui, &env,
            (int)server->cursor->x, (int)server->cursor->y, server->focus.auto_raise_delay_ms);
    }
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    fbwl_idle_notify_activity(&server->idle);
    const struct fbwl_cursor_menu_hooks hooks = server_cursor_menu_hooks(server);
    fbwl_cursor_handle_motion_absolute(server->cursor, server->cursor_mgr, server->scene, server->seat,
        server->pointer_constraints.relative_pointer_mgr,
        server->pointer_constraints.constraints,
        &server->pointer_constraints.active,
        &server->pointer_constraints.phys_valid, &server->pointer_constraints.phys_x, &server->pointer_constraints.phys_y,
        server->grab.mode, cursor_grab_update, server,
        &hooks, event);

    if (server->focus.model != FBWL_FOCUS_MODEL_CLICK_TO_FOCUS &&
            server->grab.mode == FBWL_CURSOR_PASSTHROUGH &&
            !server->cmd_dialog_ui.open &&
            !server->menu_ui.open) {
        double sx = 0, sy = 0;
        struct wlr_surface *surface = NULL;
        struct fbwl_view *view = fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        if (view != NULL && view != server->focused_view) {
            const enum fbwl_focus_reason prev_reason = server->focus_reason;
            server->focus_reason = FBWL_FOCUS_REASON_POINTER_MOTION;
            fbwm_core_focus_view(&server->wm, &view->wm_view);
            server->focus_reason = prev_reason;
        }
    }

    {
        const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
        fbwl_ui_toolbar_handle_motion(&server->toolbar_ui, &env,
            (int)server->cursor->x, (int)server->cursor->y, server->focus.auto_raise_delay_ms);
    }
}

static void server_menu_ui_open_root(struct fbwl_server *server, int x, int y);
static void server_menu_ui_open_window(struct fbwl_server *server, struct fbwl_view *view, int x, int y);

void server_cursor_button(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    fbwl_idle_notify_activity(&server->idle);

    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
                event->button == server->grab.button) {
            struct fbwl_view *view = server->grab.view;
            if (view != NULL && server->grab.mode == FBWL_CURSOR_MOVE) {
                wlr_log(WLR_INFO, "Move: %s x=%d y=%d",
                    fbwl_view_display_title(view),
                    view->x, view->y);
            }
            if (view != NULL && server->grab.mode == FBWL_CURSOR_RESIZE) {
                wlr_log(WLR_INFO, "Resize: %s w=%d h=%d",
                    fbwl_view_display_title(view),
                    server->grab.last_w, server->grab.last_h);
                if (view->type == FBWL_VIEW_XDG) {
                    wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, false);
                }
            }
            server->grab.mode = FBWL_CURSOR_PASSTHROUGH;
            server->grab.view = NULL;
            server->grab.button = 0;
            server->grab.resize_edges = WLR_EDGE_NONE;
        }
        return;
    }

    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (server->cmd_dialog_ui.open) {
            server_cmd_dialog_ui_close(server, "pointer");
            return;
        }

        if (server->menu_ui.open) {
            if (server_menu_ui_handle_click(server,
                    (int)server->cursor->x, (int)server->cursor->y, event->button)) {
                return;
            }
        }

        if (server_toolbar_ui_handle_click(server,
                (int)server->cursor->x, (int)server->cursor->y, event->button)) {
            return;
        }

        double sx = 0, sy = 0;
        struct wlr_surface *surface = NULL;
        struct fbwl_view *view = fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        wlr_log(WLR_INFO, "Pointer press at %.1f,%.1f hit=%s",
            server->cursor->x, server->cursor->y,
            view != NULL ? fbwl_view_display_title(view) : "(none)");

        const uint32_t modifiers = server_keyboard_modifiers(server);
        const bool alt = (modifiers & WLR_MODIFIER_ALT) != 0;

        if (view == NULL && surface == NULL && event->button == BTN_RIGHT) {
            server_menu_ui_open_root(server, (int)server->cursor->x, (int)server->cursor->y);
            return;
        }

        if (alt && view != NULL && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
            const enum fbwl_focus_reason prev_reason = server->focus_reason;
            server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
            fbwm_core_focus_view(&server->wm, &view->wm_view);
            server->focus_reason = prev_reason;
            raise_view(view, "alt-grab");

            server->grab.view = view;
            server->grab.button = event->button;
            server->grab.resize_edges = (event->button == BTN_RIGHT) ? (WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM) : WLR_EDGE_NONE;
            server->grab.grab_x = server->cursor->x;
            server->grab.grab_y = server->cursor->y;
            server->grab.view_x = view->x;
            server->grab.view_y = view->y;
            server->grab.view_w =
                fbwl_view_current_width(view);
            server->grab.view_h =
                fbwl_view_current_height(view);
            server->grab.last_w = server->grab.view_w;
            server->grab.last_h = server->grab.view_h;

            if (event->button == BTN_LEFT) {
                server->grab.mode = FBWL_CURSOR_MOVE;
            } else {
                server->grab.mode = FBWL_CURSOR_RESIZE;
                if (view->type == FBWL_VIEW_XDG) {
                    wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, true);
                }
            }
            return;
        }

        if (view != NULL && surface == NULL && event->button == BTN_RIGHT) {
            const struct fbwl_decor_hit hit =
                fbwl_view_decor_hit_test(view, &server->decor_theme, server->cursor->x, server->cursor->y);
            if (hit.kind == FBWL_DECOR_HIT_TITLEBAR) {
                const enum fbwl_focus_reason prev_reason = server->focus_reason;
                server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
                fbwm_core_focus_view(&server->wm, &view->wm_view);
                server->focus_reason = prev_reason;
                raise_view(view, "titlebar-menu");
                server_menu_ui_open_window(server, view, (int)server->cursor->x, (int)server->cursor->y);
                return;
            }
        }

        if (view != NULL && surface == NULL && event->button == BTN_LEFT) {
            const struct fbwl_decor_hit hit =
                fbwl_view_decor_hit_test(view, &server->decor_theme, server->cursor->x, server->cursor->y);
            if (hit.kind != FBWL_DECOR_HIT_NONE) {
                const enum fbwl_focus_reason prev_reason = server->focus_reason;
                server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
                fbwm_core_focus_view(&server->wm, &view->wm_view);
                server->focus_reason = prev_reason;
                raise_view(view, "decor");

                if (hit.kind == FBWL_DECOR_HIT_TITLEBAR) {
                    server->grab.view = view;
                    server->grab.button = event->button;
                    server->grab.resize_edges = WLR_EDGE_NONE;
                    server->grab.grab_x = server->cursor->x;
                    server->grab.grab_y = server->cursor->y;
                    server->grab.view_x = view->x;
                    server->grab.view_y = view->y;
                    server->grab.view_w = fbwl_view_current_width(view);
                    server->grab.view_h = fbwl_view_current_height(view);
                    server->grab.last_w = server->grab.view_w;
                    server->grab.last_h = server->grab.view_h;
                    server->grab.mode = FBWL_CURSOR_MOVE;
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_RESIZE) {
                    server->grab.view = view;
                    server->grab.button = event->button;
                    server->grab.resize_edges = hit.edges;
                    server->grab.grab_x = server->cursor->x;
                    server->grab.grab_y = server->cursor->y;
                    server->grab.view_x = view->x;
                    server->grab.view_y = view->y;
                    server->grab.view_w = fbwl_view_current_width(view);
                    server->grab.view_h = fbwl_view_current_height(view);
                    server->grab.last_w = server->grab.view_w;
                    server->grab.last_h = server->grab.view_h;
                    server->grab.mode = FBWL_CURSOR_RESIZE;
                    if (view->type == FBWL_VIEW_XDG) {
                        wlr_xdg_toplevel_set_resizing(view->xdg_toplevel, true);
                    }
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_CLOSE) {
                    if (view->type == FBWL_VIEW_XDG) {
                        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
                    } else if (view->type == FBWL_VIEW_XWAYLAND) {
                        wlr_xwayland_surface_close(view->xwayland_surface);
                    }
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_MAX) {
                    fbwl_view_set_maximized(view, !view->maximized, server->output_layout, &server->outputs);
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_MIN) {
                    view_set_minimized(view, !view->minimized, "decor-button");
                    return;
                }
            }
        }

        if (view != NULL) {
            const bool click_raises_anywhere = server->focus.click_raises;
            bool click_raises = click_raises_anywhere;
            if (!click_raises_anywhere && surface == NULL) {
                const struct fbwl_decor_hit hit =
                    fbwl_view_decor_hit_test(view, &server->decor_theme, server->cursor->x, server->cursor->y);
                click_raises = hit.kind != FBWL_DECOR_HIT_NONE;
            }

            const enum fbwl_focus_reason prev_reason = server->focus_reason;
            server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
            fbwm_core_focus_view(&server->wm, &view->wm_view);
            server->focus_reason = prev_reason;
            if (click_raises) {
                raise_view(view, "click");
            }
        }
    }

    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
        event->button, event->state);
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    fbwl_idle_notify_activity(&server->idle);
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void toolbar_ui_apply_workspace_visibility(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    apply_workspace_visibility(server, why);
}

static void toolbar_ui_view_set_minimized(void *userdata, struct fbwl_view *view, bool minimized, const char *why) {
    (void)userdata;
    view_set_minimized(view, minimized, why);
}

static struct fbwl_ui_toolbar_env toolbar_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_toolbar_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_top = server != NULL ? server->layer_top : NULL,
        .output_layout = server != NULL ? server->output_layout : NULL,
        .wl_display = server != NULL ? server->wl_display : NULL,
        .wm = server != NULL ? &server->wm : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
        .focused_view = server != NULL ? server->focused_view : NULL,
#ifdef HAVE_SYSTEMD
        .sni = server != NULL ? &server->sni : NULL,
#endif
    };
}

static struct fbwl_ui_toolbar_hooks toolbar_ui_hooks(struct fbwl_server *server) {
    return (struct fbwl_ui_toolbar_hooks){
        .userdata = server,
        .apply_workspace_visibility = toolbar_ui_apply_workspace_visibility,
        .view_set_minimized = toolbar_ui_view_set_minimized,
    };
}

void server_toolbar_ui_rebuild(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    fbwl_ui_toolbar_rebuild(&server->toolbar_ui, &env);
}

#ifdef HAVE_SYSTEMD
void server_sni_on_change(void *userdata) {
    server_toolbar_ui_rebuild(userdata);
}
#endif

void server_toolbar_ui_update_position(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    fbwl_ui_toolbar_update_position(&server->toolbar_ui, &env);
}

void server_toolbar_ui_update_iconbar_focus(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_ui_toolbar_update_iconbar_focus(&server->toolbar_ui, &server->decor_theme, server->focused_view);
}

bool server_toolbar_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button) {
    if (server == NULL) {
        return false;
    }

    const struct fbwl_ui_toolbar_env env = toolbar_ui_env(server);
    const struct fbwl_ui_toolbar_hooks hooks = toolbar_ui_hooks(server);
    return fbwl_ui_toolbar_handle_click(&server->toolbar_ui, &env, &hooks, lx, ly, button);
}

void server_cmd_dialog_ui_update_position(struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return;
    }
    fbwl_ui_cmd_dialog_update_position(&server->cmd_dialog_ui, server->output_layout);
}

void server_cmd_dialog_ui_close(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_cmd_dialog_close(&server->cmd_dialog_ui, why);
}

static void server_menu_ui_close(struct fbwl_server *server, const char *why);

void server_cmd_dialog_ui_open(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL) {
        return;
    }

    server_menu_ui_close(server, "cmd-dialog-open");
    fbwl_ui_cmd_dialog_open(&server->cmd_dialog_ui, server->scene, server->layer_overlay,
        &server->decor_theme, server->output_layout);
}

bool server_cmd_dialog_ui_handle_key(struct fbwl_server *server, xkb_keysym_t sym, uint32_t modifiers) {
    if (server == NULL) {
        return false;
    }
    return fbwl_ui_cmd_dialog_handle_key(&server->cmd_dialog_ui, sym, modifiers);
}

static void server_osd_ui_hide(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_osd_hide(&server->osd_ui, why);
}

int server_osd_hide_timer(void *data) {
    struct fbwl_server *server = data;
    server_osd_ui_hide(server, "timer");
    return 0;
}

void server_osd_ui_update_position(struct fbwl_server *server) {
    if (server == NULL || server->output_layout == NULL) {
        return;
    }
    fbwl_ui_osd_update_position(&server->osd_ui, server->output_layout);
}

static void server_osd_ui_show_workspace(struct fbwl_server *server, int workspace) {
    if (server == NULL || server->scene == NULL) {
        return;
    }
    const char *name = fbwm_core_workspace_name(&server->wm, workspace);
    fbwl_ui_osd_show_workspace(&server->osd_ui, server->scene, server->layer_top,
        &server->decor_theme, server->output_layout, workspace, name);
}

void server_osd_ui_destroy(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_osd_destroy(&server->osd_ui);
}

static void menu_ui_spawn(void *userdata, const char *cmd) {
    (void)userdata;
    fbwl_spawn(cmd);
}

static void menu_ui_terminate(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    wl_display_terminate(server->wl_display);
}

static void menu_ui_view_close(void *userdata, struct fbwl_view *view) {
    (void)userdata;
    if (view == NULL) {
        return;
    }
    if (view->type == FBWL_VIEW_XDG) {
        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
    } else if (view->type == FBWL_VIEW_XWAYLAND) {
        wlr_xwayland_surface_close(view->xwayland_surface);
    }
}

static void menu_ui_view_set_minimized(void *userdata, struct fbwl_view *view, bool minimized, const char *why) {
    (void)userdata;
    view_set_minimized(view, minimized, why);
}

static void menu_ui_view_set_maximized(void *userdata, struct fbwl_view *view, bool maximized) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_view_set_maximized(view, maximized, server->output_layout, &server->outputs);
}

static void menu_ui_view_set_fullscreen(void *userdata, struct fbwl_view *view, bool fullscreen) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_view_set_fullscreen(view, fullscreen, server->output_layout, &server->outputs,
        server->layer_normal, server->layer_fullscreen, NULL);
}

static struct fbwl_ui_menu_env menu_ui_env(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_env){
        .scene = server != NULL ? server->scene : NULL,
        .layer_overlay = server != NULL ? server->layer_overlay : NULL,
        .decor_theme = server != NULL ? &server->decor_theme : NULL,
    };
}

static struct fbwl_ui_menu_hooks menu_ui_hooks(struct fbwl_server *server) {
    return (struct fbwl_ui_menu_hooks){
        .userdata = server,
        .spawn = menu_ui_spawn,
        .terminate = menu_ui_terminate,
        .view_close = menu_ui_view_close,
        .view_set_minimized = menu_ui_view_set_minimized,
        .view_set_maximized = menu_ui_view_set_maximized,
        .view_set_fullscreen = menu_ui_view_set_fullscreen,
    };
}

static void server_menu_ui_close(struct fbwl_server *server, const char *why) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_menu_close(&server->menu_ui, why);
}

static void server_menu_ui_open_root(struct fbwl_server *server, int x, int y) {
    if (server == NULL) {
        return;
    }
    if (server->root_menu == NULL) {
        server_menu_create_default(server);
    }
    if (server->root_menu == NULL) {
        return;
    }

    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_root(&server->menu_ui, &env, server->root_menu, x, y);
}

static void server_menu_ui_open_window(struct fbwl_server *server, struct fbwl_view *view, int x, int y) {
    if (server == NULL || view == NULL) {
        return;
    }
    if (server->window_menu == NULL) {
        server_menu_create_window(server);
    }
    if (server->window_menu == NULL) {
        return;
    }

    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    fbwl_ui_menu_open_window(&server->menu_ui, &env, server->window_menu, view, x, y);
}

ssize_t server_menu_ui_index_at(const struct fbwl_menu_ui *ui, int lx, int ly) {
    return fbwl_ui_menu_index_at(ui, lx, ly);
}

void server_menu_ui_set_selected(struct fbwl_server *server, size_t idx) {
    if (server == NULL) {
        return;
    }
    fbwl_ui_menu_set_selected(&server->menu_ui, idx);
}

bool server_menu_ui_handle_keypress(struct fbwl_server *server, xkb_keysym_t sym) {
    if (server == NULL) {
        return false;
    }
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    const struct fbwl_ui_menu_hooks hooks = menu_ui_hooks(server);
    return fbwl_ui_menu_handle_keypress(&server->menu_ui, &env, &hooks, sym);
}

bool server_menu_ui_handle_click(struct fbwl_server *server, int lx, int ly, uint32_t button) {
    if (server == NULL) {
        return false;
    }
    const struct fbwl_ui_menu_env env = menu_ui_env(server);
    const struct fbwl_ui_menu_hooks hooks = menu_ui_hooks(server);
    return fbwl_ui_menu_handle_click(&server->menu_ui, &env, &hooks, lx, ly, button);
}

void server_menu_free(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    server_menu_ui_close(server, "free");
    fbwl_menu_free(server->root_menu);
    server->root_menu = NULL;
    fbwl_menu_free(server->window_menu);
    server->window_menu = NULL;
    free(server->menu_file);
    server->menu_file = NULL;
}

static void keybindings_terminate(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->wl_display == NULL) {
        return;
    }
    wl_display_terminate(server->wl_display);
}

static void keybindings_spawn(void *userdata, const char *cmd) {
    (void)userdata;
    fbwl_spawn(cmd);
}

static void keybindings_command_dialog_open(void *userdata) {
    struct fbwl_server *server = userdata;
    server_cmd_dialog_ui_open(server);
}

static void keybindings_apply_workspace_visibility(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    apply_workspace_visibility(server, why);
}

static void keybindings_view_set_maximized(void *userdata, struct fbwl_view *view, bool maximized) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_view_set_maximized(view, maximized, server->output_layout, &server->outputs);
}

static void keybindings_view_set_fullscreen(void *userdata, struct fbwl_view *view, bool fullscreen) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    fbwl_view_set_fullscreen(view, fullscreen, server->output_layout, &server->outputs,
        server->layer_normal, server->layer_fullscreen, NULL);
}

static void keybindings_view_set_minimized(void *userdata, struct fbwl_view *view, bool minimized, const char *why) {
    (void)userdata;
    view_set_minimized(view, minimized, why);
}

struct fbwl_keybindings_hooks keybindings_hooks(struct fbwl_server *server) {
    return (struct fbwl_keybindings_hooks){
        .userdata = server,
        .wm = server != NULL ? &server->wm : NULL,
        .terminate = keybindings_terminate,
        .spawn = keybindings_spawn,
        .command_dialog_open = keybindings_command_dialog_open,
        .apply_workspace_visibility = keybindings_apply_workspace_visibility,
        .view_set_maximized = keybindings_view_set_maximized,
        .view_set_fullscreen = keybindings_view_set_fullscreen,
        .view_set_minimized = keybindings_view_set_minimized,
    };
}

void server_apps_rules_apply_pre_map(struct fbwl_view *view,
        const struct fbwl_apps_rule *rule) {
    if (view == NULL || view->server == NULL || rule == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;

    if (rule->set_workspace) {
        int ws = rule->workspace;
        const int count = fbwm_core_workspace_count(&server->wm);
            if (ws < 0 || ws >= count) {
                wlr_log(WLR_ERROR, "Apps: ignoring out-of-range workspace_id=%d (count=%d) for %s",
                ws, count, fbwl_view_display_title(view));
            } else {
                view->wm_view.workspace = ws;
            }
    }

    if (rule->set_sticky) {
        view->wm_view.sticky = rule->sticky;
    }

    if (rule->set_workspace && rule->set_jump && rule->jump) {
        const int count = fbwm_core_workspace_count(&server->wm);
        if (rule->workspace >= 0 && rule->workspace < count) {
            fbwm_core_workspace_switch(&server->wm, rule->workspace);
        }
    }
}

void server_apps_rules_apply_post_map(struct fbwl_view *view,
        const struct fbwl_apps_rule *rule) {
    if (view == NULL || rule == NULL) {
        return;
    }

    if (rule->set_maximized) {
        fbwl_view_set_maximized(view, rule->maximized, view->server->output_layout, &view->server->outputs);
    }
    if (rule->set_fullscreen) {
        fbwl_view_set_fullscreen(view, rule->fullscreen, view->server->output_layout, &view->server->outputs,
            view->server->layer_normal, view->server->layer_fullscreen, NULL);
    }
    if (rule->set_minimized) {
        view_set_minimized(view, rule->minimized, "apps");
    }
}
