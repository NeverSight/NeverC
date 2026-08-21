/*
 * NeverC HTTP/2 Server — Frame handling, stream state machine, server loop.
 * Split from http2.c (HPACK) to avoid large-TU compiler issue.
 */
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/thread.h"
#include "../_http_internal.h"
#include "../../_net_thread.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef NC_H2_REALLOC
#define NC_H2_REALLOC realloc
#endif

/* RFC 9113 §10.5.1: bound CONTINUATION frames per header block. */
#define H2_MAX_CONTINUATION_FRAMES 128

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int ssize_t;
#else
  #include <errno.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  #ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
  #endif
#endif

/* Match neverc_h2_listen_and_serve: a stalled peer must not block forever. */
#define H2_IO_TIMEOUT_MS 30000

/* ======================================================================
 * HTTP/2 Frame Header Parse/Write
 * ====================================================================== */

int neverc_h2_frame_header_read(const uint8_t *data, size_t len,
                                  neverc_h2_frame_header_t *hdr) {
    if (!data || !hdr || len < NC_H2_FRAME_HEADER_SIZE) return -1;
    hdr->length = ((uint32_t)data[0] << 16) |
                  ((uint32_t)data[1] << 8) |
                  (uint32_t)data[2];
    hdr->type = data[3];
    hdr->flags = data[4];
    hdr->stream_id = ((uint32_t)(data[5] & 0x7f) << 24) |
                     ((uint32_t)data[6] << 16) |
                     ((uint32_t)data[7] << 8) |
                     (uint32_t)data[8];
    return 0;
}

int neverc_h2_frame_header_write(const neverc_h2_frame_header_t *hdr,
                                   uint8_t *out) {
    if (!hdr || !out || hdr->length > 0x00ffffffu ||
        hdr->stream_id > 0x7fffffffu)
        return -1;
    out[0] = (uint8_t)(hdr->length >> 16);
    out[1] = (uint8_t)(hdr->length >> 8);
    out[2] = (uint8_t)(hdr->length);
    out[3] = hdr->type;
    out[4] = hdr->flags;
    out[5] = (uint8_t)((hdr->stream_id >> 24) & 0x7f);
    out[6] = (uint8_t)(hdr->stream_id >> 16);
    out[7] = (uint8_t)(hdr->stream_id >> 8);
    out[8] = (uint8_t)(hdr->stream_id);
    return 0;
}

/* ======================================================================
 * HTTP/2 Settings
 * ====================================================================== */

void neverc_h2_settings_init(neverc_h2_settings_t *s) {
    if (!s) return;
    s->header_table_size = NC_H2_DEFAULT_HEADER_TABLE_SIZE;
    s->enable_push = 1;
    s->max_concurrent_streams = NC_H2_DEFAULT_MAX_CONCURRENT;
    s->initial_window_size = NC_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    s->max_frame_size = NC_H2_DEFAULT_MAX_FRAME_SIZE;
    s->max_header_list_size = NC_H2_DEFAULT_MAX_HEADER_LIST_SIZE;
}

typedef struct h2_conn h2_conn_t;
#define H2_CLOSED_STREAM_HISTORY 256

typedef struct {
    uint32_t id;
    int reset;
} h2_closed_stream_t;

typedef struct h2_inbound_chunk {
    size_t length;
    size_t offset;
    uint8_t data[];
} h2_inbound_chunk_t;

struct neverc_h2_server {
    neverc_http_mux_t    *mux;
    neverc_h2_settings_t  settings;
    size_t                 max_body_size;
    int                    handler_timeout_ms;
    char                  *alt_svc;
    neverc_thread_executor_t *handler_executor;
    neverc_thread_executor_t *connection_executor;
    nc_mutex_t              lifecycle_lock;
    nc_cond_t               lifecycle_changed;
    volatile int            serving;
    volatile int            running;
    volatile int            destroying;
    size_t                  active_connections;
    size_t                  pending_connection_tasks;
    neverc_context_t       *serve_context;
    neverc_context_cancel_handle_t *serve_cancel;
    neverc_tcp_listener_t  *listener;
    neverc_tls_config_t    *tls_config;
    h2_conn_t              *connections;
};

static neverc_h2_server_t *g_legacy_h2_server;

