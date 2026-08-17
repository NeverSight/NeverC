#if !defined(NEVERC_TLS_DISABLE_TRANSPORT) && \
    !defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
#define NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT 1
#endif

#include "neverc/std/crypto/tls.h"
#include "neverc/std/_platform.h"
#include "tls_internal.h"
#include "tls_key.h"
#include "tls_key_schedule.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/net/tcp.h"

/* ======================================================================
 * Public API
 * ====================================================================== */

#if !defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
static const char k_tls_unavailable[] =
    "TLS transport was disabled at compile time";

static void tls_set_unavailable(const char **errp) {
    if (errp) *errp = k_tls_unavailable;
}
#endif

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
static const char k_tls_invalid_argument[] =
    "TLS transport received an invalid argument";
static const char k_tls_handshake_failed[] =
    "TLS 1.3 handshake failed";
static const char k_tls_allocation_failed[] =
    "TLS transport allocation failed";

neverc_tls_conn_t *nci_tls_start_handshake(
    neverc_tcp_conn_t *tcp, neverc_tls_config_t *cfg,
    int from_server, int owns_tcp, neverc_context_t *ctx,
    const char **errp) {
    if (errp)
        *errp = NULL;
    if (!tcp || !cfg ||
        (from_server &&
         (!cfg->cert_der || !cfg->key_der)) ||
        (from_server &&
         cfg->client_auth ==
             NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY &&
         !cfg->root_certificates) ||
        (!from_server && !cfg->skip_verify &&
         (!cfg->server_name || cfg->server_name[0] == '\0')) ||
        (ctx && neverc_context_done(ctx))) {
        if (errp)
            *errp = ctx && neverc_context_done(ctx)
                ? neverc_context_err(ctx) : k_tls_invalid_argument;
        return NULL;
    }

    neverc_tls_conn_t *conn = nci_tls_conn_new(tcp, owns_tcp);
    if (!conn) {
        if (errp)
            *errp = k_tls_allocation_failed;
        return NULL;
    }
    conn->is_server = from_server != 0;
    nci_tls_config_retain(cfg);
    conn->config = cfg;
#if defined(NEVERC_TLS_TESTING)
    conn->test_handshake_fragment_size =
        cfg->test_handshake_fragment_size;
#endif
    conn->read_context = ctx;
    conn->write_context = ctx;
    int result = from_server ?
        nci_tls_server_handshake(conn, cfg) :
        nci_tls_client_handshake(conn, cfg);
    conn->read_context = NULL;
    conn->write_context = NULL;
    if (result != 0) {
        const char *failure_reason = conn->failure_reason;
        conn->owns_tcp = 0;
        neverc_tls_close(conn);
        if (errp)
            *errp = failure_reason ?
                failure_reason : k_tls_handshake_failed;
        return NULL;
    }
    return conn;
}
#endif

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
static int tls_split_dial_host(const char *addr, char *host, size_t hostlen) {
    if (!addr || !host || hostlen == 0)
        return -1;
    host[0] = '\0';
    if (addr[0] == '[') {
        const char *end = strchr(addr, ']');
        if (!end || end == addr + 1)
            return -1;
        size_t n = (size_t)(end - addr - 1);
        if (n >= hostlen)
            return -1;
        memcpy(host, addr + 1, n);
        host[n] = '\0';
        return 0;
    }
    const char *colon = strrchr(addr, ':');
    if (!colon || colon == addr)
        return -1;
    for (const char *p = addr; p < colon; p++) {
        if (*p == ':')
            return -1;
    }
    size_t n = (size_t)(colon - addr);
    if (n == 0 || n >= hostlen)
        return -1;
    memcpy(host, addr, n);
    host[n] = '\0';
    return 0;
}
#endif

