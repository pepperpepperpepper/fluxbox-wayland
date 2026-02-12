#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_util.h"

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

static bool strip_outer_quotes_inplace(char *s) {
    if (s == NULL) {
        return false;
    }
    const size_t len = strlen(s);
    if (len < 2) {
        return false;
    }
    if (!((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\''))) {
        return false;
    }
    memmove(s, s + 1, len - 1);
    s[len - 2] = '\0';
    return true;
}

char *fbwl_path_join(const char *dir, const char *rel) {
    if (dir == NULL || *dir == '\0' || rel == NULL || *rel == '\0') {
        return NULL;
    }

    size_t dir_len = strlen(dir);
    size_t rel_len = strlen(rel);
    const bool need_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    size_t needed = dir_len + (need_slash ? 1 : 0) + rel_len + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }

    int n = snprintf(out, needed, need_slash ? "%s/%s" : "%s%s", dir, rel);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

bool fbwl_file_exists(const char *path) {
    return path != NULL && *path != '\0' && access(path, R_OK) == 0;
}

static char *expand_tilde(const char *path) {
    if (path == NULL || *path == '\0') {
        return NULL;
    }
    if (path[0] != '~') {
        return strdup(path);
    }

    const char *home = getenv("HOME");
    if (home == NULL || *home == '\0') {
        return strdup(path);
    }

    const char *tail = path + 1;
    if (*tail == '\0') {
        return strdup(home);
    }
    if (*tail != '/') {
        return strdup(path);
    }

    size_t home_len = strlen(home);
    size_t tail_len = strlen(tail);
    size_t needed = home_len + tail_len + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }
    int n = snprintf(out, needed, "%s%s", home, tail);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

char *fbwl_resolve_config_path(const char *config_dir, const char *value) {
    if (value == NULL) {
        return NULL;
    }

    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }
    if (*value == '\0') {
        return NULL;
    }

    char *tmp = strdup(value);
    if (tmp == NULL) {
        return NULL;
    }
    char *s = trim_inplace(tmp);
    if (s == NULL || *s == '\0') {
        free(tmp);
        return NULL;
    }

    const size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '\'' && s[len - 1] == '\'') || (s[0] == '"' && s[len - 1] == '"'))) {
        s[len - 1] = '\0';
        s++;
    }

    char *expanded = expand_tilde(s);
    free(tmp);
    if (expanded == NULL) {
        return NULL;
    }

    if (expanded[0] == '/' || config_dir == NULL || *config_dir == '\0') {
        return expanded;
    }

    char *joined = fbwl_path_join(config_dir, expanded);
    free(expanded);
    return joined;
}

static bool resource_db_reserve(struct fbwl_resource_db *db, size_t n) {
    if (db == NULL) {
        return false;
    }
    if (n <= db->items_cap) {
        return true;
    }
    size_t new_cap = db->items_cap > 0 ? db->items_cap : 16;
    while (new_cap < n) {
        new_cap *= 2;
    }

    void *p = realloc(db->items, new_cap * sizeof(db->items[0]));
    if (p == NULL) {
        return false;
    }
    db->items = p;
    db->items_cap = new_cap;
    return true;
}

static bool resource_db_set_owned(struct fbwl_resource_db *db, char *key, char *value) {
    if (db == NULL || key == NULL || *key == '\0' || value == NULL) {
        free(key);
        free(value);
        return false;
    }

    for (size_t i = 0; i < db->items_len; i++) {
        if (strcasecmp(db->items[i].key, key) == 0) {
            free(db->items[i].value);
            db->items[i].value = value;
            free(key);
            return true;
        }
    }

    if (!resource_db_reserve(db, db->items_len + 1)) {
        free(key);
        free(value);
        return false;
    }

    db->items[db->items_len].key = key;
    db->items[db->items_len].value = value;
    db->items_len++;
    return true;
}

void fbwl_resource_db_free(struct fbwl_resource_db *db) {
    if (db == NULL) {
        return;
    }
    for (size_t i = 0; i < db->items_len; i++) {
        free(db->items[i].key);
        free(db->items[i].value);
    }
    free(db->items);
    db->items = NULL;
    db->items_len = 0;
    db->items_cap = 0;
}

