#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

struct fbwl_injector {
    struct wl_display *display;
    struct wl_registry *registry;

    struct wl_seat *seat;
    struct zwp_virtual_keyboard_manager_v1 *vkbd_mgr;
    struct zwlr_virtual_pointer_manager_v1 *vptr_mgr;
};

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
    return (uint32_t)ms;
}

static int create_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("fbwl-input-injector", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
#endif

    char template[] = "/tmp/fbwl-input-injector-keymap-XXXXXX";
    fd = mkstemp(template);
    if (fd < 0) {
        return -1;
    }
    unlink(template);
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct fbwl_injector *inj = data;

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t v = version < 5 ? version : 5;
        inj->seat = wl_registry_bind(registry, name, &wl_seat_interface, v);
        return;
    }

    if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        uint32_t v = version < 1 ? version : 1;
        inj->vkbd_mgr = wl_registry_bind(registry, name,
            &zwp_virtual_keyboard_manager_v1_interface, v);
        return;
    }

    if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
        uint32_t v = version < 2 ? version : 2;
        inj->vptr_mgr = wl_registry_bind(registry, name,
            &zwlr_virtual_pointer_manager_v1_interface, v);
        return;
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
        uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void cleanup(struct fbwl_injector *inj) {
    if (inj->vptr_mgr != NULL) {
        zwlr_virtual_pointer_manager_v1_destroy(inj->vptr_mgr);
    }
    if (inj->vkbd_mgr != NULL) {
        zwp_virtual_keyboard_manager_v1_destroy(inj->vkbd_mgr);
    }
    if (inj->seat != NULL) {
        wl_seat_destroy(inj->seat);
    }
    if (inj->registry != NULL) {
        wl_registry_destroy(inj->registry);
    }
    if (inj->display != NULL) {
        wl_display_disconnect(inj->display);
    }
}

static int init_injector(struct fbwl_injector *inj, const char *socket_name) {
    inj->display = wl_display_connect(socket_name);
    if (inj->display == NULL) {
        fprintf(stderr, "fbwl-input-injector: wl_display_connect failed: %s\n",
            strerror(errno));
        return 1;
    }

    inj->registry = wl_display_get_registry(inj->display);
    wl_registry_add_listener(inj->registry, &registry_listener, inj);
    if (wl_display_roundtrip(inj->display) < 0) {
        fprintf(stderr, "fbwl-input-injector: wl_display_roundtrip failed: %s\n",
            strerror(errno));
        return 1;
    }

    if (inj->seat == NULL) {
        fprintf(stderr, "fbwl-input-injector: missing wl_seat\n");
        return 1;
    }
    if (inj->vkbd_mgr == NULL && inj->vptr_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: missing virtual input globals\n");
        return 1;
    }
    return 0;
}

static int send_keymap(struct zwp_virtual_keyboard_v1 *vkbd,
        struct xkb_keymap *keymap) {
    char *keymap_str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (keymap_str == NULL) {
        return 1;
    }

    const size_t size = strlen(keymap_str) + 1;
    int fd = create_shm_fd(size);
    if (fd < 0) {
        free(keymap_str);
        return 1;
    }

    ssize_t written = write(fd, keymap_str, size);
    free(keymap_str);
    if (written < 0 || (size_t)written != size) {
        close(fd);
        return 1;
    }
    lseek(fd, 0, SEEK_SET);

    zwp_virtual_keyboard_v1_keymap(vkbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)size);
    close(fd);
    return 0;
}

static void send_modifiers(struct zwp_virtual_keyboard_v1 *vkbd, struct xkb_state *state) {
    uint32_t depressed = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
    uint32_t latched = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
    uint32_t locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
    uint32_t group = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);
    zwp_virtual_keyboard_v1_modifiers(vkbd, depressed, latched, locked, group);
}