neverc_tls_conn_t *neverc_tls_dial(const char *addr,
                                    neverc_tls_config_t *cfg,
                                    const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (!addr || !cfg) {
        if (errp)
            *errp = k_tls_invalid_argument;
        return NULL;
    }
    if (!cfg->server_name || cfg->server_name[0] == '\0') {
        char host[TLS_MAX_SERVER_NAME + 1];
        if (tls_split_dial_host(addr, host, sizeof(host)) == 0 &&
            host[0] != '\0')
            neverc_tls_config_set_server_name(cfg, host);
    }
    const char *tcp_error = NULL;
    neverc_tcp_conn_t *tcp = neverc_tcp_dial(addr, &tcp_error);
    if (!tcp) {
        if (errp)
            *errp = tcp_error ? tcp_error : k_tls_handshake_failed;
        return NULL;
    }
    neverc_tls_conn_t *conn =
        nci_tls_start_handshake(tcp, cfg, 0, 1, NULL, errp);
    if (!conn)
        neverc_tcp_close(tcp);
    return conn;
#else
    (void)addr;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

neverc_tls_conn_t *neverc_tls_server(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    return nci_tls_start_handshake(tcp, cfg, 1, 0, NULL, errp);
#else
    (void)tcp;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

neverc_tls_conn_t *neverc_tls_client(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    return nci_tls_start_handshake(tcp, cfg, 0, 0, NULL, errp);
#else
    (void)tcp;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

neverc_tls_conn_t *neverc_tls_client_context(
    neverc_tcp_conn_t *tcp, neverc_tls_config_t *cfg,
    neverc_context_t *ctx, const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (!ctx) {
        if (errp) *errp = k_tls_invalid_argument;
        return NULL;
    }
    return nci_tls_start_handshake(tcp, cfg, 0, 0, ctx, errp);
#else
    (void)tcp;
    (void)cfg;
    (void)ctx;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

neverc_tls_conn_t *neverc_tls_server_begin(
    neverc_tcp_conn_t *tcp, neverc_tls_config_t *cfg, const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (errp) *errp = NULL;
    if (!tcp || !cfg || !cfg->cert_der || !cfg->key_der ||
        (cfg->client_auth ==
             NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY &&
         !cfg->root_certificates)) {
        if (errp) *errp = k_tls_invalid_argument;
        return NULL;
    }
    neverc_tls_conn_t *conn = nci_tls_conn_new(tcp, 0);
    if (!conn) {
        if (errp) *errp = k_tls_allocation_failed;
        return NULL;
    }
    conn->is_server = 1;
    nci_tls_config_retain(cfg);
    conn->config = cfg;
#if defined(NEVERC_TLS_TESTING)
    conn->test_handshake_fragment_size =
        cfg->test_handshake_fragment_size;
#endif
    if (nci_tls_server_handshake_begin(conn, cfg) != 0) {
        conn->owns_tcp = 0;
        neverc_tls_close(conn);
        if (errp) *errp = k_tls_allocation_failed;
        return NULL;
    }
    return conn;
#else
    (void)tcp;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

int neverc_tls_handshake_step(neverc_tls_conn_t *conn, const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (errp) *errp = NULL;
    if (!conn || !conn->is_server || !conn->async_handshake) {
        if (errp) *errp = k_tls_invalid_argument;
        return NEVERC_TLS_HANDSHAKE_ERROR;
    }
    int result = nci_tls_server_handshake_step(conn);
    if (result == NCI_TLS_WANT_READ)
        return NEVERC_TLS_HANDSHAKE_WANT_READ;
    if (result == NCI_TLS_WANT_WRITE)
        return NEVERC_TLS_HANDSHAKE_WANT_WRITE;
    if (result != 0) {
        if (errp)
            *errp = conn->failure_reason
                ? conn->failure_reason : k_tls_handshake_failed;
        return NEVERC_TLS_HANDSHAKE_ERROR;
    }
    nci_tls_async_handshake_free(conn);
    return NEVERC_TLS_HANDSHAKE_COMPLETE;
#else
    (void)conn;
    tls_set_unavailable(errp);
    return NEVERC_TLS_HANDSHAKE_ERROR;
#endif
}

static int nci_tls_read_unlocked(
    neverc_tls_conn_t *conn, void *buf, size_t buflen) {
    if (!conn || !buf || buflen == 0)
        return -1;
    if (conn->closed || !conn->handshake_done)
        return -1;

    /* Return bytes transferred by a previous reactor owner first. */
    if (conn->preload_app_len > conn->preload_app_pos) {
        size_t available = conn->preload_app_len - conn->preload_app_pos;
        size_t count = buflen < available ? buflen : available;
        memcpy(buf, conn->preload_app_buf + conn->preload_app_pos, count);
        conn->preload_app_pos += count;
        if (conn->preload_app_pos == conn->preload_app_len) {
            neverc_platform_secure_zero(
                conn->preload_app_buf, conn->preload_app_len);
            free(conn->preload_app_buf);
            conn->preload_app_buf = NULL;
            conn->preload_app_len = 0;
            conn->preload_app_pos = 0;
        }
        return (int)count;
    }

    /* Return record-layer buffered data next. */
    if (conn->decrypt_buf_len > conn->decrypt_buf_pos) {
        size_t avail = conn->decrypt_buf_len - conn->decrypt_buf_pos;
        size_t n = buflen < avail ? buflen : avail;
        memcpy(buf, conn->decrypt_buf + conn->decrypt_buf_pos, n);
        conn->decrypt_buf_pos += n;
        if (conn->decrypt_buf_pos >= conn->decrypt_buf_len) {
            conn->decrypt_buf_pos = 0;
            conn->decrypt_buf_len = 0;
        }
        return (int)n;
    }
    if (conn->peer_closed)
        return 0;

    for (;;) {
        uint8_t inner_type;
        uint8_t data[TLS_MAX_PLAINTEXT];
        size_t data_len;

        int receive_result = nci_tls_recv_decrypt(
            conn, &inner_type, data, &data_len);
        if (receive_result == NCI_TLS_WANT_READ)
            return NCI_TLS_IO_WANT_READ;
        if (receive_result != 0) return -1;

        if (conn->post_handshake_len > 0 &&
            inner_type != TLS_CT_HANDSHAKE)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "TLS handshake messages were interleaved");

        if (inner_type == TLS_CT_ALERT) {
            if (++conn->non_advancing_records >
                TLS_MAX_NON_ADVANCING_RECORDS)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "too many non-advancing TLS records");
            int alert_result =
                nci_tls_handle_peer_alert(conn, data, data_len);
            if (alert_result > 0)
                return 0;
            if (alert_result < 0)
                return -1;
            continue;
        }

        if (inner_type == TLS_CT_HANDSHAKE) {
            if (nci_tls_handle_post_handshake(
                    conn, data, data_len) != 0)
                return -1;
            if (conn->nonblocking_io &&
                conn->pending_write_len > conn->pending_write_pos)
                return NCI_TLS_IO_WANT_WRITE;
            continue;
        }

        if (inner_type != TLS_CT_APPLICATION_DATA)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "received an unexpected TLS record");
        if (data_len == 0) {
            if (++conn->non_advancing_records >
                TLS_MAX_NON_ADVANCING_RECORDS)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "too many non-advancing TLS records");
            continue;
        }
        conn->non_advancing_records = 0;

        size_t n = buflen < data_len ? buflen : data_len;
        memcpy(buf, data, n);

        if (data_len > n) {
            size_t rem = data_len - n;
            memcpy(conn->decrypt_buf, data + n, rem);
            conn->decrypt_buf_len = rem;
            conn->decrypt_buf_pos = 0;
        }

        return (int)n;
    }
}

int neverc_tls_read(
    neverc_tls_conn_t *conn, void *buf, size_t buflen) {
    if (!conn || !conn->mutexes_initialized)
        return -1;
    tls_mutex_lock(&conn->read_mutex);
    int result = nci_tls_read_unlocked(conn, buf, buflen);
    tls_mutex_unlock(&conn->read_mutex);
    return result;
}

int neverc_tls_read_context(neverc_tls_conn_t *conn, neverc_context_t *ctx,
                            void *buf, size_t buflen) {
    if (!conn || !conn->mutexes_initialized || !ctx ||
        neverc_context_done(ctx))
        return -1;
    tls_mutex_lock(&conn->read_mutex);
    conn->read_context = ctx;
    int result = nci_tls_read_unlocked(conn, buf, buflen);
    conn->read_context = NULL;
    tls_mutex_unlock(&conn->read_mutex);
    return result;
}

static neverc_tls_io_result_t tls_io_result(
    neverc_tls_io_status_t status, size_t transferred) {
    neverc_tls_io_result_t result;
    result.status = status;
    result.transferred = transferred;
    return result;
}

neverc_tls_io_result_t neverc_tls_flush(neverc_tls_conn_t *conn) {
    if (!conn || !conn->nonblocking_io || !conn->mutexes_initialized)
        return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
    tls_mutex_lock(&conn->write_mutex);
    int result = nci_tls_flush_pending_write(conn);
    tls_mutex_unlock(&conn->write_mutex);
    if (result == 0) return tls_io_result(NEVERC_TLS_IO_OK, 0);
    if (result == NCI_TLS_WANT_WRITE)
        return tls_io_result(NEVERC_TLS_IO_WANT_WRITE, 0);
    return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
}

neverc_tls_io_result_t neverc_tls_try_read(
    neverc_tls_conn_t *conn, void *buffer, size_t capacity) {
    if (!conn || !conn->nonblocking_io || !conn->mutexes_initialized ||
        !buffer || capacity == 0 || capacity > (size_t)INT_MAX)
        return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
    neverc_tls_io_result_t flush_result = neverc_tls_flush(conn);
    if (flush_result.status != NEVERC_TLS_IO_OK) return flush_result;

    tls_mutex_lock(&conn->read_mutex);
    int result = nci_tls_read_unlocked(conn, buffer, capacity);
    tls_mutex_unlock(&conn->read_mutex);
    if (result > 0)
        return tls_io_result(NEVERC_TLS_IO_OK, (size_t)result);
    if (result == 0) return tls_io_result(NEVERC_TLS_IO_EOF, 0);
    if (result == NCI_TLS_IO_WANT_READ)
        return tls_io_result(NEVERC_TLS_IO_WANT_READ, 0);
    if (result == NCI_TLS_IO_WANT_WRITE)
        return tls_io_result(NEVERC_TLS_IO_WANT_WRITE, 0);
    return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
}

neverc_tls_io_result_t neverc_tls_try_write(
    neverc_tls_conn_t *conn, const void *data, size_t length) {
    if (!conn || !conn->nonblocking_io || !conn->mutexes_initialized ||
        !data || length == 0)
        return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
    tls_mutex_lock(&conn->write_mutex);
    int flush_result = nci_tls_flush_pending_write(conn);
    if (flush_result == NCI_TLS_WANT_WRITE) {
        tls_mutex_unlock(&conn->write_mutex);
        return tls_io_result(NEVERC_TLS_IO_WANT_WRITE, 0);
    }
    if (flush_result != 0 || conn->closed || conn->write_closed ||
        !conn->handshake_done || !conn->application_keys_active) {
        tls_mutex_unlock(&conn->write_mutex);
        return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
    }
    size_t accepted = length;
    if (accepted > TLS_MAX_PLAINTEXT - 1) accepted = TLS_MAX_PLAINTEXT - 1;
    if (nci_tls_send_encrypted_unlocked(
            conn, TLS_CT_APPLICATION_DATA,
            (const uint8_t *)data, accepted) != 0) {
        tls_mutex_unlock(&conn->write_mutex);
        return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
    }
    flush_result = nci_tls_flush_pending_write(conn);
    tls_mutex_unlock(&conn->write_mutex);
    if (flush_result == NCI_TLS_WANT_WRITE)
        return tls_io_result(
            NEVERC_TLS_IO_WANT_WRITE, accepted);
    if (flush_result != 0)
        return tls_io_result(NEVERC_TLS_IO_ERROR, accepted);
    return tls_io_result(NEVERC_TLS_IO_OK, accepted);
}

neverc_tls_io_result_t neverc_tls_try_close_notify(
    neverc_tls_conn_t *conn) {
    if (!conn || !conn->nonblocking_io || !conn->mutexes_initialized)
        return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
    if (!conn->handshake_done)
        return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
    if (!conn->alert_sent && !conn->closed &&
        nci_tls_send_close_notify(conn) != 0)
        return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
    tls_mutex_lock(&conn->write_mutex);
    int flush_result = nci_tls_flush_pending_write(conn);
    tls_mutex_unlock(&conn->write_mutex);
    if (flush_result == 0)
        return tls_io_result(NEVERC_TLS_IO_OK, 0);
    if (flush_result == NCI_TLS_WANT_WRITE)
        return tls_io_result(NEVERC_TLS_IO_WANT_WRITE, 0);
    return tls_io_result(NEVERC_TLS_IO_ERROR, 0);
}

int neverc_tls_set_reactor_mode(neverc_tls_conn_t *conn, int enabled) {
    if (!conn || !conn->mutexes_initialized || !conn->handshake_done ||
        (enabled != 0 && enabled != 1))
        return -1;
    tls_mutex_lock(&conn->write_mutex);
    if (!enabled &&
        conn->pending_write_pos < conn->pending_write_len) {
        tls_mutex_unlock(&conn->write_mutex);
        return -1;
    }
    conn->nonblocking_io = enabled;
    tls_mutex_unlock(&conn->write_mutex);
    return 0;
}

int neverc_tls_preload_application_data(
    neverc_tls_conn_t *conn, const void *data, size_t length) {
    if (!conn || (!data && length > 0) || length > TLS_MAX_PENDING_WRITE ||
        !conn->mutexes_initialized || !conn->handshake_done)
        return -1;
    if (length == 0) return 0;

    tls_mutex_lock(&conn->read_mutex);
    size_t remaining = conn->preload_app_len - conn->preload_app_pos;
    if (remaining > TLS_MAX_PENDING_WRITE ||
        length > TLS_MAX_PENDING_WRITE - remaining) {
        tls_mutex_unlock(&conn->read_mutex);
        return -1;
    }
    uint8_t *combined = (uint8_t *)malloc(remaining + length);
    if (!combined) {
        tls_mutex_unlock(&conn->read_mutex);
        return -1;
    }
    memcpy(combined, data, length);
    if (remaining > 0)
        memcpy(combined + length,
               conn->preload_app_buf + conn->preload_app_pos, remaining);
    if (conn->preload_app_buf) {
        neverc_platform_secure_zero(
            conn->preload_app_buf, conn->preload_app_len);
        free(conn->preload_app_buf);
    }
    conn->preload_app_buf = combined;
    conn->preload_app_len = remaining + length;
    conn->preload_app_pos = 0;
    tls_mutex_unlock(&conn->read_mutex);
    return 0;
}

static int nci_tls_write_unlocked(neverc_tls_conn_t *conn,
                                  const void *data, size_t len) {
    if (conn->closed || conn->write_closed ||
        !conn->handshake_done || !conn->application_keys_active) {
        return -1;
    }

    const uint8_t *p = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t chunk = remaining < (TLS_MAX_PLAINTEXT - 1)
                     ? remaining : (TLS_MAX_PLAINTEXT - 1);
        if (nci_tls_send_encrypted_unlocked(
                conn, TLS_CT_APPLICATION_DATA, p, chunk) != 0) {
            conn->closed = 1;
            return -1;
        }
        p += chunk;
        remaining -= chunk;
    }

    return (int)len;
}

