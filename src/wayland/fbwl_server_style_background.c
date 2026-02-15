#include "wayland/fbwl_server_internal.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_texture.h"
#include "wayland/fbwl_ui_text.h"

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL || *needle == '\0') {
        return false;
    }
    const size_t nlen = strlen(needle);
    for (const char *p = haystack; *p != '\0'; p++) {
        size_t i = 0;
        while (i < nlen && p[i] != '\0' && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) {
            return true;
        }
    }
    return false;
}

static bool path_is_dir(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

static bool path_is_regular_file_limited(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        return false;
    }
    const off_t max_size = 64 * 1024 * 1024;
    return st.st_size >= 0 && st.st_size <= max_size;
}

static char *path_join2_owned(const char *a, const char *b) {
    if (a == NULL || *a == '\0' || b == NULL || *b == '\0') {
        return NULL;
    }
    const size_t a_len = strlen(a);
    const bool need_slash = a[a_len - 1] != '/';
    const size_t needed = a_len + (need_slash ? 1 : 0) + strlen(b) + 1;
    char *out = malloc(needed);
    if (out == NULL) {
        return NULL;
    }
    int n = snprintf(out, needed, need_slash ? "%s/%s" : "%s%s", a, b);
    if (n < 0 || (size_t)n >= needed) {
        free(out);
        return NULL;
    }
    return out;
}

static bool path_has_suffix_ci(const char *path, const char *suffix) {
    if (path == NULL || suffix == NULL) {
        return false;
    }
    const size_t plen = strlen(path);
    const size_t slen = strlen(suffix);
    if (plen < slen) {
        return false;
    }
    return strcasecmp(path + (plen - slen), suffix) == 0;
}

static bool wallpaper_path_supported(const char *path) {
    if (path_has_suffix_ci(path, ".png")) {
        return true;
    }
#ifdef HAVE_XPM
    if (path_has_suffix_ci(path, ".xpm")) {
        return true;
    }
#endif
    return false;
}

static char *wallpaper_pick_random_from_dir_owned(const char *dir_path) {
    if (dir_path == NULL || *dir_path == '\0') {
        return NULL;
    }
    if (!path_is_dir(dir_path)) {
        return NULL;
    }

    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        unsigned seed = (unsigned)time(NULL) ^ (unsigned)getpid();
        srand(seed);
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return NULL;
    }

    char *picked = NULL;
    size_t picked_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }

        char *full = path_join2_owned(dir_path, ent->d_name);
        if (full == NULL) {
            continue;
        }

        if (!wallpaper_path_supported(full) || access(full, R_OK) != 0 || !path_is_regular_file_limited(full)) {
            free(full);
            continue;
        }

        picked_count++;
        if ((size_t)(rand() % (int)picked_count) == 0) {
            free(picked);
            picked = full;
        } else {
            free(full);
        }
    }
    closedir(dir);
    return picked;
}

static void server_background_target_size(const struct fbwl_server *server, int *out_w, int *out_h) {
    if (out_w != NULL) {
        *out_w = 1024;
    }
    if (out_h != NULL) {
        *out_h = 768;
    }
    if (server == NULL || server->output_layout == NULL) {
        return;
    }

    struct wlr_output *output = wlr_output_layout_get_center_output(server->output_layout);
    if (output == NULL) {
        return;
    }
    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, output, &box);
    if (box.width < 1 || box.height < 1) {
        return;
    }

    if (out_w != NULL) {
        *out_w = box.width;
    }
    if (out_h != NULL) {
        *out_h = box.height;
    }
}

