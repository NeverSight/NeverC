#include "neverc/std/net/quic.h"

#include "_quic_internal.h"
#include "../_net_socket.h"
#include "../_net_thread.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/crypto/tls.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char k_quic_invalid_config[] = "invalid QUIC configuration";
static const char k_quic_closed[] = "QUIC connection is closed";
static const char k_quic_stream_limit[] = "QUIC stream limit reached";

static char *quic_strdup(const char *value) {
    if (!value) return NULL;
    size_t length = strlen(value);
    char *copy = (char *)malloc(length + 1U);
    if (copy) memcpy(copy, value, length + 1U);
    return copy;
}

static void quic_set_error(const char **errp, const char *error) {
    if (errp) *errp = error;
}

neverc_quic_config_t neverc_quic_config_default(void) {
    neverc_quic_config_t config;
    memset(&config, 0, sizeof(config));
    config.max_idle_timeout_ms = 30000;
    config.max_stream_data_bidi_local = 1024U * 1024U;
    config.max_stream_data_bidi_remote = 1024U * 1024U;
    config.max_stream_data_uni = 1024U * 1024U;
    config.max_data = 10U * 1024U * 1024U;
    config.max_streams_bidi = 100;
    config.max_streams_uni = 100;
    config.max_udp_payload_size = QUIC_DEFAULT_MAX_PACKET_SIZE;
    return config;
}

static int quic_config_valid(const neverc_quic_config_t *config,
                             int server) {
    if (!config || config->max_idle_timeout_ms == 0 ||
        config->max_idle_timeout_ms > QUIC_VARINT_MAX ||
        config->max_stream_data_bidi_local > QUIC_VARINT_MAX ||
        config->max_stream_data_bidi_remote > QUIC_VARINT_MAX ||
        config->max_stream_data_uni > QUIC_VARINT_MAX ||
        config->max_data > QUIC_VARINT_MAX ||
        config->max_streams_bidi > QUIC_MAX_STREAMS ||
        config->max_streams_uni > QUIC_MAX_STREAMS ||
        config->max_udp_payload_size < QUIC_MIN_INITIAL_SIZE ||
        config->max_udp_payload_size > QUIC_MAX_PACKET_SIZE ||
        (config->insecure_skip_verify != 0 &&
         config->insecure_skip_verify != 1) ||
        (config->disable_migration != 0 &&
         config->disable_migration != 1) ||
        config->enable_0rtt || config->congestion_algorithm != 0 ||
        (server && (!config->cert_file || !config->key_file)) ||
        (server && (config->server_name || config->root_cert_file ||
                    config->insecure_skip_verify)) ||
        (!server && (config->cert_file || config->key_file)))
        return 0;
    if (config->alpn) {
        size_t count = 0;
        size_t encoded = 0;
        while (config->alpn[count]) {
            size_t length = strlen(config->alpn[count]);
            if (length == 0 || length > 255 || count == 32 ||
                encoded > 2048U - 1U - length)
                return 0;
            encoded += 1U + length;
            count++;
        }
        if (count == 0) return 0;
    }
    return 1;
}

static int quic_server_credentials_valid(
    const neverc_quic_config_t *config) {
    neverc_tls_config_t *tls_config = neverc_tls_config_new();
    if (!tls_config) return 0;
    int valid = neverc_tls_config_load_cert(
        tls_config, config->cert_file, config->key_file) == 0;
    neverc_tls_config_free(tls_config);
    return valid;
}

static int quic_clone_config(neverc_quic_endpoint_t *endpoint,
                             const neverc_quic_config_t *source) {
    endpoint->config = *source;
    endpoint->cert_file = quic_strdup(source->cert_file);
    endpoint->key_file = quic_strdup(source->key_file);
    if ((source->cert_file && !endpoint->cert_file) ||
        (source->key_file && !endpoint->key_file))
        return -1;
    endpoint->config.cert_file = endpoint->cert_file;
    endpoint->config.key_file = endpoint->key_file;
    if (source->alpn) {
        while (source->alpn[endpoint->alpn_count]) endpoint->alpn_count++;
        endpoint->alpn = (char **)calloc(endpoint->alpn_count + 1U,
                                          sizeof(char *));
        if (!endpoint->alpn) return -1;
        for (size_t i = 0; i < endpoint->alpn_count; i++) {
            endpoint->alpn[i] = quic_strdup(source->alpn[i]);
            if (!endpoint->alpn[i]) return -1;
        }
        endpoint->config.alpn = (const char **)endpoint->alpn;
    }
    return 0;
}

