#include <stddef.h>
#include <stdlib.h>

#include <wlr/backend.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/xwayland.h>

#include "wayland/fbwl_output.h"
#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_xembed_sni_proxy.h"
#include "wayland/fbwl_util.h"

void fbwl_server_finish(struct fbwl_server *server) {
    if (server == NULL) {
        return;
    }

    fbwl_xembed_sni_proxy_stop(server);

    if (server->slitlist_file != NULL && *server->slitlist_file != '\0') {
        (void)fbwl_ui_slit_save_order_file(&server->slit_ui, server->slitlist_file);
    }

    if (server->wl_display != NULL) {
        wl_display_destroy_clients(server->wl_display);
    }
    fbwl_ipc_finish(&server->ipc);
#ifdef HAVE_SYSTEMD
    fbwl_sni_finish(&server->sni);
#endif

    fbwl_cleanup_listener(&server->new_xdg_toplevel);
    fbwl_cleanup_listener(&server->new_xdg_popup);
    fbwl_xdg_activation_finish(&server->xdg_activation);
    fbwl_xdg_decoration_finish(&server->xdg_decoration);
    fbwl_cleanup_listener(&server->xwayland_ready);
    fbwl_cleanup_listener(&server->xwayland_new_surface);
    fbwl_cleanup_listener(&server->new_layer_surface);

    fbwl_cleanup_listener(&server->cursor_motion);
    fbwl_cleanup_listener(&server->cursor_motion_absolute);
    fbwl_cleanup_listener(&server->cursor_button);
    fbwl_cleanup_listener(&server->cursor_axis);
    fbwl_cleanup_listener(&server->cursor_frame);
    fbwl_cleanup_listener(&server->cursor_shape_request_set_shape);

    fbwl_cleanup_listener(&server->new_input);
    fbwl_cleanup_listener(&server->request_cursor);
    fbwl_cleanup_listener(&server->request_set_selection);
    fbwl_cleanup_listener(&server->request_set_primary_selection);
    fbwl_cleanup_listener(&server->request_start_drag);
    fbwl_shortcuts_inhibit_finish(&server->shortcuts_inhibit);
    fbwl_cleanup_listener(&server->new_virtual_keyboard);
    fbwl_cleanup_listener(&server->new_virtual_pointer);
    fbwl_pointer_constraints_finish(&server->pointer_constraints);
    fbwl_idle_finish(&server->idle);
    fbwl_session_lock_finish(&server->session_lock);
    fbwl_cleanup_listener(&server->output_manager_apply);
    fbwl_cleanup_listener(&server->output_manager_test);
    fbwl_cleanup_listener(&server->output_power_set_mode);
    fbwl_text_input_finish(&server->text_input);

    fbwl_cleanup_listener(&server->new_output);

    if (server->xwayland != NULL) {
        wlr_xwayland_destroy(server->xwayland);
        server->xwayland = NULL;
    }

    server_cmd_dialog_ui_close(server, "shutdown");
    server_osd_ui_destroy(server);
    fbwl_ui_tooltip_destroy(&server->tooltip_ui);
    if (server->auto_raise_timer != NULL) {
        wl_event_source_remove(server->auto_raise_timer);
    server->auto_raise_timer = NULL;
    }
    server->auto_raise_pending_view = NULL;
    server_menu_free(server);
    free(server->config_dir);
    server->config_dir = NULL;
    free(server->group_file);
    server->group_file = NULL;
    free(server->keys_file);
    server->keys_file = NULL;
    free(server->apps_file);
    server->apps_file = NULL;
    free(server->style_file);
    server->style_file = NULL;
    free(server->style_overlay_file);
    server->style_overlay_file = NULL;
    free(server->slitlist_file);
    server->slitlist_file = NULL;
    free(server->screen_configs);
    server->screen_configs = NULL;
    server->screen_configs_len = 0;
    fbwl_ui_toolbar_destroy(&server->toolbar_ui);
    fbwl_ui_slit_destroy(&server->slit_ui);
    fbwm_core_finish(&server->wm);

    struct fbwl_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->background_image != NULL) {
            wlr_scene_node_destroy(&out->background_image->node);
            out->background_image = NULL;
        }
        if (out->background_rect != NULL) {
            wlr_scene_node_destroy(&out->background_rect->node);
            out->background_rect = NULL;
        }
    }

    free(server->wallpaper_path);
    server->wallpaper_path = NULL;
    if (server->wallpaper_buf != NULL) {
        wlr_buffer_drop(server->wallpaper_buf);
        server->wallpaper_buf = NULL;
    }

    if (server->scene != NULL) {
        wlr_scene_node_destroy(&server->scene->tree.node);
        server->scene = NULL;
    }

    if (server->cursor_mgr != NULL) {
        wlr_xcursor_manager_destroy(server->cursor_mgr);
        server->cursor_mgr = NULL;
    }
    if (server->cursor != NULL) {
        wlr_cursor_destroy(server->cursor);
        server->cursor = NULL;
    }
    if (server->allocator != NULL) {
        wlr_allocator_destroy(server->allocator);
        server->allocator = NULL;
    }
    if (server->renderer != NULL) {
        wlr_renderer_destroy(server->renderer);
        server->renderer = NULL;
    }
    if (server->backend != NULL) {
        wlr_backend_destroy(server->backend);
        server->backend = NULL;
    }
    fbwl_apps_rules_free(&server->apps_rules, &server->apps_rule_count);
    fbwl_keybindings_free(&server->keybindings, &server->keybinding_count);
    fbwl_mousebindings_free(&server->mousebindings, &server->mousebinding_count);
    free(server->marked_windows.items);
    server->marked_windows.items = NULL;
    server->marked_windows.len = 0;
    server->marked_windows.cap = 0;
    free(server->key_mode);
    server->key_mode = NULL;
    free(server->keychain_saved_mode);
    server->keychain_saved_mode = NULL;
    server->keychain_active = false;
    server->keychain_start_time_msec = 0;
    server->change_workspace_binding_active = false;
    free(server->restart_cmd);
    server->restart_cmd = NULL;
    server->restart_requested = false;
    if (server->protocol_logger != NULL) {
        wl_protocol_logger_destroy(server->protocol_logger);
        server->protocol_logger = NULL;
    }
    if (server->wl_display != NULL) {
        wl_display_destroy(server->wl_display);
        server->wl_display = NULL;
    }
}
