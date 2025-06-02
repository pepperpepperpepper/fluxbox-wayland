#include "server.hpp"
#include "surface.hpp"
#include "workspace.hpp"
#include "config.hpp"
#include "theme.hpp"
#include "scene_wrapper.h"

extern "C" {
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include <wayland-server-core.h>
}

#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <glob.h>
#include <algorithm>

// External keyboard handler function
extern "C" void fluxbox_add_keyboard(FluxboxServer* server, struct wlr_input_device* device);

// External cursor handler functions
extern "C" void fluxbox_cursor_create(FluxboxServer* server);
extern "C" void fluxbox_cursor_add_device(struct wlr_input_device* device);

// External seat handler functions
extern "C" void fluxbox_seat_create(FluxboxServer* server, const char* name);
extern "C" void fluxbox_seat_add_device(struct wlr_input_device* device);
extern "C" void fluxbox_seat_set_keyboard_focus(FluxboxSurface* surface);

FluxboxServer::FluxboxServer()
    : display(nullptr)
    , backend(nullptr)
    , renderer(nullptr)
    , allocator(nullptr)
    , compositor(nullptr)
    , subcompositor(nullptr)
    , data_device_manager(nullptr)
    , scene(nullptr)
    , scene_layout(nullptr)
    , output_layout(nullptr)
    , seat(nullptr)
    , xdg_shell(nullptr)
    , export_dmabuf_manager(nullptr)
    , screencopy_manager(nullptr)
    , server_decoration_manager(nullptr)
    , xdg_decoration_manager(nullptr)
    , focused_surface(nullptr)
    , current_workspace(nullptr) {
    
    // Initialize listeners
    new_output.notify = handle_new_output;
    new_input.notify = handle_new_input;
    new_xdg_surface.notify = handle_new_xdg_surface;
    screencopy_frame.notify = handle_screencopy_frame;
    new_decoration.notify = handle_new_decoration;
}

FluxboxServer::~FluxboxServer() {
    shutdown();
}

void FluxboxServer::cleanup_stale_sockets() {
    // Clean up stale wayland sockets in runtime directory
    std::string runtime_dir = "/run/user/" + std::to_string(getuid());
    std::string pattern = runtime_dir + "/wayland-*";
    
    glob_t glob_result;
    int ret = glob(pattern.c_str(), GLOB_NOSORT, nullptr, &glob_result);
    
    if (ret == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            const char* socket_path = glob_result.gl_pathv[i];
            
            // Skip if it ends with .lock (we'll handle those separately)
            std::string path_str(socket_path);
            if (path_str.find(".lock") != std::string::npos) {
                continue;
            }
            
            // Check if socket is stale by trying to connect
            std::string lock_path = path_str + ".lock";
            
            struct stat st;
            if (stat(lock_path.c_str(), &st) == 0) {
                // Lock file exists, try to remove it
                if (unlink(lock_path.c_str()) == 0) {
                    std::cout << "Removed stale lock file: " << lock_path << std::endl;
                }
            }
            
            // Try to remove the socket itself
            if (stat(socket_path, &st) == 0) {
                if (unlink(socket_path) == 0) {
                    std::cout << "Removed stale socket: " << socket_path << std::endl;
                }
            }
        }
    }
    
    globfree(&glob_result);
}

