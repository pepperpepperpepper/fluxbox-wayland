#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_cursor.h"
#include "wayland/fbwl_seat.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_util.h"
#include "wayland/fbwl_view.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#define FBWL_KEYMOD_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | \
    WLR_MODIFIER_MOD2 | WLR_MODIFIER_MOD3 | WLR_MODIFIER_MOD5)

static void seat_notify_activity(void *userdata) {
    struct fbwl_server *server = userdata;
    fbwl_idle_notify_activity(&server->idle);
}

static bool seat_menu_is_open(void *userdata) {
    struct fbwl_server *server = userdata;
    return server != NULL && server->menu_ui.open;
}

static bool seat_menu_handle_key(void *userdata, xkb_keysym_t sym) {
    struct fbwl_server *server = userdata;
    return server_menu_ui_handle_keypress(server, sym);
}

static bool seat_cmd_dialog_is_open(void *userdata) {
    struct fbwl_server *server = userdata;
    return server != NULL && server->cmd_dialog_ui.open;
}

static bool seat_cmd_dialog_handle_key(void *userdata, xkb_keysym_t sym, uint32_t modifiers) {
    struct fbwl_server *server = userdata;
    return server_cmd_dialog_ui_handle_key(server, sym, modifiers);
}

static int grab_step_px(uint32_t modifiers) {
    int step = 10;
    if ((modifiers & WLR_MODIFIER_CTRL) != 0) {
        step = 1;
    } else if ((modifiers & WLR_MODIFIER_SHIFT) != 0) {
        step = 50;
    }
    if (step < 1) {
        step = 1;
    }
    return step;
}

static bool seat_grab_handle_key(void *userdata, xkb_keysym_t sym, uint32_t modifiers) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return false;
    }
    if (server->grab.mode == FBWL_CURSOR_PASSTHROUGH || server->grab.view == NULL) {
        return false;
    }
    if (server->grab.button != 0) {
        return false;
    }

    if (sym == XKB_KEY_Escape) {
        struct fbwl_view *view = server->grab.view;
        const enum fbwl_cursor_mode mode = server->grab.mode;

        server->grab.pending_valid = true;
        server->grab.pending_x = server->grab.view_x;
        server->grab.pending_y = server->grab.view_y;
        server->grab.pending_w = server->grab.view_w;
        server->grab.pending_h = server->grab.view_h;
        server->grab.last_w = server->grab.pending_w;
        server->grab.last_h = server->grab.pending_h;
        fbwl_grab_commit(&server->grab, server->output_layout, "key-escape");

        if (view != NULL && mode == FBWL_CURSOR_MOVE) {
            wlr_log(WLR_INFO, "Move: cancel %s x=%d y=%d",
                fbwl_view_display_title(view),
                view->x, view->y);
        }
        if (view != NULL && mode == FBWL_CURSOR_RESIZE) {
            wlr_log(WLR_INFO, "Resize: cancel %s w=%d h=%d",
                fbwl_view_display_title(view),
                server->grab.last_w, server->grab.last_h);
        }

        fbwl_grab_end(&server->grab);
        fbwl_ui_osd_hide(&server->move_osd_ui, "grab-cancel");
        server_strict_mousefocus_recheck(server, "grab-cancel");
        return true;
    }

    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        struct fbwl_view *view = server->grab.view;
        const enum fbwl_cursor_mode mode = server->grab.mode;
        fbwl_grab_commit(&server->grab, server->output_layout, "key-enter");

        if (view != NULL && mode == FBWL_CURSOR_MOVE) {
            wlr_log(WLR_INFO, "Move: %s x=%d y=%d",
                fbwl_view_display_title(view),
                view->x, view->y);
        }
        if (view != NULL && mode == FBWL_CURSOR_RESIZE) {
            wlr_log(WLR_INFO, "Resize: %s w=%d h=%d",
                fbwl_view_display_title(view),
                server->grab.last_w, server->grab.last_h);
        }

        fbwl_grab_end(&server->grab);
        fbwl_ui_osd_hide(&server->move_osd_ui, "grab-end");
        server_strict_mousefocus_recheck(server, "grab-end");
        return true;
    }

    int dx = 0;
    int dy = 0;
    const int step = grab_step_px(modifiers);
    switch (sym) {
    case XKB_KEY_Left:
        dx = -step;
        break;
    case XKB_KEY_Right:
        dx = step;
        break;
    case XKB_KEY_Up:
        dy = -step;
        break;
    case XKB_KEY_Down:
        dy = step;
        break;
    default:
        return false;
    }

    if (server->cursor == NULL) {
        return true;
    }

    const int old_x = server->grab.view != NULL ? server->grab.view->x : 0;
    const int old_y = server->grab.view != NULL ? server->grab.view->y : 0;
    const int old_w = server->grab.view != NULL ? fbwl_view_current_width(server->grab.view) : 0;
    const int old_h = server->grab.view != NULL ? fbwl_view_current_height(server->grab.view) : 0;

    server->grab.grab_x -= (double)dx;
    server->grab.grab_y -= (double)dy;
    server_grab_update(server);

    if (server->grab.view != NULL &&
            (server->grab.view->x != old_x || server->grab.view->y != old_y ||
                fbwl_view_current_width(server->grab.view) != old_w ||
                fbwl_view_current_height(server->grab.view) != old_h)) {
        server_strict_mousefocus_recheck(server, "grab-key-step");
    }
    return true;
}

