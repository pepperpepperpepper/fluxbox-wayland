#include "wayland/fbwl_server_keybinding_actions.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_style_parse.h"
#include "wayland/fbwl_view.h"

struct init_update {
    const char *key;
    const char *value;
};

static char *trim_inplace(char *s) {
    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static bool parse_key_from_line(const char *line, const char **out_key, size_t *out_key_len) {
    if (out_key != NULL) {
        *out_key = NULL;
    }
    if (out_key_len != NULL) {
        *out_key_len = 0;
    }
    if (line == NULL) {
        return false;
    }
    const char *s = line;
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0' || *s == '#' || *s == '!') {
        return false;
    }
    const char *colon = strchr(s, ':');
    if (colon == NULL) {
        return false;
    }
    const char *end = colon;
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    if (end == s) {
        return false;
    }
    if (out_key != NULL) {
        *out_key = s;
    }
    if (out_key_len != NULL) {
        *out_key_len = (size_t)(end - s);
    }
    return true;
}

static char *format_kv_line(const char *key, const char *value) {
    if (key == NULL || *key == '\0' || value == NULL) {
        return NULL;
    }
    const size_t key_len = strlen(key);
    const size_t value_len = strlen(value);
    const size_t needed = key_len + 2 + value_len + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }
    int n = snprintf(out, needed, "%s: %s", key, value);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

static void free_lines(char **lines, size_t len) {
    if (lines == NULL) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        free(lines[i]);
    }
    free(lines);
}

static bool load_lines(const char *path, char ***out_lines, size_t *out_len) {
    if (out_lines == NULL || out_len == NULL) {
        return false;
    }
    *out_lines = NULL;
    *out_len = 0;

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        if (errno == ENOENT) {
            return true;
        }
        wlr_log(WLR_ERROR, "Init: failed to open %s: %s", path, strerror(errno));
        return false;
    }

    char **lines = NULL;
    size_t lines_len = 0;
    size_t lines_cap = 0;
    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    while ((nread = getline(&line, &cap, f)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }
        char *dup = strdup(line);
        if (dup == NULL) {
            free(line);
            fclose(f);
            free_lines(lines, lines_len);
            return false;
        }
        if (lines_len >= lines_cap) {
            size_t new_cap = lines_cap > 0 ? (lines_cap * 2) : 32;
            char **tmp = realloc(lines, new_cap * sizeof(*tmp));
            if (tmp == NULL) {
                free(dup);
                free(line);
                fclose(f);
                free_lines(lines, lines_len);
                return false;
            }
            lines = tmp;
            lines_cap = new_cap;
        }
        lines[lines_len++] = dup;
    }
    free(line);
    fclose(f);

    *out_lines = lines;
    *out_len = lines_len;
    return true;
}

static bool write_lines_atomic(const char *path, char **lines, size_t len) {
    if (path == NULL || *path == '\0') {
        return false;
    }

    mode_t mode = 0644;
    struct stat st = {0};
    if (stat(path, &st) == 0) {
        mode = st.st_mode & 0777;
    }

    const size_t path_len = strlen(path);
    const char *suffix = ".tmpXXXXXX";
    const size_t tmp_len = path_len + strlen(suffix) + 1;
    char *tmp_path = malloc(tmp_len);
    if (tmp_path == NULL) {
        return false;
    }
    int n = snprintf(tmp_path, tmp_len, "%s%s", path, suffix);
    if (n < 0 || (size_t)n >= tmp_len) {
        free(tmp_path);
        return false;
    }

    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        wlr_log(WLR_ERROR, "Init: mkstemp failed path=%s: %s", tmp_path, strerror(errno));
        free(tmp_path);
        return false;
    }
    (void)fchmod(fd, mode);

    FILE *f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        unlink(tmp_path);
        free(tmp_path);
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < len; i++) {
        const char *line = lines[i] != NULL ? lines[i] : "";
        if (fprintf(f, "%s\n", line) < 0) {
            ok = false;
            break;
        }
    }

    if (ok) {
        ok = fflush(f) == 0;
    }
    if (ok) {
        ok = fsync(fd) == 0;
    }
    ok = fclose(f) == 0 && ok;

    if (!ok) {
        unlink(tmp_path);
        free(tmp_path);
        return false;
    }

    if (rename(tmp_path, path) != 0) {
        wlr_log(WLR_ERROR, "Init: rename failed tmp=%s dst=%s: %s", tmp_path, path, strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return false;
    }

    free(tmp_path);
    return true;
}

