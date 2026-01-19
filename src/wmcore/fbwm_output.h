#pragma once

#include <stdbool.h>

struct fbwm_box {
    int x;
    int y;
    int width;
    int height;
};

struct fbwm_output;

struct fbwm_output_ops {
    const char *(*name)(const struct fbwm_output *output);
    bool (*full_box)(const struct fbwm_output *output, struct fbwm_box *out);
    bool (*usable_box)(const struct fbwm_output *output, struct fbwm_box *out);
};

struct fbwm_output {
    const struct fbwm_output_ops *ops;
    void *userdata;
};

void fbwm_output_init(struct fbwm_output *output, const struct fbwm_output_ops *ops, void *userdata);

const char *fbwm_output_name(const struct fbwm_output *output);

bool fbwm_output_get_full_box(const struct fbwm_output *output, struct fbwm_box *out);
bool fbwm_output_get_usable_box(const struct fbwm_output *output, struct fbwm_box *out);
