#ifndef NEVERC_NET_HTTP3_H
#define NEVERC_NET_HTTP3_H

/*
 * NeverC HTTP/3 (RFC 9114) + QPACK (RFC 9204)
 *
 * Provides HTTP/3 control streams, static-table QPACK, mux-backed request
 * dispatch, graceful GOAWAY, a verified one-shot client, and unified serving
 * with HTTP/1.1 and HTTP/2 over TCP/TLS plus HTTP/3 over UDP/QUIC.
 *
 * Architecture:
 *   - Each HTTP/3 connection maps to one QUIC connection
 *   - Each HTTP request/response pair uses one bidirectional QUIC stream
 *   - Static-only QPACK uses capacity-zero encoder/decoder streams
 *
 * Server usage:
 *   neverc_http3_server_t *h3 = neverc_http3_server_create(mux);
 *   neverc_http3_listen_and_serve(":443", h3, "cert.pem", "key.pem");
 *
 * For automatic HTTP/1.1 + HTTP/2 + HTTP/3 (like Go's http3 package):
 *   neverc_http_serve_all(":443", mux, "cert.pem", "key.pem");
 *   // Listens on TCP (HTTP/1.1+HTTP/2) and UDP (HTTP/3) simultaneously
 */

#include <stddef.h>
#include <stdint.h>
#include "neverc/std/net/http.h"
#include "neverc/std/net/quic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * HTTP/3 Frame Types (RFC 9114 §7)
 * ====================================================================== */

#define NC_H3_FRAME_DATA           0x00
#define NC_H3_FRAME_HEADERS        0x01
#define NC_H3_FRAME_CANCEL_PUSH    0x03
#define NC_H3_FRAME_SETTINGS       0x04
#define NC_H3_FRAME_PUSH_PROMISE   0x05
#define NC_H3_FRAME_GOAWAY         0x07
#define NC_H3_FRAME_MAX_PUSH_ID    0x0D

/* HTTP/3 Settings (RFC 9114 §7.2.4.1) */
#define NC_H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY   0x01
#define NC_H3_SETTINGS_MAX_FIELD_SECTION_SIZE      0x06
#define NC_H3_SETTINGS_QPACK_BLOCKED_STREAMS       0x07

/* HTTP/3 Error Codes (RFC 9114 §8.1) */
#define NC_H3_NO_ERROR                  0x0100
#define NC_H3_GENERAL_PROTOCOL_ERROR    0x0101
#define NC_H3_INTERNAL_ERROR            0x0102
#define NC_H3_STREAM_CREATION_ERROR     0x0103
#define NC_H3_CLOSED_CRITICAL_STREAM    0x0104
#define NC_H3_FRAME_UNEXPECTED          0x0105
#define NC_H3_FRAME_ERROR               0x0106
#define NC_H3_EXCESSIVE_LOAD            0x0107
#define NC_H3_ID_ERROR                  0x0108
#define NC_H3_SETTINGS_ERROR            0x0109
#define NC_H3_MISSING_SETTINGS          0x010A
#define NC_H3_REQUEST_REJECTED          0x010B
#define NC_H3_REQUEST_CANCELLED         0x010C
#define NC_H3_REQUEST_INCOMPLETE        0x010D
#define NC_H3_MESSAGE_ERROR             0x010E
#define NC_H3_CONNECT_ERROR             0x010F
#define NC_H3_VERSION_FALLBACK          0x0110

/* QPACK Error Codes (RFC 9204 §6) */
#define NC_H3_QPACK_DECOMPRESSION_FAILED 0x0200
#define NC_H3_QPACK_ENCODER_STREAM_ERROR 0x0201
#define NC_H3_QPACK_DECODER_STREAM_ERROR 0x0202

/* ======================================================================
 * QPACK Header Compression (RFC 9204)
 * ====================================================================== */

typedef struct neverc_qpack_encoder neverc_qpack_encoder_t;
typedef struct neverc_qpack_decoder neverc_qpack_decoder_t;

typedef struct {
    char *name;
    char *value;
} neverc_qpack_header_t;

neverc_qpack_encoder_t *neverc_qpack_encoder_create(uint32_t max_table_cap);
void neverc_qpack_encoder_destroy(neverc_qpack_encoder_t *enc);
int neverc_qpack_encode(neverc_qpack_encoder_t *enc,
                          const neverc_qpack_header_t *headers, int nheaders,
                          uint8_t *out, size_t out_cap, size_t *out_len);

neverc_qpack_decoder_t *neverc_qpack_decoder_create(uint32_t max_table_cap);
void neverc_qpack_decoder_destroy(neverc_qpack_decoder_t *dec);
/* 0 on success, -2 if max_headers filled with leftover bytes, -1 if the
 * field section is damaged. */
int neverc_qpack_decode(neverc_qpack_decoder_t *dec,
                          const uint8_t *data, size_t len,
                          neverc_qpack_header_t *headers, int max_headers,
                          int *nheaders);

/* Uncompressed field-section size (RFC 9113 §6.5.2 / RFC 9114 §4.2.2):
 * sum of name length + value length + 32 per field. */
int neverc_qpack_field_section_size(const neverc_qpack_header_t *headers,
                                    int nheaders, uint64_t *size);

/* HTTP/3 namespace aliases used by net.http3 dot syntax. */
neverc_qpack_encoder_t *neverc_http3_qpack_encoder_create(
    uint32_t max_table_cap);
