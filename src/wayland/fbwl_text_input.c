#include "wayland/fbwl_text_input.h"

#include "wayland/fbwl_util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/util/log.h>

struct fbwl_text_input_client {
    struct fbwl_text_input_state *state;
    struct wlr_text_input_v3 *text_input;
    struct wl_listener enable;
    struct wl_listener commit;
    struct wl_listener disable;
    struct wl_listener destroy;
};

struct fbwl_input_method_client {
    struct fbwl_text_input_state *state;
    struct wlr_input_method_v2 *input_method;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static void fbwl_text_input_update_input_method(struct fbwl_text_input_state *state) {
    if (state == NULL || state->input_method == NULL) {
        return;
    }

    struct wlr_input_method_v2 *im = state->input_method;
    struct wlr_text_input_v3 *ti = state->active_text_input;

    const bool want_active =
        ti != NULL && ti->current_enabled && ti->focused_surface != NULL;

    if (want_active) {
        if (!im->client_active) {
            wlr_input_method_v2_send_activate(im);
        }

        const struct wlr_text_input_v3_state *st = &ti->current;
        if ((ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) != 0) {
            const char *txt = st->surrounding.text != NULL ? st->surrounding.text : "";
            wlr_input_method_v2_send_surrounding_text(im, txt,
                st->surrounding.cursor, st->surrounding.anchor);
            wlr_input_method_v2_send_text_change_cause(im, st->text_change_cause);
        }
        if ((ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) != 0) {
            wlr_input_method_v2_send_content_type(im,
                st->content_type.hint, st->content_type.purpose);
        }

        wlr_input_method_v2_send_done(im);
    } else {
        if (im->client_active) {
            wlr_input_method_v2_send_deactivate(im);
            wlr_input_method_v2_send_done(im);
        }
    }
}

void fbwl_text_input_update_focus(struct fbwl_text_input_state *state, struct wlr_surface *surface) {
    if (state == NULL || state->text_input_mgr == NULL) {
        return;
    }

    struct wl_client *focused_client = NULL;
    if (surface != NULL && surface->resource != NULL) {
        focused_client = wl_resource_get_client(surface->resource);
    }

    struct wlr_text_input_v3 *ti;
    wl_list_for_each(ti, &state->text_input_mgr->text_inputs, link) {
        if (ti->resource == NULL) {
            continue;
        }

        struct wl_client *client = wl_resource_get_client(ti->resource);
        if (surface != NULL && client == focused_client) {
            if (ti->focused_surface != surface) {
                wlr_text_input_v3_send_enter(ti, surface);
            }
        } else if (ti->focused_surface != NULL) {
            wlr_text_input_v3_send_leave(ti);
        }
    }

    if (state->active_text_input != NULL &&
            state->active_text_input->focused_surface != surface) {
        state->active_text_input = NULL;
        fbwl_text_input_update_input_method(state);
    }
}

static void text_input_enable(struct wl_listener *listener, void *data) {
    struct fbwl_text_input_client *ti = wl_container_of(listener, ti, enable);
    struct fbwl_text_input_state *state = ti != NULL ? ti->state : NULL;
    struct wlr_text_input_v3 *text_input = data;

    if (state == NULL || text_input == NULL) {
        return;
    }

    if (state->active_text_input != NULL && state->active_text_input != text_input) {
        wlr_log(WLR_INFO, "TextInput: ignoring enable (another text input already enabled)");
        return;
    }

    state->active_text_input = text_input;
    wlr_log(WLR_INFO, "TextInput: enable features=0x%x", text_input->active_features);
    fbwl_text_input_update_input_method(state);
}

static void text_input_commit(struct wl_listener *listener, void *data) {
    struct fbwl_text_input_client *ti = wl_container_of(listener, ti, commit);
    struct fbwl_text_input_state *state = ti != NULL ? ti->state : NULL;
    struct wlr_text_input_v3 *text_input = data;
    if (state == NULL || text_input == NULL) {
        return;
    }
    if (state->active_text_input == text_input) {
        fbwl_text_input_update_input_method(state);
    }
}

static void text_input_disable(struct wl_listener *listener, void *data) {
    struct fbwl_text_input_client *ti = wl_container_of(listener, ti, disable);
    struct fbwl_text_input_state *state = ti != NULL ? ti->state : NULL;
    struct wlr_text_input_v3 *text_input = data;
    if (state == NULL || text_input == NULL) {
        return;
    }
    if (state->active_text_input == text_input) {
        state->active_text_input = NULL;
        wlr_log(WLR_INFO, "TextInput: disable");
        fbwl_text_input_update_input_method(state);
    }
}

static void text_input_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_text_input_client *ti = wl_container_of(listener, ti, destroy);
    struct fbwl_text_input_state *state = ti != NULL ? ti->state : NULL;
    if (state != NULL && state->active_text_input == ti->text_input) {
        state->active_text_input = NULL;
        fbwl_text_input_update_input_method(state);
    }

    fbwl_cleanup_listener(&ti->enable);
    fbwl_cleanup_listener(&ti->commit);
    fbwl_cleanup_listener(&ti->disable);
    fbwl_cleanup_listener(&ti->destroy);
    free(ti);
}

