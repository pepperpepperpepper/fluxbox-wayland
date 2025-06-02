extern "C" {
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include <wayland-server-core.h>
#include <linux/input-event-codes.h>
}

#include <memory>
#include <vector>
#include <string>

#include "server.hpp"
#include "surface.hpp"

// Forward declarations
class FluxboxServer;
class FluxboxSurface;

// External functions from keyboard and cursor modules
extern "C" void fluxbox_add_keyboard(FluxboxServer* server, struct wlr_input_device* device);
extern "C" void fluxbox_cursor_add_device(struct wlr_input_device* device);
extern "C" struct wlr_cursor* fluxbox_get_cursor();

// Input device wrapper
struct InputDevice {
    struct wlr_input_device* device;
    std::string name;
    
    InputDevice(struct wlr_input_device* dev) 
        : device(dev)
        , name(dev->name ? dev->name : "unknown") {}
};

class FluxboxSeat {
public:
    FluxboxSeat(FluxboxServer* server, const char* name);
    ~FluxboxSeat();
    
    // Initialize seat
    bool initialize();
    
    // Input device management
    void add_input_device(struct wlr_input_device* device);
    void remove_input_device(struct wlr_input_device* device);
    
    // Focus management
    void set_keyboard_focus(FluxboxSurface* surface);
    void set_pointer_focus(FluxboxSurface* surface, double sx, double sy);
    void clear_focus();
    
    // Capabilities
    void update_capabilities();
    uint32_t get_capabilities() const { return capabilities; }
    
    // Get wlr_seat
    struct wlr_seat* get_seat() const { return seat; }
    
    // Selection/clipboard
    void set_selection(struct wlr_data_source* source, uint32_t serial);
    void set_primary_selection(struct wlr_primary_selection_source* source, uint32_t serial);
    
private:
    FluxboxServer* server;
    struct wlr_seat* seat;
    std::string seat_name;
    
    // Capabilities
    uint32_t capabilities;
    bool has_keyboard;
    bool has_pointer;
    bool has_touch;
    
    // Input devices
    std::vector<std::unique_ptr<InputDevice>> keyboards;
    std::vector<std::unique_ptr<InputDevice>> pointers;
    std::vector<std::unique_ptr<InputDevice>> touch_devices;
    
    // Current focus
    FluxboxSurface* keyboard_focus;
    FluxboxSurface* pointer_focus;
    
    // Event listeners
    struct wl_listener selection;
    struct wl_listener primary_selection;
    struct wl_listener request_set_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener destroy;
    
    // Static event handlers
    static void handle_selection(struct wl_listener* listener, void* data);
    static void handle_primary_selection(struct wl_listener* listener, void* data);
    static void handle_request_set_cursor(struct wl_listener* listener, void* data);
    static void handle_request_set_selection(struct wl_listener* listener, void* data);
    static void handle_request_set_primary_selection(struct wl_listener* listener, void* data);
    static void handle_request_start_drag(struct wl_listener* listener, void* data);
    static void handle_start_drag(struct wl_listener* listener, void* data);
    static void handle_destroy(struct wl_listener* listener, void* data);
    
    // Helper methods
    void setup_listeners();
    InputDevice* find_device(struct wlr_input_device* device);
};

// Implementation

FluxboxSeat::FluxboxSeat(FluxboxServer* server, const char* name)
    : server(server)
    , seat(nullptr)
    , seat_name(name ? name : "seat0")
    , capabilities(0)
    , has_keyboard(false)
    , has_pointer(false)
    , has_touch(false)
    , keyboard_focus(nullptr)
    , pointer_focus(nullptr) {
    
    // Initialize listeners
    selection.notify = handle_selection;
    primary_selection.notify = handle_primary_selection;
    request_set_cursor.notify = handle_request_set_cursor;
    request_set_selection.notify = handle_request_set_selection;
    request_set_primary_selection.notify = handle_request_set_primary_selection;
    request_start_drag.notify = handle_request_start_drag;
    start_drag.notify = handle_start_drag;
    destroy.notify = handle_destroy;
}

