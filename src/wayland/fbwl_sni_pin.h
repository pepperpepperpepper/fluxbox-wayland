#pragma once

#include <stddef.h>

#ifdef HAVE_SYSTEMD

struct fbwl_sni_item;
struct fbwl_sni_watcher;

// Fill `out` with non-passive SNI items ordered according to Fluxbox-style
// pinLeft/pinRight rules (see session.screenN.systray.pinLeft/pinRight).
// Ordering matches against StatusNotifierItem "Id" (case-insensitive).
size_t fbwl_sni_pin_order_items(const struct fbwl_sni_watcher *watcher, struct fbwl_sni_item **out, size_t out_cap,
    char *const *pin_left, size_t pin_left_len, char *const *pin_right, size_t pin_right_len);

#endif

