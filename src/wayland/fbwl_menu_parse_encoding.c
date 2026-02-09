#include "wayland/fbwl_menu_parse_encoding.h"

#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

static bool menu_encoding_is_utf8(const char *encoding) {
    return encoding == NULL || *encoding == '\0' ||
        strcasecmp(encoding, "utf-8") == 0 || strcasecmp(encoding, "utf8") == 0;
}

static char *menu_iconv_dup_to_utf8(const char *encoding, const char *s) {
    if (s == NULL) {
        return NULL;
    }
    if (menu_encoding_is_utf8(encoding)) {
        return strdup(s);
    }

    iconv_t cd = iconv_open("UTF-8", encoding);
    if (cd == (iconv_t)-1) {
        return strdup(s);
    }

    size_t in_left = strlen(s);
    const char *in = s;
    char *in_mut = (char *)in;

    size_t out_cap = in_left * 4 + 16;
    char *out = malloc(out_cap + 1);
    if (out == NULL) {
        iconv_close(cd);
        return NULL;
    }
    char *out_p = out;
    size_t out_left = out_cap;

    while (in_left > 0) {
        size_t r = iconv(cd, &in_mut, &in_left, &out_p, &out_left);
        if (r != (size_t)-1) {
            break;
        }

        if (errno == E2BIG) {
            size_t used = out_cap - out_left;
            size_t new_cap = out_cap * 2;
            char *new_out = realloc(out, new_cap + 1);
            if (new_out == NULL) {
                free(out);
                iconv_close(cd);
                return NULL;
            }
            out = new_out;
            out_cap = new_cap;
            out_p = out + used;
            out_left = out_cap - used;
            continue;
        }

        if (errno == EILSEQ || errno == EINVAL) {
            if (out_left < 2) {
                size_t used = out_cap - out_left;
                size_t new_cap = out_cap * 2;
                char *new_out = realloc(out, new_cap + 1);
                if (new_out == NULL) {
                    free(out);
                    iconv_close(cd);
                    return NULL;
                }
                out = new_out;
                out_cap = new_cap;
                out_p = out + used;
                out_left = out_cap - used;
            }

            *out_p++ = '?';
            out_left--;
            in_mut++;
            in_left--;
            continue;
        }

        break;
    }

    iconv_close(cd);
    *out_p = '\0';
    return out;
}

const char *fbwl_menu_parse_state_encoding(const struct fbwl_menu_parse_state *st) {
    if (st == NULL || st->encoding_depth < 1) {
        return NULL;
    }
    return st->encoding_stack[st->encoding_depth - 1];
}

void fbwl_menu_parse_state_clear(struct fbwl_menu_parse_state *st) {
    if (st == NULL) {
        return;
    }
    for (size_t i = 0; i < st->encoding_depth; i++) {
        free(st->encoding_stack[i]);
        st->encoding_stack[i] = NULL;
    }
    st->encoding_depth = 0;
}

bool fbwl_menu_parse_state_push_encoding(struct fbwl_menu_parse_state *st, const char *encoding) {
    if (st == NULL || encoding == NULL) {
        return false;
    }
    if (*encoding == '\0') {
        return true;
    }
    if (st->encoding_depth >= FBWL_MENU_PARSE_ENCODING_STACK_MAX) {
        wlr_log(WLR_ERROR, "Menu: encoding stack overflow");
        return false;
    }

    iconv_t cd = iconv_open("UTF-8", encoding);
    if (cd == (iconv_t)-1) {
        wlr_log(WLR_ERROR, "Menu: iconv_open failed for encoding %s: %s", encoding, strerror(errno));
        return false;
    }
    iconv_close(cd);

    char *dup = strdup(encoding);
    if (dup == NULL) {
        return false;
    }
    st->encoding_stack[st->encoding_depth++] = dup;
    return true;
}

bool fbwl_menu_parse_state_pop_encoding(struct fbwl_menu_parse_state *st) {
    if (st == NULL) {
        return false;
    }
    if (st->encoding_depth < 1) {
        wlr_log(WLR_ERROR, "Menu: endencoding without encoding");
        return false;
    }
    free(st->encoding_stack[st->encoding_depth - 1]);
    st->encoding_stack[st->encoding_depth - 1] = NULL;
    st->encoding_depth--;
    return true;
}

void fbwl_menu_parse_convert_owned_to_utf8(char **s, const char *encoding) {
    if (s == NULL || *s == NULL) {
        return;
    }
    if (menu_encoding_is_utf8(encoding)) {
        return;
    }

    char *converted = menu_iconv_dup_to_utf8(encoding, *s);
    if (converted == NULL) {
        return;
    }
    free(*s);
    *s = converted;
}

