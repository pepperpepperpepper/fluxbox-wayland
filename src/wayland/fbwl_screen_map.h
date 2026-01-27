#pragma once

#include <stdbool.h>
#include <stddef.h>

struct wl_list;
struct wlr_output;
struct wlr_output_layout;

struct wlr_output *fbwl_screen_map_output_for_screen(struct wlr_output_layout *output_layout,
        const struct wl_list *outputs, size_t screen);

size_t fbwl_screen_map_screen_for_output(struct wlr_output_layout *output_layout,
        const struct wl_list *outputs, const struct wlr_output *output, bool *found);

void fbwl_screen_map_log(struct wlr_output_layout *output_layout, const struct wl_list *outputs,
        const char *why);
