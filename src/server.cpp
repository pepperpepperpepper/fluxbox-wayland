#include "server.hpp"
#include "surface.hpp"
#include "workspace.hpp"

extern "C" {
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wayland-server-core.h>
}

#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <glob.h>

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
    , focused_surface(nullptr)
    , current_workspace(nullptr) {
    
    // Initialize listeners
    new_output.notify = handle_new_output;
    new_input.notify = handle_new_input;
    new_xdg_surface.notify = handle_new_xdg_surface;
}

FluxboxServer::~FluxboxServer() {
    shutdown();
}

void FluxboxServer::cleanup_stale_sockets() {
    // Clean up stale wayland sockets in /tmp
    glob_t glob_result;
    int ret = glob("/tmp/wayland-*", GLOB_NOSORT, nullptr, &glob_result);
    
    if (ret == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            const char* socket_path = glob_result.gl_pathv[i];
            
            // Check if socket is stale (no process using it)
            // For simplicity, we'll just try to remove lock files
            std::string lock_path = std::string(socket_path) + ".lock";
            
            struct stat st;
            if (stat(lock_path.c_str(), &st) == 0) {
                // Lock file exists, check if it's stale
                if (unlink(lock_path.c_str()) == 0) {
                    std::cout << "Removed stale lock file: " << lock_path << std::endl;
                }
            }
            
            // Also try to remove the socket itself if it's not in use
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
    
    // Create the Wayland display
    display = wl_display_create();
    if (!display) {
        wlr_log(WLR_ERROR, "Failed to create Wayland display");
        return false;
    }

    // Create the backend
    backend = wlr_backend_autocreate(display, nullptr);
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
    output_layout = wlr_output_layout_create();

    // Create scene
    scene = wlr_scene_create();
    scene_layout = wlr_scene_attach_output_layout(scene, output_layout);

    // Create XDG shell
    xdg_shell = wlr_xdg_shell_create(display, 3);

    // Create seat
    seat = wlr_seat_create(display, "seat0");

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
    
    // Clean up surfaces
    for (auto* surface : surfaces) {
        delete surface;
    }
    surfaces.clear();
    
    // Clean up workspaces
    workspaces.clear();
    current_workspace = nullptr;
    focused_surface = nullptr;
    
    // Clean up wlroots objects
    if (scene_layout) {
        wlr_scene_output_layout_destroy(scene_layout);
        scene_layout = nullptr;
    }
    
    if (scene) {
        wlr_scene_node_destroy(&scene->tree.node);
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
}

void FluxboxServer::handle_new_input(struct wl_listener* listener, void* data) {
    FluxboxServer* server = wl_container_of(listener, server, new_input);
    struct wlr_input_device* device = static_cast<struct wlr_input_device*>(data);

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        wlr_seat_set_capabilities(server->seat, WL_SEAT_CAPABILITY_KEYBOARD);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        wlr_seat_set_capabilities(server->seat, WL_SEAT_CAPABILITY_POINTER);
        break;
    default:
        break;
    }
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