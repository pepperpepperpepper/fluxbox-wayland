extern "C" {
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
// #include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>
}

#include <iostream>
#include <cstdlib>
#include <csignal>
#include <list>
#include <unistd.h>
#include <vector>
#include <memory>
#include <string>
#include "../include/config.hpp"

static bool running = true;
static struct wl_display* global_display = nullptr;

void signal_handler(int sig) {
    std::cout << "Received signal " << sig << ", initiating graceful shutdown..." << std::endl;
    running = false;
    if (global_display) {
        wl_display_terminate(global_display);
    }
}

// Forward declarations
struct FluxboxCompositor;

// Wrapper structure to store both listener and compositor pointer
struct output_frame_listener_data {
    struct wl_listener listener;
    FluxboxCompositor* compositor;
};

struct FluxboxWorkspace {
    int index;
    std::string name;
    bool active;
    std::list<struct FluxboxView*> views;
    
    FluxboxWorkspace(int idx, const std::string& n) : index(idx), name(n), active(false) {}
};

struct FluxboxView {
    struct wlr_xdg_toplevel* toplevel;
    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;
    FluxboxWorkspace* workspace;
    struct wlr_scene_tree* scene_tree;
    
    FluxboxView(struct wlr_xdg_toplevel* tl) : toplevel(tl), workspace(nullptr), scene_tree(nullptr) {}
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
    // struct wlr_scene* scene;
    
    // Basic rendering for screencopy support
    
    // Screenshot support
    struct wlr_export_dmabuf_manager_v1* export_dmabuf_manager;
    struct wlr_screencopy_manager_v1* screencopy_manager;
    struct wlr_xdg_output_manager_v1* xdg_output_manager;
    
    // Screencopy event listeners
    struct wl_listener screencopy_frame_destroy;
    
    // Output frame listeners for screencopy support
    std::list<struct wl_listener*> output_frame_listeners;
    
    // Input handling
    struct wlr_cursor* cursor;
    struct wlr_xcursor_manager* cursor_mgr;
    
    std::list<FluxboxView*> views;
    FluxboxView* focused_view;
    
    // Workspace management
    std::vector<std::unique_ptr<FluxboxWorkspace>> workspaces;
    FluxboxWorkspace* current_workspace;
    int num_workspaces;
    
    // Keyboard handling
    struct wl_listener keyboard_key;
    
    // Configuration
    std::unique_ptr<FluxboxConfig> config;
    
    struct wl_listener new_output;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    
    static void handle_new_output(struct wl_listener* listener, void* data);
    static void handle_new_xdg_toplevel(struct wl_listener* listener, void* data);
    static void handle_view_destroy(struct wl_listener* listener, void* data);
    static void handle_view_map(struct wl_listener* listener, void* data);
    static void handle_view_unmap(struct wl_listener* listener, void* data);
    static void handle_new_input(struct wl_listener* listener, void* data);
    static void handle_cursor_motion(struct wl_listener* listener, void* data);
    static void handle_cursor_motion_absolute(struct wl_listener* listener, void* data);
    static void handle_cursor_button(struct wl_listener* listener, void* data);
    static void handle_cursor_axis(struct wl_listener* listener, void* data);
    static void handle_cursor_frame(struct wl_listener* listener, void* data);
    static void handle_keyboard_key(struct wl_listener* listener, void* data);
    static void handle_output_frame(struct wl_listener* listener, void* data);
    
    // Workspace management
    void switch_workspace(int index);
    void move_view_to_workspace(FluxboxView* view, int workspace_index);
    void add_view_to_current_workspace(FluxboxView* view);
    
    void focus_view(FluxboxView* view);
    void add_view(FluxboxView* view);
    void remove_view(FluxboxView* view);
    FluxboxView* view_at(double lx, double ly, struct wlr_surface** surface, double* sx, double* sy);
    
