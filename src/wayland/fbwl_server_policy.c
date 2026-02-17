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
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_apps_remember.h"
#include "wayland/fbwl_cursor.h"
#include "wayland/fbwl_deco_mask.h"
#include "wayland/fbwl_output.h"
#include "wayland/fbwl_screen_map.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_tabs.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"
#include "wayland/fbwl_view_attention.h"

static bool output_layout_extents(const struct fbwl_server *server, struct wlr_box *out) {
    if (out == NULL) {
        return false;
    }
    *out = (struct wlr_box){0};
    if (server == NULL || server->output_layout == NULL) {
        return false;
    }

    bool have = false;
    struct fbwl_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output == NULL || output->wlr_output == NULL) {
            continue;
        }
        struct wlr_box box = {0};
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
        if (box.width < 1 || box.height < 1) {
            continue;
        }
        if (!have) {
            *out = box;
            have = true;
            continue;
        }
        int x1 = box.x;
        int y1 = box.y;
        int x2 = box.x + box.width;
        int y2 = box.y + box.height;
        int ox1 = out->x;
        int oy1 = out->y;
        int ox2 = out->x + out->width;
        int oy2 = out->y + out->height;
        if (x1 < ox1) {
            ox1 = x1;
        }
        if (y1 < oy1) {
            oy1 = y1;
        }
        if (x2 > ox2) {
            ox2 = x2;
        }
        if (y2 > oy2) {
            oy2 = y2;
        }
        out->x = ox1;
        out->y = oy1;
        out->width = ox2 - ox1;
        out->height = oy2 - oy1;
    }

    return have;
}

