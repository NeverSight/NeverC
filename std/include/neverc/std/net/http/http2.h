/*
 * NeverC HTTP/2 (RFC 9113)
 *
 * Full HTTP/2 implementation with:
 *   - Binary framing layer (9 frame types)
 *   - HPACK header compression (RFC 7541)
 *   - Multiplexed streams over single TCP connection
 *   - Flow control (per-stream + connection-level)
 *   - Server push
 *   - ALPN negotiation (h2, h2c)
 *   - Graceful shutdown (GOAWAY)
 *
 * Usage:
 *   neverc_http2_server_t *h2 = neverc_http2_server_create(mux);
 *   neverc_http2_server_set_max_streams(h2, 100);
 *   neverc_http2_listen_and_serve(":8443", h2, cert, key);
 *
 * Integration with existing HTTP/1.1 server via ALPN auto-upgrade.
 */

#ifndef NEVERC_HTTP2_H
#define NEVERC_HTTP2_H

#include <stddef.h>
#include <stdint.h>
#include "neverc/std/net/http.h"

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
#define NC_H2_DEFAULT_MAX_HEADER_LIST_SIZE (16 * 1024 * 1024)

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

neverc_hpack_decoder_t *neverc_hpack_decoder_create(uint32_t max_table_size);
void neverc_hpack_decoder_destroy(neverc_hpack_decoder_t *dec);
int neverc_hpack_decode(neverc_hpack_decoder_t *dec,
                         const uint8_t *data, size_t len,
                         neverc_hpack_header_t *headers, int max_headers,
                         int *nheaders);

neverc_hpack_encoder_t *neverc_hpack_encoder_create(uint32_t max_table_size);
void neverc_hpack_encoder_destroy(neverc_hpack_encoder_t *enc);
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

/* Serve a single HTTP/2 connection (after ALPN or upgrade) */
int neverc_h2_serve_conn(neverc_h2_server_t *srv, int fd);

/* Listen and serve with ALPN h2 + TLS */
int neverc_h2_listen_and_serve(const char *addr,
                                neverc_h2_server_t *srv,
                                const char *cert_file,
                                const char *key_file);

/* Listen and serve h2c (HTTP/2 cleartext, for development) */
int neverc_h2_listen_and_serve_h2c(const char *addr,
                                     neverc_h2_server_t *srv);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_HTTP2_H */
