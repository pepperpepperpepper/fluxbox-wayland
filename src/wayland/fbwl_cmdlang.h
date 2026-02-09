#pragma once

#include <stdbool.h>

#include "wayland/fbwl_keybindings.h"

typedef bool (*fbwl_cmdlang_exec_action_fn)(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth);

bool fbwl_cmdlang_execute_line(const char *cmd_line, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action);

bool fbwl_cmdlang_execute_macro(const char *macro_args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action);

bool fbwl_cmdlang_execute_togglecmd(const char *args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action);

bool fbwl_cmdlang_execute_delay(const char *args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action);

bool fbwl_cmdlang_execute_foreach(const char *args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action);

bool fbwl_cmdlang_execute_if(const char *args, struct fbwl_view *target_view,
        const struct fbwl_keybindings_hooks *hooks, int depth, fbwl_cmdlang_exec_action_fn exec_action);