bool FluxboxServer::startup() {
    // Clean up any stale sockets from previous runs
    cleanup_stale_sockets();
    
    // Load configuration
    config = std::make_unique<FluxboxConfig>();
    config->load_config();
    
    // Load theme
    theme = std::make_unique<FluxboxTheme>();
    std::string theme_path = std::string(getenv("HOME") ?: "") + "/.fluxbox/styles/Artwiz";
    if (!theme->load(theme_path)) {
        // Try system theme directory
        theme_path = "/usr/share/fluxbox/styles/Artwiz";
        if (!theme->load(theme_path)) {
            // Try local data directory
            theme_path = "data/styles/Artwiz";
            if (!theme->load(theme_path)) {
                std::cout << "Using default theme (Artwiz theme not found)" << std::endl;
            } else {
                std::cout << "Loaded theme from: " << theme_path << std::endl;
            }
        } else {
            std::cout << "Loaded theme from: " << theme_path << std::endl;
        }
    } else {
        std::cout << "Loaded theme from: " << theme_path << std::endl;
    }
    
    // Create the Wayland display
    display = wl_display_create();
    if (!display) {
        wlr_log(WLR_ERROR, "Failed to create Wayland display");
        return false;
    }

    // Create the backend
    backend = wlr_backend_autocreate(wl_display_get_event_loop(display), nullptr);
    if (!backend) {
        wlr_log(WLR_ERROR, "Failed to create backend");
        return false;
    }

    // Create renderer and allocator
    renderer = wlr_renderer_autocreate(backend);
    if (!renderer) {
        wlr_log(WLR_ERROR, "Failed to create renderer");
        return false;
    }

    wlr_renderer_init_wl_display(renderer, display);

    allocator = wlr_allocator_autocreate(backend, renderer);
    if (!allocator) {
        wlr_log(WLR_ERROR, "Failed to create allocator");
        return false;
    }

    // Create core Wayland protocols
    compositor = wlr_compositor_create(display, 5, renderer);
    subcompositor = wlr_subcompositor_create(display);
    data_device_manager = wlr_data_device_manager_create(display);

    // Create output layout
    output_layout = wlr_output_layout_create(display);

    // Create scene
    scene = fluxbox_scene_create();
    scene_layout = fluxbox_scene_attach_output_layout(scene, output_layout);

    // Create XDG shell
    xdg_shell = wlr_xdg_shell_create(display, 3);
    
    // Create screenshot support protocols
    export_dmabuf_manager = wlr_export_dmabuf_manager_v1_create(display);
    screencopy_manager = wlr_screencopy_manager_v1_create(display);
    
    if (screencopy_manager) {
        wlr_log(WLR_INFO, "Screencopy manager created successfully");
    } else {
        wlr_log(WLR_ERROR, "Failed to create screencopy manager");
    }
    
    // Create decoration managers
    server_decoration_manager = wlr_server_decoration_manager_create(display);
    if (server_decoration_manager) {
        // Set server-side decorations as the default
        wlr_server_decoration_manager_set_default_mode(server_decoration_manager, 
            WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
        wlr_log(WLR_INFO, "Server decoration manager created with server-side decorations");
    }
    
    xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(display);
    if (xdg_decoration_manager) {
        wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration, &new_decoration);
        wlr_log(WLR_INFO, "XDG decoration manager created");
    }

    // Create seat
    seat = wlr_seat_create(display, "seat0");
    
    // Initialize seat management
    fluxbox_seat_create(this, "seat0");
    
    // Initialize cursor system
    fluxbox_cursor_create(this);

    // Set up event listeners
    setup_listeners();

    // Create initial workspaces
    for (int i = 0; i < 4; ++i) {
        workspaces.emplace_back(std::make_unique<FluxboxWorkspace>(this, i, "Workspace " + std::to_string(i + 1)));
    }
    current_workspace = workspaces[0].get();
    current_workspace->activate();

    // Add socket to display
    const char* socket = wl_display_add_socket_auto(display);
    if (!socket) {
        wlr_log(WLR_ERROR, "Failed to add socket to display");
        return false;
    }

    // Start the backend
    if (!wlr_backend_start(backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        return false;
    }

    // Set environment variable for clients
    setenv("WAYLAND_DISPLAY", socket, true);

    wlr_log(WLR_INFO, "Running Fluxbox on WAYLAND_DISPLAY=%s", socket);

    return true;
}

void FluxboxServer::run() {
    wl_display_run(display);
}

void FluxboxServer::shutdown() {
    std::cout << "Starting server shutdown..." << std::endl;
    
    // Clean up output frame listeners
    for (auto* listener : output_frame_listeners) {
        wl_list_remove(&listener->link);
        delete listener;
    }
    output_frame_listeners.clear();
    
    // Clean up surfaces
    // Make a copy of the list since surfaces will be removed during destruction
    auto surfaces_copy = surfaces;
    for (auto* surface : surfaces_copy) {
        // Only delete if the surface hasn't already been destroyed
        // The surface destructor will remove itself from the list
        if (std::find(surfaces.begin(), surfaces.end(), surface) != surfaces.end()) {
            delete surface;
        }
    }
    surfaces.clear();
    
    // Clean up workspaces
    workspaces.clear();
    current_workspace = nullptr;
    focused_surface = nullptr;
    
    // Clean up wlroots objects
    if (scene_layout) {
        // Scene layout is destroyed with the scene
        scene_layout = nullptr;
    }
    
    if (scene) {
        fluxbox_scene_destroy(scene);
        scene = nullptr;
    }
    
    if (output_layout) {
        wlr_output_layout_destroy(output_layout);
        output_layout = nullptr;
    }
    
    if (allocator) {
        wlr_allocator_destroy(allocator);
        allocator = nullptr;
    }
    
    if (renderer) {
        wlr_renderer_destroy(renderer);
        renderer = nullptr;
    }
    
    if (backend) {
        wlr_backend_destroy(backend);
        backend = nullptr;
    }
    
    if (display) {
        wl_display_destroy_clients(display);
        wl_display_destroy(display);
        display = nullptr;
    }
    
    std::cout << "Server shutdown complete." << std::endl;
}

void FluxboxServer::setup_listeners() {
    wl_signal_add(&backend->events.new_output, &new_output);
    wl_signal_add(&backend->events.new_input, &new_input);
    wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_surface);
}

