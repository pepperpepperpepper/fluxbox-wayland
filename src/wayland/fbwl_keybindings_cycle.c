#include "wayland/fbwl_keybindings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "wayland/fbwl_server_internal.h"
#include "wayland/fbwl_ui_toolbar_iconbar_pattern.h"
#include "wmcore/fbwm_output.h"

static int current_workspace0(const struct fbwl_keybindings_hooks *hooks) {
    if (hooks == NULL || hooks->wm == NULL) {
        return 0;
    }
    if (hooks->workspace_current != NULL) {
        return hooks->workspace_current(hooks->userdata, hooks->cursor_x, hooks->cursor_y);
    }
    return fbwm_core_workspace_current(hooks->wm);
}

static void build_cycle_pattern_env(struct fbwl_ui_toolbar_env *env, const struct fbwl_keybindings_hooks *hooks,
        const struct fbwl_view *view) {
    if (env == NULL) {
        return;
    }

    *env = (struct fbwl_ui_toolbar_env){0};

    if (hooks != NULL) {
        env->wm = hooks->wm;
        env->cursor_valid = true;
        env->cursor_x = (double)hooks->cursor_x;
        env->cursor_y = (double)hooks->cursor_y;
        if (hooks->wm != NULL && hooks->wm->focused != NULL) {
            env->focused_view = hooks->wm->focused->userdata;
        }
    }

    const struct fbwl_server *server = view != NULL ? view->server : NULL;
    if (server != NULL) {
        env->output_layout = server->output_layout;
        env->outputs = &server->outputs;
        env->xwayland = server->xwayland;
        env->layer_background = server->layer_background;
        env->layer_bottom = server->layer_bottom;
        env->layer_normal = server->layer_normal;
        env->layer_top = server->layer_top;
        env->layer_overlay = server->layer_overlay;
    }
}

static bool cycle_pattern_matches(const struct fbwl_iconbar_pattern *pat, const struct fbwl_view *view,
        const struct fbwl_keybindings_hooks *hooks) {
    if (pat == NULL || view == NULL || hooks == NULL || hooks->wm == NULL) {
        return false;
    }
    if (hooks->cycle_view_allowed != NULL && !hooks->cycle_view_allowed(hooks->userdata, view)) {
        return false;
    }

    // Fluxbox/X11 NextWindow operates on the focus list, which excludes FocusHidden.
    if (fbwl_view_is_focus_hidden(view)) {
        return false;
    }

    struct fbwl_ui_toolbar_env env = {0};
    build_cycle_pattern_env(&env, hooks, view);
    return fbwl_client_pattern_matches(pat, &env, view, current_workspace0(hooks));
}

static int view_create_seq_cmp(const void *a, const void *b) {
    const struct fbwl_view *av = *(const struct fbwl_view *const *)a;
    const struct fbwl_view *bv = *(const struct fbwl_view *const *)b;
    if (av == NULL || bv == NULL) {
        return 0;
    }
    if (av->create_seq < bv->create_seq) {
        return -1;
    }
    if (av->create_seq > bv->create_seq) {
        return 1;
    }
    if (av < bv) {
        return -1;
    }
    if (av > bv) {
        return 1;
    }
    return 0;
}

static struct fbwl_view *pick_cycle_candidate(const struct fbwl_keybindings_hooks *hooks, bool reverse, bool groups,
        bool static_order, const struct fbwl_iconbar_pattern *pat) {
    if (hooks == NULL || hooks->wm == NULL || pat == NULL) {
        return NULL;
    }
    if (static_order) {
        struct fbwl_view **candidates = NULL;
        size_t candidates_len = 0;
        size_t candidates_cap = 0;
        for (struct fbwm_view *wm_view = hooks->wm->views.next; wm_view != &hooks->wm->views; wm_view = wm_view->next) {
            if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
                continue;
            }
            struct fbwl_view *view = wm_view->userdata;
            if (view == NULL) {
                continue;
            }
            if (groups && !fbwl_tabs_view_is_active(view)) {
                continue;
            }
            if (!cycle_pattern_matches(pat, view, hooks)) {
                continue;
            }
            if (candidates_len >= candidates_cap) {
                const size_t new_cap = candidates_cap > 0 ? candidates_cap * 2 : 16;
                struct fbwl_view **tmp = realloc(candidates, new_cap * sizeof(*tmp));
                if (tmp == NULL) {
                    free(candidates);
                    return NULL;
                }
                candidates = tmp;
                candidates_cap = new_cap;
            }
            candidates[candidates_len++] = view;
        }
        if (candidates_len == 0) {
            free(candidates);
            return NULL;
        }
        qsort(candidates, candidates_len, sizeof(*candidates), view_create_seq_cmp);
        const struct fbwl_view *focused_view = hooks->wm->focused != NULL ? hooks->wm->focused->userdata : NULL;
        size_t focus_idx = 0;
        bool focus_found = false;
        if (focused_view != NULL) {
            for (size_t i = 0; i < candidates_len; i++) {
                if (candidates[i] == focused_view) {
                    focus_idx = i;
                    focus_found = true;
                    break;
                }
            }
        }
        size_t pick_idx = 0;
        if (!focus_found) {
            pick_idx = reverse ? (candidates_len - 1) : 0;
        } else if (reverse) {
            pick_idx = (focus_idx + candidates_len - 1) % candidates_len;
        } else {
            pick_idx = (focus_idx + 1) % candidates_len;
        }
        struct fbwl_view *pick = candidates[pick_idx];
        free(candidates);
        return pick;
    }
    const struct fbwm_view *focused = hooks->wm->focused;
    if (reverse) {
        for (struct fbwm_view *wm_view = hooks->wm->views.next; wm_view != &hooks->wm->views; wm_view = wm_view->next) {
            if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
                continue;
            }
            struct fbwl_view *view = wm_view->userdata;
            if (view == NULL) {
                continue;
            }
            if (groups && !fbwl_tabs_view_is_active(view)) {
                continue;
            }
            if (focused != NULL && wm_view == focused) {
                continue;
            }
            if (!cycle_pattern_matches(pat, view, hooks)) {
                continue;
            }
            return view;
        }
        return NULL;
    }
    for (struct fbwm_view *wm_view = hooks->wm->views.prev; wm_view != &hooks->wm->views; wm_view = wm_view->prev) {
        if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
            continue;
        }
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL) {
            continue;
        }
        if (groups && !fbwl_tabs_view_is_active(view)) {
            continue;
        }
        if (!cycle_pattern_matches(pat, view, hooks)) {
            continue;
        }
        return view;
    }
    return NULL;
}