void neverc_http3_qpack_encoder_destroy(neverc_qpack_encoder_t *encoder);
int neverc_http3_qpack_encode(
    neverc_qpack_encoder_t *encoder,
    const neverc_qpack_header_t *headers, int header_count,
    uint8_t *output, size_t output_capacity, size_t *output_length);
neverc_qpack_decoder_t *neverc_http3_qpack_decoder_create(
    uint32_t max_table_cap);
void neverc_http3_qpack_decoder_destroy(neverc_qpack_decoder_t *decoder);
int neverc_http3_qpack_decode(
    neverc_qpack_decoder_t *decoder,
    const uint8_t *data, size_t length,
    neverc_qpack_header_t *headers, int max_headers, int *header_count);
int neverc_http3_qpack_field_section_size(
    const neverc_qpack_header_t *headers, int nheaders, uint64_t *size);

/* ======================================================================
 * HTTP/3 Server
 * ====================================================================== */

typedef struct neverc_http3_server neverc_http3_server_t;

/* Create an HTTP/3 server with the given mux (NULL = default mux). */
neverc_http3_server_t *neverc_http3_server_create(neverc_http_mux_t *mux);

/* Destroy an HTTP/3 server. */
void neverc_http3_server_destroy(neverc_http3_server_t *srv);

/* Configure max concurrent streams per connection (default 100). */
void neverc_http3_server_set_max_streams(neverc_http3_server_t *srv,
                                          uint32_t max);

/* Listen and serve HTTP/3 on addr with TLS. */
int neverc_http3_listen_and_serve(const char *addr,
                                   neverc_http3_server_t *srv,
                                   const char *cert_file,
                                   const char *key_file);

/* Stop a running HTTP/3 server. */
void neverc_http3_server_stop(neverc_http3_server_t *srv);

/* Check if the HTTP/3 server is currently running. */
int neverc_http3_server_is_running(const neverc_http3_server_t *srv);

/* Get the max concurrent streams setting. */
uint32_t neverc_http3_server_max_streams(const neverc_http3_server_t *srv);

/* ======================================================================
 * Unified Server — HTTP/1.1 + HTTP/2 + HTTP/3 on same port
 *
 * Listens on:
 *   - TCP port for HTTP/1.1 and HTTP/2 (ALPN: h2, http/1.1)
 *   - UDP port for HTTP/3 (QUIC with ALPN: h3)
 *   - Sends Alt-Svc header to upgrade TCP clients to HTTP/3
 *
 * This is the recommended serving entry point for all HTTP versions.
 * ====================================================================== */

typedef struct neverc_http_unified_server neverc_http_unified_server_t;

/* Create a manageable all-versions server. The mux is borrowed and must
 * outlive the unified server. */
neverc_http_unified_server_t *neverc_http_unified_server_create(
    neverc_http_mux_t *mux);
void neverc_http_unified_server_destroy(
    neverc_http_unified_server_t *server);

/* Serve all versions on one non-zero TCP/UDP port until shutdown. */
int neverc_http_unified_server_listen_and_serve(
    neverc_http_unified_server_t *server, const char *addr,
    const char *cert_file, const char *key_file);
void neverc_http_unified_server_shutdown(
    neverc_http_unified_server_t *server);
int neverc_http_unified_server_is_running(
    const neverc_http_unified_server_t *server);
int neverc_http_unified_server_bound_port(
    const neverc_http_unified_server_t *server);

/* HTTP/3 namespace aliases used by net.http3 dot syntax. */
neverc_http_unified_server_t *neverc_http3_unified_server_create(
    neverc_http_mux_t *mux);
void neverc_http3_unified_server_destroy(
    neverc_http_unified_server_t *server);
int neverc_http3_unified_server_listen_and_serve(
    neverc_http_unified_server_t *server, const char *addr,
    const char *cert_file, const char *key_file);
void neverc_http3_unified_server_shutdown(
    neverc_http_unified_server_t *server);
int neverc_http3_unified_server_is_running(
    const neverc_http_unified_server_t *server);
int neverc_http3_unified_server_bound_port(
    const neverc_http_unified_server_t *server);

/* Blocking compatibility wrapper. New code should retain a unified server
 * instance so it can request graceful shutdown. */
int neverc_http_serve_all(const char *addr, neverc_http_mux_t *mux,
                            const char *cert_file, const char *key_file);
int neverc_http3_serve_all(const char *addr, neverc_http_mux_t *mux,
                           const char *cert_file, const char *key_file);

/* ======================================================================
 * HTTP/3 Client
 * ====================================================================== */

typedef struct {
    const char *server_name;    /* optional SNI/hostname override */
    const char *root_cert_file; /* optional PEM trust roots */
    int insecure_skip_verify;   /* explicit opt-out; never the default */
} neverc_http3_client_config_t;

neverc_http3_client_config_t neverc_http3_client_config_default(void);

/* Verified HTTP/3 GET request. No implicit protocol fallback is performed.
 * Caller must call neverc_http_response_free(). */
neverc_http_response_t *neverc_http3_get(const char *url);
neverc_http_response_t *neverc_http3_get_with_config(
    const char *url, const neverc_http3_client_config_t *config);

/* Verified HTTP/3 POST request. */
neverc_http_response_t *neverc_http3_post(const char *url,
                                            const char *content_type,
                                            const void *body, size_t body_len);
neverc_http_response_t *neverc_http3_post_with_config(
    const char *url, const char *content_type,
    const void *body, size_t body_len,
    const neverc_http3_client_config_t *config);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net/_module.h>
#endif

#endif /* NEVERC_NET_HTTP3_H */
