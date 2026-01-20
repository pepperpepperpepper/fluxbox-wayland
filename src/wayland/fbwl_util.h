#pragma once

#include <stdbool.h>

struct wl_listener;

void fbwl_cleanup_listener(struct wl_listener *listener);
void fbwl_cleanup_fd(int *fd);
bool fbwl_parse_hex_color(const char *s, float rgba[static 4]);
