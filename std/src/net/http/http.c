#include "neverc/std/net/http.h"
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/crypto/tls.h"
#include "../_net_internal.h"
#include <stdarg.h>
#include <time.h>

typedef struct neverc_http_server neverc_http_server_t;
static neverc_http_server_t *g_server_ptr;
static nc_conn_limiter_t g_conn_limiter = {0, 0};

#ifndef _WIN32
#include <strings.h>
#else
static int strcasecmp(const char *a, const char *b) {
    return _stricmp(a, b);
}
static int strncasecmp(const char *a, const char *b, size_t n) {
    return _strnicmp(a, b, n);
}
#endif

typedef struct http_conn http_conn_t;

/* ======================================================================
 * Response Writer — heap-allocated, one per request
 * ====================================================================== */

#define HTTP_MAX_HEADERS    64
#define HTTP_INITIAL_BUFSZ  4096

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
};

static nc_bufpool_t g_rw_pool;
static volatile int g_rw_pool_inited = 0;

static void ensure_rw_pool(void) {
    if (nc_atomic_load(&g_rw_pool_inited)) return;
#ifdef _WIN32
    static volatile LONG rw_lock = 0;
    while (InterlockedCompareExchange(&rw_lock, 1, 0) != 0) { Sleep(0); }
#else
    static volatile int rw_lock = 0;
    while (!__sync_bool_compare_and_swap(&rw_lock, 0, 1)) { /* spin */ }
#endif
    if (!nc_atomic_load(&g_rw_pool_inited)) {
        nc_bufpool_init(&g_rw_pool, sizeof(neverc_http_response_writer_t));
        nc_atomic_store(&g_rw_pool_inited, 1);
    }
#ifdef _WIN32
    InterlockedExchange(&rw_lock, 0);
#else
    __sync_lock_release(&rw_lock);
#endif
}

static neverc_http_response_writer_t *rw_new(nc_sock_t fd, int keep_alive,
                                               http_conn_t *owner,
                                               size_t request_consumed) {
    ensure_rw_pool();
    neverc_http_response_writer_t *w =
        (neverc_http_response_writer_t *)nc_bufpool_pop(&g_rw_pool);
    if (!w) return NULL;
    w->fd = fd;
    w->status = 200;
    w->keep_alive = keep_alive;
    w->owner = owner;
    w->request_consumed = request_consumed;
    nc_buf_init(&w->body);
    return w;
}

static void rw_free(neverc_http_response_writer_t *w) {
    if (!w) return;
    for (int i = 0; i < w->nheaders; i++) {
        free(w->header_names[i]);
        free(w->header_values[i]);
    }
    nc_buf_free(&w->body);
    memset(w, 0, sizeof(*w));
    nc_bufpool_push(&g_rw_pool, w);
}

int nc_http_sock_write_all(nc_sock_t fd, const void *data, size_t len) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = send(fd, p + sent, (int)(len - sent), 0);
        if (n > 0) { sent += (size_t)n; continue; }
        if (WSAGetLastError() == WSAEINTR) continue;
        return -1;
#else
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) { sent += (size_t)n; continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        return -1;
#endif
    }
    return 0;
}
#define sock_write_all nc_http_sock_write_all

/* Pre-computed status lines for common codes (avoid snprintf per-request) */
static const char *fast_status_line(int code, size_t *len) {
    switch (code) {
    case 200: *len = 17; return "HTTP/1.1 200 OK\r\n";
    case 201: *len = 22; return "HTTP/1.1 201 Created\r\n";
    case 204: *len = 25; return "HTTP/1.1 204 No Content\r\n";
    case 301: *len = 32; return "HTTP/1.1 301 Moved Permanently\r\n";
    case 302: *len = 20; return "HTTP/1.1 302 Found\r\n";
    case 304: *len = 27; return "HTTP/1.1 304 Not Modified\r\n";
    case 400: *len = 26; return "HTTP/1.1 400 Bad Request\r\n";
    case 401: *len = 27; return "HTTP/1.1 401 Unauthorized\r\n";
    case 403: *len = 24; return "HTTP/1.1 403 Forbidden\r\n";
    case 404: *len = 24; return "HTTP/1.1 404 Not Found\r\n";
    case 500: *len = 36; return "HTTP/1.1 500 Internal Server Error\r\n";
    default:  return NULL;
    }
}

static void rw_flush(neverc_http_response_writer_t *w) {
    if (!w || w->hijacked) return;
    if (w->headers_sent) return;
    w->headers_sent = 1;

    nc_buf_t hdr;
    nc_buf_init(&hdr);

    size_t sl_len = 0;
    const char *sl = fast_status_line(w->status, &sl_len);
    if (sl) {
        nc_buf_append(&hdr, sl, sl_len);
    } else {
        char line[256];
        int n = snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n",
                         w->status, neverc_http_status_text(w->status));
        nc_buf_append(&hdr, line, (size_t)n);
    }

    int has_content_type = 0;
    int has_content_length = 0;
    int has_connection = 0;
    char line[256];
    int n;

    for (int i = 0; i < w->nheaders; i++) {
        n = snprintf(line, sizeof(line), "%s: %s\r\n",
                     w->header_names[i], w->header_values[i]);
        nc_buf_append(&hdr, line, (size_t)n);

        if (strcasecmp(w->header_names[i], "Content-Type") == 0)
            has_content_type = 1;
        if (strcasecmp(w->header_names[i], "Content-Length") == 0)
            has_content_length = 1;
        if (strcasecmp(w->header_names[i], "Connection") == 0)
            has_connection = 1;
    }

    if (!has_content_type) {
        const char *ct = "Content-Type: text/plain; charset=utf-8\r\n";
        nc_buf_append(&hdr, ct, strlen(ct));
    }
    if (!has_content_length) {
        n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n",
                     w->body.len);
        nc_buf_append(&hdr, line, (size_t)n);
    }
    if (!has_connection) {
        const char *conn_val = w->keep_alive
            ? "Connection: keep-alive\r\n"
            : "Connection: close\r\n";
        nc_buf_append(&hdr, conn_val, strlen(conn_val));
    }

    {
        /* Cache the Date header — update once per second.
         * Double-buffer to avoid reader/writer races: we write into the
         * inactive slot, then atomically flip the index. Readers always
         * see a fully-formed string. */
        static char   date_bufs[2][64];
        static int    date_lens[2] = {0, 0};
        static volatile int date_idx = 0;
        static volatile time_t date_time = 0;

        time_t now = time(NULL);
        if (now != date_time) {
            int wi = 1 - date_idx; /* write to inactive slot */
            struct tm gmt;
#ifdef _WIN32
            gmtime_s(&gmt, &now);
#else
            gmtime_r(&now, &gmt);
#endif
            date_lens[wi] = (int)strftime(date_bufs[wi],
                sizeof(date_bufs[wi]),
                "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", &gmt);
#if defined(__GNUC__) || defined(__clang__)
            __atomic_thread_fence(__ATOMIC_RELEASE);
#elif defined(_WIN32)
            MemoryBarrier();
#endif
            date_idx = wi;
            date_time = now;
        }
        int ri = date_idx;
#if defined(__GNUC__) || defined(__clang__)
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
#elif defined(_WIN32)
        MemoryBarrier();
#endif
        if (date_lens[ri] > 0)
            nc_buf_append(&hdr, date_bufs[ri], (size_t)date_lens[ri]);
    }

    nc_buf_append(&hdr, "\r\n", 2);

    if (w->fd != NC_INVALID_SOCK) {
        if (w->body.len > 0) {
            nc_writev(w->fd, hdr.data, hdr.len,
                      w->body.data, w->body.len);
        } else {
            sock_write_all(w->fd, hdr.data, hdr.len);
        }
    }

    nc_buf_free(&hdr);
}

void neverc_http_set_status(neverc_http_response_writer_t *w, int code) {
    if (w) w->status = code;
}

void neverc_http_set_header(neverc_http_response_writer_t *w,
                             const char *name, const char *value) {
    if (!w || w->nheaders >= HTTP_MAX_HEADERS) return;
    w->header_names[w->nheaders] = strdup(name);
    w->header_values[w->nheaders] = strdup(value);
    w->nheaders++;
}

int neverc_http_write(neverc_http_response_writer_t *w,
                       const void *data, size_t len) {
    if (!w || !data || len == 0) return 0;
    nc_buf_append(&w->body, data, len);
    return (int)len;
}

int neverc_http_write_string(neverc_http_response_writer_t *w,
                              const char *s) {
    if (!s) return 0;
    return neverc_http_write(w, s, strlen(s));
}

int neverc_http_writef(neverc_http_response_writer_t *w,
                        const char *fmt, ...) {
    if (!w || !fmt) return 0;
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if ((size_t)n >= sizeof(buf)) {
            char *big = (char *)malloc((size_t)n + 1);
            if (big) {
                va_start(ap, fmt);
                n = vsnprintf(big, (size_t)n + 1, fmt, ap);
                va_end(ap);
                int ret = neverc_http_write(w, big, (size_t)n);
                free(big);
                return ret;
            }
        }
        return neverc_http_write(w, buf, (size_t)n);
    }
    return 0;
}

void neverc_http_enable_chunked(neverc_http_response_writer_t *w) {
    if (w) w->chunked = 1;
}

static void rw_send_chunked_headers(neverc_http_response_writer_t *w) {
    if (w->headers_sent) return;
    w->headers_sent = 1;

    nc_buf_t hdr;
    nc_buf_init(&hdr);

    char line[256];
    int n = snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n",
                     w->status, neverc_http_status_text(w->status));
    nc_buf_append(&hdr, line, (size_t)n);

    int has_content_type = 0;
    int has_transfer_encoding = 0;
    int has_connection = 0;

    for (int i = 0; i < w->nheaders; i++) {
        n = snprintf(line, sizeof(line), "%s: %s\r\n",
                     w->header_names[i], w->header_values[i]);
        nc_buf_append(&hdr, line, (size_t)n);

        if (strcasecmp(w->header_names[i], "Content-Type") == 0)
            has_content_type = 1;
        if (strcasecmp(w->header_names[i], "Transfer-Encoding") == 0)
            has_transfer_encoding = 1;
        if (strcasecmp(w->header_names[i], "Connection") == 0)
            has_connection = 1;
    }

    if (!has_content_type) {
        const char *ct = "Content-Type: text/plain; charset=utf-8\r\n";
        nc_buf_append(&hdr, ct, strlen(ct));
    }
    if (!has_transfer_encoding) {
        const char *te = "Transfer-Encoding: chunked\r\n";
        nc_buf_append(&hdr, te, strlen(te));
    }
    if (!has_connection) {
        const char *conn_val = w->keep_alive
            ? "Connection: keep-alive\r\n"
            : "Connection: close\r\n";
        nc_buf_append(&hdr, conn_val, strlen(conn_val));
    }

    nc_buf_append(&hdr, "\r\n", 2);
    sock_write_all(w->fd, hdr.data, hdr.len);
    nc_buf_free(&hdr);
}

