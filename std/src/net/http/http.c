#include "neverc/std/net/http.h"
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/compress/gzip.h"
#include "../_net_internal.h"
#include <stdarg.h>
#include <time.h>

static neverc_http_server_t *volatile g_server_ptr;

#ifndef _WIN32
#include <poll.h>
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
typedef int (*http_writer_func_t)(void *context, const void *data,
                                  size_t len, int timeout_ms);
typedef int (*http_protocol_flush_func_t)(
    void *context, neverc_http_response_writer_t *writer, int end_stream);

static int http_valid_token(const char *s, size_t length);
static int http_valid_field_value(const char *s, size_t length);
static int http_value_has_token(const char *value, size_t length,
                                const char *expected);
int nc_http_sock_write_all_timeout(nc_sock_t fd, const void *data, size_t len,
                                   int timeout_ms);

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
    char       *trailer_names[HTTP_MAX_HEADERS];
    char       *trailer_values[HTTP_MAX_HEADERS];
    int         ntrailers;
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
    int         write_timeout_ms;
    int         has_content_length_override;
    size_t      content_length_override;
    int         head_request;
    http_writer_func_t transport_write;
    void       *transport_context;
    neverc_tcp_conn_t *transport_tcp;
    http_protocol_flush_func_t protocol_flush;
    void       *protocol_context;
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
    for (int i = 0; i < w->ntrailers; i++) {
        free(w->trailer_names[i]);
        free(w->trailer_values[i]);
    }
    nc_buf_free(&w->body);
    memset(w, 0, sizeof(*w));
    nc_bufpool_push(&g_rw_pool, w);
}

static int rw_write_all(neverc_http_response_writer_t *w,
                        const void *data, size_t len) {
    if (w->transport_write)
        return w->transport_write(w->transport_context, data, len,
                                  w->write_timeout_ms);
    if (w->fd == NC_INVALID_SOCK) return -1;
    return nc_http_sock_write_all_timeout(w->fd, data, len,
                                          w->write_timeout_ms);
}

static int http_wait_writable(nc_sock_t fd, int timeout_ms) {
#ifdef _WIN32
    for (;;) {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(fd, &writefds);
        struct timeval timeout;
        struct timeval *timeout_ptr = NULL;
        if (timeout_ms >= 0) {
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            timeout_ptr = &timeout;
        }
        int rc = select(0, NULL, &writefds, NULL, timeout_ptr);
        if (rc != SOCKET_ERROR || WSAGetLastError() != WSAEINTR)
            return rc == SOCKET_ERROR ? -1 : rc;
    }
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    int rc;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);
    if (rc > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
        return -1;
    return rc;
#endif
}

int nc_http_sock_write_all_timeout(nc_sock_t fd, const void *data, size_t len,
                                   int timeout_ms) {
    if ((!data && len > 0) || timeout_ms < 0) return -1;
    const char *p = (const char *)data;
    size_t sent = 0;
    uint64_t deadline = 0;
    if (timeout_ms > 0) {
        uint64_t now = nc_monotonic_ms();
        deadline = now > UINT64_MAX - (uint64_t)timeout_ms
            ? UINT64_MAX : now + (uint64_t)timeout_ms;
    }
    while (sent < len) {
        size_t remaining = len - sent;
        if (remaining > (size_t)INT_MAX) remaining = (size_t)INT_MAX;
#ifdef _WIN32
        int n = send(fd, p + sent, (int)remaining, 0);
        if (n > 0) { sent += (size_t)n; continue; }
        int error = WSAGetLastError();
        if (error == WSAEINTR) continue;
        if (error != WSAEWOULDBLOCK) return -1;
#else
        ssize_t n = send(fd, p + sent, remaining, MSG_NOSIGNAL);
        if (n > 0) { sent += (size_t)n; continue; }
        if (n == 0) return -1;
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
#endif
        int wait_ms = -1;
        if (deadline > 0) {
            uint64_t now = nc_monotonic_ms();
            if (now >= deadline) return -1;
            uint64_t remaining_ms = deadline - now;
            wait_ms = remaining_ms > (uint64_t)INT_MAX
                ? INT_MAX : (int)remaining_ms;
        }
        if (http_wait_writable(fd, wait_ms) <= 0) return -1;
    }
    return 0;
}

int nc_http_sock_write_all(nc_sock_t fd, const void *data, size_t len) {
    return nc_http_sock_write_all_timeout(fd, data, len, 60000);
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

static void rw_apply_gzip(neverc_http_response_writer_t *w) {
    if (!w->gzip_enabled || !w->accepts_gzip ||
        w->body.len < w->gzip_min_size || w->body.len == 0 ||
        w->status < 200 || w->status == 204 || w->status == 304)
        return;
    for (int i = 0; i < w->nheaders; i++)
        if (strcasecmp(w->header_names[i], "Content-Encoding") == 0)
            return;
    if (w->body.len > SIZE_MAX - w->body.len / 32 - 128) return;
    size_t capacity = w->body.len + w->body.len / 32 + 128;
    uint8_t *compressed = (uint8_t *)malloc(capacity);
    if (!compressed) return;
    size_t compressed_length = capacity;
    if (neverc_gzip_compress((const uint8_t *)w->body.data, w->body.len,
                             compressed, &compressed_length,
                             w->gzip_level) == 0 &&
        compressed_length < w->body.len) {
        nc_buf_reset(&w->body);
        if (nc_buf_append(&w->body, compressed, compressed_length) == 0) {
            neverc_http_set_header(w, "Content-Encoding", "gzip");
            neverc_http_set_header(w, "Vary", "Accept-Encoding");
        }
    }
    free(compressed);
}

static void rw_flush(neverc_http_response_writer_t *w) {
    if (!w || w->hijacked) return;
    if (w->headers_sent) return;
    if (w->protocol_flush) {
        rw_apply_gzip(w);
        if (w->protocol_flush(w->protocol_context, w, 1) == 0)
            w->headers_sent = 1;
        else
            w->keep_alive = 0;
        return;
    }
    w->headers_sent = 1;
    rw_apply_gzip(w);
    int status_forbids_body = w->status < 200 || w->status == 204 ||
                              w->status == 304;
    int forbids_content_length = w->status < 200 || w->status == 204;

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
    char line[256];
    int n;

    for (int i = 0; i < w->nheaders; i++) {
        if (strcasecmp(w->header_names[i], "Content-Length") == 0 ||
            strcasecmp(w->header_names[i], "Transfer-Encoding") == 0 ||
            strcasecmp(w->header_names[i], "Connection") == 0)
            continue;
        nc_buf_append(&hdr, w->header_names[i], strlen(w->header_names[i]));
        nc_buf_append(&hdr, ": ", 2);
        nc_buf_append(&hdr, w->header_values[i], strlen(w->header_values[i]));
        nc_buf_append(&hdr, "\r\n", 2);

        if (strcasecmp(w->header_names[i], "Content-Type") == 0)
            has_content_type = 1;
    }

    if (!has_content_type) {
        const char *ct = "Content-Type: text/plain; charset=utf-8\r\n";
        nc_buf_append(&hdr, ct, strlen(ct));
    }
    if (!forbids_content_length) {
        size_t content_length = w->has_content_length_override
            ? w->content_length_override : w->body.len;
        n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n",
                     content_length);
        nc_buf_append(&hdr, line, (size_t)n);
    }
    const char *conn_val = w->keep_alive
        ? "Connection: keep-alive\r\n"
        : "Connection: close\r\n";
    nc_buf_append(&hdr, conn_val, strlen(conn_val));

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

    if (w->fd != NC_INVALID_SOCK || w->transport_write) {
        if (rw_write_all(w, hdr.data, hdr.len) != 0)
            w->keep_alive = 0;
        else if (!w->head_request && !status_forbids_body &&
                 w->body.len > 0 &&
                 rw_write_all(w, w->body.data, w->body.len) != 0)
            w->keep_alive = 0;
    }

    nc_buf_free(&hdr);
}

void nc_http_writer_set_protocol(neverc_http_response_writer_t *writer,
                                 void *context,
                                 http_protocol_flush_func_t flush) {
    if (!writer) return;
    writer->protocol_context = context;
    writer->protocol_flush = flush;
}

int nc_http_writer_finish(neverc_http_response_writer_t *writer) {
    if (!writer || writer->hijacked) return -1;
    if (!writer->headers_sent)
        rw_flush(writer);
    return writer->headers_sent ? 0 : -1;
}

void neverc_http_set_status(neverc_http_response_writer_t *w, int code) {
    if (w && code >= 100 && code <= 999) w->status = code;
}

void neverc_http_set_header(neverc_http_response_writer_t *w,
                             const char *name, const char *value) {
    if (!w || !name || !value || !http_valid_token(name, strlen(name)) ||
        !http_valid_field_value(value, strlen(value)))
        return;
    if (strcasecmp(name, "Connection") == 0) {
        w->keep_alive = !http_value_has_token(value, strlen(value), "close");
        return;
    }
    if (strcasecmp(name, "Content-Length") == 0 ||
        strcasecmp(name, "Transfer-Encoding") == 0)
        return;
    for (int i = 0; i < w->nheaders; i++) {
        if (strcasecmp(w->header_names[i], name) == 0) {
            char *replacement = strdup(value);
            if (!replacement) return;
            free(w->header_values[i]);
            w->header_values[i] = replacement;
            return;
        }
    }
    if (w->nheaders >= HTTP_MAX_HEADERS) return;
    char *name_copy = strdup(name);
    char *value_copy = strdup(value);
    if (!name_copy || !value_copy) {
        free(name_copy);
        free(value_copy);
        return;
    }
    w->header_names[w->nheaders] = name_copy;
    w->header_values[w->nheaders] = value_copy;
    w->nheaders++;
}

