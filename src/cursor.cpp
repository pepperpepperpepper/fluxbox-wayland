extern "C" {
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>
#include <wlr/util/edges.h>
#include <wayland-server-core.h>
#include <linux/input-event-codes.h>
}

#include <memory>
#include <chrono>
#include <cmath>

#include "server.hpp"
#include "surface.hpp"
#include "config.hpp"

// Forward declarations
class FluxboxServer;
class FluxboxSurface;

// Cursor interaction modes
enum CursorMode {
    CURSOR_NORMAL,
    CURSOR_MOVE,
    CURSOR_RESIZE,
    CURSOR_MENU
};

// Resize edges for window resizing
enum ResizeEdge {
    RESIZE_EDGE_NONE = 0,
    RESIZE_EDGE_TOP = WLR_EDGE_TOP,
    RESIZE_EDGE_BOTTOM = WLR_EDGE_BOTTOM,
    RESIZE_EDGE_LEFT = WLR_EDGE_LEFT,
    RESIZE_EDGE_RIGHT = WLR_EDGE_RIGHT,
    RESIZE_EDGE_TOP_LEFT = WLR_EDGE_TOP | WLR_EDGE_LEFT,
    RESIZE_EDGE_TOP_RIGHT = WLR_EDGE_TOP | WLR_EDGE_RIGHT,
    RESIZE_EDGE_BOTTOM_LEFT = WLR_EDGE_BOTTOM | WLR_EDGE_LEFT,
    RESIZE_EDGE_BOTTOM_RIGHT = WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT,
};

// Click detection for double clicks
struct ClickState {
    std::chrono::steady_clock::time_point last_click_time;
    uint32_t last_button;
    double last_x, last_y;
    static constexpr auto DOUBLE_CLICK_TIME = std::chrono::milliseconds(400);
    static constexpr double DOUBLE_CLICK_DISTANCE = 5.0;
};

// Drag operation state
struct DragState {
    bool active;
    double start_x, start_y;
    double last_x, last_y;
    FluxboxSurface* surface;
    int surface_x, surface_y;
    int surface_width, surface_height;
    uint32_t resize_edges;
    static constexpr double DRAG_THRESHOLD = 3.0;
};

class FluxboxCursor {
public:
    FluxboxCursor(FluxboxServer* server);
    ~FluxboxCursor();
    
    // Initialize cursor with output layout
    bool initialize();
    
    // Add input device (mouse/touchpad)
    void add_input_device(struct wlr_input_device* device);
    
    // Update cursor position and appearance
    void update_position(double x, double y);
    void set_cursor_image(const char* cursor_name);
    
    // Get cursor position
    void get_position(double* x, double* y) const;
    
    // Get wlr_cursor
    struct wlr_cursor* get_cursor() const { return cursor; }
    
    // Configuration
    void set_focus_follows_mouse(bool enabled) { focus_follows_mouse = enabled; }
    void set_auto_raise(bool enabled) { auto_raise = enabled; }
    
private:
    FluxboxServer* server;
    struct wlr_cursor* cursor;
    struct wlr_xcursor_manager* xcursor_manager;
    
    // Cursor state
    CursorMode mode;
    ClickState click_state;
    DragState drag_state;
    
    // Configuration
    bool focus_follows_mouse;
    bool auto_raise;
    uint32_t button_state;
    
    // Event listeners
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    
    // Static event handlers
    static void handle_cursor_motion(struct wl_listener* listener, void* data);
    static void handle_cursor_motion_absolute(struct wl_listener* listener, void* data);
    static void handle_cursor_button(struct wl_listener* listener, void* data);
    static void handle_cursor_axis(struct wl_listener* listener, void* data);
    static void handle_cursor_frame(struct wl_listener* listener, void* data);
    
    // Motion handling
    void process_cursor_motion(uint32_t time_msec);
    void update_drag_state(double x, double y);
    
