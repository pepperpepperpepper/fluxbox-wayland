#pragma once

#include <stdbool.h>
#include <stddef.h>

void fbwl_string_list_free(char **items, size_t len);

// Parse a whitespace/comma-separated list into a lowercased string list.
// Returns true on success (including when the list is empty).
bool fbwl_string_list_parse(const char *s, char ***out_items, size_t *out_len);

// Replace an existing string list with the parsed contents of `s` (or empty if NULL/empty).
// Returns true if the list content changed.
bool fbwl_string_list_set(char ***io_items, size_t *io_len, const char *s);