static bool fbwl_resource_db_load_file(struct fbwl_resource_db *db, const char *path) {
    if (db == NULL || path == NULL || *path == '\0') {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Init: failed to open %s: %s", path, strerror(errno));
        return false;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t nread;
    while ((nread = getline(&line, &cap, f)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        char *s = trim_inplace(line);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (*s == '#' || *s == '!') {
            continue;
        }

        char *sep = strchr(s, ':');
        if (sep == NULL) {
            continue;
        }
        *sep = '\0';
        char *key = trim_inplace(s);
        char *val = trim_inplace(sep + 1);
        if (key == NULL || *key == '\0' || val == NULL || *val == '\0') {
            continue;
        }

        char *key_dup = strdup(key);
        char *val_dup = strdup(val);
        if (key_dup == NULL || val_dup == NULL) {
            free(key_dup);
            free(val_dup);
            continue;
        }
        strip_outer_quotes_inplace(val_dup);
        (void)resource_db_set_owned(db, key_dup, val_dup);
    }

    free(line);
    fclose(f);
    return true;
}

bool fbwl_resource_db_load_init(struct fbwl_resource_db *db, const char *config_dir) {
    if (db == NULL) {
        return false;
    }
    fbwl_resource_db_free(db);

    char *path = fbwl_path_join(config_dir, "init");
    if (path == NULL) {
        return false;
    }
    if (!fbwl_file_exists(path)) {
        free(path);
        return false;
    }

    const bool ok = fbwl_resource_db_load_file(db, path);
    if (ok) {
        wlr_log(WLR_INFO, "Init: loaded %s", path);
    }
    free(path);
    return ok;
}

const char *fbwl_resource_db_get(const struct fbwl_resource_db *db, const char *key) {
    if (db == NULL || key == NULL || *key == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < db->items_len; i++) {
        if (strcasecmp(db->items[i].key, key) == 0) {
            return db->items[i].value;
        }
    }
    return NULL;
}

size_t fbwl_resource_db_max_screen_index(const struct fbwl_resource_db *db) {
    if (db == NULL) {
        return 0;
    }
    size_t max_idx = 0;
    const char *prefix = "session.screen";
    const size_t prefix_len = strlen(prefix);
    for (size_t i = 0; i < db->items_len; i++) {
        const char *k = db->items[i].key;
        if (k == NULL) {
            continue;
        }
        if (strncasecmp(k, prefix, prefix_len) != 0) {
            continue;
        }
        const char *p = k + prefix_len;
        if (*p == '\0' || !isdigit((unsigned char)*p)) {
            continue;
        }
        unsigned long v = 0;
        const char *digits = p;
        while (*p != '\0' && isdigit((unsigned char)*p)) {
            v = v * 10 + (unsigned long)(*p - '0');
            p++;
            if (v > 1000000ul) {
                break;
            }
        }
        if (p == digits || *p != '.') {
            continue;
        }
        if (v > max_idx) {
            max_idx = (size_t)v;
        }
    }
    return max_idx;
}

static bool screen_key(char buf[static 256], size_t screen, const char *suffix) {
    if (buf == NULL || suffix == NULL || *suffix == '\0') {
        return false;
    }
    int n = snprintf(buf, 256, "session.screen%zu.%s", screen, suffix);
    return n > 0 && n < 256;
}

const char *fbwl_resource_db_get_screen(const struct fbwl_resource_db *db, size_t screen, const char *suffix) {
    if (db == NULL || suffix == NULL || *suffix == '\0') {
        return NULL;
    }

    char key[256];
    if (!screen_key(key, screen, suffix)) {
        return NULL;
    }
    const char *val = fbwl_resource_db_get(db, key);
    if (val != NULL || screen == 0) {
        return val;
    }

    if (!screen_key(key, 0, suffix)) {
        return NULL;
    }
    return fbwl_resource_db_get(db, key);
}

static bool parse_bool(const char *s, bool *out) {
    if (s == NULL || out == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    if (strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 || strcasecmp(s, "on") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(s, "false") == 0 || strcasecmp(s, "no") == 0 || strcasecmp(s, "off") == 0) {
        *out = false;
        return true;
    }

    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end != s && end != NULL) {
        while (*end != '\0' && isspace((unsigned char)*end)) {
            end++;
        }
        if (*end == '\0') {
            *out = v != 0;
            return true;
        }
    }

    return false;
}

bool fbwl_resource_db_get_bool(const struct fbwl_resource_db *db, const char *key, bool *out) {
    return parse_bool(fbwl_resource_db_get(db, key), out);
}

bool fbwl_resource_db_get_screen_bool(const struct fbwl_resource_db *db, size_t screen, const char *suffix, bool *out) {
    return parse_bool(fbwl_resource_db_get_screen(db, screen, suffix), out);
}

bool fbwl_resource_db_get_int(const struct fbwl_resource_db *db, const char *key, int *out) {
    if (out == NULL) {
        return false;
    }
    const char *s = fbwl_resource_db_get(db, key);
    if (s == NULL) {
        return false;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || end == NULL) {
        return false;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }
    *out = (int)v;
    return true;
}

bool fbwl_resource_db_get_screen_int(const struct fbwl_resource_db *db, size_t screen, const char *suffix, int *out) {
    if (out == NULL) {
        return false;
    }
    const char *s = fbwl_resource_db_get_screen(db, screen, suffix);
    if (s == NULL) {
        return false;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || end == NULL) {
        return false;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }
    *out = (int)v;
    return true;
}

bool fbwl_resource_db_get_color(const struct fbwl_resource_db *db, const char *key, float rgba[static 4]) {
    return fbwl_parse_hex_color(fbwl_resource_db_get(db, key), rgba);
}

char *fbwl_resource_db_resolve_path(const struct fbwl_resource_db *db, const char *config_dir, const char *key) {
    const char *val = fbwl_resource_db_get(db, key);
    if (val == NULL || *val == '\0') {
        return NULL;
    }
    return fbwl_resolve_config_path(config_dir, val);
}

char *fbwl_resource_db_discover_path(const struct fbwl_resource_db *db, const char *config_dir, const char *key,
        const char *fallback_rel) {
    char *resolved = fbwl_resource_db_resolve_path(db, config_dir, key);
    if (resolved != NULL) {
        return resolved;
    }
    if (fallback_rel == NULL || config_dir == NULL || *config_dir == '\0') {
        return NULL;
    }
    char *joined = fbwl_path_join(config_dir, fallback_rel);
    if (joined != NULL && !fbwl_file_exists(joined)) {
        free(joined);
        joined = NULL;
    }
    return joined;
}

enum fbwl_focus_model fbwl_parse_focus_model(const char *s) {
    if (s == NULL) {
        return FBWL_FOCUS_MODEL_CLICK_TO_FOCUS;
    }
    if (strcasecmp(s, "ClickToFocus") == 0 || strcasecmp(s, "ClickFocus") == 0) {
        return FBWL_FOCUS_MODEL_CLICK_TO_FOCUS;
    }
    if (strcasecmp(s, "MouseFocus") == 0 || strcasecmp(s, "SloppyFocus") == 0 || strcasecmp(s, "SemiSloppyFocus") == 0) {
        return FBWL_FOCUS_MODEL_MOUSE_FOCUS;
    }
    if (strcasecmp(s, "StrictMouseFocus") == 0) {
        return FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS;
    }
    return FBWL_FOCUS_MODEL_CLICK_TO_FOCUS;
}

const char *fbwl_focus_model_str(enum fbwl_focus_model model) {
    switch (model) {
    case FBWL_FOCUS_MODEL_CLICK_TO_FOCUS:
        return "ClickToFocus";
    case FBWL_FOCUS_MODEL_MOUSE_FOCUS:
        return "MouseFocus";
    case FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS:
        return "StrictMouseFocus";
    default:
        return "ClickToFocus";
    }
}

enum fbwl_wallpaper_mode fbwl_wallpaper_mode_parse(const char *s) {
    if (s == NULL) {
        return FBWL_WALLPAPER_MODE_STRETCH;
    }
    if (strcasecmp(s, "stretch") == 0 || strcasecmp(s, "scale") == 0 || strcasecmp(s, "full") == 0) {
        return FBWL_WALLPAPER_MODE_STRETCH;
    }
    if (strcasecmp(s, "fill") == 0 || strcasecmp(s, "aspect") == 0 || strcasecmp(s, "cover") == 0) {
        return FBWL_WALLPAPER_MODE_FILL;
    }
    if (strcasecmp(s, "center") == 0) {
        return FBWL_WALLPAPER_MODE_CENTER;
    }
    if (strcasecmp(s, "tile") == 0) {
        return FBWL_WALLPAPER_MODE_TILE;
    }
    return FBWL_WALLPAPER_MODE_STRETCH;
}

const char *fbwl_wallpaper_mode_str(enum fbwl_wallpaper_mode mode) {
    switch (mode) {
    case FBWL_WALLPAPER_MODE_STRETCH:
        return "stretch";
    case FBWL_WALLPAPER_MODE_FILL:
        return "fill";
    case FBWL_WALLPAPER_MODE_CENTER:
        return "center";
    case FBWL_WALLPAPER_MODE_TILE:
        return "tile";
    default:
        return "stretch";
    }
}

enum fbwm_window_placement_strategy fbwl_parse_window_placement(const char *s) {
    if (s == NULL) {
        return FBWM_PLACE_ROW_SMART;
    }
    if (strcasecmp(s, "RowSmartPlacement") == 0) {
        return FBWM_PLACE_ROW_SMART;
    }
    if (strcasecmp(s, "ColSmartPlacement") == 0) {
        return FBWM_PLACE_COL_SMART;
    }
    if (strcasecmp(s, "CascadePlacement") == 0) {
        return FBWM_PLACE_CASCADE;
    }
    if (strcasecmp(s, "UnderMousePlacement") == 0) {
        return FBWM_PLACE_UNDER_MOUSE;
    }
    if (strcasecmp(s, "RowMinOverlapPlacement") == 0) {
        return FBWM_PLACE_ROW_MIN_OVERLAP;
    }
    if (strcasecmp(s, "ColMinOverlapPlacement") == 0) {
        return FBWM_PLACE_COL_MIN_OVERLAP;
    }
    if (strcasecmp(s, "AutotabPlacement") == 0) {
        return FBWM_PLACE_AUTOTAB;
    }
    return FBWM_PLACE_ROW_SMART;
}

const char *fbwl_window_placement_str(enum fbwm_window_placement_strategy placement) {
    switch (placement) {
    case FBWM_PLACE_ROW_SMART:
        return "RowSmartPlacement";
    case FBWM_PLACE_COL_SMART:
        return "ColSmartPlacement";
    case FBWM_PLACE_CASCADE:
        return "CascadePlacement";
    case FBWM_PLACE_UNDER_MOUSE:
        return "UnderMousePlacement";
    case FBWM_PLACE_ROW_MIN_OVERLAP:
        return "RowMinOverlapPlacement";
    case FBWM_PLACE_COL_MIN_OVERLAP:
        return "ColMinOverlapPlacement";
    case FBWM_PLACE_AUTOTAB:
        return "AutotabPlacement";
    default:
        return "RowSmartPlacement";
    }
}

enum fbwl_toolbar_placement fbwl_parse_toolbar_placement(const char *s) {
    if (s == NULL) {
        return FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER;
    }
    if (strcasecmp(s, "BottomLeft") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT;
    }
    if (strcasecmp(s, "BottomCenter") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER;
    }
    if (strcasecmp(s, "BottomRight") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT;
    }
    if (strcasecmp(s, "LeftBottom") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM;
    }
    if (strcasecmp(s, "LeftCenter") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER;
    }
    if (strcasecmp(s, "LeftTop") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_LEFT_TOP;
    }
    if (strcasecmp(s, "RightBottom") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM;
    }
    if (strcasecmp(s, "RightCenter") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER;
    }
    if (strcasecmp(s, "RightTop") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP;
    }
    if (strcasecmp(s, "TopLeft") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_TOP_LEFT;
    }
    if (strcasecmp(s, "TopCenter") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_TOP_CENTER;
    }
    if (strcasecmp(s, "TopRight") == 0) {
        return FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT;
    }
    return FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER;
}

const char *fbwl_toolbar_placement_str(enum fbwl_toolbar_placement placement) {
    switch (placement) {
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT:
        return "BottomLeft";
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER:
        return "BottomCenter";
    case FBWL_TOOLBAR_PLACEMENT_BOTTOM_RIGHT:
        return "BottomRight";
    case FBWL_TOOLBAR_PLACEMENT_LEFT_BOTTOM:
        return "LeftBottom";
    case FBWL_TOOLBAR_PLACEMENT_LEFT_CENTER:
        return "LeftCenter";
    case FBWL_TOOLBAR_PLACEMENT_LEFT_TOP:
        return "LeftTop";
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM:
        return "RightBottom";
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_CENTER:
        return "RightCenter";
    case FBWL_TOOLBAR_PLACEMENT_RIGHT_TOP:
        return "RightTop";
    case FBWL_TOOLBAR_PLACEMENT_TOP_LEFT:
        return "TopLeft";
    case FBWL_TOOLBAR_PLACEMENT_TOP_CENTER:
        return "TopCenter";
    case FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT:
        return "TopRight";
    default:
        return "BottomCenter";
    }
}

uint32_t fbwl_toolbar_tools_default(void) {
    return FBWL_TOOLBAR_TOOL_WORKSPACES | FBWL_TOOLBAR_TOOL_ICONBAR | FBWL_TOOLBAR_TOOL_SYSTEMTRAY | FBWL_TOOLBAR_TOOL_CLOCK;
}

uint32_t fbwl_parse_toolbar_tools(const char *s) {
    if (s == NULL || *s == '\0') {
        return fbwl_toolbar_tools_default();
    }

    uint32_t tools = 0;
    char *copy = strdup(s);
    if (copy == NULL) {
        return fbwl_toolbar_tools_default();
    }

    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
        while (*tok != '\0' && isspace((unsigned char)*tok)) {
            tok++;
        }
        char *end = tok + strlen(tok);
        while (end > tok && isspace((unsigned char)end[-1])) {
            end--;
        }
        *end = '\0';
        if (*tok == '\0') {
            continue;
        }

        for (char *p = tok; *p != '\0'; p++) {
            *p = (char)tolower((unsigned char)*p);
        }

        if (strcmp(tok, "workspacename") == 0 || strcmp(tok, "prevworkspace") == 0 || strcmp(tok, "nextworkspace") == 0) {
            tools |= FBWL_TOOLBAR_TOOL_WORKSPACES;
        } else if (strcmp(tok, "iconbar") == 0 || strcmp(tok, "prevwindow") == 0 || strcmp(tok, "nextwindow") == 0) {
            tools |= FBWL_TOOLBAR_TOOL_ICONBAR;
        } else if (strcmp(tok, "systemtray") == 0) {
            tools |= FBWL_TOOLBAR_TOOL_SYSTEMTRAY;
        } else if (strcmp(tok, "clock") == 0) {
            tools |= FBWL_TOOLBAR_TOOL_CLOCK;
        } else if (strncmp(tok, "button.", 7) == 0) {
            tools |= FBWL_TOOLBAR_TOOL_BUTTONS;
        }
    }

    free(copy);
    if (tools == 0) {
        tools = fbwl_toolbar_tools_default();
    }
    return tools;
}

bool fbwl_parse_layer_num(const char *s, int *out_layer) {
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

    if (strcasecmp(s, "menu") == 0 || strcasecmp(s, "overlay") == 0) {
        *out_layer = 0;
        return true;
    }
    if (strcasecmp(s, "abovedock") == 0) {
        *out_layer = 2;
        return true;
    }
    if (strcasecmp(s, "dock") == 0) {
        *out_layer = 4;
        return true;
    }
    if (strcasecmp(s, "top") == 0) {
        *out_layer = 6;
        return true;
    }
    if (strcasecmp(s, "normal") == 0) {
        *out_layer = 8;
        return true;
    }
    if (strcasecmp(s, "bottom") == 0) {
        *out_layer = 10;
        return true;
    }
    if (strcasecmp(s, "desktop") == 0 || strcasecmp(s, "background") == 0) {
        *out_layer = 12;
        return true;
    }

    return false;
}

enum fbwm_row_placement_direction fbwl_parse_row_dir(const char *s) {
    if (s != NULL && strcasecmp(s, "RightToLeft") == 0) {
        return FBWM_ROW_RIGHT_TO_LEFT;
    }
    return FBWM_ROW_LEFT_TO_RIGHT;
}

const char *fbwl_row_dir_str(enum fbwm_row_placement_direction dir) {
    return dir == FBWM_ROW_RIGHT_TO_LEFT ? "RightToLeft" : "LeftToRight";
}

enum fbwm_col_placement_direction fbwl_parse_col_dir(const char *s) {
    if (s != NULL && strcasecmp(s, "BottomToTop") == 0) {
        return FBWM_COL_BOTTOM_TO_TOP;
    }
    return FBWM_COL_TOP_TO_BOTTOM;
}

const char *fbwl_col_dir_str(enum fbwm_col_placement_direction dir) {
    return dir == FBWM_COL_BOTTOM_TO_TOP ? "BottomToTop" : "TopToBottom";
}

enum fbwl_tab_focus_model fbwl_parse_tab_focus_model(const char *s) {
    if (s == NULL) {
        return FBWL_TAB_FOCUS_CLICK;
    }
    if (strcasecmp(s, "MouseTabFocus") == 0) {
        return FBWL_TAB_FOCUS_MOUSE;
    }
    if (strcasecmp(s, "ClickTabFocus") == 0) {
        return FBWL_TAB_FOCUS_CLICK;
    }
    return FBWL_TAB_FOCUS_CLICK;
}

const char *fbwl_tab_focus_model_str(enum fbwl_tab_focus_model model) {
    switch (model) {
    case FBWL_TAB_FOCUS_MOUSE:
        return "MouseTabFocus";
    case FBWL_TAB_FOCUS_CLICK:
    default:
        return "ClickTabFocus";
    }
}

enum fbwl_tabs_attach_area fbwl_parse_tabs_attach_area(const char *s) {
    if (s == NULL) {
        return FBWL_TABS_ATTACH_WINDOW;
    }
    if (strcasecmp(s, "Titlebar") == 0) {
        return FBWL_TABS_ATTACH_TITLEBAR;
    }
    if (strcasecmp(s, "Window") == 0) {
        return FBWL_TABS_ATTACH_WINDOW;
    }
    return FBWL_TABS_ATTACH_WINDOW;
}

const char *fbwl_tabs_attach_area_str(enum fbwl_tabs_attach_area area) {
    switch (area) {
    case FBWL_TABS_ATTACH_TITLEBAR:
        return "Titlebar";
    case FBWL_TABS_ATTACH_WINDOW:
    default:
        return "Window";
    }
}

static enum fbwl_decor_hit_kind titlebar_button_from_token(const char *tok) {
    if (tok == NULL || *tok == '\0') {
        return FBWL_DECOR_HIT_NONE;
    }
    if (strcasecmp(tok, "Close") == 0) {
        return FBWL_DECOR_HIT_BTN_CLOSE;
    }
    if (strcasecmp(tok, "Maximize") == 0) {
        return FBWL_DECOR_HIT_BTN_MAX;
    }
    if (strcasecmp(tok, "MenuIcon") == 0 || strcasecmp(tok, "Menu") == 0) {
        return FBWL_DECOR_HIT_BTN_MENU;
    }
    if (strcasecmp(tok, "Minimize") == 0) {
        return FBWL_DECOR_HIT_BTN_MIN;
    }
    if (strcasecmp(tok, "Shade") == 0) {
        return FBWL_DECOR_HIT_BTN_SHADE;
    }
    if (strcasecmp(tok, "Stick") == 0) {
        return FBWL_DECOR_HIT_BTN_STICK;
    }
    if (strcasecmp(tok, "LHalf") == 0) {
        return FBWL_DECOR_HIT_BTN_LHALF;
    }
    if (strcasecmp(tok, "RHalf") == 0) {
        return FBWL_DECOR_HIT_BTN_RHALF;
    }
    return FBWL_DECOR_HIT_NONE;
}

bool fbwl_titlebar_buttons_parse(const char *s, enum fbwl_decor_hit_kind *out, size_t cap, size_t *out_len) {
    if (s == NULL || out == NULL || out_len == NULL || cap < 1) {
        return false;
    }

    char *copy = strdup(s);
    if (copy == NULL) {
        return false;
    }

    char *trim = trim_inplace(copy);
    if (trim == NULL || *trim == '\0') {
        *out_len = 0;
        free(copy);
        return true;
    }

    size_t n = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(trim, " \t\r\n", &saveptr);
            tok != NULL;
            tok = strtok_r(NULL, " \t\r\n", &saveptr)) {
        const enum fbwl_decor_hit_kind kind = titlebar_button_from_token(tok);
        if (kind == FBWL_DECOR_HIT_NONE) {
            continue;
        }
        if (n < cap) {
            out[n++] = kind;
        }
    }

    free(copy);
    if (n < 1) {
        return false;
    }

    *out_len = n;
    return true;
}

void fbwl_apply_workspace_names_from_init(struct fbwm_core *wm, const char *csv) {
    if (wm == NULL || csv == NULL) {
        return;
    }

    fbwm_core_clear_workspace_names(wm);

    char *tmp = strdup(csv);
    if (tmp == NULL) {
        return;
    }

    char *saveptr = NULL;
    char *tok = strtok_r(tmp, ",", &saveptr);
    int idx = 0;
    while (tok != NULL && idx < 1000) {
        char *name = trim_inplace(tok);
        if (name != NULL && *name != '\0') {
            (void)fbwm_core_set_workspace_name(wm, idx, name);
            idx++;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }

    free(tmp);
}