static void quic_free_endpoint_config(neverc_quic_endpoint_t *endpoint) {
    if (!endpoint) return;
    for (size_t i = 0; i < endpoint->alpn_count; i++)
        free(endpoint->alpn[i]);
    free(endpoint->alpn);
    free(endpoint->cert_file);
    free(endpoint->key_file);
}

static void quic_endpoint_release(neverc_quic_endpoint_t *endpoint) {
    if (!endpoint || nc_atomic_dec(&endpoint->ref_count) != 0) return;
    neverc_udp_close(endpoint->udp);
    quic_free_endpoint_config(endpoint);
    nc_mutex_destroy(&endpoint->lock);
    nc_cond_destroy(&endpoint->accept_cond);
    free(endpoint);
}

static int quic_parse_invariant_header(const uint8_t *packet, size_t length,
                                       uint32_t *version,
                                       quic_packet_type_t *type,
                                       quic_conn_id_t *dcid,
                                       quic_conn_id_t *scid) {
    if (!packet || length < 7 || (packet[0] & 0xc0U) != 0xc0U ||
        !version || !type || !dcid || !scid)
        return -1;
    *version = ((uint32_t)packet[1] << 24) |
               ((uint32_t)packet[2] << 16) |
               ((uint32_t)packet[3] << 8) | packet[4];
    uint8_t encoded = (packet[0] >> 4) & 3U;
    if (*version == NEVERC_QUIC_VERSION_2) {
        static const quic_packet_type_t v2_types[4] = {
            QUIC_PKT_RETRY, QUIC_PKT_INITIAL, QUIC_PKT_0RTT,
            QUIC_PKT_HANDSHAKE};
        *type = v2_types[encoded];
    } else {
        *type = (quic_packet_type_t)encoded;
    }
    size_t position = 5;
    dcid->len = packet[position++];
    if (dcid->len > QUIC_MAX_CID_LEN || dcid->len > length - position)
        return -1;
    memcpy(dcid->data, packet + position, dcid->len);
    position += dcid->len;
    if (position == length) return -1;
    scid->len = packet[position++];
    if (scid->len > QUIC_MAX_CID_LEN || scid->len > length - position)
        return -1;
    memcpy(scid->data, packet + position, scid->len);
    return 0;
}

static int quic_extract_destination_cid(const uint8_t *packet, size_t length,
                                        uint32_t *version,
                                        quic_packet_type_t *type,
                                        quic_conn_id_t *dcid,
                                        quic_conn_id_t *scid) {
    if (!packet || length == 0 || !version || !type || !dcid || !scid ||
        (packet[0] & 0x40U) == 0)
        return -1;
    if (packet[0] & 0x80U)
        return quic_parse_invariant_header(packet, length, version, type,
                                           dcid, scid);
    if (length < 1U + 8U) return -1;
    *version = NEVERC_QUIC_VERSION_1;
    *type = QUIC_PKT_1RTT;
    dcid->len = 8;
    memcpy(dcid->data, packet + 1, dcid->len);
    memset(scid, 0, sizeof(*scid));
    return 0;
}

static neverc_quic_conn_t *quic_endpoint_find_connection(
    neverc_quic_endpoint_t *endpoint, const quic_conn_id_t *dcid) {
    for (size_t i = 0; i < endpoint->connection_count; i++) {
        neverc_quic_conn_t *conn = endpoint->connections[i];
        if (conn && neverc_quic_conn_id_matches(conn, dcid->data, dcid->len))
            return conn;
    }
    return NULL;
}

