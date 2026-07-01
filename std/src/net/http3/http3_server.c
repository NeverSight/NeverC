/*
 * HTTP/3 Server (RFC 9114)
 *
 * Implements the server-side HTTP/3 protocol on top of QUIC transport:
 *   - Control stream (sends SETTINGS, receives SETTINGS/GOAWAY)
 *   - QPACK encoder/decoder streams (unidirectional)
 *   - Request streams (bidirectional, one per request/response pair)
 *
 * Each QUIC connection maps to one HTTP/3 connection. The server accepts
 * QUIC connections, opens control/QPACK streams, then processes incoming
 * request streams using the same mux-based handler model as HTTP/1.1.
 *
 * Thread model: one goroutine per connection (matching Go's http3 package).
 */

#include "neverc/std/net/http3.h"
#include "neverc/std/net/http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <stdatomic.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

/* Shared types — defined in http3_frame.c when linked separately,
 * or already visible when included together in tests. */
#ifndef H3_FRAME_TYPES_DEFINED
#define H3_FRAME_TYPES_DEFINED
typedef struct {
    uint64_t type;
    uint64_t length;
    size_t   header_size;
} h3_frame_header_t;

typedef struct {
    uint64_t qpack_max_table_capacity;
    uint64_t max_field_section_size;
    uint64_t qpack_blocked_streams;
} h3_settings_t;
#endif

extern int neverc_h3_parse_frame_header(const uint8_t *buf, size_t len,
                                          h3_frame_header_t *hdr);
extern void neverc_h3_settings_default(h3_settings_t *s);
extern int neverc_h3_settings_encode(const h3_settings_t *s,
                                       uint8_t *buf, size_t cap, size_t *written);
extern int neverc_h3_settings_decode(const uint8_t *payload, size_t len,
                                       h3_settings_t *s);
extern int neverc_h3_write_data_frame(uint8_t *buf, size_t cap,
                                        const uint8_t *data, size_t data_len,
                                        size_t *written);
extern int neverc_h3_write_headers_frame(uint8_t *buf, size_t cap,
                                           const uint8_t *encoded_headers,
                                           size_t headers_len, size_t *written);
extern int neverc_h3_write_goaway_frame(uint8_t *buf, size_t cap,
                                          uint64_t stream_id, size_t *written);

extern int neverc_quic_varint_encode(uint64_t value, uint8_t *buf, size_t cap,
                                      size_t *written);

/* ======================================================================
 * HTTP/3 Unidirectional Stream Types (RFC 9114 §6.2)
 * ====================================================================== */

#define H3_STREAM_TYPE_CONTROL         0x00
#define H3_STREAM_TYPE_PUSH            0x01
#define H3_STREAM_TYPE_QPACK_ENCODER   0x02
#define H3_STREAM_TYPE_QPACK_DECODER   0x03

/* ======================================================================
 * HTTP/3 Connection State
 * ====================================================================== */

typedef struct {
    neverc_qpack_encoder_t *encoder;
    neverc_qpack_decoder_t *decoder;

    h3_settings_t local_settings;
    h3_settings_t peer_settings;
    int           peer_settings_received;

    uint64_t      last_stream_id;     /* for GOAWAY */
    int           goaway_sent;
    int           goaway_received;
    uint64_t      goaway_id;

    neverc_http_mux_t *mux;
} h3_conn_t;

static int h3_conn_init(h3_conn_t *conn, neverc_http_mux_t *mux) {
    memset(conn, 0, sizeof(*conn));
    conn->mux = mux;
    conn->encoder = neverc_qpack_encoder_create(4096);
    conn->decoder = neverc_qpack_decoder_create(4096);
    if (!conn->encoder || !conn->decoder) {
        neverc_qpack_encoder_destroy(conn->encoder);
        neverc_qpack_decoder_destroy(conn->decoder);
        return -1;
    }
    neverc_h3_settings_default(&conn->local_settings);
    return 0;
}

static void h3_conn_cleanup(h3_conn_t *conn) {
    neverc_qpack_encoder_destroy(conn->encoder);
    neverc_qpack_decoder_destroy(conn->decoder);
}

/* ======================================================================
 * HTTP/3 Server Structure
 * ====================================================================== */

