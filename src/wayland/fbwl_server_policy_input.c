#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include "wayland/fbwl_cursor.h"
#include "wayland/fbwl_server_keybinding_actions.h"
#include "wayland/fbwl_server_menu_actions.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"

static void server_update_pointer_focus(struct fbwl_server *server, enum fbwl_focus_reason reason,
        const char *why);

struct fbwl_view *server_strict_mousefocus_view_under_cursor(struct fbwl_server *server) {
    if (server == NULL || server->scene == NULL || server->cursor == NULL) {
        return NULL;
    }

    const struct fbwl_screen_config *cfg =
        fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y);
    const enum fbwl_focus_model model =
        cfg != NULL ? cfg->focus.model : server->focus.model;
    if (model != FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS) {
        return NULL;
    }

    double sx = 0, sy = 0;
    struct wlr_surface *surface = NULL;
    return fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
}

void server_strict_mousefocus_recheck(struct fbwl_server *server, const char *why) {
    if (server == NULL || server->scene == NULL || server->cursor == NULL) {
        return;
    }

    const struct fbwl_screen_config *cfg =
        fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y);
    const enum fbwl_focus_model model =
        cfg != NULL ? cfg->focus.model : server->focus.model;
    if (model != FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS) {
        return;
    }

    server_update_pointer_focus(server, FBWL_FOCUS_REASON_POINTER_MOTION, why);
}

void server_strict_mousefocus_recheck_after_restack(struct fbwl_server *server, struct fbwl_view *before, const char *why) {
    if (server == NULL || server->scene == NULL || server->cursor == NULL) {
        return;
    }

    const struct fbwl_screen_config *cfg =
        fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y);
    const enum fbwl_focus_model model =
        cfg != NULL ? cfg->focus.model : server->focus.model;
    if (model != FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS) {
        return;
    }

    double sx = 0, sy = 0;
    struct wlr_surface *surface = NULL;
    struct fbwl_view *after =
        fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (after != before) {
        server_update_pointer_focus(server, FBWL_FOCUS_REASON_POINTER_MOTION, why);
    }
}

void server_raise_view(struct fbwl_view *view, const char *why) {
    if (view == NULL || view->scene_tree == NULL) {
        return;
    }
    struct fbwl_server *server = view->server;
    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wlr_log(WLR_INFO, "Raise: %s reason=%s",
        fbwl_view_display_title(view),
        why != NULL ? why : "(null)");
    server_strict_mousefocus_recheck_after_restack(server, before, why);
}

void server_lower_view(struct fbwl_view *view, const char *why) {
    if (view == NULL || view->scene_tree == NULL) {
        return;
    }
    struct fbwl_server *server = view->server;
    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);
    wlr_scene_node_lower_to_bottom(&view->scene_tree->node);
    wlr_log(WLR_INFO, "Lower: %s reason=%s",
        fbwl_view_display_title(view),
        why != NULL ? why : "(null)");
    server_strict_mousefocus_recheck_after_restack(server, before, why);
}