void neverc_http_set_trailer(neverc_http_response_writer_t *w,
                              const char *name, const char *value) {
    if (!w || !name || !value || name[0] == ':' ||
        !http_valid_token(name, strlen(name)) ||
        !http_valid_field_value(value, strlen(value)) ||
        strcasecmp(name, "Content-Length") == 0 ||
        strcasecmp(name, "Transfer-Encoding") == 0 ||
        strcasecmp(name, "Trailer") == 0)
        return;
    for (int i = 0; i < w->ntrailers; i++) {
        if (strcasecmp(w->trailer_names[i], name) == 0) {
            char *replacement = strdup(value);
            if (!replacement) return;
            free(w->trailer_values[i]);
            w->trailer_values[i] = replacement;
            return;
        }
    }
    if (w->ntrailers >= HTTP_MAX_HEADERS) return;
    char *name_copy = strdup(name);
    char *value_copy = strdup(value);
    if (!name_copy || !value_copy) {
        free(name_copy);
        free(value_copy);
        return;
    }
    w->trailer_names[w->ntrailers] = name_copy;
    w->trailer_values[w->ntrailers] = value_copy;
    w->ntrailers++;
}

int neverc_http_write(neverc_http_response_writer_t *w,
                       const void *data, size_t len) {
    if (!w || (!data && len > 0) || len > INT_MAX) return -1;
    if (w->body_limit_exceeded) return -1;
    if (len == 0) return 0;
    if (nc_buf_append(&w->body, data, len) != 0) return -1;
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
    for (int i = 0; i < w->nheaders; i++) {
        if (strcasecmp(w->header_names[i], "Content-Length") == 0 ||
            strcasecmp(w->header_names[i], "Transfer-Encoding") == 0 ||
            strcasecmp(w->header_names[i], "Connection") == 0)
            continue;
        nc_buf_append(&hdr, w->header_names[i], strlen(w->header_names[i]));
        nc_buf_append(&hdr, ": ", 2);
        nc_buf_append(&hdr, w->header_values[i], strlen(w->header_values[i]));
        nc_buf_append(&hdr, "\r\n", 2);

        if (strcasecmp(w->header_names[i], "Content-Type") == 0)
            has_content_type = 1;
    }

    if (!has_content_type) {
        const char *ct = "Content-Type: text/plain; charset=utf-8\r\n";
        nc_buf_append(&hdr, ct, strlen(ct));
    }
    const char *te = "Transfer-Encoding: chunked\r\n";
    nc_buf_append(&hdr, te, strlen(te));
    if (w->ntrailers > 0) {
        nc_buf_append(&hdr, "Trailer: ", 9);
        for (int i = 0; i < w->ntrailers; i++) {
            if (i > 0) nc_buf_append(&hdr, ", ", 2);
            nc_buf_append(&hdr, w->trailer_names[i],
                          strlen(w->trailer_names[i]));
        }
        nc_buf_append(&hdr, "\r\n", 2);
    }
    const char *conn_val = w->keep_alive
        ? "Connection: keep-alive\r\n"
        : "Connection: close\r\n";
    nc_buf_append(&hdr, conn_val, strlen(conn_val));

    nc_buf_append(&hdr, "\r\n", 2);
    if (rw_write_all(w, hdr.data, hdr.len) != 0)
        w->keep_alive = 0;
    nc_buf_free(&hdr);
}

int neverc_http_flush_chunk(neverc_http_response_writer_t *w) {
    if (!w || !w->chunked) return -1;
    if (w->protocol_flush)
        return w->protocol_flush(w->protocol_context, w, 0);
    if (w->head_request || w->status < 200 ||
        w->status == 204 || w->status == 304) {
        rw_flush(w);
        nc_buf_reset(&w->body);
        return 0;
    }

    if (!w->headers_sent)
        rw_send_chunked_headers(w);

    if (w->body.len == 0) return 0;

    char chunk_hdr[32];
    int n = snprintf(chunk_hdr, sizeof(chunk_hdr), "%zx\r\n", w->body.len);
    if (rw_write_all(w, chunk_hdr, (size_t)n) != 0) return -1;
    if (rw_write_all(w, w->body.data, w->body.len) != 0) return -1;
    if (rw_write_all(w, "\r\n", 2) != 0) return -1;

    nc_buf_reset(&w->body);
    return 0;
}

int neverc_http_end_chunked(neverc_http_response_writer_t *w) {
    if (!w || !w->chunked) return -1;
    if (w->protocol_flush)
        return w->protocol_flush(w->protocol_context, w, 1);
    if (w->head_request || w->status < 200 ||
        w->status == 204 || w->status == 304) {
        rw_flush(w);
        nc_buf_reset(&w->body);
        return 0;
    }

    if (w->body.len > 0)
        neverc_http_flush_chunk(w);

    if (!w->headers_sent)
        rw_send_chunked_headers(w);

    nc_buf_t ending;
    nc_buf_init(&ending);
    if (nc_buf_append(&ending, "0\r\n", 3) != 0) {
        nc_buf_free(&ending);
        return -1;
    }
    for (int i = 0; i < w->ntrailers; i++) {
        if (nc_buf_append(&ending, w->trailer_names[i],
                          strlen(w->trailer_names[i])) != 0 ||
            nc_buf_append(&ending, ": ", 2) != 0 ||
            nc_buf_append(&ending, w->trailer_values[i],
                          strlen(w->trailer_values[i])) != 0 ||
            nc_buf_append(&ending, "\r\n", 2) != 0) {
            nc_buf_free(&ending);
            return -1;
        }
    }
    int result = nc_buf_append(&ending, "\r\n", 2) == 0
        ? rw_write_all(w, ending.data, ending.len) : -1;
    nc_buf_free(&ending);
    return result;
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
    neverc_http_handler_context_func_t context_handler;
    void *handler_context;
    int streaming;
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
    if (!mux || !pattern || !handler) return;
    nc_mutex_lock(&mux->lock);
    if (mux->nroutes < MAX_ROUTES) {
        route_parse_pattern(&mux->routes[mux->nroutes], pattern);
        mux->routes[mux->nroutes].handler = handler;
        mux->nroutes++;
    }
    nc_mutex_unlock(&mux->lock);
}

int neverc_http_mux_handle_context(
    neverc_http_mux_t *mux, const char *pattern,
    neverc_http_handler_context_func_t handler, void *context) {
    if (!mux || !pattern || !handler) return -1;
    int result = -1;
    nc_mutex_lock(&mux->lock);
    if (mux->nroutes < MAX_ROUTES) {
        route_t *route = &mux->routes[mux->nroutes];
        route_parse_pattern(route, pattern);
        route->context_handler = handler;
        route->handler_context = context;
        mux->nroutes++;
        result = 0;
    }
    nc_mutex_unlock(&mux->lock);
    return result;
}

int neverc_http_mux_handle_stream_context(
    neverc_http_mux_t *mux, const char *pattern,
    neverc_http_handler_context_func_t handler, void *context) {
    if (!mux || !pattern || !handler) return -1;
    int result = -1;
    nc_mutex_lock(&mux->lock);
    if (mux->nroutes < MAX_ROUTES) {
        route_t *route = &mux->routes[mux->nroutes];
        route_parse_pattern(route, pattern);
        route->context_handler = handler;
        route->handler_context = context;
        route->streaming = 1;
        mux->nroutes++;
        result = 0;
    }
    nc_mutex_unlock(&mux->lock);
    return result;
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

static route_t *mux_match_ex(neverc_http_mux_t *mux,
                             const char *method, const char *path,
                             path_params_t *params) {
    route_t *best = NULL;
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
                    best = r;
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
                best = r;
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
                best = r;
                best_len = plen;
                best_specificity = specificity;
                if (params) { params->len = 0; params->count = 0; }
            }
        }
    }
    return best;
}

static void route_invoke(route_t *route, neverc_http_request_t *request,
                         neverc_http_response_writer_t *writer) {
    if (!route) return;
    if (route->context_handler)
        route->context_handler(request, writer, route->handler_context);
    else if (route->handler)
        route->handler(request, writer);
}

int nc_http_mux_is_streaming(neverc_http_mux_t *mux, const char *method,
                             const char *path) {
    if (!path) return 0;
    if (!mux) {
        ensure_default_mux();
        mux = &default_mux;
    }
    path_params_t ignored;
    memset(&ignored, 0, sizeof(ignored));
    route_t *route = mux_match_ex(mux, method, path, &ignored);
    return route && route->streaming;
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
static int g_gzip_enabled = 0;
static int g_gzip_level = 6;
static size_t g_gzip_min_size = 256;
static int g_access_log_enabled = 0;
static neverc_http_access_log_func_t g_access_log_func = NULL;

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
    if (ms >= 0) g_handler_timeout_ms = ms;
}

void neverc_http_enable_gzip(int level, size_t min_size) {
    g_gzip_enabled = 1;
    g_gzip_level = (level >= 1 && level <= 9) ? level : 6;
    g_gzip_min_size = min_size > 0 ? min_size : 256;
}

void neverc_http_disable_gzip(void) {
    g_gzip_enabled = 0;
}