void server_grab_update(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    int moved_x = 0;
    int moved_y = 0;
    if (server->cursor != NULL) {
        const int cx = (int)server->cursor->x;
        const int cy = (int)server->cursor->y;
        if (server->grab.last_cursor_valid) {
            moved_x = cx - server->grab.last_cursor_x;
            moved_y = cy - server->grab.last_cursor_y;
        }
        server->grab.last_cursor_valid = true;
        server->grab.last_cursor_x = cx;
        server->grab.last_cursor_y = cy;
    }

    const struct fbwl_screen_config *cfg = NULL;
    if (server->grab.view != NULL) {
        cfg = fbwl_server_screen_config_for_view(server, server->grab.view);
    } else if (server->cursor != NULL) {
        cfg = fbwl_server_screen_config_at(server, server->cursor->x, server->cursor->y);
    }

    const int edge_snap_px = cfg != NULL ? cfg->edge_snap_threshold_px : server->edge_snap_threshold_px;
    const int edge_resize_snap_px =
        cfg != NULL ? cfg->edge_resize_snap_threshold_px : server->edge_resize_snap_threshold_px;
    const bool opaque_move = cfg != NULL ? cfg->opaque_move : server->opaque_move;
    const bool opaque_resize = cfg != NULL ? cfg->opaque_resize : server->opaque_resize;
    const int opaque_resize_delay_ms =
        cfg != NULL ? cfg->opaque_resize_delay_ms : server->opaque_resize_delay_ms;
    const bool workspace_warping = cfg != NULL ? cfg->workspace_warping : server->workspace_warping;
    const bool workspace_warping_horizontal =
        cfg != NULL ? cfg->workspace_warping_horizontal : server->workspace_warping_horizontal;
    const bool workspace_warping_vertical =
        cfg != NULL ? cfg->workspace_warping_vertical : server->workspace_warping_vertical;
    const int workspace_warping_horizontal_offset =
        cfg != NULL ? cfg->workspace_warping_horizontal_offset : server->workspace_warping_horizontal_offset;
    const int workspace_warping_vertical_offset =
        cfg != NULL ? cfg->workspace_warping_vertical_offset : server->workspace_warping_vertical_offset;
    const bool show_window_position = cfg != NULL ? cfg->show_window_position : server->show_window_position;

    fbwl_grab_update(&server->grab, server->cursor, server->output_layout, &server->outputs,
        edge_snap_px, edge_resize_snap_px,
        opaque_move, opaque_resize, opaque_resize_delay_ms);

    if (server->grab.mode == FBWL_CURSOR_MOVE &&
            workspace_warping &&
            (moved_x != 0 || moved_y != 0)) {
        const int ws_count = fbwm_core_workspace_count(&server->wm);
        if (ws_count > 1 && server->cursor != NULL) {
            struct wlr_box layout = {0};
            if (output_layout_extents(server, &layout) && layout.width > 0 && layout.height > 0) {
                const int cx = (int)server->cursor->x;
                const int cy = (int)server->cursor->y;
                const size_t head = fbwl_server_screen_index_at(server, cx, cy);
                const int cur_ws = fbwm_core_workspace_current_for_head(&server->wm, head);
                int new_ws = cur_ws;
                int warp_pad = edge_snap_px;
                if (warp_pad < 0) {
                    warp_pad = 0;
                }

                const int right_edge = layout.x + layout.width - warp_pad - 1;
                const int left_edge = layout.x + warp_pad;
                const int bottom_edge = layout.y + layout.height - warp_pad - 1;
                const int top_edge = layout.y + warp_pad;

                int warp_x = cx;
                int warp_y = cy;

                if (moved_x != 0 && workspace_warping_horizontal) {
                    int off = workspace_warping_horizontal_offset;
                    if (off < 1) {
                        off = 1;
                    }
                    off %= ws_count;
                    if (off < 1) {
                        off = 1;
                    }

                    if (cx >= right_edge && moved_x > 0) {
                        new_ws = (cur_ws + off) % ws_count;
                        warp_x = layout.x;
                    } else if (cx <= left_edge && moved_x < 0) {
                        new_ws = (cur_ws + ws_count - off) % ws_count;
                        warp_x = layout.x + layout.width - 1;
                    }
                }

                if (moved_y != 0 && workspace_warping_vertical) {
                    int off = workspace_warping_vertical_offset;
                    if (off < 1) {
                        off = 1;
                    }
                    off %= ws_count;
                    if (off < 1) {
                        off = 1;
                    }

                    if (cy >= bottom_edge && moved_y > 0) {
                        new_ws = (cur_ws + off) % ws_count;
                        warp_y = layout.y;
                    } else if (cy <= top_edge && moved_y < 0) {
                        new_ws = (cur_ws + ws_count - off) % ws_count;
                        warp_y = layout.y + layout.height - 1;
                    }
                }

                if (new_ws != cur_ws) {
                    struct fbwl_view *view = server->grab.view;
                    if (view != NULL && opaque_move) {
                        if (server->wm.focused != &view->wm_view) {
                            fbwm_core_focus_view(&server->wm, &view->wm_view);
                        }
                        fbwm_core_move_focused_to_workspace(&server->wm, new_ws);
                    }
                    server_workspace_switch_on_head(server, head, new_ws, "warp");

                    if (warp_x < layout.x) {
                        warp_x = layout.x;
                    }
                    if (warp_x > layout.x + layout.width - 1) {
                        warp_x = layout.x + layout.width - 1;
                    }
                    if (warp_y < layout.y) {
                        warp_y = layout.y;
                    }
                    if (warp_y > layout.y + layout.height - 1) {
                        warp_y = layout.y + layout.height - 1;
                    }

                    if (warp_x != cx || warp_y != cy) {
                        (void)wlr_cursor_warp(server->cursor, NULL, warp_x, warp_y);
                        server->pointer_constraints.phys_valid = true;
                        server->pointer_constraints.phys_x = server->cursor->x;
                        server->pointer_constraints.phys_y = server->cursor->y;
                    }

                    server->grab.grab_x = server->cursor->x;
                    server->grab.grab_y = server->cursor->y;
                    if (view != NULL) {
                        server->grab.view_x = opaque_move ? view->x : server->grab.pending_x;
                        server->grab.view_y = opaque_move ? view->y : server->grab.pending_y;
                    }
                    server->grab.last_cursor_valid = true;
                    server->grab.last_cursor_x = (int)server->cursor->x;
                    server->grab.last_cursor_y = (int)server->cursor->y;
                }
            }
        }
    }

    if (show_window_position && server->scene != NULL && server->grab.view != NULL) {
        struct fbwl_view *view = server->grab.view;
        if (server->grab.mode == FBWL_CURSOR_MOVE) {
            int pos_x = server->grab.pending_x;
            int pos_y = server->grab.pending_y;
            int frame_left = 0;
            int frame_top = 0;
            fbwl_view_decor_frame_extents(view, &server->decor_theme, &frame_left, &frame_top, NULL, NULL);
            pos_x -= frame_left;
            pos_y -= frame_top;
            fbwl_ui_osd_show_window_position(&server->move_osd_ui, server->scene, server->layer_top,
                &server->decor_theme, server->output_layout, pos_x, pos_y);
        } else if (server->grab.mode == FBWL_CURSOR_RESIZE) {
            fbwl_ui_osd_show_window_geometry(&server->move_osd_ui, server->scene, server->layer_top,
                &server->decor_theme, server->output_layout, server->grab.pending_w, server->grab.pending_h);
        }
    } else if (server->move_osd_ui.visible) {
        fbwl_ui_osd_hide(&server->move_osd_ui, show_window_position ? "no-view" : "disabled");
    }
}


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

