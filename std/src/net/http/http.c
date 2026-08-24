#include "neverc/std/net/http.h"
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/net/url.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/compress/gzip.h"
#include "../_net_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static int https_transport_write(void *context, const void *data,
                                 size_t len, int timeout_ms);
typedef void (*nc_http_h2_connection_done_func_t)(void *context);
int nc_h2_server_start_embedded(neverc_h2_server_t *server);
int nc_h2_server_submit_tls(
    neverc_h2_server_t *server, neverc_tls_conn_t *tls,
    neverc_tcp_conn_t *tcp, nc_http_h2_connection_done_func_t done,
    void *done_context);
int nc_http_sock_write_all_timeout(nc_sock_t fd, const void *data, size_t len,
                                   int timeout_ms);

/* ======================================================================
 * Response Writer — heap-allocated, one per request
 * ====================================================================== */

#define HTTP_MAX_HEADERS    64
#define HTTP_INITIAL_BUFSZ  4096
#define HTTP_MAX_PENDING_OUTPUT (16U * 1024U * 1024U)

struct neverc_http_response_writer {
    nc_sock_t   fd;
    int         status;
    int         headers_sent;
    int         aborted;
    int         chunked;
    int         chunked_ended;
    char       *header_names[HTTP_MAX_HEADERS];
    char       *header_values[HTTP_MAX_HEADERS];
    int         nheaders;
    char       *trailer_names[HTTP_MAX_HEADERS];
    char       *trailer_values[HTTP_MAX_HEADERS];
    int         ntrailers;
    nc_buf_t    body;
    int         keep_alive;
    int         initial_keep_alive;
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
    w->initial_keep_alive = keep_alive;
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
    if (fd == NC_INVALID_SOCK || (!data && len > 0) || timeout_ms < 0)
        return -1;
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
        w->has_content_length_override ||
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

/* Go net/http chunkWriter.writeHeader: emit Date only if the handler
 * did not set one. Cached and double-buffered so concurrent flushes
 * never observe a torn timestamp. */
static int rw_append_cached_date(nc_buf_t *hdr) {
    static char   date_bufs[2][64];
    static int    date_lens[2] = {0, 0};
    static volatile int date_idx = 0;
    static volatile time_t date_time = 0;

    time_t now = time(NULL);
    if (now != date_time) {
        int wi = 1 - date_idx;
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
    if (date_lens[ri] <= 0)
        return 0;
    return nc_buf_append(hdr, date_bufs[ri], (size_t)date_lens[ri]);
}

static int rw_flush(neverc_http_response_writer_t *w) {
    if (!w || w->hijacked || w->aborted) return -1;
    if (w->headers_sent) return 0;
    if (w->protocol_flush) {
        rw_apply_gzip(w);
        if (w->protocol_flush(w->protocol_context, w, 1) == 0) {
            w->headers_sent = 1;
            return 0;
        }
        w->keep_alive = 0;
        w->aborted = 1;
        return -1;
    }
    rw_apply_gzip(w);
    int status_forbids_body = w->status < 200 || w->status == 204 ||
                              w->status == 304;
    int emit_content_length =
        w->status >= 200 && w->status != 204 &&
        (w->has_content_length_override || w->status != 304);
    /* RFC 9110 §8.6 / Go chunkWriter: HEAD of a chunked GET must not
     * advertise Content-Length or Transfer-Encoding. The first flush
     * would otherwise publish only the first chunk's size. */
    if (w->head_request && w->chunked)
        emit_content_length = 0;

    nc_buf_t hdr;
    nc_buf_init(&hdr);

    if (w->has_content_length_override &&
        !w->head_request && !status_forbids_body) {
        if (w->body.len > w->content_length_override)
            goto fail;
        /* A non-empty short body with an advertised Content-Length would
         * desynchronize a keep-alive peer. Empty body + override is left
         * intact for sendfile, which writes the file after headers. */
        if (w->body.len > 0 &&
            w->body.len < w->content_length_override)
            goto fail;
    }

    size_t sl_len = 0;
    const char *sl = fast_status_line(w->status, &sl_len);
    if (sl) {
        if (nc_buf_append(&hdr, sl, sl_len) != 0) goto fail;
    } else {
        char line[256];
        int n = snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n",
                         w->status, neverc_http_status_text(w->status));
        if (n < 0 || (size_t)n >= sizeof(line) ||
            nc_buf_append(&hdr, line, (size_t)n) != 0)
            goto fail;
    }

    int has_content_type = 0;
    int has_date = 0;
    char line[256];
    int n;

    for (int i = 0; i < w->nheaders; i++) {
        if (strcasecmp(w->header_names[i], "Content-Length") == 0 ||
            strcasecmp(w->header_names[i], "Transfer-Encoding") == 0 ||
            strcasecmp(w->header_names[i], "Connection") == 0)
            continue;
        if (nc_buf_append(&hdr, w->header_names[i],
                          strlen(w->header_names[i])) != 0 ||
            nc_buf_append(&hdr, ": ", 2) != 0 ||
            nc_buf_append(&hdr, w->header_values[i],
                          strlen(w->header_values[i])) != 0 ||
            nc_buf_append(&hdr, "\r\n", 2) != 0)
            goto fail;

        if (strcasecmp(w->header_names[i], "Content-Type") == 0)
            has_content_type = 1;
        if (strcasecmp(w->header_names[i], "Date") == 0)
            has_date = 1;
    }

    if (!has_content_type) {
        const char *ct = "Content-Type: text/plain; charset=utf-8\r\n";
        if (nc_buf_append(&hdr, ct, strlen(ct)) != 0) goto fail;
    }
    if (emit_content_length) {
        size_t content_length = w->has_content_length_override
            ? w->content_length_override : w->body.len;
        n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n",
                     content_length);
        if (n < 0 || (size_t)n >= sizeof(line) ||
            nc_buf_append(&hdr, line, (size_t)n) != 0)
            goto fail;
    }
    const char *conn_val = w->keep_alive
        ? "Connection: keep-alive\r\n"
        : "Connection: close\r\n";
    if (nc_buf_append(&hdr, conn_val, strlen(conn_val)) != 0) goto fail;

    if (!has_date && rw_append_cached_date(&hdr) != 0)
        goto fail;

    if (nc_buf_append(&hdr, "\r\n", 2) != 0) goto fail;

    if (w->fd != NC_INVALID_SOCK || w->transport_write) {
        if (rw_write_all(w, hdr.data, hdr.len) != 0) goto fail;
        w->headers_sent = 1;
        if (!w->head_request && !status_forbids_body && w->body.len > 0 &&
            rw_write_all(w, w->body.data, w->body.len) != 0)
            goto fail;
    } else {
        w->headers_sent = 1;
    }

    nc_buf_free(&hdr);
    return 0;

fail:
    w->keep_alive = 0;
    w->aborted = 1;
    nc_buf_free(&hdr);
    return -1;
}

void nc_http_writer_set_protocol(neverc_http_response_writer_t *writer,
                                 void *context,
                                 http_protocol_flush_func_t flush) {
    if (!writer) return;
    writer->protocol_context = context;
    writer->protocol_flush = flush;
}

int nc_http_writer_finish(neverc_http_response_writer_t *writer) {
    if (!writer || writer->hijacked || writer->aborted) return -1;
    if (!writer->headers_sent) return rw_flush(writer);
    return 0;
}

void neverc_http_set_status(neverc_http_response_writer_t *w, int code) {
    if (!w) return;
    /* Invalid codes must not remain 200 OK: neverc_http_error("denied", 0)
     * used to fail-open as success. Match Go WriteHeader's 100..999 range,
     * but substitute 500 instead of panicking. */
    w->status = (code >= 100 && code <= 999) ? code : 500;
}

int neverc_http_add_header(neverc_http_response_writer_t *w,
                            const char *name, const char *value) {
    if (!w || !name || !value || !http_valid_token(name, strlen(name)) ||
        !http_valid_field_value(value, strlen(value)) ||
        strcasecmp(name, "Connection") == 0 ||
        strcasecmp(name, "Content-Length") == 0 ||
        strcasecmp(name, "Transfer-Encoding") == 0 ||
        w->headers_sent || w->aborted || w->hijacked ||
        w->nheaders >= HTTP_MAX_HEADERS)
        return -1;

    char *name_copy = strdup(name);
    char *value_copy = strdup(value);
    if (!name_copy || !value_copy) {
        free(name_copy);
        free(value_copy);
        return -1;
    }
    w->header_names[w->nheaders] = name_copy;
    w->header_values[w->nheaders] = value_copy;
    w->nheaders++;
    return 0;
}

int nc_http_writer_add_header(neverc_http_response_writer_t *w,
                              const char *name, const char *value) {
    return neverc_http_add_header(w, name, value);
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
    (void)nc_http_writer_add_header(w, name, value);
}

int neverc_http_set_content_length(neverc_http_response_writer_t *w,
                                    size_t content_length) {
    if (!w || w->headers_sent || w->aborted || w->hijacked ||
        w->chunked)
        return -1;
    w->has_content_length_override = 1;
    w->content_length_override = content_length;
    return 0;
}

int neverc_http_reset_response(neverc_http_response_writer_t *w) {
    if (!w || w->headers_sent || w->aborted || w->hijacked)
        return -1;
    for (int i = 0; i < w->nheaders; i++) {
        free(w->header_names[i]);
        free(w->header_values[i]);
        w->header_names[i] = NULL;
        w->header_values[i] = NULL;
    }
    for (int i = 0; i < w->ntrailers; i++) {
        free(w->trailer_names[i]);
        free(w->trailer_values[i]);
        w->trailer_names[i] = NULL;
        w->trailer_values[i] = NULL;
    }
    w->nheaders = 0;
    w->ntrailers = 0;
    nc_buf_reset(&w->body);
    w->status = 200;
    w->chunked = 0;
    w->chunked_ended = 0;
    w->has_content_length_override = 0;
    w->content_length_override = 0;
    w->keep_alive = w->initial_keep_alive;
    return 0;
}

void neverc_http_set_trailer(neverc_http_response_writer_t *w,
                              const char *name, const char *value) {
    if (!w || !name || !value || name[0] == ':' ||
        !http_valid_token(name, strlen(name)) ||
        !http_valid_field_value(value, strlen(value)) ||
        strcasecmp(name, "Content-Length") == 0 ||
        strcasecmp(name, "Transfer-Encoding") == 0 ||
        strcasecmp(name, "Trailer") == 0 ||
        strcasecmp(name, "Host") == 0 ||
        strcasecmp(name, "Connection") == 0 ||
        strcasecmp(name, "TE") == 0)
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
    if (!w) return 0;
    if ((!data && len > 0) || len > INT_MAX) return -1;
    if (w->body_limit_exceeded || w->chunked_ended) return -1;
    if (len == 0) return 0;
    if (nc_buf_append(&w->body, data, len) != 0) return -1;
    if (w->chunked && neverc_http_flush_chunk(w) != 0) return -1;
    return (int)len;
}

int neverc_http_write_string(neverc_http_response_writer_t *w,
                              const char *s) {
    if (!w || !s) return 0;
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
    if (n < 0)
        return -1;
    if (n > 0) {
        if ((size_t)n >= sizeof(buf)) {
            char *big = (char *)malloc((size_t)n + 1);
            if (!big)
                return -1;
            va_start(ap, fmt);
            int formatted = vsnprintf(big, (size_t)n + 1, fmt, ap);
            va_end(ap);
            if (formatted < 0 || formatted > n) {
                free(big);
                return -1;
            }
            int ret = neverc_http_write(w, big, (size_t)formatted);
            free(big);
            return ret;
        }
        return neverc_http_write(w, buf, (size_t)n);
    }
    return 0;
}

void neverc_http_enable_chunked(neverc_http_response_writer_t *w) {
    if (!w || w->headers_sent) return;
    w->chunked = 1;
    /* Chunked framing is mutually exclusive with Content-Length. HTTP/1
     * already omits CL on the chunked path; drop the override so HTTP/2
     * cannot advertise a length that later DATA will not match. */
    w->has_content_length_override = 0;
    w->content_length_override = 0;
}

static int rw_send_chunked_headers(neverc_http_response_writer_t *w) {
    if (!w || w->aborted) return -1;
    if (w->headers_sent) return 0;

    nc_buf_t hdr;
    nc_buf_init(&hdr);

    char line[256];
    int n = snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n",
                     w->status, neverc_http_status_text(w->status));
    if (n < 0 || (size_t)n >= sizeof(line) ||
        nc_buf_append(&hdr, line, (size_t)n) != 0)
        goto fail;

    int has_content_type = 0;
    int has_date = 0;
    for (int i = 0; i < w->nheaders; i++) {
        if (strcasecmp(w->header_names[i], "Content-Length") == 0 ||
            strcasecmp(w->header_names[i], "Transfer-Encoding") == 0 ||
            strcasecmp(w->header_names[i], "Connection") == 0)
            continue;
        if (nc_buf_append(&hdr, w->header_names[i],
                          strlen(w->header_names[i])) != 0 ||
            nc_buf_append(&hdr, ": ", 2) != 0 ||
            nc_buf_append(&hdr, w->header_values[i],
                          strlen(w->header_values[i])) != 0 ||
            nc_buf_append(&hdr, "\r\n", 2) != 0)
            goto fail;

        if (strcasecmp(w->header_names[i], "Content-Type") == 0)
            has_content_type = 1;
        if (strcasecmp(w->header_names[i], "Date") == 0)
            has_date = 1;
    }

    if (!has_content_type) {
        const char *ct = "Content-Type: text/plain; charset=utf-8\r\n";
        if (nc_buf_append(&hdr, ct, strlen(ct)) != 0) goto fail;
    }
    const char *te = "Transfer-Encoding: chunked\r\n";
    if (nc_buf_append(&hdr, te, strlen(te)) != 0) goto fail;
    if (w->ntrailers > 0) {
        if (nc_buf_append(&hdr, "Trailer: ", 9) != 0) goto fail;
        for (int i = 0; i < w->ntrailers; i++) {
            if ((i > 0 && nc_buf_append(&hdr, ", ", 2) != 0) ||
                nc_buf_append(&hdr, w->trailer_names[i],
                              strlen(w->trailer_names[i])) != 0)
                goto fail;
        }
        if (nc_buf_append(&hdr, "\r\n", 2) != 0) goto fail;
    }
    const char *conn_val = w->keep_alive
        ? "Connection: keep-alive\r\n"
        : "Connection: close\r\n";
    if (nc_buf_append(&hdr, conn_val, strlen(conn_val)) != 0) goto fail;
    if (!has_date && rw_append_cached_date(&hdr) != 0) goto fail;

    if (nc_buf_append(&hdr, "\r\n", 2) != 0 ||
        rw_write_all(w, hdr.data, hdr.len) != 0)
        goto fail;
    w->headers_sent = 1;
    nc_buf_free(&hdr);
    return 0;

