#pragma once

#include <stdbool.h>

struct wl_event_loop;
struct wl_event_source;

struct fbwl_ipc;

typedef void (*fbwl_ipc_command_fn)(void *userdata, int client_fd, char *line);

struct fbwl_ipc {
    int listen_fd;
    struct wl_event_source *listen_source;
    char *socket_path;

    struct wl_event_loop *loop;
    fbwl_ipc_command_fn command_fn;
    void *command_userdata;
};

void fbwl_ipc_init(struct fbwl_ipc *ipc);
bool fbwl_ipc_start(struct fbwl_ipc *ipc, struct wl_event_loop *loop,
        const char *wayland_socket_name, const char *ipc_socket_path_opt,
        fbwl_ipc_command_fn command_fn, void *command_userdata);
void fbwl_ipc_finish(struct fbwl_ipc *ipc);
const char *fbwl_ipc_socket_path(const struct fbwl_ipc *ipc);

void fbwl_ipc_send_line(int fd, const char *line);
