#include "wayland/fbwl_util.h"
#include "wayland/fbwl_deco_mask.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include "wayland/fbwl_output.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"

void fbwl_cleanup_listener(struct wl_listener *listener) {
    if (listener->link.prev != NULL && listener->link.next != NULL) {
        wl_list_remove(&listener->link);
        listener->link.prev = NULL;
        listener->link.next = NULL;
    }
}

void fbwl_cleanup_fd(int *fd) {
    if (fd != NULL && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

bool fbwl_parse_hex_color(const char *s, float rgba[static 4]) {
    if (s == NULL || rgba == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '#') {
        s++;
    }

    const size_t len = strlen(s);
    if (len != 6 && len != 8) {
        return false;
    }

    uint32_t comps[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i])) {
            return false;
        }
    }

    char buf[3] = {0};
    for (size_t i = 0; i < len / 2; i++) {
        buf[0] = s[i * 2];
        buf[1] = s[i * 2 + 1];
        char *end = NULL;
        unsigned long v = strtoul(buf, &end, 16);
        if (end == NULL || *end != '\0' || v > 255) {
            return false;
        }
        comps[i] = (uint32_t)v;
    }

    rgba[0] = (float)comps[0] / 255.0f;
    rgba[1] = (float)comps[1] / 255.0f;
    rgba[2] = (float)comps[2] / 255.0f;
    rgba[3] = (float)comps[3] / 255.0f;
    return true;
}

static bool parse_x_hex_component_16(const char *s, size_t len, uint16_t *out) {
    if (s == NULL || out == NULL || len < 1 || len > 4) {
        return false;
    }

    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (!isxdigit(c)) {
            return false;
        }
        v *= 16;
        if (c >= '0' && c <= '9') {
            v += (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v += 10u + (uint32_t)(c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            v += 10u + (uint32_t)(c - 'A');
        } else {
            return false;
        }
    }

    const uint32_t max = (1u << (len * 4)) - 1u;
    const uint32_t scaled = max > 0 ? (v * 65535u + max / 2u) / max : 0u;
    if (scaled > 65535u) {
        return false;
    }

    *out = (uint16_t)scaled;
    return true;
}

static bool parse_rgb_hex(const char *s, float rgba[static 4]) {
    if (s == NULL || rgba == NULL) {
        return false;
    }

    const char *p = s;
    uint16_t comps16[3] = {0, 0, 0};

    for (size_t i = 0; i < 3; i++) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        const char *start = p;
        while (*p != '\0' && isxdigit((unsigned char)*p)) {
            p++;
        }
        const char *end = p;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (start == end || (size_t)(end - start) > 4) {
            return false;
        }
        if (!parse_x_hex_component_16(start, (size_t)(end - start), &comps16[i])) {
            return false;
        }
        if (i < 2) {
            if (*p != '/') {
                return false;
            }
            p++;
        } else {
            if (*p != '\0') {
                return false;
            }
        }
    }

    rgba[0] = (float)comps16[0] / 65535.0f;
    rgba[1] = (float)comps16[1] / 65535.0f;
    rgba[2] = (float)comps16[2] / 65535.0f;
    rgba[3] = 1.0f;
    return true;
}

static bool parse_rgb_float(const char *s, float rgba[static 4]) {
    if (s == NULL || rgba == NULL) {
        return false;
    }

    const char *p = s;
    float comps[3] = {0.0f, 0.0f, 0.0f};

    for (size_t i = 0; i < 3; i++) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            return false;
        }
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p || end == NULL) {
            return false;
        }
        p = end;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (i < 2) {
            if (*p != '/') {
                return false;
            }
            p++;
        } else {
            if (*p != '\0') {
                return false;
            }
        }

        if (v < 0.0) {
            v = 0.0;
        }
        if (v > 1.0) {
            v = 1.0;
        }
        comps[i] = (float)v;
    }

    rgba[0] = comps[0];
    rgba[1] = comps[1];
    rgba[2] = comps[2];
    rgba[3] = 1.0f;
    return true;
}

