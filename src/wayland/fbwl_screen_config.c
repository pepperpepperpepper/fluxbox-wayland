#include "wayland/fbwl_server_internal.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

#include "wayland/fbwl_screen_map.h"

static void parse_struts(const char *s, struct fbwl_screen_struts_config *out) {
    if (s == NULL || out == NULL) {
        return;
    }

    int *vals[4] = {
        &out->left_px,
        &out->right_px,
        &out->top_px,
        &out->bottom_px,
    };

    const char *p = s;
    for (size_t i = 0; i < 4; i++) {
        while (*p != '\0' && (isspace((unsigned char)*p) || *p == ',')) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == NULL || end == p) {
            break;
        }
        if (vals[i] != NULL) {
            if (v < 0) {
                v = 0;
            }
            if (v > INT32_MAX) {
                v = INT32_MAX;
            }
            *vals[i] = (int)v;
        }
        p = end;
    }
}

static enum fbwl_iconbar_alignment parse_iconbar_alignment(const char *s) {
    if (s == NULL) {
        return FBWL_ICONBAR_ALIGN_RELATIVE;
    }
    if (strcasecmp(s, "left") == 0) {
        return FBWL_ICONBAR_ALIGN_LEFT;
    }
    if (strcasecmp(s, "right") == 0) {
        return FBWL_ICONBAR_ALIGN_RIGHT;
    }
    if (strcasecmp(s, "relative") == 0) {
        return FBWL_ICONBAR_ALIGN_RELATIVE;
    }
    if (strcasecmp(s, "relativesmart") == 0 || strcasecmp(s, "relative (smart)") == 0) {
        return FBWL_ICONBAR_ALIGN_RELATIVE_SMART;
    }
    return FBWL_ICONBAR_ALIGN_RELATIVE;
}

static void parse_iconified_pattern(const char *s, char *out_prefix, size_t prefix_size, char *out_suffix,
        size_t suffix_size) {
    if (out_prefix != NULL && prefix_size > 0) {
        out_prefix[0] = '\0';
    }
    if (out_suffix != NULL && suffix_size > 0) {
        out_suffix[0] = '\0';
    }
    if (s == NULL || out_prefix == NULL || out_suffix == NULL || prefix_size < 2 || suffix_size < 2) {
        return;
    }

    const char *tidx = strstr(s, "%t");
    if (tidx == NULL) {
        return;
    }

    size_t pre_len = (size_t)(tidx - s);
    if (pre_len >= prefix_size) {
        pre_len = prefix_size - 1;
    }
    memcpy(out_prefix, s, pre_len);
    out_prefix[pre_len] = '\0';

    const char *after = tidx + 2;
    size_t suf_len = strlen(after);
    if (suf_len >= suffix_size) {
        suf_len = suffix_size - 1;
    }
    memcpy(out_suffix, after, suf_len);
    out_suffix[suf_len] = '\0';
}

static size_t resource_db_max_struts_head_override(const struct fbwl_resource_db *db) {
    if (db == NULL) {
        return 0;
    }

    const char *prefix = "session.screen0.struts.";
    const size_t prefix_len = strlen(prefix);

    size_t max_head = 0;
    for (size_t i = 0; i < db->items_len; i++) {
        const char *key = db->items[i].key;
        if (key == NULL) {
            continue;
        }
        if (strncmp(key, prefix, prefix_len) != 0) {
            continue;
        }
        const char *p = key + prefix_len;
        if (*p == '\0' || !isdigit((unsigned char)*p)) {
            continue;
        }
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == NULL || end == p || *end != '\0') {
            continue;
        }
        if (v > 0 && (size_t)v > max_head) {
            max_head = (size_t)v;
        }
    }
    return max_head;
}

