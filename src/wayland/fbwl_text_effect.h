#pragma once

#include <stdint.h>

enum fbwl_text_effect_kind {
    FBWL_TEXT_EFFECT_NONE = 0,
    FBWL_TEXT_EFFECT_SHADOW,
    FBWL_TEXT_EFFECT_HALO,
};

struct fbwl_text_effect {
    enum fbwl_text_effect_kind kind;

    float shadow_color[4];
    int shadow_x;
    int shadow_y;

    float halo_color[4];

    uint8_t prio_kind;
    uint8_t prio_shadow_color;
    uint8_t prio_shadow_x;
    uint8_t prio_shadow_y;
    uint8_t prio_halo_color;
};