bool fbwl_parse_color(const char *s, float rgba[static 4]) {
    if (fbwl_parse_hex_color(s, rgba)) {
        return true;
    }

    if (s == NULL || rgba == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    if (strncasecmp(s, "rgb:", 4) == 0) {
        return parse_rgb_hex(s + 4, rgba);
    }
    if (strncasecmp(s, "rgbi:", 5) == 0) {
        return parse_rgb_float(s + 5, rgba);
    }

    if (strcasecmp(s, "transparent") == 0 || strcasecmp(s, "none") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 0.0f;
        rgba[2] = 0.0f;
        rgba[3] = 0.0f;
        return true;
    }

    if (strncasecmp(s, "gray", 4) == 0 || strncasecmp(s, "grey", 4) == 0) {
        const char *p = s + 4;
        if (strncasecmp(s, "grey", 4) == 0) {
            p = s + 4;
        }
        if (*p == '\0') {
            rgba[0] = 0.5f;
            rgba[1] = 0.5f;
            rgba[2] = 0.5f;
            rgba[3] = 1.0f;
            return true;
        }
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end != p && end != NULL && *end == '\0' && v >= 0 && v <= 100) {
            float f = (float)v / 100.0f;
            rgba[0] = f;
            rgba[1] = f;
            rgba[2] = f;
            rgba[3] = 1.0f;
            return true;
        }
        return false;
    }

    if (strcasecmp(s, "black") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 0.0f;
        rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "white") == 0) {
        rgba[0] = 1.0f;
        rgba[1] = 1.0f;
        rgba[2] = 1.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "red") == 0) {
        rgba[0] = 1.0f;
        rgba[1] = 0.0f;
        rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "green") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 1.0f;
        rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "blue") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 0.0f;
        rgba[2] = 1.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "yellow") == 0) {
        rgba[0] = 1.0f;
        rgba[1] = 1.0f;
        rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "cyan") == 0) {
        rgba[0] = 0.0f;
        rgba[1] = 1.0f;
        rgba[2] = 1.0f;
        rgba[3] = 1.0f;
        return true;
    }
    if (strcasecmp(s, "magenta") == 0) {
        rgba[0] = 1.0f;
        rgba[1] = 0.0f;
        rgba[2] = 1.0f;
        rgba[3] = 1.0f;
        return true;
    }

    return false;
}

void fbwl_spawn(const char *cmd) {
    if (cmd == NULL || *cmd == '\0') {
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
        _exit(127);
    }
}

uint64_t fbwl_now_msec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool pseudo_bg_never_accepts_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    (void)buffer;
    (void)sx;
    (void)sy;
    return false;
}

static bool pseudo_bg_compute_wallpaper_src_box(enum fbwl_wallpaper_mode wallpaper_mode,
        struct wlr_output_layout *output_layout,
        struct wlr_buffer *wallpaper_buf, int global_x, int global_y, int width, int height,
        struct wlr_fbox *out_src_box) {
    if (output_layout == NULL || wallpaper_buf == NULL || out_src_box == NULL || width < 1 || height < 1) {
        return false;
    }

    const double cx = (double)global_x + (double)width / 2.0;
    const double cy = (double)global_y + (double)height / 2.0;
    struct wlr_output *output = wlr_output_layout_output_at(output_layout, cx, cy);
    if (output == NULL) {
        output = wlr_output_layout_get_center_output(output_layout);
    }
    if (output == NULL) {
        return false;
    }

    struct wlr_box output_box = {0};
    wlr_output_layout_get_box(output_layout, output, &output_box);
    if (output_box.width < 1 || output_box.height < 1) {
        return false;
    }

    const double buf_w = (double)wallpaper_buf->width;
    const double buf_h = (double)wallpaper_buf->height;
    if (buf_w <= 0.0 || buf_h <= 0.0) {
        return false;
    }

    double base_x = 0.0;
    double base_y = 0.0;
    double base_w = buf_w;
    double base_h = buf_h;
    if (wallpaper_mode == FBWL_WALLPAPER_MODE_FILL) {
        const double out_aspect = (double)output_box.width / (double)output_box.height;
        const double buf_aspect = buf_w / buf_h;
        if (buf_aspect > out_aspect) {
            const double crop_w = buf_h * out_aspect;
            if (crop_w > 0.0 && crop_w < buf_w) {
                base_w = crop_w;
                base_x = (buf_w - crop_w) / 2.0;
            }
        } else if (buf_aspect < out_aspect) {
            const double crop_h = buf_w / out_aspect;
            if (crop_h > 0.0 && crop_h < buf_h) {
                base_h = crop_h;
                base_y = (buf_h - crop_h) / 2.0;
            }
        }
    }
    if (base_w <= 0.0 || base_h <= 0.0) {
        return false;
    }

    double sx = base_x + ((double)global_x - (double)output_box.x) / (double)output_box.width * base_w;
    double sy = base_y + ((double)global_y - (double)output_box.y) / (double)output_box.height * base_h;
    double sw = (double)width / (double)output_box.width * base_w;
    double sh = (double)height / (double)output_box.height * base_h;
    if (sw <= 0.0 || sh <= 0.0) {
        return false;
    }

    const double min_x = base_x;
    const double min_y = base_y;
    const double max_x = base_x + base_w;
    const double max_y = base_y + base_h;

    if (sx < min_x) {
        sw -= (min_x - sx);
        sx = min_x;
    }
    if (sy < min_y) {
        sh -= (min_y - sy);
        sy = min_y;
    }
    if (sx + sw > max_x) {
        sw = max_x - sx;
    }
    if (sy + sh > max_y) {
        sh = max_y - sy;
    }
    if (sw <= 0.0 || sh <= 0.0) {
        return false;
    }

    *out_src_box = (struct wlr_fbox){
        .x = sx,
        .y = sy,
        .width = sw,
        .height = sh,
    };
    return true;
}

