// WLR_USE_UNSTABLE is already defined in meson.build

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
#include <wlr/util/log.h>
#include <wayland-server-core.h>
}

#include <iostream>
#include <cstdlib>

struct FluxboxServer {
    struct wl_display* display;
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    struct wlr_compositor* compositor;
    struct wlr_scene* scene;
    struct wlr_output_layout* output_layout;
    struct wlr_seat* seat;
    
    struct wl_listener new_output;
    
    static void handle_new_output(struct wl_listener* listener, void* data);
    
    bool startup() {
        display = wl_display_create();
        if (!display) return false;
        
        backend = wlr_backend_autocreate(wl_display_get_event_loop(display), nullptr);
        if (!backend) return false;
        
        renderer = wlr_renderer_autocreate(backend);
        if (!renderer) return false;
        
        wlr_renderer_init_wl_display(renderer, display);
        
        allocator = wlr_allocator_autocreate(backend, renderer);
        if (!allocator) return false;
        
        compositor = wlr_compositor_create(display, 5, renderer);
        wlr_subcompositor_create(display);
        wlr_data_device_manager_create(display);
        
        output_layout = wlr_output_layout_create(display);
        scene = wlr_scene_create();
        wlr_scene_attach_output_layout(scene, output_layout);
        
        seat = wlr_seat_create(display, "seat0");
        
        new_output.notify = handle_new_output;
        wl_signal_add(&backend->events.new_output, &new_output);
        
        const char* socket = wl_display_add_socket_auto(display);
        if (!socket) return false;
        
        if (!wlr_backend_start(backend)) return false;
        
        setenv("WAYLAND_DISPLAY", socket, true);
        wlr_log(WLR_INFO, "Running Fluxbox on WAYLAND_DISPLAY=%s", socket);
        
        return true;
    }
    
    void run() {
        wl_display_run(display);
    }
    
    void shutdown() {
        if (display) {
            wl_display_destroy_clients(display);
            wl_display_destroy(display);
        }
    }
};

void FluxboxServer::handle_new_output(struct wl_listener* listener, void* data) {
    FluxboxServer* server = wl_container_of(listener, server, new_output);
    struct wlr_output* wlr_output = static_cast<struct wlr_output*>(data);
    
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    
    struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }
    
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);
    
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
}

int main() {
    wlr_log_init(WLR_DEBUG, nullptr);
    
    std::cout << "Starting minimal Fluxbox for Wayland..." << std::endl;
    
    FluxboxServer server{};
    
    if (!server.startup()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Server started, running..." << std::endl;
    server.run();
    
    return 0;
}