    bool startup() {
        wlr_log_init(WLR_DEBUG, nullptr);
        
        display = wl_display_create();
        if (!display) {
            std::cerr << "Failed to create display" << std::endl;
            return false;
        }
        
        // Set global display for signal handler
        global_display = display;
        
        // Set headless backend if not running as root or with proper permissions
        if (getuid() != 0) {
            setenv("WLR_BACKENDS", "headless", 0);
            std::cout << "Running in headless mode (non-root user)" << std::endl;
        }
        
        backend = wlr_backend_autocreate(wl_display_get_event_loop(display), nullptr);
        if (!backend) {
            std::cerr << "Failed to create backend" << std::endl;
            return false;
        }
        
        renderer = wlr_renderer_autocreate(backend);
        if (!renderer) {
            std::cerr << "Failed to create renderer" << std::endl;
            return false;
        }
        
        wlr_renderer_init_wl_display(renderer, display);
        
        allocator = wlr_allocator_autocreate(backend, renderer);
        if (!allocator) {
            std::cerr << "Failed to create allocator" << std::endl;
            return false;
        }
        
        // Create core protocols
        compositor = wlr_compositor_create(display, 5, renderer);
        wlr_subcompositor_create(display);
        wlr_data_device_manager_create(display);
        
        // Create XDG shell
        xdg_shell = wlr_xdg_shell_create(display, 3);
        
        // Create output layout first
        output_layout = wlr_output_layout_create(display);
        
        // Create scene graph
        // scene = wlr_scene_create();
        // wlr_scene_attach_output_layout(scene, output_layout);
        
        // Create screenshot support protocols
        export_dmabuf_manager = wlr_export_dmabuf_manager_v1_create(display);
        screencopy_manager = wlr_screencopy_manager_v1_create(display);
        xdg_output_manager = wlr_xdg_output_manager_v1_create(display, output_layout);
        
        if (screencopy_manager) {
            wlr_log(WLR_INFO, "Screencopy manager created successfully");
        } else {
            wlr_log(WLR_ERROR, "Failed to create screencopy manager");
        }
        seat = wlr_seat_create(display, "seat0");
        
        // Create cursor
        cursor = wlr_cursor_create();
        wlr_cursor_attach_output_layout(cursor, output_layout);
        
        cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);
        
        focused_view = nullptr;
        
        // Load configuration
        config = std::make_unique<FluxboxConfig>();
        config->load_config(); // Load from default location or create defaults
        
        // Initialize workspaces from config
        num_workspaces = config->get_num_workspaces();
        const auto& workspace_names = config->get_workspace_names();
        for (int i = 0; i < num_workspaces; i++) {
            std::string name = (i < workspace_names.size()) ? workspace_names[i] : ("Workspace " + std::to_string(i + 1));
            workspaces.push_back(std::make_unique<FluxboxWorkspace>(i, name));
        }
        current_workspace = workspaces[0].get();
        current_workspace->active = true;
        
        // Set up event listeners
        new_output.notify = handle_new_output;
        wl_signal_add(&backend->events.new_output, &new_output);
        
        new_xdg_toplevel.notify = handle_new_xdg_toplevel;
        wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
        
        new_input.notify = handle_new_input;
        wl_signal_add(&backend->events.new_input, &new_input);
        
        cursor_motion.notify = handle_cursor_motion;
        wl_signal_add(&cursor->events.motion, &cursor_motion);
        
        cursor_motion_absolute.notify = handle_cursor_motion_absolute;
        wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
        
        cursor_button.notify = handle_cursor_button;
        wl_signal_add(&cursor->events.button, &cursor_button);
        
        cursor_axis.notify = handle_cursor_axis;
        wl_signal_add(&cursor->events.axis, &cursor_axis);
        
        cursor_frame.notify = handle_cursor_frame;
        wl_signal_add(&cursor->events.frame, &cursor_frame);
        
