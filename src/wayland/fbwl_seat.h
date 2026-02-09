#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-server-core.h>

#include <xkbcommon/xkbcommon.h>

struct wlr_cursor;
struct wlr_input_device;
struct wlr_seat;
struct wlr_seat_pointer_request_set_cursor_event;
struct wlr_seat_request_set_primary_selection_event;
struct wlr_seat_request_set_selection_event;
struct wlr_seat_request_start_drag_event;

struct fbwl_seat_keyboard_hooks {
    void *userdata;
    void (*notify_activity)(void *userdata);

    bool (*menu_is_open)(void *userdata);
    bool (*menu_handle_key)(void *userdata, xkb_keysym_t sym);

    bool (*cmd_dialog_is_open)(void *userdata);
    bool (*cmd_dialog_handle_key)(void *userdata, xkb_keysym_t sym, uint32_t modifiers);

    bool (*grab_handle_key)(void *userdata, xkb_keysym_t sym, uint32_t modifiers);

    bool (*shortcuts_inhibited)(void *userdata);
    bool (*keybindings_handle)(void *userdata, uint32_t keycode, xkb_keysym_t sym, uint32_t modifiers);

    void (*notify_key_to_client)(void *userdata, const xkb_keysym_t *syms, size_t nsyms, uint32_t modifiers);
};

void fbwl_seat_add_input_device(struct wlr_seat *seat, struct wlr_cursor *cursor,
        struct wl_list *keyboards, bool *has_pointer,
        struct wlr_input_device *device,
        const struct fbwl_seat_keyboard_hooks *keyboard_hooks);

void fbwl_seat_handle_request_cursor(struct wlr_seat *seat, struct wlr_cursor *cursor,
        const struct wlr_seat_pointer_request_set_cursor_event *event);

void fbwl_seat_handle_request_set_selection(struct wlr_seat *seat,
        const struct wlr_seat_request_set_selection_event *event);

void fbwl_seat_handle_request_set_primary_selection(struct wlr_seat *seat,
        const struct wlr_seat_request_set_primary_selection_event *event);

void fbwl_seat_handle_request_start_drag(struct wlr_seat *seat,
        struct wlr_seat_request_start_drag_event *event);