static void send_key(struct zwp_virtual_keyboard_v1 *vkbd, struct xkb_state *state,
        uint32_t key, enum wl_keyboard_key_state key_state) {
    xkb_state_update_key(state, key + 8,
        key_state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
    zwp_virtual_keyboard_v1_key(vkbd, now_ms(), key, key_state);
    send_modifiers(vkbd, state);
}

static int do_key_sequence_alt_key(struct fbwl_injector *inj, uint32_t key, const char *name) {
    if (inj->vkbd_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: %s: virtual keyboard manager not available\n",
            name != NULL ? name : "(unknown)");
        return 1;
    }

    struct zwp_virtual_keyboard_v1 *vkbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(inj->vkbd_mgr, inj->seat);
    if (vkbd == NULL) {
        fprintf(stderr, "fbwl-input-injector: %s: create_virtual_keyboard failed\n",
            name != NULL ? name : "(unknown)");
        return 1;
    }

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_context_new failed\n");
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_keymap_new_from_names failed\n");
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_state *state = xkb_state_new(keymap);
    if (state == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_state_new failed\n");
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    if (send_keymap(vkbd, keymap) != 0) {
        fprintf(stderr, "fbwl-input-injector: failed to send keymap\n");
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_PRESSED);
    send_key(vkbd, state, key, WL_KEYBOARD_KEY_STATE_PRESSED);
    send_key(vkbd, state, key, WL_KEYBOARD_KEY_STATE_RELEASED);
    send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_RELEASED);

    wl_display_roundtrip(inj->display);

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    zwp_virtual_keyboard_v1_destroy(vkbd);
    return 0;
}

static int do_key_sequence_alt_ctrl_key(struct fbwl_injector *inj, uint32_t key, const char *name) {
    if (inj->vkbd_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: %s: virtual keyboard manager not available\n",
            name != NULL ? name : "(unknown)");
        return 1;
    }

    struct zwp_virtual_keyboard_v1 *vkbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(inj->vkbd_mgr, inj->seat);
    if (vkbd == NULL) {
        fprintf(stderr, "fbwl-input-injector: %s: create_virtual_keyboard failed\n",
            name != NULL ? name : "(unknown)");
        return 1;
    }

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_context_new failed\n");
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_keymap_new_from_names failed\n");
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_state *state = xkb_state_new(keymap);
    if (state == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_state_new failed\n");
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    if (send_keymap(vkbd, keymap) != 0) {
        fprintf(stderr, "fbwl-input-injector: failed to send keymap\n");
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_PRESSED);
    send_key(vkbd, state, KEY_LEFTCTRL, WL_KEYBOARD_KEY_STATE_PRESSED);
    send_key(vkbd, state, key, WL_KEYBOARD_KEY_STATE_PRESSED);
    send_key(vkbd, state, key, WL_KEYBOARD_KEY_STATE_RELEASED);
    send_key(vkbd, state, KEY_LEFTCTRL, WL_KEYBOARD_KEY_STATE_RELEASED);
    send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_RELEASED);

    wl_display_roundtrip(inj->display);

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    zwp_virtual_keyboard_v1_destroy(vkbd);
    return 0;
}

static int do_key_sequence_alt_return(struct fbwl_injector *inj) {
    return do_key_sequence_alt_key(inj, 28, "alt-return"); /* KEY_ENTER */
}

static int do_key_sequence_alt_f1(struct fbwl_injector *inj) {
    return do_key_sequence_alt_key(inj, 59, "alt-f1"); /* KEY_F1 */
}

static int do_key_sequence_alt_f2(struct fbwl_injector *inj) {
    return do_key_sequence_alt_key(inj, 60, "alt-f2"); /* KEY_F2 */
}

static int do_key_sequence_alt_escape(struct fbwl_injector *inj) {
    return do_key_sequence_alt_key(inj, 1, "alt-escape"); /* KEY_ESC */
}

