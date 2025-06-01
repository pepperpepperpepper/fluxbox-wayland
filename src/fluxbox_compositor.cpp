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
#include <wlr/util/log.h>
#include <wayland-server-core.h>
}

#include <iostream>
#include <cstdlib>
#include <csignal>
#include <list>

static bool running = true;

void signal_handler(int) {
    running = false;
}

struct FluxboxView {
    struct wlr_xdg_toplevel* toplevel;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    
    FluxboxView(struct wlr_xdg_toplevel* tl) : toplevel(tl) {}
};

struct FluxboxCompositor {
    struct wl_display* display;
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    struct wlr_compositor* compositor;
    struct wlr_output_layout* output_layout;
    struct wlr_seat* seat;
    struct wlr_xdg_shell* xdg_shell;
    
    std::list<FluxboxView*> views;
    FluxboxView* focused_view;
    
    struct wl_listener new_output;
    struct wl_listener new_xdg_toplevel;
    
    static void handle_new_output(struct wl_listener* listener, void* data);
    static void handle_new_xdg_toplevel(struct wl_listener* listener, void* data);
    static void handle_view_destroy(struct wl_listener* listener, void* data);
    static void handle_view_request_move(struct wl_listener* listener, void* data);
    static void handle_view_request_resize(struct wl_listener* listener, void* data);
    static void handle_view_request_maximize(struct wl_listener* listener, void* data);
    static void handle_view_request_fullscreen(struct wl_listener* listener, void* data);
    
    void focus_view(FluxboxView* view);
    void add_view(FluxboxView* view);
    void remove_view(FluxboxView* view);
    
    bool startup() {
        wlr_log_init(WLR_DEBUG, nullptr);
        
        display = wl_display_create();
        if (!display) {
            wlr_log(WLR_ERROR, "Failed to create display");
            return false;
        }
        
        backend = wlr_backend_autocreate(wl_display_get_event_loop(display), nullptr);
        if (!backend) {
            wlr_log(WLR_ERROR, "Failed to create backend");
            return false;
        }
        
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
        
        // Create core protocols
        compositor = wlr_compositor_create(display, 5, renderer);
        wlr_subcompositor_create(display);
        wlr_data_device_manager_create(display);
        
        // Create XDG shell
        xdg_shell = wlr_xdg_shell_create(display, 3);
        
        output_layout = wlr_output_layout_create(display);
        seat = wlr_seat_create(display, "seat0");
        
        focused_view = nullptr;
        
        // Set up event listeners
        new_output.notify = handle_new_output;
        wl_signal_add(&backend->events.new_output, &new_output);
        
        new_xdg_toplevel.notify = handle_new_xdg_toplevel;
        wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
        
        const char* socket = wl_display_add_socket_auto(display);
        if (!socket) {
            wlr_log(WLR_ERROR, "Failed to add socket");
            return false;
        }
        
        if (!wlr_backend_start(backend)) {
            wlr_log(WLR_ERROR, "Failed to start backend");
            return false;
        }
        
        setenv("WAYLAND_DISPLAY", socket, true);
        wlr_log(WLR_INFO, "Fluxbox compositor running on %s", socket);
        
        return true;
    }
    
    void run() {
        wl_display_run(display);
    }
    
    void shutdown() {
        // Clean up views
        for (auto* view : views) {
            delete view;
        }
        views.clear();
        
        if (display) {
            wl_display_destroy_clients(display);
            wl_display_destroy(display);
        }
    }
};

void FluxboxCompositor::handle_new_output(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, new_output);
    struct wlr_output* wlr_output = static_cast<struct wlr_output*>(data);
    
    wlr_output_init_render(wlr_output, compositor->allocator, compositor->renderer);
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    
    struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }
    
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);
    
    wlr_output_layout_add_auto(compositor->output_layout, wlr_output);
    
    wlr_log(WLR_INFO, "Output %s added", wlr_output->name);
}