static void screen_config_set_defaults(struct fbwl_screen_config *cfg) {
    if (cfg == NULL) {
        return;
    }

    *cfg = (struct fbwl_screen_config){0};
    cfg->focus = (struct fbwl_focus_config){
        .model = FBWL_FOCUS_MODEL_CLICK_TO_FOCUS,
        .auto_raise = true,
        .auto_raise_delay_ms = 250,
        .click_raises = true,
        .focus_new_windows = true,
        .no_focus_while_typing_delay_ms = 0,
        .focus_same_head = false,
        .demands_attention_timeout_ms = 500,
        .allow_remote_actions = false, // Fluxbox/X11 default when driven by init
    };

    cfg->tooltip_delay_ms = 500;

    snprintf(cfg->iconbar.mode, sizeof(cfg->iconbar.mode), "%s", "{static groups} (workspace)");
    cfg->iconbar.alignment = FBWL_ICONBAR_ALIGN_RELATIVE;
    cfg->iconbar.icon_width_px = 128;
    cfg->iconbar.icon_text_padding_px = 10;
    cfg->iconbar.use_pixmap = true;
    snprintf(cfg->iconbar.iconified_prefix, sizeof(cfg->iconbar.iconified_prefix), "%s", "( ");
    snprintf(cfg->iconbar.iconified_suffix, sizeof(cfg->iconbar.iconified_suffix), "%s", " )");

    cfg->edge_snap_threshold_px = 10;
    cfg->edge_resize_snap_threshold_px = 0;
    cfg->opaque_move = true;
    cfg->opaque_resize = false;
    cfg->opaque_resize_delay_ms = 50;
    cfg->full_maximization = false;
    cfg->max_ignore_increment = true;
    cfg->max_disable_move = false;
    cfg->max_disable_resize = false;
    cfg->workspace_warping = true;
    cfg->workspace_warping_horizontal = true;
    cfg->workspace_warping_vertical = true;
    cfg->workspace_warping_horizontal_offset = 1;
    cfg->workspace_warping_vertical_offset = 1;
    cfg->show_window_position = false;

    cfg->placement_strategy = FBWM_PLACE_ROW_SMART;
    cfg->placement_row_dir = FBWM_ROW_LEFT_TO_RIGHT;
    cfg->placement_col_dir = FBWM_COL_TOP_TO_BOTTOM;

    fbwl_tabs_init_defaults(&cfg->tabs);

    cfg->slit.placement = FBWL_TOOLBAR_PLACEMENT_RIGHT_BOTTOM;
    cfg->slit.on_head = 0;
    cfg->slit.layer_num = 4;
    cfg->slit.auto_hide = false;
    cfg->slit.auto_raise = false;
    cfg->slit.max_over = false;
    cfg->slit.accept_kde_dockapps = true;
    cfg->slit.alpha = 255;
    cfg->slit.direction = FBWL_SLIT_DIR_VERTICAL;

    cfg->toolbar.enabled = true;
    cfg->toolbar.placement = FBWL_TOOLBAR_PLACEMENT_BOTTOM_CENTER;
    cfg->toolbar.on_head = 0;
    cfg->toolbar.layer_num = 4;
    cfg->toolbar.width_percent = 100;
    cfg->toolbar.height_override = 0;
    cfg->toolbar.tools = fbwl_toolbar_tools_default();
    cfg->toolbar.auto_hide = false;
    cfg->toolbar.auto_raise = false;
    cfg->toolbar.max_over = false;
    cfg->toolbar.alpha = 255;
    snprintf(cfg->toolbar.strftime_format, sizeof(cfg->toolbar.strftime_format), "%s", "%H:%M");

    cfg->menu.delay_ms = 200;
    cfg->menu.alpha = 255;
    cfg->menu.client_menu_use_pixmap = true;
}