FluxboxSeat::~FluxboxSeat() {
    // Remove listeners
    wl_list_remove(&selection.link);
    wl_list_remove(&primary_selection.link);
    wl_list_remove(&request_set_cursor.link);
    wl_list_remove(&request_set_selection.link);
    wl_list_remove(&request_set_primary_selection.link);
    wl_list_remove(&request_start_drag.link);
    wl_list_remove(&start_drag.link);
    wl_list_remove(&destroy.link);
    
    // Seat is destroyed by wlroots
}

bool FluxboxSeat::initialize() {
    // The seat should already be created by the server
    seat = server->get_seat();
    if (!seat) {
        wlr_log(WLR_ERROR, "Server seat not available");
        return false;
    }
    
    // Set up event listeners
    setup_listeners();
    
    // Initialize capabilities (will be updated when devices are added)
    update_capabilities();
    
    wlr_log(WLR_INFO, "Seat '%s' initialized", seat_name.c_str());
    return true;
}

void FluxboxSeat::add_input_device(struct wlr_input_device* device) {
    if (!device) return;
    
    wlr_log(WLR_INFO, "Adding input device: %s (type: %d)", 
            device->name ? device->name : "unknown", device->type);
    
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        auto input_device = std::make_unique<InputDevice>(device);
        keyboards.push_back(std::move(input_device));
        has_keyboard = true;
        
        // Delegate to keyboard module
        fluxbox_add_keyboard(server, device);
        break;
    }
    
    case WLR_INPUT_DEVICE_POINTER: {
        auto input_device = std::make_unique<InputDevice>(device);
        pointers.push_back(std::move(input_device));
        has_pointer = true;
        
        // Delegate to cursor module
        fluxbox_cursor_add_device(device);
        break;
    }
    
    case WLR_INPUT_DEVICE_TOUCH: {
        auto input_device = std::make_unique<InputDevice>(device);
        touch_devices.push_back(std::move(input_device));
        has_touch = true;
        
        // TODO: Implement touch support
        wlr_log(WLR_INFO, "Touch device added (not fully implemented)");
        break;
    }
    
    default:
        wlr_log(WLR_INFO, "Unsupported input device type: %d", device->type);
        break;
    }
    
    // Update seat capabilities
    update_capabilities();
}

void FluxboxSeat::remove_input_device(struct wlr_input_device* device) {
    if (!device) return;
    
    wlr_log(WLR_INFO, "Removing input device: %s", 
            device->name ? device->name : "unknown");
    
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        keyboards.erase(
            std::remove_if(keyboards.begin(), keyboards.end(),
                [device](const auto& dev) { return dev->device == device; }),
            keyboards.end());
        has_keyboard = !keyboards.empty();
        break;
        
    case WLR_INPUT_DEVICE_POINTER:
        pointers.erase(
            std::remove_if(pointers.begin(), pointers.end(),
                [device](const auto& dev) { return dev->device == device; }),
            pointers.end());
        has_pointer = !pointers.empty();
        break;
        
    case WLR_INPUT_DEVICE_TOUCH:
        touch_devices.erase(
            std::remove_if(touch_devices.begin(), touch_devices.end(),
                [device](const auto& dev) { return dev->device == device; }),
            touch_devices.end());
        has_touch = !touch_devices.empty();
        break;
        
    default:
        break;
    }
    
    // Update seat capabilities
    update_capabilities();
}

