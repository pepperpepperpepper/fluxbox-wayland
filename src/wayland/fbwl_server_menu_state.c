#include "wayland/fbwl_server_menu_state.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_scene.h>

#include "wayland/fbwl_menu.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"

static const struct fbwl_screen_config *screen_cfg_for_menu(const struct fbwl_server *server, int x, int y) {
    return server != NULL ? fbwl_server_screen_config_at(server, (double)x, (double)y) : NULL;
}

static enum fbwl_focus_model focus_model_for_menu(const struct fbwl_server *server, const struct fbwl_screen_config *cfg) {
    return cfg != NULL ? cfg->focus.model : (server != NULL ? server->focus.model : FBWL_FOCUS_MODEL_CLICK_TO_FOCUS);
}

static bool auto_raise_for_menu(const struct fbwl_server *server, const struct fbwl_screen_config *cfg) {
    return cfg != NULL ? cfg->focus.auto_raise : (server != NULL ? server->focus.auto_raise : false);
}

static bool click_raises_for_menu(const struct fbwl_server *server, const struct fbwl_screen_config *cfg) {
    return cfg != NULL ? cfg->focus.click_raises : (server != NULL ? server->focus.click_raises : false);
}

static bool focus_new_windows_for_menu(const struct fbwl_server *server, const struct fbwl_screen_config *cfg) {
    return cfg != NULL ? cfg->focus.focus_new_windows : (server != NULL ? server->focus.focus_new_windows : false);
}

static enum fbwm_window_placement_strategy placement_strategy_for_menu(const struct fbwl_server *server,
        const struct fbwl_screen_config *cfg) {
    return cfg != NULL ? cfg->placement_strategy :
        (server != NULL ? fbwm_core_window_placement(&server->wm) : FBWM_PLACE_ROW_SMART);
}

static enum fbwm_row_placement_direction row_dir_for_menu(const struct fbwl_server *server, const struct fbwl_screen_config *cfg) {
    return cfg != NULL ? cfg->placement_row_dir :
        (server != NULL ? fbwm_core_row_placement_direction(&server->wm) : FBWM_ROW_LEFT_TO_RIGHT);
}

static enum fbwm_col_placement_direction col_dir_for_menu(const struct fbwl_server *server, const struct fbwl_screen_config *cfg) {
    return cfg != NULL ? cfg->placement_col_dir :
        (server != NULL ? fbwm_core_col_placement_direction(&server->wm) : FBWM_COL_TOP_TO_BOTTOM);
}

static bool view_layer_matches_menu_arg(const struct fbwl_server *server, const struct fbwl_view *view, int layer_arg) {
    if (server == NULL || view == NULL) {
        return false;
    }

    struct wlr_scene_tree *want = NULL;
    if (layer_arg <= 0) {
        want = server->layer_overlay;
    } else if (layer_arg <= 6) {
        want = server->layer_top;
    } else if (layer_arg <= 8) {
        want = server->layer_normal;
    } else if (layer_arg <= 10) {
        want = server->layer_bottom;
    } else {
        want = server->layer_background;
    }
    if (want == NULL) {
        want = server->layer_normal != NULL ? server->layer_normal : &server->scene->tree;
    }

    return view->base_layer == want;
}

