#pragma once

#include <string>
#include <map>
#include <vector>

struct KeyBinding {
    std::string key_combination;  // e.g., "Alt+1", "Ctrl+Shift+q"
    std::string action;          // e.g., "workspace 1", "close window"
    std::vector<std::string> args; // Additional arguments
};

class FluxboxConfig {
public:
    FluxboxConfig();
    ~FluxboxConfig();

    // Configuration loading
    bool load_config(const std::string& config_path = "");
    bool load_default_config();
    void save_config(const std::string& config_path = "");

    // Workspace settings
    int get_num_workspaces() const { return num_workspaces; }
    void set_num_workspaces(int count) { num_workspaces = count; }
    
    const std::vector<std::string>& get_workspace_names() const { return workspace_names; }
    void set_workspace_name(int index, const std::string& name);

    // Key bindings
    const std::vector<KeyBinding>& get_key_bindings() const { return key_bindings; }
    void add_key_binding(const std::string& keys, const std::string& action, const std::vector<std::string>& args = {});
    void clear_key_bindings() { key_bindings.clear(); }

    // Window management settings
    bool get_focus_follows_mouse() const { return focus_follows_mouse; }
    void set_focus_follows_mouse(bool value) { focus_follows_mouse = value; }
    
    bool get_auto_raise() const { return auto_raise; }
    void set_auto_raise(bool value) { auto_raise = value; }

    // Appearance settings
    const std::string& get_theme() const { return theme; }
    void set_theme(const std::string& theme_name) { theme = theme_name; }

    // Generic settings access
    std::string get_setting(const std::string& key, const std::string& default_value = "") const;
    void set_setting(const std::string& key, const std::string& value);

private:
    // Core settings
    int num_workspaces;
    std::vector<std::string> workspace_names;
    std::vector<KeyBinding> key_bindings;
    
    // Window management
    bool focus_follows_mouse;
    bool auto_raise;
    
    // Appearance
    std::string theme;
    
    // Generic settings storage
    std::map<std::string, std::string> settings;
    
    // Default configuration path
    std::string get_default_config_path() const;
    std::string config_file_path;
    
    // Config file parsing
    void parse_config_line(const std::string& line);
    void setup_default_key_bindings();
    void setup_default_workspaces();
};