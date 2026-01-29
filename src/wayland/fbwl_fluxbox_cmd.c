#include "wayland/fbwl_fluxbox_cmd.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool parse_one_based_workspace(const char *s, int *out_ws0) {
    if (s == NULL || out_ws0 == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    char *end = NULL;
    long ws = strtol(s, &end, 10);
    if (end == s) {
        return false;
    }

    if (ws > 0) {
        ws -= 1;
    }

    if (ws < -100000 || ws > 100000) {
        return false;
    }

    *out_ws0 = (int)ws;
    return true;
}

bool fbwl_fluxbox_cmd_resolve(const char *cmd_name, const char *cmd_args,
        enum fbwl_keybinding_action *out_action, int *out_arg, const char **out_cmd) {
    if (cmd_name == NULL || *cmd_name == '\0' || out_action == NULL || out_arg == NULL || out_cmd == NULL) {
        return false;
    }

    *out_action = FBWL_KEYBIND_EXIT;
    *out_arg = 0;
    *out_cmd = NULL;

    if (strcasecmp(cmd_name, "execcommand") == 0 ||
            strcasecmp(cmd_name, "exec") == 0 ||
            strcasecmp(cmd_name, "execute") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_EXEC;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "commanddialog") == 0 ||
            strcasecmp(cmd_name, "rundialog") == 0) {
        *out_action = FBWL_KEYBIND_COMMAND_DIALOG;
        return true;
    }

    if (strcasecmp(cmd_name, "exit") == 0 ||
            strcasecmp(cmd_name, "quit") == 0) {
        *out_action = FBWL_KEYBIND_EXIT;
        return true;
    }

    if (strcasecmp(cmd_name, "reconfig") == 0 ||
            strcasecmp(cmd_name, "reconfigure") == 0) {
        *out_action = FBWL_KEYBIND_RECONFIGURE;
        return true;
    }

    if (strcasecmp(cmd_name, "keymode") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
            cmd_args++;
        }
        if (*cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_KEYMODE;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "nextwindow") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS_NEXT;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "prevwindow") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS_PREV;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "nexttab") == 0) {
        *out_action = FBWL_KEYBIND_TAB_NEXT;
        return true;
    }
    if (strcasecmp(cmd_name, "prevtab") == 0) {
        *out_action = FBWL_KEYBIND_TAB_PREV;
        return true;
    }
    if (strcasecmp(cmd_name, "tab") == 0) {
        int tab0 = 0;
        if (cmd_args != NULL && *cmd_args != '\0') {
            while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
                cmd_args++;
            }
            if (*cmd_args == '\0') {
                return false;
            }
            char *end = NULL;
            long tab = strtol(cmd_args, &end, 10);
            if (end == cmd_args) {
                return false;
            }
            while (*end != '\0' && isspace((unsigned char)*end)) {
                end++;
            }
            if (*end != '\0') {
                return false;
            }
            if (tab < 1 || tab > 100000) {
                return false;
            }
            tab0 = (int)(tab - 1);
        }
        *out_action = FBWL_KEYBIND_TAB_GOTO;
        *out_arg = tab0;
        return true;
    }

    if (strcasecmp(cmd_name, "maximize") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_MAXIMIZE;
        return true;
    }
    if (strcasecmp(cmd_name, "fullscreen") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_FULLSCREEN;
        return true;
    }
    if (strcasecmp(cmd_name, "minimize") == 0 || strcasecmp(cmd_name, "iconify") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_MINIMIZE;
        return true;
    }

    if (strcasecmp(cmd_name, "close") == 0) {
        *out_action = FBWL_KEYBIND_CLOSE;
        return true;
    }
    if (strcasecmp(cmd_name, "kill") == 0) {
        *out_action = FBWL_KEYBIND_KILL;
        return true;
    }

    if (strcasecmp(cmd_name, "windowmenu") == 0) {
        *out_action = FBWL_KEYBIND_WINDOW_MENU;
        return true;
    }
    if (strcasecmp(cmd_name, "rootmenu") == 0) {
        *out_action = FBWL_KEYBIND_ROOT_MENU;
        return true;
    }
    if (strcasecmp(cmd_name, "workspacemenu") == 0) {
        *out_action = FBWL_KEYBIND_WORKSPACE_MENU;
        return true;
    }
    if (strcasecmp(cmd_name, "hidemenu") == 0 || strcasecmp(cmd_name, "hidemenus") == 0) {
        *out_action = FBWL_KEYBIND_HIDE_MENUS;
        return true;
    }

    if (strcasecmp(cmd_name, "workspace") == 0) {
        int ws0 = 0;
        if (!parse_one_based_workspace(cmd_args, &ws0)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_WORKSPACE_SWITCH;
        *out_arg = ws0;
        return true;
    }

    if (strcasecmp(cmd_name, "nextworkspace") == 0) {
        *out_action = FBWL_KEYBIND_WORKSPACE_NEXT;
        return true;
    }
    if (strcasecmp(cmd_name, "prevworkspace") == 0) {
        *out_action = FBWL_KEYBIND_WORKSPACE_PREV;
        return true;
    }

    if (strcasecmp(cmd_name, "sendtoworkspace") == 0) {
        int ws0 = 0;
        if (!parse_one_based_workspace(cmd_args, &ws0)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_SEND_TO_WORKSPACE;
        *out_arg = ws0;
        return true;
    }

    if (strcasecmp(cmd_name, "taketoworkspace") == 0) {
        int ws0 = 0;
        if (!parse_one_based_workspace(cmd_args, &ws0)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_TAKE_TO_WORKSPACE;
        *out_arg = ws0;
        return true;
    }

    if (strcasecmp(cmd_name, "sendtonextworkspace") == 0) {
        *out_action = FBWL_KEYBIND_SEND_TO_REL_WORKSPACE;
        *out_arg = +1;
        return true;
    }
    if (strcasecmp(cmd_name, "sendtoprevworkspace") == 0) {
        *out_action = FBWL_KEYBIND_SEND_TO_REL_WORKSPACE;
        *out_arg = -1;
        return true;
    }

    if (strcasecmp(cmd_name, "taketonextworkspace") == 0) {
        *out_action = FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE;
        *out_arg = +1;
        return true;
    }
    if (strcasecmp(cmd_name, "taketoprevworkspace") == 0) {
        *out_action = FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE;
        *out_arg = -1;
        return true;
    }

    if (strcasecmp(cmd_name, "raise") == 0) {
        *out_action = FBWL_KEYBIND_RAISE;
        return true;
    }
    if (strcasecmp(cmd_name, "lower") == 0) {
        *out_action = FBWL_KEYBIND_LOWER;
        return true;
    }
    if (strcasecmp(cmd_name, "focus") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS;
        return true;
    }
    if (strcasecmp(cmd_name, "activatetab") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS;
        return true;
    }

    if (strcasecmp(cmd_name, "startmoving") == 0) {
        *out_action = FBWL_KEYBIND_START_MOVING;
        return true;
    }

    if (strcasecmp(cmd_name, "startresizing") == 0) {
        *out_action = FBWL_KEYBIND_START_RESIZING;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }

    if (strcasecmp(cmd_name, "macrocmd") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_MACRO;
        *out_cmd = cmd_args;
        return true;
    }

    return false;
}