static void tile_view_half(struct fbwl_server *server, struct fbwl_view *view, bool right) {
    if (server == NULL || view == NULL) {
        return;
    }

    struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);

    if (view->fullscreen) {
        fbwl_view_set_fullscreen(view, false, server->output_layout, &server->outputs,
            server->layer_normal, server->layer_fullscreen, NULL);
    }
    if (view->maximized) {
        fbwl_view_set_maximized(view, false, server->output_layout, &server->outputs);
    }

    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    struct wlr_box box = {0};
    const struct fbwl_screen_config *cfg =
        fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y);
    const bool full_max = cfg != NULL ? cfg->full_maximization : server->full_maximization;
    if (full_max) {
        fbwl_view_get_output_box(view, server->output_layout, output, &box);
    } else {
        fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, output, &box);
    }
    if (box.width < 2 || box.height < 1) {
        return;
    }

    const int left_w = box.width / 2;
    const int frame_x = right ? box.x + left_w : box.x;
    const int frame_y = box.y;
    const int frame_w = right ? (box.width - left_w) : left_w;
    const int frame_h = box.height;

    int x = frame_x;
    int y = frame_y;
    int w = frame_w;
    int h = frame_h;
    int frame_left = 0;
    int frame_top = 0;
    int frame_right = 0;
    int frame_bottom = 0;
    fbwl_view_decor_frame_extents(view, &server->decor_theme, &frame_left, &frame_top, &frame_right, &frame_bottom);
    x += frame_left;
    y += frame_top;
    w -= frame_left + frame_right;
    h -= frame_top + frame_bottom;
    if (w < 1 || h < 1) {
        return;
    }

    view->x = x;
    view->y = y;
    if (view->scene_tree != NULL) {
        wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
    fbwl_view_pseudo_bg_update(view, right ? "rhalf" : "lhalf");

    if (view->type == FBWL_VIEW_XDG) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
    } else if (view->type == FBWL_VIEW_XWAYLAND) {
        wlr_xwayland_surface_configure(view->xwayland_surface, view->x, view->y,
            (uint16_t)w, (uint16_t)h);
    }

    fbwl_tabs_sync_geometry_from_view(view, true, w, h, right ? "rhalf" : "lhalf");
    fbwl_view_foreign_update_output_from_position(view, server->output_layout);
    wlr_log(WLR_INFO, "Tile: %s %s w=%d h=%d",
        fbwl_view_display_title(view),
        right ? "rhalf" : "lhalf",
        w, h);
    server_strict_mousefocus_recheck_after_restack(server, before, right ? "rhalf" : "lhalf");
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
        if (why != NULL && strcmp(why, "showdesktop") == 0) {
            return;
        }
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
    server_grab_update(userdata);
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
static bool server_pointer_focus_allowed(const struct fbwl_server *server) {
    if (server == NULL) {
        return false;
    }
    if (server->scene == NULL || server->cursor == NULL) {
        return false;
    }
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y);
    const enum fbwl_focus_model model =
        cfg != NULL ? cfg->focus.model : server->focus.model;
    if (model == FBWL_FOCUS_MODEL_CLICK_TO_FOCUS) {
        return false;
    }
    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        return false;
    }
    if (server->cmd_dialog_ui.open || server->menu_ui.open) {
        return false;
    }
    return true;
}
static void server_update_pointer_focus(struct fbwl_server *server, enum fbwl_focus_reason reason,
        const char *why) {
    (void)why;
    if (server == NULL || server->scene == NULL || server->cursor == NULL) {
        return;
    }

    double sx = 0, sy = 0;
    struct wlr_surface *surface = NULL;
    struct fbwl_view *view = fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (view != NULL && !fbwl_view_accepts_focus(view)) {
        view = NULL;
    }

    if (server->auto_raise_pending_view != NULL && view != server->auto_raise_pending_view) {
        server->auto_raise_pending_view = NULL;
    }

    if (!server_pointer_focus_allowed(server)) {
        return;
    }

    if (view != NULL) {
        if (view != server->focused_view) {
            const enum fbwl_focus_reason prev_reason = server->focus_reason;
            server->focus_reason = reason;
            fbwm_core_focus_view(&server->wm, &view->wm_view);
            server->focus_reason = prev_reason;
        }
        return;
    }
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
    server_update_pointer_focus(server, FBWL_FOCUS_REASON_POINTER_MOTION, "pointer-motion");
    server_mousebind_capture_handle_motion(server);
    server_toolbar_ui_handle_motion(server);
    server_slit_ui_handle_motion(server);
    server_tooltip_ui_handle_motion(server);
    server_tabs_ui_handle_motion(server);
    if (server->menu_ui.open) {
        fbwl_ui_menu_handle_motion(&server->menu_ui, (int)server->cursor->x, (int)server->cursor->y);
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
    server_update_pointer_focus(server, FBWL_FOCUS_REASON_POINTER_MOTION, "pointer-motion-absolute");
    server_mousebind_capture_handle_motion(server);
    server_toolbar_ui_handle_motion(server);
    server_slit_ui_handle_motion(server);
    server_tooltip_ui_handle_motion(server);
    server_tabs_ui_handle_motion(server);
    if (server->menu_ui.open) {
        fbwl_ui_menu_handle_motion(&server->menu_ui, (int)server->cursor->x, (int)server->cursor->y);
    }
}

void server_cursor_button(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    fbwl_idle_notify_activity(&server->idle);

    if (server->grab.mode != FBWL_CURSOR_PASSTHROUGH) {
        if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
                event->button == server->grab.button) {
            const enum fbwl_cursor_mode mode = server->grab.mode;
            struct fbwl_view *view = server->grab.view;
            fbwl_grab_commit(&server->grab, server->output_layout, "release");

            // Drag-to-tab attach: only when initiated via StartTabbing.
            if (mode == FBWL_CURSOR_MOVE && view != NULL && view->scene_tree != NULL && server->grab.tab_attach_enabled) {
                const bool had_group = view->tab_group != NULL;
                wlr_scene_node_set_enabled(&view->scene_tree->node, false);

                double sx = 0, sy = 0;
                struct wlr_surface *surface = NULL;
                struct fbwl_view *anchor = fbwl_view_at(server->scene, server->cursor->x, server->cursor->y,
                    &surface, &sx, &sy);

                wlr_scene_node_set_enabled(&view->scene_tree->node, true);

                const struct fbwl_screen_config *cfg =
                    fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y);
                const struct fbwl_tabs_config *tabs = cfg != NULL ? &cfg->tabs : &server->tabs;

                bool allow = anchor != NULL && anchor != view;
                if (allow && tabs->attach_area == FBWL_TABS_ATTACH_TITLEBAR) {
                    allow = false;
                    if (surface == NULL) {
                        const struct fbwl_decor_hit hit =
                            fbwl_view_decor_hit_test(anchor, &server->decor_theme, server->cursor->x, server->cursor->y);
                        if (hit.kind == FBWL_DECOR_HIT_TITLEBAR || fbwl_view_tabs_bar_contains(anchor, server->cursor->x, server->cursor->y)) {
                            allow = true;
                        }
                    }
                }

                if (allow) {
                    wlr_log(WLR_INFO, "TabsUI: drag-attach view=%s anchor=%s area=%s",
                        fbwl_view_display_title(view),
                        fbwl_view_display_title(anchor),
                        fbwl_tabs_attach_area_str(tabs->attach_area));

                    if (had_group) {
                        fbwl_tabs_detach(view, "drag");
                    }
                    if (fbwl_tabs_attach(view, anchor, "drag")) {
                        fbwl_tabs_activate(view, "drag-attach");
                    }
                } else if (had_group) {
                    fbwl_tabs_detach(view, "drag");
                }
            }

            if (view != NULL && server->grab.mode == FBWL_CURSOR_MOVE) {
                wlr_log(WLR_INFO, "Move: %s x=%d y=%d",
                    fbwl_view_display_title(view),
                    view->x, view->y);
            }
            if (view != NULL && server->grab.mode == FBWL_CURSOR_RESIZE) {
                wlr_log(WLR_INFO, "Resize: %s w=%d h=%d",
                    fbwl_view_display_title(view),
                    server->grab.last_w, server->grab.last_h);
            }
            fbwl_grab_end(&server->grab);
            fbwl_ui_osd_hide(&server->move_osd_ui, "grab-end");
        }
        return;
    }

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (server->decor_button_pressed_view != NULL &&
                event->button == server->decor_button_pressed_button) {
            struct fbwl_view *view = server->decor_button_pressed_view;
            server->decor_button_pressed_view = NULL;
            server->decor_button_pressed_kind = FBWL_DECOR_HIT_NONE;
            server->decor_button_pressed_button = 0;
            fbwl_view_decor_update(view, &server->decor_theme);
        }
        if (server_mousebind_capture_handle_release(server, event)) {
            return;
        }
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

        if (server_mousebind_capture_handle_press(server, view, surface, event, modifiers)) {
            return;
        }

        if (server_slit_ui_handle_button(server, event)) {
            return;
        }

        if (view == NULL && surface == NULL && event->button == BTN_RIGHT) {
            server_menu_ui_open_root(server, (int)server->cursor->x, (int)server->cursor->y, NULL);
            return;
        }

        if (alt && view != NULL && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
            if (fbwl_view_accepts_focus(view)) {
                const enum fbwl_focus_reason prev_reason = server->focus_reason;
                server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
                fbwm_core_focus_view(&server->wm, &view->wm_view);
                server->focus_reason = prev_reason;
            }
            server_raise_view(view, "alt-grab");

            server->grab.view = view;
            if (event->button == BTN_LEFT) {
                fbwl_grab_begin_move(&server->grab, view, server->cursor, event->button);
            } else {
                fbwl_grab_begin_resize(&server->grab, view, server->cursor, event->button,
                    WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);
            }
            return;
        }

        if (view != NULL && surface == NULL && event->button == BTN_RIGHT) {
            const struct fbwl_decor_hit hit =
                fbwl_view_decor_hit_test(view, &server->decor_theme, server->cursor->x, server->cursor->y);
            if (hit.kind == FBWL_DECOR_HIT_TITLEBAR || fbwl_view_tabs_bar_contains(view, server->cursor->x, server->cursor->y)) {
                if (fbwl_view_accepts_focus(view)) {
                    const enum fbwl_focus_reason prev_reason = server->focus_reason;
                    server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
                    fbwm_core_focus_view(&server->wm, &view->wm_view);
                    server->focus_reason = prev_reason;
                }
                server_raise_view(view, "titlebar-menu");
                server_menu_ui_open_window(server, view, (int)server->cursor->x, (int)server->cursor->y);
                return;
            }
        }

        if (view != NULL && surface == NULL && event->button == BTN_LEFT) {
            int tab_index0 = -1;
            if (fbwl_view_tabs_index_at(view, server->cursor->x, server->cursor->y, &tab_index0) && tab_index0 >= 0) {
                struct fbwl_view *tab_view = fbwl_tabs_group_mapped_at(view, (size_t)tab_index0);
                if (tab_view != NULL) {
                    wlr_log(WLR_INFO, "TabsUI: click idx=%d title=%s", tab_index0, fbwl_view_display_title(tab_view));
                    fbwl_tabs_activate(tab_view, "tab-click");

                    if (fbwl_view_accepts_focus(tab_view)) {
                        const enum fbwl_focus_reason prev_reason = server->focus_reason;
                        server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
                        fbwm_core_focus_view(&server->wm, &tab_view->wm_view);
                        server->focus_reason = prev_reason;
                    }
                    server_raise_view(tab_view, "tab-click");
                }
                return;
            }

            const struct fbwl_decor_hit hit =
                fbwl_view_decor_hit_test(view, &server->decor_theme, server->cursor->x, server->cursor->y);
            if (hit.kind != FBWL_DECOR_HIT_NONE) {
                if (fbwl_view_accepts_focus(view)) {
                    const enum fbwl_focus_reason prev_reason = server->focus_reason;
                    server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
                    fbwm_core_focus_view(&server->wm, &view->wm_view);
                    server->focus_reason = prev_reason;
                }
                server_raise_view(view, "decor");

                if (hit.kind == FBWL_DECOR_HIT_TITLEBAR) {
                    fbwl_grab_begin_move(&server->grab, view, server->cursor, event->button);
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_RESIZE) {
                    fbwl_grab_begin_resize(&server->grab, view, server->cursor, event->button, hit.edges);
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_MENU ||
                        hit.kind == FBWL_DECOR_HIT_BTN_SHADE ||
                        hit.kind == FBWL_DECOR_HIT_BTN_STICK ||
                        hit.kind == FBWL_DECOR_HIT_BTN_CLOSE ||
                        hit.kind == FBWL_DECOR_HIT_BTN_MAX ||
                        hit.kind == FBWL_DECOR_HIT_BTN_MIN ||
                        hit.kind == FBWL_DECOR_HIT_BTN_LHALF ||
                        hit.kind == FBWL_DECOR_HIT_BTN_RHALF) {
                    struct fbwl_view *prev = server->decor_button_pressed_view;
                    server->decor_button_pressed_view = view;
                    server->decor_button_pressed_kind = hit.kind;
                    server->decor_button_pressed_button = event->button;
                    if (prev != NULL && prev != view) {
                        fbwl_view_decor_update(prev, &server->decor_theme);
                    }
                    fbwl_view_decor_update(view, &server->decor_theme);
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_MENU) {
                    server_menu_ui_open_window(server, view, (int)server->cursor->x, (int)server->cursor->y);
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_SHADE) {
                    fbwl_view_set_shaded(view, !view->shaded, "decor-button");
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_STICK) {
                    view->wm_view.sticky = !view->wm_view.sticky;
                    wlr_log(WLR_INFO, "Stick: %s %s", fbwl_view_display_title(view), view->wm_view.sticky ? "on" : "off");
                    apply_workspace_visibility(server, view->wm_view.sticky ? "stick-on" : "stick-off");
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
                if (hit.kind == FBWL_DECOR_HIT_BTN_LHALF) {
                    tile_view_half(server, view, false);
                    return;
                }
                if (hit.kind == FBWL_DECOR_HIT_BTN_RHALF) {
                    tile_view_half(server, view, true);
                    return;
                }
            }
        }

        if (view != NULL) {
            const struct fbwl_screen_config *cfg = fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y);
            const bool click_raises_anywhere = cfg != NULL ? cfg->focus.click_raises : server->focus.click_raises;
            bool click_raises = click_raises_anywhere;
            if (!click_raises_anywhere && surface == NULL) {
                const struct fbwl_decor_hit hit =
                    fbwl_view_decor_hit_test(view, &server->decor_theme, server->cursor->x, server->cursor->y);
                click_raises = hit.kind != FBWL_DECOR_HIT_NONE;
            }

            if (fbwl_view_accepts_focus(view)) {
                const enum fbwl_focus_reason prev_reason = server->focus_reason;
                server->focus_reason = FBWL_FOCUS_REASON_POINTER_CLICK;
                fbwm_core_focus_view(&server->wm, &view->wm_view);
                server->focus_reason = prev_reason;
            }
            if (click_raises) {
                server_raise_view(view, "click");
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

    if (server->menu_ui.open) {
        const int fb_button = server_fluxbox_mouse_button_from_axis(event);
        if (fb_button != 0) {
            if (server_menu_ui_handle_click(server,
                    (int)server->cursor->x, (int)server->cursor->y, (uint32_t)fb_button)) {
                return;
            }
        }
    }

    if (server->grab.mode == FBWL_CURSOR_PASSTHROUGH &&
            !server->cmd_dialog_ui.open) {
        const int fb_button = server_fluxbox_mouse_button_from_axis(event);
        if (fb_button != 0) {
            if (server_toolbar_ui_handle_click(server,
                    (int)server->cursor->x, (int)server->cursor->y, (uint32_t)fb_button)) {
                return;
            }
            double sx = 0, sy = 0;
            struct wlr_surface *surface = NULL;
            struct fbwl_view *view =
                fbwl_view_at(server->scene, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

            struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
            hooks.button = 0;
            hooks.cursor_x = (int)server->cursor->x;
            hooks.cursor_y = (int)server->cursor->y;

            const uint32_t modifiers = server_keyboard_modifiers(server);
            const enum fbwl_mousebinding_context ctx = server_mousebinding_context_at(server, view, surface);
            struct fbwl_view *target =
                (ctx == FBWL_MOUSEBIND_DESKTOP || ctx == FBWL_MOUSEBIND_TOOLBAR || ctx == FBWL_MOUSEBIND_SLIT) ? NULL : view;

            if (fbwl_mousebindings_handle(server->mousebindings, server->mousebinding_count, ctx,
                    FBWL_MOUSEBIND_EVENT_PRESS, fb_button, modifiers, false, target, &hooks)) {
                return;
            }
        }
    }

    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
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
static void keybindings_reconfigure(void *userdata) {
    struct fbwl_server *server = userdata;
    server_reconfigure(server);
}
static void keybindings_key_mode_set(void *userdata, const char *mode) {
    fbwl_server_key_mode_set(userdata, mode);
}
static void keybindings_menu_open_root(void *userdata, int x, int y, const char *menu_file) {
    struct fbwl_server *server = userdata;
    server_menu_ui_open_root(server, x, y, menu_file);
}
static void keybindings_menu_open_workspace(void *userdata, int x, int y) {
    struct fbwl_server *server = userdata;
    server_menu_ui_open_workspace(server, x, y);
}
static void keybindings_menu_open_client(void *userdata, int x, int y, const char *pattern) {
    struct fbwl_server *server = userdata;
    server_menu_ui_open_client(server, x, y, pattern);
}
static void keybindings_menu_open_window(void *userdata, struct fbwl_view *view, int x, int y) {
    struct fbwl_server *server = userdata;
    server_menu_ui_open_window(server, view, x, y);
}
static void keybindings_menu_close(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    server_menu_ui_close(server, why);
}
static void keybindings_apply_workspace_visibility(void *userdata, const char *why) {
    struct fbwl_server *server = userdata;
    apply_workspace_visibility(server, why);
}
static int keybindings_workspace_current(void *userdata, int x, int y) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return 0;
    }
    const size_t head = fbwl_server_screen_index_at(server, x, y);
    return fbwm_core_workspace_current_for_head(&server->wm, head);
}
static void keybindings_workspace_switch(void *userdata, int x, int y, int workspace0, const char *why) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    const size_t head = fbwl_server_screen_index_at(server, x, y);
    server_workspace_switch_on_head(server, head, workspace0, why);
}
static void keybindings_view_close(void *userdata, struct fbwl_view *view, bool force) {
    (void)userdata;
    (void)force;
    if (view == NULL) {
        return;
    }
    if (view->type == FBWL_VIEW_XDG && view->xdg_toplevel != NULL) {
        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
    } else if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        wlr_xwayland_surface_close(view->xwayland_surface);
    }
}
static void keybindings_view_raise(void *userdata, struct fbwl_view *view, const char *why) {
    (void)userdata;
    server_raise_view(view, why);
}
static void keybindings_view_lower(void *userdata, struct fbwl_view *view, const char *why) {
    (void)userdata;
    server_lower_view(view, why);
}
static void keybindings_grab_begin_move(void *userdata, struct fbwl_view *view, uint32_t button) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->cursor == NULL || view == NULL) {
        return;
    }
    fbwl_grab_begin_move(&server->grab, view, server->cursor, button);
}
static void keybindings_grab_begin_tabbing(void *userdata, struct fbwl_view *view, uint32_t button) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->cursor == NULL || view == NULL) return;
    fbwl_grab_begin_tabbing(&server->grab, view, server->cursor, button);
}
static void keybindings_grab_begin_resize(void *userdata, struct fbwl_view *view, uint32_t button, uint32_t edges) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->cursor == NULL || view == NULL) {
        return;
    }
    fbwl_grab_begin_resize(&server->grab, view, server->cursor, button, edges);
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
bool server_refocus_candidate_allowed(void *userdata, const struct fbwm_view *candidate,
        const struct fbwm_view *reference) {
    (void)reference;
    const struct fbwl_server *server = userdata;
    const struct fbwl_view *c = candidate != NULL ? candidate->userdata : NULL;
    if (c != NULL && fbwl_view_is_focus_hidden(c)) {
        return false;
    }
    const struct fbwl_screen_config *cfg =
        server != NULL && server->cursor != NULL ? fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y) : NULL;
    const bool focus_same_head = cfg != NULL ? cfg->focus.focus_same_head :
        (server != NULL ? server->focus.focus_same_head : false);
    if (server == NULL || !focus_same_head) {
        return true;
    }
    if (candidate == NULL) {
        return true;
    }
    if (server->grab.mode == FBWL_CURSOR_MOVE) {
        return true;
    }
    if (server->output_layout == NULL || server->cursor == NULL) {
        return true;
    }
    if (c == NULL) {
        return true;
    }

    struct wlr_output *cur =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    if (cur == NULL) {
        return true;
    }
    struct wlr_output *out =
        wlr_output_layout_output_at(server->output_layout, c->x + 1, c->y + 1);
    return out == cur;
}
static bool keybindings_cycle_view_allowed(void *userdata, const struct fbwl_view *view) {
    const struct fbwl_server *server = userdata;
    if (server == NULL || view == NULL) {
        return true;
    }
    const struct fbwl_screen_config *cfg =
        server->cursor != NULL ? fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y) : NULL;
    const bool focus_same_head = cfg != NULL ? cfg->focus.focus_same_head : server->focus.focus_same_head;
    if (!focus_same_head) {
        return true;
    }
    if (server->grab.mode == FBWL_CURSOR_MOVE) {
        return true;
    }
    if (server->output_layout == NULL || server->cursor == NULL) {
        return true;
    }
    struct wlr_output *cur =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    if (cur == NULL) {
        return true;
    }
    struct wlr_output *out =
        wlr_output_layout_output_at(server->output_layout, view->x + 1, view->y + 1);
    return out == cur;
}
struct fbwl_keybindings_hooks keybindings_hooks(struct fbwl_server *server) {
    return (struct fbwl_keybindings_hooks){
        .userdata = server,
        .cmdlang_scope = NULL,
        .wm = server != NULL ? &server->wm : NULL,
        .key_mode = server != NULL ? server->key_mode : NULL,
        .key_mode_set = keybindings_key_mode_set,
        .terminate = keybindings_terminate, .restart = server_keybindings_restart,
        .spawn = keybindings_spawn,
        .command_dialog_open = keybindings_command_dialog_open,
        .reconfigure = keybindings_reconfigure,
        .apply_workspace_visibility = keybindings_apply_workspace_visibility,
        .workspace_current = keybindings_workspace_current,
        .workspace_switch = keybindings_workspace_switch,
        .view_close = keybindings_view_close,
        .view_set_maximized = keybindings_view_set_maximized,
        .view_toggle_maximize_horizontal = server_keybindings_view_toggle_maximize_horizontal,
        .view_toggle_maximize_vertical = server_keybindings_view_toggle_maximize_vertical,
        .view_set_fullscreen = keybindings_view_set_fullscreen,
        .view_set_minimized = keybindings_view_set_minimized,
        .view_raise = keybindings_view_raise,
        .view_lower = keybindings_view_lower,
        .view_raise_layer = server_keybindings_view_raise_layer,
        .view_lower_layer = server_keybindings_view_lower_layer,
        .view_set_layer = server_keybindings_view_set_layer,
        .view_set_xprop = server_keybindings_view_set_xprop,
        .views_attach_pattern = server_keybindings_views_attach_pattern,
        .show_desktop = server_keybindings_show_desktop,
        .arrange_windows = server_keybindings_arrange_windows,
        .unclutter = server_keybindings_unclutter,
        .menu_open_root = keybindings_menu_open_root,
        .menu_open_workspace = keybindings_menu_open_workspace,
        .menu_open_client = keybindings_menu_open_client,
        .menu_open_window = keybindings_menu_open_window,
        .menu_close = keybindings_menu_close,
        .grab_begin_move = keybindings_grab_begin_move,
        .grab_begin_resize = keybindings_grab_begin_resize,
        .grab_begin_tabbing = keybindings_grab_begin_tabbing,
        .cycle_view_allowed = server != NULL ? keybindings_cycle_view_allowed : NULL,
        .cursor_x = server != NULL && server->cursor != NULL ? (int)server->cursor->x : 0,
        .cursor_y = server != NULL && server->cursor != NULL ? (int)server->cursor->y : 0,
        .button = 0,
    };
}