fail:
    w->keep_alive = 0;
    w->aborted = 1;
    nc_buf_free(&hdr);
    return -1;
}

int neverc_http_flush_chunk(neverc_http_response_writer_t *w) {
    if (!w || !w->chunked || w->chunked_ended) return -1;
    if (w->protocol_flush)
        return w->protocol_flush(w->protocol_context, w, 0);
    if (w->head_request || w->status < 200 ||
        w->status == 204 || w->status == 304) {
        if (rw_flush(w) != 0) return -1;
        nc_buf_reset(&w->body);
        return 0;
    }

    if (!w->headers_sent && rw_send_chunked_headers(w) != 0) return -1;

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
    if (w->chunked_ended) return 0;
    if (w->protocol_flush) {
        int result = w->protocol_flush(w->protocol_context, w, 1);
        if (result == 0) w->chunked_ended = 1;
        return result;
    }
    if (w->head_request || w->status < 200 ||
        w->status == 204 || w->status == 304) {
        if (rw_flush(w) != 0) return -1;
        nc_buf_reset(&w->body);
        w->chunked_ended = 1;
        return 0;
    }

    if (w->body.len > 0 && neverc_http_flush_chunk(w) != 0)
        return -1;

    if (!w->headers_sent && rw_send_chunked_headers(w) != 0) return -1;

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
    if (result == 0) w->chunked_ended = 1;
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
 *   GET patterns also serve HEAD (Go 1.22 ServeMux) unless a HEAD
 *   route exists for the same path.
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
    void (*destroy_context)(void *context);
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

static int route_parse_pattern(route_t *r, const char *pattern) {
    char *pattern_copy = strdup(pattern);
    char *method = NULL;
    char *path_pattern = NULL;
    if (!pattern_copy) return -1;

    /* Check for "METHOD /path" syntax. */
    const char *space = strchr(pattern, ' ');
    if (space && space > pattern) {
        method = strndup_safe(pattern, (size_t)(space - pattern));
        path_pattern = strdup(space + 1);
        if (!method || !path_pattern) {
            free(path_pattern);
            free(method);
            free(pattern_copy);
            return -1;
        }
    } else {
        path_pattern = strdup(pattern);
        if (!path_pattern) {
            free(pattern_copy);
            return -1;
        }
    }

    /* Go-style: {$} and {name...} may appear only at the end. */
    {
        const char *cursor = path_pattern;
        while ((cursor = strchr(cursor, '{')) != NULL) {
            const char *close = strchr(cursor, '}');
            if (!close) break;
            /* Go: empty `{}` / `{...}` are not valid wildcards. */
            if ((size_t)(close - cursor) == 1 ||
                ((size_t)(close - cursor) == 4 && cursor[1] == '.' &&
                 cursor[2] == '.' && cursor[3] == '.')) {
                free(path_pattern);
                free(method);
                free(pattern_copy);
                return -1;
            }
            if ((size_t)(close - cursor) == 2 && cursor[1] == '$' &&
                close[1] != '\0') {
                free(path_pattern);
                free(method);
                free(pattern_copy);
                return -1;
            }
            if ((size_t)(close - cursor) >= 4 && close[-3] == '.' &&
                close[-2] == '.' && close[-1] == '.' && close[1] != '\0') {
                free(path_pattern);
                free(method);
                free(pattern_copy);
                return -1;
            }
            cursor = close + 1;
        }
    }

    r->pattern = pattern_copy;
    r->pattern_len = strlen(pattern);
    r->method = method;
    r->path_pattern = path_pattern;
    r->has_params = strchr(path_pattern, '{') != NULL;
    return 0;
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
        route_t *route = &mux->routes[mux->nroutes];
        if (route_parse_pattern(route, pattern) == 0) {
            route->handler = handler;
            mux->nroutes++;
        }
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
        if (route_parse_pattern(route, pattern) == 0) {
            route->context_handler = handler;
            route->handler_context = context;
            mux->nroutes++;
            result = 0;
        }
    }
    nc_mutex_unlock(&mux->lock);
    return result;
}

int nc_http_mux_handle_owned_context(
    neverc_http_mux_t *mux, const char *pattern,
    neverc_http_handler_context_func_t handler, void *context,
    void (*destroy_context)(void *context)) {
    if (!mux || !pattern || !handler || !destroy_context) return -1;
    int result = -1;
    nc_mutex_lock(&mux->lock);
    if (mux->nroutes < MAX_ROUTES) {
        route_t *route = &mux->routes[mux->nroutes];
        if (route_parse_pattern(route, pattern) == 0) {
            route->context_handler = handler;
            route->handler_context = context;
            route->destroy_context = destroy_context;
            mux->nroutes++;
            result = 0;
        }
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
        if (route_parse_pattern(route, pattern) == 0) {
            route->context_handler = handler;
            route->handler_context = context;
            route->streaming = 1;
            mux->nroutes++;
            result = 0;
        }
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
        if (mux->routes[i].destroy_context)
            mux->routes[i].destroy_context(mux->routes[i].handler_context);
    }
    nc_mutex_destroy(&mux->lock);
    free(mux);
}

void neverc_http_handle_func(const char *pattern,
                              neverc_http_handler_func_t handler) {
    ensure_default_mux();
    neverc_http_mux_handle(&default_mux, pattern, handler);
}

int nc_http_default_handle_owned_context(
    const char *pattern, neverc_http_handler_context_func_t handler,
    void *context, void (*destroy_context)(void *context)) {
    ensure_default_mux();
    return nc_http_mux_handle_owned_context(
        &default_mux, pattern, handler, context, destroy_context);
}

/* Go pathUnescape: on error keep the original bytes. `+` stays `+`. */
#define HTTP_MUX_SEG 2048

static int http_mux_unescape_span(const char *s, size_t n,
                                  char *out, size_t cap, size_t *outn) {
    char tmp[HTTP_MUX_SEG];
    int decoded;

    if (!s || !out || cap == 0)
        return -1;
    if (n >= HTTP_MUX_SEG) {
        if (n >= cap) return -1;
        memcpy(out, s, n);
        out[n] = '\0';
        if (outn) *outn = n;
        return 0;
    }
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    decoded = neverc_url_path_unescape(tmp, out, cap);
    if (decoded < 0) {
        if (n >= cap) return -1;
        memcpy(out, s, n);
        out[n] = '\0';
        if (outn) *outn = n;
        return 0;
    }
    if (outn) *outn = (size_t)decoded;
    return 0;
}

static int http_mux_store_param(path_params_t *params,
                                const char *name, size_t namelen,
                                const char *val, size_t vallen) {
    if (!params) return 1;
    if (params->len + (int)namelen + 1 + (int)vallen + 1
        >= (int)sizeof(params->buf))
        return 0;
    memcpy(params->buf + params->len, name, namelen);
    params->len += (int)namelen;
    params->buf[params->len++] = '\0';
    if (vallen)
        memcpy(params->buf + params->len, val, vallen);
    params->len += (int)vallen;
    params->buf[params->len++] = '\0';
    params->count++;
    return 1;
}

/* Walk both paths segment-by-segment after PathUnescape. A trailing slash
 * is its own segment so `/foo` ≠ `/foo/`. prefix: pattern ends with `/` and
 * the request must still have a `/` after the shared segments. */
static int http_mux_unescaped_compare(const char *pat, const char *path,
                                      int prefix) {
    char pseg[HTTP_MUX_SEG], rseg[HTTP_MUX_SEG];
    size_t pn, rn;

    if (!pat || !path) return 0;
    while (*pat || *path) {
        const char *pe;
        const char *re;
        int pat_slash;
        int path_slash;

        if (*pat == '/') pat++;
        else if (*pat) return 0;
        if (*path == '/') path++;
        else if (*path) return 0;

        pat_slash = (*pat == '\0' && pat[-1] == '/');
        path_slash = (*path == '\0' && path[-1] == '/');
        pe = pat;
        while (*pe && *pe != '/') pe++;
        re = path;
        while (*re && *re != '/') re++;

        if (prefix && pat_slash) {
            /* Trailing `/` on the pattern: path must still have a `/`
             * after the last compared segment (already consumed). */
            return path_slash || *path != '\0' || re > path;
        }

        if (http_mux_unescape_span(pat, (size_t)(pe - pat),
                                   pseg, sizeof(pseg), &pn) != 0)
            return 0;
        if (http_mux_unescape_span(path, (size_t)(re - path),
                                   rseg, sizeof(rseg), &rn) != 0)
            return 0;
        if (pat_slash != path_slash || pn != rn ||
            memcmp(pseg, rseg, pn) != 0)
            return 0;
        pat = pe;
        path = re;
    }
    return 1;
}

/* Match a pattern with path parameters.
 * Returns 1 on match, 0 on no match. Fills params if non-NULL.
 * Go 1.22: each segment is PathUnescape'd before compare/capture;
 * `%2F` does not split a segment. */
static int pattern_match(const char *pattern, const char *path,
                          path_params_t *params) {
    char captured[HTTP_MUX_SEG];
    size_t captured_len = 0;

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

            /* Go {$}: matches only the end of the URL. A leftover
             * segment used to be captured as a parameter named `$`.
             * `{$...}` is a multi wildcard, not this sentinel. */
            if (!wildcard && namelen == 1 && name[0] == '$')
                return close[1] == '\0' && *rp == '\0';

            if (wildcard) {
                if (close[1] != '\0') return 0;
                /* Go {name...}: remainder after the joining '/', including
                 * empty (/files and /files/ → ""). PathUnescape the rest
                 * as a whole (`a%2Fb` → `a/b`). */
                const char *rest = rp;
                if (rest[0] == '/') rest++;
                if (http_mux_unescape_span(rest, strlen(rest), captured,
                                           sizeof(captured),
                                           &captured_len) != 0)
                    return 0;
                return http_mux_store_param(params, name, namelen,
                                            captured, captured_len);
            }

            /* Find end of this path segment; do not split on `%2F`. */
            const char *seg_end = rp;
            while (*seg_end && *seg_end != '/') seg_end++;
            size_t vallen = (size_t)(seg_end - rp);

            if (vallen == 0) return 0;
            if (http_mux_unescape_span(rp, vallen, captured,
                                       sizeof(captured),
                                       &captured_len) != 0)
                return 0;
            if (!http_mux_store_param(params, name, namelen,
                                      captured, captured_len))
                return 0;

            rp = seg_end;
            pp = close + 1;
            /* Go 1.22: a trailing `/` after `{name}` is an anonymous
             * `{...}` subtree. `/items/42` does not match here so the
             * mux can slash-redirect to `/items/42/`. */
            if (*pp == '/' && pp[1] == '\0')
                return *rp == '/';
        } else if (*pp == '/' && *rp == '/') {
            const char *pe;
            const char *re;
            char pseg[HTTP_MUX_SEG], rseg[HTTP_MUX_SEG];
            size_t pn, rn;

            pp++;
            rp++;
            if (*pp == '{')
                continue;
            pe = pp;
            while (*pe && *pe != '/' && *pe != '{') pe++;
            re = rp;
            while (*re && *re != '/') re++;
            if (http_mux_unescape_span(pp, (size_t)(pe - pp),
                                       pseg, sizeof(pseg), &pn) != 0)
                return 0;
            if (http_mux_unescape_span(rp, (size_t)(re - rp),
                                       rseg, sizeof(rseg), &rn) != 0)
                return 0;
            if (pn != rn || memcmp(pseg, rseg, pn) != 0)
                return 0;
            pp = pe;
            rp = re;
        } else {
            if (*pp != *rp) return 0;
            pp++;
            rp++;
        }
    }

    /* Both consumed = exact match. Pattern ends with / = prefix match.
     * Go: `{name...}` also matches when the request path ends at the
     * wildcard (`/files` against `/files/{path...}`). Do not eat the
     * slash before `{$}`: `/posts` is not `/posts/`. */
    if (*pp == '/' && pp[1] == '{') {
        const char *close = strchr(pp + 1, '}');
        if (close && close[1] == '\0') {
            const char *name = pp + 2;
            size_t namelen = (size_t)(close - name);
            if (namelen >= 3 && name[namelen - 3] == '.' &&
                name[namelen - 2] == '.' && name[namelen - 1] == '.')
                pp++;
        }
    }
    if (*pp == '{') {
        const char *close = strchr(pp, '}');
        if (close && close[1] == '\0') {
            const char *name = pp + 1;
            size_t namelen = (size_t)(close - name);
            if (namelen == 1 && name[0] == '$')
                return 1;
            if (namelen >= 3 && name[namelen - 3] == '.' &&
                name[namelen - 2] == '.' && name[namelen - 1] == '.') {
                namelen -= 3;
                return http_mux_store_param(params, name, namelen, "", 0);
            }
        }
    }
    if (*pp == '\0' && *rp == '\0') return 1;
    if (*pp == '\0' && pp > pattern && pp[-1] == '/') return 1;
    return 0;
}

