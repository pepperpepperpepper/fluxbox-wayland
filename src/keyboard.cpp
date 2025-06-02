extern "C" {
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-server-core.h>
}

#include <iostream>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <unistd.h>
#include <functional>

#include "server.hpp"
#include "config.hpp"
#include "surface.hpp"
#include "workspace.hpp"

// Forward declarations
class FluxboxKeyboard;
class FluxboxServer;

// Key binding context
enum KeyContext {
    CONTEXT_NONE = 0,
    CONTEXT_WINDOW = 1 << 0,
    CONTEXT_TITLEBAR = 1 << 1,
    CONTEXT_DESKTOP = 1 << 2,
    CONTEXT_TOOLBAR = 1 << 3,
    CONTEXT_TAB = 1 << 4,
    CONTEXT_ALL = 0xFF
};

// Key binding chain state
struct KeyChain {
    std::vector<uint32_t> keys;
    std::vector<uint32_t> modifiers;
    std::chrono::steady_clock::time_point last_press;
    static constexpr auto CHAIN_TIMEOUT = std::chrono::milliseconds(1000);
};

// Key handler class
class FluxboxKeyboard {
public:
    FluxboxKeyboard(FluxboxServer* server, struct wlr_input_device* device);
    ~FluxboxKeyboard();

    // Initialize keyboard with XKB context
    bool initialize();
    
    // Handle key events
    void handle_key(struct wlr_keyboard_key_event* event);
    void handle_modifiers();
    
    // Configuration
    void load_key_bindings(const FluxboxConfig& config);
    void reload_key_bindings();
    
    // Get keyboard device
    struct wlr_keyboard* get_keyboard() { return keyboard; }
    
private:
    FluxboxServer* server;
    struct wlr_keyboard* keyboard;
    struct wlr_input_device* device;
    
    // XKB context
    struct xkb_context* xkb_context;
    
    // Event listeners
    struct wl_listener key;
    struct wl_listener modifiers;
    struct wl_listener destroy;
    
    // Key state
    uint32_t current_modifiers;
    KeyChain current_chain;
    bool chain_active;
    
    // Key bindings storage
    struct KeyBinding {
        std::vector<uint32_t> keys;      // Keysyms
        uint32_t modifiers;              // Modifier mask
        KeyContext context;              // Where the binding is active
        std::string action;              // Action to execute
        std::vector<std::string> args;   // Arguments
        bool is_chain;                   // Part of a key chain
    };
    
    std::vector<KeyBinding> bindings;
    std::map<std::string, std::function<void(const std::vector<std::string>&)> > actions;
    
    // Static event handlers
    static void handle_key_event(struct wl_listener* listener, void* data);
    static void handle_modifiers_event(struct wl_listener* listener, void* data);
    static void handle_destroy_event(struct wl_listener* listener, void* data);
    
    // Helper methods
    KeyContext get_current_context() const;
    bool matches_binding(const KeyBinding& binding, uint32_t keysym, uint32_t modifiers) const;
    void execute_action(const std::string& action, const std::vector<std::string>& args);
    void reset_chain();
    bool process_chain(uint32_t keysym, uint32_t modifiers);
    
    // Action implementations
    void action_close_window(const std::vector<std::string>& args);
    void action_workspace(const std::vector<std::string>& args);
    void action_send_to_workspace(const std::vector<std::string>& args);
    void action_maximize(const std::vector<std::string>& args);
    void action_minimize(const std::vector<std::string>& args);
    void action_exec(const std::vector<std::string>& args);
    void action_focus_next(const std::vector<std::string>& args);
    void action_focus_prev(const std::vector<std::string>& args);
    
    // Register all available actions
    void register_actions();
    
    // Parse modifier string (e.g., "Alt+Shift")
    uint32_t parse_modifiers(const std::string& mod_string) const;
    
    // Parse key string (e.g., "q", "Return", "F1")
    uint32_t parse_key(const std::string& key_string) const;
};

// Implementation

FluxboxKeyboard::FluxboxKeyboard(FluxboxServer* server, struct wlr_input_device* device)
    : server(server)
    , device(device)
    , keyboard(wlr_keyboard_from_input_device(device))
    , xkb_context(nullptr)
    , current_modifiers(0)
    , chain_active(false) {
    
    // Initialize listeners
    key.notify = handle_key_event;
    modifiers.notify = handle_modifiers_event;
    destroy.notify = handle_destroy_event;
    
    // Register default actions
    register_actions();
}

FluxboxKeyboard::~FluxboxKeyboard() {
    // Remove listeners
    wl_list_remove(&key.link);
    wl_list_remove(&modifiers.link);
    wl_list_remove(&destroy.link);
    
    if (xkb_context) {
        xkb_context_unref(xkb_context);
    }
}

