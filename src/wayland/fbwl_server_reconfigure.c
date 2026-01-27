#include <stddef.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_keys_parse.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_style_parse.h"

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

    bool did_any = false;

    const char *keys_file = server->keys_file;
    if (keys_file != NULL && *keys_file != '\0') {
        fbwl_keybindings_free(&server->keybindings, &server->keybinding_count);
        fbwl_mousebindings_free(&server->mousebindings, &server->mousebinding_count);

        fbwl_keybindings_add_defaults(&server->keybindings, &server->keybinding_count, server->terminal_cmd);
        (void)fbwl_keys_parse_file(keys_file, server_keybindings_add_from_keys_file, server, NULL);
        (void)fbwl_keys_parse_file_mouse(keys_file, server_mousebindings_add_from_keys_file, server, NULL);

        wlr_log(WLR_INFO, "Reconfigure: reloaded keys from %s", keys_file);
        did_any = true;
    } else {
        wlr_log(WLR_INFO, "Reconfigure: no keys file configured");
    }

    const char *apps_file = server->apps_file;
    if (apps_file != NULL && *apps_file != '\0') {
        fbwl_apps_rules_free(&server->apps_rules, &server->apps_rule_count);
        if (fbwl_apps_rules_load_file(&server->apps_rules, &server->apps_rule_count, apps_file)) {
            wlr_log(WLR_INFO, "Reconfigure: reloaded apps from %s", apps_file);
        } else {
            wlr_log(WLR_ERROR, "Reconfigure: failed to reload apps from %s", apps_file);
        }
        did_any = true;
    }

    const char *style_file = server->style_file;
    if (style_file != NULL && *style_file != '\0') {
        struct fbwl_decor_theme new_theme = {0};
        decor_theme_set_defaults(&new_theme);
        if (fbwl_style_load_file(&new_theme, style_file)) {
            server->decor_theme = new_theme;
            server_toolbar_ui_rebuild(server);
            wlr_log(WLR_INFO, "Reconfigure: reloaded style from %s", style_file);
            did_any = true;
        } else {
            wlr_log(WLR_ERROR, "Reconfigure: failed to reload style from %s", style_file);
        }
    }

    const char *menu_file = server->menu_file;
    if (menu_file != NULL && *menu_file != '\0') {
        if (!server_menu_load_file(server, menu_file)) {
            wlr_log(WLR_ERROR, "Reconfigure: failed to reload menu from %s", menu_file);
            server_menu_create_default(server);
        } else {
            wlr_log(WLR_INFO, "Reconfigure: reloaded menu from %s", menu_file);
        }
        did_any = true;
    }

    if (!did_any) {
        wlr_log(WLR_INFO, "Reconfigure: nothing to reload");
    }
}
