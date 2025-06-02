#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/util/log.h>
#include <wayland-server-core.h>

static struct wl_display* display = NULL;
static bool running = true;

struct Server {
    struct wl_display* display;
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    struct wlr_scene* scene;
    struct wlr_scene_output_layout* scene_layout;
    struct wlr_output_layout* output_layout;
    struct wlr_xdg_shell* xdg_shell;
    struct wlr_seat* seat;
    struct wlr_screencopy_manager_v1* screencopy_manager;
    
    struct wl_listener new_output;
    struct wl_listener new_xdg_toplevel;
};

struct Output {
    struct Server* server;
    struct wlr_output* wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct View {
    struct Server* server;
    struct wlr_xdg_toplevel* xdg_toplevel;
    struct wlr_scene_tree* scene_tree;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static void output_frame(struct wl_listener* listener, void* data) {
    struct Output* output = wl_container_of(listener, output, frame);
    struct wlr_scene* scene = output->server->scene;
    
    struct wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);
    wlr_scene_output_commit(scene_output, NULL);
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener* listener, void* data) {
    struct Output* output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state* event = (struct wlr_output_event_request_state*)data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener* listener, void* data) {
    struct Output* output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    free(output);
}

static void new_output(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, new_output);
    struct wlr_output* wlr_output = (struct wlr_output*)data;
    
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    
    struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }
    
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);
    
    struct Output* output = (struct Output*)calloc(1, sizeof(struct Output));
    output->server = server;
    output->wlr_output = wlr_output;
    
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    
    struct wlr_scene_output* scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    
    wlr_log(WLR_INFO, "Output '%s' added", wlr_output->name);
}

static void xdg_toplevel_map(struct wl_listener* listener, void* data) {
    struct View* view = wl_container_of(listener, view, map);
    
    wlr_scene_node_set_enabled(&view->scene_tree->node, true);
    
    // Focus the surface
    struct wlr_surface* surface = view->xdg_toplevel->base->surface;
    struct wlr_seat* seat = view->server->seat;
    wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
}

static void xdg_toplevel_unmap(struct wl_listener* listener, void* data) {
    struct View* view = wl_container_of(listener, view, unmap);
    wlr_scene_node_set_enabled(&view->scene_tree->node, false);
}

static void xdg_toplevel_commit(struct wl_listener* listener, void* data) {
    struct View* view = wl_container_of(listener, view, commit);
    
    if (view->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
    }
}

static void xdg_toplevel_destroy(struct wl_listener* listener, void* data) {
    struct View* view = wl_container_of(listener, view, destroy);
    
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    
    free(view);
}

static void new_xdg_toplevel(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel* xdg_toplevel = (struct wlr_xdg_toplevel*)data;
    
    struct View* view = (struct View*)calloc(1, sizeof(struct View));
    view->server = server;
    view->xdg_toplevel = xdg_toplevel;
    view->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, xdg_toplevel->base);
    
    view->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &view->map);
    
    view->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &view->unmap);
    
    view->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &view->commit);
    
    view->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->base->events.destroy, &view->destroy);
    
    wlr_log(WLR_INFO, "New XDG toplevel");
}

static void signal_handler(int sig) {
    running = false;
    if (display) {
        wl_display_terminate(display);
    }
}

int main(int argc, char* argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);
    
    struct Server server = {0};
    
    server.display = wl_display_create();
    display = server.display;
    
    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.display), NULL);
    server.renderer = wlr_renderer_autocreate(server.backend);
    wlr_renderer_init_wl_display(server.renderer, server.display);
    
    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    
    server.scene = wlr_scene_create();
    server.output_layout = wlr_output_layout_create(server.display);
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
    
    wlr_compositor_create(server.display, 5, server.renderer);
    wlr_subcompositor_create(server.display);
    wlr_data_device_manager_create(server.display);
    
    server.xdg_shell = wlr_xdg_shell_create(server.display, 3);
    server.new_xdg_toplevel.notify = new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
    
    server.seat = wlr_seat_create(server.display, "seat0");
    
    // Add screencopy support
    server.screencopy_manager = wlr_screencopy_manager_v1_create(server.display);
    
    server.new_output.notify = new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);
    
    const char* socket = wl_display_add_socket_auto(server.display);
    if (!socket) {
        wlr_backend_destroy(server.backend);
        return 1;
    }
    
    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return 1;
    }
    
    setenv("WAYLAND_DISPLAY", socket, true);
    wlr_log(WLR_INFO, "Running Fluxbox Wayland on WAYLAND_DISPLAY=%s", socket);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    wl_display_run(server.display);
    
    wl_display_destroy_clients(server.display);
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_output_layout_destroy(server.output_layout);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.display);
    
    return 0;
}