bool FluxboxKeyboard::initialize() {
    // Create XKB context
    xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_context) {
        wlr_log(WLR_ERROR, "Failed to create XKB context");
        return false;
    }
    
    // Configure keyboard with default XKB rules
    struct xkb_rule_names rules = {
        .rules = nullptr,
        .model = nullptr,
        .layout = nullptr,
        .variant = nullptr,
        .options = nullptr,
    };
    
    struct xkb_keymap* keymap = xkb_keymap_new_from_names(xkb_context, &rules,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    if (!keymap) {
        wlr_log(WLR_ERROR, "Failed to create XKB keymap");
        return false;
    }
    
    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    
    // Set repeat info
    wlr_keyboard_set_repeat_info(keyboard, 25, 600);
    
    // Add event listeners
    wl_signal_add(&keyboard->events.key, &key);
    wl_signal_add(&keyboard->events.modifiers, &modifiers);
    wl_signal_add(&device->events.destroy, &destroy);
    
    // Set keyboard for seat
    wlr_seat_set_keyboard(server->get_seat(), keyboard);
    
    wlr_log(WLR_INFO, "Keyboard initialized successfully");
    return true;
}

void FluxboxKeyboard::handle_key(struct wlr_keyboard_key_event* event) {
    // Get keysyms for this key
    uint32_t keycode = event->keycode + 8; // wlroots keycodes are offset by 8
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(keyboard->xkb_state, keycode, &syms);
    
    bool handled = false;
    
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            uint32_t keysym = syms[i];
            
            // Check for chain continuation
            if (chain_active) {
                if (process_chain(keysym, current_modifiers)) {
                    handled = true;
                    break;
                }
            }
            
            // Check regular bindings
            for (const auto& binding : bindings) {
                if (matches_binding(binding, keysym, current_modifiers)) {
                    if (binding.is_chain) {
                        // Start a new chain
                        current_chain.keys.clear();
                        current_chain.modifiers.clear();
                        current_chain.keys.push_back(keysym);
                        current_chain.modifiers.push_back(current_modifiers);
                        current_chain.last_press = std::chrono::steady_clock::now();
                        chain_active = true;
                    } else {
                        execute_action(binding.action, binding.args);
                        reset_chain();
                    }
                    handled = true;
                    break;
                }
            }
            
            if (handled) break;
        }
    }
    
    // If not handled, pass to focused client
    if (!handled) {
        wlr_seat_set_keyboard(server->get_seat(), keyboard);
        wlr_seat_keyboard_notify_key(server->get_seat(), event->time_msec,
            event->keycode, event->state);
    }
}

void FluxboxKeyboard::handle_modifiers() {
    // Update modifier state
    wlr_seat_set_keyboard(server->get_seat(), keyboard);
    wlr_seat_keyboard_notify_modifiers(server->get_seat(), &keyboard->modifiers);
    
    // Cache current modifiers
    current_modifiers = wlr_keyboard_get_modifiers(keyboard);
}

void FluxboxKeyboard::load_key_bindings(const FluxboxConfig& config) {
    bindings.clear();
    
    // Load bindings from config
    for (const auto& kb : config.get_key_bindings()) {
        KeyBinding binding;
        
        // Parse key combination (e.g., "Alt+Shift+q")
        std::istringstream iss(kb.key_combination);
        std::string part;
        std::vector<std::string> parts;
        
        while (std::getline(iss, part, '+')) {
            parts.push_back(part);
        }
        
        if (parts.empty()) continue;
        
        // Last part is the key, others are modifiers
        std::string key_str = parts.back();
        parts.pop_back();
        
        // Parse modifiers
        binding.modifiers = 0;
        for (const auto& mod : parts) {
            binding.modifiers |= parse_modifiers(mod);
        }
        
        // Parse key
        uint32_t keysym = parse_key(key_str);
        if (keysym != XKB_KEY_NoSymbol) {
            binding.keys.push_back(keysym);
        }
        
        binding.context = CONTEXT_ALL; // Default to all contexts
        binding.action = kb.action;
        binding.args = kb.args;
        binding.is_chain = false; // TODO: Detect chain bindings
        
        bindings.push_back(binding);
    }
    
    wlr_log(WLR_INFO, "Loaded %zu key bindings", bindings.size());
}

void FluxboxKeyboard::register_actions() {
    actions["close"] = [this](const auto& args) { action_close_window(args); };
    actions["workspace"] = [this](const auto& args) { action_workspace(args); };
    actions["sendtoworkspace"] = [this](const auto& args) { action_send_to_workspace(args); };
    actions["maximize"] = [this](const auto& args) { action_maximize(args); };
    actions["minimize"] = [this](const auto& args) { action_minimize(args); };
    actions["exec"] = [this](const auto& args) { action_exec(args); };
    actions["nextwindow"] = [this](const auto& args) { action_focus_next(args); };
    actions["prevwindow"] = [this](const auto& args) { action_focus_prev(args); };
}