void neverc_http_enable_access_log(neverc_http_access_log_func_t func) {
    g_access_log_enabled = 1;
    g_access_log_func = func;
}

int neverc_http_server_port(void) {
    return g_server_port;
}

void neverc_http_set_max_bytes(neverc_http_response_writer_t *w,
                                int64_t max_bytes) {
    if (!w || max_bytes < 0) return;
    if (w->request_body_len > (uint64_t)max_bytes) {
        w->body_limit_exceeded = 1;
        w->status = 413;
        w->keep_alive = 0;
        nc_buf_reset(&w->body);
    }
}

void nc_http_mux_dispatch(neverc_http_mux_t *mux,
                          neverc_http_request_t *request,
                          neverc_http_response_writer_t *writer) {
    if (!request || !writer) return;
    if (!mux) {
        ensure_default_mux();
        mux = &default_mux;
    }
    path_params_t params;
    memset(&params, 0, sizeof(params));
    route_t *route = mux_match_ex(
        mux, request->method, request->path, &params);
    if (params.count > 0) {
        request->path_params = params.buf;
        request->nparams = params.count;
    }
    if (g_global_rate_limiter &&
        !neverc_http_rate_limiter_allow(g_global_rate_limiter)) {
        neverc_http_set_status(writer, 429);
        neverc_http_set_header(writer, "Retry-After", "1");
        (void)neverc_http_write_string(writer, "Too Many Requests\n");
        return;
    }
    if (g_cors_enabled) {
        const char *origin = neverc_http_request_header(request, "Origin");
        neverc_http_cors_headers(writer, &g_cors_config, origin);
    }
    if (route) {
        route_invoke(route, request, writer);
    } else {
        neverc_http_set_status(writer, 404);
        (void)neverc_http_write_string(writer, "404 page not found\n");
    }
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

#define HTTP_MAX_REQUEST_HEADERS 128
#define HTTP_MAX_REQUEST_BODY    1073741824U

static int http_is_tchar(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '!' || c == '#' || c == '$' ||
           c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
           c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
           c == '|' || c == '~';
}

static int http_valid_token(const char *s, size_t length) {
    if (!s || length == 0) return 0;
    for (size_t i = 0; i < length; i++)
        if (!http_is_tchar((unsigned char)s[i])) return 0;
    return 1;
}

static int http_valid_field_value(const char *s, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) return 0;
    }
    return 1;
}

static void http_trim_ows(const char **value, size_t *length) {
    while (*length > 0 && (**value == ' ' || **value == '\t')) {
        (*value)++;
        (*length)--;
    }
    while (*length > 0 && ((*value)[*length - 1] == ' ' ||
                            (*value)[*length - 1] == '\t'))
        (*length)--;
}

static int http_field_name_is(const char *name, size_t length,
                              const char *expected) {
    size_t expected_length = strlen(expected);
    return length == expected_length &&
           strcasecmp_n(name, expected, length) == 0;
}

static int http_value_has_token(const char *value, size_t length,
                                const char *expected) {
    size_t expected_length = strlen(expected);
    size_t offset = 0;
    while (offset < length) {
        while (offset < length &&
               (value[offset] == ' ' || value[offset] == '\t' ||
                value[offset] == ','))
            offset++;
        size_t start = offset;
        while (offset < length && value[offset] != ',') offset++;
        size_t token_length = offset - start;
        while (token_length > 0 &&
               (value[start + token_length - 1] == ' ' ||
                value[start + token_length - 1] == '\t'))
            token_length--;
        if (token_length == expected_length &&
            strcasecmp_n(value + start, expected, token_length) == 0)
            return 1;
    }
    return 0;
}

static int http_parse_content_length(const char *value, size_t length,
                                     int *result) {
    if (!value || length == 0) return -1;
    size_t parsed = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < '0' || c > '9' ||
            parsed > (HTTP_MAX_REQUEST_BODY - (size_t)(c - '0')) / 10)
            return -1;
        parsed = parsed * 10 + (size_t)(c - '0');
    }
    *result = (int)parsed;
    return 0;
}

static int parsed_request_add_header(parsed_request_t *request,
                                     const char *name, size_t name_length,
                                     const char *value, size_t value_length) {
    if (request->nheaders >= HTTP_MAX_REQUEST_HEADERS) return -1;
    if (request->nheaders >= request->header_cap) {
        int new_capacity = request->header_cap > 0
            ? request->header_cap * 2 : 32;
        if (new_capacity > HTTP_MAX_REQUEST_HEADERS)
            new_capacity = HTTP_MAX_REQUEST_HEADERS;
        char **new_names = (char **)malloc(
            (size_t)new_capacity * sizeof(*new_names));
        char **new_values = (char **)malloc(
            (size_t)new_capacity * sizeof(*new_values));
        if (!new_names || !new_values) {
            free(new_names);
            free(new_values);
            return -1;
        }
        if (request->nheaders > 0) {
            memcpy(new_names, request->header_names,
                   (size_t)request->nheaders * sizeof(*new_names));
            memcpy(new_values, request->header_values,
                   (size_t)request->nheaders * sizeof(*new_values));
        }
        free(request->header_names);
        free(request->header_values);
        request->header_names = new_names;
        request->header_values = new_values;
        request->header_cap = new_capacity;
    }
    char *name_copy = strndup_safe(name, name_length);
    char *value_copy = strndup_safe(value, value_length);
    if (!name_copy || !value_copy) {
        free(name_copy);
        free(value_copy);
        return -1;
    }
    request->header_names[request->nheaders] = name_copy;
    request->header_values[request->nheaders] = value_copy;
    request->nheaders++;
    return 0;
}

static const char *http_find_crlf(const char *start, const char *end) {
    for (const char *p = start; p + 1 < end; p++)
        if (p[0] == '\r' && p[1] == '\n') return p;
    return NULL;
}

static int http_parse_chunk_size(const char *line, size_t length,
                                 size_t *chunk_size) {
    size_t value = 0;
    size_t digits = 0;
    while (digits < length) {
        unsigned char c = (unsigned char)line[digits];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else break;
        if (value > (HTTP_MAX_REQUEST_BODY - digit) / 16) return -1;
        value = value * 16 + digit;
        digits++;
    }
    if (digits == 0 || (digits < length && line[digits] != ';')) return -1;
    if (!http_valid_field_value(line + digits, length - digits)) return -1;
    *chunk_size = value;
    return 0;
}

static int parse_chunked_request_body(const char *raw, size_t raw_length,
                                      size_t header_size,
                                      parsed_request_t *request,
                                      size_t *consumed) {
    const char *body = raw + header_size;
    const char *end = raw + raw_length;
    const char *cursor = body;
    nc_buf_t decoded;
    nc_buf_init(&decoded);

    for (;;) {
        const char *line_end = http_find_crlf(cursor, end);
        if (!line_end) goto incomplete;
        size_t chunk_size = 0;
        if (http_parse_chunk_size(cursor, (size_t)(line_end - cursor),
                                  &chunk_size) != 0)
            goto invalid;
        cursor = line_end + 2;

        if (chunk_size == 0) {
            for (;;) {
                line_end = http_find_crlf(cursor, end);
                if (!line_end) goto incomplete;
                if (line_end == cursor) {
                    cursor += 2;
                    request->body = decoded.data;
                    request->body_len = decoded.len;
                    *consumed = (size_t)(cursor - raw);
                    return 0;
                }
                if (*cursor == ' ' || *cursor == '\t') goto invalid;
                const char *colon = (const char *)memchr(
                    cursor, ':', (size_t)(line_end - cursor));
                if (!colon || !http_valid_token(
                        cursor, (size_t)(colon - cursor)))
                    goto invalid;
                const char *value = colon + 1;
                size_t value_length = (size_t)(line_end - value);
                http_trim_ows(&value, &value_length);
                if (!http_valid_field_value(value, value_length) ||
                    http_field_name_is(cursor, (size_t)(colon - cursor),
                                       "Content-Length") ||
                    http_field_name_is(cursor, (size_t)(colon - cursor),
                                       "Transfer-Encoding") ||
                    http_field_name_is(cursor, (size_t)(colon - cursor),
                                       "Host") ||
                    parsed_request_add_header(
                        request, cursor, (size_t)(colon - cursor),
                        value, value_length) != 0)
                    goto invalid;
                cursor = line_end + 2;
            }
        }

        size_t available = (size_t)(end - cursor);
        if (chunk_size > available || available - chunk_size < 2)
            goto incomplete;
        if (cursor[chunk_size] != '\r' || cursor[chunk_size + 1] != '\n')
            goto invalid;
        if (decoded.len > HTTP_MAX_REQUEST_BODY - chunk_size ||
            nc_buf_append(&decoded, cursor, chunk_size) != 0)
            goto invalid;
        cursor += chunk_size + 2;
    }

incomplete:
    nc_buf_free(&decoded);
    return -1;
invalid:
    nc_buf_free(&decoded);
    return -2;
}