void fbwl_pseudo_bg_destroy(struct fbwl_pseudo_bg *bg) {
    if (bg == NULL) {
        return;
    }
    if (bg->image != NULL) {
        wlr_scene_node_destroy(&bg->image->node);
        bg->image = NULL;
    }
    if (bg->rect != NULL) {
        wlr_scene_node_destroy(&bg->rect->node);
        bg->rect = NULL;
    }
}

void fbwl_pseudo_bg_update(struct fbwl_pseudo_bg *bg,
        struct wlr_scene_tree *parent,
        struct wlr_output_layout *output_layout,
        int global_x,
        int global_y,
        int rel_x,
        int rel_y,
        int width,
        int height,
        enum fbwl_wallpaper_mode wallpaper_mode,
        struct wlr_buffer *wallpaper_buf,
        const float background_color[4]) {
    if (bg == NULL || parent == NULL || width < 1 || height < 1) {
        fbwl_pseudo_bg_destroy(bg);
        return;
    }

    struct wlr_buffer *use_wallpaper_buf = wallpaper_buf;
    if (wallpaper_mode == FBWL_WALLPAPER_MODE_TILE && wallpaper_buf != NULL && output_layout != NULL) {
        const double cx = (double)global_x + (double)width / 2.0;
        const double cy = (double)global_y + (double)height / 2.0;
        struct wlr_output *wlr_output = wlr_output_layout_output_at(output_layout, cx, cy);
        if (wlr_output == NULL) {
            wlr_output = wlr_output_layout_get_center_output(output_layout);
        }
        struct fbwl_output *out = wlr_output != NULL ? wlr_output->data : NULL;
        if (out != NULL && out->wallpaper_tile_buf != NULL) {
            use_wallpaper_buf = out->wallpaper_tile_buf;
        }
    }

    if (use_wallpaper_buf != NULL && output_layout != NULL && wallpaper_mode == FBWL_WALLPAPER_MODE_CENTER) {
        float color[4] = {0};
        if (background_color != NULL) {
            color[0] = background_color[0];
            color[1] = background_color[1];
            color[2] = background_color[2];
        }
        color[3] = 1.0f;

        if (bg->rect == NULL) {
            bg->rect = wlr_scene_rect_create(parent, width, height, color);
        } else {
            wlr_scene_rect_set_size(bg->rect, width, height);
            wlr_scene_rect_set_color(bg->rect, color);
        }
        if (bg->rect == NULL) {
            return;
        }
        wlr_scene_node_set_position(&bg->rect->node, rel_x, rel_y);
        wlr_scene_node_lower_to_bottom(&bg->rect->node);

        const double cx = (double)global_x + (double)width / 2.0;
        const double cy = (double)global_y + (double)height / 2.0;
        struct wlr_output *output = wlr_output_layout_output_at(output_layout, cx, cy);
        if (output == NULL) {
            output = wlr_output_layout_get_center_output(output_layout);
        }

        struct wlr_box output_box = {0};
        if (output == NULL) {
            goto center_done;
        }
        wlr_output_layout_get_box(output_layout, output, &output_box);
        if (output_box.width < 1 || output_box.height < 1) {
            goto center_done;
        }

        const int img_w = wallpaper_buf->width;
        const int img_h = wallpaper_buf->height;
        const int img_x = output_box.x + (output_box.width - img_w) / 2;
        const int img_y = output_box.y + (output_box.height - img_h) / 2;

        const int reg_x1 = global_x;
        const int reg_y1 = global_y;
        const int reg_x2 = global_x + width;
        const int reg_y2 = global_y + height;
        const int img_x2 = img_x + img_w;
        const int img_y2 = img_y + img_h;

        const int ix1 = reg_x1 > img_x ? reg_x1 : img_x;
        const int iy1 = reg_y1 > img_y ? reg_y1 : img_y;
        const int ix2 = reg_x2 < img_x2 ? reg_x2 : img_x2;
        const int iy2 = reg_y2 < img_y2 ? reg_y2 : img_y2;

        const int iw = ix2 - ix1;
        const int ih = iy2 - iy1;
        if (iw < 1 || ih < 1) {
            goto center_done;
        }

        if (bg->image == NULL) {
            bg->image = wlr_scene_buffer_create(parent, use_wallpaper_buf);
            if (bg->image != NULL) {
                bg->image->point_accepts_input = pseudo_bg_never_accepts_input;
            }
        } else {
            wlr_scene_buffer_set_buffer(bg->image, use_wallpaper_buf);
        }
        if (bg->image == NULL) {
            return;
        }

        wlr_scene_node_set_position(&bg->image->node, rel_x + (ix1 - global_x), rel_y + (iy1 - global_y));
        wlr_scene_node_raise_to_top(&bg->image->node);
        wlr_scene_buffer_set_opacity(bg->image, 1.0f);
        wlr_scene_buffer_set_dest_size(bg->image, iw, ih);

        const struct wlr_fbox src = {
            .x = (double)(ix1 - img_x),
            .y = (double)(iy1 - img_y),
            .width = (double)iw,
            .height = (double)ih,
        };
        wlr_scene_buffer_set_source_box(bg->image, &src);

        return;

center_done:
        if (bg->image != NULL) {
            wlr_scene_node_destroy(&bg->image->node);
            bg->image = NULL;
        }
        return;
    }

    if (use_wallpaper_buf != NULL && output_layout != NULL) {
        if (bg->rect != NULL) {
            wlr_scene_node_destroy(&bg->rect->node);
            bg->rect = NULL;
        }
        if (bg->image == NULL) {
            bg->image = wlr_scene_buffer_create(parent, use_wallpaper_buf);
            if (bg->image != NULL) {
                bg->image->point_accepts_input = pseudo_bg_never_accepts_input;
            }
        } else {
            wlr_scene_buffer_set_buffer(bg->image, use_wallpaper_buf);
        }
        if (bg->image == NULL) {
            return;
        }

        wlr_scene_node_set_position(&bg->image->node, rel_x, rel_y);
        wlr_scene_node_lower_to_bottom(&bg->image->node);
        wlr_scene_buffer_set_opacity(bg->image, 1.0f);
        wlr_scene_buffer_set_dest_size(bg->image, width, height);

        struct wlr_fbox src = {0};
        if (pseudo_bg_compute_wallpaper_src_box(wallpaper_mode, output_layout, use_wallpaper_buf,
                global_x, global_y, width, height, &src)) {
            wlr_scene_buffer_set_source_box(bg->image, &src);
        } else {
            wlr_scene_buffer_set_source_box(bg->image, NULL);
        }

        return;
    }

    if (bg->image != NULL) {
        wlr_scene_node_destroy(&bg->image->node);
        bg->image = NULL;
    }
    if (bg->rect == NULL) {
        float color[4] = {0};
        if (background_color != NULL) {
            color[0] = background_color[0];
            color[1] = background_color[1];
            color[2] = background_color[2];
        }
        color[3] = 1.0f;
        bg->rect = wlr_scene_rect_create(parent, width, height, color);
    } else {
        wlr_scene_rect_set_size(bg->rect, width, height);
        float color[4] = {0};
        if (background_color != NULL) {
            color[0] = background_color[0];
            color[1] = background_color[1];
            color[2] = background_color[2];
        }
        color[3] = 1.0f;
        wlr_scene_rect_set_color(bg->rect, color);
    }
    if (bg->rect == NULL) {
        return;
    }
    wlr_scene_node_set_position(&bg->rect->node, rel_x, rel_y);
    wlr_scene_node_lower_to_bottom(&bg->rect->node);
}