static bool seat_shortcuts_inhibited(void *userdata) {
    struct fbwl_server *server = userdata;
    return server_keyboard_shortcuts_inhibited(server);
}

static bool seat_keysym_is_modifier(xkb_keysym_t sym);

static bool key_mode_is_keychain(const char *mode) {
    static const char prefix[] = "__fbwl_chain__";
    return mode != NULL && strncmp(mode, prefix, sizeof(prefix) - 1) == 0;
}

static void keychain_clear(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }
    free(server->keychain_saved_mode);
    server->keychain_saved_mode = NULL;
    server->keychain_active = false;
    server->keychain_start_time_msec = 0;
}

static void keychain_restore(struct fbwl_server *server, const char *why) {
    if (server == NULL || !server->keychain_active) {
        return;
    }
    wlr_log(WLR_INFO, "Keychain: restore reason=%s", why != NULL ? why : "(null)");
    fbwl_server_key_mode_set(server, server->keychain_saved_mode);
    keychain_clear(server);
}

static bool key_mode_return_matches(const struct fbwl_server *server, uint32_t keycode, xkb_keysym_t sym,
        uint32_t modifiers) {
    if (server == NULL || !server->key_mode_return_active || server->key_mode == NULL) {
        return false;
    }

    modifiers &= FBWL_KEYMOD_MASK;
    if (server->key_mode_return_modifiers != modifiers) {
        return false;
    }

    if (server->key_mode_return_kind == FBWL_KEYBIND_KEYCODE) {
        return server->key_mode_return_keycode == keycode;
    }

    if (server->key_mode_return_kind == FBWL_KEYBIND_KEYSYM) {
        return server->key_mode_return_sym == xkb_keysym_to_lower(sym);
    }

    return false;
}

static bool seat_keybindings_handle(void *userdata, uint32_t keycode, xkb_keysym_t sym, uint32_t modifiers) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return false;
    }

    const uint64_t now = fbwl_now_msec();

    if (server->keychain_active && server->keychain_start_time_msec != 0 &&
            now - server->keychain_start_time_msec > 5000) {
        keychain_restore(server, "timeout");
    }

    if (server->keychain_active && sym == XKB_KEY_Escape) {
        keychain_restore(server, "escape");
        return true;
    }

    if (!server->keychain_active && key_mode_return_matches(server, keycode, sym, modifiers)) {
        fbwl_server_key_mode_set(server, NULL);
        return true;
    }

    const bool in_chain_before = server->keychain_active;
    char *mode_before = server->key_mode != NULL ? strdup(server->key_mode) : NULL;
    const bool mode_before_is_chain = key_mode_is_keychain(mode_before);

    const struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
    const bool handled =
        fbwl_keybindings_handle(server->keybindings, server->keybinding_count, keycode, sym, modifiers, &hooks);

    const bool mode_after_is_chain = key_mode_is_keychain(server->key_mode);

    if (!in_chain_before && mode_after_is_chain) {
        server->keychain_active = true;
        server->keychain_start_time_msec = now;
        free(server->keychain_saved_mode);
        server->keychain_saved_mode = mode_before;
        mode_before = NULL;
        wlr_log(WLR_INFO, "Keychain: start");
    }

    if (in_chain_before) {
        if (handled) {
            if (!mode_after_is_chain) {
                keychain_clear(server);
            } else if (mode_before_is_chain && mode_before != NULL &&
                    server->key_mode != NULL && strcmp(mode_before, server->key_mode) == 0) {
                keychain_restore(server, "complete");
            }
        } else if (!seat_keysym_is_modifier(sym)) {
            keychain_restore(server, "invalid");
        }
    }

    free(mode_before);
    return handled;
}

