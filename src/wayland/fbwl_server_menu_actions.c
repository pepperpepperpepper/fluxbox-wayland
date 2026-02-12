#include "wayland/fbwl_server_menu_actions.h"

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_style_parse.h"
#include "wayland/fbwl_view_foreign_toplevel.h"

static struct fbwl_view *find_view_by_create_seq(struct fbwl_server *server, uint64_t create_seq);

static const char *focus_model_str(enum fbwl_focus_model model) {
    switch (model) {
    case FBWL_FOCUS_MODEL_CLICK_TO_FOCUS:
        return "ClickToFocus";
    case FBWL_FOCUS_MODEL_MOUSE_FOCUS:
        return "MouseFocus";
    case FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS:
        return "StrictMouseFocus";
    default:
        return "Unknown";
    }
}

static struct fbwl_screen_config *active_screen_config(struct fbwl_server *server, size_t *out_screen) {
    if (out_screen != NULL) {
        *out_screen = 0;
    }
    if (server == NULL) {
        return NULL;
    }

    size_t screen = 0;
    if (server->cursor != NULL) {
        screen = fbwl_server_screen_index_at(server, server->cursor->x, server->cursor->y);
    }
    if (server->screen_configs == NULL || server->screen_configs_len < 1) {
        return NULL;
    }
    if (screen >= server->screen_configs_len) {
        screen = 0;
    }
    if (out_screen != NULL) {
        *out_screen = screen;
    }
    return &server->screen_configs[screen];
}

static struct fbwl_screen_config *slit_screen_config(struct fbwl_server *server, size_t *out_screen) {
    if (out_screen != NULL) {
        *out_screen = 0;
    }
    if (server == NULL || server->screen_configs == NULL || server->screen_configs_len < 1) {
        return NULL;
    }
    size_t screen = (size_t)(server->slit_ui.on_head >= 0 ? server->slit_ui.on_head : 0);
    if (screen >= server->screen_configs_len) {
        screen = 0;
    }
    if (out_screen != NULL) {
        *out_screen = screen;
    }
    return &server->screen_configs[screen];
}

static bool slit_cycle_up(struct fbwl_slit_ui *ui) {
    if (ui == NULL || ui->items_len < 2 || wl_list_empty(&ui->items)) {
        return false;
    }
    struct fbwl_slit_item *first = wl_container_of(ui->items.next, first, link);
    wl_list_remove(&first->link);
    wl_list_insert(ui->items.prev, &first->link);
    return true;
}

static bool slit_cycle_down(struct fbwl_slit_ui *ui) {
    if (ui == NULL || ui->items_len < 2 || wl_list_empty(&ui->items)) {
        return false;
    }
    struct fbwl_slit_item *last = wl_container_of(ui->items.prev, last, link);
    wl_list_remove(&last->link);
    wl_list_insert(&ui->items, &last->link);
    return true;
}

static bool slit_client_up(struct fbwl_slit_ui *ui, struct fbwl_slit_item *it) {
    if (ui == NULL || it == NULL || ui->items_len < 2 || wl_list_empty(&ui->items)) {
        return false;
    }
    struct wl_list *insert_after = it->link.prev->prev;
    wl_list_remove(&it->link);
    wl_list_insert(insert_after, &it->link);
    return true;
}

static bool slit_client_down(struct fbwl_slit_ui *ui, struct fbwl_slit_item *it) {
    if (ui == NULL || it == NULL || ui->items_len < 2 || wl_list_empty(&ui->items)) {
        return false;
    }
    struct wl_list *insert_after = it->link.next;
    wl_list_remove(&it->link);
    wl_list_insert(insert_after, &it->link);
    return true;
}

