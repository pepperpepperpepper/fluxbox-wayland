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

static bool parse_layer_arg(const char *s, int *out_layer) {
    if (s == NULL || out_layer == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    char *endptr = NULL;
    long n = strtol(s, &endptr, 10);
    if (endptr != s && endptr != NULL) {
        while (*endptr != '\0' && isspace((unsigned char)*endptr)) {
            endptr++;
        }
        if (*endptr == '\0') {
            *out_layer = (int)n;
            return true;
        }
    }

    const char *v = s;
    if (strcasecmp(v, "menu") == 0) {
        *out_layer = 0;
        return true;
    }
    if (strcasecmp(v, "abovedock") == 0) {
        *out_layer = 2;
        return true;
    }
    if (strcasecmp(v, "dock") == 0) {
        *out_layer = 4;
        return true;
    }
    if (strcasecmp(v, "top") == 0) {
        *out_layer = 6;
        return true;
    }
    if (strcasecmp(v, "normal") == 0) {
        *out_layer = 8;
        return true;
    }
    if (strcasecmp(v, "bottom") == 0) {
        *out_layer = 10;
        return true;
    }
    if (strcasecmp(v, "desktop") == 0) {
        *out_layer = 12;
        return true;
    }
    if (strcasecmp(v, "overlay") == 0) {
        *out_layer = 0;
        return true;
    }
    if (strcasecmp(v, "background") == 0) {
        *out_layer = 12;
        return true;
    }
    return false;
}

static bool parse_leading_int(const char *s, int default_value, int *out) {
    if (out == NULL) {
        return false;
    }
    *out = default_value;
    if (s == NULL) {
        return true;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return true;
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) {
        return false;
    }
    if (v < -100000 || v > 100000) {
        return false;
    }
    *out = (int)v;
    return true;
}

static bool args_have_at_least_tokens(const char *args, int min_tokens) {
    if (min_tokens <= 0) {
        return true;
    }
    if (args == NULL) {
        return false;
    }
    int count = 0;
    const char *p = args;
    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        count++;
        if (count >= min_tokens) {
            return true;
        }
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
    }
    return count >= min_tokens;
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

    if (strcasecmp(cmd_name, "setenv") == 0 ||
            strcasecmp(cmd_name, "export") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
            cmd_args++;
        }
        if (*cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_SET_ENV;
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

    if (strcasecmp(cmd_name, "restart") == 0) {
        if (cmd_args != NULL) { while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) { cmd_args++; } }
        *out_action = FBWL_KEYBIND_RESTART;
        if (cmd_args != NULL && *cmd_args != '\0') { *out_cmd = cmd_args; }
        return true;
    }
    if (strcasecmp(cmd_name, "reconfig") == 0 ||
            strcasecmp(cmd_name, "reconfigure") == 0) {
        *out_action = FBWL_KEYBIND_RECONFIGURE;
        return true;
    }

    if (strcasecmp(cmd_name, "reloadstyle") == 0) {
        *out_action = FBWL_KEYBIND_RELOAD_STYLE;
        return true;
    }

    if (strcasecmp(cmd_name, "setstyle") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
            cmd_args++;
        }
        if (*cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_SET_STYLE;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "saverc") == 0) {
        *out_action = FBWL_KEYBIND_SAVE_RC;
        return true;
    }

    if (strcasecmp(cmd_name, "setresourcevalue") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
            cmd_args++;
        }
        if (*cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_SET_RESOURCE_VALUE;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "setresourcevaluedialog") == 0) {
        *out_action = FBWL_KEYBIND_SET_RESOURCE_VALUE_DIALOG;
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
    if (strcasecmp(cmd_name, "nextgroup") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS_NEXT_GROUP;
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
    if (strcasecmp(cmd_name, "prevgroup") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS_PREV_GROUP;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "gotowindow") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
            cmd_args++;
        }
        if (*cmd_args == '\0') {
            return false;
        }
        char *end = NULL;
        long num = strtol(cmd_args, &end, 10);
        if (end == cmd_args) {
            return false;
        }
        while (end != NULL && *end != '\0' && isspace((unsigned char)*end)) {
            end++;
        }
        *out_action = FBWL_KEYBIND_GOTO_WINDOW;
        *out_arg = (int)num;
        if (end != NULL && *end != '\0') {
            *out_cmd = end;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "attach") == 0) {
        *out_action = FBWL_KEYBIND_ATTACH;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "showdesktop") == 0) {
        *out_action = FBWL_KEYBIND_SHOW_DESKTOP;
        return true;
    }
    if (strcasecmp(cmd_name, "arrangewindows") == 0) {
        *out_action = FBWL_KEYBIND_ARRANGE_WINDOWS;
        *out_arg = FBWL_ARRANGE_WINDOWS_UNSPECIFIED;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "arrangewindowsvertical") == 0) {
        *out_action = FBWL_KEYBIND_ARRANGE_WINDOWS;
        *out_arg = FBWL_ARRANGE_WINDOWS_VERTICAL;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "arrangewindowshorizontal") == 0) {
        *out_action = FBWL_KEYBIND_ARRANGE_WINDOWS;
        *out_arg = FBWL_ARRANGE_WINDOWS_HORIZONTAL;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "arrangewindowsstackleft") == 0) {
        *out_action = FBWL_KEYBIND_ARRANGE_WINDOWS;
        *out_arg = FBWL_ARRANGE_WINDOWS_STACK_LEFT;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "arrangewindowsstackright") == 0) {
        *out_action = FBWL_KEYBIND_ARRANGE_WINDOWS;
        *out_arg = FBWL_ARRANGE_WINDOWS_STACK_RIGHT;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "arrangewindowsstacktop") == 0) {
        *out_action = FBWL_KEYBIND_ARRANGE_WINDOWS;
        *out_arg = FBWL_ARRANGE_WINDOWS_STACK_TOP;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "arrangewindowsstackbottom") == 0) {
        *out_action = FBWL_KEYBIND_ARRANGE_WINDOWS;
        *out_arg = FBWL_ARRANGE_WINDOWS_STACK_BOTTOM;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "unclutter") == 0) {
        *out_action = FBWL_KEYBIND_UNCLUTTER;
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
    if (strcasecmp(cmd_name, "movetableft") == 0) {
        *out_action = FBWL_KEYBIND_MOVE_TAB_LEFT;
        return true;
    }
    if (strcasecmp(cmd_name, "movetabright") == 0) {
        *out_action = FBWL_KEYBIND_MOVE_TAB_RIGHT;
        return true;
    }
    if (strcasecmp(cmd_name, "detachclient") == 0) {
        *out_action = FBWL_KEYBIND_DETACH_CLIENT;
        return true;
    }

    if (strcasecmp(cmd_name, "maximize") == 0 ||
            strcasecmp(cmd_name, "maximizewindow") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_MAXIMIZE;
        return true;
    }
    if (strcasecmp(cmd_name, "maximizehorizontal") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_MAXIMIZE_HORIZONTAL;
        return true;
    }
    if (strcasecmp(cmd_name, "maximizevertical") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_MAXIMIZE_VERTICAL;
        return true;
    }
    if (strcasecmp(cmd_name, "fullscreen") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_FULLSCREEN;
        return true;
    }
    if (strcasecmp(cmd_name, "minimize") == 0 ||
            strcasecmp(cmd_name, "minimizewindow") == 0 ||
            strcasecmp(cmd_name, "iconify") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_MINIMIZE;
        return true;
    }
    if (strcasecmp(cmd_name, "deiconify") == 0) {
        *out_action = FBWL_KEYBIND_DEICONIFY;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }

    if (strcasecmp(cmd_name, "markwindow") == 0) {
        *out_action = FBWL_KEYBIND_MARK_WINDOW;
        return true;
    }
    if (strcasecmp(cmd_name, "gotomarkedwindow") == 0) {
        *out_action = FBWL_KEYBIND_GOTO_MARKED_WINDOW;
        return true;
    }

    if (strcasecmp(cmd_name, "close") == 0) {
        *out_action = FBWL_KEYBIND_CLOSE;
        return true;
    }
    if (strcasecmp(cmd_name, "kill") == 0 || strcasecmp(cmd_name, "killwindow") == 0) {
        *out_action = FBWL_KEYBIND_KILL;
        return true;
    }
    if (strcasecmp(cmd_name, "closeallwindows") == 0) {
        *out_action = FBWL_KEYBIND_CLOSE_ALL_WINDOWS;
        return true;
    }

    if (strcasecmp(cmd_name, "shade") == 0 || strcasecmp(cmd_name, "shadewindow") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_SHADE;
        return true;
    }
    if (strcasecmp(cmd_name, "shadeon") == 0) {
        *out_action = FBWL_KEYBIND_SHADE_ON;
        return true;
    }
    if (strcasecmp(cmd_name, "shadeoff") == 0) {
        *out_action = FBWL_KEYBIND_SHADE_OFF;
        return true;
    }

    if (strcasecmp(cmd_name, "stick") == 0 || strcasecmp(cmd_name, "stickwindow") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_STICK;
        return true;
    }
    if (strcasecmp(cmd_name, "stickon") == 0) {
        *out_action = FBWL_KEYBIND_STICK_ON;
        return true;
    }
    if (strcasecmp(cmd_name, "stickoff") == 0) {
        *out_action = FBWL_KEYBIND_STICK_OFF;
        return true;
    }

    if (strcasecmp(cmd_name, "setalpha") == 0) {
        *out_action = FBWL_KEYBIND_SET_ALPHA;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "toggledecor") == 0) {
        *out_action = FBWL_KEYBIND_TOGGLE_DECOR;
        return true;
    }
    if (strcasecmp(cmd_name, "setdecor") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
            cmd_args++;
        }
        if (*cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_SET_DECOR;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "settitle") == 0) {
        *out_action = FBWL_KEYBIND_SET_TITLE;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "settitledialog") == 0) {
        *out_action = FBWL_KEYBIND_SET_TITLE_DIALOG;
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
    if (strcasecmp(cmd_name, "custommenu") == 0) {
        if (cmd_args != NULL) { while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) { cmd_args++; } }
        if (cmd_args == NULL || *cmd_args == '\0') { return false; }
        *out_action = FBWL_KEYBIND_ROOT_MENU;
        *out_cmd = cmd_args;
        return true;
    }
    if (strcasecmp(cmd_name, "workspacemenu") == 0) {
        *out_action = FBWL_KEYBIND_WORKSPACE_MENU;
        return true;
    }

    if (strcasecmp(cmd_name, "addworkspace") == 0) {
        *out_action = FBWL_KEYBIND_ADD_WORKSPACE;
        return true;
    }
    if (strcasecmp(cmd_name, "removelastworkspace") == 0) {
        *out_action = FBWL_KEYBIND_REMOVE_LAST_WORKSPACE;
        return true;
    }
    if (strcasecmp(cmd_name, "setworkspacename") == 0) {
        if (cmd_args != NULL) {
            while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
                cmd_args++;
            }
            *out_cmd = cmd_args;
        }
        *out_action = FBWL_KEYBIND_SET_WORKSPACE_NAME;
        return true;
    }
    if (strcasecmp(cmd_name, "setworkspacenamedialog") == 0) {
        *out_action = FBWL_KEYBIND_SET_WORKSPACE_NAME_DIALOG;
        return true;
    }

    if (strcasecmp(cmd_name, "clientmenu") == 0) {
        *out_action = FBWL_KEYBIND_CLIENT_MENU;
        if (cmd_args != NULL) { while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) { cmd_args++; } }
        if (cmd_args != NULL && *cmd_args != '\0') { *out_cmd = cmd_args; }
        return true;
    }
    if (strcasecmp(cmd_name, "toggletoolbarhidden") == 0 || strcasecmp(cmd_name, "toggletoolbarvisible") == 0) { *out_action = FBWL_KEYBIND_TOGGLE_TOOLBAR_HIDDEN; return true; }
    if (strcasecmp(cmd_name, "toggletoolbarabove") == 0) { *out_action = FBWL_KEYBIND_TOGGLE_TOOLBAR_ABOVE; return true; }
    if (strcasecmp(cmd_name, "toggleslithidden") == 0) { *out_action = FBWL_KEYBIND_TOGGLE_SLIT_HIDDEN; return true; }
    if (strcasecmp(cmd_name, "toggleslitabove") == 0 || strcasecmp(cmd_name, "toggleslitbarabove") == 0) { *out_action = FBWL_KEYBIND_TOGGLE_SLIT_ABOVE; return true; }
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
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_WORKSPACE_NEXT;
        *out_arg = offset;
        return true;
    }
    if (strcasecmp(cmd_name, "prevworkspace") == 0) {
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_WORKSPACE_PREV;
        *out_arg = offset;
        return true;
    }
    if (strcasecmp(cmd_name, "rightworkspace") == 0) {
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_WORKSPACE_NEXT;
        *out_arg = offset;
        *out_cmd = "nowrap";
        return true;
    }
    if (strcasecmp(cmd_name, "leftworkspace") == 0) {
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_WORKSPACE_PREV;
        *out_arg = offset;
        *out_cmd = "nowrap";
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
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_SEND_TO_REL_WORKSPACE;
        *out_arg = offset;
        return true;
    }
    if (strcasecmp(cmd_name, "sendtoprevworkspace") == 0) {
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_SEND_TO_REL_WORKSPACE;
        *out_arg = -offset;
        return true;
    }

    if (strcasecmp(cmd_name, "taketonextworkspace") == 0) {
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE;
        *out_arg = offset;
        return true;
    }
    if (strcasecmp(cmd_name, "taketoprevworkspace") == 0) {
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_TAKE_TO_REL_WORKSPACE;
        *out_arg = -offset;
        return true;
    }

    if (strcasecmp(cmd_name, "sethead") == 0) {
        int head = 1;
        if (!parse_leading_int(cmd_args, 1, &head)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_SET_HEAD;
        *out_arg = head;
        return true;
    }
    if (strcasecmp(cmd_name, "sendtonexthead") == 0) {
        int delta = 1;
        if (!parse_leading_int(cmd_args, 1, &delta)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_SEND_TO_REL_HEAD;
        *out_arg = delta;
        return true;
    }
    if (strcasecmp(cmd_name, "sendtoprevhead") == 0) {
        int delta = 1;
        if (!parse_leading_int(cmd_args, 1, &delta)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_SEND_TO_REL_HEAD;
        *out_arg = -delta;
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
    if (strcasecmp(cmd_name, "raiselayer") == 0) {
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_RAISE_LAYER;
        *out_arg = offset;
        return true;
    }
    if (strcasecmp(cmd_name, "lowerlayer") == 0) {
        int offset = 1;
        if (!parse_leading_int(cmd_args, 1, &offset)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_LOWER_LAYER;
        *out_arg = offset;
        return true;
    }
    if (strcasecmp(cmd_name, "setlayer") == 0) {
        int layer = 0;
        if (!parse_layer_arg(cmd_args, &layer)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_SET_LAYER;
        *out_arg = layer;
        return true;
    }
    if (strcasecmp(cmd_name, "activate") == 0 ||
            strcasecmp(cmd_name, "focus") == 0) {
        if (cmd_args != NULL) {
            while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
                cmd_args++;
            }
            if (*cmd_args != '\0') {
                *out_action = FBWL_KEYBIND_GOTO_WINDOW;
                *out_arg = 1;
                *out_cmd = cmd_args;
                return true;
            }
        }
        *out_action = FBWL_KEYBIND_FOCUS;
        return true;
    }
    if (strcasecmp(cmd_name, "focusleft") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS_DIR;
        *out_arg = FBWL_FOCUS_DIR_LEFT;
        return true;
    }
    if (strcasecmp(cmd_name, "focusright") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS_DIR;
        *out_arg = FBWL_FOCUS_DIR_RIGHT;
        return true;
    }
    if (strcasecmp(cmd_name, "focusup") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS_DIR;
        *out_arg = FBWL_FOCUS_DIR_UP;
        return true;
    }
    if (strcasecmp(cmd_name, "focusdown") == 0) {
        *out_action = FBWL_KEYBIND_FOCUS_DIR;
        *out_arg = FBWL_FOCUS_DIR_DOWN;
        return true;
    }
    if (strcasecmp(cmd_name, "activatetab") == 0) {
        *out_action = FBWL_KEYBIND_TAB_ACTIVATE;
        return true;
    }

    if (strcasecmp(cmd_name, "moveto") == 0) {
        if (!args_have_at_least_tokens(cmd_args, 2)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_MOVE_TO;
        *out_cmd = cmd_args;
        return true;
    }
    if (strcasecmp(cmd_name, "move") == 0) {
        *out_action = FBWL_KEYBIND_MOVE_REL;
        *out_arg = 0;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "moveright") == 0) {
        *out_action = FBWL_KEYBIND_MOVE_REL;
        *out_arg = 1;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "moveleft") == 0) {
        *out_action = FBWL_KEYBIND_MOVE_REL;
        *out_arg = 2;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "moveup") == 0) {
        *out_action = FBWL_KEYBIND_MOVE_REL;
        *out_arg = 3;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }
    if (strcasecmp(cmd_name, "movedown") == 0) {
        *out_action = FBWL_KEYBIND_MOVE_REL;
        *out_arg = 4;
        if (cmd_args != NULL && *cmd_args != '\0') {
            *out_cmd = cmd_args;
        }
        return true;
    }

    if (strcasecmp(cmd_name, "resizeto") == 0) {
        if (!args_have_at_least_tokens(cmd_args, 2)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_RESIZE_TO;
        *out_cmd = cmd_args;
        return true;
    }
    if (strcasecmp(cmd_name, "resize") == 0) {
        if (!args_have_at_least_tokens(cmd_args, 2)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_RESIZE_REL;
        *out_arg = 0;
        *out_cmd = cmd_args;
        return true;
    }
    if (strcasecmp(cmd_name, "resizehorizontal") == 0) {
        if (!args_have_at_least_tokens(cmd_args, 1)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_RESIZE_REL;
        *out_arg = 1;
        *out_cmd = cmd_args;
        return true;
    }
    if (strcasecmp(cmd_name, "resizevertical") == 0) {
        if (!args_have_at_least_tokens(cmd_args, 1)) {
            return false;
        }
        *out_action = FBWL_KEYBIND_RESIZE_REL;
        *out_arg = 2;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "setxprop") == 0) {
        if (cmd_args == NULL) {
            return false;
        }
        while (*cmd_args != '\0' && isspace((unsigned char)*cmd_args)) {
            cmd_args++;
        }
        if (*cmd_args == '\0' || cmd_args[1] == '\0' || *cmd_args == '=') {
            return false;
        }
        *out_action = FBWL_KEYBIND_SET_XPROP;
        *out_cmd = cmd_args;
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
    if (strcasecmp(cmd_name, "starttabbing") == 0) {
        *out_action = FBWL_KEYBIND_START_TABBING;
        return true;
    }

    if (strcasecmp(cmd_name, "if") == 0 || strcasecmp(cmd_name, "cond") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_IF;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "foreach") == 0 || strcasecmp(cmd_name, "map") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_FOREACH;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "togglecmd") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_TOGGLECMD;
        *out_cmd = cmd_args;
        return true;
    }

    if (strcasecmp(cmd_name, "delay") == 0) {
        if (cmd_args == NULL || *cmd_args == '\0') {
            return false;
        }
        *out_action = FBWL_KEYBIND_DELAY;
        *out_cmd = cmd_args;
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