neverc_h2_server_t *neverc_h2_server_create(neverc_http_mux_t *mux) {
    neverc_h2_server_t *srv = (neverc_h2_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    srv->mux = mux;
    neverc_h2_settings_init(&srv->settings);
    srv->max_body_size = 10U * 1024U * 1024U;
    nc_mutex_init(&srv->lifecycle_lock);
    nc_cond_init(&srv->lifecycle_changed);
    srv->handler_executor = neverc_thread_executor_create(8, 1024);
    if (!srv->handler_executor) {
        nc_cond_destroy(&srv->lifecycle_changed);
        nc_mutex_destroy(&srv->lifecycle_lock);
        free(srv);
        return NULL;
    }
    return srv;
}

void neverc_h2_server_destroy(neverc_h2_server_t *srv) {
    if (!srv) return;
    nc_atomic_store(&srv->destroying, 1);
    neverc_h2_server_shutdown(srv);
    if (srv->connection_executor) {
        neverc_thread_executor_shutdown(srv->connection_executor);
        neverc_thread_executor_free(srv->connection_executor);
    }
    nc_mutex_lock(&srv->lifecycle_lock);
    while (srv->serving || srv->active_connections > 0 ||
           srv->pending_connection_tasks > 0)
        nc_cond_wait(&srv->lifecycle_changed, &srv->lifecycle_lock);
    nc_mutex_unlock(&srv->lifecycle_lock);
    neverc_thread_executor_shutdown(srv->handler_executor);
    neverc_thread_executor_free(srv->handler_executor);
    neverc_tls_config_free(srv->tls_config);
    free(srv->alt_svc);
    if (srv->serve_cancel)
        neverc_context_cancel_handle_free(srv->serve_cancel);
    if (srv->serve_context)
        neverc_context_free(srv->serve_context);
    nc_cond_destroy(&srv->lifecycle_changed);
    nc_mutex_destroy(&srv->lifecycle_lock);
    free(srv);
}

void neverc_h2_server_set_max_streams(neverc_h2_server_t *srv, uint32_t max) {
    srv->settings.max_concurrent_streams = max;
}

void neverc_h2_server_set_max_frame_size(neverc_h2_server_t *srv, uint32_t max) {
    if (max >= NC_H2_DEFAULT_MAX_FRAME_SIZE && max <= NC_H2_MAX_FRAME_SIZE_LIMIT)
        srv->settings.max_frame_size = max;
}

void neverc_h2_server_set_initial_window_size(neverc_h2_server_t *srv, uint32_t win) {
    if (win <= 0x7fffffff)
        srv->settings.initial_window_size = win;
}

void neverc_h2_server_set_max_header_list_size(neverc_h2_server_t *srv, uint32_t max) {
    srv->settings.max_header_list_size = max;
}

void neverc_h2_server_set_max_body_size(neverc_h2_server_t *srv, size_t max) {
    if (srv && max > 0) srv->max_body_size = max;
}

void neverc_h2_server_set_handler_timeout(neverc_h2_server_t *srv, int ms) {
    if (srv && ms >= 0) srv->handler_timeout_ms = ms;
}

void neverc_h2_server_set_alt_svc(neverc_h2_server_t *srv,
                                  const char *value) {
    if (!srv || nc_atomic_load(&srv->running)) return;
    char *copy = value ? strdup(value) : NULL;
    if (value && !copy) return;
    free(srv->alt_svc);
    srv->alt_svc = copy;
}

/* ======================================================================
 * HTTP/2 Stream State Machine (RFC 9113 §5.1)
 * ====================================================================== */

typedef enum {
    H2_STREAM_IDLE,
    H2_STREAM_OPEN,
    H2_STREAM_HALF_CLOSED_REMOTE,
    H2_STREAM_HALF_CLOSED_LOCAL,
    H2_STREAM_CLOSED
} h2_stream_state_t;

typedef struct h2_stream {
    h2_conn_t          *conn;
    uint32_t           id;
    h2_stream_state_t  state;
    int32_t            recv_window;
    int32_t            send_window;

    neverc_hpack_header_t headers[64];
    int                nheaders;
    uint8_t           *body;
    size_t             body_len;
    size_t             body_cap;
    int64_t            content_length;
    int                headers_complete;
    neverc_context_t  *context;
    neverc_context_t  *context_background;
    neverc_context_cancel_handle_t *cancel;
    int                handler_active;
    volatile int       handler_done;
    int                counted_active;
    volatile int       reset;
    int                streaming_request;
    volatile int       remote_ended;
    neverc_thread_channel_t *receive_queue;
    h2_inbound_chunk_t *receive_current;

    struct h2_stream  *next;
} h2_stream_t;

/* ======================================================================
 * HTTP/2 Connection State
 * ====================================================================== */

typedef enum {
    H2_IO_SOCKET,
    H2_IO_TCP,
    H2_IO_TLS
} h2_io_kind_t;

typedef struct {
    h2_io_kind_t         kind;
    nc_sock_t            fd;
    neverc_tcp_conn_t   *tcp;
    neverc_tls_conn_t   *tls;
} h2_io_t;

struct h2_conn {
    h2_io_t                  io;
    neverc_h2_server_t      *srv;
    neverc_hpack_decoder_t  *hpack_dec;
    neverc_hpack_encoder_t  *hpack_enc;

    neverc_h2_settings_t     local_settings;
    neverc_h2_settings_t     peer_settings;

    int32_t  conn_recv_window;
    int32_t  conn_send_window;
    uint32_t last_stream_id;
    uint32_t max_stream_id;
    volatile int goaway_sent;
    uint32_t goaway_error_code;
    int      peer_goaway_received;
    int      initial_settings_received;
    volatile int running;
    nc_mutex_t state_lock;
    nc_mutex_t write_lock;
    nc_cond_t window_changed;
    nc_cond_t handlers_done;
    size_t active_handlers;
    h2_conn_t *server_next;
    h2_conn_t *server_prev;

    h2_closed_stream_t closed_streams[H2_CLOSED_STREAM_HISTORY];
    size_t closed_stream_count;
    size_t closed_stream_next;

    h2_stream_t *streams;
    volatile int  active_streams;

    uint8_t  *pending_hdr_block;
    size_t    pending_hdr_len;
    size_t    pending_hdr_cap;
    uint32_t  pending_hdr_stream_id;
    int       pending_hdr_active;
    int       pending_hdr_discard;
    uint32_t  pending_hdr_rst_code;
    int       pending_end_stream;
    int       pending_hdr_continuations;
    uint32_t  settings_ack_owed;
};

static void h2_io_init_socket(h2_io_t *io, nc_sock_t fd) {
    io->kind = H2_IO_SOCKET;
    io->fd = fd;
    io->tcp = NULL;
    io->tls = NULL;
}

static void h2_io_init_tcp(h2_io_t *io, neverc_tcp_conn_t *tcp) {
    io->kind = H2_IO_TCP;
    io->tcp = tcp;
    io->fd = -1;
    io->tls = NULL;
}

static void h2_io_init_tls(h2_io_t *io, neverc_tls_conn_t *tls) {
    io->kind = H2_IO_TLS;
    io->tls = tls;
    io->fd = -1;
    io->tcp = NULL;
}

static void h2_io_set_timeout(h2_io_t *io, int ms) {
    if (!io || ms < 0) return;
    if (io->kind == H2_IO_TCP) {
        (void)neverc_tcp_set_timeout(io->tcp, ms);
        return;
    }
    if (io->kind != H2_IO_SOCKET) return;
#ifdef _WIN32
    DWORD timeout = (DWORD)ms;
    (void)setsockopt(io->fd, SOL_SOCKET, SO_RCVTIMEO,
                     (const char *)&timeout, sizeof(timeout));
    (void)setsockopt(io->fd, SOL_SOCKET, SO_SNDTIMEO,
                     (const char *)&timeout, sizeof(timeout));
#else
    struct timeval timeout = {
        .tv_sec = ms / 1000,
        .tv_usec = (ms % 1000) * 1000
    };
    (void)setsockopt(io->fd, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, sizeof(timeout));
    (void)setsockopt(io->fd, SOL_SOCKET, SO_SNDTIMEO,
                     &timeout, sizeof(timeout));
#endif
}

static int h2_io_read_all(h2_io_t *io, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t got = 0;
    while (got < len) {
        int n = -1;
        switch (io->kind) {
        case H2_IO_SOCKET:
#ifdef _WIN32
            n = recv(io->fd, p + got, (int)(len - got), 0);
            if (n < 0 && WSAGetLastError() == WSAEINTR)
                continue;
#else
            n = (int)recv(io->fd, p + got, len - got, 0);
            if (n < 0 && errno == EINTR)
                continue;
#endif
            break;
        case H2_IO_TCP:
            n = neverc_tcp_read(io->tcp, p + got, len - got);
            break;
        case H2_IO_TLS:
            n = neverc_tls_read(io->tls, p + got, len - got);
            break;
        }
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

static int h2_io_read_some(h2_io_t *io, void *buf, size_t len) {
    switch (io->kind) {
    case H2_IO_SOCKET:
#ifdef _WIN32
        return recv(io->fd, (char *)buf, (int)len, 0);
#else
        return (int)recv(io->fd, buf, len, 0);
#endif
    case H2_IO_TCP:
        return neverc_tcp_read(io->tcp, buf, len);
    case H2_IO_TLS:
        return neverc_tls_read(io->tls, buf, len);
    }
    return -1;
}

static int h2_io_discard(h2_io_t *io, size_t len) {
    uint8_t buffer[4096];
    while (len > 0) {
        size_t chunk = len < sizeof(buffer) ? len : sizeof(buffer);
        if (h2_io_read_all(io, buffer, chunk) != 0) return -1;
        len -= chunk;
    }
    return 0;
}

static int h2_io_write_all(h2_io_t *io, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        int n = -1;
        switch (io->kind) {
        case H2_IO_SOCKET:
#ifdef _WIN32
            n = send(io->fd, p + sent, (int)(len - sent), 0);
            if (n < 0 && WSAGetLastError() == WSAEINTR)
                continue;
#else
            n = (int)send(io->fd, p + sent, len - sent, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR)
                continue;
#endif
            break;
        case H2_IO_TCP:
            n = neverc_tcp_write(io->tcp, p + sent, len - sent);
            break;
        case H2_IO_TLS:
            n = neverc_tls_write(io->tls, p + sent, len - sent);
            break;
        }
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void h2_io_shutdown(h2_io_t *io) {
    switch (io->kind) {
    case H2_IO_SOCKET:
#ifdef _WIN32
        (void)shutdown((SOCKET)io->fd, SD_BOTH);
#else
        (void)shutdown(io->fd, SHUT_RDWR);
#endif
        break;
    case H2_IO_TCP:
        (void)neverc_tcp_shutdown_read(io->tcp);
        (void)neverc_tcp_shutdown_write(io->tcp);
        break;
    case H2_IO_TLS:
        (void)neverc_tls_shutdown_read(io->tls);
        (void)neverc_tls_shutdown_write(io->tls);
        break;
    }
}

static void h2_io_shutdown_write(h2_io_t *io) {
    switch (io->kind) {
    case H2_IO_SOCKET:
#ifdef _WIN32
        (void)shutdown((SOCKET)io->fd, SD_SEND);
#else
        (void)shutdown(io->fd, SHUT_WR);
#endif
        break;
    case H2_IO_TCP:
        (void)neverc_tcp_shutdown_write(io->tcp);
        break;
    case H2_IO_TLS:
        (void)neverc_tls_shutdown_write(io->tls);
        break;
    }
}

static void h2_io_drain_after_error(h2_io_t *io) {
    uint8_t buffer[4096];
    size_t drained = 0;
    const size_t maximum = 64U * 1024U;

    h2_io_shutdown_write(io);
    if (io->kind == H2_IO_TLS) return;
    if (io->kind == H2_IO_TCP) {
        (void)neverc_tcp_set_read_timeout(io->tcp, 100);
    } else {
#ifdef _WIN32
        DWORD timeout = 100;
        (void)setsockopt(io->fd, SOL_SOCKET, SO_RCVTIMEO,
                         (const char *)&timeout, sizeof(timeout));
#else
        struct timeval timeout = { .tv_sec = 0, .tv_usec = 100000 };
        (void)setsockopt(io->fd, SOL_SOCKET, SO_RCVTIMEO,
                         &timeout, sizeof(timeout));
#endif
    }
    while (drained < maximum) {
        size_t capacity = maximum - drained;
        if (capacity > sizeof(buffer)) capacity = sizeof(buffer);
        int count = h2_io_read_some(io, buffer, capacity);
        if (count <= 0) break;
        drained += (size_t)count;
    }
}

static int h2_write_frame(h2_io_t *io, uint8_t type, uint8_t flags,
                            uint32_t stream_id, const void *payload, uint32_t len) {
    if (len > 0 && !payload) return -1;
    neverc_h2_frame_header_t hdr = { .length = len, .type = type,
                                      .flags = flags, .stream_id = stream_id };
    uint8_t hbuf[NC_H2_FRAME_HEADER_SIZE];
    if (neverc_h2_frame_header_write(&hdr, hbuf) != 0) return -1;
    if (h2_io_write_all(io, hbuf, NC_H2_FRAME_HEADER_SIZE) != 0) return -1;
    if (len > 0)
        return h2_io_write_all(io, payload, len);
    return 0;
}

static int h2_write_settings(h2_io_t *io, const neverc_h2_settings_t *s) {
    uint8_t payload[6 * 6];
    int pos = 0;

    #define WRITE_SETTING(id, val) do { \
        payload[pos++] = (uint8_t)((id) >> 8); \
        payload[pos++] = (uint8_t)(id); \
        payload[pos++] = (uint8_t)((val) >> 24); \
        payload[pos++] = (uint8_t)((val) >> 16); \
        payload[pos++] = (uint8_t)((val) >> 8); \
        payload[pos++] = (uint8_t)(val); \
    } while(0)

    WRITE_SETTING(NC_H2_SETTINGS_HEADER_TABLE_SIZE, s->header_table_size);
    WRITE_SETTING(NC_H2_SETTINGS_MAX_CONCURRENT_STREAMS, s->max_concurrent_streams);
    WRITE_SETTING(NC_H2_SETTINGS_INITIAL_WINDOW_SIZE, s->initial_window_size);
    WRITE_SETTING(NC_H2_SETTINGS_MAX_FRAME_SIZE, s->max_frame_size);
    WRITE_SETTING(NC_H2_SETTINGS_MAX_HEADER_LIST_SIZE, s->max_header_list_size);
    if (!s->enable_push) {
        WRITE_SETTING(NC_H2_SETTINGS_ENABLE_PUSH, 0);
    }
    #undef WRITE_SETTING

    return h2_write_frame(io, NC_H2_FRAME_SETTINGS, 0, 0,
                           payload, (uint32_t)(s->enable_push ? 30 : 36));
}

static int h2_write_settings_ack(h2_io_t *io) {
    return h2_write_frame(io, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, NULL, 0);
}

static int h2_write_ping_ack(h2_io_t *io, const uint8_t *data) {
    return h2_write_frame(io, NC_H2_FRAME_PING, NC_H2_FLAG_ACK, 0, data, 8);
}

static int h2_write_goaway(h2_io_t *io, uint32_t last_stream, uint32_t error_code) {
    uint8_t payload[8];
    payload[0] = (uint8_t)(last_stream >> 24);
    payload[1] = (uint8_t)(last_stream >> 16);
    payload[2] = (uint8_t)(last_stream >> 8);
    payload[3] = (uint8_t)(last_stream);
    payload[4] = (uint8_t)(error_code >> 24);
    payload[5] = (uint8_t)(error_code >> 16);
    payload[6] = (uint8_t)(error_code >> 8);
    payload[7] = (uint8_t)(error_code);
    return h2_write_frame(io, NC_H2_FRAME_GOAWAY, 0, 0, payload, 8);
}

static int h2_write_rst_stream(h2_io_t *io, uint32_t stream_id, uint32_t error_code) {
    uint8_t payload[4];
    payload[0] = (uint8_t)(error_code >> 24);
    payload[1] = (uint8_t)(error_code >> 16);
    payload[2] = (uint8_t)(error_code >> 8);
    payload[3] = (uint8_t)(error_code);
    return h2_write_frame(io, NC_H2_FRAME_RST_STREAM, 0, stream_id, payload, 4);
}

static int h2_write_window_update(h2_io_t *io, uint32_t stream_id, uint32_t increment) {
    /* RFC 9113 §6.9: a WINDOW_UPDATE increment of 0 is a protocol error. */
    if (increment == 0) return 0;
    if (increment > 0x7fffffffu) return -1;
    uint8_t payload[4];
    payload[0] = (uint8_t)((increment >> 24) & 0x7f);
    payload[1] = (uint8_t)(increment >> 16);
    payload[2] = (uint8_t)(increment >> 8);
    payload[3] = (uint8_t)(increment);
    return h2_write_frame(io, NC_H2_FRAME_WINDOW_UPDATE, 0, stream_id, payload, 4);
}

static int h2_conn_write_frame(h2_conn_t *conn, uint8_t type, uint8_t flags,
                               uint32_t stream_id, const void *payload,
                               uint32_t length) {
    nc_mutex_lock(&conn->write_lock);
    int result = h2_write_frame(&conn->io, type, flags, stream_id,
                                payload, length);
    nc_mutex_unlock(&conn->write_lock);
    return result;
}

static int h2_conn_write_rst(h2_conn_t *conn, uint32_t stream_id,
                             uint32_t error_code) {
    nc_mutex_lock(&conn->write_lock);
    int result = h2_write_rst_stream(&conn->io, stream_id, error_code);
    nc_mutex_unlock(&conn->write_lock);
    return result;
}

static int h2_conn_write_window_update(h2_conn_t *conn, uint32_t stream_id,
                                       uint32_t increment) {
    nc_mutex_lock(&conn->write_lock);
    int result = h2_write_window_update(&conn->io, stream_id, increment);
    nc_mutex_unlock(&conn->write_lock);
    return result;
}

/* RFC 9113 §5.1: discarded DATA (closed / reset / rejected streams) still
 * counts toward the connection window. Restore it and WINDOW_UPDATE so an
 * honest sender is not stalled at 0. Caller must already have subtracted
 * `length` from conn_recv_window. */
static void h2_refund_connection_window(h2_conn_t *conn, uint32_t length) {
    if (!conn || length == 0) return;
    nc_mutex_lock(&conn->state_lock);
    if ((int64_t)conn->conn_recv_window + (int64_t)length <= INT32_MAX)
        conn->conn_recv_window += (int32_t)length;
    nc_mutex_unlock(&conn->state_lock);
    (void)h2_conn_write_window_update(conn, 0, length);
}

static int h2_conn_write_settings_ack(h2_conn_t *conn) {
    nc_mutex_lock(&conn->write_lock);
    int result = h2_write_settings_ack(&conn->io);
    nc_mutex_unlock(&conn->write_lock);
    return result;
}

static int h2_conn_write_ping_ack(h2_conn_t *conn, const uint8_t *payload) {
    nc_mutex_lock(&conn->write_lock);
    int result = h2_write_ping_ack(&conn->io, payload);
    nc_mutex_unlock(&conn->write_lock);
    return result;
}

static int h2_conn_write_goaway(h2_conn_t *conn, uint32_t error_code) {
    nc_mutex_lock(&conn->write_lock);
    int result = 0;
    if (!nc_atomic_load(&conn->goaway_sent)) {
        result = h2_write_goaway(&conn->io, conn->last_stream_id,
                                 error_code);
        conn->goaway_error_code = error_code;
        nc_atomic_store(&conn->goaway_sent, 1);
    }
    nc_mutex_unlock(&conn->write_lock);
    return result;
}

static int h2_server_register_connection(neverc_h2_server_t *server,
                                         h2_conn_t *connection) {
    nc_mutex_lock(&server->lifecycle_lock);
    if (nc_atomic_load(&server->destroying) ||
        (nc_atomic_load(&server->serving) &&
         !nc_atomic_load(&server->running))) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return -1;
    }
    connection->server_next = server->connections;
    connection->server_prev = NULL;
    if (server->connections)
        server->connections->server_prev = connection;
    server->connections = connection;
    server->active_connections++;
    nc_cond_broadcast(&server->lifecycle_changed);
    nc_mutex_unlock(&server->lifecycle_lock);
    return 0;
}

static void h2_server_remove_connection(neverc_h2_server_t *server,
                                        h2_conn_t *connection) {
    nc_mutex_lock(&server->lifecycle_lock);
    if (connection->server_prev)
        connection->server_prev->server_next = connection->server_next;
    else if (server->connections == connection)
        server->connections = connection->server_next;
    if (connection->server_next)
        connection->server_next->server_prev = connection->server_prev;
    connection->server_next = NULL;
    connection->server_prev = NULL;
    if (server->active_connections > 0)
        server->active_connections--;
    nc_cond_broadcast(&server->lifecycle_changed);
    nc_mutex_unlock(&server->lifecycle_lock);
}

size_t neverc_h2_server_active_connections(neverc_h2_server_t *server) {
    if (!server) return 0;
    nc_mutex_lock(&server->lifecycle_lock);
    size_t result = server->active_connections;
    nc_mutex_unlock(&server->lifecycle_lock);
    return result;
}

void neverc_h2_server_shutdown(neverc_h2_server_t *server) {
    if (!server) return;
    nc_atomic_store(&server->running, 0);
    nc_mutex_lock(&server->lifecycle_lock);
    if (server->serve_cancel)
        neverc_context_cancel_handle_cancel(server->serve_cancel);
    for (h2_conn_t *connection = server->connections; connection;
         connection = connection->server_next) {
        (void)h2_conn_write_goaway(connection, NC_H2_NO_ERROR);
        if (nc_atomic_load(&connection->active_streams) == 0)
            h2_io_shutdown(&connection->io);
    }
    nc_cond_broadcast(&server->lifecycle_changed);
    nc_mutex_unlock(&server->lifecycle_lock);
}

int neverc_h2_request_stream_read(void *protocol_stream,
                                   neverc_context_t *context,
                                   void *output, size_t output_capacity) {
    h2_stream_t *stream = (h2_stream_t *)protocol_stream;
    if (!stream || !stream->streaming_request || !stream->receive_queue ||
        !output || output_capacity == 0 ||
        nc_atomic_load(&stream->reset))
        return -1;
    while (!stream->receive_current) {
        void *value = NULL;
        int received = neverc_thread_channel_receive_context(
            stream->receive_queue, context, &value);
        if (received == NEVERC_THREAD_CLOSED)
            return nc_atomic_load(&stream->remote_ended) ? 0 : -1;
        if (received != NEVERC_THREAD_OK || !value) return -1;
        stream->receive_current = (h2_inbound_chunk_t *)value;
    }
    h2_inbound_chunk_t *chunk = stream->receive_current;
    size_t count = chunk->length - chunk->offset;
    if (count > output_capacity) count = output_capacity;
    memcpy(output, chunk->data + chunk->offset, count);
    chunk->offset += count;
    if (chunk->offset == chunk->length) {
        free(chunk);
        stream->receive_current = NULL;
    }
    h2_conn_t *connection = stream->conn;
    nc_mutex_lock(&connection->state_lock);
    if ((int64_t)stream->recv_window + count <= INT32_MAX)
        stream->recv_window += (int32_t)count;
    nc_mutex_unlock(&connection->state_lock);
    if (count > 0 && h2_conn_write_window_update(
            connection, stream->id, (uint32_t)count) != 0)
        return -1;
    return (int)count;
}

void neverc_h2_request_stream_cancel(void *protocol_stream,
                                      uint32_t error_code) {
    h2_stream_t *stream = (h2_stream_t *)protocol_stream;
    if (!stream || !stream->conn || nc_atomic_load(&stream->reset)) return;
    nc_atomic_store(&stream->reset, 1);
    if (stream->cancel)
        neverc_context_cancel_handle_cancel(stream->cancel);
    if (stream->receive_queue)
        (void)neverc_thread_channel_close(stream->receive_queue);
    (void)h2_conn_write_rst(stream->conn, stream->id,
                            error_code ? error_code : NC_H2_CANCEL);
}

static void h2_clear_pending_hdr(h2_conn_t *conn);
static int h2_append_hdr_block(h2_conn_t *conn, const uint8_t *data,
                               size_t len);

static uint32_t h2_frame_header_error(const neverc_h2_frame_header_t *header) {
    switch (header->type) {
    case NC_H2_FRAME_DATA:
        if (header->stream_id == 0) return NC_H2_PROTOCOL_ERROR;
        break;
    case NC_H2_FRAME_HEADERS:
        if (header->stream_id == 0) return NC_H2_PROTOCOL_ERROR;
        break;
    case NC_H2_FRAME_PRIORITY:
        if (header->stream_id == 0) return NC_H2_PROTOCOL_ERROR;
        if (header->length != 5) return NC_H2_FRAME_SIZE_ERROR;
        break;
    case NC_H2_FRAME_RST_STREAM:
        if (header->stream_id == 0) return NC_H2_PROTOCOL_ERROR;
        if (header->length != 4) return NC_H2_FRAME_SIZE_ERROR;
        break;
    case NC_H2_FRAME_SETTINGS:
        if (header->stream_id != 0) return NC_H2_PROTOCOL_ERROR;
        if ((header->flags & NC_H2_FLAG_ACK) != 0 && header->length != 0)
            return NC_H2_FRAME_SIZE_ERROR;
        if ((header->flags & NC_H2_FLAG_ACK) == 0 &&
            header->length % 6 != 0)
            return NC_H2_FRAME_SIZE_ERROR;
        break;
    case NC_H2_FRAME_PUSH_PROMISE:
        return NC_H2_PROTOCOL_ERROR; /* clients cannot push */
    case NC_H2_FRAME_PING:
        if (header->stream_id != 0) return NC_H2_PROTOCOL_ERROR;
        if (header->length != 8) return NC_H2_FRAME_SIZE_ERROR;
        break;
    case NC_H2_FRAME_GOAWAY:
        if (header->stream_id != 0) return NC_H2_PROTOCOL_ERROR;
        if (header->length < 8) return NC_H2_FRAME_SIZE_ERROR;
        break;
    case NC_H2_FRAME_WINDOW_UPDATE:
        if (header->length != 4) return NC_H2_FRAME_SIZE_ERROR;
        break;
    case NC_H2_FRAME_CONTINUATION:
        if (header->stream_id == 0) return NC_H2_PROTOCOL_ERROR;
        break;
    default:
        return NC_H2_NO_ERROR; /* unknown frame types are ignored */
    }
    /* RFC 9113 requires recipients to ignore undefined frame flags. */
    return NC_H2_NO_ERROR;
}

static int h2_header_fragment(const neverc_h2_frame_header_t *header,
                              const uint8_t *payload,
                              const uint8_t **fragment,
                              size_t *fragment_length) {
    if (!header || !fragment || !fragment_length ||
        (header->length != 0 && !payload))
        return -1;
    size_t offset = 0;
    size_t padding = 0;
    if ((header->flags & NC_H2_FLAG_PADDED) != 0) {
        if (header->length == 0) return -1;
        padding = payload[offset++];
    }
    int self_dep = 0;
    if ((header->flags & NC_H2_FLAG_PRIORITY) != 0) {
        if ((size_t)header->length - offset < 5) return -1;
        uint32_t dependency = ((uint32_t)(payload[offset] & 0x7f) << 24) |
                              ((uint32_t)payload[offset + 1] << 16) |
                              ((uint32_t)payload[offset + 2] << 8) |
                              (uint32_t)payload[offset + 3];
        if (dependency == header->stream_id)
            self_dep = 1;
        offset += 5;
    }
    if (padding > (size_t)header->length - offset) return -1;
    *fragment_length = (size_t)header->length - offset - padding;
    *fragment = payload ? payload + offset : NULL;
    return self_dep ? -2 : 0;
}

static int h2_decode_header_block_discard(h2_conn_t *conn,
                                          const uint8_t *block,
                                          size_t block_len) {
    neverc_hpack_header_t headers[64];
    memset(headers, 0, sizeof(headers));
    int count = 0;
    int rc = neverc_hpack_decode(conn->hpack_dec, block, block_len,
                                 headers, 64, &count);
    for (int i = 0; i < count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    return rc;
}

static int h2_finish_discarded_header_block(h2_conn_t *conn) {
    int rc = h2_decode_header_block_discard(conn, conn->pending_hdr_block,
                                            conn->pending_hdr_len);
    uint32_t stream_id = conn->pending_hdr_stream_id;
    uint32_t rst_code = conn->pending_hdr_rst_code;
    h2_clear_pending_hdr(conn);
    if (rc < 0) {
        (void)h2_conn_write_goaway(conn, NC_H2_COMPRESSION_ERROR);
        return -1;
    }
    /* rst_code 0 means RST_STREAM was already sent (e.g. self-dependency
     * abort) while CONTINUATION still had to keep HPACK in sync. */
    if (rst_code != 0)
        (void)h2_conn_write_rst(conn, stream_id, rst_code);
    return 0;
}

static int h2_reject_headers_keep_hpack(h2_conn_t *conn,
                                        const neverc_h2_frame_header_t *header,
                                        const uint8_t *payload,
                                        uint32_t rst_code) {
    const uint8_t *fragment = NULL;
    size_t fragment_length = 0;
    int fragment_rc = h2_header_fragment(header, payload, &fragment,
                                         &fragment_length);
    if (fragment_rc != 0 && fragment_rc != -2) {
        (void)h2_conn_write_goaway(conn, NC_H2_PROTOCOL_ERROR);
        return -1;
    }
    /* RFC 9113 §4.3: the header block is still decoded so HPACK stays in
     * sync. Self-dependency (§6.2) is a stream PROTOCOL_ERROR. */
    if (fragment_rc == -2)
        rst_code = NC_H2_PROTOCOL_ERROR;
    /* RFC 9113 §4.3: a refused or closed stream still consumes the header
     * block so HPACK stays in sync. Split HEADERS+CONTINUATION must be
     * buffered, not treated as COMPRESSION_ERROR. */
    if ((header->flags & NC_H2_FLAG_END_HEADERS) == 0) {
        conn->pending_hdr_stream_id = header->stream_id;
        conn->pending_hdr_active = 1;
        conn->pending_hdr_discard = 1;
        conn->pending_hdr_rst_code = rst_code;
        conn->pending_end_stream =
            (header->flags & NC_H2_FLAG_END_STREAM) != 0;
        conn->pending_hdr_continuations = 0;
        if (h2_append_hdr_block(conn, fragment, fragment_length) != 0) {
            h2_clear_pending_hdr(conn);
            (void)h2_conn_write_goaway(conn, NC_H2_ENHANCE_YOUR_CALM);
            return -1;
        }
        return 0;
    }
    if (h2_decode_header_block_discard(conn, fragment, fragment_length) < 0) {
        (void)h2_conn_write_goaway(conn, NC_H2_COMPRESSION_ERROR);
        return -1;
    }
    (void)h2_conn_write_rst(conn, header->stream_id, rst_code);
    return 0;
}

static int h2_data_fragment(const neverc_h2_frame_header_t *header,
                            const uint8_t *payload, const uint8_t **data,
                            size_t *data_length) {
    if (!header || !data || !data_length ||
        (header->length != 0 && !payload))
        return -1;
    size_t offset = 0;
    size_t padding = 0;
    if ((header->flags & NC_H2_FLAG_PADDED) != 0) {
        if (header->length == 0) return -1;
        padding = payload[offset++];
    }
    if (padding > (size_t)header->length - offset) return -1;
    *data_length = (size_t)header->length - offset - padding;
    *data = payload ? payload + offset : NULL;
    return 0;
}

static void h2_clear_pending_hdr(h2_conn_t *conn) {
    free(conn->pending_hdr_block);
    conn->pending_hdr_block = NULL;
    conn->pending_hdr_len = 0;
    conn->pending_hdr_cap = 0;
    conn->pending_hdr_stream_id = 0;
    conn->pending_hdr_active = 0;
    conn->pending_hdr_discard = 0;
    conn->pending_hdr_rst_code = 0;
    conn->pending_end_stream = 0;
    conn->pending_hdr_continuations = 0;
}

static int h2_buffer_append(uint8_t **buffer, size_t *length,
                            size_t *capacity, const uint8_t *data,
                            size_t data_len) {
    if (!buffer || !length || !capacity || (data_len > 0 && !data))
        return -1;
    if (data_len > SIZE_MAX - *length)
        return -1;

    size_t new_len = *length + data_len;
    if (new_len > *capacity) {
        size_t new_cap = *capacity < 4096 ? 4096 : *capacity;
        while (new_cap < new_len) {
            if (new_cap > SIZE_MAX / 2) {
                new_cap = new_len;
                break;
            }
            new_cap *= 2;
        }

        uint8_t *new_buffer =
            (uint8_t *)NC_H2_REALLOC(*buffer, new_cap);
        if (!new_buffer)
            return -1;
        *buffer = new_buffer;
        *capacity = new_cap;
    }

    if (data_len > 0)
        memcpy(*buffer + *length, data, data_len);
    *length = new_len;
    return 0;
}

static int h2_append_hdr_block(h2_conn_t *conn, const uint8_t *data, size_t len) {
    if (len > conn->local_settings.max_header_list_size ||
        conn->pending_hdr_len >
            conn->local_settings.max_header_list_size - len)
        return -1;
    return h2_buffer_append(&conn->pending_hdr_block,
                            &conn->pending_hdr_len,
                            &conn->pending_hdr_cap, data, len);
}

static h2_stream_t *h2_find_stream(h2_conn_t *conn, uint32_t id);
static h2_stream_t *h2_create_stream(h2_conn_t *conn, uint32_t id);
static void h2_close_stream(h2_conn_t *conn, h2_stream_t *s);
static void h2_remove_stream(h2_conn_t *conn, uint32_t id);
static void h2_dispatch_request(h2_conn_t *conn, h2_stream_t *stream);
static int h2_submit_request(h2_conn_t *conn, h2_stream_t *stream);

static int h2_is_tchar(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
           c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
           c == '^' || c == '_' || c == 0x60 || c == '|' || c == '~';
}

static int h2_name_valid(const char *name) {
    if (!name || !name[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') return 0;
        if (!h2_is_tchar(*p)) return 0;
    }
    return 1;
}

static int h2_token_valid(const char *value) {
    if (!value || !value[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (!h2_is_tchar(*p)) return 0;
    return 1;
}

static int h2_value_valid(const char *value) {
    if (!value) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if ((*p < 0x20 && *p != '\t') || *p == 0x7f) return 0;
    return 1;
}

static int h2_valid_port(const char *s, size_t length) {
    if (!s || length == 0) return 0;
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

/* Same Host byte allowlist as HTTP/1 (Go ValidHostHeader without comma). */
static int h2_host_reg_name_byte(unsigned char c) {
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

/* Same rules as HTTP/1 Host: reject userinfo, paths, commas, bad ports,
 * HTML-special bytes, and unbracketed / unclosed IPv6 so intermediaries
 * cannot desync and dumps cannot XSS. */
static int h2_valid_host(const char *value, size_t length) {
    if (!value || length == 0) return 0;
    if (value[0] == '[') {
        const char *close = (const char *)memchr(value, ']', length);
        if (!close || close == value + 1) return 0;
        size_t inner = (size_t)(close - value - 1);
        int has_colon = 0;
        for (size_t i = 0; i < inner; i++) {
            unsigned char c = (unsigned char)value[1 + i];
            if (c == ':') has_colon = 1;
            else if (!h2_host_reg_name_byte(c))
                return 0;
        }
        if (!has_colon &&
            !(inner > 2 && (value[1] == 'v' || value[1] == 'V')))
            return 0;
        size_t after = length - (size_t)(close - value) - 1;
        if (after == 0) return 1;
        return close[1] == ':' && h2_valid_port(close + 2, after - 1);
    }

    const char *colon = (const char *)memchr(value, ':', length);
    size_t host_length = colon ? (size_t)(colon - value) : length;
    if (host_length == 0) return 0;
    for (size_t i = 0; i < host_length; i++) {
        if (!h2_host_reg_name_byte((unsigned char)value[i]))
            return 0;
    }
    if (!colon) return 1;
    if (memchr(colon + 1, ':', length - host_length - 1)) return 0;
    return h2_valid_port(colon + 1, length - host_length - 1);
}

static int h2_valid_authority(const char *value) {
    if (!value || !*value) return 0;
    return h2_valid_host(value, strlen(value));
}

static int h2_valid_path(const char *method, const char *path) {
    if (!method || !path || !path[0]) return 0;
    if (strcmp(path, "*") == 0)
        return strcmp(method, "OPTIONS") == 0;
    if (path[0] != '/') return 0;
    /* Same origin-form rule as HTTP/1: scheme-relative "//host" and a
     * leading backslash are open-redirect / XSS if reflected into Location.
     * Percent-decoded `/%2f` / `/%5c` are the same leftover. */
    {
        const char *query = strchr(path, '?');
        size_t path_len = query ? (size_t)(query - path) : strlen(path);
        if (neverc_url_path_n_is_protocol_relative(path, path_len))
            return 0;
    }
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p <= 0x20 || *p == 0x7f || *p == '#' || *p == '\\')
            return 0;
    return 1;
}

static int h2_ascii_ieq(const char *left, const char *right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return *left == *right;
}

static int h2_parse_content_length(const char *value, int64_t *result) {
    if (!value || !*value) return -1;
    uint64_t parsed = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p < '0' || *p > '9' ||
            parsed > (uint64_t)INT64_MAX / 10 ||
            parsed * 10 > (uint64_t)INT64_MAX - (uint64_t)(*p - '0'))
            return -1;
        parsed = parsed * 10 + (uint64_t)(*p - '0');
    }
    *result = (int64_t)parsed;
    return 0;
}

static int h2_validate_request_headers(h2_conn_t *conn,
                                       h2_stream_t *stream) {
    const char *method = NULL;
    const char *path = NULL;
    const char *scheme = NULL;
    const char *authority = NULL;
    const char *host = NULL;
    int regular_seen = 0;
    int content_length_seen = 0;
    size_t list_size = 0;
    stream->content_length = -1;
    for (int i = 0; i < stream->nheaders; i++) {
        const char *name = stream->headers[i].name;
        const char *value = stream->headers[i].value;
        size_t name_length = strlen(name);
        size_t value_length = strlen(value);
        if (name_length > SIZE_MAX - value_length - 32 ||
            list_size > SIZE_MAX - name_length - value_length - 32)
            return -1;
        list_size += name_length + value_length + 32;
        if (list_size > conn->local_settings.max_header_list_size ||
            !h2_value_valid(value))
            return -1;
        if (name[0] == ':') {
            if (regular_seen) return -1;
            const char **slot = NULL;
            if (strcmp(name, ":method") == 0) slot = &method;
            else if (strcmp(name, ":path") == 0) slot = &path;
            else if (strcmp(name, ":scheme") == 0) slot = &scheme;
            else if (strcmp(name, ":authority") == 0) slot = &authority;
            else return -1;
            if (*slot) return -1;
            *slot = value;
            continue;
        }
        regular_seen = 1;
        if (!h2_name_valid(name) ||
            strcmp(name, "connection") == 0 ||
            strcmp(name, "proxy-connection") == 0 ||
            strcmp(name, "keep-alive") == 0 ||
            strcmp(name, "transfer-encoding") == 0 ||
            strcmp(name, "upgrade") == 0)
            return -1;
        if (strcmp(name, "te") == 0 && strcasecmp(value, "trailers") != 0)
            return -1;
        if (strcmp(name, "host") == 0) {
            if (host) return -1;
            host = value;
        }
        if (strcmp(name, "content-length") == 0) {
            if (content_length_seen ||
                h2_parse_content_length(value, &stream->content_length) != 0)
                return -1;
            content_length_seen = 1;
        }
    }
    if (!method || !h2_token_valid(method) ||
        strcmp(method, "CONNECT") == 0 ||
        !scheme || (strcmp(scheme, "http") != 0 &&
                    strcmp(scheme, "https") != 0) ||
        !h2_valid_path(method, path))
        return -1;
    /* RFC 9113 §8.3.1: if both are present they MUST be equivalent.
     * Compare before treating empty as absent, or ":authority: " plus
     * "host: victim" (and the reverse) would skip the check and later
     * disagree between request.host and the Host header. */
    if (authority && host && !h2_ascii_ieq(authority, host))
        return -1;
    if (authority && !*authority) authority = NULL;
    if (host && !*host) host = NULL;
    if (!authority && !host)
        return -1;
    if (authority && !h2_valid_authority(authority))
        return -1;
    if (host && !h2_valid_authority(host))
        return -1;
    return 0;
}

static int h2_validate_trailers(h2_conn_t *conn,
                                neverc_hpack_header_t *trailers,
                                int count) {
    size_t list_size = 0;
    for (int i = 0; i < count; i++) {
        const char *name = trailers[i].name;
        const char *value = trailers[i].value;
        size_t name_length = strlen(name);
        size_t value_length = strlen(value);
        if (name_length == 0 || name[0] == ':' || !h2_name_valid(name) ||
            !h2_value_valid(value) ||
            name_length > SIZE_MAX - value_length - 32 ||
            list_size > SIZE_MAX - name_length - value_length - 32)
            return -1;
        list_size += name_length + value_length + 32;
        if (list_size > conn->local_settings.max_header_list_size)
            return -1;
        if (strcmp(name, "connection") == 0 ||
            strcmp(name, "proxy-connection") == 0 ||
            strcmp(name, "keep-alive") == 0 ||
            strcmp(name, "transfer-encoding") == 0 ||
            strcmp(name, "upgrade") == 0 ||
            strcmp(name, "content-length") == 0 ||
            strcmp(name, "host") == 0 || strcmp(name, "te") == 0)
            return -1;
    }
    return 0;
}

static int h2_process_trailer_block(h2_conn_t *conn, h2_stream_t *stream,
                                    const uint8_t *block, size_t block_len,
                                    int end_stream) {
    neverc_hpack_header_t trailers[64];
    memset(trailers, 0, sizeof(trailers));
    int count = 0;
    int rc = neverc_hpack_decode(conn->hpack_dec, block, block_len,
                                 trailers, 64, &count);
    if (rc < 0) {
        for (int i = 0; i < count; i++) {
            free(trailers[i].name);
            free(trailers[i].value);
        }
        return -1;
    }
    if (rc > 0) {
        for (int i = 0; i < count; i++) {
            free(trailers[i].name);
            free(trailers[i].value);
        }
        return -2;
    }
    if (!end_stream ||
        (stream->state != H2_STREAM_OPEN &&
         stream->state != H2_STREAM_HALF_CLOSED_LOCAL)) {
        for (int i = 0; i < count; i++) {
            free(trailers[i].name);
            free(trailers[i].value);
        }
        return -2;
    }
    int valid = h2_validate_trailers(conn, trailers, count) == 0 &&
        (stream->content_length < 0 ||
         (uint64_t)stream->content_length == stream->body_len);
    for (int i = 0; i < count; i++) {
        free(trailers[i].name);
        free(trailers[i].value);
    }
    if (!valid) return -2;

    nc_mutex_lock(&conn->state_lock);
    stream->state = stream->state == H2_STREAM_HALF_CLOSED_LOCAL
        ? H2_STREAM_CLOSED : H2_STREAM_HALF_CLOSED_REMOTE;
    nc_mutex_unlock(&conn->state_lock);
    nc_atomic_store(&stream->remote_ended, 1);
    if (stream->receive_queue)
        (void)neverc_thread_channel_close(stream->receive_queue);
    if (!stream->streaming_request)
        (void)h2_submit_request(conn, stream);
    return 0;
}

static int h2_request_uses_streaming_route(h2_conn_t *connection,
                                           h2_stream_t *stream) {
    const char *method = NULL;
    const char *path = NULL;
    for (int i = 0; i < stream->nheaders; i++) {
        if (strcmp(stream->headers[i].name, ":method") == 0)
            method = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":path") == 0)
            path = stream->headers[i].value;
    }
    if (!method || !path) return 0;
    char *route_path = strdup(path);
    if (!route_path) return 0;
    char *query = strchr(route_path, '?');
    if (query) *query = '\0';
    int streaming = nc_http_mux_is_streaming(
        connection->srv->mux, method, route_path);
    free(route_path);
    return streaming;
}

static int h2_process_header_block(h2_conn_t *conn, h2_stream_t *stream,
                                    const uint8_t *block, size_t block_len,
                                    int end_stream) {
    if (stream->headers_complete)
        return h2_process_trailer_block(conn, stream, block, block_len,
                                        end_stream);
    int nh = 0;
    int rc = neverc_hpack_decode(conn->hpack_dec, block, block_len,
                                 stream->headers, 64, &nh);
    if (rc < 0) {
        for (int i = 0; i < nh; i++) {
            free(stream->headers[i].name);
            free(stream->headers[i].value);
            stream->headers[i].name = NULL;
            stream->headers[i].value = NULL;
        }
        return -1;
    }
    stream->nheaders = nh;
    if (rc > 0) return -2;
    if (h2_validate_request_headers(conn, stream) != 0) return -2;
    stream->headers_complete = 1;
    stream->streaming_request = h2_request_uses_streaming_route(conn, stream);
    if (stream->streaming_request) {
        stream->receive_queue = neverc_thread_channel_create(8);
        if (!stream->receive_queue) return -2;
    }
    if (end_stream) {
        if (stream->content_length > 0) return -2;
        nc_atomic_store(&stream->remote_ended, 1);
        if (stream->receive_queue)
            (void)neverc_thread_channel_close(stream->receive_queue);
        stream->state = H2_STREAM_HALF_CLOSED_REMOTE;
        (void)h2_submit_request(conn, stream);
    } else if (stream->streaming_request) {
        (void)h2_submit_request(conn, stream);
    }
    return 0;
}

static int h2_finish_header_block(h2_conn_t *conn, h2_stream_t *stream) {
    int result = h2_process_header_block(
        conn, stream, conn->pending_hdr_block, conn->pending_hdr_len,
        conn->pending_end_stream);
    if (result != 0) return result;
    h2_clear_pending_hdr(conn);
    return 0;
}

static int h2_handle_header_result(h2_conn_t *conn, h2_stream_t *stream,
                                   int result) {
    if (result == 0) return 0;
    if (result == -2) {
        h2_clear_pending_hdr(conn);
        nc_atomic_store(&stream->reset, 1);
        if (stream->cancel)
            neverc_context_cancel_handle_cancel(stream->cancel);
        if (stream->receive_queue)
            (void)neverc_thread_channel_close(stream->receive_queue);
        (void)h2_conn_write_rst(conn, stream->id, NC_H2_PROTOCOL_ERROR);
        nc_mutex_lock(&conn->state_lock);
        int handler_active = stream->handler_active;
        nc_mutex_unlock(&conn->state_lock);
        if (!handler_active)
            h2_remove_stream(conn, stream->id);
        return 0;
    }
    (void)h2_conn_write_goaway(conn, NC_H2_COMPRESSION_ERROR);
    return -1;
}

static h2_stream_t *h2_find_stream(h2_conn_t *conn, uint32_t id) {
    for (h2_stream_t *s = conn->streams; s; s = s->next)
        if (s->id == id) return s;
    return NULL;
}

static void h2_record_closed_stream(h2_conn_t *conn, h2_stream_t *stream) {
    h2_closed_stream_t *entry =
        &conn->closed_streams[conn->closed_stream_next];
    entry->id = stream->id;
    entry->reset = nc_atomic_load(&stream->reset) != 0;
    conn->closed_stream_next =
        (conn->closed_stream_next + 1U) % H2_CLOSED_STREAM_HISTORY;
    if (conn->closed_stream_count < H2_CLOSED_STREAM_HISTORY)
        conn->closed_stream_count++;
}

static int h2_closed_stream_was_reset(h2_conn_t *conn, uint32_t id,
                                      int *found) {
    *found = 0;
    for (size_t i = 0; i < conn->closed_stream_count; i++) {
        const h2_closed_stream_t *entry = &conn->closed_streams[i];
        if (entry->id == id) {
            *found = 1;
            return entry->reset;
        }
    }
    return 0;
}

/* RFC 9113 §5.1.1: a stream identifier cannot be reused after the client
 * first uses it, even when the server refuses the stream. */
static void h2_consume_idle_stream_id(h2_conn_t *conn, uint32_t id) {
    if (id <= conn->max_stream_id)
        return;
    conn->max_stream_id = id;
    h2_closed_stream_t *entry =
        &conn->closed_streams[conn->closed_stream_next];
    entry->id = id;
    entry->reset = 1;
    conn->closed_stream_next =
        (conn->closed_stream_next + 1U) % H2_CLOSED_STREAM_HISTORY;
    if (conn->closed_stream_count < H2_CLOSED_STREAM_HISTORY)
        conn->closed_stream_count++;
}

/* PRIORITY does not open a stream (RFC 9113 §5.1 / §6.3), so recording a
 * self-dependent idle id must not raise max_stream_id and close lower ids. */
static void h2_remember_reset_stream_id(h2_conn_t *conn, uint32_t id) {
    size_t i;
    for (i = 0; i < conn->closed_stream_count; i++) {
        if (conn->closed_streams[i].id == id) {
            conn->closed_streams[i].reset = 1;
            return;
        }
    }
    h2_closed_stream_t *entry =
        &conn->closed_streams[conn->closed_stream_next];
    entry->id = id;
    entry->reset = 1;
    conn->closed_stream_next =
        (conn->closed_stream_next + 1U) % H2_CLOSED_STREAM_HISTORY;
    if (conn->closed_stream_count < H2_CLOSED_STREAM_HISTORY)
        conn->closed_stream_count++;
}

static h2_stream_t *h2_create_stream(h2_conn_t *conn, uint32_t id) {
    h2_stream_t *s = (h2_stream_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->conn = conn;
    s->context_background = neverc_context_background();
    if (!s->context_background) {
        free(s);
        return NULL;
    }
    s->context = conn->srv->handler_timeout_ms > 0
        ? neverc_context_with_timeout_handle(
              s->context_background, conn->srv->handler_timeout_ms,
              &s->cancel)
        : neverc_context_with_cancel_handle(
              s->context_background, &s->cancel);
    if (!s->context || !s->cancel) {
        if (s->context) neverc_context_free(s->context);
        if (s->cancel) neverc_context_cancel_handle_free(s->cancel);
        neverc_context_free(s->context_background);
        free(s);
        return NULL;
    }
    s->id = id;
    s->state = H2_STREAM_OPEN;
    s->counted_active = 1;
    s->recv_window = (int32_t)conn->local_settings.initial_window_size;
    s->send_window = (int32_t)conn->peer_settings.initial_window_size;
    s->next = conn->streams;
    conn->streams = s;
    nc_atomic_inc(&conn->active_streams);
    if (id > conn->last_stream_id)
        conn->last_stream_id = id;
    return s;
}

static void h2_close_stream(h2_conn_t *conn, h2_stream_t *s) {
    s->state = H2_STREAM_CLOSED;
    if (s->cancel) neverc_context_cancel_handle_cancel(s->cancel);
    for (int i = 0; i < s->nheaders; i++) {
        free(s->headers[i].name);
        free(s->headers[i].value);
    }
    free(s->body);
    if (s->receive_queue) {
        (void)neverc_thread_channel_close(s->receive_queue);
        void *queued = NULL;
        while (neverc_thread_channel_try_receive(
                   s->receive_queue, &queued) == NEVERC_THREAD_OK)
            free(queued);
        neverc_thread_channel_free(s->receive_queue);
    }
    free(s->receive_current);
    s->nheaders = 0;
    s->body = NULL;
    s->body_len = 0;
    s->receive_queue = NULL;
    s->receive_current = NULL;
    if (s->cancel) neverc_context_cancel_handle_free(s->cancel);
    if (s->context) neverc_context_free(s->context);
    if (s->context_background) neverc_context_free(s->context_background);
    s->cancel = NULL;
    s->context = NULL;
    s->context_background = NULL;
    if (s->counted_active) {
        nc_atomic_dec(&conn->active_streams);
        s->counted_active = 0;
    }
}

static void h2_remove_stream(h2_conn_t *conn, uint32_t id) {
    h2_stream_t **pp = &conn->streams;
    while (*pp) {
        if ((*pp)->id == id) {
            h2_stream_t *s = *pp;
            *pp = s->next;
            h2_record_closed_stream(conn, s);
            h2_close_stream(conn, s);
            free(s);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* Caller holds state_lock. Sends RST_STREAM, then releases the lock and
 * removes the stream unless a handler is still running. */
static void h2_abort_stream_locked(h2_conn_t *conn, h2_stream_t *stream,
                                   uint32_t error_code) {
    nc_atomic_store(&stream->reset, 1);
    if (stream->cancel)
        neverc_context_cancel_handle_cancel(stream->cancel);
    if (stream->receive_queue)
        (void)neverc_thread_channel_close(stream->receive_queue);
    int handler_active = stream->handler_active;
    uint32_t id = stream->id;
    nc_mutex_unlock(&conn->state_lock);
    (void)h2_conn_write_rst(conn, id, error_code);
    if (!handler_active)
        h2_remove_stream(conn, id);
}

static void h2_reap_completed_streams(h2_conn_t *conn) {
    h2_stream_t **link = &conn->streams;
    while (*link) {
        h2_stream_t *stream = *link;
        if (!nc_atomic_load(&stream->handler_done)) {
            link = &stream->next;
            continue;
        }
        *link = stream->next;
        h2_record_closed_stream(conn, stream);
        h2_close_stream(conn, stream);
        free(stream);
    }
}

static void h2_connection_write_failed(h2_conn_t *conn) {
    nc_atomic_store(&conn->running, 0);
    h2_io_shutdown(&conn->io);
    nc_mutex_lock(&conn->state_lock);
    nc_cond_broadcast(&conn->window_changed);
    nc_mutex_unlock(&conn->state_lock);
}

static void h2_window_wait_tick(h2_conn_t *conn) {
#ifdef _WIN32
    (void)SleepConditionVariableCS(&conn->window_changed,
                                   &conn->state_lock, 25);
#else
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) return;
    deadline.tv_nsec += 25L * 1000L * 1000L;
    if (deadline.tv_nsec >= 1000L * 1000L * 1000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000L * 1000L * 1000L;
    }
    (void)pthread_cond_timedwait(&conn->window_changed,
                                 &conn->state_lock, &deadline);
#endif
}

static void h2_mark_local_end_stream(h2_conn_t *conn, h2_stream_t *stream) {
    nc_mutex_lock(&conn->state_lock);
    if (stream->state == H2_STREAM_OPEN)
        stream->state = H2_STREAM_HALF_CLOSED_LOCAL;
    else if (stream->state == H2_STREAM_HALF_CLOSED_REMOTE)
        stream->state = H2_STREAM_CLOSED;
    nc_mutex_unlock(&conn->state_lock);
}

static int h2_send_header_block(h2_conn_t *conn, h2_stream_t *stream,
                                const neverc_hpack_header_t *headers,
                                int header_count, int end_stream) {
    uint8_t block[64 * 1024];
    size_t block_length = 0;
    nc_mutex_lock(&conn->state_lock);
    uint32_t max_frame_size = conn->peer_settings.max_frame_size;
    nc_mutex_unlock(&conn->state_lock);
    nc_mutex_lock(&conn->write_lock);
    int result = neverc_hpack_encode(conn->hpack_enc, headers, header_count,
                                     block, sizeof(block), &block_length);
    size_t offset = 0;
    int first = 1;
    while (result == 0 && (first || offset < block_length)) {
        size_t chunk = block_length - offset;
        if (chunk > max_frame_size) chunk = max_frame_size;
        uint8_t flags = offset + chunk == block_length
            ? NC_H2_FLAG_END_HEADERS : 0;
        if (first && end_stream) flags |= NC_H2_FLAG_END_STREAM;
        result = h2_write_frame(
            &conn->io, first ? NC_H2_FRAME_HEADERS
                             : NC_H2_FRAME_CONTINUATION,
            flags, stream->id, block + offset, (uint32_t)chunk);
        first = 0;
        offset += chunk;
    }
    nc_mutex_unlock(&conn->write_lock);
    if (result != 0) h2_connection_write_failed(conn);
    else if (end_stream)
        h2_mark_local_end_stream(conn, stream);
    return result;
}

static int h2_send_response_data(h2_conn_t *conn, h2_stream_t *stream,
                                 const uint8_t *body, size_t length,
                                 int end_stream) {
    size_t offset = 0;
    if (length == 0) {
        if (!end_stream)
            return 0;
        if (h2_conn_write_frame(conn, NC_H2_FRAME_DATA,
                                NC_H2_FLAG_END_STREAM, stream->id,
                                NULL, 0) != 0) {
            h2_connection_write_failed(conn);
            return -1;
        }
        h2_mark_local_end_stream(conn, stream);
        return 0;
    }
    while (offset < length) {
        nc_mutex_lock(&conn->state_lock);
        while (nc_atomic_load(&conn->running) && !stream->reset &&
               !neverc_context_done(stream->context) &&
               (conn->conn_send_window <= 0 || stream->send_window <= 0))
            h2_window_wait_tick(conn);
        if (!nc_atomic_load(&conn->running) || stream->reset ||
            neverc_context_done(stream->context)) {
            nc_mutex_unlock(&conn->state_lock);
            return -1;
        }
        size_t chunk = length - offset;
        if (chunk > conn->peer_settings.max_frame_size)
            chunk = conn->peer_settings.max_frame_size;
        /* A SETTINGS INITIAL_WINDOW_SIZE shrink can make send_window
         * negative. Casting that to size_t would wrap and send past the
         * advertised window. */
        if (conn->conn_send_window <= 0 || stream->send_window <= 0) {
            nc_mutex_unlock(&conn->state_lock);
            continue;
        }
        if (chunk > (size_t)conn->conn_send_window)
            chunk = (size_t)conn->conn_send_window;
        if (chunk > (size_t)stream->send_window)
            chunk = (size_t)stream->send_window;
        conn->conn_send_window -= (int32_t)chunk;
        stream->send_window -= (int32_t)chunk;
        nc_mutex_unlock(&conn->state_lock);
        uint8_t flags = end_stream && offset + chunk == length
            ? NC_H2_FLAG_END_STREAM : 0;
        if (h2_conn_write_frame(conn, NC_H2_FRAME_DATA, flags, stream->id,
                                body + offset, (uint32_t)chunk) != 0) {
            h2_connection_write_failed(conn);
            return -1;
        }
        offset += chunk;
        if (flags & NC_H2_FLAG_END_STREAM)
            h2_mark_local_end_stream(conn, stream);
    }
    return 0;
}

typedef struct {
    h2_conn_t *connection;
    h2_stream_t *stream;
    int ended;
} h2_response_adapter_t;

static char *h2_lowercase_name(const char *name) {
    size_t length = strlen(name);
    char *lower = (char *)malloc(length + 1);
    if (!lower) return NULL;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name[i];
        lower[i] = c >= 'A' && c <= 'Z'
            ? (char)(c + ('a' - 'A')) : (char)c;
    }
    lower[length] = '\0';
    return lower;
}

static int h2_is_connection_specific_header(const char *name,
                                            const char *value) {
    if (!name) return 1;
    if (strcasecmp(name, "connection") == 0 ||
        strcasecmp(name, "keep-alive") == 0 ||
        strcasecmp(name, "proxy-connection") == 0 ||
        strcasecmp(name, "transfer-encoding") == 0 ||
        strcasecmp(name, "upgrade") == 0)
        return 1;
    if (strcasecmp(name, "te") == 0 &&
        (!value || strcasecmp(value, "trailers") != 0))
        return 1;
    return 0;
}

static int h2_response_flush(void *context,
                             neverc_http_response_writer_t *writer,
                             int end_stream) {
    h2_response_adapter_t *adapter = (h2_response_adapter_t *)context;
    h2_conn_t *connection = adapter->connection;
    h2_stream_t *stream = adapter->stream;
    if (adapter->ended || nc_atomic_load(&stream->reset) ||
        !nc_atomic_load(&connection->running))
        return adapter->ended ? 0 : -1;

    size_t representation_length = writer->body.len;
    int body_allowed = !writer->head_request && writer->status >= 200 &&
                       writer->status != 204 && writer->status != 304;
    if (!body_allowed)
        nc_buf_reset(&writer->body);
    int has_body = writer->body.len > 0;
    int has_trailers = writer->ntrailers > 0;
    int result = 0;

    if (!writer->headers_sent) {
        char status[4];
        if (snprintf(status, sizeof(status), "%d", writer->status) != 3)
            return -1;
        neverc_hpack_header_t headers[HTTP_MAX_HEADERS + 2];
        char *lower_names[HTTP_MAX_HEADERS];
        char content_length[32];
        memset(lower_names, 0, sizeof(lower_names));
        int count = 0;
        headers[count++] = (neverc_hpack_header_t){
            .name = ":status", .value = status, .sensitive = 0};
        for (int i = 0; i < writer->nheaders; i++) {
            if (h2_is_connection_specific_header(
                    writer->header_names[i], writer->header_values[i]))
                continue;
            lower_names[i] = h2_lowercase_name(writer->header_names[i]);
            if (!lower_names[i]) {
                result = -1;
                break;
            }
            headers[count++] = (neverc_hpack_header_t){
                .name = lower_names[i], .value = writer->header_values[i],
                .sensitive = 0};
        }
        /* RFC 9113 §8.1.1: if content-length is present, it MUST equal the
         * sum of DATA payload lengths. HTTP/2 cannot paper over a short
         * body by closing the connection. Chunked responses have no known
         * length — do not advertise one. */
        if (result == 0 && writer->status >= 200 &&
            writer->status != 204 && !writer->chunked) {
            size_t content_length_value = 0;
            int emit_content_length = 0;
            if (writer->has_content_length_override) {
                if (body_allowed &&
                    writer->body.len != writer->content_length_override)
                    result = -1;
                else {
                    content_length_value =
                        writer->content_length_override;
                    emit_content_length = 1;
                }
            } else if (writer->head_request && writer->status != 304) {
                content_length_value = representation_length;
                emit_content_length = 1;
            }
            if (result == 0 && emit_content_length) {
                int length = snprintf(
                    content_length, sizeof(content_length), "%zu",
                    content_length_value);
                if (length < 0 ||
                    (size_t)length >= sizeof(content_length)) {
                    result = -1;
                } else {
                    headers[count++] = (neverc_hpack_header_t){
                        .name = "content-length",
                        .value = content_length,
                        .sensitive = 0};
                }
            }
        }
        if (result == 0)
            result = h2_send_header_block(
                connection, stream, headers, count,
                end_stream && !has_body && !has_trailers);
        for (int i = 0; i < HTTP_MAX_HEADERS; i++)
            free(lower_names[i]);
        if (result != 0) return -1;
        writer->headers_sent = 1;
        if (end_stream && !has_body && !has_trailers)
            adapter->ended = 1;
    }

    if (has_body) {
        result = h2_send_response_data(
            connection, stream, (const uint8_t *)writer->body.data,
            writer->body.len, end_stream && !has_trailers);
        nc_buf_reset(&writer->body);
        if (result != 0) return -1;
        if (end_stream && !has_trailers)
            adapter->ended = 1;
    }

    if (end_stream && has_trailers) {
        neverc_hpack_header_t trailers[HTTP_MAX_HEADERS];
        char *lower_names[HTTP_MAX_HEADERS];
        memset(lower_names, 0, sizeof(lower_names));
        int count = 0;
        for (int i = 0; i < writer->ntrailers; i++) {
            if (h2_is_connection_specific_header(
                    writer->trailer_names[i], writer->trailer_values[i]))
                continue;
            lower_names[i] = h2_lowercase_name(writer->trailer_names[i]);
            if (!lower_names[i]) {
                result = -1;
                break;
            }
            trailers[count++] = (neverc_hpack_header_t){
                .name = lower_names[i], .value = writer->trailer_values[i],
                .sensitive = 0};
        }
        if (result == 0)
            result = h2_send_header_block(connection, stream, trailers,
                                          count, 1);
        for (int i = 0; i < HTTP_MAX_HEADERS; i++)
            free(lower_names[i]);
        if (result != 0) return -1;
        adapter->ended = 1;
    } else if (end_stream && !adapter->ended) {
        if (h2_send_response_data(connection, stream, NULL, 0, 1) != 0)
            return -1;
        adapter->ended = 1;
    }
    return 0;
}

static void h2_dispatch_request(h2_conn_t *conn, h2_stream_t *stream) {
    const char *method = NULL, *path = NULL, *authority = NULL;
    const char *host = NULL, *content_type = NULL;
    nc_buf_t raw_headers;
    nc_buf_init(&raw_headers);
    int regular_headers = 0;
    for (int i = 0; i < stream->nheaders; i++) {
        if (strcmp(stream->headers[i].name, ":method") == 0)
            method = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":path") == 0)
            path = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":authority") == 0)
            authority = stream->headers[i].value;
        else if (stream->headers[i].name[0] != ':') {
            if (strcmp(stream->headers[i].name, "host") == 0)
                host = stream->headers[i].value;
            if (strcmp(stream->headers[i].name, "content-type") == 0)
                content_type = stream->headers[i].value;
            if (nc_buf_append(&raw_headers, stream->headers[i].name,
                              strlen(stream->headers[i].name) + 1) != 0 ||
                nc_buf_append(&raw_headers, stream->headers[i].value,
                              strlen(stream->headers[i].value) + 1) != 0) {
                nc_buf_free(&raw_headers);
                (void)h2_conn_write_rst(conn, stream->id,
                                        NC_H2_INTERNAL_ERROR);
                return;
            }
            regular_headers++;
        }
    }
    if (!method || !path) {
        nc_buf_free(&raw_headers);
        (void)h2_conn_write_rst(conn, stream->id, NC_H2_PROTOCOL_ERROR);
        return;
    }
    (void)nc_buf_append(&raw_headers, "", 1);
    char *path_copy = strdup(path);
    neverc_http_response_writer_t *writer =
        neverc_http_memory_writer_new();
    if (!path_copy || !writer) {
        free(path_copy);
        neverc_http_memory_writer_free(writer);
        nc_buf_free(&raw_headers);
        (void)h2_conn_write_rst(conn, stream->id, NC_H2_INTERNAL_ERROR);
        return;
    }
    char *query = strchr(path_copy, '?');
    if (query) *query++ = '\0';
    neverc_http_request_t request;
    memset(&request, 0, sizeof(request));
    request.method = method;
    request.path = path_copy;
    request.query = query;
    request.http_version = "HTTP/2.0";
    request.host = (authority && *authority) ? authority : host;
    request.content_type = content_type;
    request.body = stream->streaming_request
        ? NULL : (const char *)stream->body;
    request.body_len = stream->streaming_request ? 0U : stream->body_len;
    request.raw_headers = raw_headers.data;
    request.nheaders = regular_headers;
    request.context = stream->context;
    request.protocol_stream = stream->streaming_request ? stream : NULL;
    request.body_stream = stream->streaming_request ? stream : NULL;
    request.body_stream_read = stream->streaming_request
        ? neverc_h2_request_stream_read : NULL;
    request.body_stream_cancel = stream->streaming_request
        ? neverc_h2_request_stream_cancel : NULL;
    writer->head_request = strcmp(method, "HEAD") == 0;
    writer->request_body_len = stream->streaming_request
        ? (stream->content_length > 0 ?
           (size_t)stream->content_length : 0U)
        : stream->body_len;
    if (conn->srv->alt_svc)
        neverc_http_set_header(writer, "Alt-Svc", conn->srv->alt_svc);
    h2_response_adapter_t response_adapter = {
        .connection = conn, .stream = stream, .ended = 0};
    nc_http_writer_set_protocol(writer, &response_adapter,
                                h2_response_flush);
    nc_http_mux_dispatch(conn->srv->mux, &request, writer);
    if (!nc_atomic_load(&conn->running) ||
        nc_atomic_load(&stream->reset)) {
        neverc_http_memory_writer_free(writer);
        free(path_copy);
        nc_buf_free(&raw_headers);
        return;
    }
    if (neverc_context_done(stream->context)) {
        writer->status = 503;
        writer->body_limit_exceeded = 0;
        nc_buf_reset(&writer->body);
        neverc_http_set_header(writer, "Content-Type",
                               "text/plain; charset=utf-8");
        neverc_http_set_header(writer, "Connection", "close");
    }
    int sent = nc_http_writer_finish(writer);
    if (sent != 0 && nc_atomic_load(&conn->running))
        (void)h2_conn_write_rst(conn, stream->id, NC_H2_INTERNAL_ERROR);
    neverc_http_memory_writer_free(writer);
    free(path_copy);
    nc_buf_free(&raw_headers);
}

static void h2_handler_task(void *arg) {
    h2_stream_t *stream = (h2_stream_t *)arg;
    h2_conn_t *conn = stream->conn;
    if (!nc_atomic_load(&stream->reset) &&
        !neverc_context_done(stream->context) &&
        nc_atomic_load(&conn->running))
        h2_dispatch_request(conn, stream);
    nc_mutex_lock(&conn->state_lock);
    stream->handler_active = 0;
    stream->state = H2_STREAM_CLOSED;
    if (stream->counted_active) {
        nc_atomic_dec(&conn->active_streams);
        stream->counted_active = 0;
    }
    nc_atomic_store(&stream->handler_done, 1);
    conn->active_handlers--;
    int finish_draining = nc_atomic_load(&conn->goaway_sent) &&
                          nc_atomic_load(&conn->active_streams) == 0;
    nc_cond_broadcast(&conn->handlers_done);
    nc_cond_broadcast(&conn->window_changed);
    nc_mutex_unlock(&conn->state_lock);
    if (finish_draining)
        h2_io_shutdown(&conn->io);
}

static int h2_submit_request(h2_conn_t *conn, h2_stream_t *stream) {
    nc_mutex_lock(&conn->state_lock);
    if (stream->handler_active || stream->reset ||
        !nc_atomic_load(&conn->running)) {
        nc_mutex_unlock(&conn->state_lock);
        return -1;
    }
    stream->handler_active = 1;
    conn->active_handlers++;
    nc_mutex_unlock(&conn->state_lock);
    int submitted = neverc_thread_executor_try_submit(
        conn->srv->handler_executor, h2_handler_task, stream);
    if (submitted == NEVERC_THREAD_OK) return 0;
    nc_mutex_lock(&conn->state_lock);
    stream->handler_active = 0;
    conn->active_handlers--;
    nc_cond_broadcast(&conn->handlers_done);
    nc_mutex_unlock(&conn->state_lock);
    nc_atomic_store(&stream->reset, 1);
    (void)h2_conn_write_rst(conn, stream->id, NC_H2_REFUSED_STREAM);
    h2_remove_stream(conn, stream->id);
    return -1;
}

static uint32_t h2_process_settings(h2_conn_t *conn, const uint8_t *payload,
                                    uint32_t len) {
    if (len % 6 != 0) return NC_H2_FRAME_SIZE_ERROR;
    uint32_t error = NC_H2_NO_ERROR;
    nc_mutex_lock(&conn->state_lock);
    for (uint32_t i = 0; i < len; i += 6) {
        uint16_t id = (uint16_t)((payload[i] << 8) | payload[i + 1]);
        uint32_t val = ((uint32_t)payload[i + 2] << 24) |
                       ((uint32_t)payload[i + 3] << 16) |
                       ((uint32_t)payload[i + 4] << 8) |
                       (uint32_t)payload[i + 5];
        switch (id) {
        case NC_H2_SETTINGS_HEADER_TABLE_SIZE: {
            conn->peer_settings.header_table_size = val;
            uint32_t encoder_table_size =
                val > NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE
                    ? NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE : val;
            nc_mutex_lock(&conn->write_lock);
            int table_result = neverc_hpack_encoder_set_max_table_size(
                conn->hpack_enc, encoder_table_size);
            nc_mutex_unlock(&conn->write_lock);
            if (table_result != 0) {
                error = NC_H2_COMPRESSION_ERROR;
                goto done;
            }
            break;
        }
        case NC_H2_SETTINGS_ENABLE_PUSH:
            if (val > 1) {
                error = NC_H2_PROTOCOL_ERROR;
                goto done;
            }
            conn->peer_settings.enable_push = (int)val;
            break;
        case NC_H2_SETTINGS_MAX_CONCURRENT_STREAMS:
            conn->peer_settings.max_concurrent_streams = val;
            break;
        case NC_H2_SETTINGS_INITIAL_WINDOW_SIZE:
            if (val > 0x7fffffff) {
                error = NC_H2_FLOW_CONTROL_ERROR;
                goto done;
            }
            int64_t delta = (int64_t)val -
                (int64_t)conn->peer_settings.initial_window_size;
            for (h2_stream_t *stream = conn->streams; stream;
                 stream = stream->next) {
                int64_t updated = (int64_t)stream->send_window + delta;
                if (updated < INT32_MIN || updated > INT32_MAX) {
                    error = NC_H2_FLOW_CONTROL_ERROR;
                    goto done;
                }
            }
            for (h2_stream_t *stream = conn->streams; stream;
                 stream = stream->next)
                stream->send_window += (int32_t)delta;
            conn->peer_settings.initial_window_size = val;
            nc_cond_broadcast(&conn->window_changed);
            break;
        case NC_H2_SETTINGS_MAX_FRAME_SIZE:
            if (val < 16384 || val > 16777215) {
                error = NC_H2_PROTOCOL_ERROR;
                goto done;
            }
            conn->peer_settings.max_frame_size = val;
            break;
        case NC_H2_SETTINGS_MAX_HEADER_LIST_SIZE:
            conn->peer_settings.max_header_list_size = val;
            break;
        default:
            break; /* unknown settings are ignored */
        }
    }
done:
    nc_mutex_unlock(&conn->state_lock);
    return error;
}

static int h2_serve_io(neverc_h2_server_t *srv, h2_io_t *io) {
    if (!srv || !io)
        return -1;
    h2_io_set_timeout(io, H2_IO_TIMEOUT_MS);

    h2_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    int registered = 0;
    int preface_received = 0;
    nc_mutex_init(&conn.state_lock);
    nc_mutex_init(&conn.write_lock);
    nc_cond_init(&conn.window_changed);
    nc_cond_init(&conn.handlers_done);
    nc_atomic_store(&conn.running, 1);
    conn.io = *io;
    conn.srv = srv;
    conn.local_settings = srv->settings;
    neverc_h2_settings_init(&conn.peer_settings);
    /* RFC 9113 §6.9.1: the connection window starts at 65,535. Stream
     * windows use SETTINGS_INITIAL_WINDOW_SIZE; the connection window
     * does not. */
    conn.conn_recv_window = NC_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    conn.conn_send_window = NC_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    if (h2_server_register_connection(srv, &conn) != 0)
        goto cleanup;
    registered = 1;

    char preface[NC_H2_CLIENT_PREFACE_LEN];
    if (h2_io_read_all(io, preface, NC_H2_CLIENT_PREFACE_LEN) != 0)
        goto cleanup;
    if (memcmp(preface, NC_H2_CLIENT_PREFACE,
               NC_H2_CLIENT_PREFACE_LEN) != 0) {
        (void)h2_conn_write_goaway(&conn, NC_H2_PROTOCOL_ERROR);
        goto cleanup;
    }
    preface_received = 1;

    conn.hpack_dec = neverc_hpack_decoder_create(conn.local_settings.header_table_size);
    conn.hpack_enc = neverc_hpack_encoder_create(conn.peer_settings.header_table_size);
    if (!conn.hpack_dec || !conn.hpack_enc)
        goto cleanup;

    if (h2_write_settings(&conn.io, &conn.local_settings) != 0)
        goto cleanup;
    conn.settings_ack_owed = 1;

    for (;;) {
        if (nc_atomic_load(&conn.goaway_sent) &&
            nc_atomic_load(&conn.active_streams) == 0)
            break;
        uint8_t frame_hdr_buf[NC_H2_FRAME_HEADER_SIZE];
        if (h2_io_read_all(&conn.io, frame_hdr_buf, NC_H2_FRAME_HEADER_SIZE) != 0)
            break;

        neverc_h2_frame_header_t fhdr;
        if (neverc_h2_frame_header_read(frame_hdr_buf, NC_H2_FRAME_HEADER_SIZE,
                                        &fhdr) != 0)
            break;
        h2_reap_completed_streams(&conn);

        if (fhdr.length > conn.local_settings.max_frame_size) {
            (void)h2_conn_write_goaway(&conn, NC_H2_FRAME_SIZE_ERROR);
            (void)h2_io_discard(&conn.io, fhdr.length);
            break;
        }

        uint8_t *payload = NULL;
        if (fhdr.length > 0) {
            payload = (uint8_t *)malloc(fhdr.length);
            if (!payload) {
                (void)h2_conn_write_goaway(&conn, NC_H2_INTERNAL_ERROR);
                break;
            }
            if (h2_io_read_all(&conn.io, payload, fhdr.length) != 0) {
                free(payload);
                break;
            }
        }

        uint32_t header_error = h2_frame_header_error(&fhdr);
        if (header_error != NC_H2_NO_ERROR ||
            (!conn.initial_settings_received &&
             (fhdr.type != NC_H2_FRAME_SETTINGS ||
              (fhdr.flags & NC_H2_FLAG_ACK) != 0)) ||
            (conn.pending_hdr_active &&
             fhdr.type != NC_H2_FRAME_CONTINUATION)) {
            (void)h2_conn_write_goaway(
                &conn,
                header_error != NC_H2_NO_ERROR
                    ? header_error : NC_H2_PROTOCOL_ERROR);
            free(payload);
            goto cleanup;
        }

        switch (fhdr.type) {
        case NC_H2_FRAME_SETTINGS:
            if (fhdr.flags & NC_H2_FLAG_ACK) {
                /* RFC 9113 §6.5.3: ACK with a payload is already rejected
                 * as FRAME_SIZE_ERROR. An ACK with no outstanding SETTINGS
                 * is a PROTOCOL_ERROR. */
                if (conn.settings_ack_owed == 0) {
                    (void)h2_conn_write_goaway(&conn, NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                conn.settings_ack_owed--;
            } else {
                uint32_t settings_error = h2_process_settings(
                    &conn, payload, fhdr.length);
                if (settings_error != NC_H2_NO_ERROR) {
                    (void)h2_conn_write_goaway(&conn, settings_error);
                    free(payload);
                    goto cleanup;
                }
                conn.initial_settings_received = 1;
                (void)h2_conn_write_settings_ack(&conn);
            }
            break;

        case NC_H2_FRAME_PING:
            if (fhdr.length != 8) {
                (void)h2_conn_write_goaway(&conn, NC_H2_FRAME_SIZE_ERROR);
                free(payload);
                goto cleanup;
            }
            if (!(fhdr.flags & NC_H2_FLAG_ACK))
                (void)h2_conn_write_ping_ack(&conn, payload);
            break;

        case NC_H2_FRAME_HEADERS: {
            h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
            if (stream && stream->state == H2_STREAM_CLOSED &&
                !nc_atomic_load(&stream->reset)) {
                if (h2_reject_headers_keep_hpack(
                        &conn, &fhdr, payload, NC_H2_STREAM_CLOSED) != 0) {
                    free(payload);
                    goto cleanup;
                }
                break;
            }
            if (stream &&
                (stream->state == H2_STREAM_HALF_CLOSED_REMOTE ||
                 stream->state == H2_STREAM_CLOSED ||
                 nc_atomic_load(&stream->reset))) {
                if (h2_reject_headers_keep_hpack(
                        &conn, &fhdr, payload, NC_H2_STREAM_CLOSED) != 0) {
                    free(payload);
                    goto cleanup;
                }
                break;
            }
            if (!stream) {
                if ((fhdr.stream_id & 1U) == 0) {
                    (void)h2_conn_write_goaway(&conn,
                                               NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                if (nc_atomic_load(&conn.goaway_sent)) {
                    h2_consume_idle_stream_id(&conn, fhdr.stream_id);
                    if (h2_reject_headers_keep_hpack(
                            &conn, &fhdr, payload,
                            NC_H2_REFUSED_STREAM) != 0) {
                        free(payload);
                        goto cleanup;
                    }
                    break;
                }
                {
                    int found_reset = 0;
                    int was_reset = h2_closed_stream_was_reset(
                        &conn, fhdr.stream_id, &found_reset);
                    if (found_reset && was_reset) {
                        if (h2_reject_headers_keep_hpack(
                                &conn, &fhdr, payload,
                                NC_H2_STREAM_CLOSED) != 0) {
                            free(payload);
                            goto cleanup;
                        }
                        break;
                    }
                }
                if (fhdr.stream_id <= conn.max_stream_id) {
                    int found = 0;
                    int reset = h2_closed_stream_was_reset(
                        &conn, fhdr.stream_id, &found);
                    if (found && reset) {
                        if (h2_reject_headers_keep_hpack(
                                &conn, &fhdr, payload,
                                NC_H2_STREAM_CLOSED) != 0) {
                            free(payload);
                            goto cleanup;
                        }
                        break;
                    }
                    (void)h2_conn_write_goaway(
                        &conn, found ? NC_H2_STREAM_CLOSED
                                     : NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                if (nc_atomic_load(&conn.active_streams) >=
                    (int)conn.local_settings.max_concurrent_streams) {
                    h2_consume_idle_stream_id(&conn, fhdr.stream_id);
                    if (h2_reject_headers_keep_hpack(
                            &conn, &fhdr, payload,
                            NC_H2_REFUSED_STREAM) != 0) {
                        free(payload);
                        goto cleanup;
                    }
                    break;
                }
                stream = h2_create_stream(&conn, fhdr.stream_id);
                if (!stream) {
                    h2_consume_idle_stream_id(&conn, fhdr.stream_id);
                    if (h2_reject_headers_keep_hpack(
                            &conn, &fhdr, payload,
                            NC_H2_REFUSED_STREAM) != 0) {
                        free(payload);
                        goto cleanup;
                    }
                    break;
                }
                conn.max_stream_id = fhdr.stream_id;
            }

            const uint8_t *fragment = NULL;
            size_t fragment_length = 0;
            int fragment_rc = h2_header_fragment(&fhdr, payload, &fragment,
                                                 &fragment_length);
            if (fragment_rc == -2) {
                /* RFC 9113 §6.2: self-dependency is a stream PROTOCOL_ERROR.
                 * Decode the block first (§4.3) so HPACK stays in sync, then
                 * abort so a later DATA/HEADERS cannot succeed. */
                int end_headers =
                    (fhdr.flags & NC_H2_FLAG_END_HEADERS) != 0;
                int end_stream =
                    (fhdr.flags & NC_H2_FLAG_END_STREAM) != 0;
                if (!end_headers) {
                    conn.pending_hdr_stream_id = fhdr.stream_id;
                    conn.pending_hdr_active = 1;
                    conn.pending_hdr_discard = 1;
                    conn.pending_hdr_rst_code = 0;
                    conn.pending_end_stream = end_stream;
                    conn.pending_hdr_continuations = 0;
                    if (h2_append_hdr_block(&conn, fragment,
                                            fragment_length) != 0) {
                        h2_clear_pending_hdr(&conn);
                        (void)h2_conn_write_goaway(
                            &conn, NC_H2_ENHANCE_YOUR_CALM);
                        free(payload);
                        goto cleanup;
                    }
                } else if (h2_decode_header_block_discard(
                               &conn, fragment, fragment_length) < 0) {
                    (void)h2_conn_write_goaway(&conn,
                                               NC_H2_COMPRESSION_ERROR);
                    free(payload);
                    goto cleanup;
                }
                nc_mutex_lock(&conn.state_lock);
                h2_abort_stream_locked(&conn, stream, NC_H2_PROTOCOL_ERROR);
                break;
            }
            if (fragment_rc != 0) {
                (void)h2_conn_write_goaway(&conn, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }

            int end_headers = (fhdr.flags & NC_H2_FLAG_END_HEADERS) != 0;
            int end_stream = (fhdr.flags & NC_H2_FLAG_END_STREAM) != 0;

            if (!end_headers) {
                conn.pending_hdr_stream_id = fhdr.stream_id;
                conn.pending_hdr_active = 1;
                conn.pending_end_stream = end_stream;
                if (h2_append_hdr_block(&conn, fragment,
                                        fragment_length) != 0) {
                    (void)h2_conn_write_goaway(&conn,
                                               NC_H2_ENHANCE_YOUR_CALM);
                    free(payload);
                    goto cleanup;
                }
                break;
            }

            if (conn.pending_hdr_active &&
                conn.pending_hdr_stream_id == fhdr.stream_id) {
                if (h2_append_hdr_block(&conn, fragment,
                                        fragment_length) != 0) {
                    (void)h2_conn_write_goaway(&conn,
                                               NC_H2_ENHANCE_YOUR_CALM);
                    free(payload);
                    goto cleanup;
                }
                int header_result = h2_finish_header_block(&conn, stream);
                if (h2_handle_header_result(&conn, stream,
                                            header_result) != 0) {
                    free(payload);
                    goto cleanup;
                }
                break;
            }

            int header_result = h2_process_header_block(
                &conn, stream, fragment, fragment_length, end_stream);
            if (h2_handle_header_result(&conn, stream, header_result) != 0) {
                free(payload);
                goto cleanup;
            }
            break;
        }

        case NC_H2_FRAME_DATA: {
            const uint8_t *data = NULL;
            size_t data_length = 0;
            if (h2_data_fragment(&fhdr, payload, &data, &data_length) != 0) {
                (void)h2_conn_write_goaway(&conn, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
            /* Unused even IDs stay idle after a higher odd client stream.
             * Unused odd IDs <= max_stream_id are implicitly closed. */
            if (!stream && ((fhdr.stream_id & 1u) == 0 ||
                            fhdr.stream_id > conn.max_stream_id)) {
                (void)h2_conn_write_goaway(&conn, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            nc_mutex_lock(&conn.state_lock);
            h2_stream_state_t stream_state = stream
                ? stream->state : H2_STREAM_CLOSED;
            int accepting_data = stream && stream->headers_complete &&
                !nc_atomic_load(&stream->reset) &&
                (stream_state == H2_STREAM_OPEN ||
                 stream_state == H2_STREAM_HALF_CLOSED_LOCAL);
            /* RFC 9113 §5.1 / §6.9: every DATA payload counts against the
             * connection window, including frames that will be discarded. */
            if (conn.conn_recv_window < 0 ||
                fhdr.length > (uint32_t)conn.conn_recv_window) {
                nc_mutex_unlock(&conn.state_lock);
                (void)h2_conn_write_goaway(&conn,
                                           NC_H2_FLOW_CONTROL_ERROR);
                free(payload);
                goto cleanup;
            }
            conn.conn_recv_window -= (int32_t)fhdr.length;
            if (!accepting_data) {
                if ((int64_t)conn.conn_recv_window +
                        (int64_t)fhdr.length <= INT32_MAX)
                    conn.conn_recv_window += (int32_t)fhdr.length;
                nc_mutex_unlock(&conn.state_lock);
                (void)h2_conn_write_rst(&conn, fhdr.stream_id,
                                        NC_H2_STREAM_CLOSED);
                if (fhdr.length > 0)
                    (void)h2_conn_write_window_update(&conn, 0,
                                                      fhdr.length);
                break;
            }
            if (stream->recv_window < 0 ||
                fhdr.length > (uint32_t)stream->recv_window) {
                if ((int64_t)conn.conn_recv_window +
                        (int64_t)fhdr.length <= INT32_MAX)
                    conn.conn_recv_window += (int32_t)fhdr.length;
                nc_mutex_unlock(&conn.state_lock);
                (void)h2_conn_write_goaway(&conn,
                                           NC_H2_FLOW_CONTROL_ERROR);
                free(payload);
                goto cleanup;
            }
            stream->recv_window -= (int32_t)fhdr.length;
            nc_mutex_unlock(&conn.state_lock);
            if (data_length > 0) {
                int length_error = stream->content_length >= 0 &&
                    ((uint64_t)stream->body_len >
                         (uint64_t)stream->content_length ||
                     (uint64_t)data_length >
                         (uint64_t)stream->content_length -
                             (uint64_t)stream->body_len);
                if (length_error ||
                    data_length > conn.srv->max_body_size ||
                    stream->body_len > conn.srv->max_body_size - data_length) {
                    int streaming = stream->streaming_request;
                    nc_atomic_store(&stream->reset, 1);
                    (void)h2_conn_write_rst(&conn, stream->id,
                                            length_error
                                                ? NC_H2_PROTOCOL_ERROR
                                                : NC_H2_ENHANCE_YOUR_CALM);
                    if (streaming) {
                        neverc_context_cancel_handle_cancel(stream->cancel);
                        (void)neverc_thread_channel_close(
                            stream->receive_queue);
                    } else {
                        h2_remove_stream(&conn, stream->id);
                    }
                    h2_refund_connection_window(&conn, fhdr.length);
                    break;
                }
            }
            if (stream->streaming_request && fhdr.length > 0) {
                (void)h2_conn_write_window_update(
                    &conn, 0, fhdr.length);
                conn.conn_recv_window += (int32_t)fhdr.length;
            } else if (conn.conn_recv_window < 16384) {
                int32_t increment =
                    NC_H2_DEFAULT_INITIAL_WINDOW_SIZE -
                    conn.conn_recv_window;
                if (increment > 0) {
                    (void)h2_conn_write_window_update(
                        &conn, 0, (uint32_t)increment);
                    conn.conn_recv_window += increment;
                }
            }
            if (data_length > 0) {
                if (stream->streaming_request) {
                    h2_inbound_chunk_t *chunk =
                        (h2_inbound_chunk_t *)malloc(
                            sizeof(*chunk) + data_length);
                    if (!chunk) {
                        (void)h2_conn_write_goaway(
                            &conn, NC_H2_INTERNAL_ERROR);
                        free(payload);
                        goto cleanup;
                    }
                    chunk->length = data_length;
                    chunk->offset = 0;
                    memcpy(chunk->data, data, data_length);
                    if (neverc_thread_channel_try_send(
                            stream->receive_queue, chunk) !=
                        NEVERC_THREAD_OK) {
                        free(chunk);
                        nc_atomic_store(&stream->reset, 1);
                        neverc_context_cancel_handle_cancel(stream->cancel);
                        (void)neverc_thread_channel_close(
                            stream->receive_queue);
                        (void)h2_conn_write_rst(
                            &conn, stream->id, NC_H2_ENHANCE_YOUR_CALM);
                        break;
                    }
                    stream->body_len += data_length;
                } else if (h2_buffer_append(
                               &stream->body, &stream->body_len,
                               &stream->body_cap, data,
                               data_length) != 0) {
                    (void)h2_conn_write_goaway(&conn,
                                               NC_H2_INTERNAL_ERROR);
                    free(payload);
                    goto cleanup;
                }

            }
            if (stream->streaming_request) {
                uint32_t overhead = fhdr.length - (uint32_t)data_length;
                if (overhead > 0) {
                    (void)h2_conn_write_window_update(
                        &conn, stream->id, overhead);
                    nc_mutex_lock(&conn.state_lock);
                    stream->recv_window += (int32_t)overhead;
                    nc_mutex_unlock(&conn.state_lock);
                }
            } else if (stream->recv_window < 16384) {
                int32_t inc = (int32_t)conn.local_settings.initial_window_size -
                              stream->recv_window;
                if (inc > 0) {
                    (void)h2_conn_write_window_update(&conn, stream->id,
                                                      (uint32_t)inc);
                    stream->recv_window += inc;
                }
            }
            if (fhdr.flags & NC_H2_FLAG_END_STREAM) {
                if (stream->content_length >= 0 &&
                    (uint64_t)stream->content_length != stream->body_len) {
                    nc_atomic_store(&stream->reset, 1);
                    (void)h2_conn_write_rst(&conn, stream->id,
                                            NC_H2_PROTOCOL_ERROR);
                    if (stream->streaming_request) {
                        nc_atomic_store(&stream->reset, 1);
                        neverc_context_cancel_handle_cancel(stream->cancel);
                        (void)neverc_thread_channel_close(
                            stream->receive_queue);
                    } else {
                        h2_remove_stream(&conn, stream->id);
                    }
                    h2_refund_connection_window(&conn, fhdr.length);
                    break;
                }
                nc_mutex_lock(&conn.state_lock);
                stream->state = H2_STREAM_HALF_CLOSED_REMOTE;
                nc_mutex_unlock(&conn.state_lock);
                nc_atomic_store(&stream->remote_ended, 1);
                if (stream->receive_queue)
                    (void)neverc_thread_channel_close(stream->receive_queue);
                if (!stream->streaming_request)
                    (void)h2_submit_request(&conn, stream);
            }
            break;
        }

        case NC_H2_FRAME_WINDOW_UPDATE: {
            if (fhdr.length != 4) {
                (void)h2_conn_write_goaway(&conn, NC_H2_FRAME_SIZE_ERROR);
                free(payload);
                goto cleanup;
            }
            uint32_t inc = ((uint32_t)(payload[0] & 0x7f) << 24) |
                           ((uint32_t)payload[1] << 16) |
                           ((uint32_t)payload[2] << 8) |
                           (uint32_t)payload[3];
            if (inc == 0) {
                if (fhdr.stream_id == 0) {
                    (void)h2_conn_write_goaway(&conn,
                                               NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                nc_mutex_lock(&conn.state_lock);
                h2_stream_t *zero_stream =
                    h2_find_stream(&conn, fhdr.stream_id);
                if (!zero_stream &&
                    ((fhdr.stream_id & 1u) == 0 ||
                     fhdr.stream_id > conn.max_stream_id)) {
                    nc_mutex_unlock(&conn.state_lock);
                    (void)h2_conn_write_goaway(&conn,
                                               NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                if (zero_stream) {
                    h2_abort_stream_locked(&conn, zero_stream,
                                           NC_H2_PROTOCOL_ERROR);
                    break;
                }
                nc_mutex_unlock(&conn.state_lock);
                (void)h2_conn_write_rst(&conn, fhdr.stream_id,
                                        NC_H2_PROTOCOL_ERROR);
                break;
            }
            nc_mutex_lock(&conn.state_lock);
            if (fhdr.stream_id == 0) {
                if ((int64_t)conn.conn_send_window + inc > INT32_MAX) {
                    nc_mutex_unlock(&conn.state_lock);
                    (void)h2_conn_write_goaway(
                        &conn, NC_H2_FLOW_CONTROL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                conn.conn_send_window += (int32_t)inc;
            } else {
                h2_stream_t *s = h2_find_stream(&conn, fhdr.stream_id);
                if (!s && ((fhdr.stream_id & 1u) == 0 ||
                           fhdr.stream_id > conn.max_stream_id)) {
                    nc_mutex_unlock(&conn.state_lock);
                    (void)h2_conn_write_goaway(
                        &conn, NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                } else if (s &&
                           (int64_t)s->send_window + inc <= INT32_MAX) {
                    s->send_window += (int32_t)inc;
                } else if (s) {
                    /* RFC 9113 §6.9.1: stream window overflow is RST_STREAM,
                     * not a connection error. The stream must still close so
                     * later DATA cannot be dispatched as a successful request. */
                    h2_abort_stream_locked(&conn, s,
                                           NC_H2_FLOW_CONTROL_ERROR);
                    break;
                }
            }
            nc_cond_broadcast(&conn.window_changed);
            nc_mutex_unlock(&conn.state_lock);
            break;
        }

        case NC_H2_FRAME_RST_STREAM: {
            if (fhdr.stream_id == 0 || fhdr.length != 4) {
                (void)h2_conn_write_goaway(&conn, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
            if (!stream && ((fhdr.stream_id & 1u) == 0 ||
                            fhdr.stream_id > conn.max_stream_id)) {
                (void)h2_conn_write_goaway(&conn, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            if (stream) {
                nc_mutex_lock(&conn.state_lock);
                nc_atomic_store(&stream->reset, 1);
                neverc_context_cancel_handle_cancel(stream->cancel);
                int handler_active = stream->handler_active;
                nc_cond_broadcast(&conn.window_changed);
                nc_mutex_unlock(&conn.state_lock);
                if (!handler_active)
                    h2_remove_stream(&conn, fhdr.stream_id);
            }
            break;
        }

        case NC_H2_FRAME_GOAWAY: {
            conn.peer_goaway_received = 1;
            break;
        }

        case NC_H2_FRAME_PRIORITY: {
            uint32_t dependency =
                ((uint32_t)(payload[0] & 0x7f) << 24) |
                ((uint32_t)payload[1] << 16) |
                ((uint32_t)payload[2] << 8) |
                (uint32_t)payload[3];
            if (dependency != fhdr.stream_id)
                break;
            /* RFC 9113 §6.3: self-dependency is a stream PROTOCOL_ERROR.
             * RST alone is not enough — idle IDs must be consumed so a
             * later HEADERS cannot succeed, and open streams must abort
             * so later DATA cannot be dispatched. */
            nc_mutex_lock(&conn.state_lock);
            h2_stream_t *pri_stream =
                h2_find_stream(&conn, fhdr.stream_id);
            if (pri_stream) {
                h2_abort_stream_locked(&conn, pri_stream,
                                       NC_H2_PROTOCOL_ERROR);
                break;
            }
            if (fhdr.stream_id > conn.max_stream_id)
                h2_remember_reset_stream_id(&conn, fhdr.stream_id);
            nc_mutex_unlock(&conn.state_lock);
            (void)h2_conn_write_rst(&conn, fhdr.stream_id,
                                    NC_H2_PROTOCOL_ERROR);
            break;
        }

        case NC_H2_FRAME_CONTINUATION: {
            if (fhdr.stream_id == 0 ||
                !conn.pending_hdr_active ||
                fhdr.stream_id != conn.pending_hdr_stream_id) {
                (void)h2_conn_write_goaway(&conn, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            if (h2_append_hdr_block(&conn, payload, fhdr.length) != 0 ||
                ++conn.pending_hdr_continuations >
                    H2_MAX_CONTINUATION_FRAMES) {
                (void)h2_conn_write_goaway(&conn, NC_H2_ENHANCE_YOUR_CALM);
                free(payload);
                goto cleanup;
            }
            if (fhdr.flags & NC_H2_FLAG_END_HEADERS) {
                if (conn.pending_hdr_discard) {
                    if (h2_finish_discarded_header_block(&conn) != 0) {
                        free(payload);
                        goto cleanup;
                    }
                    break;
                }
                h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
                if (!stream) {
                    (void)h2_conn_write_goaway(&conn,
                                               NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                int header_result = h2_finish_header_block(&conn, stream);
                if (h2_handle_header_result(&conn, stream,
                                            header_result) != 0) {
                    free(payload);
                    goto cleanup;
                }
            }
            break;
        }

        default:
            break;
        }

        free(payload);
    }

cleanup:
    nc_atomic_store(&conn.running, 0);
    nc_mutex_lock(&conn.state_lock);
    for (h2_stream_t *stream = conn.streams; stream; stream = stream->next) {
        nc_atomic_store(&stream->reset, 1);
        if (stream->cancel)
            neverc_context_cancel_handle_cancel(stream->cancel);
        if (stream->receive_queue)
            (void)neverc_thread_channel_close(stream->receive_queue);
    }
    nc_cond_broadcast(&conn.window_changed);
    nc_mutex_unlock(&conn.state_lock);
    if (preface_received && !nc_atomic_load(&conn.goaway_sent))
        (void)h2_conn_write_goaway(&conn, NC_H2_NO_ERROR);
    /* Unblock a handler stuck in send() before waiting for it. */
    h2_io_shutdown(&conn.io);
    nc_mutex_lock(&conn.state_lock);
    while (conn.active_handlers > 0)
        nc_cond_wait(&conn.handlers_done, &conn.state_lock);
    nc_mutex_unlock(&conn.state_lock);
    while (conn.streams) {
        h2_stream_t *next = conn.streams->next;
        h2_close_stream(&conn, conn.streams);
        free(conn.streams);
        conn.streams = next;
    }
    h2_clear_pending_hdr(&conn);
    if (conn.hpack_dec)
        neverc_hpack_decoder_destroy(conn.hpack_dec);
    if (conn.hpack_enc)
        neverc_hpack_encoder_destroy(conn.hpack_enc);
    if (nc_atomic_load(&conn.goaway_sent) &&
        conn.goaway_error_code != NC_H2_NO_ERROR)
        h2_io_drain_after_error(&conn.io);
    if (registered)
        h2_server_remove_connection(srv, &conn);
    nc_cond_destroy(&conn.handlers_done);
    nc_cond_destroy(&conn.window_changed);
    nc_mutex_destroy(&conn.write_lock);
    nc_mutex_destroy(&conn.state_lock);
    return 0;
}

int neverc_h2_serve_conn(neverc_h2_server_t *srv,
                         uintptr_t socket_handle) {
#ifdef _WIN32
    if (!srv || socket_handle == (uintptr_t)INVALID_SOCKET)
        return -1;
    nc_sock_t fd = (nc_sock_t)socket_handle;
#else
    if (!srv || socket_handle > (uintptr_t)INT_MAX)
        return -1;
    nc_sock_t fd = (nc_sock_t)socket_handle;
#endif
    h2_io_t io;
    h2_io_init_socket(&io, fd);
    return h2_serve_io(srv, &io);
}

int neverc_h2_serve_tls_conn(neverc_h2_server_t *srv, neverc_tls_conn_t *tls) {
    if (!srv || !tls)
        return -1;
    h2_io_t io;
    h2_io_init_tls(&io, tls);
    return h2_serve_io(srv, &io);
}

void neverc_h2_server_stop(void) {
    neverc_h2_server_t *server = g_legacy_h2_server;
    if (server)
        neverc_h2_server_shutdown(server);
}

typedef struct {
    neverc_h2_server_t *server;
    neverc_tcp_conn_t *tcp;
    neverc_tls_conn_t *tls;
    int use_tls;
    nc_http_h2_connection_done_func_t done;
    void *done_context;
} h2_connection_task_t;

static void h2_connection_task_done(neverc_h2_server_t *server) {
    nc_mutex_lock(&server->lifecycle_lock);
    if (server->pending_connection_tasks > 0)
        server->pending_connection_tasks--;
    nc_cond_broadcast(&server->lifecycle_changed);
    nc_mutex_unlock(&server->lifecycle_lock);
}

static void h2_connection_task_run(void *argument) {
    h2_connection_task_t *task = (h2_connection_task_t *)argument;
    neverc_h2_server_t *server = task->server;
    neverc_tcp_conn_t *tcp = task->tcp;
    neverc_tls_conn_t *tls = task->tls;
    int use_tls = task->use_tls;
    nc_http_h2_connection_done_func_t done = task->done;
    void *done_context = task->done_context;
    free(task);

    if (nc_atomic_load(&server->running) &&
        !nc_atomic_load(&server->destroying)) {
        if (tls) {
            (void)neverc_h2_serve_tls_conn(server, tls);
        } else if (use_tls) {
            const char *tls_error = NULL;
            tls = neverc_tls_server(
                tcp, server->tls_config, &tls_error);
            (void)tls_error;
            if (tls) {
                const char *protocol = neverc_tls_alpn(tls);
                if (protocol && strcmp(protocol, "h2") == 0)
                    (void)neverc_h2_serve_tls_conn(server, tls);
                neverc_tls_close(tls);
                tls = NULL;
            }
        } else {
            h2_io_t io;
            h2_io_init_tcp(&io, tcp);
            (void)h2_serve_io(server, &io);
        }
    }
    if (tls) neverc_tls_close(tls);
    neverc_tcp_close(tcp);
    if (done) done(done_context);
    h2_connection_task_done(server);
}

static int h2_submit_connection(neverc_h2_server_t *server,
                                neverc_tcp_conn_t *tcp, int use_tls) {
    h2_connection_task_t *task =
        (h2_connection_task_t *)calloc(1, sizeof(*task));
    if (!task) return -1;
    task->server = server;
    task->tcp = tcp;
    task->use_tls = use_tls;
    nc_mutex_lock(&server->lifecycle_lock);
    if (!nc_atomic_load(&server->running) ||
        server->active_connections + server->pending_connection_tasks >=
            1024) {
        nc_mutex_unlock(&server->lifecycle_lock);
        free(task);
        return -1;
    }
    server->pending_connection_tasks++;
    nc_mutex_unlock(&server->lifecycle_lock);
    int submitted = neverc_thread_executor_try_submit(
        server->connection_executor, h2_connection_task_run, task);
    if (submitted == NEVERC_THREAD_OK) return 0;
    free(task);
    h2_connection_task_done(server);
    return -1;
}

int nc_h2_server_start_embedded(neverc_h2_server_t *server) {
    if (!server) return -1;
    nc_mutex_lock(&server->lifecycle_lock);
    if (nc_atomic_load(&server->destroying) ||
        nc_atomic_load(&server->serving)) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return -1;
    }
    if (!server->connection_executor) {
        server->connection_executor =
            neverc_thread_executor_create(32, 1024);
        if (!server->connection_executor) {
            nc_mutex_unlock(&server->lifecycle_lock);
            return -1;
        }
    }
    nc_atomic_store(&server->running, 1);
    nc_mutex_unlock(&server->lifecycle_lock);
    return 0;
}

int nc_h2_server_submit_tls(
    neverc_h2_server_t *server, neverc_tls_conn_t *tls,
    neverc_tcp_conn_t *tcp, nc_http_h2_connection_done_func_t done,
    void *done_context) {
    if (!server || !tls || !tcp) return -1;
    h2_connection_task_t *task =
        (h2_connection_task_t *)calloc(1, sizeof(*task));
    if (!task) return -1;
    task->server = server;
    task->tcp = tcp;
    task->tls = tls;
    task->done = done;
    task->done_context = done_context;

    nc_mutex_lock(&server->lifecycle_lock);
    if (!nc_atomic_load(&server->running) ||
        nc_atomic_load(&server->destroying) ||
        !server->connection_executor ||
        server->active_connections + server->pending_connection_tasks >=
            1024) {
        nc_mutex_unlock(&server->lifecycle_lock);
        free(task);
        return -1;
    }
    server->pending_connection_tasks++;
    nc_mutex_unlock(&server->lifecycle_lock);
    int submitted = neverc_thread_executor_try_submit(
        server->connection_executor, h2_connection_task_run, task);
    if (submitted == NEVERC_THREAD_OK) return 0;
    free(task);
    h2_connection_task_done(server);
    return -1;
}

static int h2_server_serve(neverc_h2_server_t *server, const char *addr,
                           const char *cert_file, const char *key_file) {
    if (!server || !addr || ((cert_file == NULL) != (key_file == NULL)) ||
        nc_atomic_load(&server->destroying) ||
        !nc_atomic_cas(&server->serving, 0, 1))
        return -1;
    int result = -1;
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *context = NULL;
    neverc_context_t *serve_background = NULL;
    neverc_tls_config_t *tls_config = NULL;
    neverc_tcp_listener_t *listener = NULL;
    if (!server->connection_executor) {
        server->connection_executor =
            neverc_thread_executor_create(32, 1024);
        if (!server->connection_executor) goto cleanup;
    }
    serve_background = neverc_context_background();
    if (!serve_background) goto cleanup;
    context = neverc_context_with_cancel_handle(
        serve_background, &cancel);
    if (!context || !cancel) goto cleanup;

    if (cert_file) {
        tls_config = neverc_tls_config_new();
        if (!tls_config ||
            neverc_tls_config_load_cert(tls_config, cert_file, key_file) != 0)
            goto cleanup;
        const char *alpn[] = {"h2"};
        neverc_tls_config_set_alpn(tls_config, alpn, 1);
    }

    const char *listen_error = NULL;
    listener = neverc_tcp_listen(addr, &listen_error);
    (void)listen_error;
    if (!listener) goto cleanup;

    nc_mutex_lock(&server->lifecycle_lock);
    if (nc_atomic_load(&server->destroying)) {
        nc_mutex_unlock(&server->lifecycle_lock);
        goto cleanup;
    }
    server->serve_context = context;
    server->serve_cancel = cancel;
    server->listener = listener;
    server->tls_config = tls_config;
    g_legacy_h2_server = server;
    nc_atomic_store(&server->running, 1);
    nc_mutex_unlock(&server->lifecycle_lock);
    context = NULL;
    cancel = NULL;
    tls_config = NULL;
    listener = NULL;

    while (nc_atomic_load(&server->running)) {
        neverc_tcp_conn_t *tcp = NULL;
        neverc_net_result_t accepted = neverc_tcp_accept_context(
            server->listener, server->serve_context, &tcp);
        if (accepted.status != NEVERC_NET_OK || !tcp) {
            if (!nc_atomic_load(&server->running) ||
                neverc_context_done(server->serve_context))
                break;
            goto cleanup;
        }
        (void)neverc_tcp_set_timeout(tcp, 30000);
        if (h2_submit_connection(server, tcp, cert_file != NULL) != 0)
            neverc_tcp_close(tcp);
    }
    result = 0;

cleanup:
    neverc_h2_server_shutdown(server);
    nc_mutex_lock(&server->lifecycle_lock);
    while (server->active_connections > 0 ||
           server->pending_connection_tasks > 0)
        nc_cond_wait(&server->lifecycle_changed, &server->lifecycle_lock);
    neverc_tcp_listener_t *owned_listener = server->listener;
    neverc_context_t *owned_context = server->serve_context;
    neverc_context_cancel_handle_t *owned_cancel = server->serve_cancel;
    neverc_tls_config_t *owned_tls_config = server->tls_config;
    server->listener = NULL;
    server->serve_context = NULL;
    server->serve_cancel = NULL;
    server->tls_config = NULL;
    if (g_legacy_h2_server == server)
        g_legacy_h2_server = NULL;
    nc_atomic_store(&server->serving, 0);
    nc_cond_broadcast(&server->lifecycle_changed);
    nc_mutex_unlock(&server->lifecycle_lock);
    if (owned_listener) neverc_tcp_listener_close(owned_listener);
    if (owned_cancel) neverc_context_cancel_handle_free(owned_cancel);
    if (owned_context) neverc_context_free(owned_context);
    neverc_context_free(serve_background);
    neverc_tls_config_free(owned_tls_config);
    if (listener) neverc_tcp_listener_close(listener);
    if (cancel) neverc_context_cancel_handle_free(cancel);
    if (context) neverc_context_free(context);
    neverc_tls_config_free(tls_config);
    return result;
}

int neverc_h2_listen_and_serve_h2c(const char *addr,
                                    neverc_h2_server_t *server) {
    return h2_server_serve(server, addr, NULL, NULL);
}

int neverc_h2_listen_and_serve(const char *addr,
                                neverc_h2_server_t *server,
                                const char *cert_file,
                                const char *key_file) {
    if (!cert_file || !key_file) return -1;
    return h2_server_serve(server, addr, cert_file, key_file);
}