static void quic_send_version_negotiation(neverc_quic_endpoint_t *endpoint,
                                          const neverc_udp_addr_t *peer,
                                          const quic_conn_id_t *dcid,
                                          const quic_conn_id_t *scid) {
    uint8_t packet[64];
    uint8_t first = 0;
    if (neverc_crypto_rand_read(&first, 1) != 0) return;
    /* RFC 8999: Version Negotiation SHOULD appear to have the Fixed Bit. */
    first = (uint8_t)(0xC0U | (first & 0x3fU));
    uint32_t versions[2] = { NEVERC_QUIC_VERSION_1, NEVERC_QUIC_VERSION_2 };
    size_t written = 0;
    /* Wire DCID is the client's SCID; wire SCID is the client's DCID. */
    if (neverc_quic_write_version_negotiation(packet, sizeof(packet), first,
                                              scid, dcid, versions, 2,
                                              &written) != 0)
        return;
    (void)neverc_udp_try_write(endpoint->udp, packet, written, peer);
}

static neverc_quic_conn_t *quic_endpoint_create_connection(
    neverc_quic_endpoint_t *endpoint, const quic_conn_id_t *dcid,
    const quic_conn_id_t *scid, const neverc_udp_addr_t *peer,
    uint32_t version) {
    if (!endpoint || endpoint->connection_count == QUIC_MAX_CONNECTIONS ||
        (version != NEVERC_QUIC_VERSION_1 &&
         version != NEVERC_QUIC_VERSION_2))
        return NULL;
    neverc_quic_conn_t *conn = neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    if (!conn) return NULL;
    /* RFC 9369: v2 Initials use a different salt, HKDF labels, and
     * long-header type map. create() defaults to v1; stamp the wire
     * version before configure() derives Initial keys. */
    conn->version = version;
    if (neverc_quic_conn_configure(conn, &endpoint->config, endpoint->udp, 0,
                                    peer, endpoint, dcid, scid, NULL) != 0) {
        neverc_quic_conn_destroy(conn);
        return NULL;
    }
    endpoint->connections[endpoint->connection_count++] = conn;
    return conn;
}

static void quic_endpoint_enqueue_accept(neverc_quic_endpoint_t *endpoint,
                                         neverc_quic_conn_t *conn) {
    if (!endpoint || !conn || conn->accept_enqueued ||
        conn->state != QUIC_CONN_ESTABLISHED)
        return;
    if (endpoint->accept_count == QUIC_MAX_PENDING_ACCEPTS) {
        neverc_quic_conn_close_internal(conn, QUIC_ERR_CONNECTION_REFUSED,
                                         "accept queue is full", 0);
        return;
    }
    endpoint->accept_queue[endpoint->accept_tail] = conn;
    endpoint->accept_tail =
        (endpoint->accept_tail + 1U) % QUIC_MAX_PENDING_ACCEPTS;
    endpoint->accept_count++;
    conn->accept_enqueued = 1;
    nc_cond_signal(&endpoint->accept_cond);
}

static void *quic_endpoint_io(void *argument) {
    neverc_quic_endpoint_t *endpoint =
        (neverc_quic_endpoint_t *)argument;
    uint8_t *packet = (uint8_t *)malloc(NEVERC_UDP_MAX_DATAGRAM_SIZE);
    if (!packet) {
        nc_atomic_store(&endpoint->running, 0);
        nc_cond_broadcast(&endpoint->accept_cond);
        return NULL;
    }
    (void)neverc_udp_set_read_timeout(endpoint->udp, 20);
    while (nc_atomic_load(&endpoint->running)) {
        neverc_udp_packet_info_t info;
        int received = neverc_udp_read_packet(endpoint->udp, packet,
                                               NEVERC_UDP_MAX_DATAGRAM_SIZE,
                                               &info);
        nc_mutex_lock(&endpoint->lock);
        if (received > 0 && !info.truncated) {
            uint32_t version;
            quic_packet_type_t type;
            quic_conn_id_t dcid;
            quic_conn_id_t scid;
            if (quic_extract_destination_cid(packet, (size_t)received,
                                             &version, &type,
                                             &dcid, &scid) == 0) {
                neverc_quic_conn_t *conn =
                    quic_endpoint_find_connection(endpoint, &dcid);
                if (!conn && type == QUIC_PKT_INITIAL &&
                    dcid.len >= 8 && (size_t)received >= QUIC_MIN_INITIAL_SIZE) {
                    if (version != NEVERC_QUIC_VERSION_1 &&
                        version != NEVERC_QUIC_VERSION_2) {
                        quic_send_version_negotiation(endpoint, &info.source,
                                                       &dcid, &scid);
                    } else {
                        conn = quic_endpoint_create_connection(
                            endpoint, &dcid, &scid, &info.source, version);
                    }
                }
                if (conn) {
                    (void)neverc_quic_conn_process_datagram(
                        conn, packet, (size_t)received, &info.source);
                    quic_endpoint_enqueue_accept(endpoint, conn);
                }
            }
        }
        uint64_t now = nc_monotonic_ms();
        for (size_t i = 0; i < endpoint->connection_count; i++) {
            neverc_quic_conn_t *conn = endpoint->connections[i];
            if (conn) {
                neverc_quic_conn_tick(conn, now);
                quic_endpoint_enqueue_accept(endpoint, conn);
            }
        }
        nc_mutex_unlock(&endpoint->lock);
    }
    free(packet);
    nc_mutex_lock(&endpoint->lock);
    nc_cond_broadcast(&endpoint->accept_cond);
    nc_mutex_unlock(&endpoint->lock);
    return NULL;
}

