#include <stddef.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_keys_parse.h"
#include "wayland/fbwl_server_internal.h"

static bool server_keybindings_add_from_keys_file(void *userdata, enum fbwl_keybinding_key_kind key_kind,
        uint32_t keycode, xkb_keysym_t sym, uint32_t modifiers, enum fbwl_keybinding_action action, int arg,
        const char *cmd) {
    struct fbwl_server *server = userdata;
    if (key_kind == FBWL_KEYBIND_KEYCODE) {
        return fbwl_keybindings_add_keycode(&server->keybindings, &server->keybinding_count, keycode, modifiers,
            action, arg, cmd);
    }
    return fbwl_keybindings_add(&server->keybindings, &server->keybinding_count, sym, modifiers, action, arg, cmd);
}

static bool server_mousebindings_add_from_keys_file(void *userdata, enum fbwl_mousebinding_context context,
        int button, uint32_t modifiers, enum fbwl_keybinding_action action, int arg, const char *cmd) {
    struct fbwl_server *server = userdata;
    return fbwl_mousebindings_add(&server->mousebindings, &server->mousebinding_count, context, button, modifiers,
        action, arg, cmd);
}

void server_reconfigure(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    const char *keys_file = server->keys_file;
    if (keys_file == NULL || *keys_file == '\0') {
        wlr_log(WLR_INFO, "Reconfigure: no keys file configured");
        return;
    }

    fbwl_keybindings_free(&server->keybindings, &server->keybinding_count);
    fbwl_mousebindings_free(&server->mousebindings, &server->mousebinding_count);

    fbwl_keybindings_add_defaults(&server->keybindings, &server->keybinding_count, server->terminal_cmd);
    (void)fbwl_keys_parse_file(keys_file, server_keybindings_add_from_keys_file, server, NULL);
    (void)fbwl_keys_parse_file_mouse(keys_file, server_mousebindings_add_from_keys_file, server, NULL);

    wlr_log(WLR_INFO, "Reconfigure: reloaded keys from %s", keys_file);
}