static bool apply_updates_to_lines(char ***io_lines, size_t *io_len, const struct init_update *updates,
        size_t updates_len) {
    if (io_lines == NULL || io_len == NULL || updates == NULL) {
        return false;
    }

    bool *found = calloc(updates_len, sizeof(*found));
    if (found == NULL) {
        return false;
    }

    char **lines = *io_lines;
    size_t len = *io_len;

    for (size_t i = 0; i < len; i++) {
        const char *key = NULL;
        size_t key_len = 0;
        if (!parse_key_from_line(lines[i], &key, &key_len)) {
            continue;
        }

        for (size_t j = 0; j < updates_len; j++) {
            const char *update_key = updates[j].key;
            const char *update_value = updates[j].value;
            if (update_key == NULL || *update_key == '\0' || update_value == NULL) {
                continue;
            }
            if (strlen(update_key) != key_len) {
                continue;
            }
            if (strncasecmp(key, update_key, key_len) != 0) {
                continue;
            }

            char *repl = format_kv_line(update_key, update_value);
            if (repl == NULL) {
                free(found);
                return false;
            }
            free(lines[i]);
            lines[i] = repl;
            found[j] = true;
        }
    }

    size_t missing = 0;
    for (size_t j = 0; j < updates_len; j++) {
        if (updates[j].key == NULL || *updates[j].key == '\0' || updates[j].value == NULL) {
            continue;
        }
        if (!found[j]) {
            missing++;
        }
    }

    if (missing > 0) {
        char **tmp = realloc(lines, (len + missing) * sizeof(*tmp));
        if (tmp == NULL) {
            free(found);
            return false;
        }
        lines = tmp;
        for (size_t j = 0; j < updates_len; j++) {
            if (updates[j].key == NULL || *updates[j].key == '\0' || updates[j].value == NULL) {
                continue;
            }
            if (found[j]) {
                continue;
            }
            char *repl = format_kv_line(updates[j].key, updates[j].value);
            if (repl == NULL) {
                free(found);
                return false;
            }
            lines[len++] = repl;
        }
    }

    *io_lines = lines;
    *io_len = len;
    free(found);
    return true;
}

static bool init_update_file(const char *config_dir, const struct init_update *updates, size_t updates_len) {
    if (config_dir == NULL || *config_dir == '\0' || updates == NULL || updates_len == 0) {
        return false;
    }

    char *path = fbwl_path_join(config_dir, "init");
    if (path == NULL) {
        return false;
    }

    char **lines = NULL;
    size_t len = 0;
    if (!load_lines(path, &lines, &len)) {
        free(path);
        return false;
    }

    if (!apply_updates_to_lines(&lines, &len, updates, updates_len)) {
        free_lines(lines, len);
        free(path);
        return false;
    }

    const bool ok = write_lines_atomic(path, lines, len);
    if (!ok) {
        wlr_log(WLR_ERROR, "Init: failed to write %s", path);
    } else {
        wlr_log(WLR_INFO, "Init: updated %s", path);
    }
    free_lines(lines, len);
    free(path);
    return ok;
}

static void server_apply_style_theme(struct fbwl_server *server, const struct fbwl_decor_theme *theme,
        const char *why) {
    if (server == NULL || theme == NULL) {
        return;
    }
    server->decor_theme = *theme;
    server_toolbar_ui_rebuild(server);
    server_slit_ui_rebuild(server);
    for (struct fbwm_view *wm_view = server->wm.views.next; wm_view != &server->wm.views; wm_view = wm_view->next) {
        struct fbwl_view *view = wm_view->userdata;
        if (view != NULL) {
            fbwl_view_decor_update(view, &server->decor_theme);
        }
    }
    wlr_log(WLR_INFO, "Style: applied reason=%s", why != NULL ? why : "(unknown)");
}

static bool server_load_style_theme(struct fbwl_server *server, const char *path, struct fbwl_decor_theme *out) {
    if (server == NULL || out == NULL) {
        return false;
    }
    decor_theme_set_defaults(out);
    if (path == NULL || *path == '\0') {
        return true;
    }
    return fbwl_style_load_file(out, path);
}

void server_keybindings_reload_style(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }

    const char *style_file = server->style_file;
    struct fbwl_decor_theme theme = {0};
    if (!server_load_style_theme(server, style_file, &theme)) {
        wlr_log(WLR_ERROR, "Style: reload failed path=%s", style_file != NULL ? style_file : "(null)");
        return;
    }

    const char *overlay = server->style_overlay_file;
    if (overlay != NULL && fbwl_file_exists(overlay)) {
        (void)fbwl_style_load_file(&theme, overlay);
    }

    server_apply_style_theme(server, &theme, "reloadstyle");
    server_keybindings_save_rc(server);
}