int neverc_tls_write(neverc_tls_conn_t *conn, const void *data, size_t len) {
    if (!conn || !conn->mutexes_initialized || !data || len == 0 ||
        len > (size_t)INT_MAX)
        return -1;
    tls_mutex_lock(&conn->write_mutex);
    int result = nci_tls_write_unlocked(conn, data, len);
    tls_mutex_unlock(&conn->write_mutex);
    return result;
}

int neverc_tls_write_context(neverc_tls_conn_t *conn, neverc_context_t *ctx,
                             const void *data, size_t len) {
    if (!conn || !conn->mutexes_initialized || !ctx || !data || len == 0 ||
        len > (size_t)INT_MAX || neverc_context_done(ctx))
        return -1;
    tls_mutex_lock(&conn->write_mutex);
    conn->write_context = ctx;
    int result = nci_tls_write_unlocked(conn, data, len);
    conn->write_context = NULL;
    tls_mutex_unlock(&conn->write_mutex);
    return result;
}

int neverc_tls_shutdown_read(neverc_tls_conn_t *conn) {
    if (!conn || !conn->tcp) return -1;
    return neverc_tcp_shutdown_read(conn->tcp);
}

int neverc_tls_shutdown_write(neverc_tls_conn_t *conn) {
    if (!conn || !conn->tcp || !conn->mutexes_initialized) return -1;
    tls_mutex_lock(&conn->write_mutex);
    conn->write_closed = 1;
    int result = neverc_tcp_shutdown_write(conn->tcp);
    tls_mutex_unlock(&conn->write_mutex);
    return result;
}