static bool seat_keysym_is_modifier(xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
    case XKB_KEY_Hyper_L:
    case XKB_KEY_Hyper_R:
    case XKB_KEY_Caps_Lock:
    case XKB_KEY_Num_Lock:
    case XKB_KEY_Scroll_Lock:
    case XKB_KEY_ISO_Level3_Shift:
    case XKB_KEY_ISO_Level5_Shift:
    case XKB_KEY_Mode_switch:
        return true;
    default:
        return false;
    }
}

static void seat_notify_key_to_client(void *userdata, const xkb_keysym_t *syms, size_t nsyms, uint32_t modifiers) {
    struct fbwl_server *server = userdata;
    if (server == NULL || syms == NULL || nsyms == 0) {
        return;
    }

    struct fbwl_view *view = server->focused_view;
    if (view == NULL) {
        return;
    }

    const uint32_t typing_mods = modifiers & (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO);
    if (typing_mods != 0) {
        return;
    }

    bool any_non_modifier = false;
    for (size_t i = 0; i < nsyms; i++) {
        if (syms[i] == XKB_KEY_Return || syms[i] == XKB_KEY_KP_Enter) {
            view->last_typing_time_msec = 0;
            return;
        }
        if (!seat_keysym_is_modifier(syms[i])) {
            any_non_modifier = true;
        }
    }
    if (!any_non_modifier) {
        return;
    }
    view->last_typing_time_msec = fbwl_now_msec();
}

static struct fbwl_seat_keyboard_hooks seat_keyboard_hooks(struct fbwl_server *server) {
    return (struct fbwl_seat_keyboard_hooks){
        .userdata = server,
        .notify_activity = seat_notify_activity,
        .menu_is_open = seat_menu_is_open,
        .menu_handle_key = seat_menu_handle_key,
        .cmd_dialog_is_open = seat_cmd_dialog_is_open,
        .cmd_dialog_handle_key = seat_cmd_dialog_handle_key,
        .grab_handle_key = seat_grab_handle_key,
        .shortcuts_inhibited = seat_shortcuts_inhibited,
        .keybindings_handle = seat_keybindings_handle,
        .notify_key_to_client = seat_notify_key_to_client,
    };
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_cursor);
    fbwl_seat_handle_request_cursor(server->seat, server->cursor, data);
}

void cursor_shape_request_set_shape(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, cursor_shape_request_set_shape);
    fbwl_cursor_handle_shape_request(server != NULL ? server->seat : NULL,
        server != NULL ? server->cursor : NULL,
        server != NULL ? server->cursor_mgr : NULL,
        data);
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_set_selection);
    fbwl_seat_handle_request_set_selection(server->seat, data);
}

void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_set_primary_selection);
    fbwl_seat_handle_request_set_primary_selection(server->seat, data);
}

void seat_request_start_drag(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, request_start_drag);
    fbwl_seat_handle_request_start_drag(server->seat, data);
}

void server_new_input(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    const struct fbwl_seat_keyboard_hooks hooks = seat_keyboard_hooks(server);
    fbwl_seat_add_input_device(server->seat, server->cursor, &server->keyboards, &server->has_pointer, device, &hooks);
}

void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *vkbd = data;
    wlr_log(WLR_INFO, "New virtual keyboard");
    const struct fbwl_seat_keyboard_hooks hooks = seat_keyboard_hooks(server);
    fbwl_seat_add_input_device(server->seat, server->cursor, &server->keyboards, &server->has_pointer,
        &vkbd->keyboard.base, &hooks);
}

void server_new_virtual_pointer(struct wl_listener *listener, void *data) {
    struct fbwl_server *server = wl_container_of(listener, server, new_virtual_pointer);
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    wlr_log(WLR_INFO, "New virtual pointer");
    const struct fbwl_seat_keyboard_hooks hooks = seat_keyboard_hooks(server);
    fbwl_seat_add_input_device(server->seat, server->cursor, &server->keyboards, &server->has_pointer,
        &event->new_pointer->pointer.base, &hooks);
}