        const char* socket = wl_display_add_socket_auto(display);
        if (!socket) {
            std::cerr << "Failed to add socket" << std::endl;
            return false;
        }
        
        if (!wlr_backend_start(backend)) {
            std::cerr << "Failed to start backend" << std::endl;
            return false;
        }
        
        setenv("WAYLAND_DISPLAY", socket, true);
        std::cout << "Fluxbox compositor running on " << socket << std::endl;
        
        return true;
    }
    
    void run() {
        wl_display_run(display);
    }
    
    void shutdown() {
        // Clean up output frame listeners
        for (auto* listener : output_frame_listeners) {
            wl_list_remove(&listener->link);
            // Get the wrapper and delete it
            struct output_frame_listener_data* wrapper = wl_container_of(listener, wrapper, listener);
            delete wrapper;
        }
        output_frame_listeners.clear();
        
        // Clean up views
        for (auto* view : views) {
            delete view;
        }
        views.clear();
        
        if (display) {
            wl_display_destroy_clients(display);
            wl_display_destroy(display);
        }
        
        // Clear global display
        global_display = nullptr;
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
    
    // Create scene output for this output
    // struct wlr_scene_output* scene_output = wlr_scene_output_create(compositor->scene, wlr_output);
    // if (!scene_output) {
    //     std::cerr << "Failed to create scene output for " << wlr_output->name << std::endl;
    //     return;
    // }
    
    // Add frame listener for screencopy support
    struct output_frame_listener_data* frame_data = new output_frame_listener_data;
    frame_data->compositor = compositor;
    frame_data->listener.notify = handle_output_frame;
    wl_signal_add(&wlr_output->events.frame, &frame_data->listener);
    compositor->output_frame_listeners.push_back(&frame_data->listener);
    
    // Load cursor theme for this output
    wlr_xcursor_manager_load(compositor->cursor_mgr, wlr_output->scale);
    
    // Schedule an initial frame
    wlr_output_schedule_frame(wlr_output);
    
    std::cout << "Output " << wlr_output->name << " added with scene rendering and screencopy support" << std::endl;
}

void FluxboxCompositor::handle_new_xdg_toplevel(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, new_xdg_toplevel);
    struct wlr_xdg_toplevel* toplevel = static_cast<struct wlr_xdg_toplevel*>(data);
    
    FluxboxView* view = new FluxboxView(toplevel);
    
    // Create scene tree for this surface
    // view->scene_tree = wlr_scene_xdg_surface_create(&compositor->scene->tree, toplevel->base);
    
    // Set up event listeners
    view->destroy.notify = handle_view_destroy;
    wl_signal_add(&toplevel->base->events.destroy, &view->destroy);
    
    view->map.notify = handle_view_map;
    wl_signal_add(&toplevel->base->surface->events.map, &view->map);
    
    view->unmap.notify = handle_view_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
    
    compositor->add_view(view);
    compositor->add_view_to_current_workspace(view);
    
    std::cout << "New toplevel: " << (toplevel->title ? toplevel->title : "(no title)") << std::endl;
}

void FluxboxCompositor::handle_view_destroy(struct wl_listener* listener, void* data) {
    FluxboxView* view = wl_container_of(listener, view, destroy);
    
    std::cout << "View destroyed" << std::endl;
    
    // Remove listeners
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    
    delete view;
}

void FluxboxCompositor::handle_view_map(struct wl_listener* listener, void* data) {
    FluxboxView* view = wl_container_of(listener, view, map);
    
    // When the surface is mapped (ready to be displayed), focus it
    // Find the compositor instance - we need a better way to access it
    // For now, just ensure the surface is visible
    // wlr_scene_node_set_enabled(&view->scene_tree->node, true);
    
    // Send a configure event to ensure proper sizing
    wlr_xdg_toplevel_set_size(view->toplevel, 0, 0);
}

