#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"

static bool str_is_truthy(const char *s) {
    if (s == NULL) {
        return false;
    }
    if (strcasecmp(s, "1") == 0 || strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 ||
            strcasecmp(s, "on") == 0 || strcasecmp(s, "auto") == 0) {
        return true;
    }
    return false;
}

static bool str_is_falsey(const char *s) {
    if (s == NULL) {
        return false;
    }
    if (strcasecmp(s, "0") == 0 || strcasecmp(s, "false") == 0 || strcasecmp(s, "no") == 0 ||
            strcasecmp(s, "off") == 0 || strcasecmp(s, "disable") == 0 || strcasecmp(s, "disabled") == 0) {
        return true;
    }
    return false;
}

static bool exe_in_path(const char *exe) {
    if (exe == NULL || *exe == '\0') {
        return false;
    }

    if (strchr(exe, '/') != NULL) {
        return access(exe, X_OK) == 0;
    }

    const char *path = getenv("PATH");
    if (path == NULL || *path == '\0') {
        return false;
    }

    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        return false;
    }

    bool ok = false;
    char *saveptr = NULL;
    for (char *dir = strtok_r(path_copy, ":", &saveptr); dir != NULL; dir = strtok_r(NULL, ":", &saveptr)) {
        if (*dir == '\0') {
            dir = ".";
        }

        const size_t dir_len = strlen(dir);
        const size_t exe_len = strlen(exe);
        if (dir_len > SIZE_MAX - 2 - exe_len) {
            continue;
        }

        const size_t cap = dir_len + 1 + exe_len + 1;
        char *candidate = malloc(cap);
        if (candidate == NULL) {
            continue;
        }
        snprintf(candidate, cap, "%s/%s", dir, exe);
        if (access(candidate, X_OK) == 0) {
            ok = true;
            free(candidate);
            break;
        }
        free(candidate);
    }

    free(path_copy);
    return ok;
}

static pid_t spawn_shell(const char *cmd) {
    if (cmd == NULL || *cmd == '\0') {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
        _exit(127);
    }
    return pid;
}

static pid_t spawn_exe(const char *exe) {
    if (exe == NULL || *exe == '\0') {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execlp(exe, exe, (void *)NULL);
        _exit(127);
    }
    return pid;
}

static bool pid_is_alive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

void fbwl_xembed_sni_proxy_maybe_start(struct fbwl_server *server, const char *display_name) {
    if (server == NULL) {
        return;
    }

    if (pid_is_alive(server->xembed_sni_proxy_pid)) {
        return;
    }
    server->xembed_sni_proxy_pid = 0;

    const char *cfg = getenv("FBWL_XEMBED_SNI_PROXY");
    if (str_is_falsey(cfg)) {
        wlr_log(WLR_INFO, "XEmbedProxy: disabled (FBWL_XEMBED_SNI_PROXY=%s)", cfg != NULL ? cfg : "(null)");
        return;
    }

    if (server->xwayland == NULL || display_name == NULL || *display_name == '\0') {
        return;
    }

    // For custom commands (including args), allow a shell command override.
    // For boolean-ish values, fall through to auto.
    if (cfg != NULL && *cfg != '\0' && !str_is_truthy(cfg)) {
        pid_t pid = spawn_shell(cfg);
        if (pid > 0) {
            server->xembed_sni_proxy_pid = pid;
            wlr_log(WLR_INFO, "XEmbedProxy: started pid=%d cmd=%s DISPLAY=%s",
                (int)pid, cfg, display_name);
        } else {
            wlr_log(WLR_INFO, "XEmbedProxy: failed to start cmd=%s DISPLAY=%s", cfg, display_name);
        }
        return;
    }

    static const char *const candidates[] = {
        "xembedsniproxy",
        "snixembed",
        "xembed-sni-proxy",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        const char *exe = candidates[i];
        if (!exe_in_path(exe)) {
            continue;
        }

        pid_t pid = spawn_exe(exe);
        if (pid > 0) {
            server->xembed_sni_proxy_pid = pid;
            wlr_log(WLR_INFO, "XEmbedProxy: started pid=%d exe=%s DISPLAY=%s",
                (int)pid, exe, display_name);
            return;
        }

        wlr_log(WLR_INFO, "XEmbedProxy: failed to start exe=%s DISPLAY=%s", exe, display_name);
    }
}

void fbwl_xembed_sni_proxy_stop(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    pid_t pid = server->xembed_sni_proxy_pid;
    if (pid <= 0) {
        return;
    }
    server->xembed_sni_proxy_pid = 0;

    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
        return;
    }

    (void)kill(pid, SIGTERM);
    for (int i = 0; i < 50; i++) {
        r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            return;
        }
        usleep(10 * 1000);
    }

    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, &status, 0);
}
