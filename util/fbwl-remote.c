#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void usage(const char *argv0) {
    printf("Usage: %s [--socket WAYLAND_DISPLAY] [--ipc-socket PATH] [--timeout-ms N] <command...>\n", argv0);
    printf("\n");
    printf("Examples:\n");
    printf("  %s ping\n", argv0);
    printf("  %s --socket wayland-1 get-workspace\n", argv0);
    printf("  %s --socket wayland-1 workspace 2\n", argv0);
    printf("  %s --socket wayland-1 quit\n", argv0);
}

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

static char *default_ipc_socket_path(const char *wayland_socket_name) {
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

static char *join_args(int argc, char **argv, int start_index) {
    if (start_index >= argc) {
        return NULL;
    }

    size_t needed = 0;
    for (int i = start_index; i < argc; i++) {
        needed += strlen(argv[i]) + 1;
    }

    char *out = malloc(needed + 1);
    if (out == NULL) {
        return NULL;
    }

    char *p = out;
    size_t remaining = needed + 1;
    for (int i = start_index; i < argc; i++) {
        const char *arg = argv[i];
        if (i != start_index && remaining > 1) {
            *p++ = ' ';
            *p = '\0';
            remaining--;
        }

        size_t arg_len = strlen(arg);
        if (arg_len >= remaining) {
            arg_len = remaining - 1;
        }
        memcpy(p, arg, arg_len);
        p += arg_len;
        remaining -= arg_len;
        *p = '\0';
    }

    return out;
}

static bool write_all(int fd, const void *buf, size_t len) {
    const unsigned char *p = buf;
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

int main(int argc, char **argv) {
    const char *wayland_socket_name = NULL;
    const char *ipc_socket_path = NULL;
    int timeout_ms = 2000;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"ipc-socket", required_argument, NULL, 2},
        {"timeout-ms", required_argument, NULL, 3},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "h", options, NULL)) != -1) {
        switch (c) {
        case 1:
            wayland_socket_name = optarg;
            break;
        case 2:
            ipc_socket_path = optarg;
            break;
        case 3:
            timeout_ms = atoi(optarg);
            if (timeout_ms < 1) {
                timeout_ms = 1;
            }
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }

    char *cmdline = join_args(argc, argv, optind);
    if (cmdline == NULL) {
        fprintf(stderr, "fbwl-remote: failed to build command line\n");
        return 1;
    }

    char *path = NULL;
    if (ipc_socket_path != NULL && *ipc_socket_path != '\0') {
        path = strdup(ipc_socket_path);
    } else if (getenv("FBWL_IPC_SOCKET") != NULL && *getenv("FBWL_IPC_SOCKET") != '\0') {
        path = strdup(getenv("FBWL_IPC_SOCKET"));
    } else {
        path = default_ipc_socket_path(wayland_socket_name);
    }

    if (path == NULL || *path == '\0') {
        fprintf(stderr, "fbwl-remote: failed to determine IPC socket path\n");
        free(cmdline);
        free(path);
        return 1;
    }

    if (strlen(path) >= sizeof(((struct sockaddr_un){0}).sun_path)) {
        fprintf(stderr, "fbwl-remote: IPC socket path too long: %s\n", path);
        free(cmdline);
        free(path);
        return 1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "fbwl-remote: socket() failed: %s\n", strerror(errno));
        free(cmdline);
        free(path);
        return 1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "fbwl-remote: connect(%s) failed: %s\n", path, strerror(errno));
        close(fd);
        free(cmdline);
        free(path);
        return 1;
    }

    bool ok = write_all(fd, cmdline, strlen(cmdline)) && write_all(fd, "\n", 1);
    free(cmdline);

    if (!ok) {
        fprintf(stderr, "fbwl-remote: failed to send command\n");
        close(fd);
        free(path);
        return 1;
    }

    char resp[1024];
    size_t len = 0;
    bool saw_newline = false;
    resp[0] = '\0';

    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    while (!saw_newline && len + 1 < sizeof(resp)) {
        int prc = poll(&pfd, 1, timeout_ms);
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-remote: poll() failed: %s\n", strerror(errno));
            close(fd);
            free(path);
            return 1;
        }
        if (prc == 0) {
            fprintf(stderr, "fbwl-remote: timed out waiting for response (%d ms)\n", timeout_ms);
            close(fd);
            free(path);
            return 1;
        }

        ssize_t n = read(fd, resp + len, sizeof(resp) - 1 - len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fbwl-remote: read() failed: %s\n", strerror(errno));
            close(fd);
            free(path);
            return 1;
        }
        if (n == 0) {
            break;
        }

        len += (size_t)n;
        resp[len] = '\0';

        if (strchr(resp, '\n') != NULL) {
            saw_newline = true;
        }
    }

    close(fd);
    free(path);

    char *nl = strchr(resp, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }

    if (len == 0 || resp[0] == '\0') {
        fprintf(stderr, "fbwl-remote: empty response\n");
        return 1;
    }

    printf("%s\n", resp);

    if (strncmp(resp, "ok", 2) == 0) {
        return 0;
    }
    return 1;
}