bool server_focus_request_allowed(struct fbwl_server *server, struct fbwl_view *view, enum fbwl_focus_reason reason,
        const char *why) {
    if (server == NULL || view == NULL) {
        return false;
    }
    if (reason != FBWL_FOCUS_REASON_MAP && reason != FBWL_FOCUS_REASON_ACTIVATE) {
        return true;
    }

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const int typing_delay_ms =
        cfg != NULL ? cfg->focus.no_focus_while_typing_delay_ms : server->focus.no_focus_while_typing_delay_ms;
    const int attention_timeout_ms =
        cfg != NULL ? cfg->focus.demands_attention_timeout_ms : server->focus.demands_attention_timeout_ms;

    const struct fbwl_view *cur = server->focused_view;

    if (cur != NULL && cur != view && (cur->focus_protection & FBWL_APPS_FOCUS_PROTECT_LOCK)) {
        wlr_log(WLR_INFO, "Focus: blocked by lock target=%s", fbwl_view_display_title(view));
        fbwl_view_attention_request(view, attention_timeout_ms, &server->decor_theme,
            why != NULL ? why : "lock");
        return false;
    }
    if (view->focus_protection & FBWL_APPS_FOCUS_PROTECT_DENY) {
        wlr_log(WLR_INFO, "Focus: blocked by deny target=%s", fbwl_view_display_title(view));
        fbwl_view_attention_request(view, attention_timeout_ms, &server->decor_theme,
            why != NULL ? why : "deny");
        return false;
    }

    if (cur != NULL && cur != view && typing_delay_ms > 0) {
        const bool same_group = cur->tab_group != NULL && cur->tab_group == view->tab_group;
        if (!same_group) {
            const uint64_t now = fbwl_now_msec();
            if (cur->last_typing_time_msec != 0 &&
                    now - cur->last_typing_time_msec < (uint64_t)typing_delay_ms) {
                wlr_log(WLR_INFO, "Focus: blocked by typing delay target=%s delay=%d",
                    fbwl_view_display_title(view),
                    typing_delay_ms);
                fbwl_view_attention_request(view, attention_timeout_ms, &server->decor_theme,
                    why != NULL ? why : "typing-delay");
                return false;
            }
        }
    }

    return true;
}