static void screen_config_apply_init(struct fbwl_screen_config *cfg, const struct fbwl_resource_db *init, size_t screen,
        int global_auto_raise_delay_ms, int global_tab_padding_px, enum fbwl_tabs_attach_area global_tabs_attach_area) {
    if (cfg == NULL || init == NULL) {
        return;
    }

    struct fbwl_screen_struts_config struts = {0};

    const char *global_struts = fbwl_resource_db_get(init, "session.screen0.struts");
    parse_struts(global_struts, &struts);

    char head_struts_key[256];
    int head_key_n = snprintf(head_struts_key, sizeof(head_struts_key), "session.screen0.struts.%zu", screen + 1);
    if (head_key_n > 0 && (size_t)head_key_n < sizeof(head_struts_key)) {
        const char *head_struts = fbwl_resource_db_get(init, head_struts_key);
        parse_struts(head_struts, &struts);
    }

    if (screen > 0) {
        char screen_struts_key[256];
        int n = snprintf(screen_struts_key, sizeof(screen_struts_key), "session.screen%zu.struts", screen);
        if (n > 0 && (size_t)n < sizeof(screen_struts_key)) {
            const char *screen_struts = fbwl_resource_db_get(init, screen_struts_key);
            parse_struts(screen_struts, &struts);
        }
    }

    cfg->struts = struts;

    cfg->focus.model = fbwl_parse_focus_model(fbwl_resource_db_get_screen(init, screen, "focusModel"));
    cfg->focus.auto_raise_delay_ms = global_auto_raise_delay_ms;
    cfg->tabs.padding_px = global_tab_padding_px;
    cfg->tabs.attach_area = global_tabs_attach_area;

    bool bool_val = false;
    int int_val = 0;

    if (fbwl_resource_db_get_screen_bool(init, screen, "autoRaise", &bool_val)) {
        cfg->focus.auto_raise = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "clickRaises", &bool_val)) {
        cfg->focus.click_raises = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "focusNewWindows", &bool_val)) {
        cfg->focus.focus_new_windows = bool_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "noFocusWhileTypingDelay", &int_val) &&
            int_val >= 0 && int_val <= 3600000) {
        cfg->focus.no_focus_while_typing_delay_ms = int_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "focusSameHead", &bool_val)) {
        cfg->focus.focus_same_head = bool_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "demandsAttentionTimeout", &int_val) &&
            int_val >= 0 && int_val <= 3600000) {
        cfg->focus.demands_attention_timeout_ms = int_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "allowRemoteActions", &bool_val)) {
        cfg->focus.allow_remote_actions = bool_val;
    }

    if (fbwl_resource_db_get_screen_int(init, screen, "edgeSnapThreshold", &int_val) && int_val >= 0) {
        cfg->edge_snap_threshold_px = int_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "edgeResizeSnapThreshold", &int_val) && int_val >= 0) {
        cfg->edge_resize_snap_threshold_px = int_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "opaqueMove", &bool_val)) {
        cfg->opaque_move = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "opaqueResize", &bool_val)) {
        cfg->opaque_resize = bool_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "opaqueResizeDelay", &int_val) &&
            int_val >= 0 && int_val <= 60000) {
        cfg->opaque_resize_delay_ms = int_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "fullMaximization", &bool_val)) {
        cfg->full_maximization = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "maxIgnoreIncrement", &bool_val)) {
        cfg->max_ignore_increment = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "maxDisableMove", &bool_val)) {
        cfg->max_disable_move = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "maxDisableResize", &bool_val)) {
        cfg->max_disable_resize = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "workspacewarping", &bool_val)) {
        cfg->workspace_warping = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "workspacewarpinghorizontal", &bool_val)) {
        cfg->workspace_warping_horizontal = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "workspacewarpingvertical", &bool_val)) {
        cfg->workspace_warping_vertical = bool_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "workspacewarpinghorizontaloffset", &int_val) && int_val > 0) {
        cfg->workspace_warping_horizontal_offset = int_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "workspacewarpingverticaloffset", &int_val) && int_val > 0) {
        cfg->workspace_warping_vertical_offset = int_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "showwindowposition", &bool_val)) {
        cfg->show_window_position = bool_val;
    }

    const char *placement = fbwl_resource_db_get_screen(init, screen, "windowPlacement");
    if (placement != NULL) {
        cfg->placement_strategy = fbwl_parse_window_placement(placement);
    }
    const char *row_dir = fbwl_resource_db_get_screen(init, screen, "rowPlacementDirection");
    if (row_dir != NULL) {
        cfg->placement_row_dir = fbwl_parse_row_dir(row_dir);
    }
    const char *col_dir = fbwl_resource_db_get_screen(init, screen, "colPlacementDirection");
    if (col_dir != NULL) {
        cfg->placement_col_dir = fbwl_parse_col_dir(col_dir);
    }

    const char *strftime_format = fbwl_resource_db_get_screen(init, screen, "strftimeFormat");
    if (strftime_format != NULL) {
        strncpy(cfg->toolbar.strftime_format, strftime_format, sizeof(cfg->toolbar.strftime_format));
        cfg->toolbar.strftime_format[sizeof(cfg->toolbar.strftime_format) - 1] = '\0';
    }

    if (fbwl_resource_db_get_screen_int(init, screen, "tooltipDelay", &int_val)) {
        if (int_val < 0) {
            int_val = -1;
        } else if (int_val > 3600000) {
            int_val = 3600000;
        }
        cfg->tooltip_delay_ms = int_val;
    }

    const char *iconbar_mode = fbwl_resource_db_get_screen(init, screen, "iconbar.mode");
    if (iconbar_mode != NULL) {
        strncpy(cfg->iconbar.mode, iconbar_mode, sizeof(cfg->iconbar.mode));
        cfg->iconbar.mode[sizeof(cfg->iconbar.mode) - 1] = '\0';
    }
    const char *iconbar_align = fbwl_resource_db_get_screen(init, screen, "iconbar.alignment");
    if (iconbar_align != NULL) {
        cfg->iconbar.alignment = parse_iconbar_alignment(iconbar_align);
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "iconbar.iconWidth", &int_val)) {
        if (int_val < 10) {
            int_val = 10;
        } else if (int_val > 400) {
            int_val = 400;
        }
        cfg->iconbar.icon_width_px = int_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "iconbar.iconTextPadding", &int_val)) {
        if (int_val < 0) {
            int_val = 0;
        } else if (int_val > 1000) {
            int_val = 1000;
        }
        cfg->iconbar.icon_text_padding_px = int_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "iconbar.usePixmap", &bool_val)) {
        cfg->iconbar.use_pixmap = bool_val;
    }
    const char *iconified_pat = fbwl_resource_db_get_screen(init, screen, "iconbar.iconifiedPattern");
    if (iconified_pat != NULL) {
        parse_iconified_pattern(iconified_pat, cfg->iconbar.iconified_prefix, sizeof(cfg->iconbar.iconified_prefix),
            cfg->iconbar.iconified_suffix, sizeof(cfg->iconbar.iconified_suffix));
    }

    if (fbwl_resource_db_get_screen_bool(init, screen, "slit.autoHide", &bool_val)) {
        cfg->slit.auto_hide = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "slit.autoRaise", &bool_val)) {
        cfg->slit.auto_raise = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "slit.maxOver", &bool_val)) {
        cfg->slit.max_over = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "slit.acceptKdeDockapps", &bool_val)) {
        cfg->slit.accept_kde_dockapps = bool_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "slit.onhead", &int_val)) {
        if (int_val < 1) {
            int_val = 1;
        }
        if (int_val > 1000) {
            int_val = 1000;
        }
        cfg->slit.on_head = int_val - 1;
    }
    const char *slit_layer = fbwl_resource_db_get_screen(init, screen, "slit.layer");
    if (slit_layer != NULL) {
        int layer_num = 0;
        if (fbwl_parse_layer_num(slit_layer, &layer_num)) {
            cfg->slit.layer_num = layer_num;
        }
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "slit.alpha", &int_val)) {
        if (int_val < 0) {
            int_val = 0;
        } else if (int_val > 255) {
            int_val = 255;
        }
        cfg->slit.alpha = (uint8_t)int_val;
    }
    const char *slit_placement = fbwl_resource_db_get_screen(init, screen, "slit.placement");
    if (slit_placement != NULL) {
        cfg->slit.placement = fbwl_parse_toolbar_placement(slit_placement);
    }
    const char *slit_direction = fbwl_resource_db_get_screen(init, screen, "slit.direction");
    if (slit_direction != NULL) {
        if (strcasecmp(slit_direction, "horizontal") == 0) {
            cfg->slit.direction = FBWL_SLIT_DIR_HORIZONTAL;
        } else if (strcasecmp(slit_direction, "vertical") == 0) {
            cfg->slit.direction = FBWL_SLIT_DIR_VERTICAL;
        }
    }

    if (fbwl_resource_db_get_screen_bool(init, screen, "toolbar.visible", &bool_val)) {
        cfg->toolbar.enabled = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "toolbar.autoHide", &bool_val)) {
        cfg->toolbar.auto_hide = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "toolbar.autoRaise", &bool_val)) {
        cfg->toolbar.auto_raise = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "toolbar.maxOver", &bool_val)) {
        cfg->toolbar.max_over = bool_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "toolbar.onhead", &int_val)) {
        if (int_val < 1) {
            int_val = 1;
        }
        if (int_val > 1000) {
            int_val = 1000;
        }
        cfg->toolbar.on_head = int_val - 1;
    }
    const char *toolbar_layer = fbwl_resource_db_get_screen(init, screen, "toolbar.layer");
    if (toolbar_layer != NULL) {
        int layer_num = 0;
        if (fbwl_parse_layer_num(toolbar_layer, &layer_num)) {
            cfg->toolbar.layer_num = layer_num;
        }
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "toolbar.widthPercent", &int_val)) {
        if (int_val < 1) {
            int_val = 1;
        }
        if (int_val > 100) {
            int_val = 100;
        }
        cfg->toolbar.width_percent = int_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "toolbar.height", &int_val) && int_val >= 0) {
        cfg->toolbar.height_override = int_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "toolbar.alpha", &int_val)) {
        if (int_val < 0) {
            int_val = 0;
        } else if (int_val > 255) {
            int_val = 255;
        }
        cfg->toolbar.alpha = (uint8_t)int_val;
    }
    const char *toolbar_placement = fbwl_resource_db_get_screen(init, screen, "toolbar.placement");
    if (toolbar_placement != NULL) {
        cfg->toolbar.placement = fbwl_parse_toolbar_placement(toolbar_placement);
    }
    const char *toolbar_tools = fbwl_resource_db_get_screen(init, screen, "toolbar.tools");
    if (toolbar_tools != NULL) {
        cfg->toolbar.tools = fbwl_parse_toolbar_tools(toolbar_tools);
    }
    if (cfg->toolbar.tools == 0) {
        cfg->toolbar.tools = fbwl_toolbar_tools_default();
    }

    if (fbwl_resource_db_get_screen_int(init, screen, "menuDelay", &int_val) && int_val >= 0) {
        cfg->menu.delay_ms = int_val;
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "menu.alpha", &int_val)) {
        if (int_val < 0) {
            int_val = 0;
        } else if (int_val > 255) {
            int_val = 255;
        }
        cfg->menu.alpha = (uint8_t)int_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "clientMenu.usePixmap", &bool_val)) {
        cfg->menu.client_menu_use_pixmap = bool_val;
    }

    if (fbwl_resource_db_get_screen_bool(init, screen, "tabs.intitlebar", &bool_val)) {
        cfg->tabs.intitlebar = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "tabs.maxOver", &bool_val)) {
        cfg->tabs.max_over = bool_val;
    }
    if (fbwl_resource_db_get_screen_bool(init, screen, "tabs.usePixmap", &bool_val)) {
        cfg->tabs.use_pixmap = bool_val;
    }
    const char *tab_place = fbwl_resource_db_get_screen(init, screen, "tab.placement");
    if (tab_place != NULL) {
        cfg->tabs.placement = fbwl_parse_toolbar_placement(tab_place);
    }
    if (fbwl_resource_db_get_screen_int(init, screen, "tab.width", &int_val) && int_val >= 0) {
        cfg->tabs.width_px = int_val;
    }
    const char *tab_focus = fbwl_resource_db_get_screen(init, screen, "tabFocusModel");
    if (tab_focus != NULL) {
        cfg->tabs.focus_model = fbwl_parse_tab_focus_model(tab_focus);
    }
}

