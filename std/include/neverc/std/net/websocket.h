#ifndef NEVERC_NET_WEBSOCKET_H
#define NEVERC_NET_WEBSOCKET_H

/*
 * NeverC net/websocket — WebSocket protocol (RFC 6455).
 *
 * Client/server handshake + framed I/O on top of TCP.
 * Cross-platform: POSIX + WinSock.
 */

#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NC_WS_OPCODE_CONTINUATION 0x0
#define NC_WS_OPCODE_TEXT         0x1
#define NC_WS_OPCODE_BINARY       0x2
#define NC_WS_OPCODE_CLOSE        0x8
#define NC_WS_OPCODE_PING         0x9
#define NC_WS_OPCODE_PONG         0xA

typedef struct neverc_ws_conn neverc_ws_conn_t;

typedef struct {
    const char *origin;          /* optional Origin request header */
    const char *subprotocol;     /* optional single subprotocol token */
    int handshake_timeout_ms;    /* 0 = default (30000ms) */
    size_t max_message_size;     /* 0 = default (16MiB) */
    int read_timeout_ms;         /* 0 = no read timeout */
    int write_timeout_ms;        /* 0 = no write timeout */
    int ping_interval_ms;        /* 0 = keepalive disabled */
    int pong_timeout_ms;         /* required when keepalive is enabled */
} neverc_ws_client_config_t;

/* --- Handshake (client) --- */

/* Connect to a ws:// or verified TLS wss:// URL and complete the RFC 6455
 * client handshake. wss:// uses platform roots and SNI for the URL host. */
neverc_ws_conn_t *neverc_ws_dial(const char *url,
                                  const neverc_ws_client_config_t *config,
                                  const char **errp);

/* --- Handshake (server) --- */

/* Compute Sec-WebSocket-Accept from Sec-WebSocket-Key. */
int neverc_ws_compute_accept(const char *key, char *accept, size_t accept_cap);

/* Validate upgrade headers and write HTTP 101 response. Returns 0 on success. */
int neverc_ws_handshake_server(neverc_tcp_conn_t *conn, const char *raw_request,
                                size_t raw_len, size_t *consumed);

/* Upgrade an HTTP handler connection to WebSocket (RFC 6455).
 * Validates request headers, sends 101, hijacks the connection.
 * Returns ws conn on success (caller must free), NULL on failure. */
neverc_ws_conn_t *neverc_ws_upgrade_http(neverc_http_request_t *req,
                                          neverc_http_response_writer_t *w);

/* --- Connection --- */

/* Create a server-side WebSocket connection from a TCP conn after a custom
 * server handshake (takes ownership of socket). */
neverc_ws_conn_t *neverc_ws_conn_new(neverc_tcp_conn_t *conn);

void neverc_ws_conn_free(neverc_ws_conn_t *conn);

/* Compatibility helper that sets both read and write timeouts. */
int neverc_ws_set_timeout(neverc_ws_conn_t *conn, int ms);

/* Set read and total-frame write timeouts independently (0 = no timeout).
 * The write timeout is an absolute budget for the complete WebSocket frame,
 * which bounds synchronous backpressure from a slow consumer. */
int neverc_ws_set_read_timeout(neverc_ws_conn_t *conn, int ms);
int neverc_ws_set_write_timeout(neverc_ws_conn_t *conn, int ms);

/* Send periodic ping frames and fail the connection unless the peer returns
 * the matching pong before pong_timeout_ms. Passing two zeroes disables it.
 * A read loop must be active so incoming pong frames can be processed. */
int neverc_ws_set_keepalive(neverc_ws_conn_t *conn, int ping_interval_ms,
                            int pong_timeout_ms);

/* Return 1 after a keepalive pong deadline expired, otherwise 0. */
int neverc_ws_keepalive_expired(neverc_ws_conn_t *conn);

/* Set the maximum accepted frame/message size. 0 disables the limit. */
int neverc_ws_set_read_limit(neverc_ws_conn_t *conn, size_t max_bytes);

/* --- Frame I/O --- */

/* Read next frame. Returns 0 on success, -1 on error. Only one concurrent
 * reader is supported; writes may safely run concurrently with that reader.
 * If fin is non-NULL, set to 1 when this is the final fragment. */
int neverc_ws_read_frame(neverc_ws_conn_t *conn, int *opcode, int *fin,
                          void *buf, size_t buflen, size_t *out_len);

/* Write text/binary frame. Client connections mask every frame; server
 * connections leave frames unmasked as required by RFC 6455. */
int neverc_ws_write_text(neverc_ws_conn_t *conn, const void *data, size_t len);
int neverc_ws_write_binary(neverc_ws_conn_t *conn, const void *data, size_t len);
int neverc_ws_write_frame(neverc_ws_conn_t *conn, int opcode, int fin,
                          const void *data, size_t len);

/* Control frames */
int neverc_ws_send_ping(neverc_ws_conn_t *conn, const void *data, size_t len);
int neverc_ws_send_pong(neverc_ws_conn_t *conn, const void *data, size_t len);
int neverc_ws_send_close(neverc_ws_conn_t *conn, uint16_t code, const char *reason);

/* Read a complete text message (handles fragmentation). */
int neverc_ws_read_message(neverc_ws_conn_t *conn, char *buf, size_t buflen,
                            size_t *out_len);

/* Read one complete text or binary message and preserve its opcode. Control
 * frames are handled internally. output is not NUL-terminated. */
int neverc_ws_read_data_message(neverc_ws_conn_t *conn, int *opcode,
                                void *output, size_t output_capacity,
                                size_t *output_length);

/* Write a complete text message. */
int neverc_ws_write_message(neverc_ws_conn_t *conn, const char *msg);

/* UTF-8 helpers for callers doing their own frame assembly. */
int neverc_ws_valid_utf8(const void *data, size_t len);
int neverc_ws_valid_utf8_prefix(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_WEBSOCKET_H */