void FluxboxServer::handle_new_output(struct wl_listener* listener, void* data) {
    FluxboxServer* server = wl_container_of(listener, server, new_output);
    struct wlr_output* wlr_output = static_cast<struct wlr_output*>(data);

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode != nullptr) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    
    // Create scene output for this output
    struct wlr_scene_output* scene_output = fluxbox_scene_output_create(server->scene, wlr_output);
    if (!scene_output) {
        wlr_log(WLR_ERROR, "Failed to create scene output for %s", wlr_output->name);
        return;
    }
    
    // Add frame listener for screencopy support
    struct wl_listener* frame_listener = new wl_listener;
    frame_listener->notify = handle_output_frame;
    wl_signal_add(&wlr_output->events.frame, frame_listener);
    server->output_frame_listeners.push_back(frame_listener);
    
    // Schedule an initial frame to get rendering started
    wlr_output_schedule_frame(wlr_output);
    
    wlr_log(WLR_INFO, "Output %s added with scene and frame listener for screencopy", wlr_output->name);
}

void FluxboxServer::handle_new_input(struct wl_listener* listener, void* data) {
    FluxboxServer* server = wl_container_of(listener, server, new_input);
    struct wlr_input_device* device = static_cast<struct wlr_input_device*>(data);

    // Let seat management handle the device and update capabilities
    fluxbox_seat_add_device(device);
}

void FluxboxServer::handle_new_xdg_surface(struct wl_listener* listener, void* data) {
    FluxboxServer* server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_toplevel* toplevel = static_cast<struct wlr_xdg_toplevel*>(data);

    FluxboxSurface* surface = new FluxboxSurface(server, toplevel);
    server->add_surface(surface);
}

void FluxboxServer::add_surface(FluxboxSurface* surface) {
    surfaces.push_back(surface);
    current_workspace->add_surface(surface);
    set_focus(surface);
}

void FluxboxServer::remove_surface(FluxboxSurface* surface) {
    surfaces.remove(surface);
    if (surface->get_workspace()) {
        surface->get_workspace()->remove_surface(surface);
    }
    
    if (focused_surface == surface) {
        focused_surface = nullptr;
        // Focus the next surface if available
        if (!surfaces.empty()) {
            set_focus(surfaces.back());
        }
    }
    
    // Note: The surface will be deleted by the destroy handler in surface.cpp
    // Do NOT delete it here to avoid double-free
}

