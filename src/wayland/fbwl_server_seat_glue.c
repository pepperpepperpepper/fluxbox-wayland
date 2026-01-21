#include <stdbool.h>
#include <stdint.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_cursor.h"
#include "wayland/fbwl_seat.h"
#include "wayland/fbwl_server_internal.h"

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

static bool seat_shortcuts_inhibited(void *userdata) {
    struct fbwl_server *server = userdata;
    return server_keyboard_shortcuts_inhibited(server);
}

static bool seat_keybindings_handle(void *userdata, xkb_keysym_t sym, uint32_t modifiers) {
    struct fbwl_server *server = userdata;
    const struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
    return fbwl_keybindings_handle(server->keybindings, server->keybinding_count, sym, modifiers, &hooks);
}

static struct fbwl_seat_keyboard_hooks seat_keyboard_hooks(struct fbwl_server *server) {
    return (struct fbwl_seat_keyboard_hooks){
        .userdata = server,
        .notify_activity = seat_notify_activity,
        .menu_is_open = seat_menu_is_open,
        .menu_handle_key = seat_menu_handle_key,
        .cmd_dialog_is_open = seat_cmd_dialog_is_open,
        .cmd_dialog_handle_key = seat_cmd_dialog_handle_key,
        .shortcuts_inhibited = seat_shortcuts_inhibited,
        .keybindings_handle = seat_keybindings_handle,
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

