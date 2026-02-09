#include "wayland/fbwl_seat.h"

#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_util.h"

struct fbwl_keyboard {
    struct wl_list link;
    struct wlr_seat *seat;
    struct wlr_keyboard *wlr_keyboard;
    struct fbwl_seat_keyboard_hooks hooks;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    if (keyboard == NULL) {
        return;
    }

    if (keyboard->hooks.notify_activity != NULL) {
        keyboard->hooks.notify_activity(keyboard->hooks.userdata);
    }
    if (keyboard->seat == NULL || keyboard->wlr_keyboard == NULL) {
        return;
    }

    wlr_seat_set_keyboard(keyboard->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->seat, &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct fbwl_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = data;
    if (keyboard == NULL || event == NULL || keyboard->seat == NULL || keyboard->wlr_keyboard == NULL) {
        return;
    }

    if (keyboard->hooks.notify_activity != NULL) {
        keyboard->hooks.notify_activity(keyboard->hooks.userdata);
    }

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
        keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
            keyboard->hooks.menu_is_open != NULL &&
            keyboard->hooks.menu_is_open(keyboard->hooks.userdata) &&
            keyboard->hooks.menu_handle_key != NULL) {
        for (int i = 0; i < nsyms; i++) {
            if (keyboard->hooks.menu_handle_key(keyboard->hooks.userdata, syms[i])) {
                handled = true;
                break;
            }
        }
    }

    if (!handled && keyboard->hooks.cmd_dialog_is_open != NULL &&
            keyboard->hooks.cmd_dialog_is_open(keyboard->hooks.userdata)) {
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
                keyboard->hooks.cmd_dialog_handle_key != NULL) {
            for (int i = 0; i < nsyms; i++) {
                if (keyboard->hooks.cmd_dialog_handle_key(keyboard->hooks.userdata, syms[i], modifiers)) {
                    break;
                }
            }
        }
        handled = true;
    }

    if (!handled && event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
            keyboard->hooks.grab_handle_key != NULL) {
        for (int i = 0; i < nsyms; i++) {
            if (keyboard->hooks.grab_handle_key(keyboard->hooks.userdata, syms[i], modifiers)) {
                handled = true;
                break;
            }
        }
    }

    if (!handled &&
            (keyboard->hooks.shortcuts_inhibited == NULL ||
                !keyboard->hooks.shortcuts_inhibited(keyboard->hooks.userdata)) &&
            event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
            keyboard->hooks.keybindings_handle != NULL) {
        for (int i = 0; i < nsyms; i++) {
            if (keyboard->hooks.keybindings_handle(keyboard->hooks.userdata, keycode, syms[i], modifiers)) {
                handled = true;
                break;
            }
        }
    }

    if (!handled) {
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && keyboard->hooks.notify_key_to_client != NULL) {
            keyboard->hooks.notify_key_to_client(keyboard->hooks.userdata, syms, (size_t)nsyms, modifiers);
        }
        wlr_seat_set_keyboard(keyboard->seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(keyboard->seat,
            event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    if (keyboard == NULL) {
        return;
    }

    fbwl_cleanup_listener(&keyboard->modifiers);
    fbwl_cleanup_listener(&keyboard->key);
    fbwl_cleanup_listener(&keyboard->destroy);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

static void add_keyboard(struct wlr_seat *seat, struct wl_list *keyboards,
        struct wlr_input_device *device,
        const struct fbwl_seat_keyboard_hooks *keyboard_hooks) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct fbwl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    if (keyboard == NULL) {
        return;
    }

    keyboard->seat = seat;
    keyboard->wlr_keyboard = wlr_keyboard;
    if (keyboard_hooks != NULL) {
        keyboard->hooks = *keyboard_hooks;
    }

    if (wlr_input_device_get_virtual_keyboard(device) == NULL) {
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
            XKB_KEYMAP_COMPILE_NO_FLAGS);

        wlr_keyboard_set_keymap(wlr_keyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
    }

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wl_list_insert(keyboards, &keyboard->link);
    wlr_seat_set_keyboard(seat, wlr_keyboard);
}

void fbwl_seat_add_input_device(struct wlr_seat *seat, struct wlr_cursor *cursor,
        struct wl_list *keyboards, bool *has_pointer,
        struct wlr_input_device *device,
        const struct fbwl_seat_keyboard_hooks *keyboard_hooks) {
    if (seat == NULL || cursor == NULL || keyboards == NULL || has_pointer == NULL || device == NULL) {
        return;
    }

    wlr_log(WLR_INFO, "New input device: type=%d name=%s",
        device->type, device->name != NULL ? device->name : "(null)");

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        add_keyboard(seat, keyboards, device, keyboard_hooks);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        *has_pointer = true;
        wlr_cursor_attach_input_device(cursor, device);
        break;
    default:
        break;
    }

    uint32_t caps = 0;
    if (!wl_list_empty(keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    if (*has_pointer) {
        caps |= WL_SEAT_CAPABILITY_POINTER;
    }
    wlr_seat_set_capabilities(seat, caps);
}

void fbwl_seat_handle_request_cursor(struct wlr_seat *seat, struct wlr_cursor *cursor,
        const struct wlr_seat_pointer_request_set_cursor_event *event) {
    if (seat == NULL || cursor == NULL || event == NULL) {
        return;
    }

    struct wlr_seat_client *focused_client =
        seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(cursor, event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

void fbwl_seat_handle_request_set_selection(struct wlr_seat *seat,
        const struct wlr_seat_request_set_selection_event *event) {
    if (seat == NULL || event == NULL) {
        return;
    }
    wlr_seat_set_selection(seat, event->source, event->serial);
}

void fbwl_seat_handle_request_set_primary_selection(struct wlr_seat *seat,
        const struct wlr_seat_request_set_primary_selection_event *event) {
    if (seat == NULL || event == NULL) {
        return;
    }
    wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void fbwl_seat_handle_request_start_drag(struct wlr_seat *seat,
        struct wlr_seat_request_start_drag_event *event) {
    if (seat == NULL || event == NULL) {
        return;
    }

    if (!wlr_seat_validate_pointer_grab_serial(seat, event->origin, event->serial)) {
        wlr_log(WLR_ERROR, "DnD: rejected start_drag request due to invalid serial");
        if (event->drag != NULL && event->drag->source != NULL) {
            wlr_data_source_destroy(event->drag->source);
        }
        return;
    }

    wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
}