static bool parse_u64_strict(const char *s, uint64_t *out) {
    if (out != NULL) {
        *out = 0;
    }
    if (s == NULL || *s == '\0' || out == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s || end == NULL || *end != '\0') {
        return false;
    }
    if (errno != 0) {
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

static bool slit_client_visible_for_seq(const struct fbwl_server *server, const char *create_seq_str) {
    if (server == NULL || create_seq_str == NULL || *create_seq_str == '\0') {
        return false;
    }
    uint64_t seq = 0;
    if (!parse_u64_strict(create_seq_str, &seq)) {
        return false;
    }
    const struct fbwl_slit_item *it;
    wl_list_for_each(it, &server->slit_ui.items, link) {
        const struct fbwl_view *view = it->view;
        if (view != NULL && view->create_seq == seq) {
            return it->visible;
        }
    }
    return false;
}

static void menu_sync_item(struct fbwl_server *server, struct fbwl_menu_item *it, struct fbwl_view *target_view,
        const struct fbwl_screen_config *cfg, size_t head0, int cur_ws) {
    if (it == NULL) {
        return;
    }

    it->toggle = false;
    it->selected = false;

    switch (it->kind) {
    case FBWL_MENU_ITEM_WORKSPACE_SWITCH:
        it->toggle = true;
        it->selected = cur_ws >= 0 && it->arg == cur_ws;
        return;
    case FBWL_MENU_ITEM_VIEW_ACTION:
        if (target_view == NULL) {
            return;
        }
        switch (it->view_action) {
        case FBWL_MENU_VIEW_TOGGLE_MINIMIZE:
            it->toggle = true;
            it->selected = target_view->minimized;
            return;
        case FBWL_MENU_VIEW_TOGGLE_MAXIMIZE:
            it->toggle = true;
            it->selected = target_view->maximized;
            return;
        case FBWL_MENU_VIEW_TOGGLE_FULLSCREEN:
            it->toggle = true;
            it->selected = target_view->fullscreen;
            return;
        case FBWL_MENU_VIEW_CLOSE:
        default:
            return;
        }
    case FBWL_MENU_ITEM_SERVER_ACTION:
        break;
    default:
        return;
    }

    switch (it->server_action) {
    case FBWL_MENU_SERVER_SET_FOCUS_MODEL: {
        it->toggle = true;
        enum fbwl_focus_model model = focus_model_for_menu(server, cfg);
        it->selected = (enum fbwl_focus_model)it->arg == model;
        return;
    }
    case FBWL_MENU_SERVER_TOGGLE_AUTO_RAISE:
        it->toggle = true;
        it->selected = auto_raise_for_menu(server, cfg);
        return;
    case FBWL_MENU_SERVER_TOGGLE_CLICK_RAISES:
        it->toggle = true;
        it->selected = click_raises_for_menu(server, cfg);
        return;
    case FBWL_MENU_SERVER_TOGGLE_FOCUS_NEW_WINDOWS:
        it->toggle = true;
        it->selected = focus_new_windows_for_menu(server, cfg);
        return;
    case FBWL_MENU_SERVER_SET_WINDOW_PLACEMENT: {
        it->toggle = true;
        enum fbwm_window_placement_strategy s = placement_strategy_for_menu(server, cfg);
        it->selected = (int)s == it->arg;
        return;
    }
    case FBWL_MENU_SERVER_SET_ROW_PLACEMENT_DIRECTION: {
        it->toggle = true;
        enum fbwm_row_placement_direction d = row_dir_for_menu(server, cfg);
        it->selected = (int)d == it->arg;
        return;
    }
    case FBWL_MENU_SERVER_SET_COL_PLACEMENT_DIRECTION: {
        it->toggle = true;
        enum fbwm_col_placement_direction d = col_dir_for_menu(server, cfg);
        it->selected = (int)d == it->arg;
        return;
    }
    case FBWL_MENU_SERVER_WINDOW_TOGGLE_SHADE:
        it->toggle = true;
        it->selected = target_view != NULL && target_view->shaded;
        return;
    case FBWL_MENU_SERVER_WINDOW_TOGGLE_STICK:
        it->toggle = true;
        it->selected = target_view != NULL && target_view->wm_view.sticky;
        return;
    case FBWL_MENU_SERVER_WINDOW_SEND_TO_WORKSPACE:
        it->toggle = true;
        it->selected = target_view != NULL && !target_view->wm_view.sticky && target_view->wm_view.workspace == it->arg;
        return;
    case FBWL_MENU_SERVER_WINDOW_SET_LAYER:
        it->toggle = true;
        it->selected = target_view != NULL && view_layer_matches_menu_arg(server, target_view, it->arg);
        return;
    case FBWL_MENU_SERVER_WINDOW_SET_ALPHA_FOCUSED:
        it->toggle = true;
        if (target_view != NULL) {
            const uint8_t cur = target_view->alpha_set ? target_view->alpha_focused : server->window_alpha_default_focused;
            it->selected = cur == (uint8_t)it->arg;
        }
        return;
    case FBWL_MENU_SERVER_WINDOW_SET_ALPHA_UNFOCUSED:
        it->toggle = true;
        if (target_view != NULL) {
            const uint8_t cur = target_view->alpha_set ? target_view->alpha_unfocused : server->window_alpha_default_unfocused;
            it->selected = cur == (uint8_t)it->arg;
        }
        return;
    case FBWL_MENU_SERVER_FOCUS_VIEW: {
        it->toggle = true;
        if (server != NULL && server->wm.focused != NULL) {
            const struct fbwl_view *focus = server->wm.focused->userdata;
            uint64_t seq = 0;
            if (focus != NULL && it->cmd != NULL && parse_u64_strict(it->cmd, &seq)) {
                it->selected = focus->create_seq == seq;
            }
        }
        return;
    }
    case FBWL_MENU_SERVER_SLIT_SET_PLACEMENT:
        it->toggle = true;
        it->selected = server != NULL && server->slit_ui.placement == (enum fbwl_toolbar_placement)it->arg;
        return;
    case FBWL_MENU_SERVER_SLIT_SET_LAYER:
        it->toggle = true;
        it->selected = server != NULL && server->slit_ui.layer_num == it->arg;
        return;
    case FBWL_MENU_SERVER_SLIT_SET_ON_HEAD:
        it->toggle = true;
        it->selected = server != NULL && server->slit_ui.on_head == it->arg;
        return;
    case FBWL_MENU_SERVER_SLIT_TOGGLE_AUTO_HIDE:
        it->toggle = true;
        it->selected = server != NULL && server->slit_ui.auto_hide;
        return;
    case FBWL_MENU_SERVER_SLIT_TOGGLE_AUTO_RAISE:
        it->toggle = true;
        it->selected = server != NULL && server->slit_ui.auto_raise;
        return;
    case FBWL_MENU_SERVER_SLIT_TOGGLE_MAX_OVER:
        it->toggle = true;
        it->selected = server != NULL && server->slit_ui.max_over;
        return;
    case FBWL_MENU_SERVER_SLIT_SET_ALPHA:
        it->toggle = true;
        it->selected = server != NULL && server->slit_ui.alpha == (uint8_t)it->arg;
        return;
    case FBWL_MENU_SERVER_SLIT_TOGGLE_CLIENT_VISIBLE:
        it->toggle = true;
        it->selected = server != NULL && slit_client_visible_for_seq(server, it->cmd);
        return;
    case FBWL_MENU_SERVER_SLIT_CYCLE_UP:
    case FBWL_MENU_SERVER_SLIT_CYCLE_DOWN:
    case FBWL_MENU_SERVER_SLIT_SAVE_CLIENT_LIST:
    case FBWL_MENU_SERVER_SLIT_CLIENT_UP:
    case FBWL_MENU_SERVER_SLIT_CLIENT_DOWN:
    case FBWL_MENU_SERVER_SLIT_ALPHA_PROMPT:
    case FBWL_MENU_SERVER_RECONFIGURE:
    case FBWL_MENU_SERVER_SET_STYLE:
    case FBWL_MENU_SERVER_SET_WALLPAPER:
    default:
        (void)head0;
        return;
    }
}

static void menu_sync_recurse(struct fbwl_server *server, struct fbwl_menu *menu, struct fbwl_view *target_view,
        const struct fbwl_screen_config *cfg, size_t head0, int cur_ws) {
    if (menu == NULL) {
        return;
    }
    for (size_t i = 0; i < menu->item_count; i++) {
        struct fbwl_menu_item *it = &menu->items[i];
        menu_sync_item(server, it, target_view, cfg, head0, cur_ws);
        if (it->submenu != NULL) {
            menu_sync_recurse(server, it->submenu, target_view, cfg, head0, cur_ws);
        }
    }
}

void server_menu_sync_toggle_states(struct fbwl_server *server, struct fbwl_menu *menu,
        struct fbwl_view *target_view, int menu_x, int menu_y) {
    if (server == NULL || menu == NULL) {
        return;
    }

    const struct fbwl_screen_config *cfg = screen_cfg_for_menu(server, menu_x, menu_y);
    const size_t head0 = fbwl_server_screen_index_at(server, (double)menu_x, (double)menu_y);
    const int cur_ws = fbwm_core_workspace_current_for_head(&server->wm, head0);
    menu_sync_recurse(server, menu, target_view, cfg, head0, cur_ws);
}
