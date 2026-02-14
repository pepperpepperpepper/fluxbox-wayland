#pragma once

#include <stdbool.h>

bool fbwl_menu_parse_skip_name(const char *name);

char *fbwl_menu_parse_trim_inplace(char *s);
char *fbwl_menu_parse_dup_trim_range(const char *start, const char *end);

char *fbwl_menu_parse_paren_value(const char *s);
char *fbwl_menu_parse_brace_value(const char *s);
char *fbwl_menu_parse_angle_value(const char *s);
const char *fbwl_menu_parse_after_delim(const char *s, char open_ch, char close_ch);

char *fbwl_menu_parse_dirname_owned(const char *path);
char *fbwl_menu_parse_path_join(const char *dir, const char *rel);
char *fbwl_menu_parse_expand_tilde_owned(const char *path);
char *fbwl_menu_parse_resolve_path(const char *base_dir, const char *path);

bool fbwl_menu_parse_stat_is_dir(const char *path);
bool fbwl_menu_parse_stat_is_regular_file(const char *path);

char *fbwl_menu_parse_shell_escape_single_quoted(const char *s);
const char *fbwl_menu_parse_basename(const char *path);