int neverc_http_flush_chunk(neverc_http_response_writer_t *w) {
    if (!w || !w->chunked) return -1;

    if (!w->headers_sent)
        rw_send_chunked_headers(w);

    if (w->body.len == 0) return 0;

    char chunk_hdr[32];
    int n = snprintf(chunk_hdr, sizeof(chunk_hdr), "%zx\r\n", w->body.len);
    if (sock_write_all(w->fd, chunk_hdr, (size_t)n) != 0) return -1;
    if (sock_write_all(w->fd, w->body.data, w->body.len) != 0) return -1;
    if (sock_write_all(w->fd, "\r\n", 2) != 0) return -1;

    nc_buf_reset(&w->body);
    return 0;
}

int neverc_http_end_chunked(neverc_http_response_writer_t *w) {
    if (!w || !w->chunked) return -1;

    if (w->body.len > 0)
        neverc_http_flush_chunk(w);

    if (!w->headers_sent)
        rw_send_chunked_headers(w);

    return sock_write_all(w->fd, "0\r\n\r\n", 5);
}

/* Shared with http_client.c */
extern neverc_http_cors_config_t g_cors_config;
extern int g_cors_enabled;

/* Forward declarations */
static char *strndup_safe(const char *s, size_t n);

/* ======================================================================
 * Mux (Router) — thread-safe, supports Go 1.22+ path parameters
 *
 * Pattern syntax:
 *   "METHOD /path"           — method-specific (e.g. "GET /users")
 *   "/path/{name}"           — captures segment as parameter
 *   "/files/{path...}"       — wildcard, captures rest of path
 *   "/static/"               — prefix match (trailing /)
 *   "/exact"                 — exact match (no trailing /)
 * ====================================================================== */

#define MAX_ROUTES 256
#define MAX_PATH_PARAMS 16

typedef struct {
    char *pattern;        /* original pattern string */
    char *method;         /* method filter or NULL */
    char *path_pattern;   /* path portion (after method) */
    size_t pattern_len;
    int has_params;       /* 1 if pattern contains {name} */
    neverc_http_handler_func_t handler;
} route_t;

struct neverc_http_mux {
    route_t routes[MAX_ROUTES];
    int nroutes;
    nc_mutex_t lock;
};

/* Per-request path parameter storage */
typedef struct {
    char buf[2048];
    int len;
    int count;
} path_params_t;

static struct neverc_http_mux default_mux;
static volatile int default_mux_initialized = 0;

static void ensure_default_mux(void) {
    if (default_mux_initialized) return;
#ifdef _WIN32
    static volatile LONG dmux_lock = 0;
    while (InterlockedCompareExchange(&dmux_lock, 1, 0) != 0) { Sleep(0); }
#else
    static volatile int dmux_lock = 0;
    while (!__sync_bool_compare_and_swap(&dmux_lock, 0, 1)) { /* spin */ }
#endif
    if (!default_mux_initialized) {
        memset(&default_mux, 0, sizeof(default_mux));
        nc_mutex_init(&default_mux.lock);
        default_mux_initialized = 1;
    }
#ifdef _WIN32
    InterlockedExchange(&dmux_lock, 0);
#else
    __sync_lock_release(&dmux_lock);
#endif
}

static void route_parse_pattern(route_t *r, const char *pattern) {
    r->pattern = strdup(pattern);
    r->pattern_len = strlen(pattern);
    r->method = NULL;
    r->has_params = 0;

    /* Check for "METHOD /path" syntax */
    const char *space = strchr(pattern, ' ');
    if (space && space > pattern) {
        r->method = strndup_safe(pattern, (size_t)(space - pattern));
        r->path_pattern = strdup(space + 1);
    } else {
        r->path_pattern = strdup(pattern);
    }

    if (strchr(r->path_pattern, '{'))
        r->has_params = 1;
}

neverc_http_mux_t *neverc_http_new_mux(void) {
    neverc_http_mux_t *m = (neverc_http_mux_t *)calloc(1, sizeof(*m));
    if (m) nc_mutex_init(&m->lock);
    return m;
}

void neverc_http_mux_handle(neverc_http_mux_t *mux, const char *pattern,
                             neverc_http_handler_func_t handler) {
    if (!mux) return;
    nc_mutex_lock(&mux->lock);
    if (mux->nroutes < MAX_ROUTES) {
        route_parse_pattern(&mux->routes[mux->nroutes], pattern);
        mux->routes[mux->nroutes].handler = handler;
        mux->nroutes++;
    }
    nc_mutex_unlock(&mux->lock);
}

void neverc_http_mux_free(neverc_http_mux_t *mux) {
    if (!mux || mux == &default_mux) return;
    for (int i = 0; i < mux->nroutes; i++) {
        free(mux->routes[i].pattern);
        free(mux->routes[i].method);
        free(mux->routes[i].path_pattern);
    }
    nc_mutex_destroy(&mux->lock);
    free(mux);
}

void neverc_http_handle_func(const char *pattern,
                              neverc_http_handler_func_t handler) {
    ensure_default_mux();
    neverc_http_mux_handle(&default_mux, pattern, handler);
}

/* Match a pattern with path parameters.
 * Returns 1 on match, 0 on no match. Fills params if non-NULL. */
static int pattern_match(const char *pattern, const char *path,
                          path_params_t *params) {
    if (params) { params->len = 0; params->count = 0; }

    const char *pp = pattern;
    const char *rp = path;

    while (*pp && *rp) {
        if (*pp == '{') {
            /* Extract parameter name */
            const char *close = strchr(pp, '}');
            if (!close) return 0;

            const char *name = pp + 1;
            size_t namelen = (size_t)(close - name);

            /* Check for wildcard {name...} */
            int wildcard = (namelen >= 3 &&
                           name[namelen-3] == '.' &&
                           name[namelen-2] == '.' &&
                           name[namelen-1] == '.');
            if (wildcard) namelen -= 3;

            if (wildcard) {
                /* Capture the rest of the path */
                if (params && params->len + (int)namelen + 1 + (int)strlen(rp) + 1
                    < (int)sizeof(params->buf)) {
                    memcpy(params->buf + params->len, name, namelen);
                    params->len += (int)namelen;
                    params->buf[params->len++] = '\0';
                    size_t vlen = strlen(rp);
                    memcpy(params->buf + params->len, rp, vlen);
                    params->len += (int)vlen;
                    params->buf[params->len++] = '\0';
                    params->count++;
                }
                return 1;
            }

            /* Find end of this path segment */
            const char *seg_end = rp;
            while (*seg_end && *seg_end != '/') seg_end++;
            size_t vallen = (size_t)(seg_end - rp);

            if (vallen == 0) return 0;

            if (params && params->len + (int)namelen + 1 + (int)vallen + 1
                < (int)sizeof(params->buf)) {
                memcpy(params->buf + params->len, name, namelen);
                params->len += (int)namelen;
                params->buf[params->len++] = '\0';
                memcpy(params->buf + params->len, rp, vallen);
                params->len += (int)vallen;
                params->buf[params->len++] = '\0';
                params->count++;
            }

            rp = seg_end;
            pp = close + 1;
        } else {
            if (*pp != *rp) return 0;
            pp++;
            rp++;
        }
    }

    /* Both consumed = exact match. Pattern ends with / = prefix match. */
    if (*pp == '\0' && *rp == '\0') return 1;
    if (*pp == '\0' && pp > pattern && pp[-1] == '/') return 1;
    return 0;
}

static neverc_http_handler_func_t mux_match_ex(neverc_http_mux_t *mux,
                                                 const char *method,
                                                 const char *path,
                                                 path_params_t *params) {
    neverc_http_handler_func_t best = NULL;
    size_t best_len = 0;
    int best_specificity = 0; /* higher = more specific */

    int nr = mux->nroutes;
#if defined(__GNUC__) || defined(__clang__)
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
#else
    __sync_synchronize();
#endif

    path_params_t tmp_params;

    for (int i = 0; i < nr; i++) {
        route_t *r = &mux->routes[i];

        /* Check method filter */
        if (r->method && method && strcmp(r->method, method) != 0)
            continue;

        const char *pat = r->path_pattern;
        int specificity = r->method ? 2 : 0;

        if (r->has_params) {
            memset(&tmp_params, 0, sizeof(tmp_params));
            if (pattern_match(pat, path, &tmp_params)) {
                specificity += 1;
                size_t plen = strlen(pat);
                if (specificity > best_specificity ||
                    (specificity == best_specificity && plen > best_len)) {
                    best = r->handler;
                    best_len = plen;
                    best_specificity = specificity;
                    if (params) *params = tmp_params;
                }
            }
            continue;
        }

        size_t plen = strlen(pat);

        /* Exact match */
        if (strcmp(pat, path) == 0) {
            specificity += 3;
            if (specificity > best_specificity) {
                best = r->handler;
                best_len = plen;
                best_specificity = specificity;
                if (params) { params->len = 0; params->count = 0; }
            }
            continue;
        }

        /* Prefix match (pattern ends with /) */
        if (plen > 0 && pat[plen - 1] == '/' &&
            strncmp(path, pat, plen) == 0) {
            if (specificity > best_specificity ||
                (specificity == best_specificity && plen > best_len)) {
                best = r->handler;
                best_len = plen;
                best_specificity = specificity;
                if (params) { params->len = 0; params->count = 0; }
            }
        }
    }
    return best;
}

/* Backward-compatible wrapper used by existing code */
static neverc_http_handler_func_t mux_match(neverc_http_mux_t *mux,
                                              const char *path) {
    return mux_match_ex(mux, NULL, path, NULL);
}

