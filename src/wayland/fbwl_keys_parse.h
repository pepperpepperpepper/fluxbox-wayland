#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

#include "wayland/fbwl_keybindings.h"
#include "wayland/fbwl_mousebindings.h"

typedef bool (*fbwl_keys_add_binding_fn)(void *userdata, enum fbwl_keybinding_key_kind key_kind,
    uint32_t keycode, xkb_keysym_t sym, uint32_t modifiers, enum fbwl_keybinding_action action, int arg,
    const char *cmd);

typedef bool (*fbwl_keys_add_mouse_binding_fn)(void *userdata, enum fbwl_mousebinding_context context,
    int button, uint32_t modifiers, enum fbwl_keybinding_action action, int arg, const char *cmd);

bool fbwl_keys_parse_file(const char *path, fbwl_keys_add_binding_fn add_binding, void *userdata,
    size_t *out_added);

bool fbwl_keys_parse_file_mouse(const char *path, fbwl_keys_add_mouse_binding_fn add_binding, void *userdata,
    size_t *out_added);