void fbwl_server_load_screen_configs(struct fbwl_server *server, const struct fbwl_resource_db *init) {
    if (server == NULL || init == NULL) {
        return;
    }

    size_t len = fbwl_resource_db_max_screen_index(init) + 1;
    const size_t head_overrides = resource_db_max_struts_head_override(init);
    if (head_overrides > len) {
        len = head_overrides;
    }
    if (len < 1) {
        len = 1;
    }

    struct fbwl_screen_config *cfgs = calloc(len, sizeof(*cfgs));
    if (cfgs == NULL) {
        return;
    }

    int global_auto_raise_delay_ms = 250;
    int global_tab_padding_px = 0;
    enum fbwl_tabs_attach_area global_tabs_attach_area = FBWL_TABS_ATTACH_WINDOW;

    int int_val = 0;
    if (fbwl_resource_db_get_int(init, "session.autoRaiseDelay", &int_val) && int_val >= 0) {
        global_auto_raise_delay_ms = int_val;
    }
    if (fbwl_resource_db_get_int(init, "session.tabPadding", &int_val) && int_val >= 0) {
        global_tab_padding_px = int_val;
    }
    const char *attach_area = fbwl_resource_db_get(init, "session.tabsAttachArea");
    if (attach_area != NULL) {
        global_tabs_attach_area = fbwl_parse_tabs_attach_area(attach_area);
    }

    for (size_t i = 0; i < len; i++) {
        screen_config_set_defaults(&cfgs[i]);
        screen_config_apply_init(&cfgs[i], init, i, global_auto_raise_delay_ms, global_tab_padding_px,
            global_tabs_attach_area);
    }

    free(server->screen_configs);
    server->screen_configs = cfgs;
    server->screen_configs_len = len;
}