void server_keybindings_set_style(void *userdata, const char *path) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return;
    }
    if (path == NULL) {
        wlr_log(WLR_ERROR, "Style: setstyle missing path");
        return;
    }

    char *tmp = strdup(path);
    if (tmp == NULL) {
        wlr_log(WLR_ERROR, "Style: setstyle OOM");
        return;
    }
    char *trimmed = trim_inplace(tmp);
    if (trimmed == NULL || *trimmed == '\0') {
        free(tmp);
        wlr_log(WLR_ERROR, "Style: setstyle missing path");
        return;
    }

    char *resolved = fbwl_resolve_config_path(server->config_dir, trimmed);
    if (resolved == NULL) {
        resolved = strdup(trimmed);
    }
    free(tmp);
    if (resolved == NULL || *resolved == '\0') {
        free(resolved);
        wlr_log(WLR_ERROR, "Style: setstyle invalid path");
        return;
    }

    struct fbwl_decor_theme theme = {0};
    if (!server_load_style_theme(server, resolved, &theme)) {
        wlr_log(WLR_ERROR, "Style: setstyle failed path=%s", resolved);
        free(resolved);
        return;
    }

    const char *overlay = server->style_overlay_file;
    if (overlay != NULL && fbwl_file_exists(overlay)) {
        (void)fbwl_style_load_file(&theme, overlay);
    }

    free(server->style_file);
    server->style_file = resolved;
    server->style_file_override = false;
    server_apply_style_theme(server, &theme, "setstyle");

    if (server->config_dir != NULL && *server->config_dir != '\0') {
        struct init_update updates[] = {
            {.key = "session.styleFile", .value = server->style_file != NULL ? server->style_file : ""},
        };
        (void)init_update_file(server->config_dir, updates, sizeof(updates) / sizeof(updates[0]));
    }

    server_keybindings_save_rc(server);
}

static char *workspace_names_to_csv(const struct fbwm_core *wm) {
    if (wm == NULL) {
        return NULL;
    }

    const size_t names_len = fbwm_core_workspace_names_len(wm);
    if (names_len == 0) {
        return strdup("");
    }

    int workspaces = fbwm_core_workspace_count(wm);
    if (workspaces < 1) {
        workspaces = 1;
    }
    size_t n = (size_t)workspaces;
    if (names_len > n) {
        n = names_len;
    }
    if (n > 1000) {
        n = 1000;
    }

    size_t total = 1;
    for (size_t i = 0; i < n; i++) {
        const char *name = fbwm_core_workspace_name(wm, (int)i);
        if (name == NULL || *name == '\0') {
            char tmp[128];
            snprintf(tmp, sizeof(tmp), "Workspace %zu", i + 1);
            total += strlen(tmp) + 1;
        } else {
            total += strlen(name) + 1;
        }
    }

    char *buf = malloc(total);
    if (buf == NULL) {
        return NULL;
    }
    size_t off = 0;
    for (size_t i = 0; i < n; i++) {
        const char *name = fbwm_core_workspace_name(wm, (int)i);
        char tmp[128];
        if (name == NULL || *name == '\0') {
            snprintf(tmp, sizeof(tmp), "Workspace %zu", i + 1);
            name = tmp;
        }
        const size_t len = strlen(name);
        memcpy(buf + off, name, len);
        off += len;
        buf[off++] = ',';
    }
    buf[off] = '\0';
    return buf;
}

