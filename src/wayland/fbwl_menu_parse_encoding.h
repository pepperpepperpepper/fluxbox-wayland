#pragma once

#include <stdbool.h>
#include <stddef.h>

enum {
    FBWL_MENU_PARSE_ENCODING_STACK_MAX = 8,
};

struct fbwl_menu_parse_state {
    char *encoding_stack[FBWL_MENU_PARSE_ENCODING_STACK_MAX];
    size_t encoding_depth;
};

const char *fbwl_menu_parse_state_encoding(const struct fbwl_menu_parse_state *st);
bool fbwl_menu_parse_state_push_encoding(struct fbwl_menu_parse_state *st, const char *encoding);
bool fbwl_menu_parse_state_pop_encoding(struct fbwl_menu_parse_state *st);
void fbwl_menu_parse_state_clear(struct fbwl_menu_parse_state *st);

void fbwl_menu_parse_convert_owned_to_utf8(char **s, const char *encoding);
