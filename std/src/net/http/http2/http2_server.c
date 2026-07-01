/*
 * NeverC HTTP/2 Server — Frame handling, stream state machine, server loop.
 * Split from http2.c (HPACK) to avoid large-TU compiler issue.
 */
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/crypto/tls.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int ssize_t;
#else
  #include <sys/socket.h>
  #include <unistd.h>
  #ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
  #endif
#endif

/* ======================================================================
 * HTTP/2 Frame Header Parse/Write
 * ====================================================================== */

int neverc_h2_frame_header_read(const uint8_t *data, size_t len,
                                  neverc_h2_frame_header_t *hdr) {
    if (len < NC_H2_FRAME_HEADER_SIZE) return -1;
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
    s->header_table_size = NC_H2_DEFAULT_HEADER_TABLE_SIZE;
    s->enable_push = 1;
    s->max_concurrent_streams = NC_H2_DEFAULT_MAX_CONCURRENT;
    s->initial_window_size = NC_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    s->max_frame_size = NC_H2_DEFAULT_MAX_FRAME_SIZE;
    s->max_header_list_size = NC_H2_DEFAULT_MAX_HEADER_LIST_SIZE;
}

/* ======================================================================
 * HTTP/2 Server Stub
 *
 * Full server implementation handles connection preface, settings
 * exchange, stream multiplexing, and request dispatch.
 * ====================================================================== */

struct neverc_h2_server {
    neverc_http_mux_t    *mux;
    neverc_h2_settings_t  settings;
};