static int do_key_sequence_key(struct fbwl_injector *inj, uint32_t key, const char *name) {
    if (inj->vkbd_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: %s: virtual keyboard manager not available\n",
            name != NULL ? name : "(unknown)");
        return 1;
    }

    struct zwp_virtual_keyboard_v1 *vkbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(inj->vkbd_mgr, inj->seat);
    if (vkbd == NULL) {
        fprintf(stderr, "fbwl-input-injector: %s: create_virtual_keyboard failed\n",
            name != NULL ? name : "(unknown)");
        return 1;
    }

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_context_new failed\n");
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_keymap_new_from_names failed\n");
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_state *state = xkb_state_new(keymap);
    if (state == NULL) {
        fprintf(stderr, "fbwl-input-injector: xkb_state_new failed\n");
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    if (send_keymap(vkbd, keymap) != 0) {
        fprintf(stderr, "fbwl-input-injector: failed to send keymap\n");
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    send_key(vkbd, state, key, WL_KEYBOARD_KEY_STATE_PRESSED);
    send_key(vkbd, state, key, WL_KEYBOARD_KEY_STATE_RELEASED);

    wl_display_roundtrip(inj->display);

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    zwp_virtual_keyboard_v1_destroy(vkbd);
    return 0;
}

struct fbwl_keychar {
    uint32_t key;
    bool shift;
};

static bool keychar_for_char(char c, struct fbwl_keychar *out) {
    if (out == NULL) {
        return false;
    }
    out->key = 0;
    out->shift = false;

    if (c >= 'a' && c <= 'z') {
        switch (c) {
        case 'a': out->key = KEY_A; break;
        case 'b': out->key = KEY_B; break;
        case 'c': out->key = KEY_C; break;
        case 'd': out->key = KEY_D; break;
        case 'e': out->key = KEY_E; break;
        case 'f': out->key = KEY_F; break;
        case 'g': out->key = KEY_G; break;
        case 'h': out->key = KEY_H; break;
        case 'i': out->key = KEY_I; break;
        case 'j': out->key = KEY_J; break;
        case 'k': out->key = KEY_K; break;
        case 'l': out->key = KEY_L; break;
        case 'm': out->key = KEY_M; break;
        case 'n': out->key = KEY_N; break;
        case 'o': out->key = KEY_O; break;
        case 'p': out->key = KEY_P; break;
        case 'q': out->key = KEY_Q; break;
        case 'r': out->key = KEY_R; break;
        case 's': out->key = KEY_S; break;
        case 't': out->key = KEY_T; break;
        case 'u': out->key = KEY_U; break;
        case 'v': out->key = KEY_V; break;
        case 'w': out->key = KEY_W; break;
        case 'x': out->key = KEY_X; break;
        case 'y': out->key = KEY_Y; break;
        case 'z': out->key = KEY_Z; break;
        default: return false;
        }
        return true;
    }

    if (c >= 'A' && c <= 'Z') {
        if (!keychar_for_char((char)(c - 'A' + 'a'), out)) {
            return false;
        }
        out->shift = true;
        return true;
    }

    if (c == '0') {
        out->key = KEY_0;
        return true;
    }
    if (c >= '1' && c <= '9') {
        out->key = KEY_1 + (uint32_t)(c - '1');
        return true;
    }

    switch (c) {
    case ' ':
        out->key = KEY_SPACE;
        return true;
    case '/':
        out->key = KEY_SLASH;
        return true;
    case '-':
        out->key = KEY_MINUS;
        return true;
    case '_':
        out->key = KEY_MINUS;
        out->shift = true;
        return true;
    case '.':
        out->key = KEY_DOT;
        return true;
    default:
        return false;
    }
}

static int do_type_sequence(struct fbwl_injector *inj, const char *text) {
    if (inj->vkbd_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: type: virtual keyboard manager not available\n");
        return 1;
    }

    struct zwp_virtual_keyboard_v1 *vkbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(inj->vkbd_mgr, inj->seat);
    if (vkbd == NULL) {
        fprintf(stderr, "fbwl-input-injector: type: create_virtual_keyboard failed\n");
        return 1;
    }

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == NULL) {
        fprintf(stderr, "fbwl-input-injector: type: xkb_context_new failed\n");
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        fprintf(stderr, "fbwl-input-injector: type: xkb_keymap_new_from_names failed\n");
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_state *state = xkb_state_new(keymap);
    if (state == NULL) {
        fprintf(stderr, "fbwl-input-injector: type: xkb_state_new failed\n");
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    if (send_keymap(vkbd, keymap) != 0) {
        fprintf(stderr, "fbwl-input-injector: type: failed to send keymap\n");
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    const char *p = text != NULL ? text : "";
    while (*p != '\0') {
        struct fbwl_keychar kc;
        if (!keychar_for_char(*p, &kc)) {
            fprintf(stderr, "fbwl-input-injector: type: unsupported char: 0x%02x\n",
                (unsigned char)*p);
            xkb_state_unref(state);
            xkb_keymap_unref(keymap);
            xkb_context_unref(context);
            zwp_virtual_keyboard_v1_destroy(vkbd);
            return 1;
        }

        if (kc.shift) {
            send_key(vkbd, state, KEY_LEFTSHIFT, WL_KEYBOARD_KEY_STATE_PRESSED);
        }
        send_key(vkbd, state, kc.key, WL_KEYBOARD_KEY_STATE_PRESSED);
        send_key(vkbd, state, kc.key, WL_KEYBOARD_KEY_STATE_RELEASED);
        if (kc.shift) {
            send_key(vkbd, state, KEY_LEFTSHIFT, WL_KEYBOARD_KEY_STATE_RELEASED);
        }

        p++;
    }

    wl_display_roundtrip(inj->display);

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    zwp_virtual_keyboard_v1_destroy(vkbd);
    return 0;
}

static int do_click_sequence_button(struct fbwl_injector *inj, uint32_t button,
        int argc, char **argv) {
    if (inj->vptr_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: virtual pointer manager not available\n");
        return 1;
    }
    if (argc < 2 || (argc % 2) != 0) {
        fprintf(stderr, "fbwl-input-injector: click requires pairs of X Y coordinates\n");
        return 1;
    }

    struct zwlr_virtual_pointer_v1 *vptr =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(inj->vptr_mgr, inj->seat);
    if (vptr == NULL) {
        fprintf(stderr, "fbwl-input-injector: create_virtual_pointer failed\n");
        return 1;
    }

    /* Normalize tests by resetting to a known location. */
    zwlr_virtual_pointer_v1_motion_absolute(vptr, now_ms(), 0, 0, 1, 1);
    zwlr_virtual_pointer_v1_frame(vptr);

    double cur_x = 0;
    double cur_y = 0;

    for (int i = 0; i < argc; i += 2) {
        double x = atof(argv[i]);
        double y = atof(argv[i + 1]);

        double dx = x - cur_x;
        double dy = y - cur_y;
        cur_x = x;
        cur_y = y;

        zwlr_virtual_pointer_v1_motion(vptr, now_ms(),
            wl_fixed_from_double(dx), wl_fixed_from_double(dy));
        zwlr_virtual_pointer_v1_frame(vptr);

        zwlr_virtual_pointer_v1_button(vptr, now_ms(), button,
            WL_POINTER_BUTTON_STATE_PRESSED);
        zwlr_virtual_pointer_v1_button(vptr, now_ms(), button,
            WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(vptr);
    }

    wl_display_roundtrip(inj->display);

    zwlr_virtual_pointer_v1_destroy(vptr);
    return 0;
}

static int do_motion_sequence(struct fbwl_injector *inj, int argc, char **argv) {
    if (inj->vptr_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: motion: virtual pointer manager not available\n");
        return 1;
    }
    if (argc < 2 || (argc % 2) != 0) {
        fprintf(stderr, "fbwl-input-injector: motion requires pairs of X Y coordinates\n");
        return 1;
    }

    struct zwlr_virtual_pointer_v1 *vptr =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(inj->vptr_mgr, inj->seat);
    if (vptr == NULL) {
        fprintf(stderr, "fbwl-input-injector: motion: create_virtual_pointer failed\n");
        return 1;
    }

    /* Normalize tests by resetting to a known location. */
    zwlr_virtual_pointer_v1_motion_absolute(vptr, now_ms(), 0, 0, 1, 1);
    zwlr_virtual_pointer_v1_frame(vptr);

    double cur_x = 0;
    double cur_y = 0;

    for (int i = 0; i < argc; i += 2) {
        double x = atof(argv[i]);
        double y = atof(argv[i + 1]);

        double dx = x - cur_x;
        double dy = y - cur_y;
        cur_x = x;
        cur_y = y;

        zwlr_virtual_pointer_v1_motion(vptr, now_ms(),
            wl_fixed_from_double(dx), wl_fixed_from_double(dy));
        zwlr_virtual_pointer_v1_frame(vptr);
    }

    wl_display_roundtrip(inj->display);

    zwlr_virtual_pointer_v1_destroy(vptr);
    return 0;
}

static int do_drag_sequence_button(struct fbwl_injector *inj, uint32_t button,
        int argc, char **argv) {
    if (inj->vptr_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: virtual pointer manager not available\n");
        return 1;
    }
    if (argc != 4) {
        fprintf(stderr, "fbwl-input-injector: drag requires X1 Y1 X2 Y2\n");
        return 1;
    }

    const double x1 = atof(argv[0]);
    const double y1 = atof(argv[1]);
    const double x2 = atof(argv[2]);
    const double y2 = atof(argv[3]);

    struct zwlr_virtual_pointer_v1 *vptr =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(inj->vptr_mgr, inj->seat);
    if (vptr == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: create_virtual_pointer failed\n");
        return 1;
    }

    zwlr_virtual_pointer_v1_motion_absolute(vptr, now_ms(), 0, 0, 1, 1);
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_motion(vptr, now_ms(),
        wl_fixed_from_double(x1), wl_fixed_from_double(y1));
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_button(vptr, now_ms(), button,
        WL_POINTER_BUTTON_STATE_PRESSED);
    zwlr_virtual_pointer_v1_frame(vptr);

    /*
     * DnD (and other compositor <-> client roundtrips) can be sensitive to
     * timing: give the client time to react to the initiating press (serial)
     * before we move/release.
     */
    wl_display_roundtrip(inj->display);
    usleep(50 * 1000);

    zwlr_virtual_pointer_v1_motion(vptr, now_ms(),
        wl_fixed_from_double(x2 - x1), wl_fixed_from_double(y2 - y1));
    zwlr_virtual_pointer_v1_frame(vptr);

    wl_display_roundtrip(inj->display);
    usleep(50 * 1000);

    zwlr_virtual_pointer_v1_button(vptr, now_ms(), button,
        WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(vptr);

    wl_display_roundtrip(inj->display);

    zwlr_virtual_pointer_v1_destroy(vptr);
    return 0;
}

static int do_drag_sequence_alt_button(struct fbwl_injector *inj, uint32_t button,
        int argc, char **argv) {
    if (inj->vkbd_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: virtual keyboard manager not available\n");
        return 1;
    }
    if (inj->vptr_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: virtual pointer manager not available\n");
        return 1;
    }
    if (argc != 4) {
        fprintf(stderr, "fbwl-input-injector: drag requires X1 Y1 X2 Y2\n");
        return 1;
    }

    const double x1 = atof(argv[0]);
    const double y1 = atof(argv[1]);
    const double x2 = atof(argv[2]);
    const double y2 = atof(argv[3]);

    struct zwp_virtual_keyboard_v1 *vkbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(inj->vkbd_mgr, inj->seat);
    if (vkbd == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: create_virtual_keyboard failed\n");
        return 1;
    }

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: xkb_context_new failed\n");
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: xkb_keymap_new_from_names failed\n");
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_state *state = xkb_state_new(keymap);
    if (state == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: xkb_state_new failed\n");
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    if (send_keymap(vkbd, keymap) != 0) {
        fprintf(stderr, "fbwl-input-injector: drag: failed to send keymap\n");
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    /* Hold Alt during drag. */
    send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_PRESSED);
    wl_display_roundtrip(inj->display);

    struct zwlr_virtual_pointer_v1 *vptr =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(inj->vptr_mgr, inj->seat);
    if (vptr == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: create_virtual_pointer failed\n");
        send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_display_roundtrip(inj->display);
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    zwlr_virtual_pointer_v1_motion_absolute(vptr, now_ms(), 0, 0, 1, 1);
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_motion(vptr, now_ms(),
        wl_fixed_from_double(x1), wl_fixed_from_double(y1));
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_button(vptr, now_ms(), button,
        WL_POINTER_BUTTON_STATE_PRESSED);
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_motion(vptr, now_ms(),
        wl_fixed_from_double(x2 - x1), wl_fixed_from_double(y2 - y1));
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_button(vptr, now_ms(), button,
        WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(vptr);

    wl_display_roundtrip(inj->display);

    send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_roundtrip(inj->display);

    zwlr_virtual_pointer_v1_destroy(vptr);
    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    zwp_virtual_keyboard_v1_destroy(vkbd);
    return 0;
}

static int do_drag_sequence_alt_button_hold(struct fbwl_injector *inj, uint32_t button,
        int argc, char **argv) {
    if (inj->vkbd_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: virtual keyboard manager not available\n");
        return 1;
    }
    if (inj->vptr_mgr == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: virtual pointer manager not available\n");
        return 1;
    }
    if (argc != 5) {
        fprintf(stderr, "fbwl-input-injector: drag requires X1 Y1 X2 Y2 HOLD_MS\n");
        return 1;
    }

    const double x1 = atof(argv[0]);
    const double y1 = atof(argv[1]);
    const double x2 = atof(argv[2]);
    const double y2 = atof(argv[3]);
    int hold_ms = atoi(argv[4]);
    if (hold_ms < 0) {
        hold_ms = 0;
    }

    struct zwp_virtual_keyboard_v1 *vkbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(inj->vkbd_mgr, inj->seat);
    if (vkbd == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: create_virtual_keyboard failed\n");
        return 1;
    }

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: xkb_context_new failed\n");
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: xkb_keymap_new_from_names failed\n");
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    struct xkb_state *state = xkb_state_new(keymap);
    if (state == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: xkb_state_new failed\n");
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    if (send_keymap(vkbd, keymap) != 0) {
        fprintf(stderr, "fbwl-input-injector: drag: failed to send keymap\n");
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    /* Hold Alt during drag. */
    send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_PRESSED);
    wl_display_roundtrip(inj->display);

    struct zwlr_virtual_pointer_v1 *vptr =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(inj->vptr_mgr, inj->seat);
    if (vptr == NULL) {
        fprintf(stderr, "fbwl-input-injector: drag: create_virtual_pointer failed\n");
        send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_display_roundtrip(inj->display);
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
        return 1;
    }

    zwlr_virtual_pointer_v1_motion_absolute(vptr, now_ms(), 0, 0, 1, 1);
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_motion(vptr, now_ms(),
        wl_fixed_from_double(x1), wl_fixed_from_double(y1));
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_button(vptr, now_ms(), button,
        WL_POINTER_BUTTON_STATE_PRESSED);
    zwlr_virtual_pointer_v1_frame(vptr);

    zwlr_virtual_pointer_v1_motion(vptr, now_ms(),
        wl_fixed_from_double(x2 - x1), wl_fixed_from_double(y2 - y1));
    zwlr_virtual_pointer_v1_frame(vptr);

    wl_display_roundtrip(inj->display);
    if (hold_ms > 0) {
        usleep((useconds_t)hold_ms * 1000);
    }

    zwlr_virtual_pointer_v1_button(vptr, now_ms(), button,
        WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(vptr);

    wl_display_roundtrip(inj->display);

    send_key(vkbd, state, KEY_LEFTALT, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_roundtrip(inj->display);

    zwlr_virtual_pointer_v1_destroy(vptr);
    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    zwp_virtual_keyboard_v1_destroy(vkbd);
    return 0;
}

static int do_hold(struct fbwl_injector *inj, int hold_ms) {
    struct zwp_virtual_keyboard_v1 *vkbd = NULL;
    struct zwlr_virtual_pointer_v1 *vptr = NULL;

    struct xkb_context *context = NULL;
    struct xkb_keymap *keymap = NULL;

    if (inj->vkbd_mgr != NULL) {
        vkbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(inj->vkbd_mgr, inj->seat);
        if (vkbd == NULL) {
            fprintf(stderr, "fbwl-input-injector: hold: create_virtual_keyboard failed\n");
            return 1;
        }

        context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (context == NULL) {
            fprintf(stderr, "fbwl-input-injector: hold: xkb_context_new failed\n");
            zwp_virtual_keyboard_v1_destroy(vkbd);
            return 1;
        }

        keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (keymap == NULL) {
            fprintf(stderr, "fbwl-input-injector: hold: xkb_keymap_new_from_names failed\n");
            xkb_context_unref(context);
            zwp_virtual_keyboard_v1_destroy(vkbd);
            return 1;
        }

        if (send_keymap(vkbd, keymap) != 0) {
            fprintf(stderr, "fbwl-input-injector: hold: failed to send keymap\n");
            xkb_keymap_unref(keymap);
            xkb_context_unref(context);
            zwp_virtual_keyboard_v1_destroy(vkbd);
            return 1;
        }
    }

    if (inj->vptr_mgr != NULL) {
        vptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(inj->vptr_mgr, inj->seat);
        if (vptr == NULL) {
            fprintf(stderr, "fbwl-input-injector: hold: create_virtual_pointer failed\n");
            if (vkbd != NULL) {
                xkb_keymap_unref(keymap);
                xkb_context_unref(context);
                zwp_virtual_keyboard_v1_destroy(vkbd);
            }
            return 1;
        }
    }

    wl_display_roundtrip(inj->display);

    if (hold_ms > 0) {
        usleep((useconds_t)hold_ms * 1000);
    }

    if (vptr != NULL) {
        zwlr_virtual_pointer_v1_destroy(vptr);
    }
    if (vkbd != NULL) {
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        zwp_virtual_keyboard_v1_destroy(vkbd);
    }

    wl_display_roundtrip(inj->display);
    return 0;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s [--socket NAME] click X1 Y1 [X2 Y2 ...]\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] click-middle X1 Y1 [X2 Y2 ...]\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] click-right X1 Y1 [X2 Y2 ...]\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] motion X1 Y1 [X2 Y2 ...]\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] type TEXT\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-return\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-f1\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-f2\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-escape\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key enter\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key escape\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key left\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key right\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key up\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key down\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-f\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-m\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-i\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-[1-9]\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] key alt-ctrl-[1-9]\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] drag-left X1 Y1 X2 Y2\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] drag-right X1 Y1 X2 Y2\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] drag-alt-left X1 Y1 X2 Y2\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] drag-alt-right X1 Y1 X2 Y2\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] drag-alt-left-hold X1 Y1 X2 Y2 HOLD_MS\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] drag-alt-right-hold X1 Y1 X2 Y2 HOLD_MS\n", argv0);
    fprintf(stderr, "  %s [--socket NAME] hold [MS]\n", argv0);
}