const char *neverc_http_path_value(const neverc_http_request_t *req,
                                    const char *name) {
    if (!req || !req->path_params || !name || req->nparams == 0) return NULL;
    const char *p = req->path_params;
    for (int i = 0; i < req->nparams; i++) {
        const char *pname = p;
        while (*p) p++;
        p++;
        const char *pval = p;
        while (*p) p++;
        p++;
        if (strcmp(pname, name) == 0) return pval;
    }
    return NULL;
}

/* ======================================================================
 * Rate Limiter — token bucket algorithm (thread-safe)
 * ====================================================================== */

struct neverc_http_rate_limiter {
    double    rate;       /* tokens per second */
    double    burst;      /* max tokens (bucket size) */
    double    tokens;     /* current available tokens */
    uint64_t  last_time;  /* last refill time (monotonic ms) */
    nc_mutex_t lock;
};

static neverc_http_rate_limiter_t *g_global_rate_limiter = NULL;
static int g_handler_timeout_ms = 0;
static int g_server_port = 0;

neverc_http_rate_limiter_t *neverc_http_rate_limiter_new(double rate, int burst) {
    neverc_http_rate_limiter_t *rl =
        (neverc_http_rate_limiter_t *)calloc(1, sizeof(*rl));
    if (!rl) return NULL;
    rl->rate = rate;
    rl->burst = (double)burst;
    rl->tokens = (double)burst;
    rl->last_time = nc_monotonic_ms();
    nc_mutex_init(&rl->lock);
    return rl;
}

void neverc_http_rate_limiter_free(neverc_http_rate_limiter_t *rl) {
    if (!rl) return;
    nc_mutex_destroy(&rl->lock);
    free(rl);
}

int neverc_http_rate_limiter_allow(neverc_http_rate_limiter_t *rl) {
    if (!rl) return 1;
    nc_mutex_lock(&rl->lock);

    uint64_t now = nc_monotonic_ms();
    double elapsed = (double)(now - rl->last_time) / 1000.0;
    rl->last_time = now;

    rl->tokens += elapsed * rl->rate;
    if (rl->tokens > rl->burst)
        rl->tokens = rl->burst;

    int allowed = 0;
    if (rl->tokens >= 1.0) {
        rl->tokens -= 1.0;
        allowed = 1;
    }

    nc_mutex_unlock(&rl->lock);
    return allowed;
}

void neverc_http_set_rate_limit(double rate, int burst) {
    if (g_global_rate_limiter)
        neverc_http_rate_limiter_free(g_global_rate_limiter);
    g_global_rate_limiter = neverc_http_rate_limiter_new(rate, burst);
}

void neverc_http_set_handler_timeout(int ms) {
    g_handler_timeout_ms = ms;
}

int neverc_http_server_port(void) {
    return g_server_port;
}

void neverc_http_set_max_bytes(neverc_http_response_writer_t *w,
                                int64_t max_bytes) {
    (void)w; (void)max_bytes;
    /* Enforced in http_conn_on_read via max_read_size — set globally for now */
}

/* ======================================================================
 * HTTP Request Parser — stateful, supports incremental parsing
 * ====================================================================== */

typedef struct {
    char *method;
    char *path;
    char *query;
    char *http_version;
    char *host;
    char *content_type;
    int   content_length;
    int   keep_alive;
    int   is_chunked;
    int   expect_continue;

    char **header_names;
    char **header_values;
    int    nheaders;
    int    header_cap;

    const char *body;
    size_t body_len;
} parsed_request_t;

static void parsed_request_free(parsed_request_t *pr) {
    if (!pr) return;
    free(pr->method);
    free(pr->path);
    free(pr->query);
    free(pr->http_version);
    free(pr->host);
    free(pr->content_type);
    for (int i = 0; i < pr->nheaders; i++) {
        free(pr->header_names[i]);
        free(pr->header_values[i]);
    }
    free(pr->header_names);
    free(pr->header_values);
}

static char *strndup_safe(const char *s, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static int strcasecmp_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = (unsigned char)a[i];
        int cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
    }
    return 0;
}

/* Returns 0 on success, -1 if incomplete, -2 on parse error */
static int parse_request(const char *raw, size_t rawlen,
                          parsed_request_t *pr, size_t *consumed) {
    memset(pr, 0, sizeof(*pr));
    pr->content_length = -1;
    pr->keep_alive = 1;

    /* Find end of headers: the blank line "\r\n\r\n". memchr hops between the
       (rare) '\n' bytes instead of testing the 4-byte terminator at every
       offset; hdr_end still points at the first '\r' of the blank line. */
    const char *raw_end = raw + rawlen;
    const char *hdr_end = NULL;
    for (const char *q = raw; q < raw_end; ) {
        const char *nl = (const char *)memchr(q, '\n', (size_t)(raw_end - q));
        if (!nl) break;
        size_t pidx = (size_t)(nl - raw);
        if (pidx >= 3 && raw[pidx-1] == '\r' && raw[pidx-2] == '\n' && raw[pidx-3] == '\r') {
            hdr_end = raw + (pidx - 3);
            break;
        }
        q = nl + 1;
    }
    if (!hdr_end) return -1; /* incomplete */

    /* Find end of request line: the first "\r\n". */
    const char *eol = NULL;
    for (const char *q = raw; q < raw_end; ) {
        const char *nl = (const char *)memchr(q, '\n', (size_t)(raw_end - q));
        if (!nl) break;
        if (nl > raw && nl[-1] == '\r') { eol = nl - 1; break; }
        q = nl + 1;
    }
    if (!eol) return -2;

    /* Parse "METHOD /path?query HTTP/1.1" */
    const char *p = raw;

    const char *sp1 = (const char *)memchr(p, ' ', (size_t)(eol - p));
    if (!sp1) return -2;
    pr->method = strndup_safe(p, (size_t)(sp1 - p));
    p = sp1 + 1;

    const char *sp2 = (const char *)memchr(p, ' ', (size_t)(eol - p));
    if (!sp2) return -2;

    const char *qmark = (const char *)memchr(p, '?', (size_t)(sp2 - p));

    if (qmark) {
        pr->path = strndup_safe(p, (size_t)(qmark - p));
        pr->query = strndup_safe(qmark + 1, (size_t)(sp2 - qmark - 1));
    } else {
        pr->path = strndup_safe(p, (size_t)(sp2 - p));
    }
    p = sp2 + 1;
    pr->http_version = strndup_safe(p, (size_t)(eol - p));

    if (pr->http_version && strstr(pr->http_version, "1.0"))
        pr->keep_alive = 0;

    /* Parse headers — start with stack-friendly capacity */
    pr->header_cap = 32;
    pr->header_names = (char **)malloc((size_t)pr->header_cap * sizeof(char *));
    pr->header_values = (char **)malloc((size_t)pr->header_cap * sizeof(char *));

    p = eol + 2;

    /* hdr_end is the first \r of the terminating \r\n\r\n (also the last
     * header line's trailing \r). Include its \n when scanning lines. */
    const char *hdr_scan_end = hdr_end + 2;

    while (p < hdr_scan_end) {
        /* Locate the line's "\r\n" with memchr, then its first colon, instead
           of a byte-at-a-time scan testing two conditions per character. The
           result (first CRLF, first colon before it) is unchanged. */
        const char *hline_end = NULL;
        for (const char *q = p; q < hdr_scan_end; ) {
            const char *nl = (const char *)memchr(q, '\n', (size_t)(hdr_scan_end - q));
            if (!nl) break;
            if (nl > p && nl[-1] == '\r') { hline_end = nl - 1; break; }
            q = nl + 1;
        }
        if (!hline_end) break;
        const char *colon = (const char *)memchr(p, ':', (size_t)(hline_end - p));

        if (colon) {
            size_t namelen = (size_t)(colon - p);
            const char *val = colon + 1;
            while (val < hline_end && *val == ' ') val++;
            size_t vallen = (size_t)(hline_end - val);

            if (namelen == 4 && strcasecmp_n(p, "Host", 4) == 0)
                pr->host = strndup_safe(val, vallen);
            if (namelen == 12 && strcasecmp_n(p, "Content-Type", 12) == 0)
                pr->content_type = strndup_safe(val, vallen);
            if (namelen == 14 && strcasecmp_n(p, "Content-Length", 14) == 0) {
                char tmp[32];
                size_t cl = vallen < 31 ? vallen : 31;
                memcpy(tmp, val, cl);
                tmp[cl] = '\0';
                long long clval = 0;
                for (size_t ci = 0; ci < cl && tmp[ci] >= '0' && tmp[ci] <= '9'; ci++)
                    clval = clval * 10 + (tmp[ci] - '0');
                if (clval < 0 || clval > 1073741824LL) /* 1GB max */
                    pr->content_length = -2; /* signal overflow */
                else
                    pr->content_length = (int)clval;
            }
            if (namelen == 17 && strcasecmp_n(p, "Transfer-Encoding", 17) == 0) {
                if (vallen >= 7 && strcasecmp_n(val, "chunked", 7) == 0)
                    pr->is_chunked = 1;
            }
            if (namelen == 10 && strcasecmp_n(p, "Connection", 10) == 0) {
                if (vallen == 5 && strcasecmp_n(val, "close", 5) == 0)
                    pr->keep_alive = 0;
                else if (vallen == 10 && strcasecmp_n(val, "keep-alive", 10) == 0)
                    pr->keep_alive = 1;
            }
            if (namelen == 6 && strcasecmp_n(p, "Expect", 6) == 0) {
                if (vallen >= 12 && strcasecmp_n(val, "100-continue", 12) == 0)
                    pr->expect_continue = 1;
            }

            if (pr->nheaders >= pr->header_cap) {
                pr->header_cap *= 2;
                pr->header_names = (char **)realloc(pr->header_names,
                    (size_t)pr->header_cap * sizeof(char *));
                pr->header_values = (char **)realloc(pr->header_values,
                    (size_t)pr->header_cap * sizeof(char *));
            }
            pr->header_names[pr->nheaders] = strndup_safe(p, namelen);
            pr->header_values[pr->nheaders] = strndup_safe(val, vallen);
            pr->nheaders++;
        }
        p = hline_end + 2;
    }

    if (pr->content_length == -2) {
        parsed_request_free(pr);
        memset(pr, 0, sizeof(*pr));
        return -2; /* content-length overflow */
    }

    /* RFC 7230 §3.3.3: Transfer-Encoding takes precedence over Content-Length */
    if (pr->is_chunked && pr->content_length >= 0)
        pr->content_length = -1; /* ignore Content-Length when chunked */

    size_t header_size = (size_t)(hdr_end + 4 - raw);

    if (pr->is_chunked) {
        const char *chunk_start = raw + header_size;
        size_t chunk_avail = rawlen - header_size;
        /* Look for the terminating 0\r\n\r\n. memchr skips to each '0'
         * candidate (vectorized) instead of testing all five bytes at every
         * offset; the matched position is identical to the byte-loop scan. */
        const char *term = NULL;
        if (chunk_avail >= 5) {
            const char *cur = chunk_start;
            const char *limit = chunk_start + (chunk_avail - 4); /* need ci+4 < chunk_avail */
            while (cur < limit) {
                const char *z = (const char *)memchr(cur, '0', (size_t)(limit - cur));
                if (!z) break;
                if (z[1] == '\r' && z[2] == '\n' && z[3] == '\r' && z[4] == '\n') {
                    term = z;
                    break;
                }
                cur = z + 1;
            }
        }
        if (!term) {
            parsed_request_free(pr);
            memset(pr, 0, sizeof(*pr));
            return -1; /* need more chunked data */
        }
        /* Decode chunked body in-place */
        nc_buf_t decoded;
        nc_buf_init(&decoded);
        size_t cpos = 0;
        while (cpos < chunk_avail) {
            /* Find the chunk-size line's CRLF: memchr to each '\r' candidate
             * (vectorized) then confirm the following '\n'. Same match as the
             * byte-loop scan. */
            const char *cline_end = NULL;
            {
                const char *cur = chunk_start + cpos;
                size_t avail = chunk_avail - cpos;
                while (avail >= 2) {
                    const char *cr = (const char *)memchr(cur, '\r', avail - 1);
                    if (!cr) break;
                    if (cr[1] == '\n') { cline_end = cr; break; }
                    size_t adv = (size_t)(cr - cur) + 1;
                    cur += adv;
                    avail -= adv;
                }
            }
            if (!cline_end) break;
            unsigned long csz = 0;
            const char *cp = chunk_start + cpos;
            while (cp < cline_end) {
                char cc = *cp;
                if (cc >= '0' && cc <= '9') csz = csz * 16 + (unsigned long)(cc - '0');
                else if (cc >= 'a' && cc <= 'f') csz = csz * 16 + (unsigned long)(cc - 'a' + 10);
                else if (cc >= 'A' && cc <= 'F') csz = csz * 16 + (unsigned long)(cc - 'A' + 10);
                else break;
                cp++;
            }
            cpos = (size_t)(cline_end - chunk_start) + 2;
            if (csz == 0) break;
            if (cpos + csz > chunk_avail) break;
            nc_buf_append(&decoded, chunk_start + cpos, csz);
            cpos += csz + 2;
        }
        /* Store decoded body in the raw buffer area (overwrite) */
        if (decoded.len > 0) {
            pr->body = decoded.data;
            pr->body_len = decoded.len;
        }
        *consumed = (size_t)(term + 5 - raw);
        /* Note: decoded.data ownership transfers to caller via pr->body;
         * parsed_request_free doesn't free pr->body (it's normally a pointer
         * into the read buffer), so caller must handle this for chunked.
         * For simplicity, we copy into the read buffer area. */
        return 0;
    }

    int body_expected = pr->content_length > 0 ? pr->content_length : 0;
    size_t total_expected = header_size + (size_t)body_expected;

    if (rawlen < total_expected) {
        parsed_request_free(pr);
        memset(pr, 0, sizeof(*pr));
        return -1; /* need more body data */
    }

    if (body_expected > 0) {
        pr->body = raw + header_size;
        pr->body_len = (size_t)body_expected;
    }

    *consumed = total_expected;
    return 0;
}

