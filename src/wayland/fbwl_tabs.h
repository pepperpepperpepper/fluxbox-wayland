#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "wayland/fbwl_ui_toolbar.h"

struct fbwl_server;
struct fbwl_view;

enum fbwl_tab_focus_model {
    FBWL_TAB_FOCUS_CLICK = 0,
    FBWL_TAB_FOCUS_MOUSE,
};

enum fbwl_tabs_attach_area {
    FBWL_TABS_ATTACH_WINDOW = 0,
    FBWL_TABS_ATTACH_TITLEBAR,
};

struct fbwl_tabs_config {
    bool intitlebar;
    bool max_over;
    bool use_pixmap;
    enum fbwl_toolbar_placement placement;
    int width_px;
    int padding_px;
    enum fbwl_tab_focus_model focus_model;
    enum fbwl_tabs_attach_area attach_area;
};

void fbwl_tabs_init_defaults(struct fbwl_tabs_config *cfg);

bool fbwl_tabs_view_is_active(const struct fbwl_view *view);

bool fbwl_tabs_attach(struct fbwl_view *view, struct fbwl_view *anchor, const char *reason);
void fbwl_tabs_detach(struct fbwl_view *view, const char *reason);
void fbwl_tabs_activate(struct fbwl_view *view, const char *reason);

void fbwl_tabs_repair(struct fbwl_server *server);

void fbwl_tabs_sync_geometry_from_view(struct fbwl_view *view, bool include_size, int width, int height,
    const char *reason);
