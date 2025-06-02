extern "C" {
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_seat.h>
}

#include "surface.hpp"
#include "server.hpp"
#include "workspace.hpp"
#include "theme.hpp"
#include "scene_wrapper.h"

FluxboxSurface::FluxboxSurface(FluxboxServer* server, struct wlr_xdg_toplevel* toplevel)
    : server(server)
    , toplevel(toplevel)
    , scene_tree(nullptr)
    , workspace(nullptr)
    , focused(false)
    , maximized(false)
    , minimized(false)
    , decorated(true) {
    
    // Create scene tree for this surface
    scene_tree = fluxbox_scene_xdg_surface_create(server->get_scene(), toplevel->base);
    
    // Initialize saved geometry
    saved_geometry = {0, 0, 0, 0};
    
    // Initialize decorations structure
    decorations.tree = nullptr;
    decorations.titlebar = nullptr;
    decorations.border_top = nullptr;
    decorations.border_bottom = nullptr;
    decorations.border_left = nullptr;
    decorations.border_right = nullptr;
    
    setup_listeners();
    
    // Create decorations if enabled
    if (decorated) {
        update_decorations();
    }
}

FluxboxSurface::~FluxboxSurface() {
    // Remove listeners
    wl_list_remove(&destroy.link);
    wl_list_remove(&map.link);
    wl_list_remove(&unmap.link);
    wl_list_remove(&commit.link);
    wl_list_remove(&request_move.link);
    wl_list_remove(&request_resize.link);
    wl_list_remove(&request_maximize.link);
    wl_list_remove(&request_minimize.link);
    wl_list_remove(&request_fullscreen.link);
    wl_list_remove(&set_title.link);
    wl_list_remove(&set_app_id.link);
}

void FluxboxSurface::setup_listeners() {
    destroy.notify = handle_destroy;
    wl_signal_add(&toplevel->base->events.destroy, &destroy);
    
    map.notify = handle_map;
    wl_signal_add(&toplevel->base->surface->events.map, &map);
    
    unmap.notify = handle_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &unmap);
    
    commit.notify = handle_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &commit);
    
    request_move.notify = handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &request_move);
    
    request_resize.notify = handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &request_resize);
    
    request_maximize.notify = handle_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &request_maximize);
    
    request_minimize.notify = handle_request_minimize;
    wl_signal_add(&toplevel->events.request_minimize, &request_minimize);
    
    request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &request_fullscreen);
    
    set_title.notify = handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &set_title);
    
    set_app_id.notify = handle_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &set_app_id);
}

void FluxboxSurface::set_position(int x, int y) {
    // If decorated, adjust position to account for titlebar
    if (decorated) {
        FluxboxTheme* theme = server->get_theme();
        const int titlebar_height = theme ? theme->get_titlebar_height() : 20;
        fluxbox_scene_node_set_position(scene_tree, x, y + titlebar_height);
    } else {
        fluxbox_scene_node_set_position(scene_tree, x, y);
    }
}

void FluxboxSurface::get_position(int* x, int* y) const {
    fluxbox_scene_node_get_position(scene_tree, x, y);
    // If decorated, adjust position to account for titlebar
    if (decorated && y) {
        FluxboxTheme* theme = server->get_theme();
        const int titlebar_height = theme ? theme->get_titlebar_height() : 20;
        *y -= titlebar_height;
    }
}

void FluxboxSurface::set_size(int width, int height) {
    wlr_xdg_toplevel_set_size(toplevel, width, height);
}

void FluxboxSurface::get_size(int* width, int* height) const {
    struct wlr_box box;
    wlr_xdg_surface_get_geometry(toplevel->base, &box);
    if (width) *width = box.width;
    if (height) *height = box.height;
}

void FluxboxSurface::focus() {
    if (focused) {
        return;
    }
    
    focused = true;
    wlr_xdg_toplevel_set_activated(toplevel, true);
    
    // Set keyboard focus
    struct wlr_surface* wlr_surface = toplevel->base->surface;
    struct wlr_seat* seat = server->get_seat();
    
    if (wlr_surface) {
        wlr_seat_keyboard_notify_enter(seat, wlr_surface, nullptr, 0, nullptr);
    }
    
    // Update decoration colors
    if (decorated) {
        update_decorations();
    }
}

void FluxboxSurface::unfocus() {
    if (!focused) {
        return;
    }
    
    focused = false;
    wlr_xdg_toplevel_set_activated(toplevel, false);
    
    // Update decoration colors
    if (decorated) {
        update_decorations();
    }
}

