#ifndef NEVERC_NET_WEBSOCKET_INTERNAL_H
#define NEVERC_NET_WEBSOCKET_INTERNAL_H

#include "neverc/std/net/http.h"

#include <stddef.h>

int nc_ws_validate_http_upgrade(const neverc_http_request_t *request,
                                char *key, size_t key_capacity);

#endif
