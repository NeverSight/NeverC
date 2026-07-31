#ifndef NEVERC_NET_QUIC_H
#define NEVERC_NET_QUIC_H

/*
 * NeverC QUIC Transport (RFC 9000, RFC 9001, RFC 9002)
 *
 * The packet, frame, loss-recovery, and connection-state building blocks are
 * experimental. Endpoint handshake and UDP I/O are not integrated yet, so
 * listen/dial/accept and stream/datagram operations currently fail closed.
 *
 * Target capabilities:
 *   - UDP-based multiplexed transport
 *   - TLS 1.3 integrated handshake
 *   - Bidirectional and unidirectional streams
 *   - Connection migration
 *   - Flow control (per-stream + connection-level)
 *   - Loss detection and congestion control (New Reno / BBR)
 *   - 0-RTT early data
 *   - Connection ID rotation
 *
 * Cross-platform: POSIX (Linux/macOS/iOS/Android) + Windows.
 *
 * Target usage after endpoint integration (server):
 *   neverc_quic_config_t cfg = neverc_quic_config_default();
 *   cfg.cert_file = "cert.pem";
 *   cfg.key_file = "key.pem";
 *   const char *err = NULL;
 *   neverc_quic_endpoint_t *ep = neverc_quic_listen(":4433", &cfg, &err);
 *   neverc_quic_conn_t *conn = neverc_quic_accept(ep, &err);
 *   neverc_quic_stream_t *s = neverc_quic_accept_stream(conn, &err);
 *   neverc_quic_stream_read(s, buf, len);
 *
 * Target usage after endpoint integration (client):
 *   neverc_quic_conn_t *conn =
 *       neverc_quic_dial("example.com:4433", &cfg, &err);
 *   neverc_quic_stream_t *s = neverc_quic_open_stream(conn, &err);
 *   neverc_quic_stream_write(s, data, len);
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * QUIC Version
 * ====================================================================== */

#define NEVERC_QUIC_VERSION_1       0x00000001  /* RFC 9000 */
#define NEVERC_QUIC_VERSION_2       0x6b3343cf  /* RFC 9369 */

/* ======================================================================
 * Types
 * ====================================================================== */

typedef struct neverc_quic_endpoint neverc_quic_endpoint_t;
typedef struct neverc_quic_conn neverc_quic_conn_t;
typedef struct neverc_quic_stream neverc_quic_stream_t;

/* Stream type */
typedef enum {
    NEVERC_QUIC_STREAM_BIDI     = 0,  /* bidirectional */
    NEVERC_QUIC_STREAM_UNI_LOCAL  = 1,  /* unidirectional, locally initiated */
    NEVERC_QUIC_STREAM_UNI_REMOTE = 2,  /* unidirectional, remotely initiated */
} neverc_quic_stream_type_t;

/* Connection close reason */
typedef struct {
    uint64_t    error_code;
    const char *reason;
    int         is_app;    /* 1 = application error, 0 = transport error */
} neverc_quic_close_info_t;

/* ======================================================================
 * Configuration
 * ====================================================================== */

typedef struct {
    /* TLS */
    const char *cert_file;
    const char *key_file;
    const char **alpn;         /* NULL-terminated array, e.g. {"h3", NULL} */

    /* Limits */
    uint64_t max_idle_timeout_ms;           /* default 30000 */
    uint64_t max_stream_data_bidi_local;    /* default 1MB */
    uint64_t max_stream_data_bidi_remote;   /* default 1MB */
    uint64_t max_stream_data_uni;           /* default 1MB */
    uint64_t max_data;                      /* default 10MB */
    uint64_t max_streams_bidi;              /* default 100 */
    uint64_t max_streams_uni;               /* default 100 */
    uint32_t max_udp_payload_size;          /* default 1200 */

    /* 0-RTT */
    int      enable_0rtt;

    /* Congestion control: 0=NewReno (default), 1=BBR */
    int      congestion_algorithm;

    /* Connection migration */
    int      disable_migration;
} neverc_quic_config_t;