neverc_quic_endpoint_t *neverc_quic_listen(
    const char *addr, const neverc_quic_config_t *config, const char **errp) {
    neverc_quic_config_t defaults;
    if (!config) {
        defaults = neverc_quic_config_default();
        config = &defaults;
    }
    if (!addr || !quic_config_valid(config, 1)) {
        quic_set_error(errp, k_quic_invalid_config);
        return NULL;
    }
    if (!quic_server_credentials_valid(config)) {
        quic_set_error(errp, "QUIC server certificate/key loading failed");
        return NULL;
    }
    neverc_quic_endpoint_t *endpoint =
        (neverc_quic_endpoint_t *)calloc(1, sizeof(*endpoint));
    if (!endpoint) {
        quic_set_error(errp, "out of memory");
        return NULL;
    }
    nc_mutex_init(&endpoint->lock);
    nc_cond_init(&endpoint->accept_cond);
    nc_atomic_store(&endpoint->ref_count, 1);
    if (quic_clone_config(endpoint, config) != 0) {
        quic_set_error(errp, "failed to copy QUIC configuration");
        quic_free_endpoint_config(endpoint);
        nc_mutex_destroy(&endpoint->lock);
        nc_cond_destroy(&endpoint->accept_cond);
        free(endpoint);
        return NULL;
    }
    endpoint->udp = neverc_udp_listen(addr, errp);
    if (!endpoint->udp) {
        quic_free_endpoint_config(endpoint);
        nc_mutex_destroy(&endpoint->lock);
        nc_cond_destroy(&endpoint->accept_cond);
        free(endpoint);
        return NULL;
    }
    nc_atomic_store(&endpoint->running, 1);
    if (nc_thread_create(&endpoint->io_thread, quic_endpoint_io,
                         endpoint) != 0) {
        nc_atomic_store(&endpoint->running, 0);
        neverc_udp_close(endpoint->udp);
        quic_free_endpoint_config(endpoint);
        nc_mutex_destroy(&endpoint->lock);
        nc_cond_destroy(&endpoint->accept_cond);
        free(endpoint);
        quic_set_error(errp, "failed to start QUIC endpoint loop");
        return NULL;
    }
    endpoint->io_thread_started = 1;
    quic_set_error(errp, NULL);
    return endpoint;
}

neverc_quic_conn_t *neverc_quic_accept(neverc_quic_endpoint_t *endpoint,
                                       const char **errp) {
    if (!endpoint) {
        quic_set_error(errp, "invalid QUIC endpoint");
        return NULL;
    }
    nc_mutex_lock(&endpoint->lock);
    while (endpoint->accept_count == 0 &&
           nc_atomic_load(&endpoint->running))
        nc_cond_wait(&endpoint->accept_cond, &endpoint->lock);
    if (endpoint->accept_count == 0) {
        nc_mutex_unlock(&endpoint->lock);
        quic_set_error(errp, "QUIC endpoint is closed");
        return NULL;
    }
    neverc_quic_conn_t *conn = endpoint->accept_queue[endpoint->accept_head];
    endpoint->accept_queue[endpoint->accept_head] = NULL;
    endpoint->accept_head =
        (endpoint->accept_head + 1U) % QUIC_MAX_PENDING_ACCEPTS;
    endpoint->accept_count--;
    conn->application_owned = 1;
    nc_atomic_inc(&endpoint->ref_count);
    nc_mutex_unlock(&endpoint->lock);
    quic_set_error(errp, NULL);
    return conn;
}

