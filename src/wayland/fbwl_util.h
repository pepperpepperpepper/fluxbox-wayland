#pragma once

#include <stdbool.h>
#include <stdint.h>

struct wl_listener;

void fbwl_cleanup_listener(struct wl_listener *listener);
void fbwl_cleanup_fd(int *fd);
bool fbwl_parse_hex_color(const char *s, float rgba[static 4]);
bool fbwl_parse_color(const char *s, float rgba[static 4]);
void fbwl_spawn(const char *cmd);
uint64_t fbwl_now_msec(void);