/* Returns 0 on success, -1 if incomplete, -2 on invalid or ambiguous input. */
static int parse_request(const char *raw, size_t raw_length,
                         parsed_request_t *request, size_t *consumed) {
    memset(request, 0, sizeof(*request));
    request->content_length = -1;
    request->keep_alive = 1;
    if (!raw || !consumed) return -2;

    const char *end = raw + raw_length;
    const char *header_end = NULL;
    for (const char *p = raw; p + 3 < end; p++) {
        if (p[0] == '\r' && p[1] == '\n' &&
            p[2] == '\r' && p[3] == '\n') {
            header_end = p;
            break;
        }
    }
    if (!header_end) return -1;

    const char *request_line_end = http_find_crlf(raw, header_end + 2);
    if (!request_line_end || request_line_end == raw) goto invalid;
    const char *method_end = (const char *)memchr(
        raw, ' ', (size_t)(request_line_end - raw));
    if (!method_end || !http_valid_token(raw, (size_t)(method_end - raw)))
        goto invalid;
    const char *target = method_end + 1;
    const char *target_end = (const char *)memchr(
        target, ' ', (size_t)(request_line_end - target));
    if (!target_end || target_end == target ||
        memchr(target_end + 1, ' ',
               (size_t)(request_line_end - target_end - 1)))
        goto invalid;
    size_t target_length = (size_t)(target_end - target);
    if (target[0] != '/' && !(target_length == 1 && target[0] == '*'))
        goto invalid;
    for (size_t i = 0; i < target_length; i++) {
        unsigned char c = (unsigned char)target[i];
        if (c <= 0x20 || c == 0x7f || c == '#') goto invalid;
    }

    const char *version = target_end + 1;
    size_t version_length = (size_t)(request_line_end - version);
    int is_http_10 = version_length == 8 &&
                     memcmp(version, "HTTP/1.0", 8) == 0;
    int is_http_11 = version_length == 8 &&
                     memcmp(version, "HTTP/1.1", 8) == 0;
    if (!is_http_10 && !is_http_11) goto invalid;
    request->keep_alive = is_http_11;

    request->method = strndup_safe(raw, (size_t)(method_end - raw));
    const char *query = (const char *)memchr(target, '?', target_length);
    if (query) {
        request->path = strndup_safe(target, (size_t)(query - target));
        request->query = strndup_safe(
            query + 1, (size_t)(target_end - query - 1));
    } else {
        request->path = strndup_safe(target, target_length);
    }
    request->http_version = strndup_safe(version, version_length);
    if (!request->method || !request->path || !request->http_version ||
        (query && !request->query))
        goto invalid;

    int host_seen = 0;
    int content_length_seen = 0;
    int transfer_encoding_seen = 0;
    const char *cursor = request_line_end + 2;
    while (cursor < header_end) {
        const char *line_end = http_find_crlf(cursor, header_end + 2);
        if (!line_end || line_end == cursor ||
            *cursor == ' ' || *cursor == '\t')
            goto invalid;
        const char *colon = (const char *)memchr(
            cursor, ':', (size_t)(line_end - cursor));
        if (!colon || !http_valid_token(cursor, (size_t)(colon - cursor)))
            goto invalid;
        const char *value = colon + 1;
        size_t value_length = (size_t)(line_end - value);
        http_trim_ows(&value, &value_length);
        size_t name_length = (size_t)(colon - cursor);
        if (!http_valid_field_value(value, value_length)) goto invalid;

        if (http_field_name_is(cursor, name_length, "Host")) {
            if (host_seen || value_length == 0) goto invalid;
            host_seen = 1;
            request->host = strndup_safe(value, value_length);
            if (!request->host) goto invalid;
        } else if (http_field_name_is(cursor, name_length,
                                      "Content-Length")) {
            if (content_length_seen ||
                http_parse_content_length(value, value_length,
                                          &request->content_length) != 0)
                goto invalid;
            content_length_seen = 1;
        } else if (http_field_name_is(cursor, name_length,
                                      "Transfer-Encoding")) {
            if (transfer_encoding_seen || value_length != 7 ||
                strcasecmp_n(value, "chunked", 7) != 0)
                goto invalid;
            transfer_encoding_seen = 1;
            request->is_chunked = 1;
        } else if (http_field_name_is(cursor, name_length,
                                      "Content-Type") &&
                   !request->content_type) {
            request->content_type = strndup_safe(value, value_length);
            if (!request->content_type) goto invalid;
        } else if (http_field_name_is(cursor, name_length, "Connection")) {
            if (http_value_has_token(value, value_length, "close"))
                request->keep_alive = 0;
            else if (is_http_10 &&
                     http_value_has_token(value, value_length, "keep-alive"))
                request->keep_alive = 1;
        } else if (http_field_name_is(cursor, name_length, "Expect")) {
            if (value_length != 12 ||
                strcasecmp_n(value, "100-continue", 12) != 0)
                goto invalid;
            request->expect_continue = 1;
        }

        if (parsed_request_add_header(request, cursor, name_length,
                                      value, value_length) != 0)
            goto invalid;
        cursor = line_end + 2;
    }

    if ((is_http_11 && !host_seen) ||
        (content_length_seen && transfer_encoding_seen) ||
        (is_http_10 && transfer_encoding_seen))
        goto invalid;

    size_t header_size = (size_t)(header_end + 4 - raw);
    if (request->is_chunked) {
        int result = parse_chunked_request_body(
            raw, raw_length, header_size, request, consumed);
        if (result == 0) return 0;
        if (result == -1) goto incomplete;
        goto invalid;
    }

    size_t body_length = request->content_length > 0
        ? (size_t)request->content_length : 0;
    if (body_length > raw_length - header_size) goto incomplete;
    if (body_length > 0) {
        request->body = raw + header_size;
        request->body_len = body_length;
    }
    *consumed = header_size + body_length;
    return 0;

incomplete:
    parsed_request_free(request);
    memset(request, 0, sizeof(*request));
    return -1;
invalid:
    parsed_request_free(request);
    memset(request, 0, sizeof(*request));
    return -2;
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
    uint64_t           request_started;
    int                idle_timeout_ms;
    int                read_header_timeout_ms;
    int                read_timeout_ms;
    int                write_timeout_ms;
    size_t             max_read_size;
    int                gzip_enabled;
    int                gzip_level;
    size_t             gzip_min_size;
    int                access_log_enabled;
    neverc_http_access_log_func_t access_log;
    int                handler_timeout_ms;
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
                                    const neverc_http_server_config_t *config) {
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
    hc->max_requests = config->max_requests_per_connection;
    hc->requests_served = 0;
    hc->idle_timeout_ms = config->idle_timeout_ms;
    hc->read_header_timeout_ms = config->read_header_timeout_ms;
    hc->read_timeout_ms = config->read_timeout_ms;
    hc->write_timeout_ms = config->write_timeout_ms;
    hc->max_read_size = (size_t)config->max_header_size +
                        (size_t)config->max_body_size;
    hc->gzip_enabled = config->gzip_enabled;
    hc->gzip_level = config->gzip_level;
    hc->gzip_min_size = config->gzip_min_size;
    hc->access_log_enabled = config->access_log_enabled;
    hc->access_log = config->access_log;
    hc->handler_timeout_ms = config->handler_timeout_ms;
    hc->last_active = nc_monotonic_ms();
    hc->request_started = hc->last_active;
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

static void http_request_context_release(
    neverc_context_t *context,
    neverc_context_cancel_handle_t *cancel_handle) {
    if (!cancel_handle) return;
    neverc_context_cancel_handle_cancel(cancel_handle);
    neverc_context_cancel_handle_free(cancel_handle);
    neverc_context_free(context);
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
        neverc_context_cancel_handle_t *request_cancel = NULL;
        neverc_context_t *request_context = neverc_context_background();
        if (hc->handler_timeout_ms > 0) {
            request_context = neverc_context_with_timeout_handle(
                neverc_context_background(), hc->handler_timeout_ms,
                &request_cancel);
            if (!request_context) {
                parsed_request_free(&pr);
                hc->state = HC_STATE_CLOSING;
                return;
            }
        }
        req.context = request_context;

        /* Route with path parameter extraction */
        path_params_t params;
        memset(&params, 0, sizeof(params));
        route_t *route =
            mux_match_ex(hc->mux, req.method, req.path, &params);

        if (params.count > 0) {
            req.path_params = params.buf;
            req.nparams = params.count;
        }

        neverc_http_response_writer_t *w =
            rw_new(hc->fd, pr.keep_alive, hc, consumed);
        if (!w) {
            http_request_context_release(request_context, request_cancel);
            parsed_request_free(&pr);
            hc->state = HC_STATE_CLOSING;
            return;
        }
        w->request_body_len = req.body_len;
        w->head_request = strcmp(req.method, "HEAD") == 0;
        w->gzip_enabled = hc->gzip_enabled;
        w->gzip_level = hc->gzip_level;
        w->gzip_min_size = hc->gzip_min_size;
        w->write_timeout_ms = hc->write_timeout_ms;
        const char *accept_encoding =
            neverc_http_request_header(&req, "Accept-Encoding");
        w->accepts_gzip = accept_encoding && http_value_has_token(
            accept_encoding, strlen(accept_encoding), "gzip");
        uint64_t handler_start = nc_monotonic_ms();

        /* Auto-inject CORS headers when enabled */
        if (g_cors_enabled && w) {
            const char *origin = neverc_http_request_header(&req, "Origin");
            neverc_http_cors_headers(w, &g_cors_config, origin);
        }

        if (route) {
            route_invoke(route, &req, w);
        } else {
            neverc_http_set_status(w, 404);
            neverc_http_write_string(w, "404 page not found\n");
        }

        if (hc->handler_timeout_ms > 0 &&
            neverc_context_done(request_context)) {
            w->keep_alive = 0;
            if (!w->headers_sent) {
                w->status = 503;
                w->body_limit_exceeded = 0;
                nc_buf_reset(&w->body);
                neverc_http_set_header(w, "Content-Type",
                                       "text/plain; charset=utf-8");
                (void)neverc_http_write_string(w, "handler timeout\n");
            }
        }

        /* Free chunked body if it was allocated separately */
        if (pr.is_chunked && pr.body) {
            free((void *)pr.body);
            pr.body = NULL;
        }

        if (w->hijacked) {
            http_request_context_release(request_context, request_cancel);
            rw_free(w);
            parsed_request_free(&pr);
            hc->requests_served++;
            hc->last_active = nc_monotonic_ms();
            hc->state = HC_STATE_CLOSING;
            return;
        }

        rw_flush(w);

        if (hc->access_log_enabled) {
            double duration =
                (double)(nc_monotonic_ms() - handler_start);
            if (hc->access_log) {
                hc->access_log(req.method, req.path, w->status,
                               duration, w->body.len);
            } else {
                fprintf(stdout, "%s %s %d %.3fms %zu\n",
                        req.method, req.path, w->status,
                        duration, w->body.len);
            }
        }
        http_request_context_release(request_context, request_cancel);

        int should_close = !w->keep_alive;
        rw_free(w);

        nc_buf_consume(&hc->read_buf, consumed);
        parsed_request_free(&pr);
        hc->requests_served++;
        hc->last_active = nc_monotonic_ms();
        hc->request_started = hc->read_buf.len > 0 ? hc->last_active : 0;

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
            if (hc->read_buf.len == 0 && hc->request_started == 0)
                hc->request_started = nc_monotonic_ms();
            if (nc_buf_append(&hc->read_buf, chunk, (size_t)n) != 0) {
                hc->state = HC_STATE_CLOSING;
                return;
            }
            hc->last_active = nc_monotonic_ms();
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
    neverc_http_server_t *server;
};

struct neverc_http_server {
    volatile int           serving;
    volatile int           running;
    volatile int           stop_requested;
    nc_sock_t              listen_fd;
    neverc_http_mux_t     *mux;
    neverc_http_server_config_t config;
    nc_conn_limiter_t      conn_limiter;
    nc_mutex_t             lifecycle_lock;
    volatile int           bound_port;

    http_worker_t         *workers;
    int                    nworkers;
    volatile int           next_worker; /* atomic round-robin index */

    nc_evloop_t           *accept_loop;
    neverc_h2_server_t    *h2_server;
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
        if (w && w->server)
            nc_conn_limiter_release(&w->server->conn_limiter);
        http_conn_free(hc);
    }
}