// Action implementations

void FluxboxKeyboard::action_close_window(const std::vector<std::string>& args) {
    auto* surface = server->get_focused_surface();
    if (surface) {
        surface->close();
    }
}

void FluxboxKeyboard::action_workspace(const std::vector<std::string>& args) {
    if (args.empty()) return;
    
    const std::string& arg = args[0];
    if (arg == "next") {
        int current = server->get_current_workspace_index();
        server->switch_workspace((current + 1) % server->get_workspace_count());
    } else if (arg == "prev") {
        int current = server->get_current_workspace_index();
        int count = server->get_workspace_count();
        server->switch_workspace((current - 1 + count) % count);
    } else {
        try {
            int index = std::stoi(arg) - 1; // Convert to 0-based
            server->switch_workspace(index);
        } catch (...) {
            wlr_log(WLR_ERROR, "Invalid workspace number: %s", arg.c_str());
        }
    }
}

void FluxboxKeyboard::action_send_to_workspace(const std::vector<std::string>& args) {
    if (args.empty()) return;
    
    auto* surface = server->get_focused_surface();
    if (!surface) return;
    
    try {
        int index = std::stoi(args[0]) - 1;
        auto* workspace = server->get_workspace(index);
        if (workspace) {
            surface->move_to_workspace(workspace);
        }
    } catch (...) {
        wlr_log(WLR_ERROR, "Invalid workspace number: %s", args[0].c_str());
    }
}

void FluxboxKeyboard::action_maximize(const std::vector<std::string>& args) {
    auto* surface = server->get_focused_surface();
    if (surface) {
        if (surface->is_maximized()) {
            surface->unmaximize();
        } else {
            surface->maximize();
        }
    }
}

void FluxboxKeyboard::action_minimize(const std::vector<std::string>& args) {
    auto* surface = server->get_focused_surface();
    if (surface) {
        surface->minimize();
    }
}

void FluxboxKeyboard::action_exec(const std::vector<std::string>& args) {
    if (args.empty()) return;
    
    // Build command string
    std::string command;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) command += " ";
        command += args[i];
    }
    
    // Fork and exec
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        exit(1);
    }
}

void FluxboxKeyboard::action_focus_next(const std::vector<std::string>& args) {
    server->focus_next_surface();
}

void FluxboxKeyboard::action_focus_prev(const std::vector<std::string>& args) {
    server->focus_prev_surface();
}

// Helper methods

uint32_t FluxboxKeyboard::parse_modifiers(const std::string& mod_string) const {
    std::string mod = mod_string;
    std::transform(mod.begin(), mod.end(), mod.begin(), ::tolower);
    
    if (mod == "shift") return WLR_MODIFIER_SHIFT;
    if (mod == "ctrl" || mod == "control") return WLR_MODIFIER_CTRL;
    if (mod == "alt" || mod == "mod1") return WLR_MODIFIER_ALT;
    if (mod == "super" || mod == "mod4" || mod == "win") return WLR_MODIFIER_LOGO;
    if (mod == "caps" || mod == "capslock") return WLR_MODIFIER_CAPS;
    if (mod == "mod2") return WLR_MODIFIER_MOD2;
    if (mod == "mod3") return WLR_MODIFIER_MOD3;
    if (mod == "mod5") return WLR_MODIFIER_MOD5;
    
    return 0;
}

