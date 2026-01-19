#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
 
#include <errno.h>
 
#include <systemd/sd-bus.h>

static bool g_debug = false;

static void usage(const char *argv0) {
    printf("Usage: %s [--timeout-ms MS] [--screenshot]\n", argv0);
    printf("\n");
    printf("By default, drives a non-interactive ScreenCast flow via xdg-desktop-portal\n");
    printf("(org.freedesktop.portal.Desktop) and prints the PipeWire node id on success.\n");
    printf("\n");
    printf("When --screenshot is set, drives org.freedesktop.portal.Screenshot.Screenshot\n");
    printf("and prints the resulting file URI on success.\n");
}
 
static uint64_t now_usec(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}
 
static char *bus_name_to_path_component(const char *name) {
    if (name == NULL || *name == '\0') {
        return NULL;
    }
 
    const char *p = name;
    if (*p == ':') {
        p++;
    }
 
    size_t len = strlen(p);
    char *out = calloc(1, len + 1);
    if (out == NULL) {
        return NULL;
    }
 
    for (size_t i = 0; i < len; i++) {
        out[i] = (p[i] == '.') ? '_' : p[i];
    }
 
    out[len] = '\0';
    return out;
}
 
static int append_dict_entry_string(sd_bus_message *m, const char *key, const char *value) {
    int r = sd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_append(m, "s", key);
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "s");
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_append(m, "s", value);
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
 
    return sd_bus_message_close_container(m);
}
 
static int append_dict_entry_u32(sd_bus_message *m, const char *key, uint32_t value) {
    int r = sd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_append(m, "s", key);
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "u");
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_append(m, "u", value);
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
 
    return sd_bus_message_close_container(m);
}
 
static int append_dict_entry_bool(sd_bus_message *m, const char *key, bool value) {
    int r = sd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_append(m, "s", key);
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "b");
    if (r < 0) {
        return r;
    }
 
    int b = value ? 1 : 0;
    r = sd_bus_message_append(m, "b", b);
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
 
    return sd_bus_message_close_container(m);
}
 
struct portal_wait {
    bool done;
    uint32_t response;
    char *session_handle;
    uint32_t node_id;
    bool node_id_set;
    char *uri;
};
 
static int parse_streams(sd_bus_message *m, const char *sig, uint32_t *out_node_id) {
    if (m == NULL || sig == NULL || out_node_id == NULL) {
        return -EINVAL;
    }
 
    /* Most portal impls return streams as a(ua{sv}). */
    if (strcmp(sig, "a(ua{sv})") != 0) {
        return -ENOTSUP;
    }
 
    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "(ua{sv})");
    if (r < 0) {
        return r;
    }
 
    bool found = false;
    while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_STRUCT, "ua{sv}")) > 0) {
        uint32_t first_u = 0;
        r = sd_bus_message_read(m, "u", &first_u);
        if (r < 0) {
            return r;
        }
 
        uint32_t node_id = first_u;
        bool node_id_found = false;
 
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
        if (r < 0) {
            return r;
        }
 
        while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
            const char *key = NULL;
            r = sd_bus_message_read(m, "s", &key);
            if (r < 0) {
                return r;
            }
 
            char vtype = 0;
            const char *vcontents = NULL;
            r = sd_bus_message_peek_type(m, &vtype, &vcontents);
            if (r <= 0 || vtype != SD_BUS_TYPE_VARIANT || vcontents == NULL) {
                return -EBADMSG;
            }
 
            r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, vcontents);
            if (r < 0) {
                return r;
            }
 
            if (key != NULL && strcmp(key, "node_id") == 0 && strcmp(vcontents, "u") == 0) {
                uint32_t v = 0;
                r = sd_bus_message_read(m, "u", &v);
                if (r < 0) {
                    return r;
                }
                node_id = v;
                node_id_found = true;
            } else {
                r = sd_bus_message_skip(m, NULL);
                if (r < 0) {
                    return r;
                }
            }
 
            r = sd_bus_message_exit_container(m);
            if (r < 0) {
                return r;
            }
 
            r = sd_bus_message_exit_container(m);
            if (r < 0) {
                return r;
            }
        }
        if (r < 0) {
            return r;
        }
 
        r = sd_bus_message_exit_container(m);
        if (r < 0) {
            return r;
        }
 
        r = sd_bus_message_exit_container(m);
        if (r < 0) {
            return r;
        }
 
        /* Accept either the first tuple's u or an explicit node_id field. */
        if (node_id_found || node_id > 0) {
            *out_node_id = node_id;
            found = true;
            break;
        }
    }
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_exit_container(m);
    if (r < 0) {
        return r;
    }
 
    return found ? 0 : -ENOENT;
}
 