void FluxboxCompositor::handle_view_unmap(struct wl_listener* listener, void* data) {
    FluxboxView* view = wl_container_of(listener, view, unmap);
    
    // When unmapped, unfocus if this was the focused view
    // The scene graph will handle hiding it
}

void FluxboxCompositor::handle_new_input(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, new_input);
    struct wlr_input_device* device = static_cast<struct wlr_input_device*>(data);
    
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct wlr_keyboard* keyboard = wlr_keyboard_from_input_device(device);
        
        // Set keyboard layout
        struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap* keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
        
        wlr_keyboard_set_keymap(keyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        
        wlr_keyboard_set_repeat_info(keyboard, 25, 600);
        
        wlr_seat_set_keyboard(compositor->seat, keyboard);
        wlr_seat_set_capabilities(compositor->seat, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
        
        // Set up keyboard key event listener
        compositor->keyboard_key.notify = handle_keyboard_key;
        wl_signal_add(&keyboard->events.key, &compositor->keyboard_key);
        
        std::cout << "Keyboard added with key bindings" << std::endl;
        break;
    }
    case WLR_INPUT_DEVICE_POINTER:
        wlr_cursor_attach_input_device(compositor->cursor, device);
        wlr_seat_set_capabilities(compositor->seat, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
        std::cout << "Pointer added" << std::endl;
        break;
    default:
        break;
    }
}

void FluxboxCompositor::handle_cursor_motion(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, cursor_motion);
    struct wlr_pointer_motion_event* event = static_cast<struct wlr_pointer_motion_event*>(data);
    
    wlr_cursor_move(compositor->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    
    // Set cursor image (simplified - function signature changed in newer wlroots)
    // wlr_xcursor_manager_set_cursor_image(compositor->cursor_mgr, "default", compositor->cursor);
    
    // Notify seat of pointer motion
    wlr_seat_pointer_notify_motion(compositor->seat, event->time_msec, compositor->cursor->x, compositor->cursor->y);
}

void FluxboxCompositor::handle_cursor_motion_absolute(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event* event = static_cast<struct wlr_pointer_motion_absolute_event*>(data);
    
    wlr_cursor_warp_absolute(compositor->cursor, &event->pointer->base, event->x, event->y);
    
    // wlr_xcursor_manager_set_cursor_image(compositor->cursor_mgr, "default", compositor->cursor);
    wlr_seat_pointer_notify_motion(compositor->seat, event->time_msec, compositor->cursor->x, compositor->cursor->y);
}

void FluxboxCompositor::handle_cursor_button(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, cursor_button);
    struct wlr_pointer_button_event* event = static_cast<struct wlr_pointer_button_event*>(data);
    
    wlr_seat_pointer_notify_button(compositor->seat, event->time_msec, event->button, event->state);
    
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        // Find view under cursor and focus it
        struct wlr_surface* surface = nullptr;
        double sx, sy;
        FluxboxView* view = compositor->view_at(compositor->cursor->x, compositor->cursor->y, &surface, &sx, &sy);
        
        if (view) {
            compositor->focus_view(view);
        }
    }
}

void FluxboxCompositor::handle_cursor_axis(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, cursor_axis);
    struct wlr_pointer_axis_event* event = static_cast<struct wlr_pointer_axis_event*>(data);
    
    wlr_seat_pointer_notify_axis(compositor->seat, event->time_msec, event->orientation, event->delta, 
                                  event->delta_discrete, event->source, event->relative_direction);
}

void FluxboxCompositor::handle_cursor_frame(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, cursor_frame);
    wlr_seat_pointer_notify_frame(compositor->seat);
}

