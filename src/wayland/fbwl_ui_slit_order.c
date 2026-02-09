#include "wayland/fbwl_ui_slit_order.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wayland-server-core.h>

#include <wlr/xwayland.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_string_list.h"
#include "wayland/fbwl_view.h"

static char *trim_inplace(char *s) {
    if (s == NULL) {
        return NULL;
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static void str_lower_inplace(char *s) {
    if (s == NULL) {
        return;
    }
    for (char *p = s; *p != '\0'; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

static bool string_list_reserve(char ***items, size_t *cap, size_t need) {
    if (items == NULL || cap == NULL) {
        return false;
    }
    if (need <= *cap) {
        return true;
    }
    size_t new_cap = *cap > 0 ? *cap : 4;
    while (new_cap < need) {
        new_cap *= 2;
    }
    void *p = realloc(*items, new_cap * sizeof((*items)[0]));
    if (p == NULL) {
        return false;
    }
    *items = p;
    *cap = new_cap;
    return true;
}

static bool order_eq(char *const *a, size_t a_len, char *const *b, size_t b_len) {
    if (a_len != b_len) {
        return false;
    }
    for (size_t i = 0; i < a_len; i++) {
        const char *as = a != NULL ? a[i] : NULL;
        const char *bs = b != NULL ? b[i] : NULL;
        if (as == NULL) {
            as = "";
        }
        if (bs == NULL) {
            bs = "";
        }
        if (strcmp(as, bs) != 0) {
            return false;
        }
    }
    return true;
}

static bool order_load_file(const char *path, char ***out_items, size_t *out_len) {
    if (out_items == NULL || out_len == NULL) {
        return false;
    }
    *out_items = NULL;
    *out_len = 0;

    if (path == NULL || *path == '\0') {
        return true;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return true;
    }

    char *line = NULL;
    size_t cap = 0;

    char **items = NULL;
    size_t len = 0;
    size_t items_cap = 0;

    ssize_t n = 0;
    while ((n = getline(&line, &cap, f)) != -1) {
        (void)n;
        char *s = trim_inplace(line);
        if (s == NULL || *s == '\0') {
            continue;
        }
        if (*s == '#' || *s == '!') {
            continue;
        }
        str_lower_inplace(s);
        if (!string_list_reserve(&items, &items_cap, len + 1)) {
            fbwl_string_list_free(items, len);
            items = NULL;
            len = 0;
            break;
        }
        items[len] = strdup(s);
        if (items[len] == NULL) {
            fbwl_string_list_free(items, len);
            items = NULL;
            len = 0;
            break;
        }
        len++;
    }

    free(line);
    fclose(f);

    if (items == NULL && len != 0) {
        return false;
    }

    *out_items = items;
    *out_len = len;
    return true;
}

static const char *slit_view_match_name(const struct fbwl_view *view) {
    if (view == NULL) {
        return NULL;
    }
    if (view->type == FBWL_VIEW_XWAYLAND && view->xwayland_surface != NULL) {
        const char *name = view->xwayland_surface->instance;
        if (name != NULL && *name != '\0') {
            return name;
        }
        name = view->xwayland_surface->class;
        if (name != NULL && *name != '\0') {
            return name;
        }
    }
    const char *name = fbwl_view_app_id(view);
    if (name != NULL && *name != '\0') {
        return name;
    }
    name = fbwl_view_title(view);
    if (name != NULL && *name != '\0') {
        return name;
    }
    return NULL;
}

static size_t slit_order_index(const struct fbwl_slit_ui *ui, const char *name) {
    if (ui == NULL) {
        return 0;
    }
    if (name == NULL || *name == '\0') {
        return ui->order_len;
    }
    for (size_t i = 0; i < ui->order_len; i++) {
        if (ui->order != NULL && ui->order[i] != NULL && strcasecmp(name, ui->order[i]) == 0) {
            return i;
        }
    }
    return ui->order_len;
}

void fbwl_ui_slit_order_insert_item(struct fbwl_slit_ui *ui, struct wl_list *list, struct fbwl_slit_item *item) {
    if (ui == NULL || list == NULL || item == NULL) {
        return;
    }

    const char *name = slit_view_match_name(item->view);
    if (ui->order_len == 0 || name == NULL || *name == '\0') {
        wl_list_insert(list->prev, &item->link);
        return;
    }

    const size_t idx = slit_order_index(ui, name);
    struct fbwl_slit_item *last_same = NULL;
    struct fbwl_slit_item *insert_before = NULL;

    struct fbwl_slit_item *walk;
    wl_list_for_each(walk, list, link) {
        const char *wname = slit_view_match_name(walk->view);
        if (wname != NULL && *wname != '\0' && strcasecmp(name, wname) == 0) {
            last_same = walk;
            continue;
        }
        const size_t widx = slit_order_index(ui, wname);
        if (widx > idx) {
            insert_before = walk;
            break;
        }
    }

    if (last_same != NULL) {
        wl_list_insert(&last_same->link, &item->link);
        return;
    }
    if (insert_before != NULL) {
        wl_list_insert(insert_before->link.prev, &item->link);
        return;
    }
    wl_list_insert(list->prev, &item->link);
}

static void slit_order_resort_items(struct fbwl_slit_ui *ui) {
    if (ui == NULL || ui->order_len == 0 || ui->items_len < 2) {
        return;
    }

    struct wl_list sorted;
    wl_list_init(&sorted);

    struct fbwl_slit_item *it;
    struct fbwl_slit_item *tmp;
    wl_list_for_each_safe(it, tmp, &ui->items, link) {
        wl_list_remove(&it->link);
        fbwl_ui_slit_order_insert_item(ui, &sorted, it);
    }

    wl_list_init(&ui->items);
    if (!wl_list_empty(&sorted)) {
        wl_list_insert_list(&ui->items, &sorted);
    }
}

bool fbwl_ui_slit_set_order_file(struct fbwl_slit_ui *ui, const char *path) {
    if (ui == NULL) {
        return false;
    }

    char **new_items = NULL;
    size_t new_len = 0;
    if (!order_load_file(path, &new_items, &new_len)) {
        return false;
    }

    if (order_eq(ui->order, ui->order_len, new_items, new_len)) {
        fbwl_string_list_free(new_items, new_len);
        return false;
    }

    fbwl_string_list_free(ui->order, ui->order_len);
    ui->order = new_items;
    ui->order_len = new_len;

    slit_order_resort_items(ui);
    return true;
}

static bool order_list_has_name(char *const *items, size_t len, const char *name) {
    if (items == NULL || name == NULL || *name == '\0') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (items[i] != NULL && strcasecmp(items[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool string_list_push_dup(char ***items, size_t *len, size_t *cap, const char *s) {
    if (items == NULL || len == NULL || cap == NULL || s == NULL) {
        return false;
    }
    if (!string_list_reserve(items, cap, *len + 1)) {
        return false;
    }
    (*items)[*len] = strdup(s);
    if ((*items)[*len] == NULL) {
        return false;
    }
    (*len)++;
    return true;
}

bool fbwl_ui_slit_save_order_file(struct fbwl_slit_ui *ui, const char *path) {
    if (ui == NULL || path == NULL || *path == '\0') {
        return false;
    }

    char **present = NULL;
    size_t present_len = 0;
    size_t present_cap = 0;

    char prev_present[512];
    prev_present[0] = '\0';

    const struct fbwl_slit_item *it;
    wl_list_for_each(it, &ui->items, link) {
        const char *name = slit_view_match_name(it->view);
        if (name == NULL || *name == '\0') {
            continue;
        }
        if (prev_present[0] != '\0' && strcmp(prev_present, name) == 0) {
            continue;
        }
        if (snprintf(prev_present, sizeof(prev_present), "%s", name) < 0) {
            prev_present[0] = '\0';
        }
        if (!string_list_push_dup(&present, &present_len, &present_cap, name)) {
            fbwl_string_list_free(present, present_len);
            return false;
        }
    }

    char **base = NULL;
    size_t base_len = 0;
    size_t base_cap = 0;

    char prev_base[512];
    prev_base[0] = '\0';
    for (size_t i = 0; i < ui->order_len; i++) {
        const char *name = ui->order != NULL ? ui->order[i] : NULL;
        if (name == NULL || *name == '\0') {
            continue;
        }
        if (prev_base[0] != '\0' && strcmp(prev_base, name) == 0) {
            continue;
        }
        if (snprintf(prev_base, sizeof(prev_base), "%s", name) < 0) {
            prev_base[0] = '\0';
        }
        if (!string_list_push_dup(&base, &base_len, &base_cap, name)) {
            fbwl_string_list_free(present, present_len);
            fbwl_string_list_free(base, base_len);
            return false;
        }
    }

    char **next = NULL;
    size_t next_len = 0;
    size_t next_cap = 0;

    for (size_t i = 0; i < base_len; i++) {
        if (!string_list_push_dup(&next, &next_len, &next_cap, base[i])) {
            fbwl_string_list_free(present, present_len);
            fbwl_string_list_free(base, base_len);
            fbwl_string_list_free(next, next_len);
            return false;
        }
    }

    size_t pidx = 0;
    for (size_t i = 0; i < next_len && pidx < present_len; i++) {
        if (!order_list_has_name(present, present_len, next[i])) {
            continue;
        }
        free(next[i]);
        next[i] = strdup(present[pidx]);
        if (next[i] == NULL) {
            fbwl_string_list_free(present, present_len);
            fbwl_string_list_free(base, base_len);
            fbwl_string_list_free(next, next_len);
            return false;
        }
        pidx++;
    }

    for (; pidx < present_len; pidx++) {
        if (!string_list_push_dup(&next, &next_len, &next_cap, present[pidx])) {
            fbwl_string_list_free(present, present_len);
            fbwl_string_list_free(base, base_len);
            fbwl_string_list_free(next, next_len);
            return false;
        }
    }

    fbwl_string_list_free(present, present_len);
    fbwl_string_list_free(base, base_len);

    char **final = NULL;
    size_t final_len = 0;
    size_t final_cap = 0;

    char prev_final[512];
    prev_final[0] = '\0';
    for (size_t i = 0; i < next_len; i++) {
        const char *name = next[i];
        if (name == NULL || *name == '\0') {
            continue;
        }
        if (prev_final[0] != '\0' && strcmp(prev_final, name) == 0) {
            continue;
        }
        if (snprintf(prev_final, sizeof(prev_final), "%s", name) < 0) {
            prev_final[0] = '\0';
        }
        if (!string_list_push_dup(&final, &final_len, &final_cap, name)) {
            fbwl_string_list_free(next, next_len);
            fbwl_string_list_free(final, final_len);
            return false;
        }
    }
    fbwl_string_list_free(next, next_len);

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        wlr_log(WLR_ERROR, "Slit: save slitlist failed path=%s err=%s", path, strerror(errno));
        fbwl_string_list_free(final, final_len);
        return false;
    }

    for (size_t i = 0; i < final_len; i++) {
        if (final[i] == NULL || *final[i] == '\0') {
            continue;
        }
        fprintf(f, "%s\n", final[i]);
    }

    fclose(f);

    fbwl_string_list_free(ui->order, ui->order_len);
    ui->order = final;
    ui->order_len = final_len;

    wlr_log(WLR_INFO, "Slit: save slitlist ok path=%s items=%zu", path, final_len);
    return true;
}
