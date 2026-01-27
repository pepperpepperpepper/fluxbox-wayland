#pragma once

#include <stdbool.h>

#include "wayland/fbwl_keybindings.h"

bool fbwl_fluxbox_cmd_resolve(const char *cmd_name, const char *cmd_args,
    enum fbwl_keybinding_action *out_action, int *out_arg, const char **out_cmd);