FluxboxView* FluxboxCompositor::view_at(double lx, double ly, struct wlr_surface** surface, double* sx, double* sy) {
    // Simple implementation: just check if cursor is over any view
    // In a real implementation, we'd use proper coordinate transformation
    for (auto* view : views) {
        if (view->toplevel->base->surface && 
            wlr_surface_point_accepts_input(view->toplevel->base->surface, lx, ly)) {
            *surface = view->toplevel->base->surface;
            *sx = lx;
            *sy = ly;
            return view;
        }
    }
    return nullptr;
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
    
    // Remove from workspace
    if (view->workspace) {
        view->workspace->views.remove(view);
    }
    
    if (focused_view == view) {
        focused_view = nullptr;
        // Focus next view if available
        if (!views.empty()) {
            focus_view(views.back());
        }
    }
}

// Workspace Management Implementation
void FluxboxCompositor::switch_workspace(int index) {
    if (index < 0 || index >= num_workspaces) {
        return;
    }
    
    if (current_workspace) {
        current_workspace->active = false;
        // Hide all views in current workspace
        for (auto* view : current_workspace->views) {
            // In a real implementation, you'd hide the view here
            // For now, we'll just manage the list
        }
    }
    
    current_workspace = workspaces[index].get();
    current_workspace->active = true;
    
    std::cout << "Switched to " << current_workspace->name << std::endl;
    
    // Show views in new workspace and focus first one
    if (!current_workspace->views.empty()) {
        focus_view(current_workspace->views.front());
    } else {
        focused_view = nullptr;
    }
}

void FluxboxCompositor::move_view_to_workspace(FluxboxView* view, int workspace_index) {
    if (!view || workspace_index < 0 || workspace_index >= num_workspaces) {
        return;
    }
    
    // Remove from current workspace
    if (view->workspace) {
        view->workspace->views.remove(view);
    }
    
    // Add to new workspace
    view->workspace = workspaces[workspace_index].get();
    view->workspace->views.push_back(view);
    
    std::cout << "Moved window to " << view->workspace->name << std::endl;
}

void FluxboxCompositor::add_view_to_current_workspace(FluxboxView* view) {
    if (!view || !current_workspace) {
        return;
    }
    
    view->workspace = current_workspace;
    current_workspace->views.push_back(view);
}

void FluxboxCompositor::handle_keyboard_key(struct wl_listener* listener, void* data) {
    FluxboxCompositor* compositor = wl_container_of(listener, compositor, keyboard_key);
    struct wlr_keyboard_key_event* event = static_cast<struct wlr_keyboard_key_event*>(data);
    struct wlr_seat* seat = compositor->seat;
    
    // Get the keyboard from the seat
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    if (!keyboard) {
        return;
    }
    
    // Only handle key presses, not releases
    if (event->state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
        return;
    }
    
    // Check for modifiers (Alt key for workspace switching)
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
    bool alt_pressed = modifiers & WLR_MODIFIER_ALT;
    
    // Handle workspace switching (Alt+1, Alt+2, Alt+3, Alt+4)
    if (alt_pressed) {
        // Convert keycode to keysym
        uint32_t keycode = event->keycode + 8; // X11 keycode offset
        xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard->xkb_state, keycode);
        
        switch (keysym) {
        case XKB_KEY_1:
            compositor->switch_workspace(0);
            return;
        case XKB_KEY_2:
            compositor->switch_workspace(1);
            return;
        case XKB_KEY_3:
            compositor->switch_workspace(2);
            return;
        case XKB_KEY_4:
            compositor->switch_workspace(3);
            return;
        case XKB_KEY_Right:
            // Alt+Right: Next workspace
            if (compositor->current_workspace->index < compositor->num_workspaces - 1) {
                compositor->switch_workspace(compositor->current_workspace->index + 1);
            }
            return;
        case XKB_KEY_Left:
            // Alt+Left: Previous workspace
            if (compositor->current_workspace->index > 0) {
                compositor->switch_workspace(compositor->current_workspace->index - 1);
            }
            return;
        case XKB_KEY_q:
            // Alt+Q: Close focused window
            if (compositor->focused_view) {
                wlr_xdg_toplevel_send_close(compositor->focused_view->toplevel);
            }
            return;
        }
    }
    
    // Forward key event to the focused surface
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
}

