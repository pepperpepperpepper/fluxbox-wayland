#pragma once

// Forward declarations to avoid header dependency issues
struct wl_display;
struct wlr_backend;
struct wlr_renderer;
struct wlr_allocator;
struct wlr_compositor;
struct wlr_subcompositor;
struct wlr_data_device_manager;
struct wlr_scene;
struct wlr_scene_output_layout;
struct wlr_output_layout;
struct wlr_seat;
struct wlr_xdg_shell;
struct wlr_export_dmabuf_manager_v1;
struct wlr_screencopy_manager_v1;
struct wl_listener;

#include <list>
#include <memory>

class FluxboxOutput;
class FluxboxSurface;
class FluxboxSeat;
class FluxboxWorkspace;

class FluxboxServer {
public:
    FluxboxServer();
    ~FluxboxServer();

    bool startup();
    void run();
    void shutdown();
    void cleanup_stale_sockets();

    // Getters
    struct wl_display* get_display() const { return display; }
    struct wlr_backend* get_backend() const { return backend; }
    struct wlr_renderer* get_renderer() const { return renderer; }
    struct wlr_scene* get_scene() const { return scene; }
    struct wlr_output_layout* get_output_layout() const { return output_layout; }
    struct wlr_seat* get_seat() const { return seat; }
    struct wlr_xdg_shell* get_xdg_shell() const { return xdg_shell; }

    // Window management
    void add_surface(FluxboxSurface* surface);
    void remove_surface(FluxboxSurface* surface);
    FluxboxSurface* get_focused_surface() const { return focused_surface; }
    void set_focus(FluxboxSurface* surface);

    // Workspace management
    FluxboxWorkspace* get_current_workspace() const { return current_workspace; }
    void switch_workspace(int index);

private:
    // Core Wayland objects
    struct wl_display* display;
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    struct wlr_compositor* compositor;
    struct wlr_subcompositor* subcompositor;
    struct wlr_data_device_manager* data_device_manager;

    // Scene management
    struct wlr_scene* scene;
    struct wlr_scene_output_layout* scene_layout;

    // Output management
    struct wlr_output_layout* output_layout;
    std::list<std::unique_ptr<FluxboxOutput>> outputs;

    // Input management
    struct wlr_seat* seat;
    std::unique_ptr<FluxboxSeat> flux_seat;

    // Shell
    struct wlr_xdg_shell* xdg_shell;
    
    // Screenshot support
    struct wlr_export_dmabuf_manager_v1* export_dmabuf_manager;
    struct wlr_screencopy_manager_v1* screencopy_manager;

    // Window management
    std::list<FluxboxSurface*> surfaces;
    FluxboxSurface* focused_surface;

    // Workspace management
    std::vector<std::unique_ptr<FluxboxWorkspace>> workspaces;
    FluxboxWorkspace* current_workspace;

    // Event listeners
    struct wl_listener new_output;
    struct wl_listener new_input;
    struct wl_listener new_xdg_surface;
    struct wl_listener screencopy_frame;
    
    // Output frame listeners (for screencopy support)
    std::list<struct wl_listener*> output_frame_listeners;

    // Event handlers
    static void handle_new_output(struct wl_listener* listener, void* data);
    static void handle_new_input(struct wl_listener* listener, void* data);
    static void handle_new_xdg_surface(struct wl_listener* listener, void* data);
    static void handle_screencopy_frame(struct wl_listener* listener, void* data);
    static void handle_output_frame(struct wl_listener* listener, void* data);

    void setup_listeners();
};