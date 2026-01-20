#include "wayland/fbwl_util.h"

#include <unistd.h>

#include <wayland-server-core.h>

void fbwl_cleanup_listener(struct wl_listener *listener) {
    if (listener->link.prev != NULL && listener->link.next != NULL) {
        wl_list_remove(&listener->link);
        listener->link.prev = NULL;
        listener->link.next = NULL;
    }
}

void fbwl_cleanup_fd(int *fd) {
    if (fd != NULL && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

