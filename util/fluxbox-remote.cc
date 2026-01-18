// fluxbox-remote.cc
// Copyright (c) 2007 Fluxbox Team (fluxgen at fluxbox dot org)
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

static void usage(const char *argv0) {
    printf("Usage: %s [--x11|--wayland] [--socket WAYLAND_DISPLAY] [--ipc-socket PATH] [--timeout-ms N] <command...>\n",
        argv0);
    printf("\n");
    printf("Notes:\n");
    printf("  - With X11 Fluxbox, this uses the _FLUXBOX_ACTION root window property.\n");
    printf("  - With fluxbox-wayland, this talks to the compositor IPC socket.\n");
}

static std::string join_args(int argc, char **argv, int start_index) {
    std::string out;
    for (int i = start_index; i < argc; i++) {
        if (!out.empty()) {
            out += " ";
        }
        out += argv[i];
    }
    return out;
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

static std::string default_ipc_socket_path(const char *wayland_socket_name) {
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
        return std::string();
    }
    return std::string(path);
}

static bool write_all(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

static int wayland_ipc_call(const std::string &cmdline, const char *wayland_socket_name,
        const char *ipc_socket_path, int timeout_ms) {
    std::string path;
    if (ipc_socket_path != NULL && *ipc_socket_path != '\0') {
        path = ipc_socket_path;
    } else if (getenv("FBWL_IPC_SOCKET") != NULL && *getenv("FBWL_IPC_SOCKET") != '\0') {
        path = getenv("FBWL_IPC_SOCKET");
    } else {
        path = default_ipc_socket_path(wayland_socket_name);
    }

    if (path.empty()) {
        fprintf(stderr, "fluxbox-remote: failed to determine Wayland IPC socket path\n");
        return EXIT_FAILURE;
    }
    struct sockaddr_un dummy;
    memset(&dummy, 0, sizeof(dummy));
    if (path.size() >= sizeof(dummy.sun_path)) {
        fprintf(stderr, "fluxbox-remote: IPC socket path too long: %s\n", path.c_str());
        return EXIT_FAILURE;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "fluxbox-remote: socket() failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "fluxbox-remote: connect(%s) failed: %s\n", path.c_str(), strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }

    std::string send = cmdline;
    send += "\n";
    if (!write_all(fd, send.c_str(), send.size())) {
        fprintf(stderr, "fluxbox-remote: failed to send command\n");
        close(fd);
        return EXIT_FAILURE;
    }

    char resp[1024];
    size_t len = 0;
    bool saw_newline = false;
    resp[0] = '\0';

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!saw_newline && len + 1 < sizeof(resp)) {
        int prc = poll(&pfd, 1, timeout_ms);
        if (prc < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fluxbox-remote: poll() failed: %s\n", strerror(errno));
            close(fd);
            return EXIT_FAILURE;
        }
        if (prc == 0) {
            fprintf(stderr, "fluxbox-remote: timed out waiting for response (%d ms)\n", timeout_ms);
            close(fd);
            return EXIT_FAILURE;
        }

        ssize_t n = read(fd, resp + len, sizeof(resp) - 1 - len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "fluxbox-remote: read() failed: %s\n", strerror(errno));
            close(fd);
            return EXIT_FAILURE;
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

    char *nl = strchr(resp, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }

    if (len == 0 || resp[0] == '\0') {
        fprintf(stderr, "fluxbox-remote: empty response\n");
        return EXIT_FAILURE;
    }

    printf("%s\n", resp);
    if (strncmp(resp, "ok", 2) == 0) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

static bool g_gotError = false;
static int HandleIPCError(Display * /*disp*/, XErrorEvent * /*ptr*/) {
    g_gotError = true;
    return 0;
}

typedef int (*xerror_cb_t)(Display *, XErrorEvent *);

static int x11_call(const std::string &cmdline) {
    Display *disp = XOpenDisplay(NULL);
    if (!disp) {
        perror("error, can't open display.");
        return EXIT_FAILURE;
    }

    Atom atom_fbcmd = XInternAtom(disp, "_FLUXBOX_ACTION", False);
    Atom atom_result = XInternAtom(disp, "_FLUXBOX_ACTION_RESULT", False);
    Window root = DefaultRootWindow(disp);

    g_gotError = false;
    xerror_cb_t error_cb = XSetErrorHandler(HandleIPCError);

    if (cmdline == "result") {
        XTextProperty text_prop;
        if (XGetTextProperty(disp, root, &text_prop, atom_result) != 0 && text_prop.value != 0 &&
                text_prop.nitems > 0) {
            printf("%s", text_prop.value);
            XFree(text_prop.value);
        }
    } else {
        XChangeProperty(disp, root, atom_fbcmd, XA_STRING, 8, PropModeReplace,
            (unsigned char *)cmdline.c_str(), cmdline.size());
        XSync(disp, false);
    }

    int rc = (g_gotError ? EXIT_FAILURE : EXIT_SUCCESS);

    XSetErrorHandler(error_cb);
    XCloseDisplay(disp);

    return rc;
}

int main(int argc, char **argv) {
    bool force_x11 = false;
    bool force_wayland = false;
    const char *wayland_socket_name = NULL;
    const char *ipc_socket_path = NULL;
    int timeout_ms = 2000;

    if (argc <= 1) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }

    int argi = 1;
    while (argi < argc) {
        const char *a = argv[argi];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(a, "--x11") == 0) {
            force_x11 = true;
            argi++;
        } else if (strcmp(a, "--wayland") == 0) {
            force_wayland = true;
            argi++;
        } else if (strcmp(a, "--socket") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "fluxbox-remote: missing argument for --socket\n");
                return EXIT_FAILURE;
            }
            wayland_socket_name = argv[argi + 1];
            argi += 2;
        } else if (strcmp(a, "--ipc-socket") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "fluxbox-remote: missing argument for --ipc-socket\n");
                return EXIT_FAILURE;
            }
            ipc_socket_path = argv[argi + 1];
            argi += 2;
        } else if (strcmp(a, "--timeout-ms") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "fluxbox-remote: missing argument for --timeout-ms\n");
                return EXIT_FAILURE;
            }
            timeout_ms = atoi(argv[argi + 1]);
            if (timeout_ms < 1) {
                timeout_ms = 1;
            }
            argi += 2;
        } else if (a[0] == '-') {
            fprintf(stderr, "fluxbox-remote: unknown option: %s\n", a);
            return EXIT_FAILURE;
        } else {
            break;
        }
    }

    if (argi >= argc) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    std::string cmdline = join_args(argc, argv, argi);

    if (force_x11 && force_wayland) {
        fprintf(stderr, "fluxbox-remote: cannot use both --x11 and --wayland\n");
        return EXIT_FAILURE;
    }

    bool use_wayland = false;
    if (force_wayland) {
        use_wayland = true;
    } else if (force_x11) {
        use_wayland = false;
    } else if ((ipc_socket_path != NULL && *ipc_socket_path != '\0') ||
            (wayland_socket_name != NULL && *wayland_socket_name != '\0') ||
            (getenv("FBWL_IPC_SOCKET") != NULL && *getenv("FBWL_IPC_SOCKET") != '\0')) {
        use_wayland = true;
    } else {
        Display *disp = XOpenDisplay(NULL);
        if (disp != NULL) {
            XCloseDisplay(disp);
            use_wayland = false;
        } else if ((getenv("WAYLAND_DISPLAY") != NULL && *getenv("WAYLAND_DISPLAY") != '\0') ||
                (getenv("FBWL_IPC_SOCKET") != NULL && *getenv("FBWL_IPC_SOCKET") != '\0')) {
            use_wayland = true;
        } else {
            perror("error, can't open display.");
            return EXIT_FAILURE;
        }
    }

    if (use_wayland) {
        return wayland_ipc_call(cmdline, wayland_socket_name, ipc_socket_path, timeout_ms);
    }
    return x11_call(cmdline);
}
