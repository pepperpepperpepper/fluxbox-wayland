#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

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

    if (strcasecmp(cmd, "reconfigure") == 0 || strcasecmp(cmd, "reconfig") == 0) {
        fbwl_ipc_send_line(client_fd, "ok reconfigure");
        server_reconfigure(server);
        return;
    }

    if (strcasecmp(cmd, "dump-config") == 0 || strcasecmp(cmd, "dumpconfig") == 0 ||
            strcasecmp(cmd, "dump_config") == 0 || strcasecmp(cmd, "get-config") == 0 ||
            strcasecmp(cmd, "getconfig") == 0) {
        char resp[1024];
        snprintf(resp, sizeof(resp),
            "ok keys_file=%s apps_file=%s style_file=%s menu_file=%s workspaces=%d current=%d",
            server->keys_file != NULL ? server->keys_file : "(null)",
            server->apps_file != NULL ? server->apps_file : "(null)",
            server->style_file != NULL ? server->style_file : "(null)",
            server->menu_file != NULL ? server->menu_file : "(null)",
            fbwm_core_workspace_count(&server->wm),
            fbwm_core_workspace_current(&server->wm) + 1);
        fbwl_ipc_send_line(client_fd, resp);
        return;
    }

    if (strcasecmp(cmd, "quit") == 0 || strcasecmp(cmd, "exit") == 0) {
        fbwl_ipc_send_line(client_fd, "ok quitting");
        wl_display_terminate(server->wl_display);
        return;
    }

    if (strcasecmp(cmd, "get-workspace") == 0 || strcasecmp(cmd, "getworkspace") == 0) {
        char resp[64];
        snprintf(resp, sizeof(resp), "ok workspace=%d", fbwm_core_workspace_current(&server->wm) + 1);
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

        fbwm_core_workspace_switch(&server->wm, ws);
        apply_workspace_visibility(server, "ipc");

        char resp[64];
        snprintf(resp, sizeof(resp), "ok workspace=%d", ws + 1);
        fbwl_ipc_send_line(client_fd, resp);
        return;
    }

    if (strcasecmp(cmd, "nextworkspace") == 0) {
        const int count = fbwm_core_workspace_count(&server->wm);
        const int cur = fbwm_core_workspace_current(&server->wm);
        if (count > 0) {
            fbwm_core_workspace_switch(&server->wm, (cur + 1) % count);
            apply_workspace_visibility(server, "ipc");
        }
        fbwl_ipc_send_line(client_fd, "ok");
        return;
    }

    if (strcasecmp(cmd, "prevworkspace") == 0) {
        const int count = fbwm_core_workspace_count(&server->wm);
        const int cur = fbwm_core_workspace_current(&server->wm);
        if (count > 0) {
            fbwm_core_workspace_switch(&server->wm, (cur + count - 1) % count);
            apply_workspace_visibility(server, "ipc");
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

    fbwl_ipc_send_line(client_fd, "err unknown_command");
}