static int portal_response_cb(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)ret_error;
    struct portal_wait *w = userdata;
    if (w == NULL) {
        return 0;
    }
 
    uint32_t resp = 1;
    int r = sd_bus_message_read(m, "u", &resp);
    if (r < 0) {
        w->response = 1;
        w->done = true;
        return 0;
    }
 
    w->response = resp;
 
    /* Parse results a{sv}. */
    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) {
        w->done = true;
        return 0;
    }
 
    while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
        const char *key = NULL;
        r = sd_bus_message_read(m, "s", &key);
        if (r < 0) {
            w->done = true;
            return 0;
        }
 
        char vtype = 0;
        const char *vcontents = NULL;
        r = sd_bus_message_peek_type(m, &vtype, &vcontents);
        if (r <= 0 || vtype != SD_BUS_TYPE_VARIANT || vcontents == NULL) {
            w->done = true;
            return 0;
        }

        if (g_debug && key != NULL) {
            fprintf(stderr, "portal: Response key=%s sig=%s\n", key, vcontents);
        }

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, vcontents);
        if (r < 0) {
            w->done = true;
            return 0;
        }

        if (key != NULL && strcmp(key, "session_handle") == 0 && (strcmp(vcontents, "o") == 0 || strcmp(vcontents, "s") == 0)) {
            const char *path = NULL;
            r = sd_bus_message_read(m, strcmp(vcontents, "o") == 0 ? "o" : "s", &path);
            if (r >= 0 && path != NULL && w->session_handle == NULL) {
                if (g_debug) {
                    fprintf(stderr, "portal: session_handle=%s\n", path);
                }
                w->session_handle = strdup(path);
            }
        } else if (key != NULL && strcmp(key, "uri") == 0 && strcmp(vcontents, "s") == 0) {
            const char *uri = NULL;
            r = sd_bus_message_read(m, "s", &uri);
            if (r >= 0 && uri != NULL && w->uri == NULL) {
                if (g_debug) {
                    fprintf(stderr, "portal: uri=%s\n", uri);
                }
                w->uri = strdup(uri);
            }
        } else if (key != NULL && strcmp(key, "streams") == 0) {
            uint32_t node_id = 0;
            if (parse_streams(m, vcontents, &node_id) == 0 && node_id > 0) {
                if (g_debug) {
                    fprintf(stderr, "portal: node_id=%u\n", node_id);
                }
                w->node_id = node_id;
                w->node_id_set = true;
            }
        } else {
            (void)sd_bus_message_skip(m, NULL);
        }
 
        (void)sd_bus_message_exit_container(m); /* variant */
        (void)sd_bus_message_exit_container(m); /* dict entry */
    }
 
    (void)sd_bus_message_exit_container(m); /* array */
 
    w->done = true;
    return 0;
}
 
static int wait_for_portal_response(sd_bus *bus, struct portal_wait *w, uint64_t timeout_usec) {
    if (bus == NULL || w == NULL) {
        return -EINVAL;
    }
 
    uint64_t end = now_usec() + timeout_usec;
    while (!w->done) {
        int r = sd_bus_process(bus, NULL);
        if (r < 0) {
            return r;
        }
        if (r > 0) {
            continue;
        }
 
        uint64_t now = now_usec();
        if (now >= end) {
            return -ETIMEDOUT;
        }
 
        uint64_t remain = end - now;
        r = sd_bus_wait(bus, (uint64_t)remain);
        if (r < 0) {
            return r;
        }
    }
 
    return 0;
}