void FluxboxSeat::set_keyboard_focus(FluxboxSurface* surface) {
    if (keyboard_focus == surface) {
        return;
    }
    
    keyboard_focus = surface;
    
    if (surface) {
        struct wlr_surface* wlr_surface = surface->get_toplevel()->base->surface;
        struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
        
        if (keyboard) {
            // Send modifiers
            wlr_seat_keyboard_notify_modifiers(seat, &keyboard->modifiers);
        }
        
        // Notify keyboard enter
        wlr_seat_keyboard_notify_enter(seat, wlr_surface, 
            keyboard ? keyboard->keycodes : nullptr,
            keyboard ? keyboard->num_keycodes : 0,
            keyboard ? &keyboard->modifiers : nullptr);
        
        wlr_log(WLR_DEBUG, "Keyboard focus set to surface: %s", 
                surface->get_title() ? surface->get_title() : "untitled");
    } else {
        // Clear keyboard focus
        wlr_seat_keyboard_notify_clear_focus(seat);
        wlr_log(WLR_DEBUG, "Keyboard focus cleared");
    }
}

void FluxboxSeat::set_pointer_focus(FluxboxSurface* surface, double sx, double sy) {
    if (pointer_focus == surface) {
        // Just update motion
        if (surface) {
            wlr_seat_pointer_notify_motion(seat, 0, sx, sy);
        }
        return;
    }
    
    pointer_focus = surface;
    
    if (surface) {
        struct wlr_surface* wlr_surface = surface->get_toplevel()->base->surface;
        wlr_seat_pointer_notify_enter(seat, wlr_surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, 0, sx, sy);
    } else {
        wlr_seat_pointer_notify_clear_focus(seat);
    }
}

void FluxboxSeat::clear_focus() {
    set_keyboard_focus(nullptr);
    set_pointer_focus(nullptr, 0, 0);
}

void FluxboxSeat::update_capabilities() {
    uint32_t new_caps = 0;
    
    if (has_keyboard) {
        new_caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    if (has_pointer) {
        new_caps |= WL_SEAT_CAPABILITY_POINTER;
    }
    if (has_touch) {
        new_caps |= WL_SEAT_CAPABILITY_TOUCH;
    }
    
    if (new_caps != capabilities) {
        capabilities = new_caps;
        wlr_seat_set_capabilities(seat, capabilities);
        
        wlr_log(WLR_INFO, "Seat capabilities updated: keyboard=%d, pointer=%d, touch=%d",
                has_keyboard, has_pointer, has_touch);
    }
}

void FluxboxSeat::set_selection(struct wlr_data_source* source, uint32_t serial) {
    wlr_seat_set_selection(seat, source, serial);
}

void FluxboxSeat::set_primary_selection(struct wlr_primary_selection_source* source, uint32_t serial) {
    wlr_seat_set_primary_selection(seat, source, serial);
}

void FluxboxSeat::setup_listeners() {
    // Selection events
    wl_signal_add(&seat->events.set_selection, &selection);
    wl_signal_add(&seat->events.set_primary_selection, &primary_selection);
    
    // Request events
    wl_signal_add(&seat->events.request_set_cursor, &request_set_cursor);
    wl_signal_add(&seat->events.request_set_selection, &request_set_selection);
    wl_signal_add(&seat->events.request_set_primary_selection, &request_set_primary_selection);
    wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
    wl_signal_add(&seat->events.start_drag, &start_drag);
    wl_signal_add(&seat->events.destroy, &destroy);
}

InputDevice* FluxboxSeat::find_device(struct wlr_input_device* device) {
    // Search in keyboards
    for (auto& kbd : keyboards) {
        if (kbd->device == device) return kbd.get();
    }
    
    // Search in pointers
    for (auto& ptr : pointers) {
        if (ptr->device == device) return ptr.get();
    }
    
    // Search in touch devices
    for (auto& touch : touch_devices) {
        if (touch->device == device) return touch.get();
    }
    
    return nullptr;
}

// Static event handlers

void FluxboxSeat::handle_selection(struct wl_listener* listener, void* data) {
    FluxboxSeat* seat = wl_container_of(listener, seat, selection);
    wlr_log(WLR_DEBUG, "Selection changed");
}

void FluxboxSeat::handle_primary_selection(struct wl_listener* listener, void* data) {
    FluxboxSeat* seat = wl_container_of(listener, seat, primary_selection);
    wlr_log(WLR_DEBUG, "Primary selection changed");
}

void FluxboxSeat::handle_request_set_cursor(struct wl_listener* listener, void* data) {
    FluxboxSeat* seat = wl_container_of(listener, seat, request_set_cursor);
    struct wlr_seat_pointer_request_set_cursor_event* event = 
        static_cast<struct wlr_seat_pointer_request_set_cursor_event*>(data);
    
    // This event is sent by clients to set their cursor image
    // For now, we'll allow it if the client has pointer focus
    struct wlr_seat_client* focused_client = seat->seat->pointer_state.focused_client;
    
    if (focused_client == event->seat_client) {
        // Get the cursor from the cursor module
        struct wlr_cursor* cursor = fluxbox_get_cursor();
        if (cursor) {
            wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y);
        }
    }
}

