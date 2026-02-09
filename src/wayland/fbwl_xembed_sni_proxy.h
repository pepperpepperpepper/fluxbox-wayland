#pragma once

struct fbwl_server;

void fbwl_xembed_sni_proxy_maybe_start(struct fbwl_server *server, const char *display_name);
void fbwl_xembed_sni_proxy_stop(struct fbwl_server *server);