static int portal_screenshot(sd_bus *bus, const char *unique_part, uint64_t timeout_usec, char **out_uri) {
    if (bus == NULL || unique_part == NULL || out_uri == NULL) {
        return -EINVAL;
    }

    char token[64] = {0};
    snprintf(token, sizeof(token), "fbwl_screenshot_%ld", (long)getpid());

    char req[256] = {0};
    snprintf(req, sizeof(req), "/org/freedesktop/portal/desktop/request/%s/%s", unique_part, token);

    struct portal_wait w = {0};
    sd_bus_error error = SD_BUS_ERROR_NULL;

    sd_bus_slot *slot = NULL;
    char match[512] = {0};
    snprintf(match, sizeof(match),
        "type='signal',sender='org.freedesktop.portal.Desktop',path='%s',interface='org.freedesktop.portal.Request',member='Response'",
        req);
    int r = sd_bus_add_match(bus, &slot, match, portal_response_cb, &w);
    if (r < 0) {
        return r;
    }

    sd_bus_message *m = NULL;
    r = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Screenshot", "Screenshot");
    if (r < 0) {
        sd_bus_slot_unref(slot);
        return r;
    }

    r = sd_bus_message_append(m, "s", "");
    if (r < 0) {
        sd_bus_message_unref(m);
        sd_bus_slot_unref(slot);
        return r;
    }

    r = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) {
        sd_bus_message_unref(m);
        sd_bus_slot_unref(slot);
        return r;
    }

    r = append_dict_entry_string(m, "handle_token", token);
    if (r < 0) {
        sd_bus_message_unref(m);
        sd_bus_slot_unref(slot);
        return r;
    }

    r = append_dict_entry_bool(m, "interactive", false);
    if (r < 0) {
        sd_bus_message_unref(m);
        sd_bus_slot_unref(slot);
        return r;
    }

    r = sd_bus_message_close_container(m);
    if (r < 0) {
        sd_bus_message_unref(m);
        sd_bus_slot_unref(slot);
        return r;
    }

    sd_bus_message *reply = NULL;
    r = sd_bus_call(bus, m, (uint64_t)timeout_usec, &error, &reply);
    sd_bus_message_unref(m);
    if (r < 0) {
        fprintf(stderr, "Screenshot call failed: %s\n", error.message != NULL ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        sd_bus_slot_unref(slot);
        return r;
    }

    const char *handle = NULL;
    r = sd_bus_message_read(reply, "o", &handle);
    sd_bus_message_unref(reply);
    if (r < 0) {
        sd_bus_slot_unref(slot);
        return r;
    }
    if (handle == NULL || strcmp(handle, req) != 0) {
        fprintf(stderr, "Screenshot handle mismatch: expected=%s got=%s\n", req, handle != NULL ? handle : "(null)");
        sd_bus_slot_unref(slot);
        return -EBADMSG;
    }

    r = wait_for_portal_response(bus, &w, timeout_usec);
    sd_bus_slot_unref(slot);
    if (r < 0) {
        fprintf(stderr, "Screenshot response wait failed: %s\n", strerror(-r));
        free(w.uri);
        return r;
    }
    if (w.response != 0 || w.uri == NULL) {
        fprintf(stderr, "Screenshot failed: response=%u\n", w.response);
        free(w.uri);
        return -EIO;
    }

    *out_uri = w.uri;
    w.uri = NULL;
    return 0;
}