static void worker_sweep_idle(http_worker_t *w) {
    uint64_t now = nc_monotonic_ms();
    http_conn_t *hc = w->conns.head;
    while (hc) {
        http_conn_t *next = hc->next;
        int request_timed_out = 0;
        if (hc->request_started > 0) {
            int headers_complete = hc->read_buf.data &&
                strstr(hc->read_buf.data, "\r\n\r\n") != NULL;
            int timeout = headers_complete
                ? hc->read_timeout_ms : hc->read_header_timeout_ms;
            request_timed_out = timeout > 0 &&
                now - hc->request_started > (uint64_t)timeout;
        }
        int idle_timed_out = hc->request_started == 0 &&
            hc->idle_timeout_ms > 0 &&
            now - hc->last_active > (uint64_t)hc->idle_timeout_ms;
        if (request_timed_out || idle_timed_out) {
            if (request_timed_out) {
                const char *timeout_response =
                    "HTTP/1.1 408 Request Timeout\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n";
                (void)sock_write_all(hc->fd, timeout_response,
                                     strlen(timeout_response));
            }
            conn_list_remove(&w->conns, hc);
            nc_atomic_dec(&w->conn_count);
            nc_conn_limiter_release(&w->server->conn_limiter);
            http_conn_free(hc);
        }
        hc = next;
    }
}

static void *worker_thread_func(void *arg) {
    http_worker_t *w = (http_worker_t *)arg;
    nc_evloop_t *loop = w->loop;
    if (!nc_atomic_cas(&loop->running, 0, 1))
        return NULL;
    nc_event_t events[NC_EVLOOP_MAX_EVENTS];
    uint64_t last_sweep = nc_monotonic_ms();

    for (;;) {
        if (nc_atomic_load(&loop->stop_requested)) {
            nc_evloop_dispatch_pending(loop);
            break;
        }

        int n = nc_poller_wait(loop->poller, events, NC_EVLOOP_MAX_EVENTS, 100);
        if (n < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            nc_atomic_store(&loop->stop_requested, 1);
            break;
        }

        /* Drain the cross-thread signal before taking the task snapshot. */
        for (int i = 0; i < n; i++) {
            if (events[i].data == &loop->wakeup_marker) {
                nc_evloop_drain_wakeup(loop);
                events[i].data = NULL;
            }
        }

        nc_evloop_dispatch_pending(loop);
        if (nc_atomic_load(&loop->stop_requested))
            break;

        /* Process I/O events */
        for (int i = 0; i < n; i++) {
            if (events[i].data == NULL) continue;
            worker_event_handler(loop, &events[i]);
            if (nc_atomic_load(&loop->stop_requested))
                break;
        }

        /* Deadlines are checked at the poller's 100ms granularity. */
        uint64_t now = nc_monotonic_ms();
        if (now - last_sweep >= 100) {
            worker_sweep_idle(w);
            last_sweep = now;
        }
    }
#ifndef _WIN32
    if (nc_atomic_load(&loop->wakeup_pending))
        nc_evloop_drain_wakeup(loop);
#else
    nc_atomic_store(&loop->wakeup_pending, 0);
#endif
    nc_atomic_store(&loop->running, 0);
    return NULL;
}

static void distribute_conn_task(void *arg) {
    http_conn_t *hc = (http_conn_t *)arg;
    http_worker_t *worker = hc->worker;

    nc_set_nodelay(hc->fd);
    nc_set_keepalive(hc->fd);
    nc_set_quickack(hc->fd);

    if (nc_poller_add(hc->loop->poller, hc->fd, NC_EV_READ, hc) != 0) {
        nc_sock_close(hc->fd);
        hc->fd = NC_INVALID_SOCK;
        nc_conn_limiter_release(&worker->server->conn_limiter);
        http_conn_free(hc);
        nc_atomic_dec(&worker->conn_count);
        return;
    }
    conn_list_add(&worker->conns, hc);

    /*
     * Edge-triggered pollers (kqueue EV_CLEAR / epoll EPOLLET) may not fire
     * if data arrived before we added the fd. Do an eager read to handle
     * this race condition.
     */
    http_conn_on_read(hc);
    if (hc->state == HC_STATE_CLOSING) {
        conn_list_remove(&worker->conns, hc);
        nc_conn_limiter_release(&worker->server->conn_limiter);
        http_conn_free(hc);
        nc_atomic_dec(&worker->conn_count);
    }
}

