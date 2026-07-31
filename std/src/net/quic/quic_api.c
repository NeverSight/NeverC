#include "neverc/std/net/quic.h"

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <errno.h>
#endif

/*
 * The packet, frame, loss-recovery, and connection-state components are not
 * wired into an interoperable endpoint yet. Keep the public transport surface
 * linkable, but fail closed until RFC 9000/9001 handshake and I/O integration
 * are complete.
 */

static const char k_quic_unavailable[] =
    "QUIC transport is unavailable: endpoint handshake is not implemented";

static void quic_set_unsupported(const char **errp) {
    if (errp)
        *errp = k_quic_unavailable;
#ifdef _WIN32
    WSASetLastError(WSAEOPNOTSUPP);
#else
    errno = ENOSYS;
#endif
}

neverc_quic_config_t neverc_quic_config_default(void) {
    neverc_quic_config_t cfg = {0};
    cfg.max_idle_timeout_ms = 30000;
    cfg.max_stream_data_bidi_local = 1024 * 1024;
    cfg.max_stream_data_bidi_remote = 1024 * 1024;
    cfg.max_stream_data_uni = 1024 * 1024;
    cfg.max_data = 10 * 1024 * 1024;
    cfg.max_streams_bidi = 100;
    cfg.max_streams_uni = 100;
    cfg.max_udp_payload_size = 1200;
    return cfg;
}

neverc_quic_endpoint_t *neverc_quic_listen(
    const char *addr, const neverc_quic_config_t *cfg, const char **errp) {
    (void)addr;
    (void)cfg;
    quic_set_unsupported(errp);
    return NULL;
}

neverc_quic_conn_t *neverc_quic_accept(neverc_quic_endpoint_t *ep,
                                        const char **errp) {
    (void)ep;
    quic_set_unsupported(errp);
    return NULL;
}

void neverc_quic_endpoint_close(neverc_quic_endpoint_t *ep) {
    (void)ep;
}

neverc_quic_conn_t *neverc_quic_dial(
    const char *addr, const neverc_quic_config_t *cfg, const char **errp) {
    (void)addr;
    (void)cfg;
    quic_set_unsupported(errp);
    return NULL;
}

void neverc_quic_conn_close(neverc_quic_conn_t *conn,
                             uint64_t error_code, const char *reason) {
    (void)conn;
    (void)error_code;
    (void)reason;
    quic_set_unsupported(NULL);
}

const char *neverc_quic_conn_remote_addr(neverc_quic_conn_t *conn) {
    (void)conn;
    quic_set_unsupported(NULL);
    return NULL;
}

const char *neverc_quic_conn_alpn(neverc_quic_conn_t *conn) {
    (void)conn;
    quic_set_unsupported(NULL);
    return NULL;
}

int neverc_quic_conn_is_alive(neverc_quic_conn_t *conn) {
    (void)conn;
    quic_set_unsupported(NULL);
    return 0;
}

int neverc_quic_conn_close_info(neverc_quic_conn_t *conn,
                                 neverc_quic_close_info_t *info) {
    (void)conn;
    (void)info;
    quic_set_unsupported(NULL);
    return -1;
}

neverc_quic_stream_t *neverc_quic_open_stream(neverc_quic_conn_t *conn,
                                               const char **errp) {
    (void)conn;
    quic_set_unsupported(errp);
    return NULL;
}

neverc_quic_stream_t *neverc_quic_open_uni_stream(neverc_quic_conn_t *conn,
                                                   const char **errp) {
    (void)conn;
    quic_set_unsupported(errp);
    return NULL;
}

neverc_quic_stream_t *neverc_quic_accept_stream(neverc_quic_conn_t *conn,
                                                 const char **errp) {
    (void)conn;
    quic_set_unsupported(errp);
    return NULL;
}

int neverc_quic_stream_read(neverc_quic_stream_t *s, void *buf, size_t len) {
    (void)s;
    (void)buf;
    (void)len;
    quic_set_unsupported(NULL);
    return -1;
}

int neverc_quic_stream_write(neverc_quic_stream_t *s,
                              const void *data, size_t len) {
    (void)s;
    (void)data;
    (void)len;
    quic_set_unsupported(NULL);
    return -1;
}

int neverc_quic_stream_close_write(neverc_quic_stream_t *s) {
    (void)s;
    quic_set_unsupported(NULL);
    return -1;
}

int neverc_quic_stream_reset(neverc_quic_stream_t *s, uint64_t error_code) {
    (void)s;
    (void)error_code;
    quic_set_unsupported(NULL);
    return -1;
}

int neverc_quic_stream_stop_sending(neverc_quic_stream_t *s,
                                     uint64_t error_code) {
    (void)s;
    (void)error_code;
    quic_set_unsupported(NULL);
    return -1;
}

uint64_t neverc_quic_stream_id(neverc_quic_stream_t *s) {
    (void)s;
    quic_set_unsupported(NULL);
    return UINT64_MAX;
}

void neverc_quic_stream_free(neverc_quic_stream_t *s) {
    (void)s;
    quic_set_unsupported(NULL);
}

int neverc_quic_send_datagram(neverc_quic_conn_t *conn,
                               const void *data, size_t len) {
    (void)conn;
    (void)data;
    (void)len;
    quic_set_unsupported(NULL);
    return -1;
}

int neverc_quic_recv_datagram(neverc_quic_conn_t *conn,
                               void *buf, size_t buflen) {
    (void)conn;
    (void)buf;
    (void)buflen;
    quic_set_unsupported(NULL);
    return -1;
}