int neverc_tls_key_update(
    neverc_tls_conn_t *conn, int request_peer_update) {
    return nci_tls_send_key_update_message(
        conn, request_peer_update);
}

void neverc_tls_close(neverc_tls_conn_t *conn) {
    if (!conn) return;
    neverc_tls_config_t *config = conn->config;

    if (conn->tcp && conn->handshake_done && !conn->closed &&
        !conn->write_closed && !conn->alert_sent)
        (void)nci_tls_send_close_notify(conn);

    if (conn->tcp && conn->owns_tcp)
        neverc_tcp_close(conn->tcp);
    free(conn->alpn);
    free(conn->server_name);
    free(conn->resumption_alpn);
    free(conn->peer_cert);
    neverc_x509_cert_pool_free(conn->peer_intermediates);
    nci_tls_async_handshake_free(conn);
    nci_tls_clear_handshake_buffer(conn);
    if (conn->post_handshake_buf) {
        neverc_platform_secure_zero(
            conn->post_handshake_buf,
            conn->post_handshake_cap);
        free(conn->post_handshake_buf);
    }
    if (conn->preload_app_buf) {
        neverc_platform_secure_zero(
            conn->preload_app_buf, conn->preload_app_len);
        free(conn->preload_app_buf);
    }
    if (conn->pending_write_buf) {
        neverc_platform_secure_zero(
            conn->pending_write_buf,
            conn->pending_write_cap);
        free(conn->pending_write_buf);
    }
    if (conn->mutexes_initialized) {
        tls_mutex_destroy(&conn->read_mutex);
        tls_mutex_destroy(&conn->write_mutex);
        conn->mutexes_initialized = 0;
    }
    neverc_platform_secure_zero(conn, sizeof(*conn));
    free(conn);
    neverc_tls_config_free(config);
}