/* Accept loop event handler */
static void accept_event_handler(nc_evloop_t *loop, nc_event_t *ev) {
    neverc_http_server_t *srv = (neverc_http_server_t *)ev->data;
    if (!srv || !nc_atomic_load(&srv->running)) return;

    while (nc_atomic_load(&srv->running) &&
           !nc_atomic_load(&loop->stop_requested)) {
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
        if (!nc_conn_limiter_try_acquire(&srv->conn_limiter)) {
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

        http_conn_t *hc = http_conn_new(cfd, worker->loop, srv->mux,
                                          worker, &srv->config);
        if (!hc) {
            nc_sock_close(cfd);
            nc_conn_limiter_release(&srv->conn_limiter);
            continue;
        }

        nc_atomic_inc(&worker->conn_count);
        if (nc_evloop_post(worker->loop, distribute_conn_task, hc) != 0) {
            nc_atomic_dec(&worker->conn_count);
            nc_sock_close(hc->fd);
            hc->fd = NC_INVALID_SOCK;
            http_conn_free(hc);
            nc_conn_limiter_release(&srv->conn_limiter);
        }
    }
    (void)loop;
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

neverc_http_server_config_t neverc_http_server_config_default(void) {
    neverc_http_server_config_t config;
    config.workers = 0;
    config.max_requests_per_connection = 1000;
    config.read_timeout_ms = 60000;
    config.read_header_timeout_ms = 10000;
    config.write_timeout_ms = 60000;
    config.idle_timeout_ms = 60000;
    config.max_connections = 0;
    config.max_header_size = 1024 * 1024;
    config.max_body_size = 10 * 1024 * 1024;
    config.shutdown_timeout_ms = 5000;
    config.handler_timeout_ms = 0;
    config.gzip_enabled = 0;
    config.gzip_level = 6;
    config.gzip_min_size = 256;
    config.access_log_enabled = 0;
    config.access_log = NULL;
    return config;
}

static int server_config_valid(const neverc_http_server_config_t *config) {
    return config && config->workers >= 0 && config->workers <= 256 &&
           config->max_requests_per_connection > 0 &&
           config->read_timeout_ms >= 0 &&
           config->read_header_timeout_ms >= 0 &&
           config->write_timeout_ms >= 0 && config->idle_timeout_ms >= 0 &&
           config->max_connections >= 0 && config->max_header_size >= 0 &&
           config->max_body_size >= 0 && config->shutdown_timeout_ms >= 0 &&
           config->handler_timeout_ms >= 0 &&
           (config->gzip_enabled == 0 || config->gzip_enabled == 1) &&
           config->gzip_level >= 1 && config->gzip_level <= 9 &&
           config->gzip_min_size > 0 &&
           (config->access_log_enabled == 0 ||
            config->access_log_enabled == 1);
}

neverc_http_server_t *neverc_http_server_new(
    neverc_http_mux_t *mux, const neverc_http_server_config_t *config) {
    neverc_http_server_config_t effective = config
        ? *config : neverc_http_server_config_default();
    if (!server_config_valid(&effective)) return NULL;
    if (effective.max_header_size == 0)
        effective.max_header_size = 1024 * 1024;
    if (effective.max_body_size == 0)
        effective.max_body_size = 10 * 1024 * 1024;

    if (!mux) {
        ensure_default_mux();
        mux = &default_mux;
    }

    neverc_http_server_t *server =
        (neverc_http_server_t *)calloc(1, sizeof(*server));
    if (!server) return NULL;
    server->listen_fd = NC_INVALID_SOCK;
    server->mux = mux;
    server->config = effective;
    nc_conn_limiter_init(&server->conn_limiter, effective.max_connections);
    nc_mutex_init(&server->lifecycle_lock);
    return server;
}

void neverc_http_server_free(neverc_http_server_t *server) {
    if (!server) return;
    nc_mutex_lock(&server->lifecycle_lock);
    if (nc_atomic_load(&server->serving)) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return;
    }
    nc_mutex_unlock(&server->lifecycle_lock);
    nc_mutex_destroy(&server->lifecycle_lock);
    free(server);
}

static nc_sock_t http_server_bind(const char *addr, int *bound_port) {
    char host[256] = {0};
    uint16_t port = 0;
    if (!addr || nc_parse_addr(addr, host, sizeof(host), &port) != 0)
        return NC_INVALID_SOCK;

    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
    if (getaddrinfo(host[0] ? host : NULL, portstr, &hints, &result) != 0)
        return NC_INVALID_SOCK;

    nc_sock_t listen_fd = NC_INVALID_SOCK;
    for (struct addrinfo *candidate = result; candidate;
         candidate = candidate->ai_next) {
        listen_fd = socket(candidate->ai_family, candidate->ai_socktype,
                           candidate->ai_protocol);
        if (listen_fd == NC_INVALID_SOCK) continue;

        (void)nc_set_reuseaddr(listen_fd);
#if !defined(_WIN32) && defined(SO_REUSEPORT)
        (void)nc_set_reuseport(listen_fd);
#endif
        if (candidate->ai_family == AF_INET6 && host[0] == '\0') {
            int v6only = 0;
#ifdef _WIN32
            (void)setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY,
                             (const char *)&v6only, sizeof(v6only));
#else
            (void)setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY,
                             &v6only, sizeof(v6only));
#endif
        }
        (void)nc_set_defer_accept(listen_fd);

        if (bind(listen_fd, candidate->ai_addr,
                 (int)candidate->ai_addrlen) != NC_SOCK_ERR &&
            listen(listen_fd, 65535) != NC_SOCK_ERR &&
            nc_set_nonblocking(listen_fd) == 0)
            break;

        nc_sock_close(listen_fd);
        listen_fd = NC_INVALID_SOCK;
    }
    freeaddrinfo(result);
    if (listen_fd == NC_INVALID_SOCK) return listen_fd;

    struct sockaddr_storage actual;
    socklen_t actual_len = sizeof(actual);
    if (getsockname(listen_fd, (struct sockaddr *)&actual, &actual_len) == 0) {
        if (actual.ss_family == AF_INET)
            *bound_port = ntohs(((struct sockaddr_in *)&actual)->sin_port);
        else if (actual.ss_family == AF_INET6)
            *bound_port = ntohs(((struct sockaddr_in6 *)&actual)->sin6_port);
    }
    return listen_fd;
}

int neverc_http_server_listen_and_serve(neverc_http_server_t *server,
                                        const char *addr) {
    if (!server || !addr || nc_net_init() != 0 ||
        !nc_poller_supports_readiness() ||
        !nc_atomic_cas(&server->serving, 0, 1))
        return -1;

    int rc = -1;
    int initialized_workers = 0;
    int started_workers = 0;
    nc_atomic_store(&server->bound_port, 0);
    nc_conn_limiter_init(&server->conn_limiter,
                         server->config.max_connections);

    int actual_port = 0;
    nc_sock_t listen_fd = http_server_bind(addr, &actual_port);
    if (listen_fd == NC_INVALID_SOCK) goto done;
    server->listen_fd = listen_fd;
    nc_atomic_store(&server->bound_port, actual_port);
    if (nc_atomic_ptr_load(&g_server_ptr) == server)
        g_server_port = actual_port;
    if (nc_atomic_load(&server->stop_requested)) {
        rc = 0;
        goto done;
    }

    int worker_count = server->config.workers > 0
        ? server->config.workers : nc_cpu_count();
    if (worker_count < 1) worker_count = 1;
    if (worker_count > 64) worker_count = 64;
    server->nworkers = worker_count;
    server->workers = (http_worker_t *)calloc(
        (size_t)worker_count, sizeof(*server->workers));
    if (!server->workers) goto done;

    for (int i = 0; i < worker_count; i++) {
        http_worker_t *worker = &server->workers[i];
        conn_list_init(&worker->conns);
        initialized_workers++;
        worker->worker_idx = i;
        worker->server = server;
        worker->loop = nc_evloop_create();
        if (!worker->loop) goto stop_workers;
    }

    server->accept_loop = nc_evloop_create();
    if (!server->accept_loop ||
        nc_poller_add(server->accept_loop->poller, listen_fd,
                      NC_EV_READ, server) != 0)
        goto stop_workers;

    for (int i = 0; i < worker_count; i++) {
        if (nc_thread_create(&server->workers[i].thread, worker_thread_func,
                             &server->workers[i]) != 0)
            goto stop_workers;
        started_workers++;
    }

    nc_atomic_store(&server->running, 1);
    if (nc_atomic_load(&server->stop_requested))
        neverc_http_server_shutdown(server);
    if (nc_atomic_load(&server->running))
        nc_evloop_run(server->accept_loop, accept_event_handler);
    rc = 0;

stop_workers:
    nc_mutex_lock(&server->lifecycle_lock);
    nc_atomic_store(&server->running, 0);
    for (int i = 0; i < started_workers; i++)
        nc_evloop_stop(server->workers[i].loop);
    nc_mutex_unlock(&server->lifecycle_lock);

    for (int i = 0; i < started_workers; i++)
        nc_thread_join(server->workers[i].thread);
    for (int i = 0; i < initialized_workers; i++) {
        http_conn_t *connection = server->workers[i].conns.head;
        while (connection) {
            http_conn_t *next = connection->next;
            nc_conn_limiter_release(&server->conn_limiter);
            http_conn_free(connection);
            connection = next;
        }
        if (server->workers[i].loop)
            nc_evloop_destroy(server->workers[i].loop);
        nc_mutex_destroy(&server->workers[i].conns.lock);
    }
    if (server->accept_loop)
        nc_evloop_destroy(server->accept_loop);
    server->accept_loop = NULL;
    free(server->workers);
    server->workers = NULL;
    server->nworkers = 0;

done:
    if (server->listen_fd != NC_INVALID_SOCK)
        nc_sock_close(server->listen_fd);
    server->listen_fd = NC_INVALID_SOCK;
    nc_atomic_store(&server->running, 0);
    nc_atomic_store(&server->serving, 0);
    return rc;
}

void neverc_http_server_shutdown(neverc_http_server_t *server) {
    if (!server) return;
    nc_atomic_store(&server->stop_requested, 1);
    nc_mutex_lock(&server->lifecycle_lock);
    if (!nc_atomic_load(&server->running)) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return;
    }
    nc_atomic_store(&server->running, 0);
    if (server->accept_loop)
        nc_evloop_stop(server->accept_loop);
    if (server->h2_server)
        neverc_h2_server_shutdown(server->h2_server);

    uint64_t drain_start = nc_monotonic_ms();
    int drain_ms = server->config.shutdown_timeout_ms;
    while (drain_ms > 0) {
        if (nc_conn_limiter_count(&server->conn_limiter) <= 0) break;
        if (nc_monotonic_ms() - drain_start >= (uint64_t)drain_ms) break;
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    for (int i = 0; i < server->nworkers; i++)
        nc_evloop_stop(server->workers[i].loop);
    nc_mutex_unlock(&server->lifecycle_lock);
}

int neverc_http_server_active_connections(neverc_http_server_t *server) {
    return server ? nc_conn_limiter_count(&server->conn_limiter) : 0;
}

int neverc_http_server_bound_port(neverc_http_server_t *server) {
    return server ? nc_atomic_load(&server->bound_port) : 0;
}

static neverc_http_server_config_t legacy_server_config(void) {
    neverc_http_server_config_t config = neverc_http_server_config_default();
    config.workers = g_config_workers;
    config.max_requests_per_connection = g_config_max_requests;
    config.read_timeout_ms = g_config_read_timeout;
    config.read_header_timeout_ms = g_config_read_header_timeout;
    config.write_timeout_ms = g_config_write_timeout;
    config.idle_timeout_ms = g_config_idle_timeout;
    config.max_connections = g_config_max_conns;
    config.max_header_size = g_config_max_header_size;
    config.max_body_size = g_config_max_body_size;
    config.shutdown_timeout_ms = g_config_shutdown_timeout;
    config.handler_timeout_ms = g_handler_timeout_ms;
    config.gzip_enabled = g_gzip_enabled;
    config.gzip_level = g_gzip_level;
    config.gzip_min_size = g_gzip_min_size;
    config.access_log_enabled = g_access_log_enabled;
    config.access_log = g_access_log_func;
    return config;
}

