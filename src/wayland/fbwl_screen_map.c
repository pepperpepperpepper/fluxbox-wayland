#include "wayland/fbwl_screen_map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_output.h"

struct screen_entry {
    struct wlr_output *output;
    const char *name;
    int x;
    int y;
    int width;
    int height;
};

static const char *entry_name(const struct screen_entry *entry) {
    if (entry == NULL || entry->name == NULL) {
        return "";
    }
    return entry->name;
}

static int entry_cmp(const void *a, const void *b) {
    const struct screen_entry *ea = a;
    const struct screen_entry *eb = b;
    if (ea == NULL && eb == NULL) {
        return 0;
    }
    if (ea == NULL) {
        return 1;
    }
    if (eb == NULL) {
        return -1;
    }

    if (ea->x != eb->x) {
        return ea->x < eb->x ? -1 : 1;
    }
    if (ea->y != eb->y) {
        return ea->y < eb->y ? -1 : 1;
    }
    return strcmp(entry_name(ea), entry_name(eb));
}

static size_t build_entries(struct wlr_output_layout *output_layout, const struct wl_list *outputs,
        struct screen_entry **out_entries) {
    if (out_entries == NULL) {
        return 0;
    }
    *out_entries = NULL;
    if (output_layout == NULL || outputs == NULL) {
        return 0;
    }

    const size_t cap = fbwl_output_count(outputs);
    if (cap == 0) {
        return 0;
    }

    struct screen_entry *entries = calloc(cap, sizeof(*entries));
    if (entries == NULL) {
        return 0;
    }

    size_t n = 0;
    const struct fbwl_output *out;
    wl_list_for_each(out, outputs, link) {
        if (out == NULL || out->wlr_output == NULL) {
            continue;
        }

        struct wlr_box box = {0};
        wlr_output_layout_get_box(output_layout, out->wlr_output, &box);
        if (box.width < 1 || box.height < 1) {
            continue;
        }

        entries[n++] = (struct screen_entry){
            .output = out->wlr_output,
            .name = out->wlr_output->name != NULL ? out->wlr_output->name : "",
            .x = box.x,
            .y = box.y,
            .width = box.width,
            .height = box.height,
        };
        if (n >= cap) {
            break;
        }
    }

    if (n == 0) {
        free(entries);
        return 0;
    }

    qsort(entries, n, sizeof(*entries), entry_cmp);
    *out_entries = entries;
    return n;
}

struct wlr_output *fbwl_screen_map_output_for_screen(struct wlr_output_layout *output_layout,
        const struct wl_list *outputs, size_t screen) {
    struct screen_entry *entries = NULL;
    const size_t n = build_entries(output_layout, outputs, &entries);
    if (n == 0 || entries == NULL) {
        return output_layout != NULL ? wlr_output_layout_get_center_output(output_layout) : NULL;
    }

    const size_t idx = screen < n ? screen : 0;
    struct wlr_output *out = entries[idx].output;
    free(entries);
    return out;
}

size_t fbwl_screen_map_screen_for_output(struct wlr_output_layout *output_layout,
        const struct wl_list *outputs, const struct wlr_output *output, bool *found) {
    if (found != NULL) {
        *found = false;
    }
    if (output_layout == NULL || outputs == NULL || output == NULL) {
        return 0;
    }

    struct screen_entry *entries = NULL;
    const size_t n = build_entries(output_layout, outputs, &entries);
    if (n == 0 || entries == NULL) {
        return 0;
    }

    for (size_t i = 0; i < n; i++) {
        if (entries[i].output == output) {
            if (found != NULL) {
                *found = true;
            }
            free(entries);
            return i;
        }
    }

    free(entries);
    return 0;
}

void fbwl_screen_map_log(struct wlr_output_layout *output_layout, const struct wl_list *outputs,
        const char *why) {
    struct screen_entry *entries = NULL;
    const size_t n = build_entries(output_layout, outputs, &entries);
    wlr_log(WLR_INFO, "ScreenMap: reason=%s screens=%zu", why != NULL ? why : "(null)", n);

    if (entries == NULL) {
        return;
    }

    for (size_t i = 0; i < n; i++) {
        wlr_log(WLR_INFO, "ScreenMap: screen%zu name=%s x=%d y=%d w=%d h=%d",
            i,
            entries[i].name != NULL && entries[i].name[0] != '\0' ? entries[i].name : "(unnamed)",
            entries[i].x, entries[i].y, entries[i].width, entries[i].height);
    }

    free(entries);
}
