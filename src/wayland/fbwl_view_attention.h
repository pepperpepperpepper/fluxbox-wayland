#pragma once

#include "wayland/fbwl_view.h"

struct fbwl_decor_theme;

void fbwl_view_attention_request(struct fbwl_view *view, int interval_ms, const struct fbwl_decor_theme *theme,
        const char *why);
void fbwl_view_attention_clear(struct fbwl_view *view, const struct fbwl_decor_theme *theme, const char *why);
void fbwl_view_attention_finish(struct fbwl_view *view);