int neverc_http_active_connections(void) {
    neverc_http_server_t *server =
        (neverc_http_server_t *)nc_atomic_ptr_load(&g_server_ptr);
    return neverc_http_server_active_connections(server);
}

int neverc_http_listen_and_serve(const char *addr, neverc_http_mux_t *mux) {
    neverc_http_server_config_t config = legacy_server_config();
    neverc_http_server_t *server = neverc_http_server_new(mux, &config);
    if (!server) return -1;
    if (!nc_atomic_ptr_cas(&g_server_ptr, NULL, server)) {
        neverc_http_server_free(server);
        return -1;
    }
    int rc = neverc_http_server_listen_and_serve(server, addr);
    g_server_port = neverc_http_server_bound_port(server);
    (void)nc_atomic_ptr_cas(&g_server_ptr, server, NULL);
    neverc_http_server_free(server);
    return rc;
}

void neverc_http_shutdown(void) {
    neverc_http_server_t *server =
        (neverc_http_server_t *)nc_atomic_ptr_exchange(&g_server_ptr, NULL);
    neverc_http_server_shutdown(server);
}

/* ======================================================================
 * HTTPS — shared accept reactor and HTTP/1.1 request pipeline
 * ====================================================================== */

typedef struct {
    neverc_http_server_t *server;
    neverc_tls_config_t  *tls_config;
    nc_threadpool_t      *pool;
} https_accept_ctx_t;

typedef struct {
    neverc_http_server_t *server;
    neverc_tls_config_t  *tls_config;
    neverc_tcp_conn_t    *tcp;
} https_conn_ctx_t;

static int https_transport_write(void *context, const void *data,
                                 size_t len, int timeout_ms) {
    neverc_tls_conn_t *tls = (neverc_tls_conn_t *)context;
    if (len == 0) return 0;
    neverc_context_t *write_context = NULL;
    neverc_context_cancel_handle_t *cancel = NULL;
    if (timeout_ms > 0) {
        write_context = neverc_context_with_timeout_handle(
            neverc_context_background(), timeout_ms, &cancel);
        if (!write_context || !cancel) {
            if (write_context) neverc_context_free(write_context);
            if (cancel) neverc_context_cancel_handle_free(cancel);
            return -1;
        }
    }
    size_t written = 0;
    while (written < len) {
        size_t chunk = len - written;
        if (chunk > (size_t)INT_MAX) chunk = (size_t)INT_MAX;
        int n = write_context
            ? neverc_tls_write_context(tls, write_context,
                                       (const char *)data + written, chunk)
            : neverc_tls_write(tls, (const char *)data + written, chunk);
        if (n <= 0) break;
        written += (size_t)n;
    }
    if (cancel) neverc_context_cancel_handle_cancel(cancel);
    if (write_context) neverc_context_free(write_context);
    if (cancel) neverc_context_cancel_handle_free(cancel);
    return written == len ? 0 : -1;
}

static int https_read_with_budget(neverc_tls_conn_t *tls, void *data,
                                  size_t len, int timeout_ms,
                                  uint64_t started_ms) {
    if (timeout_ms == 0) return neverc_tls_read(tls, data, len);
    uint64_t elapsed = nc_monotonic_ms() - started_ms;
    if (elapsed >= (uint64_t)timeout_ms) return -1;
    int remaining = timeout_ms - (int)elapsed;
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *context = neverc_context_with_timeout_handle(
        neverc_context_background(), remaining, &cancel);
    if (!context || !cancel) {
        if (context) neverc_context_free(context);
        if (cancel) neverc_context_cancel_handle_free(cancel);
        return -1;
    }
    int result = neverc_tls_read_context(tls, context, data, len);
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_free(context);
    neverc_context_cancel_handle_free(cancel);
    return result;
}

static void https_handle_connection(void *arg) {
    https_conn_ctx_t *connection_ctx = (https_conn_ctx_t *)arg;
    neverc_http_server_t *server = connection_ctx->server;
    neverc_tcp_conn_t *tcp = connection_ctx->tcp;
    const char *tls_error = NULL;
    neverc_tls_conn_t *tls = neverc_tls_server(
        tcp, connection_ctx->tls_config, &tls_error);
    free(connection_ctx);
    (void)tls_error;
    if (!tls) goto close_tcp;

    const char *alpn = neverc_tls_alpn(tls);
    if (alpn && strcmp(alpn, "h2") == 0) {
        neverc_h2_server_t *h2 = server->h2_server;
        if (h2)
            (void)neverc_h2_serve_tls_conn(h2, tls);
        goto close_tls;
    }
    if (alpn && strcmp(alpn, "http/1.1") != 0) goto close_tls;

    nc_buf_t request_buffer;
    nc_buf_t raw_headers;
    nc_buf_init(&request_buffer);
    nc_buf_init(&raw_headers);
    char read_chunk[16384];

    for (int request_count = 0;
         request_count < server->config.max_requests_per_connection;
         request_count++) {
        parsed_request_t parsed;
        size_t consumed = 0;
        int parse_result;
        uint64_t request_started = nc_monotonic_ms();
        for (;;) {
            parse_result = parse_request(request_buffer.data,
                                         request_buffer.len,
                                         &parsed, &consumed);
            if (parse_result != -1) break;
            const char *header_end = request_buffer.data
                ? strstr(request_buffer.data, "\r\n\r\n") : NULL;
            size_t header_size = header_end
                ? (size_t)(header_end - request_buffer.data) + 4 : 0;
            if ((!header_end && request_buffer.len >
                                (size_t)server->config.max_header_size) ||
                (header_end && header_size >
                               (size_t)server->config.max_header_size) ||
                request_buffer.len >
                    (size_t)server->config.max_header_size +
                    (size_t)server->config.max_body_size) {
                const char *response =
                    "HTTP/1.1 413 Payload Too Large\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n";
                (void)https_transport_write(tls, response,
                                             strlen(response),
                                             server->config.write_timeout_ms);
                goto done;
            }
            int read_timeout = header_end
                ? server->config.read_timeout_ms
                : server->config.read_header_timeout_ms;
            int n = https_read_with_budget(tls, read_chunk,
                                           sizeof(read_chunk), read_timeout,
                                           request_started);
            if (n <= 0 || nc_buf_append(&request_buffer, read_chunk,
                                         (size_t)n) != 0)
                goto done;
        }
        if (parse_result == -2) {
            const char *response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            (void)https_transport_write(tls, response, strlen(response),
                                         server->config.write_timeout_ms);
            goto done;
        }
        if (parsed.body_len > (size_t)server->config.max_body_size) {
            parsed_request_free(&parsed);
            const char *response =
                "HTTP/1.1 413 Payload Too Large\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            (void)https_transport_write(tls, response, strlen(response),
                                         server->config.write_timeout_ms);
            goto done;
        }

        neverc_http_request_t request;
        fill_request(&parsed, &request, &raw_headers);
        neverc_context_cancel_handle_t *request_cancel = NULL;
        neverc_context_t *request_context = neverc_context_background();
        if (server->config.handler_timeout_ms > 0) {
            request_context = neverc_context_with_timeout_handle(
                neverc_context_background(),
                server->config.handler_timeout_ms, &request_cancel);
            if (!request_context) {
                parsed_request_free(&parsed);
                goto done;
            }
        }
        request.context = request_context;

        path_params_t params;
        memset(&params, 0, sizeof(params));
        route_t *route = mux_match_ex(
            server->mux, request.method, request.path, &params);
        if (params.count > 0) {
            request.path_params = params.buf;
            request.nparams = params.count;
        }

        neverc_http_response_writer_t *writer = rw_new(
            NC_INVALID_SOCK, parsed.keep_alive, NULL, consumed);
        if (!writer) {
            http_request_context_release(request_context, request_cancel);
            parsed_request_free(&parsed);
            goto done;
        }
        writer->transport_write = https_transport_write;
        writer->transport_context = tls;
        writer->transport_tcp = tcp;
        writer->request_body_len = request.body_len;
        writer->head_request = strcmp(request.method, "HEAD") == 0;
        writer->gzip_enabled = server->config.gzip_enabled;
        writer->gzip_level = server->config.gzip_level;
        writer->gzip_min_size = server->config.gzip_min_size;
        writer->write_timeout_ms = server->config.write_timeout_ms;
        const char *accept_encoding = neverc_http_request_header(
            &request, "Accept-Encoding");
        writer->accepts_gzip = accept_encoding && http_value_has_token(
            accept_encoding, strlen(accept_encoding), "gzip");
        uint64_t handler_started = nc_monotonic_ms();

        if (g_global_rate_limiter &&
            !neverc_http_rate_limiter_allow(g_global_rate_limiter)) {
            neverc_http_set_status(writer, 429);
            neverc_http_set_header(writer, "Retry-After", "1");
            (void)neverc_http_write_string(writer,
                                            "Too Many Requests\r\n");
            writer->keep_alive = 0;
        } else {
            if (g_cors_enabled) {
                const char *origin = neverc_http_request_header(
                    &request, "Origin");
                neverc_http_cors_headers(writer, &g_cors_config, origin);
            }
            if (route)
                route_invoke(route, &request, writer);
            else {
                neverc_http_set_status(writer, 404);
                (void)neverc_http_write_string(writer,
                                                "404 page not found\n");
            }
        }

        if (writer->hijacked) {
            if (parsed.is_chunked && parsed.body) {
                free((void *)parsed.body);
                parsed.body = NULL;
            }
            http_request_context_release(request_context, request_cancel);
            rw_free(writer);
            nc_buf_consume(&request_buffer, consumed);
            parsed_request_free(&parsed);
            nc_buf_free(&raw_headers);
            nc_buf_free(&request_buffer);
            nc_conn_limiter_release(&server->conn_limiter);
            return;
        }

        if (server->config.handler_timeout_ms > 0 &&
            neverc_context_done(request_context)) {
            writer->keep_alive = 0;
            if (!writer->headers_sent) {
                writer->status = 503;
                writer->body_limit_exceeded = 0;
                nc_buf_reset(&writer->body);
                neverc_http_set_header(writer, "Content-Type",
                                       "text/plain; charset=utf-8");
                (void)neverc_http_write_string(writer,
                                                "handler timeout\n");
            }
        }

        if (parsed.is_chunked && parsed.body) {
            free((void *)parsed.body);
            parsed.body = NULL;
        }
        rw_flush(writer);
        if (server->config.access_log_enabled) {
            double duration =
                (double)(nc_monotonic_ms() - handler_started);
            if (server->config.access_log) {
                server->config.access_log(request.method, request.path,
                                          writer->status, duration,
                                          writer->body.len);
            } else {
                fprintf(stdout, "%s %s %d %.3fms %zu\n",
                        request.method, request.path, writer->status,
                        duration, writer->body.len);
            }
        }
        int should_close = !writer->keep_alive;
        http_request_context_release(request_context, request_cancel);
        rw_free(writer);
        nc_buf_consume(&request_buffer, consumed);
        parsed_request_free(&parsed);
        if (should_close) break;
    }

