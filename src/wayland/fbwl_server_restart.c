#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"

static const char *skip_ws(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

void server_request_restart(struct fbwl_server *server, const char *cmd) {
    if (server == NULL) {
        return;
    }

    server->restart_requested = true;

    free(server->restart_cmd);
    server->restart_cmd = NULL;

    const char *trimmed = skip_ws(cmd);
    if (trimmed != NULL && *trimmed != '\0') {
        server->restart_cmd = strdup(trimmed);
        if (server->restart_cmd == NULL) {
            wlr_log(WLR_ERROR, "Restart: OOM duplicating restart cmd");
        }
    }

    wlr_log(WLR_INFO, "Restart: requested cmd=%s", server->restart_cmd != NULL ? server->restart_cmd : "(self)");

    if (server->wl_display != NULL) {
        wl_display_terminate(server->wl_display);
    }
}

void server_keybindings_restart(void *userdata, const char *cmd) {
    server_request_restart(userdata, cmd);
}