void focus_view(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;
    if (server != NULL && fbwl_session_lock_is_locked(&server->session_lock)) {
        return;
    }

    fbwl_tabs_activate(view, "focus");
    struct wlr_seat *seat = server->seat;

    struct wlr_surface *surface = fbwl_view_wlr_surface(view);
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    if (server->focused_view == view && prev_surface == surface) {
        return;
    }

    fbwl_view_attention_clear(view, &server->decor_theme, "focus");

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

    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool auto_raise = cfg != NULL ? cfg->focus.auto_raise : server->focus.auto_raise;
    const int auto_raise_delay_ms = cfg != NULL ? cfg->focus.auto_raise_delay_ms : server->focus.auto_raise_delay_ms;
    if (!auto_raise) {
        server->auto_raise_pending_view = NULL;
    } else if (server->focus_reason == FBWL_FOCUS_REASON_POINTER_MOTION && auto_raise_delay_ms > 0) {
        server->auto_raise_pending_view = view;
        if (server->auto_raise_timer != NULL) {
            wl_event_source_timer_update(server->auto_raise_timer, auto_raise_delay_ms);
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
    const struct fbwl_screen_config *cfg = fbwl_server_screen_config_for_view(server, view);
    const bool auto_raise = cfg != NULL ? cfg->focus.auto_raise : server->focus.auto_raise;
    if (view != NULL && auto_raise && server->focused_view == view) {
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

static struct wlr_scene_tree *apps_rule_layer_tree(struct fbwl_server *server, int layer) {
    if (server == NULL) {
        return NULL;
    }

    struct wlr_scene_tree *fallback = server->layer_normal != NULL ? server->layer_normal : &server->scene->tree;

    if (layer <= 0) {
        return server->layer_overlay != NULL ? server->layer_overlay : fallback;
    }
    if (layer <= 6) {
        return server->layer_top != NULL ? server->layer_top : fallback;
    }
    if (layer <= 8) {
        return server->layer_normal != NULL ? server->layer_normal : fallback;
    }
    if (layer <= 10) {
        return server->layer_bottom != NULL ? server->layer_bottom : fallback;
    }
    return server->layer_background != NULL ? server->layer_background : fallback;
}

static struct fbwl_view *apps_group_find_anchor(struct fbwl_server *server, const struct fbwl_view *self,
        int group_id) {
    if (server == NULL || self == NULL || group_id <= 0) {
        return NULL;
    }

    for (struct fbwm_view *wm_view = server->wm.views.next;
            wm_view != NULL && wm_view != &server->wm.views;
            wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || view == self) {
            continue;
        }
        if (!view->mapped || view->minimized) {
            continue;
        }
        if (view->apps_group_id != group_id) {
            continue;
        }
        return view;
    }

    return NULL;
}

void server_apps_rules_apply_pre_map(struct fbwl_view *view,
        const struct fbwl_apps_rule *rule) {
    if (view == NULL || view->server == NULL || rule == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;

    if (rule->set_focus_hidden) {
        view->focus_hidden_override_set = true;
        view->focus_hidden_override = rule->focus_hidden;
    }
    if (rule->set_icon_hidden) {
        view->icon_hidden_override_set = true;
        view->icon_hidden_override = rule->icon_hidden;
    }

    if (rule->group_id > 0) {
        view->apps_group_id = rule->group_id;
        if (view->tab_group == NULL) {
            struct fbwl_view *anchor = apps_group_find_anchor(server, view, rule->group_id);
            if (anchor != NULL) {
                (void)fbwl_tabs_attach(view, anchor, "apps-group");
            }
        }
    }

    if (rule->set_decor) {
        view->decor_forced = true;
        view->decor_mask = rule->decor_mask;
        fbwl_view_decor_set_enabled(view, fbwl_deco_mask_has_frame(rule->decor_mask));
        fbwl_view_decor_update(view, &server->decor_theme);
    }

    if (rule->set_alpha) {
        fbwl_view_set_alpha(view, (uint8_t)rule->alpha_focused, (uint8_t)rule->alpha_unfocused, "apps");
        view->alpha_is_default = false;
    }
    if (rule->set_focus_protection) { view->focus_protection = rule->focus_protection; }

    if (rule->set_layer) {
        struct wlr_scene_tree *layer = apps_rule_layer_tree(server, rule->layer);
        if (layer != NULL) {
            view->base_layer = layer;
            if (!view->fullscreen && view->scene_tree != NULL) {
                wlr_scene_node_reparent(&view->scene_tree->node, layer);
            }
        }
    }

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
            size_t head = 0;
            if (rule->set_head && rule->head >= 0) {
                head = (size_t)rule->head;
            } else if (server->cursor != NULL) {
                head = fbwl_server_screen_index_at(server, server->cursor->x, server->cursor->y);
            }
            server_workspace_switch_on_head(server, head, rule->workspace, "apps-jump");
        }
    }

    fbwl_apps_remember_apply_pre_map(view, rule);
}

void server_apps_rules_apply_post_map(struct fbwl_view *view,
        const struct fbwl_apps_rule *rule) {
    if (view == NULL || rule == NULL) {
        return;
    }

    if (rule->set_maximized) {
        fbwl_view_set_maximized_axes(view, rule->maximized_h, rule->maximized_v, view->server->output_layout, &view->server->outputs);
    }
    if (rule->set_fullscreen) {
        fbwl_view_set_fullscreen(view, rule->fullscreen, view->server->output_layout, &view->server->outputs,
            view->server->layer_normal, view->server->layer_fullscreen, NULL);
    }
    if (rule->set_shaded) {
        fbwl_view_set_shaded(view, rule->shaded, "apps");
    }
    if (rule->set_minimized) {
        view_set_minimized(view, rule->minimized, "apps");
    }
}

static bool apps_anchor_is_right(enum fbwl_apps_rule_anchor anchor) {
    switch (anchor) {
    case FBWL_APPS_ANCHOR_TOPRIGHT:
    case FBWL_APPS_ANCHOR_RIGHT:
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        return true;
    default:
        return false;
    }
}

static bool apps_anchor_is_bottom(enum fbwl_apps_rule_anchor anchor) {
    switch (anchor) {
    case FBWL_APPS_ANCHOR_BOTTOMLEFT:
    case FBWL_APPS_ANCHOR_BOTTOM:
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        return true;
    default:
        return false;
    }
}

static void apps_anchor_screen_ref(const struct wlr_box *box, enum fbwl_apps_rule_anchor anchor,
        int *out_x, int *out_y) {
    if (out_x != NULL) {
        *out_x = 0;
    }
    if (out_y != NULL) {
        *out_y = 0;
    }
    if (box == NULL || out_x == NULL || out_y == NULL) {
        return;
    }

    const int left = box->x;
    const int right = box->x + box->width;
    const int top = box->y;
    const int bottom = box->y + box->height;
    const int cx = box->x + box->width / 2;
    const int cy = box->y + box->height / 2;

    switch (anchor) {
    case FBWL_APPS_ANCHOR_TOPLEFT:
        *out_x = left;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_LEFT:
        *out_x = left;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOMLEFT:
        *out_x = left;
        *out_y = bottom;
        return;
    case FBWL_APPS_ANCHOR_TOP:
        *out_x = cx;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_CENTER:
        *out_x = cx;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOM:
        *out_x = cx;
        *out_y = bottom;
        return;
    case FBWL_APPS_ANCHOR_TOPRIGHT:
        *out_x = right;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_RIGHT:
        *out_x = right;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        *out_x = right;
        *out_y = bottom;
        return;
    default:
        *out_x = left;
        *out_y = top;
        return;
    }
}

static void apps_anchor_window_ref(int w, int h, enum fbwl_apps_rule_anchor anchor, int *out_x, int *out_y) {
    if (out_x != NULL) {
        *out_x = 0;
    }
    if (out_y != NULL) {
        *out_y = 0;
    }
    if (out_x == NULL || out_y == NULL) {
        return;
    }

    const int left = 0;
    const int right = w;
    const int top = 0;
    const int bottom = h;
    const int cx = w / 2;
    const int cy = h / 2;

    switch (anchor) {
    case FBWL_APPS_ANCHOR_TOPLEFT:
        *out_x = left;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_LEFT:
        *out_x = left;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOMLEFT:
        *out_x = left;
        *out_y = bottom;
        return;
    case FBWL_APPS_ANCHOR_TOP:
        *out_x = cx;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_CENTER:
        *out_x = cx;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOM:
        *out_x = cx;
        *out_y = bottom;
        return;
    case FBWL_APPS_ANCHOR_TOPRIGHT:
        *out_x = right;
        *out_y = top;
        return;
    case FBWL_APPS_ANCHOR_RIGHT:
        *out_x = right;
        *out_y = cy;
        return;
    case FBWL_APPS_ANCHOR_BOTTOMRIGHT:
        *out_x = right;
        *out_y = bottom;
        return;
    default:
        *out_x = left;
        *out_y = top;
        return;
    }
}

static int percent_from_pixels(int px, int base) {
    if (base <= 0) {
        return 0;
    }

    const long num = (long)px * 100L;
    if (num >= 0) {
        return (int)((num + base / 2) / base);
    }
    return (int)((num - base / 2) / base);
}

void server_apps_rules_save_on_close(struct fbwl_view *view) {
    if (view == NULL || view->server == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;
    if (server->apps_file == NULL || *server->apps_file == '\0') {
        return;
    }
    if (!server->apps_rules_rewrite_safe) {
        wlr_log(WLR_INFO, "Apps: save-on-close skipped (apps file not rewrite-safe): %s",
            server->apps_file);
        return;
    }
    if (server->apps_rules == NULL || server->apps_rule_count == 0) {
        return;
    }

    size_t rule_idx = 0;
    struct fbwl_apps_rule *rule = NULL;

    if (view->apps_rule_index_valid &&
            view->apps_rules_generation == server->apps_rules_generation &&
            view->apps_rule_index < server->apps_rule_count) {
        rule_idx = view->apps_rule_index;
        rule = &server->apps_rules[rule_idx];
    } else {
        const char *app_id = fbwl_view_app_id(view);
        const char *instance = NULL;
        if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
            instance = view->xwayland_surface->instance;
        } else {
            instance = app_id;
        }
        const char *title = fbwl_view_title(view);
        const char *role = fbwl_view_role(view);
        const struct fbwl_apps_rule *matched = fbwl_apps_rules_match(server->apps_rules, server->apps_rule_count,
            app_id, instance, title, role, &rule_idx);
        if (matched == NULL || rule_idx >= server->apps_rule_count) {
            return;
        }
        rule = &server->apps_rules[rule_idx];
    }

    if (rule == NULL || !rule->set_save_on_close || !rule->save_on_close) {
        return;
    }

    int gx = view->x;
    int gy = view->y;
    int gw = fbwl_view_current_width(view);
    int gh = fbwl_view_current_height(view);
    if ((view->fullscreen || view->maximized || view->maximized_h || view->maximized_v) &&
            view->saved_w > 0 && view->saved_h > 0) {
        gx = view->saved_x;
        gy = view->saved_y;
        gw = view->saved_w;
        gh = view->saved_h;
    }

    if (gw < 1 || gh < 1) {
        return;
    }

    struct wlr_output *out = NULL;
    if (server->output_layout != NULL) {
        const double cx = (double)gx + (double)gw / 2.0;
        const double cy = (double)gy + (double)gh / 2.0;
        out = wlr_output_layout_output_at(server->output_layout, cx, cy);
        if (out == NULL) {
            out = wlr_output_layout_get_center_output(server->output_layout);
        }
    }

    struct wlr_box screen = {0};
    if (server->output_layout != NULL) {
        fbwl_view_get_output_usable_box(view, server->output_layout, &server->outputs, out, &screen);
        if (screen.width < 1 || screen.height < 1) {
            fbwl_view_get_output_box(view, server->output_layout, out, &screen);
        }
    }

    if (rule->set_workspace) {
        rule->workspace = view->wm_view.workspace;
    }
    if (rule->set_sticky) {
        rule->sticky = view->wm_view.sticky;
    }
    if (rule->set_focus_hidden && view->focus_hidden_override_set) {
        rule->focus_hidden = view->focus_hidden_override;
    }
    if (rule->set_icon_hidden && view->icon_hidden_override_set) {
        rule->icon_hidden = view->icon_hidden_override;
    }
    if (rule->set_minimized) {
        rule->minimized = view->minimized;
    }
    if (rule->set_maximized) {
        rule->maximized_h = view->maximized_h; rule->maximized_v = view->maximized_v;
    }
    if (rule->set_fullscreen) {
        rule->fullscreen = view->fullscreen;
    }
    if (rule->set_shaded) {
        rule->shaded = view->shaded;
    }
    if (rule->set_alpha) {
        rule->alpha_focused = view->alpha_focused;
        rule->alpha_unfocused = view->alpha_unfocused;
    }
    if (rule->set_focus_protection) {
        rule->focus_protection = view->focus_protection;
    }
    if (rule->set_decor) {
        rule->decor_mask = view->decor_enabled ? view->decor_mask : FBWL_DECOR_NONE;
    }

    if (rule->set_head && out != NULL && server->output_layout != NULL) {
        bool found = false;
        size_t head = fbwl_screen_map_screen_for_output(server->output_layout, &server->outputs, out, &found);
        if (found) {
            rule->head = (int)head;
        }
    }

    if (screen.width > 0 && screen.height > 0) {
        if (rule->set_dimensions) {
            if (rule->width_percent) {
                rule->width = percent_from_pixels(gw, screen.width);
            } else {
                rule->width = gw;
            }
            if (rule->height_percent) {
                rule->height = percent_from_pixels(gh, screen.height);
            } else {
                rule->height = gh;
            }
        }

        if (rule->set_position) {
            int frame_left = 0;
            int frame_top = 0;
            int frame_right = 0;
            int frame_bottom = 0;
            fbwl_view_decor_frame_extents(view, &server->decor_theme, &frame_left, &frame_top, &frame_right, &frame_bottom);

            const int frame_w = gw + frame_left + frame_right;
            const int frame_h = gh + frame_top + frame_bottom;

            const int frame_x = gx - frame_left;
            const int frame_y = gy - frame_top;

            const enum fbwl_apps_rule_anchor anchor = rule->position_anchor;
            int wx = 0;
            int wy = 0;
            apps_anchor_window_ref(frame_w, frame_h, anchor, &wx, &wy);
            int sx = 0;
            int sy = 0;
            apps_anchor_screen_ref(&screen, anchor, &sx, &sy);

            const int x_off_px = frame_x + wx - sx;
            const int y_off_px = frame_y + wy - sy;

            const int store_x_px = apps_anchor_is_right(anchor) ? -x_off_px : x_off_px;
            const int store_y_px = apps_anchor_is_bottom(anchor) ? -y_off_px : y_off_px;

            if (rule->x_percent) {
                rule->x = percent_from_pixels(store_x_px, screen.width);
            } else {
                rule->x = store_x_px;
            }

            if (rule->y_percent) {
                rule->y = percent_from_pixels(store_y_px, screen.height);
            } else {
                rule->y = store_y_px;
            }
        }
    }

    if (fbwl_apps_rules_save_file(server->apps_rules, server->apps_rule_count, server->apps_file)) {
        wlr_log(WLR_INFO, "Apps: save-on-close wrote %s rule=%zu title=%s app_id=%s",
            server->apps_file,
            rule_idx,
            fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
            fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(no-app-id)");
    } else {
        wlr_log(WLR_ERROR, "Apps: save-on-close failed to write %s", server->apps_file);
    }
}
