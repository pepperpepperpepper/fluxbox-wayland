#pragma once

struct wl_listener;

void fbwl_cleanup_listener(struct wl_listener *listener);
void fbwl_cleanup_fd(int *fd);

