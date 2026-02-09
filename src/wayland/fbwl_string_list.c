#include "wayland/fbwl_string_list.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void fbwl_string_list_free(char **items, size_t len) {
    if (items == NULL) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        free(items[i]);
    }
    free(items);
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

bool fbwl_string_list_parse(const char *s, char ***out_items, size_t *out_len) {
    if (out_items == NULL || out_len == NULL) {
        return false;
    }

    *out_items = NULL;
    *out_len = 0;

    if (s == NULL || *s == '\0') {
        return true;
    }

    char *copy = strdup(s);
    if (copy == NULL) {
        return false;
    }

    char **items = NULL;
    size_t len = 0;
    size_t cap = 0;

    char *save = NULL;
    for (char *tok = strtok_r(copy, " ,\t\r\n", &save); tok != NULL; tok = strtok_r(NULL, " ,\t\r\n", &save)) {
        if (*tok == '\0') {
            continue;
        }
        for (char *p = tok; *p != '\0'; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
        if (!string_list_reserve(&items, &cap, len + 1)) {
            fbwl_string_list_free(items, len);
            free(copy);
            return false;
        }
        items[len] = strdup(tok);
        if (items[len] == NULL) {
            fbwl_string_list_free(items, len);
            free(copy);
            return false;
        }
        len++;
    }

    free(copy);

    *out_items = items;
    *out_len = len;
    return true;
}

static bool string_list_eq(const char *const *a, size_t a_len, const char *const *b, size_t b_len) {
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

bool fbwl_string_list_set(char ***io_items, size_t *io_len, const char *s) {
    if (io_items == NULL || io_len == NULL) {
        return false;
    }

    char **new_items = NULL;
    size_t new_len = 0;
    if (!fbwl_string_list_parse(s, &new_items, &new_len)) {
        return false;
    }

    if (string_list_eq((const char *const *)(*io_items), *io_len, (const char *const *)new_items, new_len)) {
        fbwl_string_list_free(new_items, new_len);
        return false;
    }

    fbwl_string_list_free(*io_items, *io_len);
    *io_items = new_items;
    *io_len = new_len;
    return true;
}