    // Button handling
    void handle_button_press(uint32_t button, double x, double y, uint32_t time_msec);
    void handle_button_release(uint32_t button, double x, double y, uint32_t time_msec);
    bool check_double_click(uint32_t button, double x, double y);
    
    // Interaction helpers
    FluxboxSurface* surface_at(double x, double y, double* sx, double* sy);
    ResizeEdge get_resize_edge(FluxboxSurface* surface, double sx, double sy);
    void begin_move(FluxboxSurface* surface);
    void begin_resize(FluxboxSurface* surface, uint32_t edges);
    void update_cursor_focus();
    
    // Cursor image helpers
    void set_cursor_for_mode();
    void set_cursor_for_resize_edge(uint32_t edges);
};

// Implementation

FluxboxCursor::FluxboxCursor(FluxboxServer* server)
    : server(server)
    , cursor(nullptr)
    , xcursor_manager(nullptr)
    , mode(CURSOR_NORMAL)
    , focus_follows_mouse(false)
    , auto_raise(false)
    , button_state(0) {
    
    // Initialize listeners
    cursor_motion.notify = handle_cursor_motion;
    cursor_motion_absolute.notify = handle_cursor_motion_absolute;
    cursor_button.notify = handle_cursor_button;
    cursor_axis.notify = handle_cursor_axis;
    cursor_frame.notify = handle_cursor_frame;
    
    // Initialize states
    click_state = {};
    drag_state = {};
}

FluxboxCursor::~FluxboxCursor() {
    // Clean up listeners
    wl_list_remove(&cursor_motion.link);
    wl_list_remove(&cursor_motion_absolute.link);
    wl_list_remove(&cursor_button.link);
    wl_list_remove(&cursor_axis.link);
    wl_list_remove(&cursor_frame.link);
    
    if (xcursor_manager) {
        wlr_xcursor_manager_destroy(xcursor_manager);
    }
    
    if (cursor) {
        wlr_cursor_destroy(cursor);
    }
}

bool FluxboxCursor::initialize() {
    // Create cursor
    cursor = wlr_cursor_create();
    if (!cursor) {
        wlr_log(WLR_ERROR, "Failed to create cursor");
        return false;
    }
    
    // Attach cursor to output layout
    wlr_cursor_attach_output_layout(cursor, server->get_output_layout());
    
    // Create xcursor manager
    xcursor_manager = wlr_xcursor_manager_create(nullptr, 24);
    if (!xcursor_manager) {
        wlr_log(WLR_ERROR, "Failed to create xcursor manager");
        return false;
    }
    
    // Load cursor theme at scale 1
    wlr_xcursor_manager_load(xcursor_manager, 1);
    
    // Set up event listeners
    wl_signal_add(&cursor->events.motion, &cursor_motion);
    wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
    wl_signal_add(&cursor->events.button, &cursor_button);
    wl_signal_add(&cursor->events.axis, &cursor_axis);
    wl_signal_add(&cursor->events.frame, &cursor_frame);
    
    // Set default cursor
    set_cursor_image("left_ptr");
    
    wlr_log(WLR_INFO, "Cursor initialized successfully");
    return true;
}

void FluxboxCursor::add_input_device(struct wlr_input_device* device) {
    switch (device->type) {
    case WLR_INPUT_DEVICE_POINTER:
        wlr_cursor_attach_input_device(cursor, device);
        wlr_log(WLR_INFO, "Added pointer device: %s", device->name);
        break;
    default:
        break;
    }
}

void FluxboxCursor::update_position(double x, double y) {
    wlr_cursor_warp_closest(cursor, nullptr, x, y);
    process_cursor_motion(0);
}

void FluxboxCursor::set_cursor_image(const char* cursor_name) {
    wlr_cursor_set_xcursor(cursor, xcursor_manager, cursor_name);
}

void FluxboxCursor::get_position(double* x, double* y) const {
    if (x) *x = cursor->x;
    if (y) *y = cursor->y;
}

// Motion handling