struct fbwl_view *fbwl_keybindings_pick_cycle_candidate(const struct fbwl_keybindings_hooks *hooks, bool reverse,
        bool groups, bool static_order, char *pattern) {
    struct fbwl_iconbar_pattern pat = {0};
    fbwl_iconbar_pattern_parse_inplace(&pat, pattern);
    struct fbwl_view *pick = pick_cycle_candidate(hooks, reverse, groups, static_order, &pat);
    fbwl_iconbar_pattern_free(&pat);
    return pick;
}

static struct fbwl_view *pick_goto_candidate(const struct fbwl_keybindings_hooks *hooks, int num, bool groups,
        bool static_order, const struct fbwl_iconbar_pattern *pat) {
    if (hooks == NULL || hooks->wm == NULL || pat == NULL) {
        return NULL;
    }
    if (num == 0) {
        return NULL;
    }

    bool reverse = false;
    if (num < 0) {
        reverse = true;
        num = -num;
    }
    if (num < 1) {
        return NULL;
    }

    struct fbwl_view *pick = NULL;
    int remaining = num;

    if (static_order) {
        struct fbwl_view **candidates = NULL;
        size_t candidates_len = 0;
        size_t candidates_cap = 0;

        for (struct fbwm_view *wm_view = hooks->wm->views.next; wm_view != &hooks->wm->views; wm_view = wm_view->next) {
            if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
                continue;
            }
            struct fbwl_view *view = wm_view->userdata;
            if (view == NULL) {
                continue;
            }
            if (groups && !fbwl_tabs_view_is_active(view)) {
                continue;
            }
            if (!cycle_pattern_matches(pat, view, hooks)) {
                continue;
            }
            if (candidates_len >= candidates_cap) {
                const size_t new_cap = candidates_cap > 0 ? candidates_cap * 2 : 16;
                struct fbwl_view **tmp = realloc(candidates, new_cap * sizeof(*tmp));
                if (tmp == NULL) {
                    free(candidates);
                    return NULL;
                }
                candidates = tmp;
                candidates_cap = new_cap;
            }
            candidates[candidates_len++] = view;
        }

        if (candidates_len == 0) {
            free(candidates);
            return NULL;
        }

        qsort(candidates, candidates_len, sizeof(*candidates), view_create_seq_cmp);

        if (reverse) {
            for (size_t i = candidates_len; i > 0 && remaining > 0; i--) {
                pick = candidates[i - 1];
                remaining--;
            }
        } else {
            for (size_t i = 0; i < candidates_len && remaining > 0; i++) {
                pick = candidates[i];
                remaining--;
            }
        }

        free(candidates);
        return pick;
    }

    if (reverse) {
        for (struct fbwm_view *wm_view = hooks->wm->views.prev; wm_view != &hooks->wm->views && remaining > 0;
                wm_view = wm_view->prev) {
            if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
                continue;
            }
            struct fbwl_view *view = wm_view->userdata;
            if (view == NULL) {
                continue;
            }
            if (groups && !fbwl_tabs_view_is_active(view)) {
                continue;
            }
            if (!cycle_pattern_matches(pat, view, hooks)) {
                continue;
            }
            pick = view;
            remaining--;
        }
        return pick;
    }

    for (struct fbwm_view *wm_view = hooks->wm->views.next; wm_view != &hooks->wm->views && remaining > 0;
            wm_view = wm_view->next) {
        if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
            continue;
        }
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL) {
            continue;
        }
        if (groups && !fbwl_tabs_view_is_active(view)) {
            continue;
        }
        if (!cycle_pattern_matches(pat, view, hooks)) {
            continue;
        }
        pick = view;
        remaining--;
    }

    return pick;
}