/* Get sensible defaults */
neverc_quic_config_t neverc_quic_config_default(void);

/* ======================================================================
 * Endpoint (listener)
 * ====================================================================== */

/* Currently returns NULL with an unsupported error. */
neverc_quic_endpoint_t *neverc_quic_listen(const char *addr,
                                            const neverc_quic_config_t *cfg,
                                            const char **errp);

/* Currently returns NULL with an unsupported error. */
neverc_quic_conn_t *neverc_quic_accept(neverc_quic_endpoint_t *ep,
                                        const char **errp);

/* Close the endpoint (stops accepting new connections). */
void neverc_quic_endpoint_close(neverc_quic_endpoint_t *ep);

/* ======================================================================
 * Client Connection
 * ====================================================================== */

/* Currently returns NULL with an unsupported error. */
neverc_quic_conn_t *neverc_quic_dial(const char *addr,
                                      const neverc_quic_config_t *cfg,
                                      const char **errp);

/* ======================================================================
 * Connection
 * ====================================================================== */

/* Close a connection with application error code and reason. */
void neverc_quic_conn_close(neverc_quic_conn_t *conn,
                             uint64_t error_code, const char *reason);

/* Get remote address string. */
const char *neverc_quic_conn_remote_addr(neverc_quic_conn_t *conn);

/* Get negotiated ALPN protocol. */
const char *neverc_quic_conn_alpn(neverc_quic_conn_t *conn);

/* Check if connection is still alive. */
int neverc_quic_conn_is_alive(neverc_quic_conn_t *conn);

/* Get close info (after connection is closed). */
int neverc_quic_conn_close_info(neverc_quic_conn_t *conn,
                                 neverc_quic_close_info_t *info);

/* ======================================================================
 * Streams
 * ====================================================================== */

/* Open a new stream (client-initiated bidirectional by default).
 * Returns NULL if stream limit reached. */
neverc_quic_stream_t *neverc_quic_open_stream(neverc_quic_conn_t *conn,
                                               const char **errp);

/* Open a unidirectional stream (send-only). */
neverc_quic_stream_t *neverc_quic_open_uni_stream(neverc_quic_conn_t *conn,
                                                    const char **errp);

/* Accept a peer-initiated stream. Blocks until available.
 * Returns NULL on connection close. */
neverc_quic_stream_t *neverc_quic_accept_stream(neverc_quic_conn_t *conn,
                                                  const char **errp);

/* Read from stream. Returns bytes read, 0 on FIN, -1 on error. */
int neverc_quic_stream_read(neverc_quic_stream_t *s, void *buf, size_t len);

/* Write to stream. Returns bytes written or -1 on error. */
int neverc_quic_stream_write(neverc_quic_stream_t *s,
                              const void *data, size_t len);

/* Signal end of stream (send FIN). No more writes allowed after this. */
int neverc_quic_stream_close_write(neverc_quic_stream_t *s);

/* Reset the stream with an error code (abrupt close). */
int neverc_quic_stream_reset(neverc_quic_stream_t *s, uint64_t error_code);

/* Stop reading from the stream (send STOP_SENDING). */
int neverc_quic_stream_stop_sending(neverc_quic_stream_t *s,
                                     uint64_t error_code);

/* Get stream ID. */
uint64_t neverc_quic_stream_id(neverc_quic_stream_t *s);

/* Free stream resources. */
void neverc_quic_stream_free(neverc_quic_stream_t *s);

/* ======================================================================
 * Datagram (RFC 9221)
 * ====================================================================== */

/* Send an unreliable datagram over the connection. */
int neverc_quic_send_datagram(neverc_quic_conn_t *conn,
                               const void *data, size_t len);

/* Receive a datagram. Returns length or -1. */
int neverc_quic_recv_datagram(neverc_quic_conn_t *conn,
                               void *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_QUIC_H */