static struct wlr_output *output_at(const struct fbwl_server *server, double lx, double ly) {
    if (server == NULL || server->output_layout == NULL) {
        return NULL;
    }
    struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, lx, ly);
    if (out == NULL) {
        out = wlr_output_layout_get_center_output(server->output_layout);
    }
    return out;
}

size_t fbwl_server_screen_index_at(const struct fbwl_server *server, double lx, double ly) {
    if (server == NULL || server->output_layout == NULL) {
        return 0;
    }
    struct wlr_output *out = output_at(server, lx, ly);
    if (out == NULL) {
        return 0;
    }
    bool found = false;
    const size_t screen = fbwl_screen_map_screen_for_output(server->output_layout, &server->outputs, out, &found);
    return found ? screen : 0;
}

size_t fbwl_server_screen_index_for_view(const struct fbwl_server *server, const struct fbwl_view *view) {
    if (server == NULL || view == NULL || server->output_layout == NULL) {
        return 0;
    }
    const int w = fbwl_view_current_width(view);
    const int h = fbwl_view_current_height(view);
    double cx = view->x + (double)w / 2.0;
    double cy = view->y + (double)h / 2.0;
    struct wlr_output *out = output_at(server, cx, cy);
    if (out == NULL) {
        return 0;
    }
    bool found = false;
    const size_t screen = fbwl_screen_map_screen_for_output(server->output_layout, &server->outputs, out, &found);
    return found ? screen : 0;
}

const struct fbwl_screen_config *fbwl_server_screen_config(const struct fbwl_server *server, size_t screen) {
    if (server == NULL || server->screen_configs == NULL || server->screen_configs_len < 1) {
        return NULL;
    }
    if (screen < server->screen_configs_len) {
        return &server->screen_configs[screen];
    }
    return &server->screen_configs[0];
}

const struct fbwl_screen_config *fbwl_server_screen_config_at(const struct fbwl_server *server, double lx, double ly) {
    return fbwl_server_screen_config(server, fbwl_server_screen_index_at(server, lx, ly));
}

const struct fbwl_screen_config *fbwl_server_screen_config_for_view(const struct fbwl_server *server,
        const struct fbwl_view *view) {
    return fbwl_server_screen_config(server, fbwl_server_screen_index_for_view(server, view));
}
