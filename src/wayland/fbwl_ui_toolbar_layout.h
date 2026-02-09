#pragma once

#include <stdbool.h>

struct fbwl_toolbar_ui;
struct fbwl_ui_toolbar_env;

void fbwl_ui_toolbar_layout_apply(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env, bool vertical);