static void fill_request(const parsed_request_t *pr,
                           neverc_http_request_t *req,
                           nc_buf_t *raw_hdr_buf) {
    memset(req, 0, sizeof(*req));
    req->method = pr->method;
    req->path = pr->path;
    req->query = pr->query;
    req->http_version = pr->http_version;
    req->host = pr->host;
    req->content_type = pr->content_type;
    req->body = pr->body;
    req->body_len = pr->body_len;
    req->nheaders = pr->nheaders;

    nc_buf_reset(raw_hdr_buf);
    for (int i = 0; i < pr->nheaders; i++) {
        size_t nlen = strlen(pr->header_names[i]);
        size_t vlen = strlen(pr->header_values[i]);
        nc_buf_append(raw_hdr_buf, pr->header_names[i], nlen);
        nc_buf_append(raw_hdr_buf, "\0", 1);
        nc_buf_append(raw_hdr_buf, pr->header_values[i], vlen);
        nc_buf_append(raw_hdr_buf, "\0", 1);
    }
    req->raw_headers = raw_hdr_buf->data;
}

/* ======================================================================
 * HTTP Connection — event-driven state machine (per connection)
 * ====================================================================== */

typedef struct http_worker http_worker_t;

typedef enum {
    HC_STATE_READING,
    HC_STATE_PROCESSING,
    HC_STATE_CLOSING
} hc_state_t;

struct http_conn {
    nc_sock_t          fd;
    hc_state_t         state;
    nc_buf_t           read_buf;
    nc_buf_t           raw_hdr_buf;
    nc_evloop_t       *loop;
    neverc_http_mux_t *mux;
    http_worker_t     *worker;
    int                max_requests;
    int                requests_served;
    uint64_t           last_active;
    int                idle_timeout_ms;
    size_t             max_read_size;
    struct http_conn  *next;
    struct http_conn  *prev;
};

/* Per-worker connection list for timeout scanning */
typedef struct {
    http_conn_t *head;
    nc_mutex_t   lock;
} http_conn_list_t;

static void conn_list_init(http_conn_list_t *l) {
    l->head = NULL;
    nc_mutex_init(&l->lock);
}

static void conn_list_add(http_conn_list_t *l, http_conn_t *hc) {
    hc->prev = NULL;
    hc->next = l->head;
    if (l->head) l->head->prev = hc;
    l->head = hc;
}

static void conn_list_remove(http_conn_list_t *l, http_conn_t *hc) {
    http_conn_t *p = hc->prev;
    http_conn_t *n = hc->next;
    if (p) p->next = n;
    else l->head = n;
    if (n) n->prev = p;
    hc->next = NULL;
    hc->prev = NULL;
}

static nc_bufpool_t g_conn_pool_cache;
static volatile int g_conn_pool_inited = 0;

static void ensure_conn_pool(void) {
    if (nc_atomic_load(&g_conn_pool_inited)) return;
#ifdef _WIN32
    static volatile LONG cp_lock = 0;
    while (InterlockedCompareExchange(&cp_lock, 1, 0) != 0) { Sleep(0); }
#else
    static volatile int cp_lock = 0;
    while (!__sync_bool_compare_and_swap(&cp_lock, 0, 1)) { /* spin */ }
#endif
    if (!nc_atomic_load(&g_conn_pool_inited)) {
        nc_bufpool_init(&g_conn_pool_cache, sizeof(http_conn_t));
        nc_atomic_store(&g_conn_pool_inited, 1);
    }
#ifdef _WIN32
    InterlockedExchange(&cp_lock, 0);
#else
    __sync_lock_release(&cp_lock);
#endif
}

static http_conn_t *http_conn_new(nc_sock_t fd, nc_evloop_t *loop,
                                    neverc_http_mux_t *mux,
                                    http_worker_t *worker,
                                    int max_req,
                                    int idle_timeout_ms,
                                    size_t max_read_size) {
    ensure_conn_pool();
    http_conn_t *hc = (http_conn_t *)nc_bufpool_pop(&g_conn_pool_cache);
    if (!hc) return NULL;
    hc->fd = fd;
    hc->state = HC_STATE_READING;
    nc_buf_init(&hc->read_buf);
    nc_buf_init(&hc->raw_hdr_buf);
    hc->loop = loop;
    hc->mux = mux;
    hc->worker = worker;
    hc->max_requests = max_req;
    hc->idle_timeout_ms = idle_timeout_ms > 0 ? idle_timeout_ms : 60000;
    hc->max_read_size = max_read_size > 0 ? max_read_size : 11534336;
    hc->last_active = nc_monotonic_ms();
    hc->next = hc->prev = NULL;
    return hc;
}

static void http_conn_free(http_conn_t *hc) {
    if (!hc) return;
    if (hc->fd != NC_INVALID_SOCK)
        nc_poller_del(hc->loop->poller, hc->fd);
    if (hc->fd != NC_INVALID_SOCK)
        nc_sock_close(hc->fd);
    nc_buf_free(&hc->read_buf);
    nc_buf_free(&hc->raw_hdr_buf);
    nc_bufpool_push(&g_conn_pool_cache, hc);
}

static void http_conn_process(http_conn_t *hc) {
    while (hc->read_buf.len > 0 && hc->requests_served < hc->max_requests) {
        parsed_request_t pr;
        size_t consumed = 0;
        int rc = parse_request(hc->read_buf.data, hc->read_buf.len,
                                &pr, &consumed);
        if (rc == -1) return; /* need more data */
        if (rc == -2) {
            const char *err_resp =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            sock_write_all(hc->fd, err_resp, strlen(err_resp));
            hc->state = HC_STATE_CLOSING;
            return;
        }

        /* Global rate limiting */
        if (g_global_rate_limiter &&
            !neverc_http_rate_limiter_allow(g_global_rate_limiter)) {
            const char *rate_resp =
                "HTTP/1.1 429 Too Many Requests\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 20\r\n"
                "Retry-After: 1\r\n"
                "Connection: close\r\n\r\n"
                "Too Many Requests\r\n";
            sock_write_all(hc->fd, rate_resp, strlen(rate_resp));
            parsed_request_free(&pr);
            hc->state = HC_STATE_CLOSING;
            return;
        }

        /* Send 100 Continue if client expects it */
        if (pr.expect_continue) {
            const char *cont = "HTTP/1.1 100 Continue\r\n\r\n";
            sock_write_all(hc->fd, cont, strlen(cont));
        }

        neverc_http_request_t req;
        fill_request(&pr, &req, &hc->raw_hdr_buf);

        /* Route with path parameter extraction */
        path_params_t params;
        memset(&params, 0, sizeof(params));
        neverc_http_handler_func_t handler =
            mux_match_ex(hc->mux, req.method, req.path, &params);

        if (params.count > 0) {
            req.path_params = params.buf;
            req.nparams = params.count;
        }

        neverc_http_response_writer_t *w =
            rw_new(hc->fd, pr.keep_alive, hc, consumed);

        /* Auto-inject CORS headers when enabled */
        if (g_cors_enabled && w) {
            const char *origin = neverc_http_request_header(&req, "Origin");
            neverc_http_cors_headers(w, &g_cors_config, origin);
        }

        if (handler) {
            handler(&req, w);
        } else {
            neverc_http_set_status(w, 404);
            neverc_http_write_string(w, "404 page not found\n");
        }

        /* Free chunked body if it was allocated separately */
        if (pr.is_chunked && pr.body) {
            free((void *)pr.body);
            pr.body = NULL;
        }

        if (w->hijacked) {
            rw_free(w);
            parsed_request_free(&pr);
            hc->requests_served++;
            hc->last_active = nc_monotonic_ms();
            hc->state = HC_STATE_CLOSING;
            return;
        }

        rw_flush(w);

        int should_close = !pr.keep_alive;
        rw_free(w);

        nc_buf_consume(&hc->read_buf, consumed);
        parsed_request_free(&pr);
        hc->requests_served++;
        hc->last_active = nc_monotonic_ms();

        if (should_close) {
            hc->state = HC_STATE_CLOSING;
            return;
        }
    }

    if (hc->requests_served >= hc->max_requests)
        hc->state = HC_STATE_CLOSING;
}