void FluxboxCompositor::handle_output_frame(struct wl_listener* listener, void* data) {
    struct wlr_output* output = static_cast<struct wlr_output*>(data);
    
    // Get the compositor from the wrapper structure  
    struct output_frame_listener_data* wrapper = wl_container_of(listener, wrapper, listener);
    FluxboxCompositor* compositor = wrapper->compositor;
    
    // Modern wlroots rendering approach using render passes
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    
    struct wlr_render_pass* pass = wlr_output_begin_render_pass(output, &state, nullptr, nullptr);
    if (!pass) {
        wlr_output_state_finish(&state);
        return;
    }
    
    // Clear the screen with a solid color (dark gray for testing)
    struct wlr_render_rect_options rect_options = {};
    rect_options.box.x = 0;
    rect_options.box.y = 0;
    rect_options.box.width = output->width;
    rect_options.box.height = output->height;
    rect_options.color.r = 0.1f;
    rect_options.color.g = 0.1f;
    rect_options.color.b = 0.1f;
    rect_options.color.a = 1.0f;
    
    wlr_render_pass_add_rect(pass, &rect_options);
    
    // Render all views in the current workspace
    if (compositor->current_workspace) {
        for (auto* view : compositor->current_workspace->views) {
            if (!view || !view->toplevel || !view->toplevel->base || !view->toplevel->base->surface) {
                continue;
            }
            
            struct wlr_surface* surface = view->toplevel->base->surface;
            if (!wlr_surface_has_buffer(surface)) {
                continue;
            }
            
            // Get the surface's position and size
            struct wlr_box geo;
            wlr_xdg_surface_get_geometry(view->toplevel->base, &geo);
            
            // Simple rendering: draw surface at a fixed position for now
            struct wlr_render_texture_options texture_options = {};
            texture_options.texture = wlr_surface_get_texture(surface);
            if (!texture_options.texture) {
                continue;
            }
            
            texture_options.dst_box.x = 100; // Fixed position for testing
            texture_options.dst_box.y = 100;
            texture_options.dst_box.width = geo.width > 0 ? geo.width : 800;
            texture_options.dst_box.height = geo.height > 0 ? geo.height : 600;
            
            wlr_render_pass_add_texture(pass, &texture_options);
        }
    }
    
    // Submit the render pass
    if (!wlr_render_pass_submit(pass)) {
        wlr_output_state_finish(&state);
        return;
    }
    
    // Commit the output state
    if (!wlr_output_commit_state(output, &state)) {
        std::cerr << "Failed to commit output " << output->name << std::endl;
    }
    
    wlr_output_state_finish(&state);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Starting Fluxbox Wayland compositor with input handling..." << std::endl;
    
    FluxboxCompositor compositor{};
    
    if (!compositor.startup()) {
        std::cerr << "Failed to start compositor" << std::endl;
        return 1;
    }
    
    std::cout << "=== Fluxbox Wayland Compositor Started! ===" << std::endl;
    std::cout << "WAYLAND_DISPLAY=" << getenv("WAYLAND_DISPLAY") << std::endl;
    std::cout << "Ready to accept Wayland clients!" << std::endl;
    std::cout << "Try: weston-terminal, foot, firefox, etc." << std::endl;
    std::cout << std::endl;
    std::cout << "=== Keyboard Shortcuts ===" << std::endl;
    std::cout << "Alt+1-4: Switch to workspace 1-4" << std::endl;
    std::cout << "Alt+Left/Right: Previous/Next workspace" << std::endl;
    std::cout << "Alt+Q: Close focused window" << std::endl;
    std::cout << compositor.num_workspaces << " workspaces available" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to quit" << std::endl;
    
    compositor.run();
    
    compositor.shutdown();
    std::cout << "Compositor shut down cleanly" << std::endl;
    
    return 0;
}