static void fbwl_text_input_new_text_input(struct wl_listener *listener, void *data) {
    struct fbwl_text_input_state *state = wl_container_of(listener, state, new_text_input);
    struct wlr_text_input_v3 *text_input = data;

    struct fbwl_text_input_client *ti = calloc(1, sizeof(*ti));
    if (ti == NULL) {
        wlr_log(WLR_ERROR, "TextInput: out of memory");
        return;
    }
    ti->state = state;
    ti->text_input = text_input;

    ti->enable.notify = text_input_enable;
    wl_signal_add(&text_input->events.enable, &ti->enable);
    ti->commit.notify = text_input_commit;
    wl_signal_add(&text_input->events.commit, &ti->commit);
    ti->disable.notify = text_input_disable;
    wl_signal_add(&text_input->events.disable, &ti->disable);
    ti->destroy.notify = text_input_destroy;
    wl_signal_add(&text_input->events.destroy, &ti->destroy);

    struct wlr_surface *focused = NULL;
    if (state != NULL && state->seat != NULL) {
        focused = state->seat->keyboard_state.focused_surface;
    }
    if (focused != NULL && focused->resource != NULL && text_input->resource != NULL) {
        struct wl_client *focused_client = wl_resource_get_client(focused->resource);
        struct wl_client *client = wl_resource_get_client(text_input->resource);
        if (client == focused_client) {
            wlr_text_input_v3_send_enter(text_input, focused);
        }
    }
}

static void input_method_commit(struct wl_listener *listener, void *data) {
    struct fbwl_input_method_client *fim = wl_container_of(listener, fim, commit);
    struct fbwl_text_input_state *state = fim != NULL ? fim->state : NULL;
    struct wlr_input_method_v2 *im = data;
    if (state == NULL || im == NULL) {
        return;
    }

    struct wlr_text_input_v3 *ti = state->active_text_input;
    if (ti == NULL || !ti->current_enabled) {
        return;
    }

    const uint32_t del_before = im->current.delete.before_length != 0 || im->current.delete.after_length != 0
        ? im->current.delete.before_length
        : im->pending.delete.before_length;
    const uint32_t del_after = im->current.delete.before_length != 0 || im->current.delete.after_length != 0
        ? im->current.delete.after_length
        : im->pending.delete.after_length;

    const struct wlr_input_method_v2_preedit_string *preedit =
        im->current.preedit.text != NULL ? &im->current.preedit : &im->pending.preedit;
    const char *commit_text =
        im->current.commit_text != NULL ? im->current.commit_text : im->pending.commit_text;

    bool sent = false;
    if (del_before > 0 || del_after > 0) {
        wlr_text_input_v3_send_delete_surrounding_text(ti, del_before, del_after);
        sent = true;
    }
    if (preedit->text != NULL) {
        wlr_text_input_v3_send_preedit_string(ti, preedit->text,
            preedit->cursor_begin, preedit->cursor_end);
        sent = true;
    }
    if (commit_text != NULL) {
        wlr_log(WLR_INFO, "InputMethod: commit '%s'", commit_text);
        wlr_text_input_v3_send_commit_string(ti, commit_text);
        sent = true;
    }
    if (sent) {
        wlr_text_input_v3_send_done(ti);
    }
}

static void input_method_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct fbwl_input_method_client *fim = wl_container_of(listener, fim, destroy);
    struct fbwl_text_input_state *state = fim != NULL ? fim->state : NULL;
    if (state != NULL && state->input_method == fim->input_method) {
        state->input_method = NULL;
        fbwl_text_input_update_input_method(state);
    }

    fbwl_cleanup_listener(&fim->commit);
    fbwl_cleanup_listener(&fim->destroy);
    free(fim);
}

static void fbwl_text_input_new_input_method(struct wl_listener *listener, void *data) {
    struct fbwl_text_input_state *state = wl_container_of(listener, state, new_input_method);
    struct wlr_input_method_v2 *input_method = data;

    if (state->input_method != NULL && state->input_method != input_method) {
        wlr_log(WLR_INFO, "InputMethod: refusing second input method");
        wlr_input_method_v2_send_unavailable(input_method);
        return;
    }

    struct fbwl_input_method_client *fim = calloc(1, sizeof(*fim));
    if (fim == NULL) {
        wlr_log(WLR_ERROR, "InputMethod: out of memory");
        wlr_input_method_v2_send_unavailable(input_method);
        return;
    }
    fim->state = state;
    fim->input_method = input_method;

    fim->commit.notify = input_method_commit;
    wl_signal_add(&input_method->events.commit, &fim->commit);
    fim->destroy.notify = input_method_destroy;
    wl_signal_add(&input_method->events.destroy, &fim->destroy);

    state->input_method = input_method;
    fbwl_text_input_update_input_method(state);
}

bool fbwl_text_input_init(struct fbwl_text_input_state *state, struct wl_display *display, struct wlr_seat *seat) {
    if (state == NULL || display == NULL) {
        return false;
    }

    state->seat = seat;
    state->text_input_mgr = wlr_text_input_manager_v3_create(display);
    if (state->text_input_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create text input manager");
        return false;
    }
    state->new_text_input.notify = fbwl_text_input_new_text_input;
    wl_signal_add(&state->text_input_mgr->events.text_input, &state->new_text_input);
    state->active_text_input = NULL;

    state->input_method_mgr = wlr_input_method_manager_v2_create(display);
    if (state->input_method_mgr == NULL) {
        wlr_log(WLR_ERROR, "failed to create input method manager");
        fbwl_cleanup_listener(&state->new_text_input);
        return false;
    }
    state->new_input_method.notify = fbwl_text_input_new_input_method;
    wl_signal_add(&state->input_method_mgr->events.input_method, &state->new_input_method);
    state->input_method = NULL;

    return true;
}

void fbwl_text_input_finish(struct fbwl_text_input_state *state) {
    if (state == NULL) {
        return;
    }

    fbwl_cleanup_listener(&state->new_text_input);
    fbwl_cleanup_listener(&state->new_input_method);
    state->active_text_input = NULL;
    state->input_method = NULL;
    state->text_input_mgr = NULL;
    state->input_method_mgr = NULL;
    state->seat = NULL;
}