static void http_conn_on_read(http_conn_t *hc) {
    size_t max_read = hc->max_read_size;

    char chunk[8192];
    for (;;) {
#ifdef _WIN32
        int n = recv(hc->fd, chunk, sizeof(chunk), 0);
#else
        ssize_t n = recv(hc->fd, chunk, sizeof(chunk), 0);
#endif
        if (n > 0) {
            nc_buf_append(&hc->read_buf, chunk, (size_t)n);
            if (hc->read_buf.len > max_read) {
                /* Send 413 Payload Too Large before closing */
                const char *err_resp =
                    "HTTP/1.1 413 Payload Too Large\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n";
                sock_write_all(hc->fd, err_resp, strlen(err_resp));
                hc->state = HC_STATE_CLOSING;
                return;
            }
            continue;
        }
        if (n == 0) {
            hc->state = HC_STATE_CLOSING;
            return;
        }
        /* n < 0 */
#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) break;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
        hc->state = HC_STATE_CLOSING;
        return;
    }

    http_conn_process(hc);
}

/* ======================================================================
 * HTTP Server — Multi-threaded Reactor (event-driven)
 *
 * Architecture:
 *   Main thread: accept() in non-blocking loop, round-robin to workers
 *   Worker threads: each runs nc_evloop with non-blocking I/O
 *   Connections: state machine with non-blocking read/parse/respond
 *
 * This handles 10M+ concurrent connections efficiently.
 * ====================================================================== */

struct http_worker {
    nc_evloop_t       *loop;
    nc_thread_t        thread;
    volatile int       conn_count;
    http_conn_list_t   conns;
    int                worker_idx;
};

struct neverc_http_server {
    volatile int           running;
    nc_sock_t              listen_fd;
    neverc_http_mux_t     *mux;

    http_worker_t         *workers;
    int                    nworkers;
    volatile int           next_worker; /* atomic round-robin index */

    int                    max_requests_per_conn;
    int                    read_timeout_ms;
    int                    max_header_size;
    int                    max_body_size;

    nc_evloop_t           *accept_loop;
    nc_thread_t            accept_thread;
};

/* Worker event handler — O(1) via hc->worker back-pointer */
static void worker_event_handler(nc_evloop_t *loop, nc_event_t *ev) {
    http_conn_t *hc = (http_conn_t *)ev->data;
    if (!hc) return;
    (void)loop;

    if (ev->events & NC_EV_ERROR) {
        hc->state = HC_STATE_CLOSING;
    } else if (ev->events & NC_EV_READ) {
        http_conn_on_read(hc);
    }

    if (hc->state == HC_STATE_CLOSING) {
        http_worker_t *w = hc->worker;
        if (w) {
            conn_list_remove(&w->conns, hc);
            nc_atomic_dec(&w->conn_count);
        }
        nc_conn_limiter_release(&g_conn_limiter);
        http_conn_free(hc);
    }
}

static void worker_sweep_idle(http_worker_t *w) {
    uint64_t now = nc_monotonic_ms();
    http_conn_t *hc = w->conns.head;
    while (hc) {
        http_conn_t *next = hc->next;
        if (hc->idle_timeout_ms > 0 &&
            now - hc->last_active > (uint64_t)hc->idle_timeout_ms) {
            conn_list_remove(&w->conns, hc);
            nc_atomic_dec(&w->conn_count);
            nc_conn_limiter_release(&g_conn_limiter);
            http_conn_free(hc);
        }
        hc = next;
    }
}

static void *worker_thread_func(void *arg) {
    http_worker_t *w = (http_worker_t *)arg;
    nc_evloop_t *loop = w->loop;
    loop->running = 1;
    nc_event_t events[NC_EVLOOP_MAX_EVENTS];
    uint64_t last_sweep = nc_monotonic_ms();

    while (loop->running) {
        int n = nc_poller_wait(loop->poller, events, NC_EVLOOP_MAX_EVENTS, 100);

        /* Process pending tasks */
        nc_mutex_lock(&loop->pending_lock);
        int pc = loop->pending_count;
        nc_task_t *ptasks = NULL;
        if (pc > 0) {
            ptasks = (nc_task_t *)malloc((size_t)pc * sizeof(nc_task_t));
            memcpy(ptasks, loop->pending, (size_t)pc * sizeof(nc_task_t));
            loop->pending_count = 0;
        }
        nc_mutex_unlock(&loop->pending_lock);

        if (ptasks) {
            for (int i = 0; i < pc; i++)
                ptasks[i].func(ptasks[i].arg);
            free(ptasks);
        }

        /* Process I/O events */
        for (int i = 0; i < n; i++) {
#ifndef _WIN32
            if (events[i].fd == loop->wakeup_fds[0]) {
                char drain[64];
                while (read(loop->wakeup_fds[0], drain, sizeof(drain)) > 0)
                    ;
                continue;
            }
#endif
            if (events[i].data == NULL) continue;
            worker_event_handler(loop, &events[i]);
        }

        /* Sweep idle connections every 5 seconds */
        uint64_t now = nc_monotonic_ms();
        if (now - last_sweep >= 5000) {
            worker_sweep_idle(w);
            last_sweep = now;
        }
    }
    return NULL;
}

static void distribute_conn_task(void *arg) {
    http_conn_t *hc = (http_conn_t *)arg;
    http_worker_t *worker = hc->worker;

#if !(defined(__linux__) && defined(SOCK_NONBLOCK))
    nc_set_nonblocking(hc->fd);
#endif
    nc_set_nodelay(hc->fd);
    nc_set_keepalive(hc->fd);
    nc_set_quickack(hc->fd);

    conn_list_add(&worker->conns, hc);
    nc_poller_add(hc->loop->poller, hc->fd, NC_EV_READ, hc);

    /*
     * Edge-triggered pollers (kqueue EV_CLEAR / epoll EPOLLET) may not fire
     * if data arrived before we added the fd. Do an eager read to handle
     * this race condition.
     */
    http_conn_on_read(hc);
    if (hc->state == HC_STATE_CLOSING) {
        conn_list_remove(&worker->conns, hc);
        nc_conn_limiter_release(&g_conn_limiter);
        http_conn_free(hc);
        nc_atomic_dec(&worker->conn_count);
    }
}

/* Accept loop event handler */
static void accept_event_handler(nc_evloop_t *loop, nc_event_t *ev) {
    neverc_http_server_t *srv = (neverc_http_server_t *)ev->data;
    if (!srv) return;

    for (;;) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        nc_sock_t cfd = nc_accept_nonblock(srv->listen_fd,
                                             (struct sockaddr *)&client_addr,
                                             &client_len);
        if (cfd == NC_INVALID_SOCK) {
#ifdef _WIN32
            int werr = WSAGetLastError();
            if (werr == WSAEWOULDBLOCK) break;
            if (werr == WSAEINTR) continue;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            if (errno == EMFILE || errno == ENFILE) {
                /* FD exhaustion — back off briefly to avoid spin */
                usleep(50000);
                break;
            }
#endif
            break;
        }

        /* Check connection limit */
        if (!nc_conn_limiter_try_acquire(&g_conn_limiter)) {
            nc_sock_close(cfd);
            continue;
        }

        /* Round-robin to least-loaded worker */
        int idx = 0;
        int min_load = nc_atomic_load(&srv->workers[0].conn_count);
        for (int wi = 1; wi < srv->nworkers; wi++) {
            int wc = nc_atomic_load(&srv->workers[wi].conn_count);
            if (wc < min_load) { min_load = wc; idx = wi; }
        }
        http_worker_t *worker = &srv->workers[idx];

        size_t conn_max_read = (size_t)(srv->max_header_size + srv->max_body_size);
        http_conn_t *hc = http_conn_new(cfd, worker->loop, srv->mux,
                                          worker,
                                          srv->max_requests_per_conn,
                                          srv->read_timeout_ms,
                                          conn_max_read);
        if (!hc) {
            nc_sock_close(cfd);
            nc_conn_limiter_release(&g_conn_limiter);
            continue;
        }

        nc_evloop_post(worker->loop, distribute_conn_task, hc);
        nc_atomic_inc(&worker->conn_count);
    }
    (void)loop;
}

static void *accept_thread_func(void *arg) {
    neverc_http_server_t *srv = (neverc_http_server_t *)arg;
    nc_evloop_run(srv->accept_loop, accept_event_handler);
    return NULL;
}

/* ======================================================================
 * Public Server API
 * ====================================================================== */

static int g_config_workers = 0;
static int g_config_max_requests = 1000;
static int g_config_read_timeout = 60000;
static int g_config_read_header_timeout = 10000;
static int g_config_write_timeout = 60000;
static int g_config_idle_timeout = 60000;
static int g_config_max_conns = 0;        /* 0 = unlimited */
static int g_config_max_header_size = 0;  /* 0 = default 1MB */
static int g_config_max_body_size = 0;    /* 0 = default 10MB */
static int g_config_shutdown_timeout = 5000;
int g_client_max_redirects = 10;
int g_client_timeout_ms = 30000;

