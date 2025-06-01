#include "surface.hpp"
#include "server.hpp"
#include "workspace.hpp"

FluxboxSurface::FluxboxSurface(FluxboxServer* server, struct wlr_xdg_toplevel* toplevel)
    : server(server)
    , toplevel(toplevel)
    , scene_tree(nullptr)
    , workspace(nullptr)
    , focused(false)
    , maximized(false)
    , minimized(false) {
    
    // Create scene tree for this surface
    scene_tree = wlr_scene_xdg_surface_create(server->get_scene(), toplevel->base);
    
    // Initialize saved geometry
    saved_geometry = {0, 0, 0, 0};
    
    setup_listeners();
}

FluxboxSurface::~FluxboxSurface() {
    // Remove listeners
    wl_list_remove(&destroy.link);
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
    wlr_scene_node_set_position(&scene_tree->node, x, y);
}

void FluxboxSurface::get_position(int* x, int* y) const {
    if (x) *x = scene_tree->node.x;
    if (y) *y = scene_tree->node.y;
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
}

void FluxboxSurface::unfocus() {
    if (!focused) {
        return;
    }
    
    focused = false;
    wlr_xdg_toplevel_set_activated(toplevel, false);
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
    wlr_scene_node_set_enabled(&scene_tree->node, false);
}

void FluxboxSurface::unminimize() {
    if (!minimized) {
        return;
    }
    
    minimized = false;
    wlr_scene_node_set_enabled(&scene_tree->node, true);
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
        wlr_scene_node_reparent(&scene_tree->node, workspace->get_scene_tree());
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
    // TODO: Implement interactive move
}

void FluxboxSurface::handle_request_resize(struct wl_listener* listener, void* data) {
    // TODO: Implement interactive resize
}

void FluxboxSurface::handle_request_maximize(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, request_maximize);
    struct wlr_xdg_toplevel_maximize_event* event = 
        static_cast<struct wlr_xdg_toplevel_maximize_event*>(data);
    
    if (event->maximize) {
        surface->maximize();
    } else {
        surface->unmaximize();
    }
}

void FluxboxSurface::handle_request_minimize(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, request_minimize);
    struct wlr_xdg_toplevel_minimize_event* event = 
        static_cast<struct wlr_xdg_toplevel_minimize_event*>(data);
    
    if (event->minimize) {
        surface->minimize();
    } else {
        surface->unminimize();
    }
}

void FluxboxSurface::handle_request_fullscreen(struct wl_listener* listener, void* data) {
    FluxboxSurface* surface = wl_container_of(listener, surface, request_fullscreen);
    struct wlr_xdg_toplevel_fullscreen_event* event = 
        static_cast<struct wlr_xdg_toplevel_fullscreen_event*>(data);
    
    wlr_xdg_toplevel_set_fullscreen(surface->toplevel, event->fullscreen);
}

void FluxboxSurface::handle_set_title(struct wl_listener* listener, void* data) {
    // Title changed - could update window list, taskbar, etc.
}

void FluxboxSurface::handle_set_app_id(struct wl_listener* listener, void* data) {
    // App ID changed - could update window list, taskbar, etc.
}