/* Path rank outranks method. Go ServeMux: a more specific path wins
 * even when the broader pattern has a method (`GET /` vs `/users/{id}`).
 * Method is only a tie-break on the same path. */
static int mux_pattern_literal_len(const char *pat) {
    int n = 0;
    for (; *pat; ) {
        if (*pat == '{') {
            const char *close = strchr(pat, '}');
            if (!close) break;
            pat = close + 1;
        } else {
            n++;
            pat++;
        }
    }
    return n;
}

static int mux_path_rank_exact(size_t plen) {
    if (plen > 99999U) plen = 99999U;
    return 1000000 + (int)plen;
}

static int mux_path_rank_param(const char *pat) {
    int lit = mux_pattern_literal_len(pat);
    if (lit > 99999) lit = 99999;
    return 100000 + lit;
}

static int mux_path_rank_prefix(size_t plen) {
    if (plen > 99999U) plen = 99999U;
    return (int)plen;
}

/* Go {$} is an exact end-of-path match, more specific than a trailing-slash
 * prefix of the same literal length (`/posts/{$}` vs `/posts/`). */
static int mux_pattern_ends_dollar(const char *pat) {
    size_t n;
    if (!pat) return 0;
    n = strlen(pat);
    return n >= 3 && pat[n - 3] == '{' && pat[n - 2] == '$' &&
           pat[n - 1] == '}';
}

/* Go ServeMux exactMatch: the last segment is "multi" only for a trailing
 * slash (anonymous `{...}`) or `{name...}`. `{$}` is a literal empty
 * segment, not multi. An exact current match must not slash-redirect. */
static int mux_pattern_last_is_multi(const char *pat) {
    const char *close;
    if (!pat || !pat[0]) return 0;
    close = pat + strlen(pat);
    if (close[-1] == '/')
        return 1;
    if (close[-1] != '}')
        return 0;
    return close - pat >= 5 && close[-4] == '.' && close[-3] == '.' &&
           close[-2] == '.';
}

static int mux_better(int path_rank, int method_rank, size_t plen,
                      int best_path, int best_method, size_t best_len) {
    if (path_rank != best_path) return path_rank > best_path;
    if (method_rank != best_method) return method_rank > best_method;
    return plen > best_len;
}

static int mux_route_path_rank(const route_t *r) {
    const char *pat = r->path_pattern;
    size_t plen = strlen(pat);
    if (r->has_params) {
        if (mux_pattern_ends_dollar(pat))
            return mux_path_rank_exact((size_t)mux_pattern_literal_len(pat));
        return mux_path_rank_param(pat);
    }
    if (plen > 0 && pat[plen - 1] == '/')
        return mux_path_rank_prefix(plen);
    return mux_path_rank_exact(plen);
}

static int mux_with_trailing_slash(const char *path, char *out, size_t cap) {
    size_t n;
    if (!path || !out || cap < 3) return -1;
    n = strlen(path);
    if (n == 0 || path[n - 1] == '/' || n + 2 > cap) return -1;
    memcpy(out, path, n);
    out[n] = '/';
    out[n + 1] = '\0';
    return 0;
}

static route_t *mux_match_ex(neverc_http_mux_t *mux,
                             const char *method, const char *path,
                             path_params_t *params) {
    route_t *best = NULL;
    size_t best_len = 0;
    int best_path_rank = -1;
    int best_method_rank = -1;

    int nr = mux->nroutes;
#if defined(__GNUC__) || defined(__clang__)
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
#else
    __sync_synchronize();
#endif

    path_params_t tmp_params;

    for (int i = 0; i < nr; i++) {
        route_t *r = &mux->routes[i];

        /* Check method filter. Go 1.22 ServeMux: a GET pattern also
         * matches HEAD when no dedicated HEAD route wins. */
        int get_serves_head = 0;
        if (r->method && method && strcmp(r->method, method) != 0) {
            if (strcmp(method, "HEAD") != 0 || strcmp(r->method, "GET") != 0)
                continue;
            get_serves_head = 1;
        }

        const char *pat = r->path_pattern;
        int method_rank = r->method ? (get_serves_head ? 1 : 2) : 0;

        if (r->has_params) {
            memset(&tmp_params, 0, sizeof(tmp_params));
            if (pattern_match(pat, path, &tmp_params)) {
                size_t plen = strlen(pat);
                int path_rank = mux_pattern_ends_dollar(pat)
                    ? mux_path_rank_exact(
                          (size_t)mux_pattern_literal_len(pat))
                    : mux_path_rank_param(pat);
                if (mux_better(path_rank, method_rank, plen,
                               best_path_rank, best_method_rank, best_len)) {
                    best = r;
                    best_len = plen;
                    best_path_rank = path_rank;
                    best_method_rank = method_rank;
                    if (params) *params = tmp_params;
                }
            }
            continue;
        }

        size_t plen = strlen(pat);

        /* Exact match. Go 1.22 compares PathUnescape'd segments
         * (`/a` equals `/%61`). Host-prefixed patterns keep strcmp. */
        if (strcmp(pat, path) == 0 ||
            (pat[0] == '/' && path[0] == '/' &&
             http_mux_unescaped_compare(pat, path, 0))) {
            int path_rank = mux_path_rank_exact(plen);
            if (mux_better(path_rank, method_rank, plen,
                           best_path_rank, best_method_rank, best_len)) {
                best = r;
                best_len = plen;
                best_path_rank = path_rank;
                best_method_rank = method_rank;
                if (params) { params->len = 0; params->count = 0; }
            }
            continue;
        }

        /* Prefix match (pattern ends with /). Unescape each segment
         * so `/api/` still matches `/ap%69/users`. */
        if (plen > 0 && pat[plen - 1] == '/' &&
            (strncmp(path, pat, plen) == 0 ||
             (pat[0] == '/' && path[0] == '/' &&
              http_mux_unescaped_compare(pat, path, 1)))) {
            int path_rank = mux_path_rank_prefix(plen);
            if (mux_better(path_rank, method_rank, plen,
                           best_path_rank, best_method_rank, best_len)) {
                best = r;
                best_len = plen;
                best_path_rank = path_rank;
                best_method_rank = method_rank;
                if (params) { params->len = 0; params->count = 0; }
            }
        }
    }
    return best;
}

static int mux_route_path_matches(const route_t *r, const char *path) {
    const char *pat;
    size_t plen;
    path_params_t tmp;
    if (!r || !path) return 0;
    pat = r->path_pattern;
    if (r->has_params) {
        memset(&tmp, 0, sizeof(tmp));
        return pattern_match(pat, path, &tmp);
    }
    plen = strlen(pat);
    if (strcmp(pat, path) == 0 ||
        (pat[0] == '/' && path[0] == '/' &&
         http_mux_unescaped_compare(pat, path, 0)))
        return 1;
    return plen > 0 && pat[plen - 1] == '/' &&
           (strncmp(path, pat, plen) == 0 ||
            (pat[0] == '/' && path[0] == '/' &&
             http_mux_unescaped_compare(pat, path, 1)));
}

/* True when `path` is the directory root of a multi pattern: `/files`
 * for `/files/{path...}`, or `/api` for `/api/`. `/files/a/b/c` and
 * `/api/foo` are not roots and must not 301 to add a slash. */
static int mux_path_is_multi_root(const char *pat, const char *path) {
    char slashed[4096];
    const char *brace;
    size_t prefix_len, n;
    if (!pat || !path || !mux_pattern_last_is_multi(pat))
        return 0;
    if (mux_with_trailing_slash(path, slashed, sizeof(slashed)) != 0)
        return 0;
    n = strlen(pat);
    if (n > 0 && pat[n - 1] == '/')
        return strcmp(slashed, pat) == 0;
    brace = strrchr(pat, '{');
    if (!brace)
        return 0;
    prefix_len = (size_t)(brace - pat);
    return strlen(slashed) == prefix_len &&
           memcmp(slashed, pat, prefix_len) == 0;
}

/* Go net/http cleanPath: path.Clean plus a trailing slash when the
 * original had one (except `/`). HTTP paths stay slash-separated. */
static int mux_clean_path(const char *path, char *out, size_t cap) {
    size_t w = 0;
    int had_trailing;
    const char *s;

    if (!out || cap < 2)
        return -1;
    if (!path || path[0] == '\0') {
        memcpy(out, "/", 2);
        return 0;
    }
    had_trailing = path[strlen(path) - 1] == '/';
    s = path;
    out[w++] = '/';
    if (*s == '/')
        s++;
    while (*s) {
        const char *start;
        size_t n;
        if (*s == '/') {
            s++;
            continue;
        }
        start = s;
        while (*s && *s != '/')
            s++;
        n = (size_t)(s - start);
        if (n == 1 && start[0] == '.')
            continue;
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            if (w > 1) {
                while (w > 1 && out[w - 1] != '/')
                    w--;
                if (w > 1)
                    w--;
            }
            continue;
        }
        if (w > 1) {
            if (w + 1 >= cap)
                return -1;
            out[w++] = '/';
        }
        if (w + n >= cap)
            return -1;
        memcpy(out + w, start, n);
        w += n;
    }
    if (had_trailing && w > 1) {
        if (w + 1 >= cap)
            return -1;
        out[w++] = '/';
    }
    out[w] = '\0';
    return 0;
}

static void mux_redirect_location(neverc_http_response_writer_t *writer,
                                  const char *loc, const char *query) {
    if (query && query[0]) {
        char locq[4096];
        if ((size_t)snprintf(locq, sizeof(locq), "%s?%s", loc, query) <
            sizeof(locq))
            neverc_http_redirect(writer, locq, 301);
        else
            neverc_http_redirect(writer, loc, 301);
    } else {
        neverc_http_redirect(writer, loc, 301);
    }
}

static const char *mux_effective_path(const char *method, const char *path,
                                      char *cleaned, size_t cap,
                                      int *cleaned_differs) {
    *cleaned_differs = 0;
    if (!path)
        path = "";
    if (method && strcmp(method, "CONNECT") == 0)
        return path;
    /* OPTIONS / rewrite "*" is a request-target, not a path. Go
     * ServeHTTP rejects RequestURI "*" before cleanPath; NeverC
     * serves a registered "*" route and must not 301 it to "/*". */
    if (path[0] == '*' && path[1] == '\0')
        return path;
    if (mux_clean_path(path, cleaned, cap) != 0)
        return path;
    if (strcmp(cleaned, path) != 0) {
        *cleaned_differs = 1;
        return cleaned;
    }
    return path;
}

static int mux_slash_redirect(neverc_http_mux_t *mux, const char *method,
                              const char *path, route_t *current,
                              char *loc, size_t loc_cap) {
    char slashed[4096];
    path_params_t ignored;
    route_t *slash;
    size_t plen;
    if (mux_with_trailing_slash(path, slashed, sizeof(slashed)) != 0)
        return 0;
    memset(&ignored, 0, sizeof(ignored));
    slash = mux_match_ex(mux, method, slashed, &ignored);
    if (!slash)
        return 0;
    /* Go matchOrRedirect: an exact current match (`/posts`, `{id}`)
     * wins over a more-specific trailing-slash sibling (`/posts/{$}`,
     * `{id}/`). Multi (`{name...}` or trailing `/`) is inexact at the
     * subtree root, so `/files` still 301s to `/files/`. */
    if (current && !mux_pattern_last_is_multi(current->path_pattern))
        return 0;
    /* Go {$} and `{name...}` are exact `/path/` matches, so `/path`
     * slash-redirects to `/path/`. */
    plen = strlen(slash->path_pattern);
    if (!(plen > 0 && slash->path_pattern[plen - 1] == '/') &&
        !mux_pattern_ends_dollar(slash->path_pattern) &&
        !mux_pattern_last_is_multi(slash->path_pattern))
        return 0;
    /* Same multi route matching both `/files` and `/files/` must still
     * redirect; rank equality would otherwise suppress it. Do not skip
     * the rank check for `/files/a/b/c` or `/static/file` — those are
     * not the subtree root. */
    if (current && current != slash &&
        mux_route_path_rank(slash) <= mux_route_path_rank(current))
        return 0;
    if (current && current == slash &&
        !mux_path_is_multi_root(current->path_pattern, path))
        return 0;
    if (strlen(slashed) + 1 > loc_cap) return 0;
    memcpy(loc, slashed, strlen(slashed) + 1);
    return 1;
}