void FluxboxSurface::maximize() {
    if (maximized) {
        return;
    }
    
    // Save current geometry
    get_position(&saved_geometry.x, &saved_geometry.y);
    get_size(&saved_geometry.width, &saved_geometry.height);
    
    maximized = true;
    wlr_xdg_toplevel_set_maximized(toplevel, true);
    
    // Set to full screen size (simplified - should use output bounds)
    set_position(0, 0);
    set_size(1920, 1080); // TODO: Get actual output size
}

void FluxboxSurface::unmaximize() {
    if (!maximized) {
        return;
    }
    
    maximized = false;
    wlr_xdg_toplevel_set_maximized(toplevel, false);
    
    // Restore saved geometry
    set_position(saved_geometry.x, saved_geometry.y);
    set_size(saved_geometry.width, saved_geometry.height);
}

void FluxboxSurface::minimize() {
    if (minimized) {
        return;
    }
    
    minimized = true;
    fluxbox_scene_node_set_enabled(scene_tree, false);
}

void FluxboxSurface::unminimize() {
    if (!minimized) {
        return;
    }
    
    minimized = false;
    fluxbox_scene_node_set_enabled(scene_tree, true);
}

void FluxboxSurface::close() {
    wlr_xdg_toplevel_send_close(toplevel);
}

void FluxboxSurface::move_to_workspace(FluxboxWorkspace* new_workspace) {
    if (workspace == new_workspace) {
        return;
    }
    
    if (workspace) {
        workspace->remove_surface(this);
    }
    
    workspace = new_workspace;
    
    if (workspace) {
        // Move scene tree to workspace scene tree
        fluxbox_scene_node_reparent(scene_tree, workspace->get_scene_tree());
    }
}

const char* FluxboxSurface::get_title() const {
    return toplevel->title;
}

const char* FluxboxSurface::get_app_id() const {
    return toplevel->app_id;
}

// Event handlers
void FluxboxSurface::handle_destroy(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, destroy);
    surface->server->remove_surface(surface);
    delete surface;
}

void FluxboxSurface::handle_request_move(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, request_move);
    
    // The cursor system will handle the actual move
    // This is typically triggered when a client requests a move (e.g., dragging titlebar)
    // The actual move handling is done by the cursor when Alt+Left click is used
}

void FluxboxSurface::handle_request_resize(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, request_resize);
    struct wlr_xdg_toplevel_resize_event* event = 
        static_cast<struct wlr_xdg_toplevel_resize_event*>(data);
    
    // The cursor system will handle the actual resize
    // For now, we'll need to find a way to trigger the cursor's begin_resize
    // This typically happens through the seat's request_set_cursor event
    
    // The resize is initiated by the client, so we acknowledge it
    // The actual resize handling is done by the cursor when Alt+Right click is used
}

void FluxboxSurface::handle_request_maximize(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, request_maximize);
    
    // Toggle maximize state
    if (surface->is_maximized()) {
        surface->unmaximize();
    } else {
        surface->maximize();
    }
}

void FluxboxSurface::handle_request_minimize(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, request_minimize);
    
    // Minimize the window
    surface->minimize();
}

void FluxboxSurface::handle_request_fullscreen(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, request_fullscreen);
    
    // Toggle fullscreen state
    bool currently_fullscreen = surface->toplevel->requested.fullscreen;
    wlr_xdg_toplevel_set_fullscreen(surface->toplevel, !currently_fullscreen);
}

void FluxboxSurface::handle_set_title(struct wl_listener* listener, void* data) {
    // Title changed - could update window list, taskbar, etc.
}

void FluxboxSurface::handle_set_app_id(struct wl_listener* listener, void* data) {
    // App ID changed - could update window list, taskbar, etc.
}

void FluxboxSurface::handle_map(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, map);
    
    // When the surface is mapped (ready to be displayed), ensure it's visible
    fluxbox_scene_node_set_enabled(surface->scene_tree, true);
    
    // Focus the newly mapped surface
    surface->server->set_focus(surface);
}

void FluxboxSurface::handle_unmap(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, unmap);
    
    // When unmapped, hide the surface
    fluxbox_scene_node_set_enabled(surface->scene_tree, false);
    
    // If this was the focused surface, unfocus it
    if (surface->server->get_focused_surface() == surface) {
        surface->server->set_focus(nullptr);
    }
}