struct view_pseudo_bg_bounds {
    bool any;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    const struct wlr_scene_buffer *ignore;
};

static void view_pseudo_bg_bounds_iter(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data) {
    struct view_pseudo_bg_bounds *b = user_data;
    if (buffer == NULL || b == NULL) {
        return;
    }
    if (b->ignore != NULL && buffer == b->ignore) {
        return;
    }
    if (buffer->buffer == NULL) {
        return;
    }

    int w = buffer->dst_width > 0 ? buffer->dst_width : buffer->buffer->width;
    int h = buffer->dst_height > 0 ? buffer->dst_height : buffer->buffer->height;
    if (w < 1 || h < 1) {
        return;
    }

    const int x1 = sx;
    const int y1 = sy;
    const int x2 = sx + w;
    const int y2 = sy + h;

    if (!b->any) {
        b->any = true;
        b->min_x = x1;
        b->min_y = y1;
        b->max_x = x2;
        b->max_y = y2;
        return;
    }
    if (x1 < b->min_x) {
        b->min_x = x1;
    }
    if (y1 < b->min_y) {
        b->min_y = y1;
    }
    if (x2 > b->max_x) {
        b->max_x = x2;
    }
    if (y2 > b->max_y) {
        b->max_y = y2;
    }
}

static bool view_needs_pseudo_bg(const struct fbwl_view *view) {
    if (view == NULL || view->server == NULL) {
        return false;
    }
    if (!view->server->force_pseudo_transparency) {
        return false;
    }
    if (!view->alpha_set) {
        return false;
    }
    return view->alpha_focused < 255 || view->alpha_unfocused < 255;
}

