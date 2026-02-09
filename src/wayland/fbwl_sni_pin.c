#include "wayland/fbwl_sni_pin.h"

#ifdef HAVE_SYSTEMD

#include "wayland/fbwl_sni_tray.h"

#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>

struct sni_pin_sort_item {
    struct fbwl_sni_item *item;
    int order;
    size_t seq;
};

static int sni_pin_order_for_id(const char *item_id, char *const *left, size_t left_len, char *const *right,
        size_t right_len) {
    if (item_id == NULL || item_id[0] == '\0') {
        return 0;
    }
    for (size_t i = 0; i < left_len; i++) {
        if (left != NULL && left[i] != NULL && strcasecmp(left[i], item_id) == 0) {
            return (int)i - (int)left_len;
        }
    }
    for (size_t i = 0; i < right_len; i++) {
        if (right != NULL && right[i] != NULL && strcasecmp(right[i], item_id) == 0) {
            return (int)i + 1;
        }
    }
    return 0;
}

static int sni_pin_sort_cmp(const void *pa, const void *pb) {
    const struct sni_pin_sort_item *a = pa;
    const struct sni_pin_sort_item *b = pb;
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->order < b->order) {
        return -1;
    }
    if (a->order > b->order) {
        return 1;
    }
    if (a->seq < b->seq) {
        return -1;
    }
    if (a->seq > b->seq) {
        return 1;
    }
    return 0;
}

size_t fbwl_sni_pin_order_items(const struct fbwl_sni_watcher *watcher, struct fbwl_sni_item **out, size_t out_cap,
        char *const *pin_left, size_t pin_left_len, char *const *pin_right, size_t pin_right_len) {
    if (watcher == NULL || out == NULL || out_cap < 1 || watcher->items.prev == NULL || watcher->items.next == NULL) {
        return 0;
    }

    struct sni_pin_sort_item *tmp = calloc(out_cap, sizeof(*tmp));
    if (tmp == NULL) {
        return 0;
    }

    size_t n = 0;
    size_t seq = 0;
    struct fbwl_sni_item *item;
    wl_list_for_each(item, &watcher->items, link) {
        if (item->status == FBWL_SNI_STATUS_PASSIVE) {
            continue;
        }
        if (n >= out_cap) {
            break;
        }
        tmp[n].item = item;
        tmp[n].order = sni_pin_order_for_id(item->item_id, pin_left, pin_left_len, pin_right, pin_right_len);
        tmp[n].seq = seq++;
        n++;
    }

    qsort(tmp, n, sizeof(*tmp), sni_pin_sort_cmp);
    for (size_t i = 0; i < n; i++) {
        out[i] = tmp[i].item;
    }

    free(tmp);
    return n;
}

#endif