static int portal_screencast(sd_bus *bus, const char *unique_part, uint64_t timeout_usec, uint32_t *out_node_id) {
    if (bus == NULL || unique_part == NULL || out_node_id == NULL) {
        return -EINVAL;
    }
 
    char create_token[64] = {0};
    char select_token[64] = {0};
    char start_token[64] = {0};
    char session_token[64] = {0};
    snprintf(create_token, sizeof(create_token), "fbwl_create_%ld", (long)getpid());
    snprintf(select_token, sizeof(select_token), "fbwl_select_%ld", (long)getpid());
    snprintf(start_token, sizeof(start_token), "fbwl_start_%ld", (long)getpid());
    snprintf(session_token, sizeof(session_token), "fbwl_session_%ld", (long)getpid());
 
    char req_create[256] = {0};
    char req_select[256] = {0};
    char req_start[256] = {0};
    snprintf(req_create, sizeof(req_create), "/org/freedesktop/portal/desktop/request/%s/%s", unique_part, create_token);
    snprintf(req_select, sizeof(req_select), "/org/freedesktop/portal/desktop/request/%s/%s", unique_part, select_token);
    snprintf(req_start, sizeof(req_start), "/org/freedesktop/portal/desktop/request/%s/%s", unique_part, start_token);
 
    struct portal_wait w = {0};
    sd_bus_error error = SD_BUS_ERROR_NULL;
 
    /* CreateSession */
    sd_bus_slot *slot = NULL;
    char match[512] = {0};
    snprintf(match, sizeof(match),
        "type='signal',sender='org.freedesktop.portal.Desktop',path='%s',interface='org.freedesktop.portal.Request',member='Response'",
        req_create);
    int r = sd_bus_add_match(bus, &slot, match, portal_response_cb, &w);
    if (r < 0) {
        return r;
    }
 
    sd_bus_message *m = NULL;
    r = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast", "CreateSession");
    if (r < 0) {
        return r;
    }
 
    r = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) {
        return r;
    }
    r = append_dict_entry_string(m, "handle_token", create_token);
    if (r < 0) {
        return r;
    }
    r = append_dict_entry_string(m, "session_handle_token", session_token);
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
 
    sd_bus_message *reply = NULL;
    r = sd_bus_call(bus, m, (uint64_t)timeout_usec, &error, &reply);
    sd_bus_message_unref(m);
    if (r < 0) {
        fprintf(stderr, "CreateSession call failed: %s\n", error.message != NULL ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        return r;
    }
 
    const char *handle = NULL;
    r = sd_bus_message_read(reply, "o", &handle);
    sd_bus_message_unref(reply);
    if (r < 0) {
        return r;
    }
    if (handle == NULL || strcmp(handle, req_create) != 0) {
        fprintf(stderr, "CreateSession handle mismatch: expected=%s got=%s\n", req_create, handle != NULL ? handle : "(null)");
        return -EBADMSG;
    }
 
    r = wait_for_portal_response(bus, &w, timeout_usec);
    sd_bus_slot_unref(slot);
    if (r < 0) {
        fprintf(stderr, "CreateSession response wait failed: %s\n", strerror(-r));
        return r;
    }
    if (w.response != 0 || w.session_handle == NULL) {
        fprintf(stderr, "CreateSession failed: response=%u\n", w.response);
        return -EIO;
    }
 
    char *session_handle = w.session_handle;
    w.session_handle = NULL;
 
    /* SelectSources */
    memset(&w, 0, sizeof(w));
    slot = NULL;
    snprintf(match, sizeof(match),
        "type='signal',sender='org.freedesktop.portal.Desktop',path='%s',interface='org.freedesktop.portal.Request',member='Response'",
        req_select);
    r = sd_bus_add_match(bus, &slot, match, portal_response_cb, &w);
    if (r < 0) {
        free(session_handle);
        return r;
    }
 
    m = NULL;
    r = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast", "SelectSources");
    if (r < 0) {
        free(session_handle);
        return r;
    }
 
    r = sd_bus_message_append(m, "o", session_handle);
    if (r < 0) {
        free(session_handle);
        return r;
    }
 
    r = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) {
        free(session_handle);
        return r;
    }
    r = append_dict_entry_string(m, "handle_token", select_token);
    if (r < 0) {
        free(session_handle);
        return r;
    }
    r = append_dict_entry_u32(m, "types", 1);
    if (r < 0) {
        free(session_handle);
        return r;
    }
    r = append_dict_entry_bool(m, "multiple", false);
    if (r < 0) {
        free(session_handle);
        return r;
    }
    r = append_dict_entry_u32(m, "cursor_mode", 2);
    if (r < 0) {
        free(session_handle);
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        free(session_handle);
        return r;
    }
 
    sd_bus_error_free(&error);
    reply = NULL;
    r = sd_bus_call(bus, m, (uint64_t)timeout_usec, &error, &reply);
    sd_bus_message_unref(m);
    if (r < 0) {
        fprintf(stderr, "SelectSources call failed: %s\n", error.message != NULL ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        free(session_handle);
        return r;
    }
 
    handle = NULL;
    r = sd_bus_message_read(reply, "o", &handle);
    sd_bus_message_unref(reply);
    if (r < 0) {
        free(session_handle);
        return r;
    }
    if (handle == NULL || strcmp(handle, req_select) != 0) {
        fprintf(stderr, "SelectSources handle mismatch: expected=%s got=%s\n", req_select, handle != NULL ? handle : "(null)");
        free(session_handle);
        return -EBADMSG;
    }
 
    r = wait_for_portal_response(bus, &w, timeout_usec);
    sd_bus_slot_unref(slot);
    if (r < 0) {
        fprintf(stderr, "SelectSources response wait failed: %s\n", strerror(-r));
        free(session_handle);
        return r;
    }
    if (w.response != 0) {
        fprintf(stderr, "SelectSources failed: response=%u\n", w.response);
        free(session_handle);
        return -EIO;
    }
 
    /* Start */
    memset(&w, 0, sizeof(w));
    slot = NULL;
    snprintf(match, sizeof(match),
        "type='signal',sender='org.freedesktop.portal.Desktop',path='%s',interface='org.freedesktop.portal.Request',member='Response'",
        req_start);
    r = sd_bus_add_match(bus, &slot, match, portal_response_cb, &w);
    if (r < 0) {
        free(session_handle);
        return r;
    }
 
    m = NULL;
    r = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast", "Start");
    if (r < 0) {
        free(session_handle);
        return r;
    }
 
    r = sd_bus_message_append(m, "os", session_handle, "");
    if (r < 0) {
        free(session_handle);
        return r;
    }
 
    r = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) {
        free(session_handle);
        return r;
    }
    r = append_dict_entry_string(m, "handle_token", start_token);
    if (r < 0) {
        free(session_handle);
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        free(session_handle);
        return r;
    }
 
    sd_bus_error_free(&error);
    reply = NULL;
    r = sd_bus_call(bus, m, (uint64_t)timeout_usec, &error, &reply);
    sd_bus_message_unref(m);
    if (r < 0) {
        fprintf(stderr, "Start call failed: %s\n", error.message != NULL ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        free(session_handle);
        return r;
    }
 
    handle = NULL;
    r = sd_bus_message_read(reply, "o", &handle);
    sd_bus_message_unref(reply);
    if (r < 0) {
        free(session_handle);
        return r;
    }
    if (handle == NULL || strcmp(handle, req_start) != 0) {
        fprintf(stderr, "Start handle mismatch: expected=%s got=%s\n", req_start, handle != NULL ? handle : "(null)");
        free(session_handle);
        return -EBADMSG;
    }
 
    r = wait_for_portal_response(bus, &w, timeout_usec);
    sd_bus_slot_unref(slot);
    if (r < 0) {
        fprintf(stderr, "Start response wait failed: %s\n", strerror(-r));
        free(session_handle);
        return r;
    }
    if (w.response != 0 || !w.node_id_set || w.node_id == 0) {
        fprintf(stderr, "Start failed: response=%u node_id_set=%d node_id=%u\n", w.response, w.node_id_set ? 1 : 0, w.node_id);
        free(session_handle);
        return -EIO;
    }
 
    uint32_t node_id = w.node_id;
 
    /* Close the session (best-effort). */
    sd_bus_error_free(&error);
    (void)sd_bus_call_method(bus, "org.freedesktop.portal.Desktop", session_handle, "org.freedesktop.portal.Session",
        "Close", &error, NULL, "");
 
    free(session_handle);
    *out_node_id = node_id;
    return 0;
}
 