int neverc_quic_endpoint_bound_port(neverc_quic_endpoint_t *endpoint) {
    if (!endpoint || !endpoint->udp ||
        !nc_atomic_load(&endpoint->running))
        return -1;
    neverc_udp_addr_t local;
    if (neverc_udp_local_addr(endpoint->udp, &local) != 0) return -1;
    return (int)local.port;
}

void neverc_quic_endpoint_close(neverc_quic_endpoint_t *endpoint) {
    if (!endpoint) return;
    nc_mutex_lock(&endpoint->lock);
    nc_atomic_store(&endpoint->running, 0);
    nc_cond_broadcast(&endpoint->accept_cond);
    nc_mutex_unlock(&endpoint->lock);
    if (endpoint->io_thread_started) {
        nc_thread_join(endpoint->io_thread);
        endpoint->io_thread_started = 0;
    }
    nc_mutex_lock(&endpoint->lock);
    int released_connections = 0;
    for (size_t i = 0; i < endpoint->connection_count; i++) {
        neverc_quic_conn_t *conn = endpoint->connections[i];
        if (!conn) continue;
        neverc_quic_conn_close_internal(conn, 0, "endpoint closed", 0);
        nc_mutex_lock(&conn->lock);
        neverc_quic_endpoint_t *owner = conn->endpoint;
        conn->endpoint = NULL;
        conn->udp = NULL;
        int application_owned = conn->application_owned;
        nc_mutex_unlock(&conn->lock);
        endpoint->connections[i] = NULL;
        if (!application_owned) {
            neverc_quic_conn_destroy(conn);
        } else if (owner)
            released_connections++;
    }
    endpoint->connection_count = 0;
    memset(endpoint->accept_queue, 0, sizeof(endpoint->accept_queue));
    endpoint->accept_head = 0;
    endpoint->accept_tail = 0;
    endpoint->accept_count = 0;
    nc_mutex_unlock(&endpoint->lock);
    for (int i = 0; i < released_connections; i++)
        quic_endpoint_release(endpoint);
    quic_endpoint_release(endpoint);
}

static void *quic_client_io(void *argument) {
    neverc_quic_conn_t *conn = (neverc_quic_conn_t *)argument;
    uint8_t *packet = (uint8_t *)malloc(NEVERC_UDP_MAX_DATAGRAM_SIZE);
    if (!packet) {
        neverc_quic_conn_close_internal(conn, QUIC_ERR_INTERNAL_ERROR,
                                         "receive buffer allocation failed", 0);
        return NULL;
    }
    (void)neverc_udp_set_read_timeout(conn->udp, 20);
    while (conn->io_running && conn->state != QUIC_CONN_CLOSED) {
        neverc_udp_packet_info_t info;
        int received = neverc_udp_read_packet(conn->udp, packet,
                                               NEVERC_UDP_MAX_DATAGRAM_SIZE,
                                               &info);
        if (received > 0 && !info.truncated)
            (void)neverc_quic_conn_process_datagram(
                conn, packet, (size_t)received, &info.source);
        neverc_quic_conn_tick(conn, nc_monotonic_ms());
    }
    free(packet);
    return NULL;
}