void FluxboxCursor::process_cursor_motion(uint32_t time_msec) {
    // Update drag state if active
    if (drag_state.active) {
        update_drag_state(cursor->x, cursor->y);
        return;
    }
    
    // Update cursor focus
    update_cursor_focus();
    
    // Update cursor image based on what's under it
    if (mode == CURSOR_NORMAL) {
        double sx, sy;
        FluxboxSurface* surface = surface_at(cursor->x, cursor->y, &sx, &sy);
        
        if (surface) {
            // Check if we're over a resize edge
            ResizeEdge edge = get_resize_edge(surface, sx, sy);
            if (edge != RESIZE_EDGE_NONE) {
                set_cursor_for_resize_edge(edge);
            } else {
                set_cursor_image("left_ptr");
            }
            
            // Focus follows mouse
            if (focus_follows_mouse && surface != server->get_focused_surface()) {
                server->set_focus(surface);
                if (auto_raise) {
                    // TODO: Implement raise
                }
            }
        } else {
            set_cursor_image("left_ptr");
        }
    }
}

void FluxboxCursor::update_drag_state(double x, double y) {
    if (!drag_state.surface) return;
    
    double dx = x - drag_state.last_x;
    double dy = y - drag_state.last_y;
    
    switch (mode) {
    case CURSOR_MOVE: {
        int new_x = drag_state.surface_x + (x - drag_state.start_x);
        int new_y = drag_state.surface_y + (y - drag_state.start_y);
        drag_state.surface->set_position(new_x, new_y);
        break;
    }
    
    case CURSOR_RESIZE: {
        int new_width = drag_state.surface_width;
        int new_height = drag_state.surface_height;
        int new_x = drag_state.surface_x;
        int new_y = drag_state.surface_y;
        
        if (drag_state.resize_edges & WLR_EDGE_RIGHT) {
            new_width = drag_state.surface_width + (x - drag_state.start_x);
        } else if (drag_state.resize_edges & WLR_EDGE_LEFT) {
            new_width = drag_state.surface_width - (x - drag_state.start_x);
            new_x = drag_state.surface_x + (x - drag_state.start_x);
        }
        
        if (drag_state.resize_edges & WLR_EDGE_BOTTOM) {
            new_height = drag_state.surface_height + (y - drag_state.start_y);
        } else if (drag_state.resize_edges & WLR_EDGE_TOP) {
            new_height = drag_state.surface_height - (y - drag_state.start_y);
            new_y = drag_state.surface_y + (y - drag_state.start_y);
        }
        
        // Apply minimum size constraints
        new_width = std::max(new_width, 100);
        new_height = std::max(new_height, 100);
        
        // Update position if resizing from top/left
        if (drag_state.resize_edges & (WLR_EDGE_LEFT | WLR_EDGE_TOP)) {
            drag_state.surface->set_position(new_x, new_y);
        }
        
        drag_state.surface->set_size(new_width, new_height);
        break;
    }
    
    default:
        break;
    }
    
    drag_state.last_x = x;
    drag_state.last_y = y;
}

// Button handling