const char *neverc_tls_alpn(neverc_tls_conn_t *conn) {
    return conn ? conn->alpn : NULL;
}

const char *neverc_tls_server_name(neverc_tls_conn_t *conn) {
    return conn ? conn->server_name : NULL;
}

uint16_t neverc_tls_cipher_suite(neverc_tls_conn_t *conn) {
    return conn ? conn->cipher_suite : 0;
}

const uint8_t *neverc_tls_peer_certificate(neverc_tls_conn_t *conn,
                                            size_t *out_len) {
    if (!conn || !conn->peer_cert) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (out_len) *out_len = conn->peer_cert_len;
    return conn->peer_cert;
}

/* --- TLS Listener --- */

neverc_tls_listener_t *neverc_tls_listen(const char *addr,
                                          neverc_tls_config_t *cfg,
                                          const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (!addr || !cfg || !cfg->cert_der || !cfg->key_der ||
        (cfg->client_auth ==
             NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY &&
         !cfg->root_certificates)) {
        if (errp)
            *errp = k_tls_invalid_argument;
        return NULL;
    }
    neverc_tcp_listener_t *tcp_listener =
        neverc_tcp_listen(addr, errp);
    if (!tcp_listener)
        return NULL;
    neverc_tls_listener_t *listener =
        (neverc_tls_listener_t *)calloc(1, sizeof(*listener));
    if (!listener) {
        neverc_tcp_listener_close(tcp_listener);
        if (errp)
            *errp = k_tls_allocation_failed;
        return NULL;
    }
    listener->tcp_ln = tcp_listener;
    nci_tls_config_retain(cfg);
    listener->cfg = cfg;
    if (errp)
        *errp = NULL;
    return listener;
#else
    (void)addr;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

neverc_tls_conn_t *neverc_tls_accept(neverc_tls_listener_t *ln,
                                      const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (!ln || !ln->tcp_ln || !ln->cfg) {
        if (errp)
            *errp = k_tls_invalid_argument;
        return NULL;
    }
    neverc_tcp_conn_t *tcp =
        neverc_tcp_accept(ln->tcp_ln, errp);
    if (!tcp)
        return NULL;
    neverc_tls_conn_t *conn =
        nci_tls_start_handshake(tcp, ln->cfg, 1, 1, NULL, errp);
    if (!conn)
        neverc_tcp_close(tcp);
    return conn;
#else
    (void)ln;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

void neverc_tls_listener_close(neverc_tls_listener_t *ln) {
    if (!ln) return;
    neverc_tls_config_t *config = ln->cfg;
    neverc_tcp_listener_close(ln->tcp_ln);
    free(ln);
    neverc_tls_config_free(config);
}