static neverc_quic_conn_t *quic_dial_internal(
    const char *addr, const neverc_quic_config_t *config,
    neverc_context_t *context, const char **errp) {
    neverc_quic_config_t defaults;
    if (!config) {
        defaults = neverc_quic_config_default();
        config = &defaults;
    }
    if (!addr || !quic_config_valid(config, 0) ||
        (context && neverc_context_done(context))) {
        if (context && neverc_context_done(context)) {
            quic_set_error(errp, "QUIC dial cancelled");
            return NULL;
        }
        quic_set_error(errp, k_quic_invalid_config);
        return NULL;
    }
    char host[256];
    uint16_t port;
    if (nc_parse_addr(addr, host, sizeof(host), &port) != 0 || !host[0]) {
        quic_set_error(errp, "QUIC dial requires host:port");
        return NULL;
    }
    (void)port;
    neverc_udp_conn_t *udp = neverc_udp_dial(addr, errp);
    if (!udp) return NULL;
    if (context && neverc_context_done(context)) {
        neverc_udp_close(udp);
        quic_set_error(errp, "QUIC dial cancelled");
        return NULL;
    }
    neverc_udp_addr_t peer;
    if (neverc_udp_resolve_addr(addr, &peer) != 0) {
        neverc_udp_close(udp);
        quic_set_error(errp, "failed to resolve QUIC peer address");
        return NULL;
    }
    neverc_quic_conn_t *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    if (!conn) {
        neverc_udp_close(udp);
        quic_set_error(errp, "out of memory");
        return NULL;
    }
    quic_conn_id_t initial_dcid;
    memset(&initial_dcid, 0, sizeof(initial_dcid));
    initial_dcid.len = 8;
    if (neverc_crypto_rand_read(initial_dcid.data, initial_dcid.len) != 0 ||
        neverc_quic_conn_configure(conn, config, udp, 1, &peer, NULL,
                                    &initial_dcid, &initial_dcid,
                                    host) != 0) {
        /* errp is a borrowed stable string; never expose conn-owned storage
         * immediately before destroying the connection. */
        quic_set_error(errp, "failed to configure QUIC client");
        neverc_quic_conn_destroy(conn);
        return NULL;
    }
    if (neverc_quic_conn_start_client(conn) != 0) {
        quic_set_error(errp, "QUIC client handshake failed");
        neverc_quic_conn_destroy(conn);
        return NULL;
    }
    conn->io_running = 1;
    if (nc_thread_create(&conn->io_thread, quic_client_io, conn) != 0) {
        quic_set_error(errp, "failed to start QUIC client loop");
        neverc_quic_conn_destroy(conn);
        return NULL;
    }
    conn->io_thread_started = 1;
    nc_mutex_lock(&conn->lock);
    while (conn->state == QUIC_CONN_HANDSHAKING &&
           (!context || !neverc_context_done(context))) {
        if (!context)
            nc_cond_wait(&conn->state_cond, &conn->lock);
        else if (nc_cond_wait_ms(&conn->state_cond, &conn->lock, 20) < 0)
            break;
    }
    int established = conn->state == QUIC_CONN_ESTABLISHED;
    int cancelled = !established && context &&
                    neverc_context_done(context);
    nc_mutex_unlock(&conn->lock);
    if (!established) {
        quic_set_error(errp, cancelled ? "QUIC dial cancelled"
                                       : "QUIC handshake failed");
        neverc_quic_conn_destroy(conn);
        return NULL;
    }
    quic_set_error(errp, NULL);
    return conn;
}

neverc_quic_conn_t *neverc_quic_dial(
    const char *addr, const neverc_quic_config_t *config, const char **errp) {
    return quic_dial_internal(addr, config, NULL, errp);
}

neverc_quic_conn_t *neverc_quic_dial_context(
    const char *addr, const neverc_quic_config_t *config,
    neverc_context_t *context, const char **errp) {
    if (!context) {
        quic_set_error(errp, k_quic_invalid_config);
        return NULL;
    }
    return quic_dial_internal(addr, config, context, errp);
}

void neverc_quic_conn_close(neverc_quic_conn_t *conn,
                            uint64_t error_code, const char *reason) {
    neverc_quic_conn_close_internal(conn, error_code, reason, 1);
}

static void quic_endpoint_remove_connection(neverc_quic_endpoint_t *endpoint,
                                            neverc_quic_conn_t *conn) {
    if (!endpoint || !conn) return;
    nc_mutex_lock(&endpoint->lock);
    for (size_t i = 0; i < endpoint->connection_count; i++) {
        if (endpoint->connections[i] == conn) {
            endpoint->connections[i] =
                endpoint->connections[endpoint->connection_count - 1U];
            endpoint->connections[endpoint->connection_count - 1U] = NULL;
            endpoint->connection_count--;
            break;
        }
    }
    nc_mutex_unlock(&endpoint->lock);
}