done:
    nc_buf_free(&raw_headers);
    nc_buf_free(&request_buffer);
close_tls:
    neverc_tls_close(tls);
close_tcp:
    neverc_tcp_close(tcp);
    nc_conn_limiter_release(&server->conn_limiter);
}

static void https_accept_event_handler(nc_evloop_t *loop, nc_event_t *event) {
    https_accept_ctx_t *accept_ctx = (https_accept_ctx_t *)event->data;
    neverc_http_server_t *server = accept_ctx ? accept_ctx->server : NULL;
    if (!server || !nc_atomic_load(&server->running)) return;
    while (nc_atomic_load(&server->running) &&
           !nc_atomic_load(&loop->stop_requested)) {
        struct sockaddr_storage peer;
        socklen_t peer_len = sizeof(peer);
        nc_sock_t socket_handle = nc_accept_nonblock(
            server->listen_fd, (struct sockaddr *)&peer, &peer_len);
        if (socket_handle == NC_INVALID_SOCK) {
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) break;
            if (error == WSAEINTR) continue;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
#endif
            break;
        }
        if (!nc_conn_limiter_try_acquire(&server->conn_limiter)) {
            nc_sock_close(socket_handle);
            continue;
        }
        const char *adopt_error = NULL;
        neverc_tcp_conn_t *tcp = neverc_tcp_adopt_handle(
            (uintptr_t)socket_handle, NULL, 0, &adopt_error);
        (void)adopt_error;
        if (!tcp) {
            nc_sock_close(socket_handle);
            nc_conn_limiter_release(&server->conn_limiter);
            continue;
        }
        int handshake_timeout = server->config.read_header_timeout_ms > 0
            ? server->config.read_header_timeout_ms
            : server->config.read_timeout_ms;
        (void)neverc_tcp_set_timeout(tcp, handshake_timeout);
        https_conn_ctx_t *connection_ctx =
            (https_conn_ctx_t *)calloc(1, sizeof(*connection_ctx));
        if (!connection_ctx) {
            neverc_tcp_close(tcp);
            nc_conn_limiter_release(&server->conn_limiter);
            continue;
        }
        connection_ctx->server = server;
        connection_ctx->tls_config = accept_ctx->tls_config;
        connection_ctx->tcp = tcp;
        if (nc_threadpool_submit(accept_ctx->pool,
                                 https_handle_connection,
                                 connection_ctx) != 0) {
            neverc_tcp_close(tcp);
            free(connection_ctx);
            nc_conn_limiter_release(&server->conn_limiter);
        }
    }
}

int neverc_http_server_listen_and_serve_tls(
    neverc_http_server_t *server, const char *addr,
    const char *cert_file, const char *key_file) {
    if (!server || !addr || !cert_file || !key_file ||
        nc_net_init() != 0 || !nc_poller_supports_readiness() ||
        !nc_atomic_cas(&server->serving, 0, 1))
        return -1;

    int result = -1;
    nc_threadpool_t *pool = NULL;
    neverc_h2_server_t *h2_server = NULL;
    neverc_tls_config_t *tls_config = neverc_tls_config_new();
    if (!tls_config || neverc_tls_config_load_cert(
                           tls_config, cert_file, key_file) != 0)
        goto done;
    const char *alpn[] = { "h2", "http/1.1" };
    neverc_tls_config_set_alpn(tls_config, alpn, 2);
    h2_server = neverc_h2_server_create(server->mux);
    if (!h2_server) goto done;
    neverc_h2_server_set_max_body_size(
        h2_server, (size_t)server->config.max_body_size);
    neverc_h2_server_set_handler_timeout(
        h2_server, server->config.handler_timeout_ms);
    server->h2_server = h2_server;

    nc_atomic_store(&server->bound_port, 0);
    nc_conn_limiter_init(&server->conn_limiter,
                         server->config.max_connections);
    int actual_port = 0;
    server->listen_fd = http_server_bind(addr, &actual_port);
    if (server->listen_fd == NC_INVALID_SOCK) goto done;
    nc_atomic_store(&server->bound_port, actual_port);
    if (nc_atomic_ptr_load(&g_server_ptr) == server)
        g_server_port = actual_port;
    if (nc_atomic_load(&server->stop_requested)) {
        result = 0;
        goto done;
    }

    int worker_count = server->config.workers > 0
        ? server->config.workers : nc_cpu_count();
    if (worker_count < 1) worker_count = 1;
    if (worker_count > 64) worker_count = 64;
    pool = nc_threadpool_create(worker_count);
    server->accept_loop = nc_evloop_create();
    if (!pool || !server->accept_loop) goto done;
    https_accept_ctx_t accept_ctx = {
        .server = server,
        .tls_config = tls_config,
        .pool = pool,
    };
    if (nc_poller_add(server->accept_loop->poller, server->listen_fd,
                      NC_EV_READ, &accept_ctx) != 0)
        goto done;
    nc_atomic_store(&server->running, 1);
    if (nc_atomic_load(&server->stop_requested))
        neverc_http_server_shutdown(server);
    if (nc_atomic_load(&server->running))
        (void)nc_evloop_run(server->accept_loop,
                            https_accept_event_handler);
    result = 0;

done:
    nc_atomic_store(&server->running, 0);
    if (server->accept_loop) nc_evloop_destroy(server->accept_loop);
    server->accept_loop = NULL;
    if (server->listen_fd != NC_INVALID_SOCK)
        nc_sock_close(server->listen_fd);
    server->listen_fd = NC_INVALID_SOCK;
    if (pool) nc_threadpool_destroy(pool);
    if (h2_server) neverc_h2_server_destroy(h2_server);
    server->h2_server = NULL;
    neverc_tls_config_free(tls_config);
    nc_atomic_store(&server->serving, 0);
    return result;
}

int neverc_http_listen_and_serve_tls(const char *addr, neverc_http_mux_t *mux,
                                      const char *cert_file,
                                      const char *key_file) {
    neverc_http_server_config_t config = legacy_server_config();
    neverc_http_server_t *server = neverc_http_server_new(mux, &config);
    if (!server) return -1;
    if (!nc_atomic_ptr_cas(&g_server_ptr, NULL, server)) {
        neverc_http_server_free(server);
        return -1;
    }
    int result = neverc_http_server_listen_and_serve_tls(
        server, addr, cert_file, key_file);
    g_server_port = neverc_http_server_bound_port(server);
    (void)nc_atomic_ptr_cas(&g_server_ptr, server, NULL);
    neverc_http_server_free(server);
    return result;
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

int nc_http_hijack_tls(neverc_http_response_writer_t *writer,
                       neverc_tls_conn_t **tls,
                       neverc_tcp_conn_t **tcp) {
    if (tls) *tls = NULL;
    if (tcp) *tcp = NULL;
    if (!writer || !tls || !tcp || writer->hijacked || writer->owner ||
        writer->protocol_flush || !writer->transport_context ||
        !writer->transport_tcp)
        return -1;
    writer->hijacked = 1;
    *tls = (neverc_tls_conn_t *)writer->transport_context;
    *tcp = writer->transport_tcp;
    writer->transport_write = NULL;
    writer->transport_context = NULL;
    writer->transport_tcp = NULL;
    return 0;
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
    for (int i = 0; i < w->ntrailers; i++) {
        free(w->trailer_names[i]);
        free(w->trailer_values[i]);
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
            w->has_content_length_override = 1;
            w->content_length_override = (size_t)fsize;

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
