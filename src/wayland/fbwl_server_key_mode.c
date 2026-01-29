#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"

void fbwl_server_key_mode_set(struct fbwl_server *server, const char *mode) {
    if (server == NULL) {
        return;
    }

    char *new_mode = NULL;
    if (mode != NULL && *mode != '\0') {
        const char *p = mode;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        const char *start = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len > 0 && start[len - 1] == ':') {
            len--;
        }
        if (len > 0) {
            char *tmp = malloc(len + 1);
            if (tmp != NULL) {
                memcpy(tmp, start, len);
                tmp[len] = '\0';
                if (strcasecmp(tmp, "default") != 0) {
                    new_mode = tmp;
                } else {
                    free(tmp);
                }
            }
        }
    }

    free(server->key_mode);
    server->key_mode = new_mode;

    wlr_log(WLR_INFO, "KeyMode: set to %s", server->key_mode != NULL ? server->key_mode : "default");
}