void neverc_quic_conn_free(neverc_quic_conn_t *conn) {
    if (!conn) return;
    neverc_quic_conn_close_internal(conn, 0, "connection released", 1);
    conn->io_running = 0;
    nc_mutex_lock(&conn->lock);
    neverc_quic_endpoint_t *endpoint = conn->endpoint;
    conn->endpoint = NULL;
    if (endpoint) conn->udp = NULL;
    nc_mutex_unlock(&conn->lock);
    if (endpoint) quic_endpoint_remove_connection(endpoint, conn);
    if (endpoint) {
        quic_endpoint_release(endpoint);
    }
    neverc_quic_conn_destroy(conn);
}

const char *neverc_quic_conn_remote_addr(neverc_quic_conn_t *conn) {
    return conn && conn->remote_addr[0] ? conn->remote_addr : NULL;
}

const char *neverc_quic_conn_alpn(neverc_quic_conn_t *conn) {
    return neverc_quic_conn_get_alpn(conn);
}

int neverc_quic_conn_is_alive(neverc_quic_conn_t *conn) {
    return neverc_quic_conn_is_alive_check(conn);
}

int neverc_quic_conn_close_info(neverc_quic_conn_t *conn,
                                neverc_quic_close_info_t *info) {
    if (!conn || !info ||
        (conn->state != QUIC_CONN_DRAINING && conn->state != QUIC_CONN_CLOSED))
        return -1;
    info->error_code = conn->close_error_code;
    info->reason = conn->close_reason;
    info->is_app = conn->close_is_app;
    return 0;
}

neverc_quic_stream_t *neverc_quic_open_stream(neverc_quic_conn_t *conn,
                                              const char **errp) {
    neverc_quic_stream_t *stream = neverc_quic_conn_open_stream(conn);
    quic_set_error(errp, stream ? NULL : k_quic_stream_limit);
    return stream;
}

neverc_quic_stream_t *neverc_quic_open_uni_stream(neverc_quic_conn_t *conn,
                                                  const char **errp) {
    neverc_quic_stream_t *stream = neverc_quic_conn_open_uni_stream(conn);
    quic_set_error(errp, stream ? NULL : k_quic_stream_limit);
    return stream;
}

neverc_quic_stream_t *neverc_quic_accept_stream(neverc_quic_conn_t *conn,
                                                const char **errp) {
    if (!conn) {
        quic_set_error(errp, "invalid QUIC connection");
        return NULL;
    }
    nc_mutex_lock(&conn->lock);
    for (;;) {
        for (int i = 0; i < conn->n_streams; i++) {
            quic_stream_t *stream = conn->streams[i];
            if (stream && stream->peer_initiated &&
                !stream->application_accepted) {
                stream->application_accepted = 1;
                nc_mutex_unlock(&conn->lock);
                quic_set_error(errp, NULL);
                return stream;
            }
        }
        if (!neverc_quic_conn_is_alive_check(conn)) break;
        nc_cond_wait(&conn->stream_avail_cond, &conn->lock);
    }
    nc_mutex_unlock(&conn->lock);
    quic_set_error(errp, k_quic_closed);
    return NULL;
}

int neverc_quic_try_accept_stream(neverc_quic_conn_t *conn,
                                  neverc_quic_stream_t **out) {
    if (!conn || !out) return -1;
    *out = NULL;
    nc_mutex_lock(&conn->lock);
    for (int i = 0; i < conn->n_streams; i++) {
        quic_stream_t *stream = conn->streams[i];
        if (stream && stream->peer_initiated &&
            !stream->application_accepted) {
            stream->application_accepted = 1;
            *out = stream;
            nc_mutex_unlock(&conn->lock);
            return 1;
        }
    }
    int alive = neverc_quic_conn_is_alive_check(conn);
    nc_mutex_unlock(&conn->lock);
    return alive ? 0 : -1;
}

int neverc_quic_stream_read(neverc_quic_stream_t *stream,
                            void *buffer, size_t capacity) {
    return neverc_quic_stream_read_data(stream, buffer, capacity);
}

int neverc_quic_stream_try_read(neverc_quic_stream_t *stream,
                                void *buffer, size_t capacity) {
    return neverc_quic_stream_try_read_data(stream, buffer, capacity);
}

