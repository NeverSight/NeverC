/*
 * NeverC HTTP/2 (RFC 9113).
 *
 * Framing, bounded HPACK, multiplexed request dispatch, flow control, and
 * graceful server lifecycle are shared by h2c and TLS/ALPN transports.
 *
 * Target capabilities:
 *   - Binary framing layer (9 frame types)
 *   - HPACK header compression (RFC 7541)
 *   - Multiplexed streams over single TCP connection
 *   - Flow control (per-stream + connection-level)
 *   - Server push
 *   - ALPN negotiation (h2, h2c)
 *   - Graceful shutdown (GOAWAY)
 *
 * Usage:
 *   neverc_h2_server_t *h2 = neverc_h2_server_create(mux);
 *   neverc_h2_server_set_max_streams(h2, 100);
 *   neverc_h2_listen_and_serve(":8443", h2, cert, key);
 *
 * Integration with existing HTTP/1.1 server via ALPN auto-upgrade.
 */

#ifndef NEVERC_HTTP2_H
#define NEVERC_HTTP2_H

#include <stddef.h>
#include <stdint.h>
#include "neverc/std/net/http.h"
#include "neverc/std/crypto/tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * HTTP/2 Frame Types (RFC 9113 §4)
 * ====================================================================== */

#define NC_H2_FRAME_DATA           0x0
#define NC_H2_FRAME_HEADERS        0x1
#define NC_H2_FRAME_PRIORITY       0x2
#define NC_H2_FRAME_RST_STREAM     0x3
#define NC_H2_FRAME_SETTINGS       0x4
#define NC_H2_FRAME_PUSH_PROMISE   0x5
#define NC_H2_FRAME_PING           0x6
#define NC_H2_FRAME_GOAWAY         0x7
#define NC_H2_FRAME_WINDOW_UPDATE  0x8
#define NC_H2_FRAME_CONTINUATION   0x9

/* Frame flags */
#define NC_H2_FLAG_END_STREAM   0x1
#define NC_H2_FLAG_END_HEADERS  0x4
#define NC_H2_FLAG_PADDED       0x8
#define NC_H2_FLAG_PRIORITY     0x20
#define NC_H2_FLAG_ACK          0x1

/* Settings parameters (RFC 9113 §6.5.2) */
#define NC_H2_SETTINGS_HEADER_TABLE_SIZE      0x1
#define NC_H2_SETTINGS_ENABLE_PUSH            0x2
#define NC_H2_SETTINGS_MAX_CONCURRENT_STREAMS 0x3
#define NC_H2_SETTINGS_INITIAL_WINDOW_SIZE    0x4
#define NC_H2_SETTINGS_MAX_FRAME_SIZE         0x5
#define NC_H2_SETTINGS_MAX_HEADER_LIST_SIZE   0x6

/* Error codes (RFC 9113 §7) */
#define NC_H2_NO_ERROR             0x0
#define NC_H2_PROTOCOL_ERROR       0x1
#define NC_H2_INTERNAL_ERROR       0x2
#define NC_H2_FLOW_CONTROL_ERROR   0x3
#define NC_H2_SETTINGS_TIMEOUT     0x4
#define NC_H2_STREAM_CLOSED        0x5
#define NC_H2_FRAME_SIZE_ERROR     0x6
#define NC_H2_REFUSED_STREAM       0x7
#define NC_H2_CANCEL               0x8
#define NC_H2_COMPRESSION_ERROR    0x9
#define NC_H2_CONNECT_ERROR        0xa
#define NC_H2_ENHANCE_YOUR_CALM    0xb
#define NC_H2_INADEQUATE_SECURITY  0xc
#define NC_H2_HTTP_1_1_REQUIRED    0xd

/* Default settings values */
#define NC_H2_DEFAULT_HEADER_TABLE_SIZE    4096
#define NC_H2_DEFAULT_MAX_CONCURRENT       100
#define NC_H2_DEFAULT_INITIAL_WINDOW_SIZE  65535
#define NC_H2_DEFAULT_MAX_FRAME_SIZE       16384
#define NC_H2_MAX_FRAME_SIZE_LIMIT         16777215
#define NC_H2_DEFAULT_MAX_HEADER_LIST_SIZE (64 * 1024)

/* Connection preface (RFC 9113 §3.4) */
#define NC_H2_CLIENT_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define NC_H2_CLIENT_PREFACE_LEN 24

/* ======================================================================
 * HPACK Header Compression (RFC 7541)
 * ====================================================================== */

typedef struct {
    char *name;
    char *value;
    int   sensitive;
} neverc_hpack_header_t;

typedef struct neverc_hpack_decoder neverc_hpack_decoder_t;
typedef struct neverc_hpack_encoder neverc_hpack_encoder_t;