void FluxboxCursor::handle_button_press(uint32_t button, double x, double y, uint32_t time_msec) {
    button_state |= (1 << button);
    
    // Check for double click
    bool is_double_click = check_double_click(button, x, y);
    
    // Find surface under cursor
    double sx, sy;
    FluxboxSurface* surface = surface_at(x, y, &sx, &sy);
    
    if (surface) {
        // Focus the surface
        server->set_focus(surface);
        
        // Get current modifiers
        uint32_t modifiers = wlr_keyboard_get_modifiers(wlr_seat_get_keyboard(server->get_seat()));
        
        // Alt + Left Button = Move
        if (button == BTN_LEFT && (modifiers & WLR_MODIFIER_ALT)) {
            begin_move(surface);
            return;
        }
        
        // Alt + Right Button = Resize
        if (button == BTN_RIGHT && (modifiers & WLR_MODIFIER_ALT)) {
            begin_resize(surface, RESIZE_EDGE_BOTTOM_RIGHT);
            return;
        }
        
        // Alt + Middle Button = Lower
        if (button == BTN_MIDDLE && (modifiers & WLR_MODIFIER_ALT)) {
            // TODO: Implement window lowering
            return;
        }
        
        // Check if we're on a resize edge
        ResizeEdge edge = get_resize_edge(surface, sx, sy);
        if (edge != RESIZE_EDGE_NONE && button == BTN_LEFT) {
            begin_resize(surface, edge);
            return;
        }
        
        // Handle titlebar actions
        if (sy < 30) { // Assuming 30px titlebar
            if (button == BTN_LEFT) {
                if (is_double_click) {
                    // Double click on titlebar = maximize/restore
                    if (surface->is_maximized()) {
                        surface->unmaximize();
                    } else {
                        surface->maximize();
                    }
                } else {
                    // Single click = begin move
                    begin_move(surface);
                }
            } else if (button == BTN_MIDDLE) {
                // Middle click on titlebar = lower
                // TODO: Implement window lowering
            } else if (button == BTN_RIGHT) {
                // Right click on titlebar = window menu
                // TODO: Show window menu
            }
            return;
        }
        
        // Pass click to client
        wlr_seat_pointer_notify_button(server->get_seat(), time_msec, button, WL_POINTER_BUTTON_STATE_PRESSED);
        
    } else {
        // Click on desktop
        if (button == BTN_RIGHT) {
            // Right click on desktop = root menu
            // TODO: Show root menu
        } else if (button == BTN_MIDDLE) {
            // Middle click on desktop = workspace menu
            // TODO: Show workspace menu
        }
    }
    
    // Update click state for double-click detection
    click_state.last_click_time = std::chrono::steady_clock::now();
    click_state.last_button = button;
    click_state.last_x = x;
    click_state.last_y = y;
}

void FluxboxCursor::handle_button_release(uint32_t button, double x, double y, uint32_t time_msec) {
    button_state &= ~(1 << button);
    
    // End drag operation
    if (drag_state.active && button == BTN_LEFT) {
        drag_state.active = false;
        mode = CURSOR_NORMAL;
        set_cursor_for_mode();
    }
    
    // Pass button release to client if not in drag mode
    if (mode == CURSOR_NORMAL) {
        wlr_seat_pointer_notify_button(server->get_seat(), time_msec, button, WL_POINTER_BUTTON_STATE_RELEASED);
    }
}

bool FluxboxCursor::check_double_click(uint32_t button, double x, double y) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - click_state.last_click_time;
    
    if (button == click_state.last_button &&
        elapsed < ClickState::DOUBLE_CLICK_TIME &&
        std::abs(x - click_state.last_x) < ClickState::DOUBLE_CLICK_DISTANCE &&
        std::abs(y - click_state.last_y) < ClickState::DOUBLE_CLICK_DISTANCE) {
        return true;
    }
    
    return false;
}

// Interaction helpers

FluxboxSurface* FluxboxCursor::surface_at(double x, double y, double* sx, double* sy) {
    // This is a simplified version - in reality we'd use scene graph hit testing
    struct wlr_surface* wlr_surface = nullptr;
    double lx, ly;
    
    // Find surface at cursor position
    struct wlr_output* output = wlr_output_layout_output_at(
        server->get_output_layout(), x, y);
    
    if (output) {
        // TODO: Use proper scene graph hit testing
        // For now, iterate through surfaces
        for (auto* surface : server->get_surfaces()) {
            int surf_x, surf_y, surf_w, surf_h;
            surface->get_position(&surf_x, &surf_y);
            surface->get_size(&surf_w, &surf_h);
            
            if (x >= surf_x && x < surf_x + surf_w &&
                y >= surf_y && y < surf_y + surf_h) {
                if (sx) *sx = x - surf_x;
                if (sy) *sy = y - surf_y;
                return surface;
            }
        }
    }
    
    return nullptr;
}