void FluxboxCompositor::handle_new_xdg_toplevel(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, new_xdg_toplevel);
    struct wlr_xdg_toplevel* toplevel = static_cast<struct wlr_xdg_toplevel*>(data);
    
    FluxboxView* view = new FluxboxView(toplevel);
    
    // Set up view event listeners
    view->destroy.notify = handle_view_destroy;
    wl_signal_add(&toplevel->base->events.destroy, &view->destroy);
    
    view->request_move.notify = handle_view_request_move;
    wl_signal_add(&toplevel->events.request_move, &view->request_move);
    
    view->request_resize.notify = handle_view_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &view->request_resize);
    
    view->request_maximize.notify = handle_view_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
    
    view->request_fullscreen.notify = handle_view_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
    
    compositor->add_view(view);
    compositor->focus_view(view);
    
    wlr_log(WLR_INFO, "New toplevel: %s", toplevel->title ? toplevel->title : "(no title)");
}

void FluxboxCompositor::handle_view_destroy(struct wl_listener* listener, void* data) {
    FluxboxView* view = wl_container_of(listener, view, destroy);
    
    // The compositor pointer is not directly available, so we'll need to find it
    // This is a simplified approach - in a real implementation we'd store a reference
    wlr_log(WLR_INFO, "View destroyed");
    
    // Remove listeners
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    
    delete view;
}

void FluxboxCompositor::handle_view_request_move(struct wl_listener* listener, void* data) {
    FluxboxView* view = wl_container_of(listener, view, request_move);
    wlr_log(WLR_INFO, "View %s requested move", view->toplevel->title ? view->toplevel->title : "(no title)");
    // TODO: Implement interactive move
}

void FluxboxCompositor::handle_view_request_resize(struct wl_listener* listener, void* data) {
    FluxboxView* view = wl_container_of(listener, view, request_resize);
    wlr_log(WLR_INFO, "View %s requested resize", view->toplevel->title ? view->toplevel->title : "(no title)");
    // TODO: Implement interactive resize
}

void FluxboxCompositor::handle_view_request_maximize(struct wl_listener* listener, void* data) {
    FluxboxView* view = wl_container_of(listener, view, request_maximize);
    struct wlr_xdg_toplevel_maximize_event* event = 
        static_cast<struct wlr_xdg_toplevel_maximize_event*>(data);
    
    wlr_xdg_toplevel_set_maximized(view->toplevel, event->maximize);
    wlr_log(WLR_INFO, "View %s %s", 
        view->toplevel->title ? view->toplevel->title : "(no title)",
        event->maximize ? "maximized" : "unmaximized");
}

void FluxboxCompositor::handle_view_request_fullscreen(struct wl_listener* listener, void* data) {
    FluxboxView* view = wl_container_of(listener, view, request_fullscreen);
    struct wlr_xdg_toplevel_fullscreen_event* event = 
        static_cast<struct wlr_xdg_toplevel_fullscreen_event*>(data);
    
    wlr_xdg_toplevel_set_fullscreen(view->toplevel, event->fullscreen);
    wlr_log(WLR_INFO, "View %s %s", 
        view->toplevel->title ? view->toplevel->title : "(no title)",
        event->fullscreen ? "fullscreen" : "unfullscreen");
}

void FluxboxCompositor::focus_view(FluxboxView* view) {
    if (focused_view == view) {
        return;
    }
    
    if (focused_view) {
        wlr_xdg_toplevel_set_activated(focused_view->toplevel, false);
    }
    
    focused_view = view;
    
    if (view) {
        wlr_xdg_toplevel_set_activated(view->toplevel, true);
        
        // Set keyboard focus
        struct wlr_surface* surface = view->toplevel->base->surface;
        wlr_seat_keyboard_notify_enter(seat, surface, nullptr, 0, nullptr);
    }
}

void FluxboxCompositor::add_view(FluxboxView* view) {
    views.push_back(view);
}

void FluxboxCompositor::remove_view(FluxboxView* view) {
    views.remove(view);
    
    if (focused_view == view) {
        focused_view = nullptr;
        // Focus next view if available
        if (!views.empty()) {
            focus_view(views.back());
        }
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Starting Fluxbox Wayland compositor..." << std::endl;
    
    FluxboxCompositor compositor{};
    
    if (!compositor.startup()) {
        std::cerr << "Failed to start compositor" << std::endl;
        return 1;
    }
    
    std::cout << "Compositor started successfully" << std::endl;
    std::cout << "Run 'WAYLAND_DISPLAY=" << getenv("WAYLAND_DISPLAY") << " weston-terminal' to test" << std::endl;
    
    compositor.run();
    
    compositor.shutdown();
    std::cout << "Compositor shut down" << std::endl;
    
    return 0;
}