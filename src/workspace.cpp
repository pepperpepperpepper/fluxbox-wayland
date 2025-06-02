#include "workspace.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "scene_wrapper.h"

#include <algorithm>

FluxboxWorkspace::FluxboxWorkspace(FluxboxServer* server, int index, const std::string& name)
    : server(server)
    , index(index)
    , name(name.empty() ? "Workspace " + std::to_string(index + 1) : name)
    , active(false)
    , scene_tree(nullptr) {
    
    scene_tree = fluxbox_scene_tree_create_subsurface(fluxbox_scene_tree_create(server->get_scene()));
    fluxbox_scene_node_set_enabled(scene_tree, false);
}

FluxboxWorkspace::~FluxboxWorkspace() {
    if (scene_tree) {
        fluxbox_scene_node_destroy(scene_tree);
    }
}

void FluxboxWorkspace::activate() {
    if (active) {
        return;
    }
    
    active = true;
    fluxbox_scene_node_set_enabled(scene_tree, true);
}

void FluxboxWorkspace::deactivate() {
    if (!active) {
        return;
    }
    
    active = false;
    fluxbox_scene_node_set_enabled(scene_tree, false);
}

void FluxboxWorkspace::add_surface(FluxboxSurface* surface) {
    if (!surface) {
        return;
    }
    
    auto it = std::find(surfaces.begin(), surfaces.end(), surface);
    if (it == surfaces.end()) {
        surfaces.push_back(surface);
        surface->move_to_workspace(this);
    }
}

void FluxboxWorkspace::remove_surface(FluxboxSurface* surface) {
    if (!surface) {
        return;
    }
    
    auto it = std::find(surfaces.begin(), surfaces.end(), surface);
    if (it != surfaces.end()) {
        surfaces.erase(it);
    }
}