static struct wlr_buffer *wallpaper_buffer_from_mod_pattern(int mod_x, int mod_y, const float fg[4], const float bg[4]) {
    const int s = 16;
    if (mod_x < 1) {
        mod_x = 1;
    }
    if (mod_y < 1) {
        mod_y = 1;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, s, s);
    if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != NULL) {
            cairo_surface_destroy(surface);
        }
        return NULL;
    }

    cairo_t *cr = cairo_create(surface);
    if (cr == NULL || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        if (cr != NULL) {
            cairo_destroy(cr);
        }
        return NULL;
    }

    const float bg_r = bg != NULL ? bg[0] : 0.0f;
    const float bg_g = bg != NULL ? bg[1] : 0.0f;
    const float bg_b = bg != NULL ? bg[2] : 0.0f;
    const float fg_r = fg != NULL ? fg[0] : 1.0f;
    const float fg_g = fg != NULL ? fg[1] : 1.0f;
    const float fg_b = fg != NULL ? fg[2] : 1.0f;

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, 1.0);
    cairo_paint(cr);

    cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, 1.0);
    for (int y = 0; y < s; y++) {
        if ((y % mod_y) == 0) {
            cairo_rectangle(cr, 0.0, (double)y, (double)s, 1.0);
        }
    }
    for (int x = 0; x < s; x++) {
        if ((x % mod_x) == 0) {
            cairo_rectangle(cr, (double)x, 0.0, 1.0, (double)s);
        }
    }
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct wlr_buffer *buf = fbwl_cairo_buffer_create(surface);
    if (buf == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    return buf;
}

static bool server_wallpaper_restore_last(struct fbwl_server *server) {
    if (server == NULL) {
        return false;
    }

    const char *home = getenv("HOME");
    const char *wl = getenv("WAYLAND_DISPLAY");
    if (home == NULL || *home == '\0' || wl == NULL || *wl == '\0') {
        return false;
    }

    char last[512];
    int n = snprintf(last, sizeof(last), "%s/.fluxbox/lastwallpaper", home);
    if (n < 0 || (size_t)n >= sizeof(last) || access(last, R_OK) != 0) {
        return false;
    }

    char key[256];
    n = snprintf(key, sizeof(key), "wayland:%s", wl);
    if (n < 0 || (size_t)n >= sizeof(key)) {
        return false;
    }

    FILE *f = fopen(last, "r");
    if (f == NULL) {
        return false;
    }

    char *line = NULL;
    size_t cap = 0;
    char *match = NULL;
    while (getline(&line, &cap, f) != -1) {
        char *nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        size_t len = strlen(line);
        size_t key_len = strlen(key);
        if (len < key_len + 1) {
            continue;
        }
        if (line[len - key_len - 1] != '|') {
            continue;
        }
        if (strcmp(line + (len - key_len), key) != 0) {
            continue;
        }

        free(match);
        match = strdup(line);
    }
    free(line);
    fclose(f);

    if (match == NULL) {
        return false;
    }

    char *saveptr = NULL;
    (void)strtok_r(match, "|", &saveptr); // program
    char *path = strtok_r(NULL, "|", &saveptr);
    char *mode_tok = strtok_r(NULL, "|", &saveptr);
    if (path == NULL || *path == '\0') {
        free(match);
        return false;
    }

    enum fbwl_wallpaper_mode mode = FBWL_WALLPAPER_MODE_STRETCH;
    if (mode_tok != NULL && *mode_tok != '\0') {
        mode = fbwl_wallpaper_mode_parse(mode_tok);
    }

    const bool ok = server_wallpaper_set(server, path, mode);
    free(match);
    return ok;
}