struct fbwl_view *fbwl_keybindings_pick_goto_candidate(const struct fbwl_keybindings_hooks *hooks, int num, bool groups,
        bool static_order, char *pattern) {
    struct fbwl_iconbar_pattern pat = {0};
    fbwl_iconbar_pattern_parse_inplace(&pat, pattern);
    struct fbwl_view *pick = pick_goto_candidate(hooks, num, groups, static_order, &pat);
    fbwl_iconbar_pattern_free(&pat);
    return pick;
}

struct fbwl_view *fbwl_keybindings_pick_dir_focus_candidate(const struct fbwl_keybindings_hooks *hooks,
        const struct fbwl_view *reference, enum fbwl_focus_dir dir) {
    if (hooks == NULL || hooks->wm == NULL || reference == NULL) {
        return NULL;
    }

    struct fbwm_box ref_box = {0};
    if (reference->wm_view.ops == NULL || reference->wm_view.ops->get_box == NULL) {
        return NULL;
    }
    if (!reference->wm_view.ops->get_box(&reference->wm_view, &ref_box)) {
        return NULL;
    }

    int borderW = 0;
    if (reference->decor_enabled && !reference->fullscreen) {
        const int w = fbwl_view_current_width(reference);
        if (w > 0 && ref_box.width > w) {
            borderW = (ref_box.width - w) / 2;
        }
    }

    const int top = ref_box.y + borderW;
    const int bottom = ref_box.y + ref_box.height - borderW;
    const int left = ref_box.x + borderW;
    const int right = ref_box.x + ref_box.width - borderW;

    struct fbwl_view *found = NULL;
    int weight = 999999;
    int exposure = 0;
    uint64_t found_seq = 0;

    for (struct fbwm_view *wm_view = hooks->wm->views.next; wm_view != &hooks->wm->views; wm_view = wm_view->next) {
        if (!fbwm_core_view_is_visible(hooks->wm, wm_view)) {
            continue;
        }
        struct fbwl_view *view = wm_view->userdata;
        if (view == NULL || view == reference) {
            continue;
        }
        if (view->tab_group != NULL && !fbwl_tabs_view_is_active(view)) {
            continue;
        }
        if (hooks->cycle_view_allowed != NULL && !hooks->cycle_view_allowed(hooks->userdata, view)) {
            continue;
        }
        if (fbwl_view_is_focus_hidden(view) || !fbwl_view_accepts_focus(view)) {
            continue;
        }

        struct fbwm_box box = {0};
        if (view->wm_view.ops == NULL || view->wm_view.ops->get_box == NULL) {
            continue;
        }
        if (!view->wm_view.ops->get_box(&view->wm_view, &box)) {
            continue;
        }

        const int otop = box.y + borderW;
        const int obottom = box.y + box.height - borderW;
        const int oleft = box.x + borderW;
        const int oright = box.x + box.width - borderW;

        int edge = 0, upper = 0, lower = 0, oedge = 0, oupper = 0, olower = 0;
        switch (dir) {
        case FBWL_FOCUS_DIR_UP:
            edge = obottom;
            oedge = bottom;
            upper = left;
            oupper = oleft;
            lower = right;
            olower = oright;
            break;
        case FBWL_FOCUS_DIR_DOWN:
            edge = top;
            oedge = otop;
            upper = left;
            oupper = oleft;
            lower = right;
            olower = oright;
            break;
        case FBWL_FOCUS_DIR_LEFT:
            edge = oright;
            oedge = right;
            upper = top;
            oupper = otop;
            lower = bottom;
            olower = obottom;
            break;
        case FBWL_FOCUS_DIR_RIGHT:
            edge = left;
            oedge = oleft;
            upper = top;
            oupper = otop;
            lower = bottom;
            olower = obottom;
            break;
        default:
            return NULL;
        }

        if (oedge < edge) {
            continue;
        }

        if (olower <= upper || oupper >= lower) {
            const int myweight = 100000 + oedge - edge + abs(upper - oupper) + abs(lower - olower);
            if (found == NULL || myweight < weight || (myweight == weight && view->create_seq < found_seq)) {
                found = view;
                exposure = 0;
                weight = myweight;
                found_seq = view->create_seq;
            }
            continue;
        }

        const int myweight = oedge - edge;
        const int myexp = ((lower < olower) ? lower : olower) - ((upper > oupper) ? upper : oupper);
        if (myweight < weight ||
                (found != NULL && myweight == weight &&
                    (myexp > exposure || (myexp == exposure && view->create_seq < found_seq)))) {
            found = view;
            weight = myweight;
            exposure = myexp;
            found_seq = view->create_seq;
        }
    }

    return found;
}