void FluxboxSeat::handle_request_set_selection(struct wl_listener* listener, void* data) {
    FluxboxSeat* seat = wl_container_of(listener, seat, request_set_selection);
    struct wlr_seat_request_set_selection_event* event = 
        static_cast<struct wlr_seat_request_set_selection_event*>(data);
    
    wlr_seat_set_selection(seat->seat, event->source, event->serial);
}

void FluxboxSeat::handle_request_set_primary_selection(struct wl_listener* listener, void* data) {
    FluxboxSeat* seat = wl_container_of(listener, seat, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event* event = 
        static_cast<struct wlr_seat_request_set_primary_selection_event*>(data);
    
    wlr_seat_set_primary_selection(seat->seat, event->source, event->serial);
}

void FluxboxSeat::handle_request_start_drag(struct wl_listener* listener, void* data) {
    FluxboxSeat* seat = wl_container_of(listener, seat, request_start_drag);
    struct wlr_seat_request_start_drag_event* event = 
        static_cast<struct wlr_seat_request_start_drag_event*>(data);
    
    // Validate drag request
    if (wlr_seat_validate_pointer_grab_serial(seat->seat, event->origin, event->serial)) {
        wlr_seat_start_pointer_drag(seat->seat, event->drag, event->serial);
    } else {
        wlr_data_source_destroy(event->drag->source);
    }
}

void FluxboxSeat::handle_start_drag(struct wl_listener* listener, void* data) {
    FluxboxSeat* seat = wl_container_of(listener, seat, start_drag);
    struct wlr_drag* drag = static_cast<struct wlr_drag*>(data);
    
    wlr_log(WLR_DEBUG, "Drag started");
    
    // TODO: Implement drag visualization
}

void FluxboxSeat::handle_destroy(struct wl_listener* listener, void* data) {
    FluxboxSeat* seat = wl_container_of(listener, seat, destroy);
    wlr_log(WLR_INFO, "Seat destroyed");
    delete seat;
}

// Global seat instance
static std::unique_ptr<FluxboxSeat> g_seat;

// Public API functions

extern "C" void fluxbox_seat_create(FluxboxServer* server, const char* name) {
    g_seat = std::make_unique<FluxboxSeat>(server, name);
    if (!g_seat->initialize()) {
        wlr_log(WLR_ERROR, "Failed to initialize seat");
        g_seat.reset();
    }
}

extern "C" void fluxbox_seat_add_device(struct wlr_input_device* device) {
    if (g_seat) {
        g_seat->add_input_device(device);
    }
}

extern "C" void fluxbox_seat_remove_device(struct wlr_input_device* device) {
    if (g_seat) {
        g_seat->remove_input_device(device);
    }
}

extern "C" void fluxbox_seat_set_keyboard_focus(FluxboxSurface* surface) {
    if (g_seat) {
        g_seat->set_keyboard_focus(surface);
    }
}

extern "C" void fluxbox_seat_set_pointer_focus(FluxboxSurface* surface, double sx, double sy) {
    if (g_seat) {
        g_seat->set_pointer_focus(surface, sx, sy);
    }
}

extern "C" void fluxbox_seat_clear_focus() {
    if (g_seat) {
        g_seat->clear_focus();
    }
}