void neverc_http_set_workers(int n) {
    if (n > 0 && n <= 256) g_config_workers = n;
}

void neverc_http_set_max_requests(int n) {
    if (n > 0) g_config_max_requests = n;
}

void neverc_http_set_read_timeout(int ms) {
    if (ms >= 0) g_config_read_timeout = ms;
}

void neverc_http_set_read_header_timeout(int ms) {
    if (ms >= 0) g_config_read_header_timeout = ms;
}

void neverc_http_set_write_timeout(int ms) {
    if (ms >= 0) g_config_write_timeout = ms;
}

void neverc_http_set_idle_timeout(int ms) {
    if (ms >= 0) g_config_idle_timeout = ms;
}

void neverc_http_set_max_connections(int n) {
    if (n >= 0) g_config_max_conns = n;
}

void neverc_http_set_max_header_size(int bytes) {
    if (bytes >= 0) g_config_max_header_size = bytes;
}

void neverc_http_set_max_body_size(int bytes) {
    if (bytes >= 0) g_config_max_body_size = bytes;
}

void neverc_http_set_shutdown_timeout(int ms) {
    if (ms >= 0) g_config_shutdown_timeout = ms;
}

int neverc_http_active_connections(void) {
    return nc_conn_limiter_count(&g_conn_limiter);
}

int neverc_http_listen_and_serve(const char *addr, neverc_http_mux_t *mux) {
    nc_net_init();

    if (!mux) {
        ensure_default_mux();
        mux = &default_mux;
    }

    /* Resolve and bind listen socket */
    char host[256] = {0};
    uint16_t port = 0;
    if (nc_parse_addr(addr, host, sizeof(host), &port) != 0)
        return -1;

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host[0] ? host : NULL, portstr, &hints, &result) != 0)
        return -1;

    nc_sock_t lfd = socket(result->ai_family, result->ai_socktype,
                            result->ai_protocol);
    if (lfd == NC_INVALID_SOCK) {
        freeaddrinfo(result);
        return -1;
    }

    nc_set_reuseaddr(lfd);
#if !defined(_WIN32) && defined(SO_REUSEPORT)
    nc_set_reuseport(lfd);
#endif

    nc_set_defer_accept(lfd);

    if (bind(lfd, result->ai_addr, (int)result->ai_addrlen) == NC_SOCK_ERR) {
        nc_sock_close(lfd);
        freeaddrinfo(result);
        return -1;
    }

    if (listen(lfd, 65535) == NC_SOCK_ERR) {
        nc_sock_close(lfd);
        freeaddrinfo(result);
        return -1;
    }

    nc_set_nonblocking(lfd);
    freeaddrinfo(result);

    /* Record the actual bound port (useful for :0 auto-assignment) */
    {
        struct sockaddr_storage bound_addr;
        socklen_t bound_len = sizeof(bound_addr);
        if (getsockname(lfd, (struct sockaddr *)&bound_addr, &bound_len) == 0) {
            if (bound_addr.ss_family == AF_INET)
                g_server_port = ntohs(((struct sockaddr_in *)&bound_addr)->sin_port);
            else if (bound_addr.ss_family == AF_INET6)
                g_server_port = ntohs(((struct sockaddr_in6 *)&bound_addr)->sin6_port);
        }
    }

    /* Create server */
    neverc_http_server_t *srv = (neverc_http_server_t *)calloc(1, sizeof(*srv));
    if (!srv) { nc_sock_close(lfd); return -1; }

    srv->listen_fd = lfd;
    srv->mux = mux;
    srv->max_requests_per_conn = g_config_max_requests;
    srv->read_timeout_ms = g_config_read_timeout;

    /* Global connection limiter */
    nc_conn_limiter_init(&g_conn_limiter,
                          g_config_max_conns > 0 ? g_config_max_conns : 0);

    /* Request size limits */
    srv->max_header_size = g_config_max_header_size > 0
                         ? g_config_max_header_size : (1024 * 1024);     /* 1MB */
    srv->max_body_size = g_config_max_body_size > 0
                       ? g_config_max_body_size : (10 * 1024 * 1024);     /* 10MB */

    int nw = g_config_workers > 0 ? g_config_workers : nc_cpu_count();
    if (nw < 1) nw = 1;
    if (nw > 64) nw = 64;
    srv->nworkers = nw;

    /* Create worker threads */
    srv->workers = (http_worker_t *)calloc((size_t)nw, sizeof(http_worker_t));
    for (int i = 0; i < nw; i++) {
        conn_list_init(&srv->workers[i].conns);
        srv->workers[i].worker_idx = i;
        srv->workers[i].loop = nc_evloop_create();
        if (!srv->workers[i].loop) {
            for (int j = 0; j < i; j++)
                nc_evloop_destroy(srv->workers[j].loop);
            free(srv->workers);
            free(srv);
            nc_sock_close(lfd);
            return -1;
        }
    }

    /* Create accept loop */
    srv->accept_loop = nc_evloop_create();
    if (!srv->accept_loop) {
        for (int i = 0; i < nw; i++)
            nc_evloop_destroy(srv->workers[i].loop);
        free(srv->workers);
        free(srv);
        nc_sock_close(lfd);
        return -1;
    }

    nc_poller_add(srv->accept_loop->poller, lfd, NC_EV_READ, srv);

    srv->running = 1;
    g_server_ptr = srv;

    /* Start worker threads */
    for (int i = 0; i < nw; i++) {
        nc_thread_create(&srv->workers[i].thread, worker_thread_func,
                          &srv->workers[i]);
    }

    /* Run accept loop on this thread (blocks until neverc_http_shutdown) */
    nc_evloop_run(srv->accept_loop, accept_event_handler);

    /* neverc_http_shutdown() has already drained in-flight requests
     * and signaled workers to stop. Now join them. */
    for (int i = 0; i < nw; i++) {
        nc_thread_join(srv->workers[i].thread);

        /* Clean up any remaining connections in this worker */
        http_conn_t *hc = srv->workers[i].conns.head;
        while (hc) {
            http_conn_t *next = hc->next;
            nc_conn_limiter_release(&g_conn_limiter);
            http_conn_free(hc);
            hc = next;
        }
        nc_mutex_destroy(&srv->workers[i].conns.lock);

        nc_evloop_destroy(srv->workers[i].loop);
    }

    nc_evloop_destroy(srv->accept_loop);
    if (srv->listen_fd != NC_INVALID_SOCK)
        nc_sock_close(srv->listen_fd);
    free(srv->workers);
    free(srv);
    g_server_ptr = NULL;

    return 0;
}

void neverc_http_shutdown(void) {
    neverc_http_server_t *srv = g_server_ptr;
    if (!srv) return;
    srv->running = 0;

    /* Stop accepting new connections */
    nc_sock_close(srv->listen_fd);
    srv->listen_fd = NC_INVALID_SOCK;
    nc_evloop_stop(srv->accept_loop);

    /* Let workers drain in-flight requests before stopping them.
     * Workers keep running their event loops so current requests
     * can complete. Only after the timeout do we force-stop. */
    uint64_t drain_start = nc_monotonic_ms();
    int drain_ms = g_config_shutdown_timeout;
    while (drain_ms > 0) {
        int active = 0;
        for (int i = 0; i < srv->nworkers; i++)
            active += nc_atomic_load(&srv->workers[i].conn_count);
        if (active <= 0) break;
        uint64_t elapsed = nc_monotonic_ms() - drain_start;
        if (elapsed >= (uint64_t)drain_ms) break;
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }

    /* Now stop all workers */
    for (int i = 0; i < srv->nworkers; i++) {
        nc_evloop_stop(srv->workers[i].loop);
    }
}

/* ======================================================================
 * HTTPS — TLS-wrapped HTTP server (like Go http.ListenAndServeTLS)
 *
 * Accepts TLS connections via a TLS listener, performs handshake,
 * then wraps the decrypted stream through the existing HTTP pipeline
 * using a thread-per-connection model (TLS handshake is blocking).
 * ====================================================================== */

typedef struct {
    neverc_tls_listener_t *tls_ln;
    neverc_tls_config_t   *tls_cfg;
    neverc_http_mux_t     *mux;
    volatile int           running;
    nc_threadpool_t       *pool;
} https_server_t;

static https_server_t *g_https_server = NULL;

typedef struct {
    neverc_tls_conn_t     *tls;
    neverc_http_mux_t     *mux;
} https_conn_ctx_t;

