#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_style_parse.h"
#include "wayland/fbwl_util.h"

static void usage(const char *argv0) {
    printf("Usage: %s [--socket NAME] [--ipc-socket PATH] [--no-xwayland] [--bg-color #RRGGBB[AA]] [-s CMD] [--terminal CMD] [--workspaces N] [--config-dir DIR] [--keys FILE] [--apps FILE] [--style FILE] [--menu FILE] [--log-level LEVEL] [--log-protocol]\n", argv0);
    printf("Keybindings:\n");
    printf("  Alt+Return: spawn terminal\n");
    printf("  Alt+Escape: exit\n");
    printf("  Alt+F1: cycle toplevel\n");
    printf("  Alt+F2: command dialog\n");
    printf("  Alt+M: toggle maximize\n");
    printf("  Alt+F: toggle fullscreen\n");
    printf("  Alt+I: toggle minimize\n");
    printf("  Alt+[1-9]: switch workspace\n");
    printf("  Alt+Ctrl+[1-9]: move focused view to workspace\n");
}

static bool parse_log_level(const char *s, enum wlr_log_importance *out) {
    if (s == NULL || out == NULL) {
        return false;
    }

    if (strcasecmp(s, "silent") == 0 || strcmp(s, "0") == 0) {
        *out = WLR_SILENT;
        return true;
    }
    if (strcasecmp(s, "error") == 0 || strcmp(s, "1") == 0) {
        *out = WLR_ERROR;
        return true;
    }
    if (strcasecmp(s, "info") == 0 || strcmp(s, "2") == 0) {
        *out = WLR_INFO;
        return true;
    }
    if (strcasecmp(s, "debug") == 0 || strcmp(s, "3") == 0) {
        *out = WLR_DEBUG;
        return true;
    }

    return false;
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;
    const char *ipc_socket_path = NULL;
    const char *startup_cmd = NULL;
    const char *terminal_cmd = "weston-terminal";
    const char *keys_file = NULL;
    const char *apps_file = NULL;
    const char *style_file = NULL;
    const char *menu_file = NULL;
    const char *config_dir = NULL;
    float background_color[4] = {0.08f, 0.08f, 0.08f, 1.0f};
    int workspaces = 4;
    bool workspaces_set = false;
    bool enable_xwayland = true;
    enum wlr_log_importance log_level = WLR_INFO;
    bool log_protocol = false;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"ipc-socket", required_argument, NULL, 4},
        {"no-xwayland", no_argument, NULL, 5},
        {"bg-color", required_argument, NULL, 11},
        {"terminal", required_argument, NULL, 2},
        {"workspaces", required_argument, NULL, 3},
        {"config-dir", required_argument, NULL, 8},
        {"keys", required_argument, NULL, 6},
        {"apps", required_argument, NULL, 7},
        {"style", required_argument, NULL, 9},
        {"menu", required_argument, NULL, 10},
        {"log-level", required_argument, NULL, 12},
        {"log-protocol", no_argument, NULL, 13},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "hs:", options, NULL)) != -1) {
        switch (c) {
        case 1:
            socket_name = optarg;
            break;
        case 4:
            ipc_socket_path = optarg;
            break;
        case 5:
            enable_xwayland = false;
            break;
        case 11:
            if (!fbwl_parse_hex_color(optarg, background_color)) {
                fprintf(stderr, "invalid --bg-color (expected #RRGGBB or #RRGGBBAA): %s\n", optarg);
                return 1;
            }
            break;
        case 2:
            terminal_cmd = optarg;
            break;
        case 3:
            workspaces = atoi(optarg);
            if (workspaces < 1) {
                workspaces = 1;
            }
            workspaces_set = true;
            break;
        case 8:
            config_dir = optarg;
            break;
        case 6:
            keys_file = optarg;
            break;
        case 7:
            apps_file = optarg;
            break;
        case 9:
            style_file = optarg;
            break;
        case 10:
            menu_file = optarg;
            break;
        case 12:
            if (!parse_log_level(optarg, &log_level)) {
                fprintf(stderr, "invalid --log-level (expected silent|error|info|debug or 0-3): %s\n", optarg);
                return 1;
            }
            break;
        case 13:
            log_protocol = true;
            break;
        case 's':
            startup_cmd = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }
    if (optind < argc) {
        usage(argv[0]);
        return 1;
    }

    wlr_log_init(log_level, NULL);

    struct fbwl_server server = {0};
    const struct fbwl_server_bootstrap_options bootstrap = {
        .socket_name = socket_name,
        .ipc_socket_path = ipc_socket_path,
        .startup_cmd = startup_cmd,
        .terminal_cmd = terminal_cmd,
        .keys_file = keys_file,
        .apps_file = apps_file,
        .style_file = style_file,
        .menu_file = menu_file,
        .config_dir = config_dir,
        .background_color = background_color,
        .workspaces = workspaces,
        .workspaces_set = workspaces_set,
        .enable_xwayland = enable_xwayland,
        .log_protocol = log_protocol,
    };

    if (!fbwl_server_bootstrap(&server, &bootstrap)) {
        return 1;
    }

    wl_display_run(server.wl_display);

    const bool restarting = server.restart_requested;
    char *restart_cmd = server.restart_cmd;
    server.restart_cmd = NULL;
    fbwl_server_finish(&server);

    if (restarting) {
        if (restart_cmd != NULL && *restart_cmd != '\0') {
            const char *shell = getenv("SHELL");
            if (shell == NULL || *shell == '\0') {
                shell = "/bin/sh";
            }
            execlp(shell, shell, "-c", restart_cmd, (const char *)NULL);
            perror(restart_cmd);
        }

        execvp(argv[0], argv);
        perror(argv[0]);

        const char *base = strrchr(argv[0], '/');
        base = base != NULL ? base + 1 : argv[0];
        execvp(base, argv);
        perror(base);

        free(restart_cmd);
        return 1;
    }

    free(restart_cmd);
    return 0;
}
