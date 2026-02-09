#include "wayland/fbwl_output.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "wayland/fbwl_util.h"

static void output_present(struct wl_listener *listener, void *data) {
    struct fbwl_output *output = wl_container_of(listener, output, present);
    if (output == NULL || output->wlr_output == NULL || data == NULL) {
        return;
    }
    const struct wlr_output_event_present *event = data;

    // Some X11 environments appear to emit presentation events with commit_seq lagging
    // behind the output's real commit_seq. Treat commit_seq as monotonic here to avoid
    // regressing and re-emitting synthetic present events for the same commit.
    if (!output->have_present_commit_seq || event->commit_seq >= output->last_present_commit_seq) {
        output->have_present_commit_seq = true;
        output->last_present_commit_seq = event->commit_seq;
    }
}

static int output_refresh_nsec(const struct wlr_output *wlr_output) {
    if (wlr_output == NULL || wlr_output->refresh <= 0) {
        return 0;
    }

    const uint64_t refresh_mhz = (uint64_t)wlr_output->refresh;
    const uint64_t refresh_nsec = 1000000000000ull / refresh_mhz;
    if (refresh_nsec > (uint64_t)INT32_MAX) {
        return 0;
    }
    return (int)refresh_nsec;
}

struct fbwl_output *fbwl_output_find(struct wl_list *outputs, struct wlr_output *wlr_output) {
    if (outputs == NULL || wlr_output == NULL) {
        return NULL;
    }

    struct fbwl_output *out;
    wl_list_for_each(out, outputs, link) {
        if (out->wlr_output == wlr_output) {
            return out;
        }
    }
    return NULL;
}

size_t fbwl_output_count(const struct wl_list *outputs) {
    if (outputs == NULL) {
        return 0;
    }
    size_t n = 0;
    struct fbwl_output *out;
    wl_list_for_each(out, outputs, link) {
        n++;
    }
    return n;
}

static void output_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_output *output = wl_container_of(listener, output, frame);
    if (output == NULL || output->scene == NULL || output->wlr_output == NULL) {
        return;
    }
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(output->scene, output->wlr_output);
    if (scene_output == NULL) {
        return;
    }

    uint32_t commit_seq_before = output->wlr_output->commit_seq;
    bool committed = wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (committed && output->wlr_output->commit_seq != commit_seq_before &&
            output->wlr_output->name != NULL && strncmp(output->wlr_output->name, "X11", 3) == 0) {
        if (!output->have_present_commit_seq || output->last_present_commit_seq < output->wlr_output->commit_seq) {
            struct wlr_output_event_present present = {0};
            present.output = output->wlr_output;
            present.commit_seq = output->wlr_output->commit_seq;
            present.presented = true;
            present.when = now;
            present.seq = 0;
            present.refresh = output_refresh_nsec(output->wlr_output);
            present.flags = 0;

            output->synth_present_in_progress = true;
            wl_signal_emit_mutable(&output->wlr_output->events.present, &present);
            output->synth_present_in_progress = false;
        }
    }
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    struct fbwl_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    if (output == NULL || output->wlr_output == NULL || event == NULL) {
        return;
    }
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_output *output = wl_container_of(listener, output, destroy);
    if (output == NULL) {
        return;
    }

    struct wlr_output *wlr_output = output->wlr_output;
    fbwl_output_on_destroy_fn on_destroy = output->on_destroy;
    void *on_destroy_userdata = output->on_destroy_userdata;

    fbwl_cleanup_listener(&output->frame);
    fbwl_cleanup_listener(&output->present);
    fbwl_cleanup_listener(&output->request_state);
    fbwl_cleanup_listener(&output->destroy);
    if (output->background_image != NULL) {
        wlr_scene_node_destroy(&output->background_image->node);
        output->background_image = NULL;
    }
    if (output->background_rect != NULL) {
        wlr_scene_node_destroy(&output->background_rect->node);
        output->background_rect = NULL;
    }
    wl_list_remove(&output->link);
    free(output);

    if (on_destroy != NULL) {
        on_destroy(on_destroy_userdata, wlr_output);
    }
}

struct fbwl_output *fbwl_output_create(struct wl_list *outputs, struct wlr_output *wlr_output,
        struct wlr_allocator *allocator, struct wlr_renderer *renderer,
        struct wlr_output_layout *output_layout, struct wlr_scene *scene, struct wlr_scene_output_layout *scene_layout,
        fbwl_output_on_destroy_fn on_destroy, void *on_destroy_userdata) {
    if (outputs == NULL || wlr_output == NULL || allocator == NULL || renderer == NULL ||
            output_layout == NULL || scene == NULL || scene_layout == NULL) {
        return NULL;
    }

    wlr_output_init_render(wlr_output, allocator, renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    wlr_log(WLR_INFO, "Output: %s %dx%d",
        wlr_output->name != NULL ? wlr_output->name : "(unnamed)",
        wlr_output->width, wlr_output->height);

    struct fbwl_output *output = calloc(1, sizeof(*output));
    if (output == NULL) {
        wlr_log(WLR_ERROR, "Output: failed to allocate");
        return NULL;
    }
    output->wlr_output = wlr_output;
    output->scene = scene;
    output->on_destroy = on_destroy;
    output->on_destroy_userdata = on_destroy_userdata;

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->present.notify = output_present;
    wl_signal_add(&wlr_output->events.present, &output->present);
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(outputs, &output->link);

    struct wlr_output_layout_output *layout_output = wlr_output_layout_add_auto(output_layout, wlr_output);
    struct wlr_box box = {0};
    wlr_output_layout_get_box(output_layout, wlr_output, &box);
    output->usable_area = box;
    wlr_log(WLR_INFO, "OutputLayout: name=%s x=%d y=%d w=%d h=%d",
        wlr_output->name != NULL ? wlr_output->name : "(unnamed)",
        box.x, box.y, box.width, box.height);
    struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, wlr_output);
    wlr_scene_output_layout_add_output(scene_layout, layout_output, scene_output);

    return output;
}
