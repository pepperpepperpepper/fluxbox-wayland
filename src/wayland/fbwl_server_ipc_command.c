#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_cmdlang.h"
#include "wayland/fbwl_keybindings.h"
#include "wayland/fbwl_server_internal.h"

static char *ipc_trim_inplace(char *s) {
    while (s != NULL && *s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }

    if (s == NULL || *s == '\0') {
        return s;
    }

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static bool ipc_exec_action_nodup_depth(enum fbwl_keybinding_action action, int arg, const char *cmd,
        struct fbwl_view *target_view, const struct fbwl_keybindings_hooks *hooks, int depth) {
    (void)depth;
    return fbwl_keybindings_execute_action(action, arg, cmd, target_view, hooks);
}

void server_ipc_command(void *userdata, int client_fd, char *line) {
    struct fbwl_server *server = userdata;
    if (server == NULL || line == NULL) {
        return;
    }

    line = ipc_trim_inplace(line);
    if (line == NULL || *line == '\0') {
        fbwl_ipc_send_line(client_fd, "err empty_command");
        return;
    }

    wlr_log(WLR_INFO, "IPC: cmd=%s", line);

    char *saveptr = NULL;
    char *cmd = strtok_r(line, " \t", &saveptr);
    if (cmd == NULL) {
        fbwl_ipc_send_line(client_fd, "err empty_command");
        return;
    }

    if (strcasecmp(cmd, "ping") == 0) {
        fbwl_ipc_send_line(client_fd, "ok pong");
        return;
    }

    if (!server->focus.allow_remote_actions) {
        fbwl_ipc_send_line(client_fd, "err remote_actions_disabled");
        return;
    }

    if (strcasecmp(cmd, "reconfigure") == 0 || strcasecmp(cmd, "reconfig") == 0) {
        fbwl_ipc_send_line(client_fd, "ok reconfigure");
        server_reconfigure(server);
        return;
    }

    if (strcasecmp(cmd, "dump-config") == 0 || strcasecmp(cmd, "dumpconfig") == 0 ||
            strcasecmp(cmd, "dump_config") == 0 || strcasecmp(cmd, "get-config") == 0 ||
            strcasecmp(cmd, "getconfig") == 0) {
        char resp[1024];
        const size_t heads = fbwm_core_head_count(&server->wm);
        snprintf(resp, sizeof(resp),
            "ok keys_file=%s apps_file=%s style_file=%s style_overlay_file=%s menu_file=%s workspaces=%d current=%d heads=%zu",
            server->keys_file != NULL ? server->keys_file : "(null)",
            server->apps_file != NULL ? server->apps_file : "(null)",
            server->style_file != NULL ? server->style_file : "(null)",
            server->style_overlay_file != NULL ? server->style_overlay_file : "(null)",
            server->menu_file != NULL ? server->menu_file : "(null)",
            fbwm_core_workspace_count(&server->wm),
            fbwm_core_workspace_current_for_head(&server->wm, 0) + 1,
            heads);
        fbwl_ipc_send_line(client_fd, resp);
        return;
    }

    if (strcasecmp(cmd, "wallpaper") == 0 || strcasecmp(cmd, "setwallpaper") == 0 ||
            strcasecmp(cmd, "set-wallpaper") == 0) {
        char *rest = ipc_trim_inplace(saveptr);
        if (rest == NULL || *rest == '\0') {
            fbwl_ipc_send_line(client_fd, "err wallpaper_requires_path");
            return;
        }

        enum fbwl_wallpaper_mode mode = FBWL_WALLPAPER_MODE_STRETCH;
        char *path = rest;
        if (strncasecmp(rest, "--mode", 6) == 0 && isspace((unsigned char)rest[6])) {
            char *arg_save = NULL;
            (void)strtok_r(rest, " \t", &arg_save); // --mode
            char *mode_tok = strtok_r(NULL, " \t", &arg_save);
            if (mode_tok == NULL) {
                fbwl_ipc_send_line(client_fd, "err wallpaper_requires_mode");
                return;
            }
            mode = fbwl_wallpaper_mode_parse(mode_tok);
            path = ipc_trim_inplace(arg_save);
            if (path == NULL || *path == '\0') {
                fbwl_ipc_send_line(client_fd, "err wallpaper_requires_path");
                return;
            }
        }

        if (!server_wallpaper_set(server, path, mode)) {
            fbwl_ipc_send_line(client_fd, "err wallpaper_failed");
            return;
        }

        fbwl_ipc_send_line(client_fd, "ok");
        return;
    }

    if (strcasecmp(cmd, "quit") == 0 || strcasecmp(cmd, "exit") == 0) {
        fbwl_ipc_send_line(client_fd, "ok quitting");
        wl_display_terminate(server->wl_display);
        return;
    }

    if (strcasecmp(cmd, "restart") == 0) {
        char *rest = ipc_trim_inplace(saveptr);
        if (rest != NULL && *rest == '\0') {
            rest = NULL;
        }
        fbwl_ipc_send_line(client_fd, "ok restarting");
        server_request_restart(server, rest);
        return;
    }

    if (strcasecmp(cmd, "get-workspace") == 0 || strcasecmp(cmd, "getworkspace") == 0) {
        size_t head0 = 0;
        char *arg = strtok_r(NULL, " \t", &saveptr);
        if (arg != NULL) {
            char *end = NULL;
            long requested_head = strtol(arg, &end, 10);
            if (end == arg || (end != NULL && *end != '\0') || requested_head < 1) {
                fbwl_ipc_send_line(client_fd, "err invalid_head_number");
                return;
            }
            head0 = (size_t)(requested_head - 1);
        }
        char resp[64];
        snprintf(resp, sizeof(resp), "ok workspace=%d", fbwm_core_workspace_current_for_head(&server->wm, head0) + 1);
        fbwl_ipc_send_line(client_fd, resp);
        return;
    }

    if (strcasecmp(cmd, "workspace") == 0) {
        char *arg = strtok_r(NULL, " \t", &saveptr);
        if (arg == NULL) {
            fbwl_ipc_send_line(client_fd, "err workspace_requires_number");
            return;
        }

        char *end = NULL;
        long requested = strtol(arg, &end, 10);
        if (end == arg || (end != NULL && *end != '\0') || requested < 1) {
            fbwl_ipc_send_line(client_fd, "err invalid_workspace_number");
            return;
        }

        int ws = (int)requested - 1;
        if (ws >= fbwm_core_workspace_count(&server->wm)) {
            fbwl_ipc_send_line(client_fd, "err workspace_out_of_range");
            return;
        }

        size_t head0 = 0;
        char *head_arg = strtok_r(NULL, " \t", &saveptr);
        if (head_arg != NULL) {
            char *head_end = NULL;
            long requested_head = strtol(head_arg, &head_end, 10);
            if (head_end == head_arg || (head_end != NULL && *head_end != '\0') || requested_head < 1) {
                fbwl_ipc_send_line(client_fd, "err invalid_head_number");
                return;
            }
            head0 = (size_t)(requested_head - 1);
        }

        server_workspace_switch_on_head(server, head0, ws, "ipc");

        char resp[64];
        snprintf(resp, sizeof(resp), "ok workspace=%d", ws + 1);
        fbwl_ipc_send_line(client_fd, resp);
        return;
    }

    if (strcasecmp(cmd, "nextworkspace") == 0) {
        const int count = fbwm_core_workspace_count(&server->wm);
        size_t head0 = 0;
        char *arg = strtok_r(NULL, " \t", &saveptr);
        if (arg != NULL) {
            char *end = NULL;
            long requested_head = strtol(arg, &end, 10);
            if (end == arg || (end != NULL && *end != '\0') || requested_head < 1) {
                fbwl_ipc_send_line(client_fd, "err invalid_head_number");
                return;
            }
            head0 = (size_t)(requested_head - 1);
        }
        const int cur = fbwm_core_workspace_current_for_head(&server->wm, head0);
        if (count > 0) {
            server_workspace_switch_on_head(server, head0, (cur + 1) % count, "ipc");
        }
        fbwl_ipc_send_line(client_fd, "ok");
        return;
    }

    if (strcasecmp(cmd, "prevworkspace") == 0) {
        const int count = fbwm_core_workspace_count(&server->wm);
        size_t head0 = 0;
        char *arg = strtok_r(NULL, " \t", &saveptr);
        if (arg != NULL) {
            char *end = NULL;
            long requested_head = strtol(arg, &end, 10);
            if (end == arg || (end != NULL && *end != '\0') || requested_head < 1) {
                fbwl_ipc_send_line(client_fd, "err invalid_head_number");
                return;
            }
            head0 = (size_t)(requested_head - 1);
        }
        const int cur = fbwm_core_workspace_current_for_head(&server->wm, head0);
        if (count > 0) {
            server_workspace_switch_on_head(server, head0, (cur + count - 1) % count, "ipc");
        }
        fbwl_ipc_send_line(client_fd, "ok");
        return;
    }

    if (strcasecmp(cmd, "nextwindow") == 0 ||
            strcasecmp(cmd, "focus-next") == 0 || strcasecmp(cmd, "focusnext") == 0) {
        fbwm_core_focus_next(&server->wm);
        fbwl_ipc_send_line(client_fd, "ok");
        return;
    }

    // CmdLang parity: attempt to execute unrecognized IPC commands as Fluxbox cmdlang lines.
    char *rest = ipc_trim_inplace(saveptr);
    const char *cmdlang_cmd = cmd;
    if (cmdlang_cmd[0] == ':') {
        cmdlang_cmd++;
    }

    const size_t args_len = (rest != NULL && *rest != '\0') ? strlen(rest) : 0;
    const size_t cmd_len = strlen(cmdlang_cmd);

    char *cmdlang_line = NULL;
    if (cmd_len == 0 && args_len > 0) {
        cmdlang_line = strdup(rest);
    } else if (args_len > 0) {
        cmdlang_line = malloc(cmd_len + 1 + args_len + 1);
        if (cmdlang_line != NULL) {
            snprintf(cmdlang_line, cmd_len + 1 + args_len + 1, "%s %s", cmdlang_cmd, rest);
        }
    } else {
        cmdlang_line = strdup(cmdlang_cmd);
    }

    if (cmdlang_line == NULL || *cmdlang_line == '\0') {
        free(cmdlang_line);
        fbwl_ipc_send_line(client_fd, "err unknown_command");
        return;
    }

    struct fbwl_keybindings_hooks hooks = keybindings_hooks(server);
    const bool ok = fbwl_cmdlang_execute_line(cmdlang_line, NULL, &hooks, 0, ipc_exec_action_nodup_depth);
    free(cmdlang_line);

    if (!ok) {
        fbwl_ipc_send_line(client_fd, "err unknown_command");
        return;
    }

    fbwl_ipc_send_line(client_fd, "ok");
}