static void https_handle_connection(void *arg) {
    https_conn_ctx_t *ctx = (https_conn_ctx_t *)arg;
    neverc_tls_conn_t *tls = ctx->tls;
    neverc_http_mux_t *mux = ctx->mux;
    free(ctx);

    /* ALPN negotiation: if client negotiated h2, use HTTP/2 handler */
    const char *alpn_proto = neverc_tls_alpn(tls);
    if (alpn_proto && strcmp(alpn_proto, "h2") == 0) {
        neverc_h2_server_t *h2srv = neverc_h2_server_create(mux);
        if (h2srv) {
            neverc_h2_serve_tls_conn(h2srv, tls);
            neverc_h2_server_destroy(h2srv);
        }
        neverc_tls_close(tls);
        nc_conn_limiter_release(&g_conn_limiter);
        return;
    }

    char buf[65536];
    nc_buf_t req_buf;
    nc_buf_init(&req_buf);

    /* Read and serve HTTP requests over the TLS connection */
    for (int req_count = 0; req_count < g_config_max_requests; req_count++) {
        /* Read request data */
        int got_headers = 0;
        while (!got_headers) {
            int n = neverc_tls_read(tls, buf, sizeof(buf));
            if (n <= 0) goto done;
            nc_buf_append(&req_buf, buf, (size_t)n);

            if (strstr(req_buf.data, "\r\n\r\n"))
                got_headers = 1;

            if (req_buf.len > 1048576) goto done; /* 1MB header limit */
        }

        /* Parse the request */
        parsed_request_t pr;
        size_t consumed = 0;
        int rc = parse_request(req_buf.data, req_buf.len, &pr, &consumed);
        if (rc != 0) goto done;

        /* Read remaining body if needed */
        while (pr.content_length > 0 &&
               req_buf.len < consumed + (size_t)pr.content_length) {
            int n = neverc_tls_read(tls, buf, sizeof(buf));
            if (n <= 0) { parsed_request_free(&pr); goto done; }
            nc_buf_append(&req_buf, buf, (size_t)n);
        }

        neverc_http_request_t req;
        nc_buf_t raw_hdr_buf;
        nc_buf_init(&raw_hdr_buf);
        fill_request(&pr, &req, &raw_hdr_buf);

        /* Rate limiting */
        if (g_global_rate_limiter &&
            !neverc_http_rate_limiter_allow(g_global_rate_limiter)) {
            const char *rate_resp =
                "HTTP/1.1 429 Too Many Requests\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 20\r\n"
                "Retry-After: 1\r\n"
                "Connection: close\r\n\r\n"
                "Too Many Requests\r\n";
            neverc_tls_write(tls, rate_resp, strlen(rate_resp));
            nc_buf_free(&raw_hdr_buf);
            parsed_request_free(&pr);
            goto done;
        }

        /* 100-Continue */
        if (pr.expect_continue) {
            const char *cont = "HTTP/1.1 100 Continue\r\n\r\n";
            neverc_tls_write(tls, cont, strlen(cont));
        }

        /* Route with path parameter extraction (like HTTP handler) */
        path_params_t params;
        memset(&params, 0, sizeof(params));
        neverc_http_handler_func_t handler =
            mux_match_ex(mux, req.method, req.path, &params);

        if (params.count > 0) {
            req.path_params = params.buf;
            req.nparams = params.count;
        }

        nc_buf_t resp_buf;
        nc_buf_init(&resp_buf);

        neverc_http_response_writer_t *w =
            (neverc_http_response_writer_t *)calloc(1, sizeof(*w));
        if (!w) {
            nc_buf_free(&raw_hdr_buf);
            parsed_request_free(&pr);
            goto done;
        }
        w->fd = NC_INVALID_SOCK;
        w->status = 200;
        w->keep_alive = pr.keep_alive;
        nc_buf_init(&w->body);

        /* CORS headers for HTTPS */
        if (g_cors_enabled) {
            const char *origin = neverc_http_request_header(&req, "Origin");
            neverc_http_cors_headers(w, &g_cors_config, origin);
        }

        if (handler) {
            handler(&req, w);
        } else {
            neverc_http_set_status(w, 404);
            neverc_http_write_string(w, "404 page not found\n");
        }

        /* Build HTTP response */
        char line[256];
        int n = snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n",
                         w->status, neverc_http_status_text(w->status));
        nc_buf_append(&resp_buf, line, (size_t)n);

        int has_ct = 0, has_cl = 0, has_conn = 0;
        for (int i = 0; i < w->nheaders; i++) {
            n = snprintf(line, sizeof(line), "%s: %s\r\n",
                         w->header_names[i], w->header_values[i]);
            nc_buf_append(&resp_buf, line, (size_t)n);
            if (strcasecmp(w->header_names[i], "Content-Type") == 0) has_ct = 1;
            if (strcasecmp(w->header_names[i], "Content-Length") == 0) has_cl = 1;
            if (strcasecmp(w->header_names[i], "Connection") == 0) has_conn = 1;
        }
        if (!has_ct) nc_buf_append(&resp_buf,
            "Content-Type: text/plain; charset=utf-8\r\n", 41);
        if (!has_cl) {
            n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n",
                         w->body.len);
            nc_buf_append(&resp_buf, line, (size_t)n);
        }
        if (!has_conn) {
            const char *cv = w->keep_alive
                ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
            nc_buf_append(&resp_buf, cv, strlen(cv));
        }
        nc_buf_append(&resp_buf, "\r\n", 2);
        if (w->body.len > 0)
            nc_buf_append(&resp_buf, w->body.data, w->body.len);

        /* Send response over TLS */
        neverc_tls_write(tls, resp_buf.data, resp_buf.len);
        nc_buf_free(&resp_buf);

        int should_close = !pr.keep_alive;
        for (int hi = 0; hi < w->nheaders; hi++) {
            free(w->header_names[hi]);
            free(w->header_values[hi]);
        }
        nc_buf_free(&w->body);
        free(w);
        nc_buf_free(&raw_hdr_buf);

        if (pr.is_chunked && pr.body) free((void *)pr.body);
        nc_buf_consume(&req_buf, consumed);
        parsed_request_free(&pr);

        if (should_close) break;
    }

done:
    nc_buf_free(&req_buf);
    neverc_tls_close(tls);
    if (g_https_server)
        nc_conn_limiter_release(&g_conn_limiter);
}

int neverc_http_listen_and_serve_tls(const char *addr, neverc_http_mux_t *mux,
                                      const char *cert_file,
                                      const char *key_file) {
    nc_net_init();

    if (!mux) {
        ensure_default_mux();
        mux = &default_mux;
    }

    neverc_tls_config_t *cfg = neverc_tls_config_new();
    if (!cfg) return -1;

    if (neverc_tls_config_load_cert(cfg, cert_file, key_file) != 0) {
        neverc_tls_config_free(cfg);
        return -1;
    }

    /* Advertise both h2 and http/1.1 via ALPN for automatic protocol negotiation.
     * Clients that support HTTP/2 will negotiate h2; others fall back to HTTP/1.1. */
    const char *alpn_protos[] = { "h2", "http/1.1" };
    neverc_tls_config_set_alpn(cfg, alpn_protos, 2);

    const char *err = NULL;
    neverc_tls_listener_t *tls_ln = neverc_tls_listen(addr, cfg, &err);
    if (!tls_ln) {
        neverc_tls_config_free(cfg);
        return -1;
    }

    int nw = g_config_workers > 0 ? g_config_workers : nc_cpu_count();
    if (nw < 1) nw = 1;
    if (nw > 64) nw = 64;

    nc_threadpool_t *pool = nc_threadpool_create(nw);
    if (!pool) {
        neverc_tls_listener_close(tls_ln);
        neverc_tls_config_free(cfg);
        return -1;
    }

    nc_conn_limiter_init(&g_conn_limiter,
                          g_config_max_conns > 0 ? g_config_max_conns : 0);

    https_server_t srv = {
        .tls_ln = tls_ln,
        .tls_cfg = cfg,
        .mux = mux,
        .running = 1,
        .pool = pool,
    };
    g_https_server = &srv;

    while (srv.running) {
        neverc_tls_conn_t *tls = neverc_tls_accept(tls_ln, &err);
        if (!tls) {
            if (!srv.running) break;
            continue;
        }

        if (!nc_conn_limiter_try_acquire(&g_conn_limiter)) {
            neverc_tls_close(tls);
            continue;
        }

        https_conn_ctx_t *ctx =
            (https_conn_ctx_t *)malloc(sizeof(*ctx));
        if (!ctx) {
            neverc_tls_close(tls);
            nc_conn_limiter_release(&g_conn_limiter);
            continue;
        }
        ctx->tls = tls;
        ctx->mux = mux;
        nc_threadpool_submit(pool, https_handle_connection, ctx);
    }

    g_https_server = NULL;
    nc_threadpool_destroy(pool);
    neverc_tls_listener_close(tls_ln);
    neverc_tls_config_free(cfg);

    return 0;
}

/* ======================================================================
 * Helpers
 * ====================================================================== */

const char *neverc_http_query_get(const char *query, const char *key,
                                   char *buf, size_t buflen) {
    if (!query || !key || !buf || buflen == 0) return NULL;
    size_t klen = strlen(key);
    const char *p = query;

    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val = p + klen + 1;
            size_t i = 0;
            while (val[i] && val[i] != '&' && i < buflen - 1) {
                buf[i] = val[i];
                i++;
            }
            buf[i] = '\0';
            return buf;
        }
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    return NULL;
}

neverc_tcp_conn_t *neverc_http_hijack(neverc_http_response_writer_t *w) {
    if (!w || w->hijacked || !w->owner) return NULL;

    http_conn_t *hc = w->owner;
    w->hijacked = 1;

    nc_buf_consume(&hc->read_buf, w->request_consumed);
    const void *preload = NULL;
    size_t preload_len = 0;
    if (hc->read_buf.len > 0) {
        preload = hc->read_buf.data;
        preload_len = hc->read_buf.len;
    }

    nc_sock_t fd = hc->fd;
    hc->fd = NC_INVALID_SOCK;
    nc_buf_reset(&hc->read_buf);

    const char *err = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_adopt(
#ifdef _WIN32
        (int)fd,
#else
        fd,
#endif
        preload, preload_len, &err);
    (void)err;
    return conn;
}

const char *neverc_http_request_header(const neverc_http_request_t *req,
                                        const char *name) {
    if (!req || !req->raw_headers || !name) return NULL;
    const char *p = req->raw_headers;
    for (int i = 0; i < req->nheaders; i++) {
        const char *hname = p;
        while (*p) p++;
        p++;
        const char *hval = p;
        while (*p) p++;
        p++;
        if (strcasecmp(hname, name) == 0) return hval;
    }
    return NULL;
}

const char *neverc_http_status_text(int code) {
    if (code == 100) return "Continue";
    if (code == 101) return "Switching Protocols";
    if (code == 200) return "OK";
    if (code == 201) return "Created";
    if (code == 202) return "Accepted";
    if (code == 204) return "No Content";
    if (code == 206) return "Partial Content";
    if (code == 301) return "Moved Permanently";
    if (code == 302) return "Found";
    if (code == 303) return "See Other";
    if (code == 304) return "Not Modified";
    if (code == 307) return "Temporary Redirect";
    if (code == 308) return "Permanent Redirect";
    if (code == 400) return "Bad Request";
    if (code == 401) return "Unauthorized";
    if (code == 403) return "Forbidden";
    if (code == 404) return "Not Found";
    if (code == 405) return "Method Not Allowed";
    if (code == 406) return "Not Acceptable";
    if (code == 408) return "Request Timeout";
    if (code == 409) return "Conflict";
    if (code == 410) return "Gone";
    if (code == 411) return "Length Required";
    if (code == 413) return "Payload Too Large";
    if (code == 414) return "URI Too Long";
    if (code == 415) return "Unsupported Media Type";
    if (code == 416) return "Range Not Satisfiable";
    if (code == 422) return "Unprocessable Entity";
    if (code == 429) return "Too Many Requests";
    if (code == 431) return "Request Header Fields Too Large";
    if (code == 500) return "Internal Server Error";
    if (code == 501) return "Not Implemented";
    if (code == 502) return "Bad Gateway";
    if (code == 503) return "Service Unavailable";
    if (code == 504) return "Gateway Timeout";
    return "Unknown";
}

