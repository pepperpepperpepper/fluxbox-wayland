/*
 * Simple Wayland Screenshot Tool for Fluxbox Wayland
 * Uses zwlr_screencopy_manager_v1 protocol directly
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <wayland-client.h>

#include "protocols/wlr-screencopy-unstable-v1-client-protocol.h"
#include "protocols/xdg-output-unstable-v1-client-protocol.h"

struct screencopy_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct zwlr_screencopy_manager_v1 *screencopy_manager;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    
    struct wl_output *output;
    struct zxdg_output_v1 *xdg_output;
    
    uint32_t output_width, output_height;
    uint32_t format;
    int done;
    int success;
    
    struct wl_buffer *buffer;
    void *buffer_data;
    size_t buffer_size;
};

static void shm_format(void *data, struct wl_shm *shm, uint32_t format) {
    // We'll use whatever format the compositor gives us
}

static const struct wl_shm_listener shm_listener = {
    .format = shm_format,
};

static void screencopy_buffer(void *data,
                             struct zwlr_screencopy_frame_v1 *frame,
                             uint32_t format,
                             uint32_t width,
                             uint32_t height,
                             uint32_t stride) {
    struct screencopy_state *state = data;
    
    printf("📏 Screen dimensions: %ux%u, stride: %u\n", width, height, stride);
    
    state->output_width = width;
    state->output_height = height;
    state->format = format;
    
    // Calculate buffer size
    state->buffer_size = stride * height;
    
    // Create shared memory file
    char name[] = "/tmp/wl_shm-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) {
        fprintf(stderr, "❌ Failed to create temp file\n");
        state->success = 0;
        state->done = 1;
        return;
    }
    unlink(name);
    
    if (ftruncate(fd, state->buffer_size) < 0) {
        fprintf(stderr, "❌ Failed to resize temp file\n");
        close(fd);
        state->success = 0;
        state->done = 1;
        return;
    }
    
    // Map the memory
    state->buffer_data = mmap(NULL, state->buffer_size, 
                             PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (state->buffer_data == MAP_FAILED) {
        fprintf(stderr, "❌ Failed to mmap buffer\n");
        close(fd);
        state->success = 0;
        state->done = 1;
        return;
    }
    
    // Create wl_buffer
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, state->buffer_size);
    state->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);
    
    // Start the copy
    zwlr_screencopy_frame_v1_copy(frame, state->buffer);
    
    printf("✅ Buffer created and copy started\n");
}

static void screencopy_flags(void *data,
                            struct zwlr_screencopy_frame_v1 *frame,
                            uint32_t flags) {
    printf("📋 Screencopy flags: %u\n", flags);
}

static void screencopy_ready(void *data,
                            struct zwlr_screencopy_frame_v1 *frame,
                            uint32_t tv_sec_hi,
                            uint32_t tv_sec_lo,
                            uint32_t tv_nsec) {
    struct screencopy_state *state = data;
    printf("🎉 Screencopy ready!\n");
    state->success = 1;
    state->done = 1;
}

static void screencopy_failed(void *data,
                             struct zwlr_screencopy_frame_v1 *frame) {
    struct screencopy_state *state = data;
    printf("❌ Screencopy failed\n");
    state->success = 0;
    state->done = 1;
}

static void screencopy_damage(void *data,
                              struct zwlr_screencopy_frame_v1 *frame,
                              uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height) {
    printf("📋 Damage region: %ux%u at (%u,%u)\n", width, height, x, y);
}

static void screencopy_linux_dmabuf(void *data,
                                   struct zwlr_screencopy_frame_v1 *frame,
                                   uint32_t format,
                                   uint32_t width, uint32_t height) {
    printf("📋 Linux dmabuf: %ux%u format %u\n", width, height, format);
}

static void screencopy_buffer_done(void *data,
                                  struct zwlr_screencopy_frame_v1 *frame) {
    printf("📋 Buffer done\n");
}

static const struct zwlr_screencopy_frame_v1_listener screencopy_listener = {
    .buffer = screencopy_buffer,
    .flags = screencopy_flags,
    .ready = screencopy_ready,
    .failed = screencopy_failed,
    .damage = screencopy_damage,
    .linux_dmabuf = screencopy_linux_dmabuf,
    .buffer_done = screencopy_buffer_done,
};

static void registry_global(void *data,
                           struct wl_registry *registry,
                           uint32_t name,
                           const char *interface,
                           uint32_t version) {
    struct screencopy_state *state = data;
    
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        wl_shm_add_listener(state->shm, &shm_listener, state);
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        state->screencopy_manager = wl_registry_bind(registry, name,
                                                    &zwlr_screencopy_manager_v1_interface, 
                                                    version);
        printf("✅ Found screencopy manager\n");
    } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        state->xdg_output_manager = wl_registry_bind(registry, name,
                                                    &zxdg_output_manager_v1_interface,
                                                    version);
        printf("✅ Found xdg output manager\n");
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        state->output = wl_registry_bind(registry, name, &wl_output_interface, version);
        printf("✅ Found output\n");
    }
}

static void registry_global_remove(void *data,
                                  struct wl_registry *registry,
                                  uint32_t name) {
    // Don't care about removed globals
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int save_screenshot(struct screencopy_state *state, const char *filename) {
    if (!state->buffer_data || !state->success) {
        return -1;
    }
    
    // For now, just save raw RGBA data
    // TODO: Convert to PNG
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen");
        return -1;
    }
    
    size_t written = fwrite(state->buffer_data, 1, state->buffer_size, f);
    fclose(f);
    
    if (written != state->buffer_size) {
        fprintf(stderr, "❌ Incomplete write\n");
        return -1;
    }
    
    printf("💾 Saved raw screenshot data: %s (%zu bytes)\n", filename, written);
    return 0;
}

int main(int argc, char *argv[]) {
    printf("📸 Fluxbox Wayland Screenshot Tool\n");
    printf("==================================\n");
    
    const char *filename = argc > 1 ? argv[1] : "/tmp/fluxbox_wayland_screenshot.raw";
    
    struct screencopy_state state = {0};
    
    // Connect to Wayland display
    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "❌ Failed to connect to Wayland display\n");
        return 1;
    }
    
    printf("🌊 Connected to Wayland display\n");
    
    // Get registry
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    
    // Roundtrip to get globals
    wl_display_roundtrip(state.display);
    
    if (!state.screencopy_manager) {
        fprintf(stderr, "❌ Screencopy protocol not available\n");
        return 1;
    }
    
    if (!state.output) {
        fprintf(stderr, "❌ No output available\n");
        return 1;
    }
    
    printf("🎯 Starting screencopy...\n");
    
    // Create screencopy frame
    struct zwlr_screencopy_frame_v1 *frame = 
        zwlr_screencopy_manager_v1_capture_output(state.screencopy_manager, 0, state.output);
    
    zwlr_screencopy_frame_v1_add_listener(frame, &screencopy_listener, &state);
    
    // Wait for completion
    while (!state.done && wl_display_dispatch(state.display) != -1) {
        // Keep processing events
    }
    
    zwlr_screencopy_frame_v1_destroy(frame);
    
    if (state.success) {
        printf("🎉 Screenshot completed successfully!\n");
        if (save_screenshot(&state, filename) == 0) {
            printf("✅ Screenshot saved: %s\n", filename);
        }
    } else {
        printf("❌ Screenshot failed\n");
    }
    
    // Cleanup
    if (state.buffer_data) {
        munmap(state.buffer_data, state.buffer_size);
    }
    if (state.buffer) {
        wl_buffer_destroy(state.buffer);
    }
    
    wl_display_disconnect(state.display);
    
    return state.success ? 0 : 1;
}