/* Maximum dynamic table size supported by the fixed-capacity implementation. */
#define NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE 8192U

neverc_hpack_decoder_t *neverc_hpack_decoder_create(uint32_t max_table_size);
void neverc_hpack_decoder_destroy(neverc_hpack_decoder_t *dec);
/* 0 = decoded into headers; 1 = HPACK synced but more fields than
 * max_headers (overflow is a stream error, not COMPRESSION_ERROR);
 * -1 = malformed block (connection COMPRESSION_ERROR). */
int neverc_hpack_decode(neverc_hpack_decoder_t *dec,
                         const uint8_t *data, size_t len,
                         neverc_hpack_header_t *headers, int max_headers,
                         int *nheaders);

neverc_hpack_encoder_t *neverc_hpack_encoder_create(uint32_t max_table_size);
void neverc_hpack_encoder_destroy(neverc_hpack_encoder_t *enc);
int neverc_hpack_encoder_set_max_table_size(neverc_hpack_encoder_t *enc,
                                             uint32_t max_table_size);
int neverc_hpack_encode(neverc_hpack_encoder_t *enc,
                         const neverc_hpack_header_t *headers, int nheaders,
                         uint8_t *out, size_t out_cap, size_t *out_len);

/* Huffman encoding/decoding */
int neverc_hpack_huffman_decode(const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len);
int neverc_hpack_huffman_encode(const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len);

/* ======================================================================
 * HTTP/2 Frame
 * ====================================================================== */

typedef struct {
    uint32_t length;     /* 24 bits: payload length */
    uint8_t  type;       /* 8 bits: frame type */
    uint8_t  flags;      /* 8 bits: flags */
    uint32_t stream_id;  /* 31 bits: stream identifier */
} neverc_h2_frame_header_t;

#define NC_H2_FRAME_HEADER_SIZE 9

int neverc_h2_frame_header_read(const uint8_t *data, size_t len,
                                  neverc_h2_frame_header_t *hdr);
int neverc_h2_frame_header_write(const neverc_h2_frame_header_t *hdr,
                                   uint8_t *out);

/* ======================================================================
 * HTTP/2 Settings
 * ====================================================================== */

typedef struct {
    uint32_t header_table_size;
    int      enable_push;
    uint32_t max_concurrent_streams;
    uint32_t initial_window_size;
    uint32_t max_frame_size;
    uint32_t max_header_list_size;
} neverc_h2_settings_t;

void neverc_h2_settings_init(neverc_h2_settings_t *s);

/* ======================================================================
 * HTTP/2 Server
 * ====================================================================== */

typedef struct neverc_h2_server neverc_h2_server_t;

neverc_h2_server_t *neverc_h2_server_create(neverc_http_mux_t *mux);
void neverc_h2_server_destroy(neverc_h2_server_t *srv);

void neverc_h2_server_set_max_streams(neverc_h2_server_t *srv, uint32_t max);
void neverc_h2_server_set_max_frame_size(neverc_h2_server_t *srv, uint32_t max);
void neverc_h2_server_set_initial_window_size(neverc_h2_server_t *srv, uint32_t win);
void neverc_h2_server_set_max_header_list_size(neverc_h2_server_t *srv, uint32_t max);
void neverc_h2_server_set_max_body_size(neverc_h2_server_t *srv, size_t max);
void neverc_h2_server_set_handler_timeout(neverc_h2_server_t *srv, int ms);
void neverc_h2_server_set_alt_svc(neverc_h2_server_t *srv,
                                  const char *value);
size_t neverc_h2_server_active_connections(neverc_h2_server_t *srv);

/* Stop this server instance, cancel accept, and drain active connections. */
void neverc_h2_server_shutdown(neverc_h2_server_t *srv);

/* Serve a single HTTP/2 connection on a pointer-width-safe native socket
 * handle (after ALPN or upgrade). The caller retains ownership. */
int neverc_h2_serve_conn(neverc_h2_server_t *srv,
                         uintptr_t socket_handle);

/* Serve a single HTTP/2 connection over an existing TLS connection.
 * Used by the HTTPS server after ALPN negotiates "h2". */
int neverc_h2_serve_tls_conn(neverc_h2_server_t *srv,
                              neverc_tls_conn_t *tls);

/* Listen and serve with ALPN h2 + TLS */
int neverc_h2_listen_and_serve(const char *addr,
                                neverc_h2_server_t *srv,
                                const char *cert_file,
                                const char *key_file);

/* Listen and serve h2c (HTTP/2 cleartext, for development) */
int neverc_h2_listen_and_serve_h2c(const char *addr,
                                     neverc_h2_server_t *srv);

/* Compatibility stop for the currently running legacy listen call. New code
 * should call neverc_h2_server_shutdown with the target instance. */
void neverc_h2_server_stop(void);