/* ======================================================================
 * Memory Writer — for testing / httptest (writes to buffer, not socket)
 * ====================================================================== */

neverc_http_response_writer_t *neverc_http_memory_writer_new(void) {
    neverc_http_response_writer_t *w =
        (neverc_http_response_writer_t *)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->fd = NC_INVALID_SOCK;
    w->status = 200;
    nc_buf_init(&w->body);
    return w;
}

int neverc_http_memory_writer_result(neverc_http_response_writer_t *w,
                                      char **out_data, size_t *out_len) {
    if (!w) return 0;
    if (out_data) {
        if (w->body.len > 0) {
            *out_data = (char *)malloc(w->body.len + 1);
            if (*out_data) {
                memcpy(*out_data, w->body.data, w->body.len);
                (*out_data)[w->body.len] = '\0';
            }
        } else {
            *out_data = NULL;
        }
    }
    if (out_len) *out_len = w->body.len;
    return w->status;
}

void neverc_http_memory_writer_free(neverc_http_response_writer_t *w) {
    if (!w) return;
    for (int i = 0; i < w->nheaders; i++) {
        free(w->header_names[i]);
        free(w->header_values[i]);
    }
    nc_buf_free(&w->body);
    free(w);
}

/* ======================================================================
 * Go-style convenience APIs
 * ====================================================================== */

void neverc_http_redirect(neverc_http_response_writer_t *w,
                            const char *url, int code) {
    if (!w || !url) return;
    if (code < 300 || code > 399) code = 302;
    neverc_http_set_status(w, code);
    neverc_http_set_header(w, "Location", url);
    neverc_http_set_header(w, "Content-Type", "text/html; charset=utf-8");
    neverc_http_writef(w, "<a href=\"%s\">%s</a>.\n",
                        url, neverc_http_status_text(code));
}

void neverc_http_error(neverc_http_response_writer_t *w,
                         const char *message, int code) {
    if (!w) return;
    neverc_http_set_status(w, code);
    neverc_http_set_header(w, "Content-Type", "text/plain; charset=utf-8");
    neverc_http_set_header(w, "X-Content-Type-Options", "nosniff");
    neverc_http_write_string(w, message ? message : "error");
    neverc_http_write_string(w, "\n");
}

const char *neverc_http_form_value(const char *body, size_t body_len,
                                     const char *key, char *buf, size_t buflen) {
    if (!body || !key || !buf || buflen == 0 || body_len == 0) return NULL;
    size_t klen = strlen(key);
    const char *end = body + body_len;
    const char *p = body;

    while (p < end) {
        if ((size_t)(end - p) > klen && memcmp(p, key, klen) == 0 &&
            p[klen] == '=') {
            const char *val = p + klen + 1;
            size_t i = 0;
            while (val + i < end && val[i] != '&' && i < buflen - 1) {
                if (val[i] == '+')
                    buf[i] = ' ';
                else if (val[i] == '%' && i + 2 < buflen - 1 &&
                         val + i + 2 < end) {
                    int hi = val[i + 1];
                    int lo = val[i + 2];
                    if (hi >= '0' && hi <= '9') hi -= '0';
                    else if (hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10;
                    else if (hi >= 'A' && hi <= 'F') hi = hi - 'A' + 10;
                    else { buf[i] = val[i]; i++; continue; }
                    if (lo >= '0' && lo <= '9') lo -= '0';
                    else if (lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10;
                    else if (lo >= 'A' && lo <= 'F') lo = lo - 'A' + 10;
                    else { buf[i] = val[i]; i++; continue; }
                    buf[i] = (char)(hi * 16 + lo);
                    /* Consume 2 extra chars from val but only 1 in buf */
                    val += 2;
                } else {
                    buf[i] = val[i];
                }
                i++;
            }
            buf[i] = '\0';
            return buf;
        }
        while (p < end && *p != '&') p++;
        if (p < end) p++;
    }
    return NULL;
}

int neverc_http_write_json(neverc_http_response_writer_t *w,
                             const char *json) {
    if (!w || !json) return 0;
    neverc_http_set_header(w, "Content-Type", "application/json; charset=utf-8");
    return neverc_http_write_string(w, json);
}

/* ======================================================================
 * Middleware support
 * ====================================================================== */

#define MAX_MW_CHAINS 64

typedef struct {
    neverc_http_handler_func_t inner;
    neverc_http_handler_func_t wrapped;
} mw_chain_t;

static mw_chain_t g_mw_chains[MAX_MW_CHAINS];
static int g_mw_chain_count = 0;

neverc_http_handler_func_t neverc_http_use(neverc_http_handler_func_t handler,
                                            neverc_http_middleware_t mw) {
    if (!handler || !mw) return handler;
    neverc_http_handler_func_t wrapped = mw(handler);
    if (g_mw_chain_count < MAX_MW_CHAINS) {
        g_mw_chains[g_mw_chain_count].inner = handler;
        g_mw_chains[g_mw_chain_count].wrapped = wrapped;
        g_mw_chain_count++;
    }
    return wrapped;
}

/* ======================================================================
 * Static file serving
 * ====================================================================== */

typedef struct {
    char pattern[256];
    char dir_path[2048];
} static_dir_ctx_t;

static static_dir_ctx_t g_static_dirs[16];
static int g_static_dir_count = 0;

static const char *guess_content_type(const char *path) {
    size_t len = strlen(path);
    if (len >= 5 && strcmp(path + len - 5, ".html") == 0) return "text/html";
    if (len >= 4 && strcmp(path + len - 4, ".htm") == 0) return "text/html";
    if (len >= 4 && strcmp(path + len - 4, ".css") == 0) return "text/css";
    if (len >= 3 && strcmp(path + len - 3, ".js") == 0) return "application/javascript";
    if (len >= 5 && strcmp(path + len - 5, ".json") == 0) return "application/json";
    if (len >= 4 && strcmp(path + len - 4, ".png") == 0) return "image/png";
    if (len >= 4 && strcmp(path + len - 4, ".jpg") == 0) return "image/jpeg";
    if (len >= 5 && strcmp(path + len - 5, ".jpeg") == 0) return "image/jpeg";
    if (len >= 4 && strcmp(path + len - 4, ".gif") == 0) return "image/gif";
    if (len >= 4 && strcmp(path + len - 4, ".svg") == 0) return "image/svg+xml";
    if (len >= 4 && strcmp(path + len - 4, ".ico") == 0) return "image/x-icon";
    if (len >= 4 && strcmp(path + len - 4, ".xml") == 0) return "application/xml";
    if (len >= 4 && strcmp(path + len - 4, ".txt") == 0) return "text/plain";
    if (len >= 5 && strcmp(path + len - 5, ".wasm") == 0) return "application/wasm";
    return "application/octet-stream";
}

static void static_file_handler(neverc_http_request_t *req,
                                  neverc_http_response_writer_t *w) {
    for (int d = 0; d < g_static_dir_count; d++) {
        size_t plen = strlen(g_static_dirs[d].pattern);
        if (strncmp(req->path, g_static_dirs[d].pattern, plen) != 0) continue;

        const char *relpath = req->path + plen;
        if (relpath[0] == '\0') relpath = "index.html";

        {
            const char *s = relpath;
            while (*s) {
                if (s[0] == '.' && s[1] == '.' &&
                    (s[2] == '/' || s[2] == '\\' || s[2] == '\0') &&
                    (s == relpath || s[-1] == '/' || s[-1] == '\\')) {
                    neverc_http_set_status(w, 403);
                    neverc_http_write_string(w, "403 Forbidden\n");
                    return;
                }
                s++;
            }
        }

        char filepath[4096];
        snprintf(filepath, sizeof(filepath), "%s/%s",
                 g_static_dirs[d].dir_path, relpath);

        FILE *f = fopen(filepath, "rb");
        if (!f) {
            neverc_http_set_status(w, 404);
            neverc_http_write_string(w, "404 Not Found\n");
            return;
        }

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (fsize < 0) {
            fclose(f);
            neverc_http_set_status(w, 500);
            neverc_http_write_string(w, "500 Internal Server Error\n");
            return;
        }

        neverc_http_set_header(w, "Content-Type", guess_content_type(filepath));

        if (fsize <= 1024 * 1024) {
            char *data = (char *)malloc((size_t)fsize);
            if (!data) {
                fclose(f);
                neverc_http_set_status(w, 500);
                neverc_http_write_string(w, "500 Out of memory\n");
                return;
            }
            size_t nread = fread(data, 1, (size_t)fsize, f);
            fclose(f);
            neverc_http_write(w, data, nread);
            free(data);
        } else {
#if NC_HAS_SENDFILE
            /* Zero-copy sendfile path: send headers manually, then sendfile. */
            char clbuf[32];
            snprintf(clbuf, sizeof(clbuf), "%ld", fsize);
            neverc_http_set_header(w, "Content-Length", clbuf);

            /* Manually flush headers before sendfile */
            rw_flush(w);

            int file_fd = fileno(f);
            off_t offset = 0;
            size_t remaining = (size_t)fsize;
            while (remaining > 0) {
                ssize_t sent = nc_sendfile(w->fd, file_fd, &offset, remaining);
                if (sent <= 0) break;
                remaining -= (size_t)sent;
            }
            fclose(f);
#else
            neverc_http_enable_chunked(w);
            char buf[65536];
            size_t nread;
            while ((nread = fread(buf, 1, sizeof(buf), f)) > 0) {
                neverc_http_write(w, buf, nread);
                neverc_http_flush_chunk(w);
            }
            fclose(f);
            neverc_http_end_chunked(w);
#endif
        }
        return;
    }

    neverc_http_set_status(w, 404);
    neverc_http_write_string(w, "404 Not Found\n");
}

void neverc_http_serve_dir(neverc_http_mux_t *mux, const char *pattern,
                            const char *dir_path) {
    if (!pattern || !dir_path || g_static_dir_count >= 16) return;

    size_t pi = (size_t)g_static_dir_count;
    snprintf(g_static_dirs[pi].pattern, sizeof(g_static_dirs[pi].pattern),
             "%s", pattern);
    snprintf(g_static_dirs[pi].dir_path, sizeof(g_static_dirs[pi].dir_path),
             "%s", dir_path);
    g_static_dir_count++;

    if (mux)
        neverc_http_mux_handle(mux, pattern, static_file_handler);
    else
        neverc_http_handle_func(pattern, static_file_handler);
}