int neverc_quic_stream_write(neverc_quic_stream_t *stream,
                             const void *data, size_t length) {
    return neverc_quic_stream_write_data(stream, data, length);
}

int neverc_quic_stream_close_write(neverc_quic_stream_t *stream) {
    return neverc_quic_stream_close_write_side(stream);
}

int neverc_quic_stream_reset(neverc_quic_stream_t *stream,
                             uint64_t error_code) {
    if (!stream || error_code > QUIC_VARINT_MAX ||
        (((stream->id & 2U) != 0) && stream->peer_initiated))
        return -1;
    nc_mutex_lock(&stream->lock);
    stream->reset_pending = 1;
    stream->reset_error_code = error_code;
    stream->state = QUIC_STREAM_RESET;
    nc_mutex_unlock(&stream->lock);
    return neverc_quic_conn_flush(stream->conn);
}

int neverc_quic_stream_stop_sending(neverc_quic_stream_t *stream,
                                    uint64_t error_code) {
    if (!stream || error_code > QUIC_VARINT_MAX ||
        (((stream->id & 2U) != 0) && !stream->peer_initiated))
        return -1;
    nc_mutex_lock(&stream->lock);
    stream->stop_sending_pending = 1;
    stream->stop_sending_error_code = error_code;
    nc_mutex_unlock(&stream->lock);
    return neverc_quic_conn_flush(stream->conn);
}

uint64_t neverc_quic_stream_id(neverc_quic_stream_t *stream) {
    return neverc_quic_stream_get_id(stream);
}

void neverc_quic_stream_free(neverc_quic_stream_t *stream) {
    if (!stream) return;
    nc_mutex_lock(&stream->lock);
    stream->application_released = 1;
    nc_mutex_unlock(&stream->lock);
}

int neverc_quic_send_datagram(neverc_quic_conn_t *conn,
                              const void *data, size_t length) {
    if (!conn || (!data && length != 0) ||
        conn->state != QUIC_CONN_ESTABLISHED ||
        conn->peer_params.max_datagram_frame_size == 0 ||
        length > conn->peer_params.max_datagram_frame_size)
        return -1;
    uint8_t *copy = NULL;
    if (length) {
        copy = (uint8_t *)malloc(length);
        if (!copy) return -1;
        memcpy(copy, data, length);
    }
    nc_mutex_lock(&conn->lock);
    if (conn->send_datagram_count == QUIC_DATAGRAM_QUEUE_CAPACITY) {
        nc_mutex_unlock(&conn->lock);
        free(copy);
        return -1;
    }
    quic_datagram_entry_t *entry =
        &conn->send_datagrams[conn->send_datagram_tail];
    entry->data = copy;
    entry->len = length;
    conn->send_datagram_tail =
        (conn->send_datagram_tail + 1U) % QUIC_DATAGRAM_QUEUE_CAPACITY;
    conn->send_datagram_count++;
    nc_mutex_unlock(&conn->lock);
    return neverc_quic_conn_flush(conn);
}

int neverc_quic_recv_datagram(neverc_quic_conn_t *conn,
                              void *buffer, size_t capacity) {
    if (!conn || !buffer || capacity == 0 || capacity > INT_MAX) return -1;
    nc_mutex_lock(&conn->lock);
    while (conn->recv_datagram_count == 0 &&
           neverc_quic_conn_is_alive_check(conn))
        nc_cond_wait(&conn->datagram_cond, &conn->lock);
    if (conn->recv_datagram_count == 0) {
        nc_mutex_unlock(&conn->lock);
        return -1;
    }
    quic_datagram_entry_t *entry =
        &conn->recv_datagrams[conn->recv_datagram_head];
    if (entry->len > capacity) {
        nc_mutex_unlock(&conn->lock);
        return -1;
    }
    memcpy(buffer, entry->data, entry->len);
    int result = (int)entry->len;
    free(entry->data);
    entry->data = NULL;
    entry->len = 0;
    conn->recv_datagram_head =
        (conn->recv_datagram_head + 1U) % QUIC_DATAGRAM_QUEUE_CAPACITY;
    conn->recv_datagram_count--;
    nc_mutex_unlock(&conn->lock);
    return result;
}