static bool cmd_dialog_submit_slit_alpha(void *userdata, const char *text) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return true;
    }
    if (text == NULL || *text == '\0') {
        return false;
    }

    char *end = NULL;
    long v = strtol(text, &end, 10);
    if (end == text || end == NULL || *end != '\0') {
        wlr_log(WLR_ERROR, "Slit: set-alpha prompt invalid=%s", text);
        return false;
    }
    if (v < 0) {
        v = 0;
    } else if (v > 255) {
        v = 255;
    }

    size_t screen = 0;
    struct fbwl_screen_config *cfg = slit_screen_config(server, &screen);
    if (cfg != NULL) {
        cfg->slit.alpha = (uint8_t)v;
    }
    server->slit_ui.alpha = (uint8_t)v;
    server_slit_ui_rebuild(server);
    wlr_log(WLR_INFO, "Slit: set-alpha %ld (prompt)", v);
    return true;
}

static bool cmd_dialog_submit_set_title(void *userdata, const char *text) {
    struct fbwl_server *server = userdata;
    if (server == NULL) {
        return true;
    }

    const uint64_t seq = server->cmd_dialog_target_create_seq;
    if (seq == 0) {
        wlr_log(WLR_ERROR, "Title: set-title-dialog missing target create_seq");
        return true;
    }

    struct fbwl_view *view = find_view_by_create_seq(server, seq);
    if (view == NULL) {
        wlr_log(WLR_ERROR, "Title: set-title-dialog no match create_seq=%llu",
            (unsigned long long)seq);
        server->cmd_dialog_target_create_seq = 0;
        return true;
    }

    const char *use = text != NULL ? text : "";
    if (*use == '\0') {
        free(view->title_override);
        view->title_override = NULL;
        fbwl_view_foreign_toplevel_set_title(view, fbwl_view_title(view));
        fbwl_view_decor_update_title_text(view, &server->decor_theme);
        server_toolbar_ui_rebuild(server);
        wlr_log(WLR_INFO, "Title: cleared title override create_seq=%llu",
            (unsigned long long)seq);
        server->cmd_dialog_target_create_seq = 0;
        return true;
    }

    char *dup = strdup(use);
    if (dup == NULL) {
        wlr_log(WLR_ERROR, "Title: set-title-dialog OOM");
        return false;
    }
    free(view->title_override);
    view->title_override = dup;
    fbwl_view_foreign_toplevel_set_title(view, fbwl_view_title(view));
    fbwl_view_decor_update_title_text(view, &server->decor_theme);
    server_toolbar_ui_rebuild(server);
    wlr_log(WLR_INFO, "Title: set title override create_seq=%llu title=%s",
        (unsigned long long)seq,
        fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(null)");
    server->cmd_dialog_target_create_seq = 0;
    return true;
}

static const char *window_placement_str(enum fbwm_window_placement_strategy strategy) {
    switch (strategy) {
    case FBWM_PLACE_ROW_SMART:
        return "RowSmartPlacement";
    case FBWM_PLACE_COL_SMART:
        return "ColSmartPlacement";
    case FBWM_PLACE_CASCADE:
        return "CascadePlacement";
    case FBWM_PLACE_UNDER_MOUSE:
        return "UnderMousePlacement";
    case FBWM_PLACE_ROW_MIN_OVERLAP:
        return "RowMinOverlapPlacement";
    case FBWM_PLACE_COL_MIN_OVERLAP:
        return "ColMinOverlapPlacement";
    case FBWM_PLACE_AUTOTAB:
        return "AutoTabPlacement";
    default:
        return "UnknownPlacement";
    }
}

static const char *row_dir_str(enum fbwm_row_placement_direction dir) {
    switch (dir) {
    case FBWM_ROW_LEFT_TO_RIGHT:
        return "LeftToRight";
    case FBWM_ROW_RIGHT_TO_LEFT:
        return "RightToLeft";
    default:
        return "UnknownRowDir";
    }
}

static const char *col_dir_str(enum fbwm_col_placement_direction dir) {
    switch (dir) {
    case FBWM_COL_TOP_TO_BOTTOM:
        return "TopToBottom";
    case FBWM_COL_BOTTOM_TO_TOP:
        return "BottomToTop";
    default:
        return "UnknownColDir";
    }
}

static struct wlr_scene_tree *window_layer_tree(struct fbwl_server *server, int layer) {
    if (server == NULL) {
        return NULL;
    }

    struct wlr_scene_tree *fallback = server->layer_normal != NULL ? server->layer_normal : &server->scene->tree;

    if (layer <= 0) {
        return server->layer_overlay != NULL ? server->layer_overlay : fallback;
    }
    if (layer <= 6) {
        return server->layer_top != NULL ? server->layer_top : fallback;
    }
    if (layer <= 8) {
        return server->layer_normal != NULL ? server->layer_normal : fallback;
    }
    if (layer <= 10) {
        return server->layer_bottom != NULL ? server->layer_bottom : fallback;
    }
    return server->layer_background != NULL ? server->layer_background : fallback;
}

static struct fbwl_view *window_menu_target_view(struct fbwl_server *server, const char *action) {
    if (server == NULL) {
        return NULL;
    }
    struct fbwl_view *view = server->menu_ui.target_view;
    if (view == NULL) {
        wlr_log(WLR_ERROR, "Menu: %s missing target view", action != NULL ? action : "(action)");
        return NULL;
    }
    return view;
}

static struct fbwl_view *find_view_by_create_seq(struct fbwl_server *server, uint64_t create_seq) {
    if (server == NULL) {
        return NULL;
    }
    for (struct fbwm_view *walk = server->wm.views.next; walk != &server->wm.views; walk = walk->next) {
        struct fbwl_view *view = walk != NULL ? walk->userdata : NULL;
        if (view != NULL && view->create_seq == create_seq) {
            return view;
        }
    }
    return NULL;
}

void server_menu_handle_server_action(struct fbwl_server *server, enum fbwl_menu_server_action action, int arg,
        const char *cmd) {
    if (server == NULL) {
        return;
    }

    switch (action) {
    case FBWL_MENU_SERVER_RECONFIGURE:
        wlr_log(WLR_INFO, "Menu: reconfigure");
        server_reconfigure(server);
        return;
    case FBWL_MENU_SERVER_SET_STYLE: {
        if (cmd == NULL || *cmd == '\0') {
            wlr_log(WLR_ERROR, "Menu: set-style missing path");
            return;
        }

        server->style_file_override = true;
        free(server->style_file);
        server->style_file = strdup(cmd);
        if (server->style_file == NULL) {
            wlr_log(WLR_ERROR, "Menu: set-style OOM");
            return;
        }

        struct fbwl_decor_theme new_theme = {0};
        decor_theme_set_defaults(&new_theme);
        if (!fbwl_style_load_file(&new_theme, server->style_file)) {
            wlr_log(WLR_ERROR, "Menu: set-style failed path=%s", server->style_file);
            return;
        }
        if (server->style_overlay_file != NULL && fbwl_file_exists(server->style_overlay_file)) {
            (void)fbwl_style_load_file(&new_theme, server->style_overlay_file);
        }
        server->decor_theme = new_theme;
        server_toolbar_ui_rebuild(server);
        wlr_log(WLR_INFO, "Menu: set-style ok path=%s", server->style_file);
        return;
    }
    case FBWL_MENU_SERVER_SET_WALLPAPER: {
        if (cmd == NULL || *cmd == '\0') {
            wlr_log(WLR_ERROR, "Menu: set-wallpaper missing path");
            return;
        }

        if (!server_wallpaper_set(server, cmd, FBWL_WALLPAPER_MODE_STRETCH)) {
            wlr_log(WLR_ERROR, "Menu: set-wallpaper failed path=%s", cmd);
            return;
        }

        wlr_log(WLR_INFO, "Menu: set-wallpaper ok path=%s", cmd);
        return;
    }
    case FBWL_MENU_SERVER_SET_FOCUS_MODEL: {
        enum fbwl_focus_model model = (enum fbwl_focus_model)arg;
        if (model != FBWL_FOCUS_MODEL_CLICK_TO_FOCUS &&
                model != FBWL_FOCUS_MODEL_MOUSE_FOCUS &&
                model != FBWL_FOCUS_MODEL_STRICT_MOUSE_FOCUS) {
            wlr_log(WLR_ERROR, "Menu: set-focus-model invalid=%d", arg);
            return;
        }
        size_t screen = 0;
        struct fbwl_screen_config *cfg = active_screen_config(server, &screen);
        if (cfg != NULL) {
            cfg->focus.model = model;
            if (screen == 0) {
                server->focus.model = model;
            }
        } else {
            server->focus.model = model;
        }
        wlr_log(WLR_INFO, "Menu: set-focus-model %s", focus_model_str(server->focus.model));
        return;
    }
    case FBWL_MENU_SERVER_TOGGLE_AUTO_RAISE:
        if (server->screen_configs != NULL) {
            size_t screen = 0;
            struct fbwl_screen_config *cfg = active_screen_config(server, &screen);
            if (cfg != NULL) {
                cfg->focus.auto_raise = !cfg->focus.auto_raise;
                if (screen == 0) {
                    server->focus.auto_raise = cfg->focus.auto_raise;
                }
            } else {
                server->focus.auto_raise = !server->focus.auto_raise;
            }
        } else {
            server->focus.auto_raise = !server->focus.auto_raise;
        }
        wlr_log(WLR_INFO, "Menu: toggle autoRaise=%d", server->focus.auto_raise ? 1 : 0);
        return;
    case FBWL_MENU_SERVER_TOGGLE_CLICK_RAISES:
        if (server->screen_configs != NULL) {
            size_t screen = 0;
            struct fbwl_screen_config *cfg = active_screen_config(server, &screen);
            if (cfg != NULL) {
                cfg->focus.click_raises = !cfg->focus.click_raises;
                if (screen == 0) {
                    server->focus.click_raises = cfg->focus.click_raises;
                }
            } else {
                server->focus.click_raises = !server->focus.click_raises;
            }
        } else {
            server->focus.click_raises = !server->focus.click_raises;
        }
        wlr_log(WLR_INFO, "Menu: toggle clickRaises=%d", server->focus.click_raises ? 1 : 0);
        return;
    case FBWL_MENU_SERVER_TOGGLE_FOCUS_NEW_WINDOWS:
        if (server->screen_configs != NULL) {
            size_t screen = 0;
            struct fbwl_screen_config *cfg = active_screen_config(server, &screen);
            if (cfg != NULL) {
                cfg->focus.focus_new_windows = !cfg->focus.focus_new_windows;
                if (screen == 0) {
                    server->focus.focus_new_windows = cfg->focus.focus_new_windows;
                }
            } else {
                server->focus.focus_new_windows = !server->focus.focus_new_windows;
            }
        } else {
            server->focus.focus_new_windows = !server->focus.focus_new_windows;
        }
        wlr_log(WLR_INFO, "Menu: toggle focusNewWindows=%d", server->focus.focus_new_windows ? 1 : 0);
        return;
    case FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT: {
        enum fbwm_window_placement_strategy strategy = (enum fbwm_window_placement_strategy)arg;
        size_t screen = 0;
        struct fbwl_screen_config *cfg = active_screen_config(server, &screen);
        if (cfg != NULL) {
            cfg->placement_strategy = strategy;
            if (screen == 0) {
                fbwm_core_set_window_placement(&server->wm, strategy);
            }
        } else {
            fbwm_core_set_window_placement(&server->wm, strategy);
        }
        wlr_log(WLR_INFO, "Menu: set-window-placement %s", window_placement_str(strategy));
        return;
    }
    case FBWL_MENU_SERVER_SET_ROW_PLACEMENT_DIRECTION: {
        enum fbwm_row_placement_direction dir = (enum fbwm_row_placement_direction)arg;
        size_t screen = 0;
        struct fbwl_screen_config *cfg = active_screen_config(server, &screen);
        if (cfg != NULL) {
            cfg->placement_row_dir = dir;
            if (screen == 0) {
                fbwm_core_set_row_placement_direction(&server->wm, dir);
            }
        } else {
            fbwm_core_set_row_placement_direction(&server->wm, dir);
        }
        wlr_log(WLR_INFO, "Menu: set-row-placement-direction %s", row_dir_str(dir));
        return;
    }
    case FBWL_MENU_SERVER_SET_COL_PLACEMENT_DIRECTION: {
        enum fbwm_col_placement_direction dir = (enum fbwm_col_placement_direction)arg;
        size_t screen = 0;
        struct fbwl_screen_config *cfg = active_screen_config(server, &screen);
        if (cfg != NULL) {
            cfg->placement_col_dir = dir;
            if (screen == 0) {
                fbwm_core_set_col_placement_direction(&server->wm, dir);
            }
        } else {
            fbwm_core_set_col_placement_direction(&server->wm, dir);
        }
        wlr_log(WLR_INFO, "Menu: set-col-placement-direction %s", col_dir_str(dir));
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_TOGGLE_SHADE: {
        struct fbwl_view *view = window_menu_target_view(server, "toggle-shade");
        if (view == NULL) {
            return;
        }
        fbwl_view_set_shaded(view, !view->shaded, "window-menu");
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_TOGGLE_STICK: {
        struct fbwl_view *view = window_menu_target_view(server, "toggle-stick");
        if (view == NULL) {
            return;
        }
        view->wm_view.sticky = !view->wm_view.sticky;
        wlr_log(WLR_INFO, "Stick: %s %s", fbwl_view_display_title(view), view->wm_view.sticky ? "on" : "off");
        apply_workspace_visibility(server, view->wm_view.sticky ? "stick-on" : "stick-off");
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_RAISE: {
        struct fbwl_view *view = window_menu_target_view(server, "raise");
        if (view == NULL) {
            return;
        }
        server_raise_view(view, "window-menu");
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_LOWER: {
        struct fbwl_view *view = window_menu_target_view(server, "lower");
        if (view == NULL) {
            return;
        }
        server_lower_view(view, "window-menu");
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_SEND_TO_WORKSPACE: {
        struct fbwl_view *view = window_menu_target_view(server, "send-to-workspace");
        if (view == NULL) {
            return;
        }

        const int ws = arg;
        if (ws < 0 || ws >= fbwm_core_workspace_count(&server->wm)) {
            wlr_log(WLR_ERROR, "Menu: send-to-workspace invalid=%d", ws);
            return;
        }

        if (server->wm.focused == &view->wm_view) {
            fbwm_core_move_focused_to_workspace(&server->wm, ws);
        } else {
            view->wm_view.workspace = ws;
            wlr_log(WLR_INFO, "Policy: move window-menu target to workspace %d title=%s app_id=%s",
                ws + 1, fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(null)",
                fbwl_view_app_id(view) != NULL ? fbwl_view_app_id(view) : "(null)");
        }
        apply_workspace_visibility(server, "window-sendto");
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_SET_LAYER: {
        struct fbwl_view *view = window_menu_target_view(server, "set-layer");
        if (view == NULL) {
            return;
        }

        struct fbwl_view *before = server_strict_mousefocus_view_under_cursor(server);

        struct wlr_scene_tree *layer = window_layer_tree(server, arg);
        if (layer == NULL || view->scene_tree == NULL) {
            return;
        }

        view->base_layer = layer;
        if (!view->fullscreen) {
            wlr_scene_node_reparent(&view->scene_tree->node, layer);
        }
        wlr_log(WLR_INFO, "Layer: %s set=%d", fbwl_view_display_title(view), arg);
        if (!view->fullscreen) {
            server_strict_mousefocus_recheck_after_restack(server, before, "window-menu-set-layer");
        }
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_SET_ALPHA_FOCUSED: {
        struct fbwl_view *view = window_menu_target_view(server, "set-alpha-focused");
        if (view == NULL) {
            return;
        }
        int a = arg;
        if (a < 0) {
            a = 0;
        } else if (a > 255) {
            a = 255;
        }
        const uint8_t unfocused = view->alpha_set ? view->alpha_unfocused : 255;
        fbwl_view_set_alpha(view, (uint8_t)a, unfocused, "window-menu");
        view->alpha_is_default = false;
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_SET_ALPHA_UNFOCUSED: {
        struct fbwl_view *view = window_menu_target_view(server, "set-alpha-unfocused");
        if (view == NULL) {
            return;
        }
        int a = arg;
        if (a < 0) {
            a = 0;
        } else if (a > 255) {
            a = 255;
        }
        const uint8_t focused = view->alpha_set ? view->alpha_focused : 255;
        fbwl_view_set_alpha(view, focused, (uint8_t)a, "window-menu");
        view->alpha_is_default = false;
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_SET_TITLE_DIALOG: {
        struct fbwl_view *view = window_menu_target_view(server, "set-title-dialog");
        if (view == NULL) {
            return;
        }

        if (server->scene == NULL || server->output_layout == NULL) {
            return;
        }

        server->cmd_dialog_target_create_seq = view->create_seq;

        server_menu_ui_close(server, "set-title-dialog");
        fbwl_ui_cmd_dialog_open_prompt(&server->cmd_dialog_ui, server->scene, server->layer_overlay,
            &server->decor_theme, server->output_layout, "Set Title: ", "",
            cmd_dialog_submit_set_title, server);
        return;
    }
    case FBWL_MENU_SERVER_FOCUS_VIEW: {
        if (cmd == NULL || *cmd == '\0') {
            wlr_log(WLR_ERROR, "Menu: focus-view missing create_seq");
            return;
        }

        char *end = NULL;
        unsigned long long seq = strtoull(cmd, &end, 10);
        if (end == cmd || end == NULL || *end != '\0') {
            wlr_log(WLR_ERROR, "Menu: focus-view invalid create_seq=%s", cmd);
            return;
        }

        struct fbwl_view *view = find_view_by_create_seq(server, (uint64_t)seq);
        if (view == NULL) {
            wlr_log(WLR_ERROR, "Menu: focus-view no match create_seq=%s", cmd);
            return;
        }

        if (view->minimized) {
            view_set_minimized(view, false, "client-menu");
        }

        if (fbwm_core_view_is_visible(&server->wm, &view->wm_view)) {
            fbwm_core_focus_view(&server->wm, &view->wm_view);
        } else {
            fbwm_core_refocus(&server->wm);
        }
        server_raise_view(view, "client-menu");
        return;
    }
    case FBWL_MENU_SERVER_SLIT_SET_PLACEMENT: {
        enum fbwl_toolbar_placement place = (enum fbwl_toolbar_placement)arg;
        if (place < FBWL_TOOLBAR_PLACEMENT_BOTTOM_LEFT || place > FBWL_TOOLBAR_PLACEMENT_TOP_RIGHT) {
            wlr_log(WLR_ERROR, "Slit: set-placement invalid=%d", arg);
            return;
        }
        size_t screen = 0;
        struct fbwl_screen_config *cfg = slit_screen_config(server, &screen);
        if (cfg != NULL) {
            cfg->slit.placement = place;
        }
        server->slit_ui.placement = place;
        server_slit_ui_rebuild(server);
        wlr_log(WLR_INFO, "Slit: set-placement %s", fbwl_toolbar_placement_str(place));
        return;
    }
    case FBWL_MENU_SERVER_SLIT_SET_LAYER: {
        const int layer_num = arg;
        size_t screen = 0;
        struct fbwl_screen_config *cfg = slit_screen_config(server, &screen);
        if (cfg != NULL) {
            cfg->slit.layer_num = layer_num;
        }
        server->slit_ui.layer_num = layer_num;
        server_slit_ui_rebuild(server);
        wlr_log(WLR_INFO, "Slit: set-layer %d", layer_num);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_SET_ON_HEAD: {
        const int head = arg;
        if (head < 0) {
            wlr_log(WLR_ERROR, "Slit: set-on-head invalid=%d", head);
            return;
        }
        if (server->screen_configs == NULL || server->screen_configs_len < 1) {
            return;
        }
        size_t screen = (size_t)head;
        if (screen >= server->screen_configs_len) {
            wlr_log(WLR_ERROR, "Slit: set-on-head invalid=%d (screens=%zu)", head, server->screen_configs_len);
            return;
        }
        server->screen_configs[0].slit.on_head = (int)screen;
        server->slit_ui.on_head = (int)screen;

        const struct fbwl_screen_config *sl_cfg = fbwl_server_screen_config(server, screen);
        if (sl_cfg != NULL) {
            server->slit_ui.placement = sl_cfg->slit.placement;
            server->slit_ui.layer_num = sl_cfg->slit.layer_num;
            server->slit_ui.auto_hide = sl_cfg->slit.auto_hide;
            server->slit_ui.auto_raise = sl_cfg->slit.auto_raise;
            server->slit_ui.max_over = sl_cfg->slit.max_over;
            server->slit_ui.accept_kde_dockapps = sl_cfg->slit.accept_kde_dockapps;
            server->slit_ui.alpha = sl_cfg->slit.alpha;
            server->slit_ui.direction = sl_cfg->slit.direction;
        }
        server->slit_ui.hidden = false;
        server->slit_ui.hovered = false;
        server->slit_ui.auto_pending = 0;
        server_slit_ui_rebuild(server);
        wlr_log(WLR_INFO, "Slit: set-on-head %d", head + 1);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_TOGGLE_AUTO_HIDE: {
        size_t screen = 0;
        struct fbwl_screen_config *cfg = slit_screen_config(server, &screen);
        const bool new_val = !server->slit_ui.auto_hide;
        if (cfg != NULL) {
            cfg->slit.auto_hide = new_val;
        }
        server->slit_ui.auto_hide = new_val;
        server_slit_ui_rebuild(server);
        wlr_log(WLR_INFO, "Slit: toggle autoHide=%d", new_val ? 1 : 0);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_TOGGLE_AUTO_RAISE: {
        size_t screen = 0;
        struct fbwl_screen_config *cfg = slit_screen_config(server, &screen);
        const bool new_val = !server->slit_ui.auto_raise;
        if (cfg != NULL) {
            cfg->slit.auto_raise = new_val;
        }
        server->slit_ui.auto_raise = new_val;
        server_slit_ui_rebuild(server);
        wlr_log(WLR_INFO, "Slit: toggle autoRaise=%d", new_val ? 1 : 0);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_TOGGLE_MAX_OVER: {
        size_t screen = 0;
        struct fbwl_screen_config *cfg = slit_screen_config(server, &screen);
        const bool new_val = !server->slit_ui.max_over;
        if (cfg != NULL) {
            cfg->slit.max_over = new_val;
        }
        server->slit_ui.max_over = new_val;
        server_slit_ui_rebuild(server);
        wlr_log(WLR_INFO, "Slit: toggle maxOver=%d", new_val ? 1 : 0);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_SET_ALPHA: {
        int a = arg;
        if (a < 0) {
            a = 0;
        } else if (a > 255) {
            a = 255;
        }
        size_t screen = 0;
        struct fbwl_screen_config *cfg = slit_screen_config(server, &screen);
        if (cfg != NULL) {
            cfg->slit.alpha = (uint8_t)a;
        }
        server->slit_ui.alpha = (uint8_t)a;
        server_slit_ui_rebuild(server);
        wlr_log(WLR_INFO, "Slit: set-alpha %d", a);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_ALPHA_PROMPT: {
        if (server->scene == NULL || server->output_layout == NULL) {
            return;
        }
        server_menu_ui_close(server, "slit-alpha-prompt");
        fbwl_ui_cmd_dialog_open_prompt(&server->cmd_dialog_ui, server->scene, server->layer_overlay,
            &server->decor_theme, server->output_layout, "Slit Alpha (0-255): ", "",
            cmd_dialog_submit_slit_alpha, server);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_CYCLE_UP:
        if (slit_cycle_up(&server->slit_ui)) {
            server_slit_ui_rebuild(server);
            wlr_log(WLR_INFO, "Slit: cycle up");
        }
        return;
    case FBWL_MENU_SERVER_SLIT_CYCLE_DOWN:
        if (slit_cycle_down(&server->slit_ui)) {
            server_slit_ui_rebuild(server);
            wlr_log(WLR_INFO, "Slit: cycle down");
        }
        return;
    case FBWL_MENU_SERVER_SLIT_TOGGLE_CLIENT_VISIBLE: {
        if (cmd == NULL || *cmd == '\0') {
            wlr_log(WLR_ERROR, "Slit: toggle-client-visible missing create_seq");
            return;
        }
        char *end = NULL;
        unsigned long long seq = strtoull(cmd, &end, 10);
        if (end == cmd || end == NULL || *end != '\0') {
            wlr_log(WLR_ERROR, "Slit: toggle-client-visible invalid create_seq=%s", cmd);
            return;
        }
        struct fbwl_slit_item *it;
        wl_list_for_each(it, &server->slit_ui.items, link) {
            struct fbwl_view *view = it->view;
            if (view == NULL) {
                continue;
            }
            if (view->create_seq != (uint64_t)seq) {
                continue;
            }
            it->visible = !it->visible;
            if (view->scene_tree != NULL) {
                wlr_scene_node_set_enabled(&view->scene_tree->node, it->visible);
            }
            server_slit_ui_rebuild(server);
            wlr_log(WLR_INFO, "Slit: client visible=%d title=%s", it->visible ? 1 : 0, fbwl_view_display_title(view));
            return;
        }
        wlr_log(WLR_ERROR, "Slit: toggle-client-visible no match create_seq=%s", cmd);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_CLIENT_UP:
    case FBWL_MENU_SERVER_SLIT_CLIENT_DOWN: {
        if (cmd == NULL || *cmd == '\0') {
            wlr_log(WLR_ERROR, "Slit: client-move missing create_seq");
            return;
        }
        char *end = NULL;
        unsigned long long seq = strtoull(cmd, &end, 10);
        if (end == cmd || end == NULL || *end != '\0') {
            wlr_log(WLR_ERROR, "Slit: client-move invalid create_seq=%s", cmd);
            return;
        }
        struct fbwl_slit_item *it;
        wl_list_for_each(it, &server->slit_ui.items, link) {
            struct fbwl_view *view = it->view;
            if (view == NULL) {
                continue;
            }
            if (view->create_seq != (uint64_t)seq) {
                continue;
            }

            bool moved = false;
            if (action == FBWL_MENU_SERVER_SLIT_CLIENT_UP) {
                moved = slit_client_up(&server->slit_ui, it);
            } else {
                moved = slit_client_down(&server->slit_ui, it);
            }
            if (!moved) {
                return;
            }

            server_slit_ui_rebuild(server);
            wlr_log(WLR_INFO, "Slit: client move %s title=%s",
                action == FBWL_MENU_SERVER_SLIT_CLIENT_UP ? "up" : "down",
                fbwl_view_display_title(view));
            return;
        }

        wlr_log(WLR_ERROR, "Slit: client-move no match create_seq=%s", cmd);
        return;
    }
    case FBWL_MENU_SERVER_SLIT_SAVE_CLIENT_LIST: {
        if (server->slitlist_file == NULL || *server->slitlist_file == '\0') {
            wlr_log(WLR_ERROR, "Slit: save slitlist missing session.slitlistFile");
            return;
        }
        (void)fbwl_ui_slit_save_order_file(&server->slit_ui, server->slitlist_file);
        return;
    }
    default:
        wlr_log(WLR_ERROR, "Menu: unknown server-action=%d", (int)action);
        return;
    }
}