neverc_h2_server_t *neverc_h2_server_create(neverc_http_mux_t *mux) {
    neverc_h2_server_t *srv = (neverc_h2_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    srv->mux = mux;
    neverc_h2_settings_init(&srv->settings);
    return srv;
}

void neverc_h2_server_destroy(neverc_h2_server_t *srv) {
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
    uint32_t           id;
    h2_stream_state_t  state;
    int32_t            recv_window;
    int32_t            send_window;

    neverc_hpack_header_t headers[64];
    int                nheaders;
    uint8_t           *body;
    size_t             body_len;
    size_t             body_cap;

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
    int                  fd;
    neverc_tcp_conn_t   *tcp;
    neverc_tls_conn_t   *tls;
} h2_io_t;

typedef struct {
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
    int      goaway_sent;

    h2_stream_t *streams;
    int           active_streams;

    uint8_t  *pending_hdr_block;
    size_t    pending_hdr_len;
    size_t    pending_hdr_cap;
    uint32_t  pending_hdr_stream_id;
    int       pending_hdr_active;
    int       pending_end_stream;
} h2_conn_t;

static void h2_io_init_socket(h2_io_t *io, int fd) {
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

static int h2_io_read_all(h2_io_t *io, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t got = 0;
    while (got < len) {
        int n = -1;
        switch (io->kind) {
        case H2_IO_SOCKET:
#ifdef _WIN32
            n = recv(io->fd, p + got, (int)(len - got), 0);
#else
            n = (int)recv(io->fd, p + got, len - got, 0);
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

static int h2_io_write_all(h2_io_t *io, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        int n = -1;
        switch (io->kind) {
        case H2_IO_SOCKET:
#ifdef _WIN32
            n = send(io->fd, p + sent, (int)(len - sent), 0);
#else
            n = (int)send(io->fd, p + sent, len - sent, MSG_NOSIGNAL);
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

static int h2_write_frame(h2_io_t *io, uint8_t type, uint8_t flags,
                            uint32_t stream_id, const void *payload, uint32_t len) {
    neverc_h2_frame_header_t hdr = { .length = len, .type = type,
                                      .flags = flags, .stream_id = stream_id };
    uint8_t hbuf[NC_H2_FRAME_HEADER_SIZE];
    neverc_h2_frame_header_write(&hdr, hbuf);
    if (h2_io_write_all(io, hbuf, NC_H2_FRAME_HEADER_SIZE) != 0) return -1;
    if (len > 0 && payload)
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
    uint8_t payload[4];
    payload[0] = (uint8_t)((increment >> 24) & 0x7f);
    payload[1] = (uint8_t)(increment >> 16);
    payload[2] = (uint8_t)(increment >> 8);
    payload[3] = (uint8_t)(increment);
    return h2_write_frame(io, NC_H2_FRAME_WINDOW_UPDATE, 0, stream_id, payload, 4);
}

static void h2_clear_pending_hdr(h2_conn_t *conn) {
    free(conn->pending_hdr_block);
    conn->pending_hdr_block = NULL;
    conn->pending_hdr_len = 0;
    conn->pending_hdr_cap = 0;
    conn->pending_hdr_stream_id = 0;
    conn->pending_hdr_active = 0;
    conn->pending_end_stream = 0;
}

static int h2_append_hdr_block(h2_conn_t *conn, const uint8_t *data, size_t len) {
    size_t new_len = conn->pending_hdr_len + len;
    if (new_len > conn->pending_hdr_cap) {
        size_t cap = conn->pending_hdr_cap ? conn->pending_hdr_cap : 4096;
        while (cap < new_len)
            cap *= 2;
        uint8_t *p = (uint8_t *)realloc(conn->pending_hdr_block, cap);
        if (!p)
            return -1;
        conn->pending_hdr_block = p;
        conn->pending_hdr_cap = cap;
    }
    memcpy(conn->pending_hdr_block + conn->pending_hdr_len, data, len);
    conn->pending_hdr_len = new_len;
    return 0;
}

static h2_stream_t *h2_find_stream(h2_conn_t *conn, uint32_t id);
static h2_stream_t *h2_create_stream(h2_conn_t *conn, uint32_t id);
static void h2_close_stream(h2_conn_t *conn, h2_stream_t *s);
static void h2_remove_stream(h2_conn_t *conn, uint32_t id);
static void h2_dispatch_request(h2_conn_t *conn, h2_stream_t *stream);

static int h2_process_header_block(h2_conn_t *conn, h2_stream_t *stream,
                                    const uint8_t *block, size_t block_len,
                                    int end_stream) {
    int nh;
    if (neverc_hpack_decode(conn->hpack_dec, block, block_len,
                             stream->headers, 64, &nh) != 0)
        return -1;
    stream->nheaders = nh;
    if (end_stream) {
        stream->state = H2_STREAM_HALF_CLOSED_REMOTE;
        h2_dispatch_request(conn, stream);
        h2_remove_stream(conn, stream->id);
    }
    return 0;
}

static int h2_finish_header_block(h2_conn_t *conn, h2_stream_t *stream) {
    if (h2_process_header_block(conn, stream, conn->pending_hdr_block,
                                 conn->pending_hdr_len,
                                 conn->pending_end_stream) != 0)
        return -1;
    h2_clear_pending_hdr(conn);
    return 0;
}

static h2_stream_t *h2_find_stream(h2_conn_t *conn, uint32_t id) {
    for (h2_stream_t *s = conn->streams; s; s = s->next)
        if (s->id == id) return s;
    return NULL;
}

static h2_stream_t *h2_create_stream(h2_conn_t *conn, uint32_t id) {
    h2_stream_t *s = (h2_stream_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->id = id;
    s->state = H2_STREAM_OPEN;
    s->recv_window = (int32_t)conn->local_settings.initial_window_size;
    s->send_window = (int32_t)conn->peer_settings.initial_window_size;
    s->next = conn->streams;
    conn->streams = s;
    conn->active_streams++;
    if (id > conn->last_stream_id)
        conn->last_stream_id = id;
    return s;
}

static void h2_close_stream(h2_conn_t *conn, h2_stream_t *s) {
    s->state = H2_STREAM_CLOSED;
    for (int i = 0; i < s->nheaders; i++) {
        free(s->headers[i].name);
        free(s->headers[i].value);
    }
    free(s->body);
    s->nheaders = 0;
    s->body = NULL;
    s->body_len = 0;
    conn->active_streams--;
}

static void h2_remove_stream(h2_conn_t *conn, uint32_t id) {
    h2_stream_t **pp = &conn->streams;
    while (*pp) {
        if ((*pp)->id == id) {
            h2_stream_t *s = *pp;
            *pp = s->next;
            h2_close_stream(conn, s);
            free(s);
            return;
        }
        pp = &(*pp)->next;
    }
}

static void h2_dispatch_request(h2_conn_t *conn, h2_stream_t *stream) {
    const char *method = NULL, *path = NULL, *scheme = NULL, *authority = NULL;
    for (int i = 0; i < stream->nheaders; i++) {
        if (strcmp(stream->headers[i].name, ":method") == 0)
            method = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":path") == 0)
            path = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":scheme") == 0)
            scheme = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":authority") == 0)
            authority = stream->headers[i].value;
    }
    if (!method || !path) {
        h2_write_rst_stream(&conn->io, stream->id, NC_H2_PROTOCOL_ERROR);
        return;
    }

    /* Build HEADERS response via HPACK */
    neverc_hpack_header_t resp_hdrs[8];
    int resp_count = 0;
    resp_hdrs[resp_count++] = (neverc_hpack_header_t){
        .name = ":status", .value = "200", .sensitive = 0};
    resp_hdrs[resp_count++] = (neverc_hpack_header_t){
        .name = "content-type", .value = "text/plain", .sensitive = 0};
    resp_hdrs[resp_count++] = (neverc_hpack_header_t){
        .name = "server", .value = "NeverC/1.0", .sensitive = 0};

    uint8_t hdr_buf[4096];
    size_t hdr_len;
    neverc_hpack_encode(conn->hpack_enc, resp_hdrs, resp_count,
                         hdr_buf, sizeof(hdr_buf), &hdr_len);

    /* Send HEADERS frame */
    h2_write_frame(&conn->io, NC_H2_FRAME_HEADERS,
                    NC_H2_FLAG_END_HEADERS,
                    stream->id, hdr_buf, (uint32_t)hdr_len);

    /* Generate response body */
    char body[512];
    int body_len = snprintf(body, sizeof(body),
                             "Hello from NeverC HTTP/2!\n"
                             "Method: %s\nPath: %s\n",
                             method, path);

    /* Send DATA frame with END_STREAM */
    h2_write_frame(&conn->io, NC_H2_FRAME_DATA,
                    NC_H2_FLAG_END_STREAM,
                    stream->id, body, (uint32_t)body_len);

    stream->state = H2_STREAM_HALF_CLOSED_LOCAL;

    (void)scheme; (void)authority;
}

static int h2_process_settings(h2_conn_t *conn, const uint8_t *payload, uint32_t len) {
    if (len % 6 != 0) return -1;
    for (uint32_t i = 0; i < len; i += 6) {
        uint16_t id = (uint16_t)((payload[i] << 8) | payload[i + 1]);
        uint32_t val = ((uint32_t)payload[i + 2] << 24) |
                       ((uint32_t)payload[i + 3] << 16) |
                       ((uint32_t)payload[i + 4] << 8) |
                       (uint32_t)payload[i + 5];
        switch (id) {
        case NC_H2_SETTINGS_HEADER_TABLE_SIZE:
            conn->peer_settings.header_table_size = val;
            break;
        case NC_H2_SETTINGS_ENABLE_PUSH:
            conn->peer_settings.enable_push = (int)val;
            break;
        case NC_H2_SETTINGS_MAX_CONCURRENT_STREAMS:
            conn->peer_settings.max_concurrent_streams = val;
            break;
        case NC_H2_SETTINGS_INITIAL_WINDOW_SIZE:
            if (val > 0x7fffffff) return -1;
            conn->peer_settings.initial_window_size = val;
            break;
        case NC_H2_SETTINGS_MAX_FRAME_SIZE:
            if (val < 16384 || val > 16777215) return -1;
            conn->peer_settings.max_frame_size = val;
            break;
        case NC_H2_SETTINGS_MAX_HEADER_LIST_SIZE:
            conn->peer_settings.max_header_list_size = val;
            break;
        default:
            break; /* unknown settings are ignored */
        }
    }
    return 0;
}

static int h2_serve_io(neverc_h2_server_t *srv, h2_io_t *io) {
    if (!srv || !io)
        return -1;

    char preface[NC_H2_CLIENT_PREFACE_LEN];
    if (h2_io_read_all(io, preface, NC_H2_CLIENT_PREFACE_LEN) != 0)
        return -1;
    if (memcmp(preface, NC_H2_CLIENT_PREFACE, NC_H2_CLIENT_PREFACE_LEN) != 0)
        return -1;

    h2_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.io = *io;
    conn.srv = srv;
    conn.local_settings = srv->settings;
    neverc_h2_settings_init(&conn.peer_settings);
    conn.conn_recv_window = (int32_t)conn.local_settings.initial_window_size;
    conn.conn_send_window = (int32_t)conn.peer_settings.initial_window_size;
    conn.hpack_dec = neverc_hpack_decoder_create(conn.local_settings.header_table_size);
    conn.hpack_enc = neverc_hpack_encoder_create(conn.peer_settings.header_table_size);
    if (!conn.hpack_dec || !conn.hpack_enc)
        goto cleanup;

    if (h2_write_settings(&conn.io, &conn.local_settings) != 0)
        goto cleanup;

    for (;;) {
        uint8_t frame_hdr_buf[NC_H2_FRAME_HEADER_SIZE];
        if (h2_io_read_all(&conn.io, frame_hdr_buf, NC_H2_FRAME_HEADER_SIZE) != 0)
            break;

        neverc_h2_frame_header_t fhdr;
        neverc_h2_frame_header_read(frame_hdr_buf, NC_H2_FRAME_HEADER_SIZE, &fhdr);

        if (fhdr.length > conn.local_settings.max_frame_size) {
            h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_FRAME_SIZE_ERROR);
            break;
        }

        uint8_t *payload = NULL;
        if (fhdr.length > 0) {
            payload = (uint8_t *)malloc(fhdr.length);
            if (!payload)
                break;
            if (h2_io_read_all(&conn.io, payload, fhdr.length) != 0) {
                free(payload);
                break;
            }
        }

        switch (fhdr.type) {
        case NC_H2_FRAME_SETTINGS:
            if (fhdr.flags & NC_H2_FLAG_ACK) {
                /* SETTINGS ACK */
            } else {
                if (h2_process_settings(&conn, payload, fhdr.length) != 0) {
                    h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                h2_write_settings_ack(&conn.io);
            }
            break;

        case NC_H2_FRAME_PING:
            if (fhdr.length != 8) {
                h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_FRAME_SIZE_ERROR);
                free(payload);
                goto cleanup;
            }
            if (!(fhdr.flags & NC_H2_FLAG_ACK))
                h2_write_ping_ack(&conn.io, payload);
            break;

        case NC_H2_FRAME_HEADERS: {
            if (fhdr.stream_id == 0) {
                h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }

            h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
            if (!stream) {
                if ((int)conn.active_streams >= (int)conn.local_settings.max_concurrent_streams) {
                    h2_write_rst_stream(&conn.io, fhdr.stream_id, NC_H2_REFUSED_STREAM);
                    break;
                }
                stream = h2_create_stream(&conn, fhdr.stream_id);
                if (!stream)
                    break;
            }

            int end_headers = (fhdr.flags & NC_H2_FLAG_END_HEADERS) != 0;
            int end_stream = (fhdr.flags & NC_H2_FLAG_END_STREAM) != 0;

            if (!end_headers) {
                conn.pending_hdr_stream_id = fhdr.stream_id;
                conn.pending_hdr_active = 1;
                conn.pending_end_stream = end_stream;
                if (h2_append_hdr_block(&conn, payload, fhdr.length) != 0) {
                    h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_INTERNAL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                break;
            }

            if (conn.pending_hdr_active &&
                conn.pending_hdr_stream_id == fhdr.stream_id) {
                if (h2_append_hdr_block(&conn, payload, fhdr.length) != 0) {
                    h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_INTERNAL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                end_stream = conn.pending_end_stream;
                if (h2_finish_header_block(&conn, stream) != 0) {
                    h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_COMPRESSION_ERROR);
                    free(payload);
                    goto cleanup;
                }
                break;
            }

            if (h2_process_header_block(&conn, stream, payload, fhdr.length,
                                         end_stream) != 0) {
                h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_COMPRESSION_ERROR);
                free(payload);
                goto cleanup;
            }
            break;
        }

        case NC_H2_FRAME_DATA: {
            h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
            if (!stream) {
                h2_write_rst_stream(&conn.io, fhdr.stream_id, NC_H2_STREAM_CLOSED);
                break;
            }
            if (fhdr.length > 0) {
                size_t new_len = stream->body_len + fhdr.length;
                if (new_len > stream->body_cap) {
                    size_t new_cap = stream->body_cap < 4096 ? 4096 : stream->body_cap * 2;
                    while (new_cap < new_len)
                        new_cap *= 2;
                    stream->body = (uint8_t *)realloc(stream->body, new_cap);
                    stream->body_cap = new_cap;
                }
                memcpy(stream->body + stream->body_len, payload, fhdr.length);
                stream->body_len = new_len;

                conn.conn_recv_window -= (int32_t)fhdr.length;
                stream->recv_window -= (int32_t)fhdr.length;

                if (conn.conn_recv_window < 16384) {
                    int32_t inc = (int32_t)conn.local_settings.initial_window_size -
                                  conn.conn_recv_window;
                    h2_write_window_update(&conn.io, 0, (uint32_t)inc);
                    conn.conn_recv_window += inc;
                }
                if (stream->recv_window < 16384) {
                    int32_t inc = (int32_t)conn.local_settings.initial_window_size -
                                  stream->recv_window;
                    h2_write_window_update(&conn.io, stream->id, (uint32_t)inc);
                    stream->recv_window += inc;
                }
            }
            if (fhdr.flags & NC_H2_FLAG_END_STREAM) {
                stream->state = H2_STREAM_HALF_CLOSED_REMOTE;
                h2_dispatch_request(&conn, stream);
                h2_remove_stream(&conn, stream->id);
            }
            break;
        }

        case NC_H2_FRAME_WINDOW_UPDATE: {
            if (fhdr.length != 4) {
                h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_FRAME_SIZE_ERROR);
                free(payload);
                goto cleanup;
            }
            uint32_t inc = ((uint32_t)(payload[0] & 0x7f) << 24) |
                           ((uint32_t)payload[1] << 16) |
                           ((uint32_t)payload[2] << 8) |
                           (uint32_t)payload[3];
            if (inc == 0) {
                if (fhdr.stream_id == 0)
                    h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                else
                    h2_write_rst_stream(&conn.io, fhdr.stream_id, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            if (fhdr.stream_id == 0)
                conn.conn_send_window += (int32_t)inc;
            else {
                h2_stream_t *s = h2_find_stream(&conn, fhdr.stream_id);
                if (s)
                    s->send_window += (int32_t)inc;
            }
            break;
        }

        case NC_H2_FRAME_RST_STREAM: {
            if (fhdr.stream_id == 0 || fhdr.length != 4) {
                h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            h2_remove_stream(&conn, fhdr.stream_id);
            break;
        }

        case NC_H2_FRAME_GOAWAY: {
            conn.goaway_sent = 1;
            free(payload);
            goto cleanup;
        }

        case NC_H2_FRAME_PRIORITY:
            break;

        case NC_H2_FRAME_CONTINUATION: {
            if (fhdr.stream_id == 0 ||
                !conn.pending_hdr_active ||
                fhdr.stream_id != conn.pending_hdr_stream_id) {
                h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            if (h2_append_hdr_block(&conn, payload, fhdr.length) != 0) {
                h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_INTERNAL_ERROR);
                free(payload);
                goto cleanup;
            }
            if (fhdr.flags & NC_H2_FLAG_END_HEADERS) {
                h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
                if (!stream) {
                    h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                if (h2_finish_header_block(&conn, stream) != 0) {
                    h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_COMPRESSION_ERROR);
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
    if (!conn.goaway_sent)
        h2_write_goaway(&conn.io, conn.last_stream_id, NC_H2_NO_ERROR);
    return 0;
}

int neverc_h2_serve_conn(neverc_h2_server_t *srv, int fd) {
#ifdef _WIN32
    if (!srv || fd == (int)INVALID_SOCKET)
        return -1;
#else
    if (!srv || fd < 0)
        return -1;
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

static volatile int g_h2_server_running = 1;

void neverc_h2_server_stop(void) {
    g_h2_server_running = 0;
}

int neverc_h2_listen_and_serve_h2c(const char *addr, neverc_h2_server_t *srv) {
    if (!addr || !srv)
        return -1;

    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen(addr, &err);
    if (!ln)
        return -1;

    g_h2_server_running = 1;
    while (g_h2_server_running) {
        neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
        if (!conn) {
            if (!g_h2_server_running)
                break;
            continue;
        }
        h2_io_t io;
        h2_io_init_tcp(&io, conn);
        h2_serve_io(srv, &io);
        neverc_tcp_close(conn);
    }

    neverc_tcp_listener_close(ln);
    return 0;
}

int neverc_h2_listen_and_serve(const char *addr,
                                neverc_h2_server_t *srv,
                                const char *cert_file,
                                const char *key_file) {
    if (!addr || !srv || !cert_file || !key_file)
        return -1;

    neverc_tls_config_t *cfg = neverc_tls_config_new();
    if (!cfg)
        return -1;

    if (neverc_tls_config_load_cert(cfg, cert_file, key_file) != 0) {
        neverc_tls_config_free(cfg);
        return -1;
    }

    const char *alpn[] = { "h2" };
    neverc_tls_config_set_alpn(cfg, alpn, 1);

    const char *err = NULL;
    neverc_tls_listener_t *ln = neverc_tls_listen(addr, cfg, &err);
    if (!ln) {
        neverc_tls_config_free(cfg);
        return -1;
    }

    g_h2_server_running = 1;
    while (g_h2_server_running) {
        neverc_tls_conn_t *tls = neverc_tls_accept(ln, &err);
        if (!tls) {
            if (!g_h2_server_running)
                break;
            continue;
        }

        const char *proto = neverc_tls_alpn(tls);
        if (proto && strcmp(proto, "h2") == 0)
            neverc_h2_serve_tls_conn(srv, tls);

        neverc_tls_close(tls);
    }

    neverc_tls_listener_close(ln);
    neverc_tls_config_free(cfg);
    return 0;
}