ResizeEdge FluxboxCursor::get_resize_edge(FluxboxSurface* surface, double sx, double sy) {
    const int EDGE_SIZE = 10;
    int width, height;
    surface->get_size(&width, &height);
    
    int edge = RESIZE_EDGE_NONE;
    
    if (sx < EDGE_SIZE) {
        edge |= RESIZE_EDGE_LEFT;
    } else if (sx >= width - EDGE_SIZE) {
        edge |= RESIZE_EDGE_RIGHT;
    }
    
    if (sy < EDGE_SIZE) {
        edge |= RESIZE_EDGE_TOP;
    } else if (sy >= height - EDGE_SIZE) {
        edge |= RESIZE_EDGE_BOTTOM;
    }
    
    return static_cast<ResizeEdge>(edge);
}

void FluxboxCursor::begin_move(FluxboxSurface* surface) {
    mode = CURSOR_MOVE;
    drag_state.active = true;
    drag_state.surface = surface;
    drag_state.start_x = cursor->x;
    drag_state.start_y = cursor->y;
    drag_state.last_x = cursor->x;
    drag_state.last_y = cursor->y;
    surface->get_position(&drag_state.surface_x, &drag_state.surface_y);
    
    set_cursor_image("grabbing");
}

void FluxboxCursor::begin_resize(FluxboxSurface* surface, uint32_t edges) {
    mode = CURSOR_RESIZE;
    drag_state.active = true;
    drag_state.surface = surface;
    drag_state.start_x = cursor->x;
    drag_state.start_y = cursor->y;
    drag_state.last_x = cursor->x;
    drag_state.last_y = cursor->y;
    drag_state.resize_edges = edges;
    surface->get_position(&drag_state.surface_x, &drag_state.surface_y);
    surface->get_size(&drag_state.surface_width, &drag_state.surface_height);
    
    set_cursor_for_resize_edge(edges);
}

void FluxboxCursor::update_cursor_focus() {
    double sx, sy;
    struct wlr_seat* seat = server->get_seat();
    FluxboxSurface* surface = surface_at(cursor->x, cursor->y, &sx, &sy);
    
    if (!surface) {
        // Clear pointer focus when not over a surface
        wlr_seat_pointer_clear_focus(seat);
    } else {
        // Set pointer focus to surface under cursor
        struct wlr_surface* wlr_surface = surface->get_toplevel()->base->surface;
        wlr_seat_pointer_notify_enter(seat, wlr_surface, sx, sy);
        
        if (mode == CURSOR_NORMAL) {
            wlr_seat_pointer_notify_motion(seat, 0, sx, sy);
        }
    }
}

// Cursor image helpers

void FluxboxCursor::set_cursor_for_mode() {
    switch (mode) {
    case CURSOR_MOVE:
        set_cursor_image("grabbing");
        break;
    case CURSOR_RESIZE:
        set_cursor_for_resize_edge(drag_state.resize_edges);
        break;
    default:
        set_cursor_image("left_ptr");
        break;
    }
}

void FluxboxCursor::set_cursor_for_resize_edge(uint32_t edges) {
    if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_LEFT)) {
        set_cursor_image("nw-resize");
    } else if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_RIGHT)) {
        set_cursor_image("ne-resize");
    } else if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_LEFT)) {
        set_cursor_image("sw-resize");
    } else if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_RIGHT)) {
        set_cursor_image("se-resize");
    } else if (edges & WLR_EDGE_TOP) {
        set_cursor_image("n-resize");
    } else if (edges & WLR_EDGE_BOTTOM) {
        set_cursor_image("s-resize");
    } else if (edges & WLR_EDGE_LEFT) {
        set_cursor_image("w-resize");
    } else if (edges & WLR_EDGE_RIGHT) {
        set_cursor_image("e-resize");
    }
}

// Static event handlers

