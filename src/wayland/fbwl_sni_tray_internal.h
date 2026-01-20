#pragma once

#ifdef HAVE_SYSTEMD

#include <stddef.h>
#include <stdint.h>

#include "wayland/fbwl_sni_tray.h"

struct wlr_buffer;

void sni_item_destroy(struct fbwl_sni_item *item);
void sni_item_subscribe(struct fbwl_sni_item *item);
void sni_item_request_all(struct fbwl_sni_item *item);

struct wlr_buffer *sni_icon_buffer_from_argb32(const uint8_t *argb, size_t len, int width, int height);
const char *sni_status_str(enum fbwl_sni_status status);
enum fbwl_sni_status sni_status_parse(const char *s);
struct wlr_buffer *sni_icon_compose_overlay(struct wlr_buffer *base, struct wlr_buffer *overlay);
char *sni_icon_resolve_png_path(const char *icon_name, const char *icon_theme_path);
struct wlr_buffer *sni_icon_buffer_from_png_path(const char *path);

#endif