uint32_t FluxboxKeyboard::parse_key(const std::string& key_string) const {
    // First try to parse as a single character
    if (key_string.length() == 1) {
        char c = key_string[0];
        if (c >= 'A' && c <= 'Z') {
            return XKB_KEY_A + (c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            return XKB_KEY_a + (c - 'a');
        } else if (c >= '0' && c <= '9') {
            return XKB_KEY_0 + (c - '0');
        }
    }
    
    // Common special keys
    std::string key = key_string;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    
    if (key == "return" || key == "enter") return XKB_KEY_Return;
    if (key == "tab") return XKB_KEY_Tab;
    if (key == "space") return XKB_KEY_space;
    if (key == "escape" || key == "esc") return XKB_KEY_Escape;
    if (key == "backspace") return XKB_KEY_BackSpace;
    if (key == "delete" || key == "del") return XKB_KEY_Delete;
    if (key == "left") return XKB_KEY_Left;
    if (key == "right") return XKB_KEY_Right;
    if (key == "up") return XKB_KEY_Up;
    if (key == "down") return XKB_KEY_Down;
    if (key == "home") return XKB_KEY_Home;
    if (key == "end") return XKB_KEY_End;
    if (key == "pageup" || key == "page_up") return XKB_KEY_Page_Up;
    if (key == "pagedown" || key == "page_down") return XKB_KEY_Page_Down;
    
    // Function keys
    if (key.length() >= 2 && key[0] == 'f') {
        try {
            int num = std::stoi(key.substr(1));
            if (num >= 1 && num <= 24) {
                return XKB_KEY_F1 + (num - 1);
            }
        } catch (...) {}
    }
    
    // If not found, try xkb_keysym_from_name
    xkb_keysym_t keysym = xkb_keysym_from_name(key_string.c_str(), XKB_KEYSYM_NO_FLAGS);
    if (keysym != XKB_KEY_NoSymbol) {
        return keysym;
    }
    
    wlr_log(WLR_ERROR, "Unknown key: %s", key_string.c_str());
    return XKB_KEY_NoSymbol;
}

bool FluxboxKeyboard::matches_binding(const KeyBinding& binding, uint32_t keysym, uint32_t modifiers) const {
    // Check modifiers
    if (binding.modifiers != modifiers) {
        return false;
    }
    
    // Check key
    if (binding.keys.empty() || binding.keys[0] != keysym) {
        return false;
    }
    
    // Check context
    KeyContext context = get_current_context();
    if (!(binding.context & context)) {
        return false;
    }
    
    return true;
}

void FluxboxKeyboard::execute_action(const std::string& action, const std::vector<std::string>& args) {
    auto it = actions.find(action);
    if (it != actions.end()) {
        it->second(args);
    } else {
        wlr_log(WLR_ERROR, "Unknown action: %s", action.c_str());
    }
}

KeyContext FluxboxKeyboard::get_current_context() const {
    // For now, return WINDOW if there's a focused surface, DESKTOP otherwise
    if (server->get_focused_surface()) {
        return CONTEXT_WINDOW;
    }
    return CONTEXT_DESKTOP;
}

void FluxboxKeyboard::reset_chain() {
    chain_active = false;
    current_chain.keys.clear();
    current_chain.modifiers.clear();
}

bool FluxboxKeyboard::process_chain(uint32_t keysym, uint32_t modifiers) {
    // Check if chain has timed out
    auto now = std::chrono::steady_clock::now();
    if (now - current_chain.last_press > KeyChain::CHAIN_TIMEOUT) {
        reset_chain();
        return false;
    }
    
    // Add to chain
    current_chain.keys.push_back(keysym);
    current_chain.modifiers.push_back(modifiers);
    current_chain.last_press = now;
    
    // TODO: Check if this completes a chain binding
    // For now, just reset after 2 keys
    if (current_chain.keys.size() >= 2) {
        reset_chain();
    }
    
    return true;
}

// Static event handlers

void FluxboxKeyboard::handle_key_event(struct wl_listener* listener, void* data) {
    FluxboxKeyboard* keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event* event = static_cast<struct wlr_keyboard_key_event*>(data);
    keyboard->handle_key(event);
}

void FluxboxKeyboard::handle_modifiers_event(struct wl_listener* listener, void* data) {
    FluxboxKeyboard* keyboard = wl_container_of(listener, keyboard, modifiers);
    keyboard->handle_modifiers();
}

void FluxboxKeyboard::handle_destroy_event(struct wl_listener* listener, void* data) {
    FluxboxKeyboard* keyboard = wl_container_of(listener, keyboard, destroy);
    wlr_log(WLR_INFO, "Keyboard device destroyed");
    delete keyboard;
}

// Global keyboard manager for the server
static std::vector<std::unique_ptr<FluxboxKeyboard>> keyboards;

// Function to add a keyboard to the server
extern "C" void fluxbox_add_keyboard(FluxboxServer* server, struct wlr_input_device* device) {
    auto keyboard = std::make_unique<FluxboxKeyboard>(server, device);
    if (keyboard->initialize()) {
        // Load key bindings from server config
        if (server->get_config()) {
            keyboard->load_key_bindings(*server->get_config());
        }
        keyboards.push_back(std::move(keyboard));
        wlr_log(WLR_INFO, "Added keyboard device");
    } else {
        wlr_log(WLR_ERROR, "Failed to initialize keyboard");
    }
}

// Function to reload key bindings for all keyboards
extern "C" void fluxbox_reload_key_bindings(FluxboxServer* server) {
    if (!server->get_config()) return;
    
    for (auto& keyboard : keyboards) {
        keyboard->load_key_bindings(*server->get_config());
    }
    wlr_log(WLR_INFO, "Reloaded key bindings for %zu keyboards", keyboards.size());
}