#include "config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

FluxboxConfig::FluxboxConfig()
    : num_workspaces(4)
    , focus_follows_mouse(false)
    , auto_raise(false)
    , theme("default") {
    
    setup_default_workspaces();
    setup_default_key_bindings();
}

FluxboxConfig::~FluxboxConfig() {
}

std::string FluxboxConfig::get_default_config_path() const {
    const char* home = getenv("HOME");
    if (!home) {
        return "/tmp/fluxbox-wayland.conf";
    }
    
    std::string config_dir = std::string(home) + "/.config/fluxbox-wayland";
    
    // Create config directory if it doesn't exist
    std::filesystem::create_directories(config_dir);
    
    return config_dir + "/fluxbox-wayland.conf";
}

bool FluxboxConfig::load_config(const std::string& config_path) {
    if (config_path.empty()) {
        config_file_path = get_default_config_path();
    } else {
        config_file_path = config_path;
    }
    
    std::ifstream file(config_file_path);
    if (!file.is_open()) {
        std::cout << "Config file not found: " << config_file_path << std::endl;
        std::cout << "Using default configuration" << std::endl;
        return load_default_config();
    }
    
    std::cout << "Loading config from: " << config_file_path << std::endl;
    
    // Clear existing settings
    key_bindings.clear();
    workspace_names.clear();
    settings.clear();
    
    std::string line;
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        try {
            parse_config_line(line);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing config line " << line_number << ": " << line << std::endl;
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    
    file.close();
    
    // Ensure we have default key bindings if none were loaded
    if (key_bindings.empty()) {
        setup_default_key_bindings();
    }
    
    // Ensure we have workspace names
    if (workspace_names.empty()) {
        setup_default_workspaces();
    }
    
    std::cout << "Config loaded: " << num_workspaces << " workspaces, " 
              << key_bindings.size() << " key bindings" << std::endl;
    
    return true;
}

bool FluxboxConfig::load_default_config() {
    // Reset to defaults
    num_workspaces = 4;
    focus_follows_mouse = false;
    auto_raise = false;
    theme = "default";
    
    setup_default_workspaces();
    setup_default_key_bindings();
    
    std::cout << "Loaded default configuration" << std::endl;
    return true;
}

void FluxboxConfig::save_config(const std::string& config_path) {
    std::string path = config_path.empty() ? config_file_path : config_path;
    if (path.empty()) {
        path = get_default_config_path();
    }
    
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file for writing: " << path << std::endl;
        return;
    }
    
    file << "# Fluxbox for Wayland Configuration\n";
    file << "# Generated configuration file\n\n";
    
    // Workspace settings
    file << "# Workspace Configuration\n";
    file << "session.screen0.workspaces: " << num_workspaces << "\n";
    
    for (size_t i = 0; i < workspace_names.size(); ++i) {
        file << "session.screen0.workspaceNames.workspace" << i << ": " << workspace_names[i] << "\n";
    }
    file << "\n";
    
    // Window management
    file << "# Window Management\n";
    file << "session.screen0.focusModel: " << (focus_follows_mouse ? "MouseFocus" : "ClickFocus") << "\n";
    file << "session.screen0.autoRaise: " << (auto_raise ? "true" : "false") << "\n";
    file << "\n";
    
    // Appearance
    file << "# Appearance\n";
    file << "session.styleFile: " << theme << "\n";
    file << "\n";
    
    // Key bindings
    file << "# Key Bindings\n";
    for (const auto& binding : key_bindings) {
        file << "key " << binding.key_combination << " :";
        file << " " << binding.action;
        for (const auto& arg : binding.args) {
            file << " " << arg;
        }
        file << "\n";
    }
    file << "\n";
    
    // Generic settings
    if (!settings.empty()) {
        file << "# Additional Settings\n";
        for (const auto& [key, value] : settings) {
            file << key << ": " << value << "\n";
        }
    }
    
    file.close();
    std::cout << "Configuration saved to: " << path << std::endl;
}