static int mux_fill_allow(neverc_http_mux_t *mux, const char *path,
                          char *allow, size_t allow_cap) {
    char methods[16][16];
    char slashed[4096];
    int n = 0;
    int has_get = 0;
    int have_slash = mux_with_trailing_slash(path, slashed, sizeof(slashed)) == 0;
    int i, j;
    size_t pos;
    if (!mux || !path || !allow || allow_cap == 0) return 0;
    for (i = 0; i < mux->nroutes; i++) {
        route_t *r = &mux->routes[i];
        if (!mux_route_path_matches(r, path) &&
            !(have_slash && mux_route_path_matches(r, slashed)))
            continue;
        if (!r->method || !r->method[0]) continue;
        for (j = 0; j < n; j++)
            if (strcmp(methods[j], r->method) == 0)
                break;
        if (j < n) continue;
        if (n >= 16) continue;
        strncpy(methods[n], r->method, sizeof(methods[n]) - 1);
        methods[n][sizeof(methods[n]) - 1] = '\0';
        if (strcmp(r->method, "GET") == 0) has_get = 1;
        n++;
    }
    if (has_get) {
        for (j = 0; j < n; j++)
            if (strcmp(methods[j], "HEAD") == 0)
                break;
        if (j == n && n < 16) {
            memcpy(methods[n], "HEAD", 5);
            n++;
        }
    }
    if (n == 0) return 0;
    for (i = 0; i < n; i++) {
        int best = i;
        for (j = i + 1; j < n; j++)
            if (strcmp(methods[j], methods[best]) < 0)
                best = j;
        if (best != i) {
            char tmp[16];
            memcpy(tmp, methods[i], sizeof(tmp));
            memcpy(methods[i], methods[best], sizeof(tmp));
            memcpy(methods[best], tmp, sizeof(tmp));
        }
    }
    pos = 0;
    for (i = 0; i < n; i++) {
        size_t mlen = strlen(methods[i]);
        size_t need = mlen + (i ? 2U : 0U);
        if (pos + need + 1 > allow_cap) return 0;
        if (i) {
            allow[pos++] = ',';
            allow[pos++] = ' ';
        }
        memcpy(allow + pos, methods[i], mlen);
        pos += mlen;
    }
    allow[pos] = '\0';
    return 1;
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

static int http_mux_has_streaming_routes(neverc_http_mux_t *mux) {
    if (!mux) return 0;
    for (int i = 0; i < mux->nroutes; i++)
        if (mux->routes[i].streaming) return 1;
    return 0;
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

int neverc_http_request_body_read(neverc_http_request_t *request,
                                  void *output, size_t output_capacity) {
    if (!request || !request->body_stream || !request->body_stream_read ||
        !output || output_capacity == 0)
        return -1;
    return request->body_stream_read(
        request->body_stream, request->context,
        output, output_capacity);
}

void neverc_http_request_body_cancel(neverc_http_request_t *request,
                                     uint32_t error_code) {
    if (!request || !request->body_stream || !request->body_stream_cancel)
        return;
    request->body_stream_cancel(request->body_stream, error_code);
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
    char cleaned[4096];
    int cleaned_differs = 0;
    const char *path = mux_effective_path(
        request->method, request->path, cleaned, sizeof(cleaned),
        &cleaned_differs);
    memset(&params, 0, sizeof(params));
    route_t *route = mux_match_ex(mux, request->method, path, &params);
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
    {
        char loc[4096];
        if (mux_slash_redirect(mux, request->method, path, route,
                               loc, sizeof(loc))) {
            mux_redirect_location(writer, loc, request->query);
            return;
        }
        if (cleaned_differs) {
            mux_redirect_location(writer, path, request->query);
            return;
        }
    }
    if (route) {
        route_invoke(route, request, writer);
    } else {
        char allow[256];
        if (mux_fill_allow(mux, path, allow, sizeof(allow))) {
            neverc_http_set_status(writer, 405);
            neverc_http_set_header(writer, "Allow", allow);
            (void)neverc_http_write_string(writer, "Method Not Allowed\n");
        } else {
            neverc_http_set_status(writer, 404);
            (void)neverc_http_write_string(writer, "404 page not found\n");
        }
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
    if (pr->is_chunked) free((void *)pr->body);
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

static int http_valid_port(const char *s, size_t length) {
    if (!s) return 0;
    if (length == 0) return 1;
    unsigned value = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < '0' || c > '9') return 0;
        unsigned digit = (unsigned)(c - '0');
        if (value > (65535U - digit) / 10U) return 0;
        value = value * 10U + digit;
    }
    return value > 0;
}

/* Go httpguts.ValidHostHeader allowlist without comma (comma is kept
 * banned so Host cannot smuggle a second authority). '<' '>' '"' are
 * outside the table; accepting them used to XSS httputil dumps and
 * reflected Host / X-Forwarded-Host HTML. */
static int http_host_reg_name_byte(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z'))
        return 1;
    switch (c) {
    case '!': case '$': case '%': case '&': case '\'':
    case '(': case ')': case '*': case '+':
    case '-': case '.': case ';': case '=':
    case '_': case '~':
        return 1;
    default:
        return 0;
    }
}

/* RFC 9112: Host must be uri-host [ ":" port ]. Reject values that can
 * desynchronize intermediaries (userinfo, path, spaces, bad ports). */
static int http_valid_host(const char *value, size_t length) {
    if (!value || length == 0) return 0;
    if (value[0] == '[') {
        const char *close = (const char *)memchr(value, ']', length);
        if (!close || close == value + 1) return 0;
        size_t inner = (size_t)(close - value - 1);
        int has_colon = 0;
        for (size_t i = 0; i < inner; i++) {
            unsigned char c = (unsigned char)value[1 + i];
            if (c == ':') has_colon = 1;
            else if (!http_host_reg_name_byte(c))
                return 0;
        }
        if (!has_colon &&
            !(inner > 2 && (value[1] == 'v' || value[1] == 'V')))
            return 0;
        size_t after = length - (size_t)(close - value) - 1;
        if (after == 0) return 1;
        return close[1] == ':' && http_valid_port(close + 2, after - 1);
    }

    const char *colon = (const char *)memchr(value, ':', length);
    size_t host_length = colon ? (size_t)(colon - value) : length;
    if (host_length == 0) return 0;
    for (size_t i = 0; i < host_length; i++) {
        if (!http_host_reg_name_byte((unsigned char)value[i]))
            return 0;
    }
    if (!colon) return 1;
    if (memchr(colon + 1, ':', length - host_length - 1)) return 0;
    return http_valid_port(colon + 1, length - host_length - 1);
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

static int http_forbidden_trailer(const char *name, size_t length) {
    return http_field_name_is(name, length, "Content-Length") ||
           http_field_name_is(name, length, "Transfer-Encoding") ||
           http_field_name_is(name, length, "Host") ||
           http_field_name_is(name, length, "Connection") ||
           http_field_name_is(name, length, "Trailer");
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
    if (digits == 0) return -1;
    if (digits < length) {
        if (line[digits] != ';' || digits + 1 >= length ||
            !http_is_tchar((unsigned char)line[digits + 1]) ||
            !http_valid_field_value(line + digits, length - digits))
            return -1;
    }
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
        if ((size_t)(line_end - cursor) > 8192) goto invalid;
        size_t chunk_size = 0;
        if (http_parse_chunk_size(cursor, (size_t)(line_end - cursor),
                                  &chunk_size) != 0)
            goto invalid;
        cursor = line_end + 2;

        if (chunk_size == 0) {
            int trailer_count = 0;
            size_t trailer_bytes = 0;
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
                size_t line_length = (size_t)(line_end - cursor);
                if (trailer_count >= HTTP_MAX_REQUEST_HEADERS ||
                    line_length > 8192 ||
                    trailer_bytes > (size_t)1024 * 1024 - line_length)
                    goto invalid;
                if (*cursor == ' ' || *cursor == '\t') goto invalid;
                const char *colon = (const char *)memchr(
                    cursor, ':', line_length);
                if (!colon || !http_valid_token(
                        cursor, (size_t)(colon - cursor)))
                    goto invalid;
                const char *value = colon + 1;
                size_t value_length = (size_t)(line_end - value);
                http_trim_ows(&value, &value_length);
                if (!http_valid_field_value(value, value_length) ||
                    http_forbidden_trailer(
                        cursor, (size_t)(colon - cursor)))
                    goto invalid;
                trailer_count++;
                trailer_bytes += line_length;
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

static int http_method_implies_body(const char *method) {
    return method &&
           (strcmp(method, "POST") == 0 ||
            strcmp(method, "PUT") == 0 ||
            strcmp(method, "PATCH") == 0);
}

/* Returns 0 on success, -1 if incomplete, -2 on invalid or ambiguous input. */
static int parse_request_mode(const char *raw, size_t raw_length,
                              parsed_request_t *request, size_t *consumed,
                              int headers_only) {
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
    int asterisk_form = target_length == 1 && target[0] == '*';
    if (target[0] != '/' && !asterisk_form)
        goto invalid;
    /* Origin-form is absolute-path. Reject scheme-relative "//host" (the
     * leftover of absolute-form after requiring a leading '/') and backslash
     * (browsers treat "/\\" as a network-path). RFC 9112 / Go allow empty
     * path segments (`/foo//bar`); only a path that *starts* with `//` is
     * origin-form leftover. `//` in the query is fine. */
    const char *target_query = (const char *)memchr(target, '?', target_length);
    size_t path_length = target_query
        ? (size_t)(target_query - target) : target_length;
    if (path_length >= 2 &&
        neverc_url_path_n_is_protocol_relative(target, path_length))
        goto invalid;
    for (size_t i = 0; i < target_length; i++) {
        unsigned char c = (unsigned char)target[i];
        if (c <= 0x20 || c == 0x7f || c == '#' || c == '\\') goto invalid;
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
        (query && !request->query) ||
        strcmp(request->method, "CONNECT") == 0 ||
        (asterisk_form && strcmp(request->method, "OPTIONS") != 0))
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
            if (host_seen || value_length == 0 ||
                !http_valid_host(value, value_length))
                goto invalid;
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

    if (is_http_11 && !host_seen)
        goto invalid;
    if ((content_length_seen && transfer_encoding_seen) ||
        (is_http_10 && transfer_encoding_seen))
        goto invalid;

    if (is_http_11 && !content_length_seen && !transfer_encoding_seen &&
        http_method_implies_body(request->method))
        goto invalid;
    if (is_http_10 && !content_length_seen && !transfer_encoding_seen &&
        http_method_implies_body(request->method))
        goto invalid;

    size_t header_size = (size_t)(header_end + 4 - raw);
    if (headers_only) {
        *consumed = header_size;
        return 0;
    }
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

static int parse_request(const char *raw, size_t raw_length,
                         parsed_request_t *request, size_t *consumed) {
    return parse_request_mode(raw, raw_length, request, consumed, 0);
}

static int parse_request_headers(const char *raw, size_t raw_length,
                                 parsed_request_t *request,
                                 size_t *header_size) {
    return parse_request_mode(raw, raw_length, request, header_size, 1);
}

#ifdef NEVERC_NETWORK_PROTOCOL_FUZZING
int neverc_http_test_fuzz_request_parser(const void *input,
                                         size_t input_length) {
    static const char empty_input = '\0';
    const char *raw = input ? (const char *)input : &empty_input;
    if (!input && input_length != 0) return -2;
    parsed_request_t request;
    size_t consumed = 0;
    int result = parse_request(raw, input_length, &request, &consumed);
    parsed_request_free(&request);
    return result;
}
#endif

static int fill_request(const parsed_request_t *pr,
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

    nc_buf_reset(raw_hdr_buf);
    for (int i = 0; i < pr->nheaders; i++) {
        size_t nlen = strlen(pr->header_names[i]);
        size_t vlen = strlen(pr->header_values[i]);
        if (nc_buf_append(raw_hdr_buf, pr->header_names[i], nlen) != 0 ||
            nc_buf_append(raw_hdr_buf, "\0", 1) != 0 ||
            nc_buf_append(raw_hdr_buf, pr->header_values[i], vlen) != 0 ||
            nc_buf_append(raw_hdr_buf, "\0", 1) != 0) {
            nc_buf_reset(raw_hdr_buf);
            req->raw_headers = NULL;
            req->nheaders = 0;
            return -1;
        }
    }
    req->raw_headers = raw_hdr_buf->data;
    req->nheaders = pr->nheaders;
    return 0;
}

/* ======================================================================
 * HTTP Connection — event-driven state machine (per connection)
 * ====================================================================== */

typedef struct http_worker http_worker_t;

typedef enum {
    HC_STATE_HANDSHAKING,
    HC_STATE_READING,
    HC_STATE_PROCESSING,
    HC_STATE_CLOSING
} hc_state_t;

struct http_conn {
    nc_sock_t          fd;
    hc_state_t         state;
    nc_buf_t           read_buf;
    nc_buf_t           raw_hdr_buf;
    nc_buf_t           write_buf;
    nc_evloop_t       *loop;
    neverc_http_mux_t *mux;
    http_worker_t     *worker;
    int                max_requests;
    int                requests_served;
    uint64_t           last_active;
    uint64_t           request_started;
    int                continue_sent;
    int                idle_timeout_ms;
    int                read_header_timeout_ms;
    int                read_timeout_ms;
    int                write_timeout_ms;
    size_t             max_read_size;
    size_t             max_header_size;
    size_t             max_body_size;
    int                gzip_enabled;
    int                gzip_level;
    size_t             gzip_min_size;
    int                access_log_enabled;
    neverc_http_access_log_func_t access_log;
    const char         *alt_svc;
    int                handler_timeout_ms;
    nc_threadpool_t    *stream_pool;
    void               *active_stream_task;
    neverc_tcp_conn_t  *tcp;
    neverc_tls_conn_t  *tls;
    int                 poll_registered;
    int                 handshake_interest;
    int                 tls_want_write;
    int                 tls_close_started;
    uint64_t            write_started;
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
                                    const neverc_http_server_config_t *config,
                                    nc_threadpool_t *stream_pool) {
    ensure_conn_pool();
    http_conn_t *hc = (http_conn_t *)nc_bufpool_pop(&g_conn_pool_cache);
    if (!hc) return NULL;
    hc->fd = fd;
    hc->state = HC_STATE_READING;
    nc_buf_init(&hc->read_buf);
    nc_buf_init(&hc->raw_hdr_buf);
    nc_buf_init(&hc->write_buf);
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
    hc->max_header_size = (size_t)config->max_header_size;
    hc->max_body_size = (size_t)config->max_body_size;
    hc->gzip_enabled = config->gzip_enabled;
    hc->gzip_level = config->gzip_level;
    hc->gzip_min_size = config->gzip_min_size;
    hc->access_log_enabled = config->access_log_enabled;
    hc->access_log = config->access_log;
    hc->alt_svc = config->alt_svc;
    hc->handler_timeout_ms = config->handler_timeout_ms;
    hc->stream_pool = stream_pool;
    hc->active_stream_task = NULL;
    hc->tcp = NULL;
    hc->tls = NULL;
    hc->poll_registered = 0;
    hc->handshake_interest = NC_EV_READ;
    hc->tls_want_write = 0;
    hc->tls_close_started = 0;
    hc->write_started = 0;
    hc->last_active = nc_monotonic_ms();
    hc->request_started = hc->last_active;
    hc->continue_sent = 0;
    hc->next = hc->prev = NULL;
    return hc;
}

static void http_conn_free(http_conn_t *hc) {
    if (!hc) return;
    if (hc->fd != NC_INVALID_SOCK && hc->poll_registered)
        nc_poller_del(hc->loop->poller, hc->fd);
    if (hc->tls) {
        neverc_tls_close(hc->tls);
        hc->tls = NULL;
    }
    if (hc->tcp) {
        neverc_tcp_close(hc->tcp);
        hc->tcp = NULL;
    } else if (hc->fd != NC_INVALID_SOCK) {
        nc_sock_close(hc->fd);
    }
    hc->fd = NC_INVALID_SOCK;
    nc_buf_free(&hc->read_buf);
    nc_buf_free(&hc->raw_hdr_buf);
    nc_buf_free(&hc->write_buf);
    memset(hc, 0, sizeof(*hc));
    nc_bufpool_push(&g_conn_pool_cache, hc);
}

static int http_conn_transport_write(void *context, const void *data,
                                     size_t length, int timeout_ms) {
    http_conn_t *connection = (http_conn_t *)context;
    (void)timeout_ms;
    if (!connection || (!data && length > 0) ||
        connection->state == HC_STATE_CLOSING ||
        connection->write_buf.len > HTTP_MAX_PENDING_OUTPUT ||
        length > HTTP_MAX_PENDING_OUTPUT - connection->write_buf.len)
        return -1;
    if (length == 0) return 0;
    if (nc_buf_append(&connection->write_buf, data, length) != 0)
        return -1;
    if (connection->write_started == 0)
        connection->write_started = nc_monotonic_ms();
    return 0;
}

static int http_conn_output_pending(const http_conn_t *connection) {
    return connection &&
        (connection->write_buf.len > 0 || connection->tls_want_write);
}

static int http_conn_update_interest(http_conn_t *connection) {
    if (!connection || !connection->poll_registered ||
        connection->state == HC_STATE_PROCESSING)
        return 0;
    int events = 0;
    if (connection->state == HC_STATE_HANDSHAKING) {
        events = connection->handshake_interest;
    } else {
        if (connection->state == HC_STATE_READING) events |= NC_EV_READ;
        if (http_conn_output_pending(connection)) events |= NC_EV_WRITE;
    }
    if (events == 0) return 0;
    return nc_poller_mod(connection->loop->poller, connection->fd,
                         events, connection);
}

static int http_conn_drain_output(http_conn_t *connection) {
    while (connection->write_buf.len > 0) {
        if (connection->tls) {
            neverc_tls_io_result_t result = neverc_tls_try_write(
                connection->tls, connection->write_buf.data,
                connection->write_buf.len);
            if (result.transferred > 0) {
                nc_buf_consume(&connection->write_buf,
                               result.transferred);
                connection->last_active = nc_monotonic_ms();
            }
            if (result.status == NEVERC_TLS_IO_WANT_WRITE) {
                connection->tls_want_write = 1;
                return 0;
            }
            if (result.status != NEVERC_TLS_IO_OK ||
                result.transferred == 0)
                return -1;
            connection->tls_want_write = 0;
            continue;
        }

        size_t amount = connection->write_buf.len;
        if (amount > (size_t)INT_MAX) amount = (size_t)INT_MAX;
#ifdef _WIN32
        int sent = send(connection->fd, connection->write_buf.data,
                        (int)amount, 0);
        if (sent < 0 && WSAGetLastError() == WSAEINTR) continue;
        if (sent < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#else
        ssize_t sent = send(connection->fd, connection->write_buf.data,
                            amount, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
#endif
        if (sent <= 0) return -1;
        nc_buf_consume(&connection->write_buf, (size_t)sent);
        connection->last_active = nc_monotonic_ms();
    }

    if (connection->tls) {
        neverc_tls_io_result_t flush = neverc_tls_flush(connection->tls);
        if (flush.status == NEVERC_TLS_IO_WANT_WRITE) {
            connection->tls_want_write = 1;
            return 0;
        }
        if (flush.status != NEVERC_TLS_IO_OK) return -1;
    }
    connection->tls_want_write = 0;
    connection->write_started = 0;
    return 0;
}

static int http_conn_ready_to_free(http_conn_t *connection) {
    if (!connection || connection->state != HC_STATE_CLOSING ||
        http_conn_output_pending(connection))
        return 0;
    if (!connection->tls || connection->tls_close_started) return 1;
    connection->tls_close_started = 1;
    neverc_tls_io_result_t result =
        neverc_tls_try_close_notify(connection->tls);
    if (result.status == NEVERC_TLS_IO_WANT_WRITE) {
        connection->tls_want_write = 1;
        if (connection->write_started == 0)
            connection->write_started = nc_monotonic_ms();
        return 0;
    }
    return 1;
}

static void http_request_context_release(
    neverc_context_t *context,
    neverc_context_cancel_handle_t *cancel_handle) {
    if (cancel_handle) {
        neverc_context_cancel_handle_cancel(cancel_handle);
        neverc_context_cancel_handle_free(cancel_handle);
        if (context)
            neverc_context_free(context);
    } else if (context) {
        neverc_context_free(context);
    }
}

typedef enum {
    HTTP1_BODY_CHUNK_SIZE,
    HTTP1_BODY_CHUNK_DATA,
    HTTP1_BODY_CHUNK_DATA_CRLF,
    HTTP1_BODY_CHUNK_TRAILERS,
    HTTP1_BODY_DONE,
} http1_body_state_t;

typedef struct {
    http_conn_t *connection;
    neverc_tls_conn_t *tls;
    nc_buf_t wire;
    int is_chunked;
    size_t remaining;
    size_t chunk_remaining;
    size_t decoded;
    size_t max_body_size;
    size_t max_trailer_size;
    size_t trailer_bytes;
    unsigned int trailer_count;
    uint64_t started_ms;
    int read_timeout_ms;
    volatile int canceled;
    int failed;
    http1_body_state_t state;
} http1_request_stream_t;

typedef struct {
    http_conn_t *connection;
    parsed_request_t parsed;
    neverc_http_request_t request;
    neverc_http_response_writer_t *writer;
    neverc_context_t *context;
    neverc_context_cancel_handle_t *cancel;
    http1_request_stream_t body_stream;
    uint64_t handler_started_ms;
} http1_stream_task_t;

static void http1_stream_resume_task(void *argument);

static int http1_stream_wait_readable(http1_request_stream_t *stream,
                                      neverc_context_t *context) {
    for (;;) {
        if (stream->canceled || neverc_context_done(context)) return -1;
        int wait_ms = 100;
        if (stream->read_timeout_ms > 0) {
            uint64_t now = nc_monotonic_ms();
            uint64_t elapsed = now - stream->started_ms;
            if (elapsed >= (uint64_t)stream->read_timeout_ms) return -1;
            uint64_t remaining = (uint64_t)stream->read_timeout_ms - elapsed;
            if (remaining < (uint64_t)wait_ms) wait_ms = (int)remaining;
        }
#ifdef _WIN32
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(stream->connection->fd, &readfds);
        struct timeval timeout;
        timeout.tv_sec = wait_ms / 1000;
        timeout.tv_usec = (wait_ms % 1000) * 1000;
        int ready = select(0, &readfds, NULL, NULL, &timeout);
        if (ready == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) continue;
        if (ready != 0) return ready > 0 ? 0 : -1;
#else
        struct pollfd descriptor;
        descriptor.fd = stream->connection->fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        int ready;
        do {
            ready = poll(&descriptor, 1, wait_ms);
        } while (ready < 0 && errno == EINTR);
        if (ready < 0 || (ready > 0 &&
            (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0))
            return -1;
        if (ready > 0) return 0;
#endif
    }
}

static int http1_stream_fill(http1_request_stream_t *stream,
                             neverc_context_t *context) {
    char input[8192];
    if (stream->tls) {
        neverc_context_t *read_context = context;
        neverc_context_cancel_handle_t *read_cancel = NULL;
        if (stream->read_timeout_ms > 0) {
            uint64_t elapsed = nc_monotonic_ms() - stream->started_ms;
            if (elapsed >= (uint64_t)stream->read_timeout_ms) return -1;
            int64_t remaining = (int64_t)
                ((uint64_t)stream->read_timeout_ms - elapsed);
            read_context = neverc_context_with_timeout_handle(
                context, remaining, &read_cancel);
            if (!read_context || !read_cancel) {
                if (read_context) neverc_context_free(read_context);
                if (read_cancel)
                    neverc_context_cancel_handle_free(read_cancel);
                return -1;
            }
        }
        int received = neverc_tls_read_context(
            stream->tls, read_context, input, sizeof(input));
        if (read_cancel)
            neverc_context_cancel_handle_cancel(read_cancel);
        if (read_context != context) neverc_context_free(read_context);
        if (read_cancel)
            neverc_context_cancel_handle_free(read_cancel);
        if (received <= 0) return -1;
        return nc_buf_append(&stream->wire, input,
                             (size_t)received) == 0 ? 0 : -1;
    }
    for (;;) {
        if (stream->canceled || neverc_context_done(context)) return -1;
#ifdef _WIN32
        int received = recv(stream->connection->fd, input,
                            (int)sizeof(input), 0);
#else
        ssize_t received = recv(stream->connection->fd, input,
                                sizeof(input), MSG_DONTWAIT);
#endif
        if (received > 0)
            return nc_buf_append(&stream->wire, input,
                                 (size_t)received) == 0 ? 0 : -1;
        if (received == 0) return -1;
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEINTR) continue;
        if (error != WSAEWOULDBLOCK) return -1;
#else
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
#endif
        if (http1_stream_wait_readable(stream, context) != 0) return -1;
    }
}

static int http1_stream_validate_trailer(const char *line, size_t length) {
    if (length == 0) return 1;
    if (*line == ' ' || *line == '\t') return -1;
    const char *colon = (const char *)memchr(line, ':', length);
    if (!colon || !http_valid_token(line, (size_t)(colon - line))) return -1;
    const char *value = colon + 1;
    size_t value_length = length - (size_t)(value - line);
    http_trim_ows(&value, &value_length);
    size_t name_length = (size_t)(colon - line);
    if (!http_valid_field_value(value, value_length) ||
        http_forbidden_trailer(line, name_length))
        return -1;
    return 0;
}

static int http1_request_stream_read(
    void *opaque, neverc_context_t *context,
    void *output, size_t output_capacity) {
    http1_request_stream_t *stream = (http1_request_stream_t *)opaque;
    if (!stream || !context || !output || output_capacity == 0 ||
        stream->failed || stream->canceled)
        return -1;

    if (!stream->is_chunked) {
        if (stream->remaining == 0) {
            stream->state = HTTP1_BODY_DONE;
            return 0;
        }
        if (stream->wire.len == 0 &&
            http1_stream_fill(stream, context) != 0)
            goto fail;
        size_t count = stream->wire.len;
        if (count > stream->remaining) count = stream->remaining;
        if (count > output_capacity) count = output_capacity;
        memcpy(output, stream->wire.data, count);
        nc_buf_consume(&stream->wire, count);
        stream->remaining -= count;
        stream->decoded += count;
        if (stream->remaining == 0) stream->state = HTTP1_BODY_DONE;
        return (int)count;
    }

    for (;;) {
        if (stream->state == HTTP1_BODY_DONE) return 0;
        if (stream->state == HTTP1_BODY_CHUNK_SIZE) {
            const char *line_end = stream->wire.len >= 2
                ? http_find_crlf(stream->wire.data,
                                 stream->wire.data + stream->wire.len)
                : NULL;
            if (!line_end) {
                if (stream->wire.len > 8194 ||
                    http1_stream_fill(stream, context) != 0)
                    goto fail;
                continue;
            }
            size_t line_length = (size_t)(line_end - stream->wire.data);
            if (line_length > 8192 || http_parse_chunk_size(
                    stream->wire.data, line_length,
                    &stream->chunk_remaining) != 0)
                goto fail;
            nc_buf_consume(&stream->wire, line_length + 2);
            if (stream->chunk_remaining == 0)
                stream->state = HTTP1_BODY_CHUNK_TRAILERS;
            else {
                if (stream->decoded > stream->max_body_size ||
                    stream->chunk_remaining >
                        stream->max_body_size - stream->decoded)
                    goto fail;
                stream->state = HTTP1_BODY_CHUNK_DATA;
            }
            continue;
        }
        if (stream->state == HTTP1_BODY_CHUNK_DATA) {
            if (stream->wire.len == 0 &&
                http1_stream_fill(stream, context) != 0)
                goto fail;
            size_t count = stream->wire.len;
            if (count > stream->chunk_remaining)
                count = stream->chunk_remaining;
            if (count > output_capacity) count = output_capacity;
            memcpy(output, stream->wire.data, count);
            nc_buf_consume(&stream->wire, count);
            stream->chunk_remaining -= count;
            stream->decoded += count;
            if (stream->chunk_remaining == 0)
                stream->state = HTTP1_BODY_CHUNK_DATA_CRLF;
            return (int)count;
        }
        if (stream->state == HTTP1_BODY_CHUNK_DATA_CRLF) {
            while (stream->wire.len < 2)
                if (http1_stream_fill(stream, context) != 0) goto fail;
            if (stream->wire.data[0] != '\r' ||
                stream->wire.data[1] != '\n')
                goto fail;
            nc_buf_consume(&stream->wire, 2);
            stream->state = HTTP1_BODY_CHUNK_SIZE;
            continue;
        }

        const char *line_end = stream->wire.len >= 2
            ? http_find_crlf(stream->wire.data,
                             stream->wire.data + stream->wire.len)
            : NULL;
        if (!line_end) {
            if (stream->wire.len > 8194 ||
                http1_stream_fill(stream, context) != 0)
                goto fail;
            continue;
        }
        size_t line_length = (size_t)(line_end - stream->wire.data);
        if (stream->trailer_count >= HTTP_MAX_REQUEST_HEADERS ||
            line_length > stream->max_trailer_size ||
            stream->trailer_bytes >
                stream->max_trailer_size - line_length ||
            stream->max_trailer_size - stream->trailer_bytes -
                line_length < 2)
            goto fail;
        int trailer_result = http1_stream_validate_trailer(
            stream->wire.data, line_length);
        if (trailer_result < 0) goto fail;
        stream->trailer_bytes += line_length + 2;
        if (trailer_result == 0) stream->trailer_count++;
        nc_buf_consume(&stream->wire, line_length + 2);
        if (trailer_result > 0) {
            stream->state = HTTP1_BODY_DONE;
            return 0;
        }
    }

fail:
    stream->failed = 1;
    return -1;
}

static void http1_request_stream_cancel(void *opaque, uint32_t error_code) {
    http1_request_stream_t *stream = (http1_request_stream_t *)opaque;
    (void)error_code;
    if (stream) stream->canceled = 1;
}

static void http1_stream_task_cleanup(http1_stream_task_t *task) {
    if (!task) return;
    http_request_context_release(task->context, task->cancel);
    rw_free(task->writer);
    parsed_request_free(&task->parsed);
    nc_buf_free(&task->body_stream.wire);
    free(task);
}

static void http1_stream_handler_task(void *argument) {
    http1_stream_task_t *task = (http1_stream_task_t *)argument;
    http_conn_t *connection = task->connection;
    nc_http_mux_dispatch(connection->mux, &task->request, task->writer);

    if (connection->handler_timeout_ms > 0 &&
        neverc_context_done(task->context)) {
        task->writer->keep_alive = 0;
        if (!task->writer->headers_sent) {
            task->writer->status = 503;
            task->writer->body_limit_exceeded = 0;
            nc_buf_reset(&task->writer->body);
            neverc_http_set_header(task->writer, "Content-Type",
                                   "text/plain; charset=utf-8");
            (void)neverc_http_write_string(task->writer,
                                           "handler timeout\n");
        }
    }
    if (task->body_stream.state != HTTP1_BODY_DONE)
        task->writer->keep_alive = 0;
    if (task->writer->chunked && !task->writer->chunked_ended) {
        if (neverc_http_end_chunked(task->writer) != 0)
            task->writer->keep_alive = 0;
    } else if (rw_flush(task->writer) != 0) {
        task->writer->keep_alive = 0;
    }

    if (nc_evloop_post(connection->loop,
                       http1_stream_resume_task, task) != 0) {
        connection->active_stream_task = NULL;
        connection->state = HC_STATE_CLOSING;
        http1_stream_task_cleanup(task);
    }
}

/* Returns 1 after ownership of parsed transfers to the task, otherwise -1. */
static int http_conn_start_streaming(http_conn_t *connection,
                                     parsed_request_t *parsed,
                                     size_t header_size) {
    http1_stream_task_t *task =
        (http1_stream_task_t *)calloc(1, sizeof(*task));
    if (!task) return -1;
    task->connection = connection;
    task->parsed = *parsed;
    memset(parsed, 0, sizeof(*parsed));

    if (connection->handler_timeout_ms > 0) {
        task->context = neverc_context_with_timeout_handle(
            neverc_context_background(), connection->handler_timeout_ms,
            &task->cancel);
    } else {
        task->context = neverc_context_with_cancel_handle(
            neverc_context_background(), &task->cancel);
    }
    if (!task->context || !task->cancel) goto fail;

    if (fill_request(&task->parsed, &task->request,
                     &connection->raw_hdr_buf) != 0)
        goto fail;
    task->request.body = NULL;
    task->request.body_len = 0;
    task->request.context = task->context;

    http1_request_stream_t *stream = &task->body_stream;
    stream->connection = connection;
    stream->tls = connection->tls;
    stream->is_chunked = task->parsed.is_chunked;
    stream->remaining = task->parsed.content_length > 0
        ? (size_t)task->parsed.content_length : 0;
    stream->max_body_size = connection->max_body_size;
    stream->max_trailer_size = connection->max_header_size;
    stream->started_ms = connection->request_started;
    stream->read_timeout_ms = connection->read_timeout_ms;
    stream->state = stream->is_chunked ? HTTP1_BODY_CHUNK_SIZE
        : stream->remaining == 0 ? HTTP1_BODY_DONE : HTTP1_BODY_CHUNK_DATA;

    nc_buf_consume(&connection->read_buf, header_size);
    stream->wire = connection->read_buf;
    nc_buf_init(&connection->read_buf);
    task->request.protocol_stream = stream;
    task->request.body_stream = stream;
    task->request.body_stream_read = http1_request_stream_read;
    task->request.body_stream_cancel = http1_request_stream_cancel;

    task->writer = rw_new(connection->tls ? NC_INVALID_SOCK : connection->fd,
                          task->parsed.keep_alive,
                          connection, 0);
    if (!task->writer) goto fail;
    task->writer->request_body_len = stream->remaining;
    task->writer->head_request = strcmp(task->request.method, "HEAD") == 0;
    task->writer->gzip_enabled = connection->gzip_enabled;
    task->writer->gzip_level = connection->gzip_level;
    task->writer->gzip_min_size = connection->gzip_min_size;
    task->writer->write_timeout_ms = connection->write_timeout_ms;
    if (connection->alt_svc)
        neverc_http_set_header(task->writer, "Alt-Svc",
                               connection->alt_svc);
    if (connection->tls) {
        task->writer->transport_write = https_transport_write;
        task->writer->transport_context = connection->tls;
        task->writer->transport_tcp = connection->tcp;
    }
    const char *accept_encoding = neverc_http_request_header(
        &task->request, "Accept-Encoding");
    task->writer->accepts_gzip = accept_encoding && http_value_has_token(
        accept_encoding, strlen(accept_encoding), "gzip");
    task->handler_started_ms = nc_monotonic_ms();

    if (connection->tls &&
        neverc_tls_set_reactor_mode(connection->tls, 0) != 0)
        goto fail;
    (void)nc_poller_del(connection->loop->poller, connection->fd);
    connection->poll_registered = 0;
    connection->state = HC_STATE_PROCESSING;
    connection->active_stream_task = task;
    if (task->parsed.expect_continue) {
        const char *continue_response = "HTTP/1.1 100 Continue\r\n\r\n";
        int write_result = connection->tls
            ? https_transport_write(
                connection->tls, continue_response,
                strlen(continue_response), connection->write_timeout_ms)
            : nc_http_sock_write_all_timeout(
                connection->fd, continue_response,
                strlen(continue_response), connection->write_timeout_ms);
        if (write_result != 0)
            goto fail_after_detach;
    }
    if (nc_threadpool_try_submit(connection->stream_pool,
                                 http1_stream_handler_task, task) != 0)
        goto fail_after_detach;
    return 1;

fail_after_detach:
    connection->active_stream_task = NULL;
    connection->state = HC_STATE_CLOSING;
    if (connection->tls)
        (void)neverc_tls_set_reactor_mode(connection->tls, 1);
fail:
    http1_stream_task_cleanup(task);
    return -1;
}

static int http_parsed_body_too_large(const parsed_request_t *pr,
                                      size_t max_body) {
    if (pr->content_length > 0 && (size_t)pr->content_length > max_body)
        return 1;
    if (pr->body_len > max_body)
        return 1;
    return 0;
}

static void http_conn_reject_too_large(http_conn_t *hc) {
    const char *too_large =
        "HTTP/1.1 413 Payload Too Large\r\n"
        "Content-Length: 0\r\nConnection: close\r\n\r\n";
    (void)http_conn_transport_write(
        hc, too_large, strlen(too_large), hc->write_timeout_ms);
    hc->state = HC_STATE_CLOSING;
}

static void http_conn_process(http_conn_t *hc) {
    if (http_conn_output_pending(hc)) return;
    while (hc->read_buf.len > 0 && hc->requests_served < hc->max_requests) {
        parsed_request_t stream_headers;
        size_t header_size = 0;
        int header_result = parse_request_headers(
            hc->read_buf.data, hc->read_buf.len,
            &stream_headers, &header_size);
        if (header_result == 0) {
            if (http_parsed_body_too_large(&stream_headers,
                                           hc->max_body_size)) {
                http_conn_reject_too_large(hc);
                parsed_request_free(&stream_headers);
                return;
            }
            int streaming = nc_http_mux_is_streaming(
                hc->mux, stream_headers.method, stream_headers.path);
            if (streaming) {
                if (http_conn_start_streaming(
                        hc, &stream_headers, header_size) < 0) {
                    parsed_request_free(&stream_headers);
                    hc->state = HC_STATE_CLOSING;
                }
                return;
            }
            /* RFC 9110: send 100 Continue after headers, before the body.
             * The buffered parser otherwise waits for the full body first,
             * which deadlocks clients that withhold the body until 100. */
            if (stream_headers.expect_continue && !hc->continue_sent &&
                (stream_headers.content_length > 0 ||
                 stream_headers.is_chunked)) {
                const char *cont = "HTTP/1.1 100 Continue\r\n\r\n";
                if (http_conn_transport_write(
                        hc, cont, strlen(cont), hc->write_timeout_ms) != 0 ||
                    http_conn_drain_output(hc) != 0) {
                    parsed_request_free(&stream_headers);
                    hc->state = HC_STATE_CLOSING;
                    return;
                }
                hc->continue_sent = 1;
            }
            parsed_request_free(&stream_headers);
        }

        parsed_request_t pr;
        size_t consumed = 0;
        int rc = parse_request(hc->read_buf.data, hc->read_buf.len,
                                &pr, &consumed);
        if (rc == -1) return; /* need more data */
        if (rc == -2) {
            const char *err_resp =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            (void)http_conn_transport_write(
                hc, err_resp, strlen(err_resp), hc->write_timeout_ms);
            hc->state = HC_STATE_CLOSING;
            return;
        }
        if (http_parsed_body_too_large(&pr, hc->max_body_size)) {
            http_conn_reject_too_large(hc);
            parsed_request_free(&pr);
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
            (void)http_conn_transport_write(
                hc, rate_resp, strlen(rate_resp), hc->write_timeout_ms);
            parsed_request_free(&pr);
            hc->state = HC_STATE_CLOSING;
            return;
        }

        neverc_http_request_t req;
        if (fill_request(&pr, &req, &hc->raw_hdr_buf) != 0) {
            parsed_request_free(&pr);
            hc->state = HC_STATE_CLOSING;
            return;
        }
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

        /* Route with path parameter extraction. Go ServeMux cleanPath
         * first so `/hello/../hello` 301s instead of 404. */
        path_params_t params;
        char cleaned[4096];
        int cleaned_differs = 0;
        const char *match_path = mux_effective_path(
            req.method, req.path, cleaned, sizeof(cleaned),
            &cleaned_differs);
        memset(&params, 0, sizeof(params));
        route_t *route =
            mux_match_ex(hc->mux, req.method, match_path, &params);

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
        if (hc->alt_svc)
            neverc_http_set_header(w, "Alt-Svc", hc->alt_svc);
        w->transport_write = http_conn_transport_write;
        w->transport_context = hc;
        if (hc->tls) w->transport_tcp = hc->tcp;
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

        {
            char loc[4096];
            if (mux_slash_redirect(hc->mux, req.method, match_path, route,
                                   loc, sizeof(loc))) {
                mux_redirect_location(w, loc, req.query);
            } else if (cleaned_differs) {
                mux_redirect_location(w, match_path, req.query);
            } else if (route) {
                route_invoke(route, &req, w);
            } else {
                char allow[256];
                if (mux_fill_allow(hc->mux, match_path, allow, sizeof(allow))) {
                    neverc_http_set_status(w, 405);
                    neverc_http_set_header(w, "Allow", allow);
                    neverc_http_write_string(w, "Method Not Allowed\n");
                } else {
                    neverc_http_set_status(w, 404);
                    neverc_http_write_string(w, "404 page not found\n");
                }
            }
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

        if (w->aborted) {
            w->keep_alive = 0;
        } else if (w->chunked && !w->chunked_ended) {
            if (neverc_http_end_chunked(w) != 0)
                w->keep_alive = 0;
        } else {
            if (rw_flush(w) != 0) w->keep_alive = 0;
        }

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
        hc->continue_sent = 0;

        if (should_close) {
            hc->state = HC_STATE_CLOSING;
            return;
        }
        if (http_conn_output_pending(hc)) return;
    }

    if (hc->requests_served >= hc->max_requests)
        hc->state = HC_STATE_CLOSING;
}

static void http_conn_on_read(http_conn_t *hc) {
    size_t max_read = hc->max_read_size;

    char chunk[8192];
    for (;;) {
        int n;
        if (hc->tls) {
            neverc_tls_io_result_t result = neverc_tls_try_read(
                hc->tls, chunk, sizeof(chunk));
            if (result.status == NEVERC_TLS_IO_OK) {
                if (result.transferred == 0 ||
                    result.transferred > (size_t)INT_MAX) {
                    hc->state = HC_STATE_CLOSING;
                    return;
                }
                n = (int)result.transferred;
                hc->tls_want_write = 0;
            } else if (result.status == NEVERC_TLS_IO_WANT_READ) {
                break;
            } else if (result.status == NEVERC_TLS_IO_WANT_WRITE) {
                hc->tls_want_write = 1;
                if (hc->write_started == 0)
                    hc->write_started = nc_monotonic_ms();
                break;
            } else {
                hc->state = HC_STATE_CLOSING;
                return;
            }
        } else {
#ifdef _WIN32
            n = recv(hc->fd, chunk, sizeof(chunk), 0);
#else
            ssize_t received = recv(hc->fd, chunk, sizeof(chunk), 0);
            n = received > INT_MAX ? INT_MAX : (int)received;
#endif
        }
        if (n > 0) {
            if (hc->read_buf.len == 0 && hc->request_started == 0)
                hc->request_started = nc_monotonic_ms();
            if (nc_buf_append(&hc->read_buf, chunk, (size_t)n) != 0) {
                hc->state = HC_STATE_CLOSING;
                return;
            }
            hc->last_active = nc_monotonic_ms();
            const char *header_end = hc->read_buf.data
                ? strstr(hc->read_buf.data, "\r\n\r\n") : NULL;
            size_t header_length = header_end
                ? (size_t)(header_end - hc->read_buf.data) + 4 : 0;
            if ((!header_end && hc->read_buf.len > hc->max_header_size) ||
                (header_end && header_length > hc->max_header_size) ||
                hc->read_buf.len > max_read) {
                /* Send 413 Payload Too Large before closing */
                const char *err_resp =
                    "HTTP/1.1 413 Payload Too Large\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n";
                (void)http_conn_transport_write(
                    hc, err_resp, strlen(err_resp), hc->write_timeout_ms);
                hc->state = HC_STATE_CLOSING;
                return;
            }
            if (header_end) {
                http_conn_process(hc);
                if (hc->state != HC_STATE_READING) return;
            }
            continue;
        }
        if (n == 0) {
            hc->state = HC_STATE_CLOSING;
            return;
        }
        /* n < 0 */
        if (!hc->tls) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) break;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
        }
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
    nc_threadpool_t       *stream_pool;
};

static void worker_close_connection(http_conn_t *connection);

static void http1_stream_resume_task(void *argument) {
    http1_stream_task_t *task = (http1_stream_task_t *)argument;
    http_conn_t *connection = task->connection;
    http_worker_t *worker = connection->worker;
    int hijacked = task->writer->hijacked;
    int should_close = hijacked || !task->writer->keep_alive ||
        task->body_stream.failed || task->body_stream.canceled ||
        task->body_stream.state != HTTP1_BODY_DONE;

    if (connection->access_log_enabled) {
        double duration =
            (double)(nc_monotonic_ms() - task->handler_started_ms);
        if (connection->access_log) {
            connection->access_log(
                task->request.method, task->request.path,
                task->writer->status, duration, task->writer->body.len);
        } else {
            fprintf(stdout, "%s %s %d %.3fms %zu\n",
                    task->request.method, task->request.path,
                    task->writer->status, duration,
                    task->writer->body.len);
        }
    }

    connection->active_stream_task = NULL;
    if (!hijacked) {
        nc_buf_free(&connection->read_buf);
        connection->read_buf = task->body_stream.wire;
        nc_buf_init(&task->body_stream.wire);
    }
    http1_stream_task_cleanup(task);
    connection->requests_served++;
    connection->last_active = nc_monotonic_ms();
    connection->request_started = connection->read_buf.len > 0
        ? connection->last_active : 0;
    if (connection->tls &&
        neverc_tls_set_reactor_mode(connection->tls, 1) != 0)
        should_close = 1;

    if (should_close || connection->requests_served >= connection->max_requests ||
        !worker || !worker->server ||
        !nc_atomic_load(&worker->server->running)) {
        connection->state = HC_STATE_CLOSING;
    } else {
        connection->state = HC_STATE_READING;
    }
    if (connection->state == HC_STATE_READING) {
        if (nc_poller_add(connection->loop->poller, connection->fd,
                          NC_EV_READ, connection) != 0) {
            connection->state = HC_STATE_CLOSING;
        } else {
            connection->poll_registered = 1;
            http_conn_process(connection);
            if (connection->state != HC_STATE_CLOSING)
                (void)http_conn_update_interest(connection);
        }
    }

    if (connection->state == HC_STATE_CLOSING) {
        if (http_conn_ready_to_free(connection)) {
            worker_close_connection(connection);
        } else if (!connection->poll_registered &&
                   nc_poller_add(connection->loop->poller, connection->fd,
                                 NC_EV_WRITE, connection) == 0) {
            connection->poll_registered = 1;
        } else if (!connection->poll_registered) {
            worker_close_connection(connection);
        }
    }
}

static void worker_cancel_streams_task(void *argument) {
    http_worker_t *worker = (http_worker_t *)argument;
    for (http_conn_t *connection = worker->conns.head;
         connection; connection = connection->next) {
        http1_stream_task_t *task =
            (http1_stream_task_t *)connection->active_stream_task;
        if (!task) continue;
        task->body_stream.canceled = 1;
        if (task->cancel)
            neverc_context_cancel_handle_cancel(task->cancel);
    }
}

static void http_h2_connection_done(void *context) {
    neverc_http_server_t *server = (neverc_http_server_t *)context;
    if (server) nc_conn_limiter_release(&server->conn_limiter);
}

/* Returns 1 when the connection wrapper was freed after a successful H2
 * ownership transfer. */
static int http_conn_drive_handshake(http_conn_t *connection) {
    const char *error = NULL;
    int result = neverc_tls_handshake_step(connection->tls, &error);
    (void)error;
    if (result == NEVERC_TLS_HANDSHAKE_WANT_READ) {
        connection->handshake_interest = NC_EV_READ;
        return 0;
    }
    if (result == NEVERC_TLS_HANDSHAKE_WANT_WRITE) {
        connection->handshake_interest = NC_EV_WRITE;
        return 0;
    }
    if (result != NEVERC_TLS_HANDSHAKE_COMPLETE) {
        connection->state = HC_STATE_CLOSING;
        return 0;
    }

    const char *alpn = neverc_tls_alpn(connection->tls);
    if (alpn && strcmp(alpn, "h2") == 0) {
        http_worker_t *worker = connection->worker;
        neverc_http_server_t *server = worker ? worker->server : NULL;
        if (!server || !server->h2_server ||
            neverc_tls_set_reactor_mode(connection->tls, 0) != 0) {
            connection->state = HC_STATE_CLOSING;
            return 0;
        }
        if (connection->poll_registered) {
            (void)nc_poller_del(connection->loop->poller, connection->fd);
            connection->poll_registered = 0;
        }
        if (nc_h2_server_submit_tls(
                server->h2_server, connection->tls, connection->tcp,
                http_h2_connection_done, server) != 0) {
            (void)neverc_tls_set_reactor_mode(connection->tls, 1);
            if (nc_poller_add(connection->loop->poller, connection->fd,
                              NC_EV_READ, connection) == 0)
                connection->poll_registered = 1;
            connection->state = HC_STATE_CLOSING;
            return 0;
        }
        conn_list_remove(&worker->conns, connection);
        nc_atomic_dec(&worker->conn_count);
        connection->tls = NULL;
        connection->tcp = NULL;
        connection->fd = NC_INVALID_SOCK;
        http_conn_free(connection);
        return 1;
    }
    if (alpn && strcmp(alpn, "http/1.1") != 0) {
        connection->state = HC_STATE_CLOSING;
        return 0;
    }
    connection->state = HC_STATE_READING;
    connection->request_started = 0;
    connection->last_active = nc_monotonic_ms();
    connection->handshake_interest = NC_EV_READ;

    /* The TLS record reader may have consumed the first HTTP record while
     * reassembling Client Finished. It then lives in TLS userspace buffers
     * and cannot produce another edge-triggered socket event. */
    http_conn_on_read(connection);
    return 0;
}

static void worker_close_connection(http_conn_t *connection) {
    http_worker_t *worker = connection ? connection->worker : NULL;
    if (worker) {
        conn_list_remove(&worker->conns, connection);
        nc_atomic_dec(&worker->conn_count);
        if (worker->server)
            nc_conn_limiter_release(&worker->server->conn_limiter);
    }
    http_conn_free(connection);
}

/* Worker event handler — O(1) via hc->worker back-pointer */
static void worker_event_handler(nc_evloop_t *loop, nc_event_t *ev) {
    http_conn_t *hc = (http_conn_t *)ev->data;
    if (!hc) return;
    (void)loop;

    if (ev->events & NC_EV_ERROR) {
        worker_close_connection(hc);
        return;
    } else if (hc->state == HC_STATE_HANDSHAKING) {
        if ((ev->events & (NC_EV_READ | NC_EV_WRITE)) != 0 &&
            http_conn_drive_handshake(hc))
            return;
    } else {
        if ((ev->events & NC_EV_WRITE) != 0 &&
            http_conn_drain_output(hc) != 0)
            hc->state = HC_STATE_CLOSING;
        if (hc->state == HC_STATE_READING &&
            (ev->events & NC_EV_READ) != 0)
            http_conn_on_read(hc);
        if (hc->state == HC_STATE_READING &&
            !http_conn_output_pending(hc) && hc->read_buf.len > 0)
            http_conn_process(hc);
    }

    if (http_conn_ready_to_free(hc))
        worker_close_connection(hc);
    else if (hc->state != HC_STATE_PROCESSING &&
             http_conn_update_interest(hc) != 0)
        worker_close_connection(hc);
}

static void worker_sweep_idle(http_worker_t *w) {
    uint64_t now = nc_monotonic_ms();
    http_conn_t *hc = w->conns.head;
    while (hc) {
        http_conn_t *next = hc->next;
        if (hc->state == HC_STATE_PROCESSING) {
            hc = next;
            continue;
        }
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
        int write_timed_out = hc->write_started > 0 &&
            hc->write_timeout_ms > 0 &&
            now - hc->write_started > (uint64_t)hc->write_timeout_ms;
        if (request_timed_out || idle_timed_out || write_timed_out) {
            if (request_timed_out && hc->state == HC_STATE_READING) {
                const char *timeout_response =
                    "HTTP/1.1 408 Request Timeout\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n";
                (void)http_conn_transport_write(
                    hc, timeout_response, strlen(timeout_response),
                    hc->write_timeout_ms);
            }
            hc->state = HC_STATE_CLOSING;
            if (write_timed_out || http_conn_ready_to_free(hc))
                worker_close_connection(hc);
            else if (http_conn_update_interest(hc) != 0)
                worker_close_connection(hc);
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
    nc_evloop_finish_run(loop);
    return NULL;
}

static void distribute_conn_task(void *arg) {
    http_conn_t *hc = (http_conn_t *)arg;
    http_worker_t *worker = hc->worker;

    nc_set_nodelay(hc->fd);
    nc_set_keepalive(hc->fd);
    nc_set_quickack(hc->fd);

    int initial_events = hc->state == HC_STATE_HANDSHAKING
        ? hc->handshake_interest : NC_EV_READ;
    if (nc_poller_add(hc->loop->poller, hc->fd,
                      initial_events, hc) != 0) {
        nc_conn_limiter_release(&worker->server->conn_limiter);
        http_conn_free(hc);
        nc_atomic_dec(&worker->conn_count);
        return;
    }
    hc->poll_registered = 1;
    conn_list_add(&worker->conns, hc);

    /*
     * Edge-triggered pollers (kqueue EV_CLEAR / epoll EPOLLET) may not fire
     * if data arrived before we added the fd. Do an eager read to handle
     * this race condition.
     */
    if (hc->state == HC_STATE_HANDSHAKING) {
        if (http_conn_drive_handshake(hc)) return;
    } else {
        http_conn_on_read(hc);
    }
    if (http_conn_ready_to_free(hc))
        worker_close_connection(hc);
    else if (hc->state != HC_STATE_PROCESSING &&
             http_conn_update_interest(hc) != 0)
        worker_close_connection(hc);
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
                                        worker, &srv->config,
                                        srv->stream_pool);
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
    config.alt_svc = NULL;
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
            config->access_log_enabled == 1) &&
           (!config->alt_svc ||
            (strlen(config->alt_svc) <= 1024U &&
             !strchr(config->alt_svc, '\r') &&
             !strchr(config->alt_svc, '\n')));
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
    char *alt_svc = effective.alt_svc ? strdup(effective.alt_svc) : NULL;
    if (effective.alt_svc && !alt_svc) {
        free(server);
        return NULL;
    }
    server->listen_fd = NC_INVALID_SOCK;
    server->mux = mux;
    server->config = effective;
    server->config.alt_svc = alt_svc;
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
    free((char *)server->config.alt_svc);
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
    if (http_mux_has_streaming_routes(server->mux)) {
        server->stream_pool = nc_threadpool_create(worker_count);
        if (!server->stream_pool) goto done;
    }
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
    nc_threadpool_destroy(server->stream_pool);
    server->stream_pool = NULL;
    for (int i = 0; i < initialized_workers; i++)
        if (server->workers[i].loop)
            nc_evloop_dispatch_pending(server->workers[i].loop);
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
    if (server->stream_pool) {
        nc_threadpool_destroy(server->stream_pool);
        server->stream_pool = NULL;
    }
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
    for (int i = 0; i < server->nworkers; i++)
        (void)nc_evloop_post(server->workers[i].loop,
                             worker_cancel_streams_task,
                             &server->workers[i]);

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
} https_accept_ctx_t;

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

        int worker_index = 0;
        int minimum_load = nc_atomic_load(
            &server->workers[0].conn_count);
        for (int index = 1; index < server->nworkers; index++) {
            int load = nc_atomic_load(
                &server->workers[index].conn_count);
            if (load < minimum_load) {
                minimum_load = load;
                worker_index = index;
            }
        }
        http_worker_t *worker = &server->workers[worker_index];
        http_conn_t *connection = http_conn_new(
            socket_handle, worker->loop, server->mux, worker,
            &server->config, server->stream_pool);
        if (!connection) {
            neverc_tcp_close(tcp);
            nc_conn_limiter_release(&server->conn_limiter);
            continue;
        }
        connection->tcp = tcp;
        const char *tls_error = NULL;
        connection->tls = neverc_tls_server_begin(
            tcp, accept_ctx->tls_config, &tls_error);
        (void)tls_error;
        if (!connection->tls) {
            http_conn_free(connection);
            nc_conn_limiter_release(&server->conn_limiter);
            continue;
        }
        connection->state = HC_STATE_HANDSHAKING;
        connection->request_started = nc_monotonic_ms();
        connection->last_active = connection->request_started;
        connection->handshake_interest = NC_EV_READ;
        nc_atomic_inc(&worker->conn_count);
        if (nc_evloop_post(worker->loop,
                           distribute_conn_task, connection) != 0) {
            nc_atomic_dec(&worker->conn_count);
            http_conn_free(connection);
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
    int initialized_workers = 0;
    int started_workers = 0;
    neverc_h2_server_t *h2_server = NULL;
    neverc_tls_config_t *tls_config = neverc_tls_config_new();
    if (!tls_config || neverc_tls_config_load_cert(
                           tls_config, cert_file, key_file) != 0)
        goto done;
    const char *alpn[] = { "h2", "http/1.1" };
    neverc_tls_config_set_alpn(tls_config, alpn, 2);
    h2_server = neverc_h2_server_create(server->mux);
    if (!h2_server) goto done;
    neverc_h2_server_set_alt_svc(h2_server, server->config.alt_svc);
    neverc_h2_server_set_max_body_size(
        h2_server, (size_t)server->config.max_body_size);
    neverc_h2_server_set_handler_timeout(
        h2_server, server->config.handler_timeout_ms);
    if (nc_h2_server_start_embedded(h2_server) != 0) goto done;
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
    if (http_mux_has_streaming_routes(server->mux)) {
        server->stream_pool = nc_threadpool_create(worker_count);
        if (!server->stream_pool) goto done;
    }
    server->nworkers = worker_count;
    server->workers = (http_worker_t *)calloc(
        (size_t)worker_count, sizeof(*server->workers));
    if (!server->workers) goto done;
    for (int index = 0; index < worker_count; index++) {
        http_worker_t *worker = &server->workers[index];
        conn_list_init(&worker->conns);
        initialized_workers++;
        worker->worker_idx = index;
        worker->server = server;
        worker->loop = nc_evloop_create();
        if (!worker->loop) goto stop_workers;
    }

    server->accept_loop = nc_evloop_create();
    if (!server->accept_loop) goto stop_workers;
    https_accept_ctx_t accept_ctx = {
        .server = server,
        .tls_config = tls_config,
    };
    if (nc_poller_add(server->accept_loop->poller, server->listen_fd,
                      NC_EV_READ, &accept_ctx) != 0)
        goto stop_workers;
    for (int index = 0; index < worker_count; index++) {
        if (nc_thread_create(&server->workers[index].thread,
                             worker_thread_func,
                             &server->workers[index]) != 0)
            goto stop_workers;
        started_workers++;
    }
    nc_atomic_store(&server->running, 1);
    if (nc_atomic_load(&server->stop_requested))
        neverc_http_server_shutdown(server);
    if (nc_atomic_load(&server->running))
        (void)nc_evloop_run(server->accept_loop,
                            https_accept_event_handler);
    result = 0;

stop_workers:
    nc_mutex_lock(&server->lifecycle_lock);
    nc_atomic_store(&server->running, 0);
    if (h2_server) neverc_h2_server_shutdown(h2_server);
    for (int index = 0; index < started_workers; index++)
        nc_evloop_stop(server->workers[index].loop);
    nc_mutex_unlock(&server->lifecycle_lock);
    for (int index = 0; index < started_workers; index++)
        nc_thread_join(server->workers[index].thread);
    nc_threadpool_destroy(server->stream_pool);
    server->stream_pool = NULL;
    for (int index = 0; index < initialized_workers; index++)
        if (server->workers[index].loop)
            nc_evloop_dispatch_pending(server->workers[index].loop);
    for (int index = 0; index < initialized_workers; index++) {
        http_conn_t *connection = server->workers[index].conns.head;
        while (connection) {
            http_conn_t *next = connection->next;
            nc_conn_limiter_release(&server->conn_limiter);
            http_conn_free(connection);
            connection = next;
        }
        if (server->workers[index].loop)
            nc_evloop_destroy(server->workers[index].loop);
        nc_mutex_destroy(&server->workers[index].conns.lock);
    }
    free(server->workers);
    server->workers = NULL;
    server->nworkers = 0;

done:
    nc_atomic_store(&server->running, 0);
    if (server->accept_loop) nc_evloop_destroy(server->accept_loop);
    server->accept_loop = NULL;
    if (server->listen_fd != NC_INVALID_SOCK)
        nc_sock_close(server->listen_fd);
    server->listen_fd = NC_INVALID_SOCK;
    if (server->stream_pool) {
        nc_threadpool_destroy(server->stream_pool);
        server->stream_pool = NULL;
    }
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
    /* Same leftover class as form_value: Go url.ParseQuery. */
    if (!query) return NULL;
    return neverc_http_form_value(query, strlen(query), key, buf, buflen);
}

neverc_tcp_conn_t *neverc_http_hijack(neverc_http_response_writer_t *w) {
    if (!w || w->hijacked || !w->owner) return NULL;

    http_conn_t *hc = w->owner;
    if (hc->tls || http_conn_output_pending(hc)) return NULL;

    if (w->request_consumed > hc->read_buf.len) return NULL;
    const void *preload = NULL;
    size_t preload_len = hc->read_buf.len - w->request_consumed;
    if (preload_len > 0) {
        preload = hc->read_buf.data + w->request_consumed;
    }

    const char *err = NULL;
    nc_sock_t fd = hc->fd;
    neverc_tcp_conn_t *conn = neverc_tcp_adopt(
#ifdef _WIN32
        (int)fd,
#else
        fd,
#endif
        preload, preload_len, &err);
    (void)err;
    if (!conn) return NULL;
    if (hc->poll_registered) {
        (void)nc_poller_del(hc->loop->poller, hc->fd);
        hc->poll_registered = 0;
    }
    nc_buf_consume(&hc->read_buf, w->request_consumed);
    w->hijacked = 1;
    hc->fd = NC_INVALID_SOCK;
    nc_buf_reset(&hc->read_buf);
    return conn;
}

int nc_http_hijack_tls(neverc_http_response_writer_t *writer,
                       neverc_tls_conn_t **tls,
                       neverc_tcp_conn_t **tcp) {
    if (tls) *tls = NULL;
    if (tcp) *tcp = NULL;
    if (!writer || !tls || !tcp || writer->hijacked ||
        writer->protocol_flush)
        return -1;

    if (writer->owner) {
        http_conn_t *connection = writer->owner;
        if (!connection->tls || !connection->tcp ||
            http_conn_output_pending(connection) ||
            writer->request_consumed > connection->read_buf.len)
            return -1;
        size_t preload_length = connection->read_buf.len -
            writer->request_consumed;
        const char *preload = preload_length > 0
            ? connection->read_buf.data + writer->request_consumed : NULL;
        if (neverc_tls_set_reactor_mode(connection->tls, 0) != 0)
            return -1;
        if (preload_length > 0 &&
            neverc_tls_preload_application_data(
                connection->tls, preload, preload_length) != 0) {
            (void)neverc_tls_set_reactor_mode(connection->tls, 1);
            return -1;
        }
        if (connection->poll_registered) {
            (void)nc_poller_del(connection->loop->poller,
                                connection->fd);
            connection->poll_registered = 0;
        }
        *tls = connection->tls;
        *tcp = connection->tcp;
        nc_buf_consume(&connection->read_buf,
                       writer->request_consumed);
        connection->tls = NULL;
        connection->tcp = NULL;
        connection->fd = NC_INVALID_SOCK;
        nc_buf_reset(&connection->read_buf);
    } else {
        if (!writer->transport_context || !writer->transport_tcp)
            return -1;
        *tls = (neverc_tls_conn_t *)writer->transport_context;
        *tcp = writer->transport_tcp;
    }
    writer->hijacked = 1;
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

static void http_write_html_escaped(neverc_http_response_writer_t *w,
                                    const char *s) {
    for (; *s; s++) {
        switch (*s) {
        case '&':
            neverc_http_write_string(w, "&amp;");
            break;
        case '<':
            neverc_http_write_string(w, "&lt;");
            break;
        case '>':
            neverc_http_write_string(w, "&gt;");
            break;
        case '"':
            neverc_http_write_string(w, "&quot;");
            break;
        case '\'':
            neverc_http_write_string(w, "&#39;");
            break;
        default: {
            char ch[2] = { *s, 0 };
            neverc_http_write_string(w, ch);
            break;
        }
        }
    }
}

void neverc_http_redirect(neverc_http_response_writer_t *w,
                            const char *url, int code) {
    if (!w || !url) return;
    if (code < 300 || code > 399) code = 302;
    neverc_http_set_status(w, code);
    /* Match Go http.Redirect: Location is the caller's URL, including
     * RFC 3986 network-path references (`//host`). Request-line parsing
     * already 400s origin-form `//host`; use neverc_url_is_safe_redirect
     * when the target is untrusted. */
    neverc_http_set_header(w, "Location", url);
    neverc_http_set_header(w, "Content-Type", "text/html; charset=utf-8");
    neverc_http_write_string(w, "<a href=\"");
    http_write_html_escaped(w, url);
    neverc_http_write_string(w, "\">");
    neverc_http_write_string(w, neverc_http_status_text(code));
    neverc_http_write_string(w, "</a>.\n");
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

/* Go url.QueryUnescape / ParseQuery: reject interior NUL, malformed %,
 * and encoded NUL. The span is not necessarily C-string terminated. */
static int http_form_unescape_span(const char *s, size_t n,
                                   char *out, size_t out_cap) {
    if (!out || out_cap == 0 || (!s && n > 0) || n == SIZE_MAX) return -1;
    if (s && n > 0 && memchr(s, '\0', n)) return -1;
    char *tmp = (char *)malloc(n + 1);
    if (!tmp) return -1;
    if (n) memcpy(tmp, s, n);
    tmp[n] = '\0';
    int decoded = neverc_url_query_unescape(tmp, out, out_cap);
    free(tmp);
    if (decoded < 0 || (size_t)decoded >= out_cap) return -1;
    return decoded;
}

const char *neverc_http_form_value(const char *body, size_t body_len,
                                     const char *key, char *buf, size_t buflen) {
    if (!body || !key || !buf || buflen == 0 || body_len == 0) return NULL;

    char decoded_key[256];
    const char *end = body + body_len;
    const char *p = body;

    while (p < end) {
        const char *amp = (const char *)memchr(p, '&', (size_t)(end - p));
        const char *pair_end = amp ? amp : end;
        if (pair_end == p) {
            p = amp ? amp + 1 : end;
            continue;
        }
        /* Go 1.17+ ParseQuery: a raw semicolon is not a value octet. Skip
         * that pair and keep scanning, matching FormValue-after-error. */
        if (memchr(p, ';', (size_t)(pair_end - p))) {
            p = amp ? amp + 1 : end;
            continue;
        }

        const char *eq = (const char *)memchr(p, '=', (size_t)(pair_end - p));
        const char *key_end = eq ? eq : pair_end;
        const char *val = eq ? eq + 1 : pair_end;
        if (http_form_unescape_span(p, (size_t)(key_end - p),
                                    decoded_key, sizeof(decoded_key)) < 0 ||
            strcmp(decoded_key, key) != 0) {
            p = amp ? amp + 1 : end;
            continue;
        }
        if (http_form_unescape_span(val, (size_t)(pair_end - val),
                                    buf, buflen) < 0) {
            p = amp ? amp + 1 : end;
            continue;
        }
        return buf;
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

        if (w->head_request &&
            (fsize > 1024 * 1024 || !w->gzip_enabled ||
             !w->accepts_gzip || (size_t)fsize < w->gzip_min_size)) {
            w->has_content_length_override = 1;
            w->content_length_override = (size_t)fsize;
            fclose(f);
            return;
        }

        if (fsize <= 1024 * 1024) {
            if (fsize == 0) {
                fclose(f);
                return;
            }
            char *data = (char *)malloc((size_t)fsize);
            if (!data) {
                fclose(f);
                neverc_http_set_status(w, 500);
                neverc_http_write_string(w, "500 Out of memory\n");
                return;
            }
            size_t nread = fread(data, 1, (size_t)fsize, f);
            fclose(f);
            if (nread != (size_t)fsize ||
                neverc_http_write(w, data, nread) < 0) {
                nc_buf_reset(&w->body);
                neverc_http_set_status(w, 500);
                (void)neverc_http_write_string(
                    w, "500 Internal Server Error\n");
            }
            free(data);
        } else {
#if NC_HAS_SENDFILE
            /* Direct sendfile is only safe for a blocking plain socket. Reactor
             * and TLS writers must use the configured transport abstraction. */
            if (!w->transport_write && w->fd != NC_INVALID_SOCK) {
                w->has_content_length_override = 1;
                w->content_length_override = (size_t)fsize;
                if (rw_flush(w) != 0) {
                    fclose(f);
                    return;
                }

                int file_fd = fileno(f);
                off_t offset = 0;
                size_t remaining = (size_t)fsize;
                while (remaining > 0) {
                    ssize_t sent =
                        nc_sendfile(w->fd, file_fd, &offset, remaining);
                    if (sent <= 0) break;
                    remaining -= (size_t)sent;
                }
                if (remaining > 0) w->keep_alive = 0;
                fclose(f);
                return;
            }
#endif
            neverc_http_enable_chunked(w);
            char buf[65536];
            size_t nread;
            int stream_failed = 0;
            while ((nread = fread(buf, 1, sizeof(buf), f)) > 0) {
                if (neverc_http_write(w, buf, nread) < 0) {
                    stream_failed = 1;
                    break;
                }
            }
            if (ferror(f)) stream_failed = 1;
            fclose(f);
            if (stream_failed) {
                /* Suppress the normal response finalizer: a zero-size chunk
                 * would falsely authenticate a partial file as complete. */
                w->chunked_ended = 1;
                w->aborted = 1;
                w->keep_alive = 0;
            } else if (neverc_http_end_chunked(w) != 0) {
                w->keep_alive = 0;
            }
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
