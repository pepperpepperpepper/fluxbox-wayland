#include "wayland/fbwl_view_attention.h"

#include <stdbool.h>

#include <wayland-server-core.h>

#include <wlr/util/log.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_view.h"

static void view_attention_stop(struct fbwl_view *view) {
    if (view == NULL) {
        return;
    }
    if (view->attention_timer != NULL) {
        wl_event_source_remove(view->attention_timer);
        view->attention_timer = NULL;
    }
    view->attention_active = false;
    view->attention_state = false;
    view->attention_interval_ms = 0;
    view->attention_toggle_count = 0;
    view->attention_from_xwayland_urgency = false;
}

static int view_attention_timer_cb(void *data) {
    struct fbwl_view *view = data;
    if (view == NULL || view->server == NULL) {
        return 0;
    }

    struct fbwl_server *server = view->server;
    if (!view->attention_active || view->attention_timer == NULL) {
        return 0;
    }
    if (view->attention_interval_ms <= 0) {
        view_attention_stop(view);
        return 0;
    }
    if (server->focused_view == view) {
        view_attention_stop(view);
        return 0;
    }

    view->attention_state = !view->attention_state;
    fbwl_view_decor_set_active(view, &server->decor_theme, view->attention_state);
    if (view->attention_toggle_count < 3) {
        wlr_log(WLR_INFO, "Attention: toggle title=%s state=%d",
            fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
            view->attention_state ? 1 : 0);
        view->attention_toggle_count++;
    }
    wl_event_source_timer_update(view->attention_timer, view->attention_interval_ms);
    return 0;
}

void fbwl_view_attention_request(struct fbwl_view *view, int interval_ms, const struct fbwl_decor_theme *theme,
        const char *why) {
    if (view == NULL || view->server == NULL) {
        return;
    }

    struct fbwl_server *server = view->server;
    if (interval_ms <= 0) {
        return;
    }
    if (server->focused_view == view) {
        return;
    }
    if (view->attention_active) {
        return;
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    if (loop == NULL) {
        return;
    }

    view->attention_active = true;
    view->attention_state = false;
    view->attention_interval_ms = interval_ms;
    view->attention_toggle_count = 0;
    view->attention_timer = wl_event_loop_add_timer(loop, view_attention_timer_cb, view);
    if (view->attention_timer == NULL) {
        view_attention_stop(view);
        return;
    }
    wlr_log(WLR_INFO, "Attention: start title=%s interval=%d why=%s",
        fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
        interval_ms,
        why != NULL ? why : "(null)");
    if (theme != NULL && server->scene != NULL && server->output_layout != NULL) {
        fbwl_ui_osd_show_attention(&server->osd_ui, server->scene, server->layer_top,
            theme, server->output_layout, fbwl_view_display_title(view));
    }
    server_toolbar_ui_rebuild(server);
    wl_event_source_timer_update(view->attention_timer, interval_ms);
}

void fbwl_view_attention_clear(struct fbwl_view *view, const struct fbwl_decor_theme *theme, const char *why) {
    if (view == NULL || view->server == NULL) {
        return;
    }
    const bool was_active = view->attention_active;
    view_attention_stop(view);
    if (was_active) {
        wlr_log(WLR_INFO, "Attention: clear title=%s why=%s",
            fbwl_view_title(view) != NULL ? fbwl_view_title(view) : "(no-title)",
            why != NULL ? why : "(null)");
    }
    if (theme != NULL && view->server->focused_view != view) {
        fbwl_view_decor_set_active(view, theme, false);
    }
    if (was_active) {
        server_toolbar_ui_rebuild(view->server);
    }
}

void fbwl_view_attention_finish(struct fbwl_view *view) {
    view_attention_stop(view);
}