int main(int argc, char **argv) {
    const char *socket_name = NULL;

    static const struct option options[] = {
        {"socket", required_argument, NULL, 1},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    // Use leading '+' to stop option parsing at the first non-option.
    // This allows negative coordinate arguments (e.g. -1) without getopt treating them as options.
    while ((c = getopt_long(argc, argv, "+h", options, NULL)) != -1) {
        switch (c) {
        case 1:
            socket_name = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return c == 'h' ? 0 : 1;
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }

    struct fbwl_injector inj = {0};
    if (init_injector(&inj, socket_name) != 0) {
        cleanup(&inj);
        return 1;
    }

    const char *cmd = argv[optind++];
    int rc = 0;
    if (strcmp(cmd, "click") == 0) {
        rc = do_click_sequence_button(&inj, BTN_LEFT, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "click-middle") == 0) {
        rc = do_click_sequence_button(&inj, BTN_MIDDLE, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "click-right") == 0) {
        rc = do_click_sequence_button(&inj, BTN_RIGHT, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "motion") == 0) {
        rc = do_motion_sequence(&inj, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "type") == 0) {
        if (optind >= argc) {
            fprintf(stderr, "fbwl-input-injector: missing text\n");
            rc = 1;
        } else {
            size_t need = 0;
            for (int i = optind; i < argc; i++) {
                need += strlen(argv[i]) + 1;
            }
            char *joined = calloc(need + 1, 1);
            if (joined == NULL) {
                fprintf(stderr, "fbwl-input-injector: type: out of memory\n");
                rc = 1;
            } else {
                for (int i = optind; i < argc; i++) {
                    if (i > optind) {
                        strcat(joined, " ");
                    }
                    strcat(joined, argv[i]);
                }
                rc = do_type_sequence(&inj, joined);
                free(joined);
            }
        }
    } else if (strcmp(cmd, "drag-left") == 0) {
        rc = do_drag_sequence_button(&inj, BTN_LEFT, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "drag-right") == 0) {
        rc = do_drag_sequence_button(&inj, BTN_RIGHT, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "drag-alt-left") == 0) {
        rc = do_drag_sequence_alt_button(&inj, BTN_LEFT, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "drag-alt-right") == 0) {
        rc = do_drag_sequence_alt_button(&inj, BTN_RIGHT, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "drag-alt-left-hold") == 0) {
        rc = do_drag_sequence_alt_button_hold(&inj, BTN_LEFT, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "drag-alt-right-hold") == 0) {
        rc = do_drag_sequence_alt_button_hold(&inj, BTN_RIGHT, argc - optind, &argv[optind]);
    } else if (strcmp(cmd, "hold") == 0) {
        int hold_ms = 10000;
        if (optind < argc) {
            hold_ms = atoi(argv[optind]);
            if (hold_ms < 0) {
                hold_ms = 0;
            }
        }
        rc = do_hold(&inj, hold_ms);
    } else if (strcmp(cmd, "key") == 0) {
        if (optind >= argc) {
            fprintf(stderr, "fbwl-input-injector: missing key name\n");
            rc = 1;
        } else if (strcmp(argv[optind], "alt-return") == 0) {
            rc = do_key_sequence_alt_return(&inj);
        } else if (strcmp(argv[optind], "alt-f1") == 0) {
            rc = do_key_sequence_alt_f1(&inj);
        } else if (strcmp(argv[optind], "alt-f2") == 0) {
            rc = do_key_sequence_alt_f2(&inj);
        } else if (strcmp(argv[optind], "alt-escape") == 0) {
            rc = do_key_sequence_alt_escape(&inj);
        } else if (strcmp(argv[optind], "enter") == 0) {
            rc = do_key_sequence_key(&inj, KEY_ENTER, argv[optind]);
        } else if (strcmp(argv[optind], "escape") == 0) {
            rc = do_key_sequence_key(&inj, KEY_ESC, argv[optind]);
        } else if (strcmp(argv[optind], "left") == 0) {
            rc = do_key_sequence_key(&inj, KEY_LEFT, argv[optind]);
        } else if (strcmp(argv[optind], "right") == 0) {
            rc = do_key_sequence_key(&inj, KEY_RIGHT, argv[optind]);
        } else if (strcmp(argv[optind], "up") == 0) {
            rc = do_key_sequence_key(&inj, KEY_UP, argv[optind]);
        } else if (strcmp(argv[optind], "down") == 0) {
            rc = do_key_sequence_key(&inj, KEY_DOWN, argv[optind]);
        } else if (strcmp(argv[optind], "alt-f") == 0) {
            rc = do_key_sequence_alt_key(&inj, KEY_F, argv[optind]);
        } else if (strcmp(argv[optind], "alt-m") == 0) {
            rc = do_key_sequence_alt_key(&inj, KEY_M, argv[optind]);
        } else if (strcmp(argv[optind], "alt-i") == 0) {
            rc = do_key_sequence_alt_key(&inj, KEY_I, argv[optind]);
        } else if (strncmp(argv[optind], "alt-ctrl-", 9) == 0 &&
                argv[optind][9] >= '1' && argv[optind][9] <= '9' &&
                argv[optind][10] == '\0') {
            const int digit = argv[optind][9] - '0';
            rc = do_key_sequence_alt_ctrl_key(&inj, KEY_1 + (digit - 1), argv[optind]);
        } else if (strncmp(argv[optind], "alt-", 4) == 0 &&
                argv[optind][4] >= '1' && argv[optind][4] <= '9' &&
                argv[optind][5] == '\0') {
            const int digit = argv[optind][4] - '0';
            rc = do_key_sequence_alt_key(&inj, KEY_1 + (digit - 1), argv[optind]);
        } else {
            fprintf(stderr, "fbwl-input-injector: unknown key sequence: %s\n", argv[optind]);
            rc = 1;
        }
    } else {
        fprintf(stderr, "fbwl-input-injector: unknown command: %s\n", cmd);
        rc = 1;
    }

    cleanup(&inj);
    return rc;
}
