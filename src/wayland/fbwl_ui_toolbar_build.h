#pragma once

#include <stdbool.h>

struct fbwl_toolbar_ui;
struct fbwl_ui_toolbar_env;

void fbwl_ui_toolbar_build_iconbar(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
    bool vertical, const float fg[4]);

void fbwl_ui_toolbar_build_tray(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
    bool vertical, float alpha);

void fbwl_ui_toolbar_build_buttons(struct fbwl_toolbar_ui *ui, const struct fbwl_ui_toolbar_env *env,
    bool vertical, const float fg[4]);