void FluxboxServer::set_focus(FluxboxSurface* surface) {
    if (focused_surface == surface) {
        return;
    }
    
    if (focused_surface) {
        focused_surface->unfocus();
    }
    
    focused_surface = surface;
    
    if (surface) {
        surface->focus();
        // Update seat keyboard focus
        fluxbox_seat_set_keyboard_focus(surface);
    } else {
        fluxbox_seat_set_keyboard_focus(nullptr);
    }
}

void FluxboxServer::switch_workspace(int index) {
    if (index < 0 || index >= static_cast<int>(workspaces.size())) {
        return;
    }
    
    if (current_workspace) {
        current_workspace->deactivate();
    }
    
    current_workspace = workspaces[index].get();
    current_workspace->activate();
    
    // Focus first surface in the new workspace
    const auto& surfaces = current_workspace->get_surfaces();
    if (!surfaces.empty()) {
        set_focus(surfaces[0]);
    } else {
        set_focus(nullptr);
    }
}

void FluxboxServer::handle_output_frame(struct wl_listener* listener, void* data) {
    struct wlr_output* output = static_cast<struct wlr_output*>(data);
    
    // Get the scene output for this wlr_output
    // Note: In a real implementation, we'd store the server pointer properly
    // For now, we can't access the server instance from this static function
    // This would need refactoring to store server pointer in a custom output structure
    
    // The scene graph automatically handles damage tracking and rendering
    // We just need to commit the output and send frame done events
    
    // Since we can't access server->scene here, we'll need to refactor this
    // For now, just ensure the next frame is scheduled
    wlr_output_schedule_frame(output);
    
    // TODO: Properly implement scene rendering:
    // struct wlr_scene_output* scene_output = wlr_scene_get_scene_output(server->scene, output);
    // wlr_scene_output_commit(scene_output, NULL);
    // 
    // struct timespec now;
    // clock_gettime(CLOCK_MONOTONIC, &now);
    // wlr_scene_output_send_frame_done(scene_output, &now);
}

void FluxboxServer::handle_screencopy_frame(struct wl_listener* listener, void* data) {
    // Screencopy handling would go here
    // For now, just log the event
    wlr_log(WLR_INFO, "Screencopy frame request received");
}

int FluxboxServer::get_current_workspace_index() const {
    for (size_t i = 0; i < workspaces.size(); i++) {
        if (workspaces[i].get() == current_workspace) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

FluxboxWorkspace* FluxboxServer::get_workspace(int index) {
    if (index >= 0 && index < static_cast<int>(workspaces.size())) {
        return workspaces[index].get();
    }
    return nullptr;
}

void FluxboxServer::focus_next_surface() {
    if (surfaces.empty()) return;
    
    auto it = std::find(surfaces.begin(), surfaces.end(), focused_surface);
    if (it != surfaces.end()) {
        ++it;
        if (it == surfaces.end()) {
            it = surfaces.begin();
        }
        set_focus(*it);
    } else if (!surfaces.empty()) {
        set_focus(surfaces.front());
    }
}

void FluxboxServer::focus_prev_surface() {
    if (surfaces.empty()) return;
    
    auto it = std::find(surfaces.rbegin(), surfaces.rend(), focused_surface);
    if (it != surfaces.rend()) {
        ++it;
        if (it == surfaces.rend()) {
            it = surfaces.rbegin();
        }
        set_focus(*it);
    } else if (!surfaces.empty()) {
        set_focus(surfaces.back());
    }
}

void FluxboxServer::handle_new_decoration(struct wl_listener* listener, void* data) {
    FluxboxServer* server = wl_container_of(listener, server, new_decoration);
    struct wlr_xdg_toplevel_decoration_v1* decoration = 
        static_cast<struct wlr_xdg_toplevel_decoration_v1*>(data);
    
    // Request server-side decorations
    wlr_xdg_toplevel_decoration_v1_set_mode(decoration, 
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    
    wlr_log(WLR_INFO, "New decoration requested, set to server-side mode");
}