void FluxboxCursor::handle_cursor_motion(struct wl_listener* listener, void* data) {
    FluxboxCursor* cursor = wl_container_of(listener, cursor, cursor_motion);
    struct wlr_pointer_motion_event* event = static_cast<struct wlr_pointer_motion_event*>(data);
    
    wlr_cursor_move(cursor->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    cursor->process_cursor_motion(event->time_msec);
}

void FluxboxCursor::handle_cursor_motion_absolute(struct wl_listener* listener, void* data) {
    FluxboxCursor* cursor = wl_container_of(listener, cursor, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event* event = 
        static_cast<struct wlr_pointer_motion_absolute_event*>(data);
    
    wlr_cursor_warp_absolute(cursor->cursor, &event->pointer->base, event->x, event->y);
    cursor->process_cursor_motion(event->time_msec);
}

void FluxboxCursor::handle_cursor_button(struct wl_listener* listener, void* data) {
    FluxboxCursor* cursor = wl_container_of(listener, cursor, cursor_button);
    struct wlr_pointer_button_event* event = static_cast<struct wlr_pointer_button_event*>(data);
    
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        cursor->handle_button_press(event->button, cursor->cursor->x, cursor->cursor->y, event->time_msec);
    } else {
        cursor->handle_button_release(event->button, cursor->cursor->x, cursor->cursor->y, event->time_msec);
    }
}

void FluxboxCursor::handle_cursor_axis(struct wl_listener* listener, void* data) {
    FluxboxCursor* cursor = wl_container_of(listener, cursor, cursor_axis);
    struct wlr_pointer_axis_event* event = static_cast<struct wlr_pointer_axis_event*>(data);
    
    // Handle scroll wheel
    if (cursor->mode == CURSOR_NORMAL) {
        // Check if we're over a window or desktop
        double sx, sy;
        FluxboxSurface* surface = cursor->surface_at(cursor->cursor->x, cursor->cursor->y, &sx, &sy);
        
        if (!surface) {
            // Scrolling on desktop - switch workspaces
            if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
                int current = cursor->server->get_current_workspace_index();
                int count = cursor->server->get_workspace_count();
                
                if (event->delta > 0) {
                    // Scroll down - next workspace
                    cursor->server->switch_workspace((current + 1) % count);
                } else {
                    // Scroll up - previous workspace
                    cursor->server->switch_workspace((current - 1 + count) % count);
                }
                return;
            }
        }
        
        // Pass scroll to client
        wlr_seat_pointer_notify_axis(cursor->server->get_seat(), event->time_msec,
            event->orientation, event->delta, event->delta_discrete, event->source,
            event->relative_direction);
    }
}

void FluxboxCursor::handle_cursor_frame(struct wl_listener* listener, void* data) {
    FluxboxCursor* cursor = wl_container_of(listener, cursor, cursor_frame);
    wlr_seat_pointer_notify_frame(cursor->server->get_seat());
}

// Global cursor instance
static std::unique_ptr<FluxboxCursor> g_cursor;

// Function to create and initialize cursor for the server
extern "C" void fluxbox_cursor_create(FluxboxServer* server) {
    g_cursor = std::make_unique<FluxboxCursor>(server);
    if (g_cursor->initialize()) {
        // Load configuration
        if (server->get_config()) {
            g_cursor->set_focus_follows_mouse(server->get_config()->get_focus_follows_mouse());
            g_cursor->set_auto_raise(server->get_config()->get_auto_raise());
        }
        wlr_log(WLR_INFO, "Cursor system initialized");
    } else {
        wlr_log(WLR_ERROR, "Failed to initialize cursor system");
        g_cursor.reset();
    }
}

// Function to add pointer device
extern "C" void fluxbox_cursor_add_device(struct wlr_input_device* device) {
    if (g_cursor) {
        g_cursor->add_input_device(device);
    }
}

// Function to get cursor position
extern "C" void fluxbox_cursor_get_position(double* x, double* y) {
    if (g_cursor) {
        g_cursor->get_position(x, y);
    }
}

// Function to get the wlr_cursor
extern "C" struct wlr_cursor* fluxbox_get_cursor() {
    if (g_cursor) {
        return g_cursor->get_cursor();
    }
    return nullptr;
}