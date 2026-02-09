#pragma once

#include <wayland-server-core.h>

#include "wayland/fbwl_ui_slit.h"

void fbwl_ui_slit_order_insert_item(struct fbwl_slit_ui *ui, struct wl_list *list, struct fbwl_slit_item *item);

