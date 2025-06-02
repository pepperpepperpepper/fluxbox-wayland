#pragma once

// This header provides C++ compatibility for wlroots headers
// that use C99 features not available in C++

#ifdef __cplusplus
extern "C" {
#endif

// Define static array syntax for C++ compatibility
#ifdef __cplusplus
#define WLR_ARRAY_STATIC(n) n
#else
#define WLR_ARRAY_STATIC(n) static n
#endif

// Include wlroots headers that need compatibility fixes
#include <wlr/types/wlr_scene.h>

// For C++, provide function declarations without static array syntax
#ifdef __cplusplus
// Override problematic declarations
#undef wlr_scene_rect_create
#undef wlr_scene_rect_set_color

struct wlr_scene_rect *wlr_scene_rect_create(struct wlr_scene_tree *parent,
		int width, int height, const float color[4]);

void wlr_scene_rect_set_color(struct wlr_scene_rect *rect, const float color[4]);
#endif

#ifdef __cplusplus
}
#endif