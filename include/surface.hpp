#pragma once

extern "C" {
#include <wayland-server-core.h>
}

// Forward declarations
struct wlr_xdg_toplevel;
struct wlr_scene_tree;
struct wlr_scene_rect;

class FluxboxServer;
class FluxboxWorkspace;

class FluxboxSurface {
public:
    FluxboxSurface(FluxboxServer* server, struct wlr_xdg_toplevel* toplevel);
    ~FluxboxSurface();

    // Surface properties
    void set_position(int x, int y);
    void get_position(int* x, int* y) const;
    void set_size(int width, int height);
    void get_size(int* width, int* height) const;
    
    // Window management
    void focus();
    void unfocus();
    bool is_focused() const { return focused; }
    
    void maximize();
    void unmaximize();
    bool is_maximized() const { return maximized; }
    
    void minimize();
    void unminimize();
    bool is_minimized() const { return minimized; }
    
    void close();
    
    // Workspace management
    void move_to_workspace(FluxboxWorkspace* workspace);
    FluxboxWorkspace* get_workspace() const { return workspace; }
    
    // Getters
    struct wlr_xdg_toplevel* get_toplevel() const { return toplevel; }
    struct wlr_scene_tree* get_scene_tree() const { return scene_tree; }
    const char* get_title() const;
    const char* get_app_id() const;
    
    // Decoration management
    void update_decorations();
    void set_decorated(bool decorated);
    bool is_decorated() const { return decorated; }

private:
    FluxboxServer* server;
    struct wlr_xdg_toplevel* toplevel;
    struct wlr_scene_tree* scene_tree;
    
    FluxboxWorkspace* workspace;
    
    bool focused;
    bool maximized;
    bool minimized;
    bool decorated;
    
    // Saved geometry for maximize/minimize
    struct {
        int x, y;
        int width, height;
    } saved_geometry;
    
    // Decoration elements (title bar, borders)
    struct {
        struct wlr_scene_tree* tree;
        struct wlr_scene_rect* titlebar;
        struct wlr_scene_rect* border_top;
        struct wlr_scene_rect* border_bottom;
        struct wlr_scene_rect* border_left;
        struct wlr_scene_rect* border_right;
    } decorations;
    
    // Event listeners
    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_minimize;
    struct wl_listener request_fullscreen;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
    
    // Event handlers
    static void handle_destroy(struct wl_listener* listener, void* data);
    static void handle_map(struct wl_listener* listener, void* data);
    static void handle_unmap(struct wl_listener* listener, void* data);
    static void handle_commit(struct wl_listener* listener, void* data);
    static void handle_request_move(struct wl_listener* listener, void* data);
    static void handle_request_resize(struct wl_listener* listener, void* data);
    static void handle_request_maximize(struct wl_listener* listener, void* data);
    static void handle_request_minimize(struct wl_listener* listener, void* data);
    static void handle_request_fullscreen(struct wl_listener* listener, void* data);
    static void handle_set_title(struct wl_listener* listener, void* data);
    static void handle_set_app_id(struct wl_listener* listener, void* data);
    
    void setup_listeners();
};