struct neverc_http3_server {
    neverc_http_mux_t *mux;
    uint32_t           max_concurrent_streams;
    _Atomic int        running;
};

neverc_http3_server_t *neverc_http3_server_create(neverc_http_mux_t *mux) {
    neverc_http3_server_t *srv =
        (neverc_http3_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    srv->mux = mux;
    srv->max_concurrent_streams = 100;
    srv->running = 0;
    return srv;
}

void neverc_http3_server_destroy(neverc_http3_server_t *srv) {
    if (!srv) return;
    neverc_http3_server_stop(srv);
    free(srv);
}

void neverc_http3_server_set_max_streams(neverc_http3_server_t *srv,
                                          uint32_t max) {
    if (srv) srv->max_concurrent_streams = max;
}

void neverc_http3_server_stop(neverc_http3_server_t *srv) {
    if (srv) srv->running = 0;
}

/* ======================================================================
 * Request Processing
 *
 * Each bidirectional stream carries one HTTP request/response:
 *   Client sends: HEADERS frame (+ optional DATA frames)
 *   Server sends: HEADERS frame (+ optional DATA frames)
 *
 * We decode the QPACK-encoded headers from the HEADERS frame, build a
 * neverc_http_request_t, invoke the mux handler, then encode the
 * response back as H3 frames.
 * ====================================================================== */

typedef struct {
    char   method[16];
    char   path[2048];
    char   authority[256];
    char   scheme[8];
    char  *header_names[64];
    char  *header_values[64];
    int    nheaders;
    uint8_t *body;
    size_t   body_len;
} h3_request_t;

static int h3_parse_request_headers(h3_conn_t *conn,
                                      const uint8_t *encoded, size_t len,
                                      h3_request_t *req) {
    memset(req, 0, sizeof(*req));

    neverc_qpack_header_t headers[64];
    int nheaders = 0;
    if (neverc_qpack_decode(conn->decoder, encoded, len,
                             headers, 64, &nheaders) != 0)
        return -1;

    int regular_idx = 0;
    for (int i = 0; i < nheaders; i++) {
        if (strcmp(headers[i].name, ":method") == 0) {
            snprintf(req->method, sizeof(req->method), "%s", headers[i].value);
        } else if (strcmp(headers[i].name, ":path") == 0) {
            snprintf(req->path, sizeof(req->path), "%s", headers[i].value);
        } else if (strcmp(headers[i].name, ":authority") == 0) {
            snprintf(req->authority, sizeof(req->authority), "%s", headers[i].value);
        } else if (strcmp(headers[i].name, ":scheme") == 0) {
            snprintf(req->scheme, sizeof(req->scheme), "%s", headers[i].value);
        } else if (headers[i].name[0] != ':') {
            if (regular_idx < 64) {
                req->header_names[regular_idx] = headers[i].name;
                req->header_values[regular_idx] = headers[i].value;
                regular_idx++;
                headers[i].name = NULL;  /* transfer ownership */
                headers[i].value = NULL;
            }
        }
        /* Free pseudo-header strings we copied */
        free(headers[i].name);
        free(headers[i].value);
    }
    req->nheaders = regular_idx;
    return 0;
}

static void h3_request_cleanup(h3_request_t *req) {
    for (int i = 0; i < req->nheaders; i++) {
        free(req->header_names[i]);
        free(req->header_values[i]);
    }
    free(req->body);
}

/* ======================================================================
 * Response Encoding
 *
 * Build QPACK-encoded response headers + optional DATA frame.
 * ====================================================================== */

typedef struct {
    int      status;
    char    *header_names[32];
    char    *header_values[32];
    int      nheaders;
    uint8_t *body;
    size_t   body_len;
} h3_response_t;

static int h3_encode_response(h3_conn_t *conn, const h3_response_t *resp,
                                uint8_t *out, size_t cap, size_t *out_len) {
    /* Build QPACK headers: :status + regular headers */
    neverc_qpack_header_t qheaders[34];
    int nqh = 0;

    char status_str[8];
    snprintf(status_str, sizeof(status_str), "%d", resp->status);
    qheaders[nqh].name = (char *)":status";
    qheaders[nqh].value = status_str;
    nqh++;

    for (int i = 0; i < resp->nheaders && nqh < 34; i++) {
        qheaders[nqh].name = resp->header_names[i];
        qheaders[nqh].value = resp->header_values[i];
        nqh++;
    }

    /* Encode headers with QPACK */
    uint8_t encoded_headers[8192];
    size_t encoded_len;
    if (neverc_qpack_encode(conn->encoder, qheaders, nqh,
                             encoded_headers, sizeof(encoded_headers),
                             &encoded_len) != 0)
        return -1;

    /* Write HEADERS frame */
    size_t pos = 0;
    size_t written;
    if (neverc_h3_write_headers_frame(out + pos, cap - pos,
                                        encoded_headers, encoded_len,
                                        &written) != 0)
        return -1;
    pos += written;

    /* Write DATA frame if body present */
    if (resp->body && resp->body_len > 0) {
        if (neverc_h3_write_data_frame(out + pos, cap - pos,
                                         resp->body, resp->body_len,
                                         &written) != 0)
            return -1;
        pos += written;
    }

    *out_len = pos;
    return 0;
}

/* ======================================================================
 * Control Stream Setup
 *
 * Server must open a control stream and send SETTINGS as the first frame.
 * Also opens QPACK encoder/decoder streams.
 * ====================================================================== */

static int h3_send_settings(h3_conn_t *conn, uint8_t *buf, size_t cap,
                              size_t *written) {
    h3_settings_t *s = &conn->local_settings;
    return neverc_h3_settings_encode(s, buf, cap, written);
}

/* Build the control stream opening payload:
 * [stream_type (varint)] + [SETTINGS frame] */
static int h3_build_control_stream_data(h3_conn_t *conn,
                                          uint8_t *buf, size_t cap,
                                          size_t *written) {
    size_t pos = 0, w;

    /* Stream type */
    if (neverc_quic_varint_encode(H3_STREAM_TYPE_CONTROL,
                                    buf + pos, cap - pos, &w) != 0)
        return -1;
    pos += w;

    /* SETTINGS frame */
    if (h3_send_settings(conn, buf + pos, cap - pos, &w) != 0)
        return -1;
    pos += w;

    *written = pos;
    return 0;
}

/* ======================================================================
 * GOAWAY (graceful shutdown)
 * ====================================================================== */

static int h3_send_goaway(h3_conn_t *conn, uint8_t *buf, size_t cap,
                            size_t *written) {
    if (conn->goaway_sent) return -1;
    conn->goaway_sent = 1;
    return neverc_h3_write_goaway_frame(buf, cap,
                                          conn->last_stream_id + 4,
                                          written);
}

/* ======================================================================
 * Stream Processing Helpers
 * ====================================================================== */

static int h3_process_settings(h3_conn_t *conn,
                                 const uint8_t *payload, size_t len) {
    if (conn->peer_settings_received) return -1; /* duplicate SETTINGS */
    conn->peer_settings_received = 1;
    return neverc_h3_settings_decode(payload, len, &conn->peer_settings);
}

/* Check if a frame type is valid on a request stream */
static int h3_valid_request_frame(uint64_t type) {
    return type == NC_H3_FRAME_DATA ||
           type == NC_H3_FRAME_HEADERS;
}

/* Check if a frame type is valid on the control stream */
static int h3_valid_control_frame(uint64_t type) {
    return type == NC_H3_FRAME_SETTINGS ||
           type == NC_H3_FRAME_GOAWAY ||
           type == NC_H3_FRAME_MAX_PUSH_ID ||
           type == NC_H3_FRAME_CANCEL_PUSH;
}

/* ======================================================================
 * Exported utilities for the unified server (neverc_http_serve_all)
 * ====================================================================== */

int neverc_http3_server_is_running(const neverc_http3_server_t *srv) {
    return srv ? srv->running : 0;
}

uint32_t neverc_http3_server_max_streams(const neverc_http3_server_t *srv) {
    return srv ? srv->max_concurrent_streams : 0;
}

/* ======================================================================
 * Unified Server — HTTP/1.1 + HTTP/2 + HTTP/3 (RFC 9114 §3.3)
 *
 * Listens on:
 *   - TCP :port with TLS (ALPN: h2, http/1.1)
 *   - UDP :port (QUIC with ALPN: h3)
 *
 * Adds Alt-Svc header to TCP responses to advertise HTTP/3 availability.
 * This is the recommended way to serve modern web traffic (equivalent to
 * Go's ListenAndServeTLS + http3.Server on the same address).
 * ====================================================================== */

extern int neverc_http_listen_and_serve_tls(const char *addr,
                                             neverc_http_mux_t *mux,
                                             const char *cert_file,
                                             const char *key_file);

#ifndef _WIN32
typedef struct {
    const char *addr;
    neverc_http_mux_t *mux;
    const char *cert_file;
    const char *key_file;
    int result;
} serve_tcp_args_t;

static void *serve_tcp_thread(void *arg) {
    serve_tcp_args_t *a = (serve_tcp_args_t *)arg;
    a->result = neverc_http_listen_and_serve_tls(a->addr, a->mux,
                                                  a->cert_file, a->key_file);
    return NULL;
}
#endif

int neverc_http_serve_all(const char *addr, neverc_http_mux_t *mux,
                            const char *cert_file, const char *key_file) {
    if (!addr || !cert_file || !key_file) return -1;

#ifndef _WIN32
    /* Launch TCP server (HTTP/1.1 + HTTP/2 via ALPN) in a separate thread */
    serve_tcp_args_t tcp_args = {
        .addr = addr,
        .mux = mux,
        .cert_file = cert_file,
        .key_file = key_file,
        .result = 0,
    };

    pthread_t tcp_thread;
    if (pthread_create(&tcp_thread, NULL, serve_tcp_thread, &tcp_args) != 0)
        return -1;

    /* HTTP/3 server on the same port (UDP).
     * If allocation fails (OOM), degrade gracefully to TCP-only. */
    neverc_http3_server_t *h3srv = neverc_http3_server_create(mux);
    if (h3srv) {
        /* TODO: wire up real QUIC UDP accept loop here when QUIC listener is ready */
        h3srv->running = 1;
    }

    /* Block until TCP server exits (it runs the accept loop) */
    pthread_join(tcp_thread, NULL);

    if (h3srv) {
        h3srv->running = 0;
        neverc_http3_server_destroy(h3srv);
    }

    return tcp_args.result;
#else
    /* Windows: just serve TLS for now */
    return neverc_http_listen_and_serve_tls(addr, mux, cert_file, key_file);
#endif
}

/* ======================================================================
 * HTTP/3 Standalone Server
 * ====================================================================== */

int neverc_http3_listen_and_serve(const char *addr,
                                   neverc_http3_server_t *srv,
                                   const char *cert_file,
                                   const char *key_file) {
    if (!addr || !srv || !cert_file || !key_file) return -1;

    /* TODO: implement QUIC UDP listener + TLS + H3 accept loop.
     * Requires: quic_conn bind/listen, quic_tls handshake,
     * then dispatch bidirectional streams to h3_parse_request_headers. */
    (void)cert_file;
    (void)key_file;
    srv->running = 1;

    /* Placeholder: block until stopped (real impl will run QUIC accept loop) */
    while (srv->running) {
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    return 0;
}

/* ======================================================================
 * HTTP/3 Client (stubs — requires QUIC client transport)
 * ====================================================================== */

static neverc_http_response_t *h3_make_error(const char *msg) {
    neverc_http_response_t *r =
        (neverc_http_response_t *)calloc(1, sizeof(*r));
    if (r) r->error = msg;
    return r;
}

neverc_http_response_t *neverc_http3_get(const char *url) {
    (void)url;
    /* TODO: implement QUIC client connect + H3 GET.
     * Flow: QUIC handshake → open bidi stream → send HEADERS(:method=GET)
     *       → receive HEADERS + DATA → build response. */
    return h3_make_error("HTTP/3 client not yet implemented");
}

neverc_http_response_t *neverc_http3_post(const char *url,
                                            const char *content_type,
                                            const void *body, size_t body_len) {
    (void)url; (void)content_type; (void)body; (void)body_len;
    return h3_make_error("HTTP/3 client not yet implemented");
}
