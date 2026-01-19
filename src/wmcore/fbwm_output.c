#include "fbwm_output.h"

#include <stddef.h>

void fbwm_output_init(struct fbwm_output *output, const struct fbwm_output_ops *ops, void *userdata) {
    if (output == NULL) {
        return;
    }
    output->ops = ops;
    output->userdata = userdata;
}

const char *fbwm_output_name(const struct fbwm_output *output) {
    if (output == NULL || output->ops == NULL || output->ops->name == NULL) {
        return NULL;
    }
    return output->ops->name(output);
}

static bool output_get_box(const struct fbwm_output *output,
        bool (*fn)(const struct fbwm_output *output, struct fbwm_box *out),
        struct fbwm_box *out) {
    if (out != NULL) {
        *out = (struct fbwm_box){0};
    }
    if (output == NULL || fn == NULL || out == NULL) {
        return false;
    }
    return fn(output, out);
}

bool fbwm_output_get_full_box(const struct fbwm_output *output, struct fbwm_box *out) {
    return output_get_box(output,
        output != NULL && output->ops != NULL ? output->ops->full_box : NULL,
        out);
}

bool fbwm_output_get_usable_box(const struct fbwm_output *output, struct fbwm_box *out) {
    return output_get_box(output,
        output != NULL && output->ops != NULL ? output->ops->usable_box : NULL,
        out);
}