static uint8_t float_to_u8_clamped(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

static uint32_t rgb24_from_rgba(const float rgba[4]) {
    if (rgba == NULL) {
        return 0;
    }
    const uint32_t r = float_to_u8_clamped(rgba[0]);
    const uint32_t g = float_to_u8_clamped(rgba[1]);
    const uint32_t b = float_to_u8_clamped(rgba[2]);
    return (r << 16) | (g << 8) | b;
}

void server_background_apply_style(struct fbwl_server *server, const struct fbwl_decor_theme *theme, const char *why) {
    if (server == NULL || theme == NULL || !theme->background_loaded) {
        return;
    }

    const char *opts = theme->background_options;
    if (opts == NULL) {
        opts = "";
    }

    if (str_contains_ci(opts, "unset")) {
        wlr_log(WLR_INFO, "Background: style opts=unset reason=%s", why != NULL ? why : "(null)");
        return;
    }

    if (str_contains_ci(opts, "none")) {
        if (!server->style_background_first) {
            wlr_log(WLR_INFO, "Background: style opts=none (skip) reason=%s", why != NULL ? why : "(null)");
            return;
        }
        if (server_wallpaper_restore_last(server)) {
            wlr_log(WLR_INFO, "Background: style opts=none restored last wallpaper reason=%s", why != NULL ? why : "(null)");
        } else {
            wlr_log(WLR_INFO, "Background: style opts=none (no last wallpaper) reason=%s", why != NULL ? why : "(null)");
        }
        server->style_background_first = false;
        return;
    }

    const char *pixmap = theme->background_pixmap[0] != '\0' ? theme->background_pixmap : NULL;
    if (pixmap != NULL && access(pixmap, R_OK) == 0 && path_is_regular_file_limited(pixmap)) {
        enum fbwl_wallpaper_mode mode = FBWL_WALLPAPER_MODE_STRETCH;
        if (str_contains_ci(opts, "tiled")) {
            mode = FBWL_WALLPAPER_MODE_TILE;
        } else if (str_contains_ci(opts, "centered")) {
            mode = FBWL_WALLPAPER_MODE_CENTER;
        } else if (str_contains_ci(opts, "aspect")) {
            mode = FBWL_WALLPAPER_MODE_FILL;
        }

        if (server_wallpaper_set(server, pixmap, mode)) {
            wlr_log(WLR_INFO, "Background: style pixmap path=%s mode=%s reason=%s",
                pixmap, fbwl_wallpaper_mode_str(mode), why != NULL ? why : "(null)");
            server->style_background_first = false;
            return;
        }
        wlr_log(WLR_ERROR, "Background: style pixmap failed path=%s", pixmap);
    }

    if (pixmap != NULL && path_is_dir(pixmap) && str_contains_ci(opts, "random")) {
        char *picked = wallpaper_pick_random_from_dir_owned(pixmap);
        if (picked != NULL) {
            if (server_wallpaper_set(server, picked, FBWL_WALLPAPER_MODE_STRETCH)) {
                wlr_log(WLR_INFO, "Background: style random dir=%s picked=%s reason=%s",
                    pixmap, picked, why != NULL ? why : "(null)");
                free(picked);
                server->style_background_first = false;
                return;
            }
            free(picked);
        }
        wlr_log(WLR_ERROR, "Background: style random failed dir=%s", pixmap);
    }

    if (str_contains_ci(opts, "mod")) {
        struct wlr_buffer *buf = wallpaper_buffer_from_mod_pattern(
            theme->background_mod_x, theme->background_mod_y,
            theme->background_tex.color, theme->background_tex.color_to);
        if (buf != NULL) {
            (void)server_wallpaper_set_buffer(server, buf, FBWL_WALLPAPER_MODE_TILE, "(style:mod)", why);
            server->style_background_first = false;
            return;
        }
        wlr_log(WLR_ERROR, "Background: style mod buffer failed");
    }

    if ((theme->background_tex.type & FBWL_TEXTURE_GRADIENT) != 0) {
        int w = 0;
        int h = 0;
        server_background_target_size(server, &w, &h);
        struct wlr_buffer *buf = fbwl_texture_render_buffer(&theme->background_tex, w, h);
        if (buf != NULL) {
            (void)server_wallpaper_set_buffer(server, buf, FBWL_WALLPAPER_MODE_STRETCH, "(style:gradient)", why);
            server->style_background_first = false;
            return;
        }
        wlr_log(WLR_ERROR, "Background: style gradient buffer failed");
    }

    memcpy(server->background_color, theme->background_tex.color, sizeof(server->background_color));
    server->background_color[3] = 1.0f;
    (void)server_wallpaper_set(server, "clear", FBWL_WALLPAPER_MODE_STRETCH);
    wlr_log(WLR_INFO, "Background: style solid rgb=#%06x reason=%s",
        rgb24_from_rgba(server->background_color), why != NULL ? why : "(null)");
    server->style_background_first = false;
}
