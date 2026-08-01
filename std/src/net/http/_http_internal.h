#ifndef NEVERC_HTTP_INTERNAL_H
#define NEVERC_HTTP_INTERNAL_H

#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include "../_net_internal.h"
#include <stdarg.h>
#include <time.h>

#ifndef _WIN32
#include <strings.h>
#else
static inline int strcasecmp(const char *a, const char *b) {
    return _stricmp(a, b);
}
static inline int strncasecmp(const char *a, const char *b, size_t n) {
    return _strnicmp(a, b, n);
}
#endif

#define HTTP_MAX_HEADERS    64
#define HTTP_INITIAL_BUFSZ  4096

typedef struct http_conn http_conn_t;

struct neverc_http_response_writer {
    nc_sock_t   fd;
    int         status;
    int         headers_sent;
    int         chunked;
    char       *header_names[HTTP_MAX_HEADERS];
    char       *header_values[HTTP_MAX_HEADERS];
    int         nheaders;
    nc_buf_t    body;
    int         keep_alive;
    int         hijacked;
    http_conn_t *owner;
    size_t      request_consumed;
    size_t      request_body_len;
    int         body_limit_exceeded;
    int         gzip_enabled;
    int         gzip_level;
    size_t      gzip_min_size;
    int         accepts_gzip;
};

int nc_http_sock_write_all(nc_sock_t fd, const void *data, size_t len);

extern neverc_http_cors_config_t g_cors_config;
extern int g_cors_enabled;

static inline char *nc_strndup_safe(const char *s, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

#endif /* NEVERC_HTTP_INTERNAL_H */