void FluxboxSurface::handle_commit(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, commit);
    
    // Handle surface commits (updates)
    // The scene graph will automatically handle rendering
    
    // Update decorations if size changed
    if (surface->decorated) {
        surface->update_decorations();
    }
}

void FluxboxSurface::set_decorated(bool decorated) {
    if (this->decorated == decorated) {
        return;
    }
    
    this->decorated = decorated;
    update_decorations();
}

void FluxboxSurface::update_decorations() {
    if (!decorated) {
        // Remove decorations if they exist
        if (decorations.tree) {
            fluxbox_scene_node_destroy(decorations.tree);
            decorations.tree = nullptr;
            decorations.titlebar = nullptr;
            decorations.border_top = nullptr;
            decorations.border_bottom = nullptr;
            decorations.border_left = nullptr;
            decorations.border_right = nullptr;
        }
        return;
    }
    
    // Create decoration tree if it doesn't exist
    if (!decorations.tree) {
        decorations.tree = fluxbox_scene_tree_create_subsurface(scene_tree);
        if (!decorations.tree) {
            return;
        }
    }
    
    // Get surface geometry
    struct wlr_box box;
    wlr_xdg_surface_get_geometry(toplevel->base, &box);
    
    // Get decoration dimensions from theme
    FluxboxTheme* theme = server->get_theme();
    const int titlebar_height = theme ? theme->get_titlebar_height() : 20;
    const int border_width = theme ? theme->get_border_width() : 1;
    
    // Get colors from theme
    std::array<float, 4> titlebar_color_array = theme ? theme->get_titlebar_color(focused) 
                                                       : std::array<float, 4>{0.2f, 0.2f, 0.3f, 1.0f};
    std::array<float, 4> border_color_array = theme ? theme->get_border_color(focused)
                                                     : std::array<float, 4>{0.15f, 0.15f, 0.2f, 1.0f};
    
    float* tb_color = titlebar_color_array.data();
    float* b_color = border_color_array.data();
    
    // Create or update titlebar
    if (!decorations.titlebar) {
        decorations.titlebar = fluxbox_scene_rect_create(decorations.tree, 
            box.width, titlebar_height, tb_color);
    } else {
        fluxbox_scene_rect_set_size(decorations.titlebar, box.width, titlebar_height);
        fluxbox_scene_rect_set_color(decorations.titlebar, tb_color);
    }
    fluxbox_scene_rect_set_position(decorations.titlebar, 0, -titlebar_height);
    
    // Create or update borders
    // Top border (connects to titlebar)
    if (!decorations.border_top) {
        decorations.border_top = fluxbox_scene_rect_create(decorations.tree,
            box.width + 2 * border_width, border_width, b_color);
    } else {
        fluxbox_scene_rect_set_size(decorations.border_top, box.width + 2 * border_width, border_width);
        fluxbox_scene_rect_set_color(decorations.border_top, b_color);
    }
    fluxbox_scene_rect_set_position(decorations.border_top, -border_width, -titlebar_height - border_width);
    
    // Bottom border
    if (!decorations.border_bottom) {
        decorations.border_bottom = fluxbox_scene_rect_create(decorations.tree,
            box.width + 2 * border_width, border_width, b_color);
    } else {
        fluxbox_scene_rect_set_size(decorations.border_bottom, box.width + 2 * border_width, border_width);
        fluxbox_scene_rect_set_color(decorations.border_bottom, b_color);
    }
    fluxbox_scene_rect_set_position(decorations.border_bottom, -border_width, box.height);
    
    // Left border
    if (!decorations.border_left) {
        decorations.border_left = fluxbox_scene_rect_create(decorations.tree,
            border_width, box.height + titlebar_height + border_width, b_color);
    } else {
        fluxbox_scene_rect_set_size(decorations.border_left, border_width, box.height + titlebar_height + border_width);
        fluxbox_scene_rect_set_color(decorations.border_left, b_color);
    }
    fluxbox_scene_rect_set_position(decorations.border_left, -border_width, -titlebar_height);
    
    // Right border
    if (!decorations.border_right) {
        decorations.border_right = fluxbox_scene_rect_create(decorations.tree,
            border_width, box.height + titlebar_height + border_width, b_color);
    } else {
        fluxbox_scene_rect_set_size(decorations.border_right, border_width, box.height + titlebar_height + border_width);
        fluxbox_scene_rect_set_color(decorations.border_right, b_color);
    }
    fluxbox_scene_rect_set_position(decorations.border_right, box.width, -titlebar_height);
}