void fbwl_view_pseudo_bg_update(struct fbwl_view *view, const char *why) {
    (void)why;
    if (view == NULL || view->server == NULL || view->scene_tree == NULL) {
        return;
    }

    if (!view_needs_pseudo_bg(view)) {
        fbwl_pseudo_bg_destroy(&view->pseudo_bg);
        return;
    }

    struct view_pseudo_bg_bounds bounds = {
        .any = false,
        .min_x = 0,
        .min_y = 0,
        .max_x = 0,
        .max_y = 0,
        .ignore = view->pseudo_bg.image,
    };
    wlr_scene_node_for_each_buffer(&view->scene_tree->node, view_pseudo_bg_bounds_iter, &bounds);
    if (!bounds.any) {
        fbwl_pseudo_bg_destroy(&view->pseudo_bg);
        return;
    }

    const int w = bounds.max_x - bounds.min_x;
    const int h = bounds.max_y - bounds.min_y;
    if (w < 1 || h < 1) {
        fbwl_pseudo_bg_destroy(&view->pseudo_bg);
        return;
    }

    int origin_x = 0;
    int origin_y = 0;
    if (!wlr_scene_node_coords(&view->scene_tree->node, &origin_x, &origin_y)) {
        origin_x = view->x;
        origin_y = view->y;
    }
    const int rel_x = bounds.min_x - origin_x;
    const int rel_y = bounds.min_y - origin_y;
    fbwl_pseudo_bg_update(&view->pseudo_bg, view->scene_tree, view->server->output_layout,
        bounds.min_x, bounds.min_y, rel_x, rel_y, w, h,
        view->server->wallpaper_mode,
        view->server->wallpaper_buf, view->server->background_color);
}

bool fbwl_deco_mask_parse(const char *s, uint32_t *out_mask) {
    if (out_mask != NULL) {
        *out_mask = 0u;
    }
    if (s == NULL || out_mask == NULL) {
        return false;
    }

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return false;
    }

    if (strcasecmp(s, "none") == 0) {
        *out_mask = FBWL_DECOR_NONE;
        return true;
    }
    if (strcasecmp(s, "normal") == 0) {
        *out_mask = FBWL_DECOR_NORMAL;
        return true;
    }
    if (strcasecmp(s, "tiny") == 0) {
        *out_mask = FBWL_DECOR_TINY;
        return true;
    }
    if (strcasecmp(s, "tool") == 0) {
        *out_mask = FBWL_DECOR_TOOL;
        return true;
    }
    if (strcasecmp(s, "border") == 0) {
        *out_mask = FBWL_DECOR_BORDER;
        return true;
    }
    if (strcasecmp(s, "tab") == 0) {
        *out_mask = FBWL_DECOR_TAB;
        return true;
    }

    errno = 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 0);
    if (end == s || end == NULL) {
        return false;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }
    if (errno == ERANGE) {
        return false;
    }

    *out_mask = (uint32_t)v;
    return true;
}

const char *fbwl_deco_mask_preset_name(uint32_t mask) {
    switch (mask) {
    case 0u:
        return "NONE";
    case 0xffffffffu:
    case FBWL_DECOR_NORMAL:
        return "NORMAL";
    case FBWL_DECOR_TOOL:
        return "TOOL";
    case FBWL_DECOR_TINY:
        return "TINY";
    case FBWL_DECOR_BORDER:
        return "BORDER";
    case FBWL_DECOR_TAB:
        return "TAB";
    default:
        return NULL;
    }
}
