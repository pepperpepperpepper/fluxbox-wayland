#pragma once

#include <string>
#include <unordered_map>
#include <array>

class FluxboxTheme {
public:
    FluxboxTheme();
    ~FluxboxTheme() = default;
    
    // Load theme from file
    bool load(const std::string& filename);
    bool load_from_string(const std::string& theme_data);
    
    // Window decorations
    struct WindowStyle {
        struct {
            std::array<float, 4> color;
            std::array<float, 4> color_to;  // For gradients
            std::string texture;
        } title_focus, title_unfocus;
        
        struct {
            std::array<float, 4> color;
            int width;
        } border_focus, border_unfocus;
        
        struct {
            std::array<float, 4> color;
        } handle_focus, handle_unfocus;
        
        struct {
            std::array<float, 4> color;
        } grip_focus, grip_unfocus;
        
        struct {
            std::string justify;
            std::array<float, 4> color;
            std::string font;
        } font_focus, font_unfocus;
        
        int title_height;
        int handle_width;
        int border_width;
        int bevel_width;
    };
    
    // Button styles
    struct ButtonStyle {
        std::array<float, 4> color;
        std::array<float, 4> piccolor;
        std::string texture;
    };
    
    // Get parsed styles
    const WindowStyle& get_window_style() const { return window_style; }
    const ButtonStyle& get_button_style(const std::string& name) const;
    
    // Get specific colors for decorations
    std::array<float, 4> get_titlebar_color(bool focused) const;
    std::array<float, 4> get_border_color(bool focused) const;
    std::array<float, 4> get_text_color(bool focused) const;
    int get_titlebar_height() const;
    int get_border_width() const;
    
private:
    WindowStyle window_style;
    std::unordered_map<std::string, ButtonStyle> button_styles;
    std::unordered_map<std::string, std::string> raw_values;
    
    // Parse helpers
    void parse_line(const std::string& line);
    std::array<float, 4> parse_color(const std::string& color_str);
    std::string get_value(const std::string& key) const;
    
    // Set default values (similar to classic Fluxbox themes)
    void set_defaults();
    
    // Apply parsed values to structures
    void apply_values();
};