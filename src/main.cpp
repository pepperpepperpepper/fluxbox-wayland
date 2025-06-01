#include "server.hpp"

extern "C" {
#include <wlr/util/log.h>
#include <wayland-server-core.h>
}

#include <iostream>
#include <csignal>

static FluxboxServer* server_instance = nullptr;
static volatile bool should_terminate = false;

void signal_handler(int sig) {
    std::cout << "Received signal " << sig << ", initiating graceful shutdown..." << std::endl;
    should_terminate = true;
    
    if (server_instance) {
        // Signal the event loop to terminate
        wl_display_terminate(server_instance->get_display());
    }
}

int main(int argc, char* argv[]) {
    wlr_log_init(WLR_DEBUG, nullptr);
    
    std::cout << "Starting Fluxbox for Wayland..." << std::endl;
    
    FluxboxServer server;
    server_instance = &server;
    
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (!server.startup()) {
        std::cerr << "Failed to start Fluxbox server" << std::endl;
        return 1;
    }
    
    std::cout << "Fluxbox server started successfully" << std::endl;
    
    server.run();
    
    std::cout << "Fluxbox server shutting down..." << std::endl;
    
    // Perform cleanup
    server_instance = nullptr;
    
    return 0;
}