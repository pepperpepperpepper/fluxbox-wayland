#pragma once

#include <vector>
#include <string>

// Forward declarations
struct wlr_scene_tree;

class FluxboxSurface;
class FluxboxServer;

class FluxboxWorkspace {
public:
    FluxboxWorkspace(FluxboxServer* server, int index, const std::string& name = "");
    ~FluxboxWorkspace();

    // Workspace management
    void activate();
    void deactivate();
    bool is_active() const { return active; }
    
    // Surface management
    void add_surface(FluxboxSurface* surface);
    void remove_surface(FluxboxSurface* surface);
    const std::vector<FluxboxSurface*>& get_surfaces() const { return surfaces; }
    
    // Properties
    int get_index() const { return index; }
    const std::string& get_name() const { return name; }
    void set_name(const std::string& new_name) { name = new_name; }
    
    // Scene tree for this workspace
    struct wlr_scene_tree* get_scene_tree() const { return scene_tree; }

private:
    FluxboxServer* server;
    int index;
    std::string name;
    bool active;
    
    struct wlr_scene_tree* scene_tree;
    std::vector<FluxboxSurface*> surfaces;
};