int main(int argc, char **argv) {
    if (getenv("FBWL_XDP_DEBUG") != NULL) {
        g_debug = true;
    }

    uint64_t timeout_ms = 10000;
    bool screenshot = false;
 
    static const struct option long_opts[] = {
        {"timeout-ms", required_argument, NULL, 't'},
        {"screenshot", no_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };
 
    int c;
    while ((c = getopt_long(argc, argv, "t:sh", long_opts, NULL)) != -1) {
        switch (c) {
        case 't':
            timeout_ms = (uint64_t)strtoull(optarg, NULL, 10);
            break;
        case 's':
            screenshot = true;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 2;
        }
    }
 
    sd_bus *bus = NULL;
    int r = sd_bus_default_user(&bus);
    if (r < 0) {
        fprintf(stderr, "failed to connect to user bus: %s\n", strerror(-r));
        return 1;
    }
 
    const char *unique = NULL;
    r = sd_bus_get_unique_name(bus, &unique);
    if (r < 0 || unique == NULL) {
        fprintf(stderr, "failed to get unique bus name: %s\n", r < 0 ? strerror(-r) : "null");
        sd_bus_unref(bus);
        return 1;
    }
 
    char *unique_part = bus_name_to_path_component(unique);
    if (unique_part == NULL) {
        fprintf(stderr, "failed to derive request path component\n");
        sd_bus_unref(bus);
        return 1;
    }
 
    if (screenshot) {
        char *uri = NULL;
        r = portal_screenshot(bus, unique_part, timeout_ms * 1000ULL, &uri);
        free(unique_part);
        sd_bus_unref(bus);
        if (r < 0) {
            return 1;
        }

        printf("%s\n", uri);
        free(uri);
        return 0;
    }

    uint32_t node_id = 0;
    r = portal_screencast(bus, unique_part, timeout_ms * 1000ULL, &node_id);
    free(unique_part);
    sd_bus_unref(bus);
    if (r < 0) {
        return 1;
    }
 
    printf("%u\n", node_id);
    return 0;
}