/* Read request DATA from request.protocol_stream. Returns bytes read, 0 after
 * END_STREAM, and -1 on reset/cancellation. One reader is permitted. */
int neverc_h2_request_stream_read(void *protocol_stream,
                                   neverc_context_t *context,
                                   void *output, size_t output_capacity);
void neverc_h2_request_stream_cancel(void *protocol_stream,
                                      uint32_t error_code);

/* ======================================================================
 * HTTP/2 Client
 * ====================================================================== */

typedef struct neverc_h2_client neverc_h2_client_t;
typedef struct neverc_h2_client_stream neverc_h2_client_stream_t;

typedef struct {
    int timeout_ms;
    uint32_t max_concurrent_streams;
    uint32_t initial_window_size;
    size_t max_response_header_list_size;
    size_t max_response_body_size;
    const char *root_cert_file;   /* optional custom PEM roots */
    const char *client_cert_file; /* optional mTLS certificate PEM */
    const char *client_key_file;  /* required with client_cert_file */
    int insecure_skip_verify;     /* explicit verification opt-out */
} neverc_h2_client_config_t;

typedef struct {
    int status_code;
    neverc_hpack_header_t *headers;
    size_t header_count;
    neverc_hpack_header_t *trailers;
    size_t trailer_count;
    int received_trailers; /* 1 if a trailer HEADERS block arrived, even empty */
    int received_data;     /* 1 if a DATA frame arrived, including empty */
    uint8_t *body;
    size_t body_length;
    uint32_t stream_error;
    const char *error;
} neverc_h2_response_t;

typedef enum {
    NEVERC_H2_CLIENT_EVENT_HEADERS,
    NEVERC_H2_CLIENT_EVENT_DATA,
    NEVERC_H2_CLIENT_EVENT_TRAILERS,
    NEVERC_H2_CLIENT_EVENT_END,
    NEVERC_H2_CLIENT_EVENT_ERROR
} neverc_h2_client_event_type_t;

typedef struct {
    neverc_h2_client_event_type_t type;
    int status_code;
    neverc_hpack_header_t *headers;
    size_t header_count;
    uint8_t *data;
    size_t data_length;
    size_t flow_controlled_length; /* transport accounting; do not modify */
    uint32_t error_code;
    const char *error;
} neverc_h2_client_event_t;

neverc_h2_client_config_t neverc_h2_client_config_default(void);

/* Dial one multiplexed h2 connection. server_name is used for SNI,
 * certificate verification, :authority, and is required for TLS. */
neverc_h2_client_t *neverc_h2_client_dial(
    const char *addr, const char *server_name, int use_tls,
    const neverc_h2_client_config_t *config, const char **error);
neverc_h2_client_t *neverc_h2_client_dial_context(
    const char *addr, const char *server_name, int use_tls,
    const neverc_h2_client_config_t *config, neverc_context_t *context,
    const char **error);

/* Concurrent calls share the connection and receive independent streams. */
neverc_h2_response_t *neverc_h2_client_do_context(
    neverc_h2_client_t *client, neverc_context_t *context,
    const char *method, const char *path,
    const neverc_hpack_header_t *headers, size_t header_count,
    const void *body, size_t body_length);
neverc_h2_response_t *neverc_h2_client_do(
    neverc_h2_client_t *client, const char *method, const char *path,
    const neverc_hpack_header_t *headers, size_t header_count,
    const void *body, size_t body_length);

/* Open a live request stream. DATA may be sent and response events received
 * concurrently from separate threads. */
neverc_h2_client_stream_t *neverc_h2_client_stream_open(
    neverc_h2_client_t *client, neverc_context_t *context,
    const char *method, const char *path,
    const neverc_hpack_header_t *headers, size_t header_count,
    int end_stream, const char **error);
int neverc_h2_client_stream_send(
    neverc_h2_client_stream_t *stream, neverc_context_t *context,
    const void *data, size_t length, int end_stream);
/* Return 1 with an owned event, 0 after closure, and -1 on cancellation. */
int neverc_h2_client_stream_receive(
    neverc_h2_client_stream_t *stream, neverc_context_t *context,
    neverc_h2_client_event_t **event);
void neverc_h2_client_event_free(neverc_h2_client_event_t *event);
void neverc_h2_client_stream_cancel(neverc_h2_client_stream_t *stream,
                                     uint32_t error_code);
/* Cancel if necessary, unlink, drain events, and release the stream. */
void neverc_h2_client_stream_free(neverc_h2_client_stream_t *stream);

void neverc_h2_response_free(neverc_h2_response_t *response);
void neverc_h2_client_close(neverc_h2_client_t *client);
/* The caller must ensure no client_do call is active. */
void neverc_h2_client_free(neverc_h2_client_t *client);

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_HTTP2_H */
