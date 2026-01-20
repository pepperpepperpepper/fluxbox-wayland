#include "wayland/fbwl_ipc.h"

#include "wayland/fbwl_util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <wlr/util/log.h>

struct fbwl_ipc_client {
    struct fbwl_ipc *ipc;
    int fd;
    struct wl_event_source *source;
    size_t len;
    char buf[1024];
};

static void ipc_sanitize_component(const char *in, char *out, size_t out_size) {
    if (out_size == 0) {
        return;
    }

    size_t j = 0;
    for (size_t i = 0; in != NULL && in[i] != '\0' && j + 1 < out_size; i++) {
        const unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.') {
            out[j++] = (char)c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';

    if (j == 0) {
        out[0] = 'x';
        out[1] = '\0';
    }
}

static char *ipc_default_socket_path(const char *wayland_socket_name) {
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == NULL || *runtime_dir == '\0') {
        runtime_dir = "/tmp";
    }

    const char *sock = wayland_socket_name;
    if (sock == NULL || *sock == '\0') {
        sock = getenv("WAYLAND_DISPLAY");
    }
    if (sock == NULL || *sock == '\0') {
        sock = "wayland-0";
    }

    char sanitized[64];
    ipc_sanitize_component(sock, sanitized, sizeof(sanitized));

    char path[256];
    int n = snprintf(path, sizeof(path), "%s/fluxbox-wayland-ipc-%s.sock", runtime_dir, sanitized);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return NULL;
    }
    return strdup(path);
}

static bool ipc_write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return true;
}

void fbwl_ipc_send_line(int fd, const char *line) {
    if (line == NULL) {
        return;
    }

    ipc_write_all(fd, line, strlen(line));
    ipc_write_all(fd, "\n", 1);
}

static void ipc_client_destroy(struct fbwl_ipc_client *client) {
    if (client == NULL) {
        return;
    }

    if (client->source != NULL) {
        wl_event_source_remove(client->source);
        client->source = NULL;
    }
    fbwl_cleanup_fd(&client->fd);
    free(client);
}

static int ipc_handle_client_fd(int fd, uint32_t mask, void *data) {
    (void)fd;
    struct fbwl_ipc_client *client = data;
    if (client == NULL) {
        return 0;
    }

    if ((mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) != 0) {
        ipc_client_destroy(client);
        return 0;
    }

    if ((mask & WL_EVENT_READABLE) == 0) {
        return 0;
    }

    const size_t avail = sizeof(client->buf) - 1 - client->len;
    if (avail == 0) {
        fbwl_ipc_send_line(client->fd, "err line_too_long");
        ipc_client_destroy(client);
        return 0;
    }

    ssize_t n = read(client->fd, client->buf + client->len, avail);
    if (n <= 0) {
        ipc_client_destroy(client);
        return 0;
    }

    client->len += (size_t)n;
    client->buf[client->len] = '\0';

    char *nl = strchr(client->buf, '\n');
    if (nl != NULL) {
        *nl = '\0';
        if (client->ipc != NULL && client->ipc->command_fn != NULL) {
            client->ipc->command_fn(client->ipc->command_userdata, client->fd, client->buf);
        }
        ipc_client_destroy(client);
        return 0;
    }

    return 0;
}

static int ipc_handle_listen_fd(int fd, uint32_t mask, void *data) {
    (void)fd;
    struct fbwl_ipc *ipc = data;
    if (ipc == NULL) {
        return 0;
    }

    if ((mask & WL_EVENT_READABLE) == 0) {
        return 0;
    }

    for (;;) {
        int client_fd = accept(ipc->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            wlr_log(WLR_ERROR, "IPC: accept failed: %s", strerror(errno));
            break;
        }

        int clo = fcntl(client_fd, F_GETFD);
        if (clo >= 0) {
            (void)fcntl(client_fd, F_SETFD, clo | FD_CLOEXEC);
        }

        struct fbwl_ipc_client *client = calloc(1, sizeof(*client));
        if (client == NULL) {
            close(client_fd);
            continue;
        }

        client->ipc = ipc;
        client->fd = client_fd;
        client->source = wl_event_loop_add_fd(ipc->loop, client_fd, WL_EVENT_READABLE,
            ipc_handle_client_fd, client);
        if (client->source == NULL) {
            ipc_client_destroy(client);
            continue;
        }
    }

    return 0;
}

void fbwl_ipc_init(struct fbwl_ipc *ipc) {
    if (ipc == NULL) {
        return;
    }

    ipc->listen_fd = -1;
    ipc->listen_source = NULL;
    ipc->socket_path = NULL;
    ipc->loop = NULL;
    ipc->command_fn = NULL;
    ipc->command_userdata = NULL;
}

const char *fbwl_ipc_socket_path(const struct fbwl_ipc *ipc) {
    return ipc != NULL ? ipc->socket_path : NULL;
}

bool fbwl_ipc_start(struct fbwl_ipc *ipc, struct wl_event_loop *loop,
        const char *wayland_socket_name, const char *ipc_socket_path_opt,
        fbwl_ipc_command_fn command_fn, void *command_userdata) {
    if (ipc == NULL || loop == NULL || command_fn == NULL) {
        return false;
    }

    if (ipc->listen_fd >= 0) {
        return true;
    }

    char *path = NULL;
    if (ipc_socket_path_opt != NULL && *ipc_socket_path_opt != '\0') {
        path = strdup(ipc_socket_path_opt);
    } else {
        path = ipc_default_socket_path(wayland_socket_name);
    }

    if (path == NULL || *path == '\0') {
        wlr_log(WLR_ERROR, "IPC: failed to choose socket path");
        free(path);
        return false;
    }

    if (strlen(path) >= sizeof(((struct sockaddr_un){0}).sun_path)) {
        wlr_log(WLR_ERROR, "IPC: socket path too long: %s", path);
        free(path);
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        wlr_log(WLR_ERROR, "IPC: socket() failed: %s", strerror(errno));
        free(path);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        wlr_log(WLR_ERROR, "IPC: bind(%s) failed: %s", path, strerror(errno));
        close(fd);
        free(path);
        return false;
    }

    if (listen(fd, 16) < 0) {
        wlr_log(WLR_ERROR, "IPC: listen() failed: %s", strerror(errno));
        unlink(path);
        close(fd);
        free(path);
        return false;
    }

    (void)chmod(path, 0600);

    ipc->loop = loop;
    ipc->command_fn = command_fn;
    ipc->command_userdata = command_userdata;

    ipc->socket_path = path;
    ipc->listen_fd = fd;
    ipc->listen_source = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE,
        ipc_handle_listen_fd, ipc);
    if (ipc->listen_source == NULL) {
        wlr_log(WLR_ERROR, "IPC: failed to add fd to wl_event_loop");
        unlink(path);
        fbwl_cleanup_fd(&ipc->listen_fd);
        free(ipc->socket_path);
        ipc->socket_path = NULL;
        return false;
    }

    wlr_log(WLR_INFO, "IPC: listening on %s", ipc->socket_path);
    return true;
}

void fbwl_ipc_finish(struct fbwl_ipc *ipc) {
    if (ipc == NULL) {
        return;
    }

    if (ipc->listen_source != NULL) {
        wl_event_source_remove(ipc->listen_source);
        ipc->listen_source = NULL;
    }

    fbwl_cleanup_fd(&ipc->listen_fd);

    if (ipc->socket_path != NULL) {
        unlink(ipc->socket_path);
        free(ipc->socket_path);
        ipc->socket_path = NULL;
    }

    ipc->loop = NULL;
    ipc->command_fn = NULL;
    ipc->command_userdata = NULL;
}