void FluxboxConfig::parse_config_line(const std::string& line) {
    std::istringstream iss(line);
    std::string first_word;
    iss >> first_word;
    
    if (first_word == "key") {
        // Parse key binding: key Alt+1 : workspace 1
        std::string key_combo, colon, action;
        iss >> key_combo >> colon >> action;
        
        std::vector<std::string> args;
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }
        
        add_key_binding(key_combo, action, args);
        
    } else if (first_word.find("session.screen0.workspaces:") == 0 || 
               first_word.find("session.screen0.workspaces") == 0) {
        // Parse workspace count
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string value = line.substr(colon_pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            num_workspaces = std::stoi(value);
        }
        
    } else if (first_word.find("session.screen0.workspaceNames") == 0) {
        // Parse workspace name: session.screen0.workspaceNames.workspace0: Main
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string value = line.substr(colon_pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            
            // Extract workspace index
            size_t workspace_pos = first_word.find("workspace");
            if (workspace_pos != std::string::npos) {
                std::string index_str = first_word.substr(workspace_pos + 9);
                size_t colon_in_key = index_str.find(':');
                if (colon_in_key != std::string::npos) {
                    index_str = index_str.substr(0, colon_in_key);
                }
                
                try {
                    int index = std::stoi(index_str);
                    set_workspace_name(index, value);
                } catch (...) {
                    // Ignore invalid workspace index
                }
            }
        }
        
    } else if (first_word.find("session.screen0.focusModel:") == 0) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string value = line.substr(colon_pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            focus_follows_mouse = (value == "MouseFocus");
        }
        
    } else if (first_word.find("session.screen0.autoRaise:") == 0) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string value = line.substr(colon_pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            auto_raise = (value == "true");
        }
        
    } else if (first_word.find("session.styleFile:") == 0) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string value = line.substr(colon_pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            theme = value;
        }
        
    } else {
        // Generic setting: key: value
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            set_setting(key, value);
        }
    }
}

void FluxboxConfig::setup_default_key_bindings() {
    key_bindings.clear();
    
    // Workspace switching
    add_key_binding("Alt+1", "workspace", {"1"});
    add_key_binding("Alt+2", "workspace", {"2"});
    add_key_binding("Alt+3", "workspace", {"3"});
    add_key_binding("Alt+4", "workspace", {"4"});
    
    // Workspace navigation
    add_key_binding("Alt+Left", "workspace", {"prev"});
    add_key_binding("Alt+Right", "workspace", {"next"});
    
    // Window management
    add_key_binding("Alt+q", "close", {});
    add_key_binding("Alt+F4", "close", {});
    
    // Moving windows between workspaces
    add_key_binding("Alt+Shift+1", "sendtoworkspace", {"1"});
    add_key_binding("Alt+Shift+2", "sendtoworkspace", {"2"});
    add_key_binding("Alt+Shift+3", "sendtoworkspace", {"3"});
    add_key_binding("Alt+Shift+4", "sendtoworkspace", {"4"});
    
    // Application launching (examples)
    add_key_binding("Alt+Return", "exec", {"weston-terminal"});
    add_key_binding("Alt+r", "exec", {"foot"});
}

void FluxboxConfig::setup_default_workspaces() {
    workspace_names.clear();
    workspace_names.resize(num_workspaces);
    
    for (int i = 0; i < num_workspaces; ++i) {
        workspace_names[i] = "Workspace " + std::to_string(i + 1);
    }
}

void FluxboxConfig::set_workspace_name(int index, const std::string& name) {
    if (index >= 0 && index < static_cast<int>(workspace_names.size())) {
        workspace_names[index] = name;
    } else if (index >= 0) {
        // Extend the vector if needed
        workspace_names.resize(index + 1, "Workspace");
        workspace_names[index] = name;
    }
}

void FluxboxConfig::add_key_binding(const std::string& keys, const std::string& action, const std::vector<std::string>& args) {
    KeyBinding binding;
    binding.key_combination = keys;
    binding.action = action;
    binding.args = args;
    key_bindings.push_back(binding);
}

std::string FluxboxConfig::get_setting(const std::string& key, const std::string& default_value) const {
    auto it = settings.find(key);
    return (it != settings.end()) ? it->second : default_value;
}

void FluxboxConfig::set_setting(const std::string& key, const std::string& value) {
    settings[key] = value;
}