void server_keybindings_save_rc(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->config_dir == NULL || *server->config_dir == '\0') {
        wlr_log(WLR_ERROR, "SaveRC: missing config_dir");
        return;
    }

    char ws_buf[32];
    snprintf(ws_buf, sizeof(ws_buf), "%d", fbwm_core_workspace_count(&server->wm));

    char *ws_names = workspace_names_to_csv(&server->wm);
    if (ws_names == NULL) {
        ws_names = strdup("");
    }

    char auto_raise_delay_buf[32];
    snprintf(auto_raise_delay_buf, sizeof(auto_raise_delay_buf), "%d", server->focus.auto_raise_delay_ms);

    char no_focus_while_typing_buf[32];
    snprintf(no_focus_while_typing_buf, sizeof(no_focus_while_typing_buf), "%d", server->focus.no_focus_while_typing_delay_ms);

    char demands_attention_buf[32];
    snprintf(demands_attention_buf, sizeof(demands_attention_buf), "%d", server->focus.demands_attention_timeout_ms);

    struct init_update updates[] = {
        {.key = "session.screen0.workspaces", .value = ws_buf},
        {.key = "session.screen0.workspaceNames", .value = ws_names != NULL ? ws_names : ""},
        {.key = "session.keyFile", .value = server->keys_file != NULL ? server->keys_file : ""},
        {.key = "session.appsFile", .value = server->apps_file != NULL ? server->apps_file : ""},
        {.key = "session.styleFile", .value = server->style_file != NULL ? server->style_file : ""},
        {.key = "session.styleOverlay", .value = server->style_overlay_file != NULL ? server->style_overlay_file : ""},
        {.key = "session.menuFile", .value = server->menu_file != NULL ? server->menu_file : ""},
        {.key = "session.screen0.windowMenu", .value = server->window_menu_file != NULL ? server->window_menu_file : ""},
        {.key = "session.slitlistFile", .value = server->slitlist_file != NULL ? server->slitlist_file : ""},
        {.key = "session.screen0.focusModel", .value = fbwl_focus_model_str(server->focus.model)},
        {.key = "session.screen0.autoRaise", .value = server->focus.auto_raise ? "true" : "false"},
        {.key = "session.autoRaiseDelay", .value = auto_raise_delay_buf},
        {.key = "session.screen0.clickRaises", .value = server->focus.click_raises ? "true" : "false"},
        {.key = "session.screen0.focusNewWindows", .value = server->focus.focus_new_windows ? "true" : "false"},
        {.key = "session.screen0.noFocusWhileTypingDelay", .value = no_focus_while_typing_buf},
        {.key = "session.screen0.focusSameHead", .value = server->focus.focus_same_head ? "true" : "false"},
        {.key = "session.screen0.demandsAttentionTimeout", .value = demands_attention_buf},
        {.key = "session.screen0.allowRemoteActions", .value = server->focus.allow_remote_actions ? "true" : "false"},
    };

    if (init_update_file(server->config_dir, updates, sizeof(updates) / sizeof(updates[0]))) {
        wlr_log(WLR_INFO, "SaveRC: ok");
    } else {
        wlr_log(WLR_ERROR, "SaveRC: failed");
    }

    free(ws_names);
}

void server_keybindings_set_resource_value(void *userdata, const char *args) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->config_dir == NULL || *server->config_dir == '\0') {
        wlr_log(WLR_ERROR, "SetResourceValue: missing config_dir");
        return;
    }
    if (args == NULL) {
        wlr_log(WLR_ERROR, "SetResourceValue: missing args");
        return;
    }

    char *tmp = strdup(args);
    if (tmp == NULL) {
        wlr_log(WLR_ERROR, "SetResourceValue: OOM");
        return;
    }
    char *s = trim_inplace(tmp);
    if (s == NULL || *s == '\0') {
        free(tmp);
        wlr_log(WLR_ERROR, "SetResourceValue: missing resource name");
        return;
    }

    char *p = s;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        free(tmp);
        wlr_log(WLR_ERROR, "SetResourceValue: missing value");
        return;
    }
    *p = '\0';
    const char *key = s;
    char *value = trim_inplace(p + 1);
    if (value == NULL) {
        value = "";
    }

    struct init_update updates[] = {
        {.key = key, .value = value},
    };
    (void)init_update_file(server->config_dir, updates, sizeof(updates) / sizeof(updates[0]));
    free(tmp);

    wlr_log(WLR_INFO, "SetResourceValue: %s", key);
    server_reconfigure(server);
    server_keybindings_save_rc(server);
}

static bool cmd_dialog_submit_set_resource_value(void *userdata, const char *text) {
    server_keybindings_set_resource_value(userdata, text);
    return true;
}

void server_keybindings_set_resource_value_dialog(void *userdata) {
    struct fbwl_server *server = userdata;
    if (server == NULL || server->scene == NULL || server->output_layout == NULL) {
        return;
    }

    server_menu_ui_close(server, "set-resource-value-dialog");
    fbwl_ui_cmd_dialog_open_prompt(&server->cmd_dialog_ui, server->scene, server->layer_overlay,
        &server->decor_theme, server->output_layout, "SetResourceValue ", "",
        cmd_dialog_submit_set_resource_value, server);
}
