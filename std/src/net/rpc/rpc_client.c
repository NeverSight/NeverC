#include "neverc/std/net/rpc.h"

#include "neverc/std/crypto/rand.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/net/quic.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/thread.h"
#include "../_net_buffer.h"
#include "../_net_thread.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t length;
    uint8_t bytes[];
} rpc_outbound_t;

typedef struct {
    neverc_rpc_frame_header_t header;
    uint8_t payload[];
} rpc_inbound_t;

struct neverc_rpc_stream {
    neverc_rpc_client_t *client;
    uint64_t request_id;
    uint64_t generation;
    neverc_thread_channel_t *receive_queue;
    neverc_context_t *context;
    neverc_context_cancel_handle_t *cancel;
    nc_mutex_t lock;
    int send_closed;
    int ended;
    int peer_end_queued;
    neverc_rpc_status_code_t status_code;
    char *status_message;
};

struct neverc_rpc_client {
    neverc_rpc_client_config_t config;
    char *addr;
    char *server_name;
    char *root_ca_file;
    char *client_cert_file;
    char *client_key_file;
    neverc_tcp_conn_t *tcp;
    neverc_tls_conn_t *tls;
    neverc_quic_conn_t *quic;
    neverc_quic_stream_t *quic_stream;
    neverc_thread_channel_t *send_queue;
    nc_thread_t reader_thread;
    nc_thread_t writer_thread;
    nc_thread_t keepalive_thread;
    int reader_started;
    int writer_started;
    int keepalive_started;
    volatile int running;
    volatile int closing;
    nc_mutex_t lifecycle_lock;
    uint64_t generation;
    nc_mutex_t keepalive_lock;
    int awaiting_pong;
    uint64_t next_ping_ms;
    uint64_t pong_deadline_ms;
    uint8_t ping_token[8];
    nc_mutex_t streams_lock;
    neverc_rpc_stream_t **streams;
    size_t stream_count;
    uint64_t next_request_id;
};

static void rpc_set_error(const char **errp, const char *message) {
    if (errp) *errp = message;
}

static int rpc_config_valid(const neverc_rpc_client_config_t *config) {
    return config && config->max_frame_size > 0 &&
           config->max_frame_size <= INT_MAX &&
           config->max_metadata_size > 0 &&
           config->max_metadata_size <= config->max_frame_size &&
           config->max_inflight_streams > 0 &&
           config->max_inflight_streams <= 65536 &&
           config->send_queue_capacity > 0 &&
           config->receive_queue_capacity > 0 &&
           config->connect_timeout_ms > 0 && config->io_timeout_ms > 0 &&
           config->ping_interval_ms >= 0 && config->pong_timeout_ms >= 0 &&
           ((config->ping_interval_ms == 0) ==
            (config->pong_timeout_ms == 0)) &&
           (config->reconnect_enabled == 0 ||
            config->reconnect_enabled == 1) &&
           config->reconnect_backoff_ms >= 0 &&
           (config->use_tls == 0 || config->use_tls == 1) &&
           (config->use_quic == 0 || config->use_quic == 1) &&
           !(config->use_tls && config->use_quic) &&
           ((config->client_cert_file == NULL) ==
            (config->client_key_file == NULL)) &&
           (!config->client_cert_file ||
            (config->use_tls == 1 && config->use_quic == 0));
}

neverc_rpc_client_config_t neverc_rpc_client_config_default(void) {
    neverc_rpc_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.max_frame_size = NEVERC_RPC_DEFAULT_MAX_FRAME_SIZE;
    config.max_metadata_size = NEVERC_RPC_DEFAULT_MAX_METADATA_SIZE;
    config.max_inflight_streams = 1024;
    config.send_queue_capacity = 1024;
    config.receive_queue_capacity = 32;
    config.connect_timeout_ms = 30000;
    config.io_timeout_ms = 30000;
    config.ping_interval_ms = 30000;
    config.pong_timeout_ms = 10000;
    config.reconnect_enabled = 1;
    config.reconnect_backoff_ms = 100;
    return config;
}

neverc_rpc_call_options_t neverc_rpc_call_options_default(void) {
    neverc_rpc_call_options_t options;
    options.codec = NEVERC_RPC_CODEC_RAW;
    options.idempotent = 0;
    options.max_attempts = 1;
    return options;
}

static int rpc_transport_read(neverc_rpc_client_t *client,
                              void *data, size_t length) {
    if (client->quic_stream)
        return neverc_quic_stream_read(client->quic_stream, data, length);
    return client->tls ? neverc_tls_read(client->tls, data, length)
                       : neverc_tcp_read(client->tcp, data, length);
}

static int rpc_transport_write_all(neverc_rpc_client_t *client,
                                   const void *data, size_t length) {
    size_t written = 0;
    while (written < length) {
        size_t chunk = length - written;
        if (chunk > (size_t)INT_MAX) chunk = (size_t)INT_MAX;
        int n = client->quic_stream
            ? neverc_quic_stream_write(client->quic_stream,
                                       (const uint8_t *)data + written, chunk)
            : client->tls
            ? neverc_tls_write(client->tls,
                               (const uint8_t *)data + written, chunk)
            : neverc_tcp_write(client->tcp,
                               (const uint8_t *)data + written, chunk);
        if (n <= 0) return -1;
        written += (size_t)n;
    }
    return 0;
}

static neverc_rpc_stream_t *rpc_find_stream_locked(
    neverc_rpc_client_t *client, uint64_t request_id) {
    for (size_t i = 0; i < client->config.max_inflight_streams; i++) {
        neverc_rpc_stream_t *stream = client->streams[i];
        if (stream && stream->request_id == request_id) return stream;
    }
    return NULL;
}

static void rpc_stream_set_terminal(neverc_rpc_stream_t *stream,
                                    neverc_rpc_status_code_t code,
                                    const char *message) {
    nc_mutex_lock(&stream->lock);
    if (!stream->ended) {
        stream->ended = 1;
        stream->status_code = code;
        if (message) {
            free(stream->status_message);
            stream->status_message = strdup(message);
        }
        neverc_context_cancel_handle_cancel(stream->cancel);
        neverc_thread_channel_close(stream->receive_queue);
    }
    nc_mutex_unlock(&stream->lock);
}

static void rpc_client_fail_streams(neverc_rpc_client_t *client,
                                    neverc_rpc_status_code_t code,
                                    const char *message) {
    nc_mutex_lock(&client->streams_lock);
    for (size_t i = 0; i < client->config.max_inflight_streams; i++) {
        if (client->streams[i])
            rpc_stream_set_terminal(client->streams[i], code, message);
    }
    nc_mutex_unlock(&client->streams_lock);
}

static void rpc_client_stop_transport(neverc_rpc_client_t *client,
                                      neverc_rpc_status_code_t code,
                                      const char *message) {
    if (!nc_atomic_cas(&client->running, 1, 0)) return;
    if (client->tcp) {
        (void)neverc_tcp_shutdown_read(client->tcp);
        (void)neverc_tcp_shutdown_write(client->tcp);
    }
    if (client->quic)
        neverc_quic_conn_close(client->quic, 0, "RPC transport stopped");
    neverc_thread_channel_close(client->send_queue);
    rpc_client_fail_streams(client, code, message);
}

static int rpc_queue_encoded(neverc_rpc_client_t *client,
                             neverc_context_t *context,
                             const neverc_rpc_frame_header_t *header,
                             const void *payload) {
    if (!client || !header || !nc_atomic_load(&client->running))
        return NEVERC_RPC_IO_CLOSED;
    size_t total = NEVERC_RPC_FRAME_HEADER_SIZE +
                   (size_t)header->payload_length;
    rpc_outbound_t *outbound =
        (rpc_outbound_t *)malloc(sizeof(*outbound) + total);
    if (!outbound) return NEVERC_RPC_IO_NOMEM;
    neverc_rpc_frame_t frame;
    frame.header = *header;
    frame.payload = (const uint8_t *)payload;
    if (neverc_rpc_frame_encode(&frame, outbound->bytes, total,
                                &outbound->length) != 0) {
        free(outbound);
        return NEVERC_RPC_IO_INVALID;
    }
    int result = context
        ? neverc_thread_channel_send_context(client->send_queue, context,
                                             outbound)
        : neverc_thread_channel_try_send(client->send_queue, outbound);
    if (result != NEVERC_THREAD_OK) free(outbound);
    if (result == NEVERC_THREAD_CANCELLED) return NEVERC_RPC_IO_CANCELLED;
    if (result == NEVERC_THREAD_WOULD_BLOCK)
        return NEVERC_RPC_IO_WOULD_BLOCK;
    return result == NEVERC_THREAD_OK ? NEVERC_RPC_IO_OK
                                     : NEVERC_RPC_IO_CLOSED;
}

static void *rpc_writer_main(void *arg) {
    neverc_rpc_client_t *client = (neverc_rpc_client_t *)arg;
    for (;;) {
        void *value = NULL;
        int result = neverc_thread_channel_receive(client->send_queue, &value);
        if (result == NEVERC_THREAD_CLOSED) break;
        if (result != NEVERC_THREAD_OK) continue;
        rpc_outbound_t *outbound = (rpc_outbound_t *)value;
        if (!nc_atomic_load(&client->running) ||
            rpc_transport_write_all(client, outbound->bytes,
                                    outbound->length) != 0) {
            free(outbound);
            rpc_client_stop_transport(client,
                                      NEVERC_RPC_STATUS_UNAVAILABLE,
                                      "RPC transport write failed");
            break;
        }
        free(outbound);
    }
    return NULL;
}

static int rpc_queue_control(neverc_rpc_client_t *client, uint8_t type,
                             const void *payload, size_t payload_length) {
    neverc_rpc_frame_header_t header;
    memset(&header, 0, sizeof(header));
    header.version = NEVERC_RPC_VERSION_1;
    header.type = type;
    header.payload_length = (uint32_t)payload_length;
    return rpc_queue_encoded(client, NULL, &header, payload);
}

static int rpc_dispatch_inbound(neverc_rpc_client_t *client,
                                const neverc_rpc_frame_t *frame) {
    if (frame->header.type == NEVERC_RPC_FRAME_PING)
        return rpc_queue_control(client, NEVERC_RPC_FRAME_PONG,
                                 frame->payload,
                                 frame->header.payload_length) ==
                       NEVERC_RPC_IO_OK
                   ? 0 : -1;
    if (frame->header.type == NEVERC_RPC_FRAME_PONG) {
        nc_mutex_lock(&client->keepalive_lock);
        if (client->awaiting_pong &&
            frame->header.payload_length == sizeof(client->ping_token) &&
            memcmp(frame->payload, client->ping_token,
                   sizeof(client->ping_token)) == 0) {
            client->awaiting_pong = 0;
            client->next_ping_ms = nc_monotonic_ms() +
                (uint64_t)client->config.ping_interval_ms;
        }
        nc_mutex_unlock(&client->keepalive_lock);
        return 0;
    }
    if (frame->header.type == NEVERC_RPC_FRAME_GOAWAY) {
        rpc_client_fail_streams(client,
            (neverc_rpc_status_code_t)frame->header.code,
            "RPC peer sent GOAWAY");
        return -1;
    }
    if (frame->header.type != NEVERC_RPC_FRAME_DATA &&
        frame->header.type != NEVERC_RPC_FRAME_END &&
        frame->header.type != NEVERC_RPC_FRAME_CANCEL)
        return -1;
    if ((frame->header.flags & NEVERC_RPC_FLAG_RESPONSE) == 0) return -1;

    size_t allocation = sizeof(rpc_inbound_t) +
                        (size_t)frame->header.payload_length;
    rpc_inbound_t *inbound = (rpc_inbound_t *)malloc(allocation);
    if (!inbound) return -1;
    inbound->header = frame->header;
    if (frame->header.payload_length > 0)
        memcpy(inbound->payload, frame->payload,
               frame->header.payload_length);

    nc_mutex_lock(&client->streams_lock);
    neverc_rpc_stream_t *stream = rpc_find_stream_locked(
        client, frame->header.request_id);
    if (stream && stream->peer_end_queued) {
        nc_mutex_unlock(&client->streams_lock);
        free(inbound);
        return -1;
    }
    int queued = stream ? neverc_thread_channel_try_send(
                              stream->receive_queue, inbound)
                        : NEVERC_THREAD_CLOSED;
    if (stream && queued == NEVERC_THREAD_OK &&
        (frame->header.type == NEVERC_RPC_FRAME_END ||
         frame->header.type == NEVERC_RPC_FRAME_CANCEL))
        stream->peer_end_queued = 1;
    if (stream && queued == NEVERC_THREAD_WOULD_BLOCK)
        rpc_stream_set_terminal(stream,
            NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED,
            "RPC stream receive queue is full");
    nc_mutex_unlock(&client->streams_lock);
    if (queued != NEVERC_THREAD_OK) free(inbound);
    return 0;
}

static void *rpc_reader_main(void *arg) {
    neverc_rpc_client_t *client = (neverc_rpc_client_t *)arg;
    nc_buf_t input;
    nc_buf_init(&input);
    char chunk[16384];
    int protocol_failure = 0;
    while (nc_atomic_load(&client->running)) {
        int n = rpc_transport_read(client, chunk, sizeof(chunk));
        if (n <= 0) break;
        size_t input_limit = client->config.max_frame_size +
                             NEVERC_RPC_FRAME_HEADER_SIZE + sizeof(chunk);
        if (input.len > input_limit || (size_t)n > input_limit - input.len ||
            nc_buf_append(&input, chunk, (size_t)n) != 0) {
            protocol_failure = 1;
            break;
        }
        while (input.len > 0) {
            neverc_rpc_frame_t frame;
            size_t consumed = 0;
            int decoded = neverc_rpc_frame_decode(
                input.data, input.len, client->config.max_frame_size,
                &frame, &consumed);
            if (decoded == 0) break;
            if (decoded < 0 || rpc_dispatch_inbound(client, &frame) != 0) {
                protocol_failure = 1;
                goto finished;
            }
            nc_buf_consume(&input, consumed);
        }
    }

finished:
    nc_buf_free(&input);
    if (nc_atomic_load(&client->running)) {
        rpc_client_stop_transport(
            client,
            protocol_failure ? NEVERC_RPC_STATUS_DATA_LOSS
                             : NEVERC_RPC_STATUS_UNAVAILABLE,
            protocol_failure ? "invalid RPC peer frame"
                             : "RPC transport closed");
    }
    return NULL;
}

static void rpc_sleep_ms(unsigned milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
#endif
}

static void *rpc_keepalive_main(void *arg) {
    neverc_rpc_client_t *client = (neverc_rpc_client_t *)arg;
    while (nc_atomic_load(&client->running)) {
        uint8_t token[sizeof(client->ping_token)];
        int send_ping = 0;
        int expired = 0;
        uint64_t now = nc_monotonic_ms();
        nc_mutex_lock(&client->keepalive_lock);
        if (client->awaiting_pong && now >= client->pong_deadline_ms) {
            expired = 1;
        } else if (!client->awaiting_pong &&
                   now >= client->next_ping_ms) {
            if (neverc_crypto_rand_read(token, sizeof(token)) != 0) {
                expired = 1;
            } else {
                memcpy(client->ping_token, token, sizeof(token));
                client->awaiting_pong = 1;
                client->pong_deadline_ms = now +
                    (uint64_t)client->config.pong_timeout_ms;
                send_ping = 1;
            }
        }
        nc_mutex_unlock(&client->keepalive_lock);
        if (expired) {
            rpc_client_stop_transport(client,
                                      NEVERC_RPC_STATUS_UNAVAILABLE,
                                      "RPC keepalive timed out");
            break;
        }
        if (send_ping &&
            rpc_queue_control(client, NEVERC_RPC_FRAME_PING, token,
                              sizeof(token)) != NEVERC_RPC_IO_OK) {
            rpc_client_stop_transport(client,
                                      NEVERC_RPC_STATUS_UNAVAILABLE,
                                      "RPC keepalive send queue is full");
            break;
        }
        rpc_sleep_ms(25);
    }
    return NULL;
}

static int rpc_addr_host(const char *addr, char *host, size_t capacity) {
    if (!addr || !host || capacity == 0) return -1;
    const char *start = addr;
    const char *end = NULL;
    if (*start == '[') {
        end = strchr(start + 1, ']');
        if (!end || end[1] != ':') return -1;
        start++;
    } else {
        end = strrchr(start, ':');
        if (!end || memchr(start, ':', (size_t)(end - start))) return -1;
    }
    size_t length = (size_t)(end - start);
    if (length == 0 || length >= capacity) return -1;
    memcpy(host, start, length);
    host[length] = '\0';
    return 0;
}

typedef struct {
    neverc_tcp_conn_t *tcp;
    neverc_tls_conn_t *tls;
    neverc_quic_conn_t *quic;
    neverc_quic_stream_t *quic_stream;
} rpc_client_transport_t;

static void rpc_close_dialed_transport(rpc_client_transport_t *transport) {
    if (!transport) return;
    if (transport->tls) neverc_tls_close(transport->tls);
    if (transport->tcp) neverc_tcp_close(transport->tcp);
    if (transport->quic_stream)
        neverc_quic_stream_free(transport->quic_stream);
    if (transport->quic) neverc_quic_conn_free(transport->quic);
    memset(transport, 0, sizeof(*transport));
}

static int rpc_dial_transport(
    const char *addr, const neverc_rpc_client_config_t *config,
    neverc_context_t *parent, rpc_client_transport_t *transport,
    const char **errp) {
    memset(transport, 0, sizeof(*transport));
    if (config->use_quic) {
        if (neverc_context_done(parent)) {
            rpc_set_error(errp, "RPC QUIC dial cancelled");
            return -1;
        }
        char derived_host[256];
        const char *server_name = config->server_name;
        if (!server_name && rpc_addr_host(addr, derived_host,
                                          sizeof(derived_host)) == 0)
            server_name = derived_host;
        const char *alpn[] = { "nrpc/1", NULL };
        neverc_quic_config_t quic_config = neverc_quic_config_default();
        quic_config.alpn = alpn;
        quic_config.server_name = server_name;
        quic_config.root_cert_file = config->root_ca_file;
        quic_config.max_idle_timeout_ms =
            (uint64_t)config->io_timeout_ms;
        if (!server_name || !server_name[0]) {
            rpc_set_error(errp, "RPC QUIC dial requires a server name");
            return -1;
        }
        neverc_context_cancel_handle_t *dial_cancel = NULL;
        neverc_context_t *dial_context = neverc_context_with_timeout_handle(
            parent, config->connect_timeout_ms, &dial_cancel);
        if (!dial_context || !dial_cancel) {
            if (dial_context) neverc_context_free(dial_context);
            if (dial_cancel)
                neverc_context_cancel_handle_free(dial_cancel);
            rpc_set_error(errp, "failed to create RPC QUIC dial context");
            return -1;
        }
        transport->quic = neverc_quic_dial_context(
            addr, &quic_config, dial_context, errp);
        neverc_context_cancel_handle_cancel(dial_cancel);
        neverc_context_free(dial_context);
        neverc_context_cancel_handle_free(dial_cancel);
        if (!transport->quic) return -1;
        if (neverc_context_done(parent)) {
            rpc_close_dialed_transport(transport);
            rpc_set_error(errp, "RPC QUIC dial cancelled");
            return -1;
        }
        const char *negotiated = neverc_quic_conn_alpn(transport->quic);
        if (!negotiated || strcmp(negotiated, "nrpc/1") != 0) {
            rpc_close_dialed_transport(transport);
            rpc_set_error(errp, "RPC peer did not negotiate nrpc/1");
            return -1;
        }
        transport->quic_stream = neverc_quic_open_stream(
            transport->quic, errp);
        if (!transport->quic_stream) {
            rpc_close_dialed_transport(transport);
            return -1;
        }
        return 0;
    }

    neverc_context_cancel_handle_t *dial_cancel = NULL;
    neverc_context_t *dial_context = neverc_context_with_timeout_handle(
        parent, config->connect_timeout_ms, &dial_cancel);
    if (!dial_context || !dial_cancel) {
        if (dial_context) neverc_context_free(dial_context);
        if (dial_cancel) neverc_context_cancel_handle_free(dial_cancel);
        rpc_set_error(errp, "failed to create RPC dial context");
        return -1;
    }
    neverc_tcp_conn_t *tcp = NULL;
    neverc_net_result_t dial_result = neverc_tcp_dial_context(
        addr, dial_context, &tcp);
    neverc_context_cancel_handle_cancel(dial_cancel);
    neverc_context_free(dial_context);
    neverc_context_cancel_handle_free(dial_cancel);
    if (dial_result.status != NEVERC_NET_OK || !tcp) {
        rpc_set_error(errp, "RPC TCP dial failed");
        return -1;
    }
    if (neverc_tcp_set_timeout(tcp, config->io_timeout_ms) != 0) {
        neverc_tcp_close(tcp);
        rpc_set_error(errp, "failed to configure RPC transport timeout");
        return -1;
    }

    neverc_tls_conn_t *tls = NULL;
    if (config->use_tls) {
        char derived_host[256];
        const char *server_name = config->server_name;
        if (!server_name && rpc_addr_host(addr, derived_host,
                                          sizeof(derived_host)) == 0)
            server_name = derived_host;
        neverc_tls_config_t *tls_config = neverc_tls_config_new();
        if (!tls_config || !server_name || !server_name[0] ||
            (config->root_ca_file &&
             neverc_tls_config_add_root_certificates(
                 tls_config, config->root_ca_file) != 0) ||
            (config->client_cert_file &&
             neverc_tls_config_load_cert(tls_config,
                                         config->client_cert_file,
                                         config->client_key_file) != 0)) {
            neverc_tls_config_free(tls_config);
            neverc_tcp_close(tcp);
            rpc_set_error(errp, "invalid RPC TLS configuration");
            return -1;
        }
        neverc_tls_config_set_server_name(tls_config, server_name);
        const char *alpn[] = { "nrpc/1" };
        neverc_tls_config_set_alpn(tls_config, alpn, 1);
        const char *tls_error = NULL;
        tls = neverc_tls_client(tcp, tls_config, &tls_error);
        neverc_tls_config_free(tls_config);
        if (!tls) {
            neverc_tcp_close(tcp);
            rpc_set_error(errp, tls_error ? tls_error
                                          : "RPC TLS handshake failed");
            return -1;
        }
        const char *negotiated = neverc_tls_alpn(tls);
        if (!negotiated || strcmp(negotiated, "nrpc/1") != 0) {
            neverc_tls_close(tls);
            neverc_tcp_close(tcp);
            rpc_set_error(errp, "RPC peer did not negotiate nrpc/1");
            return -1;
        }
    }
    transport->tcp = tcp;
    transport->tls = tls;
    return 0;
}

neverc_rpc_client_t *neverc_rpc_client_dial(
    const char *addr, const neverc_rpc_client_config_t *config,
    const char **errp) {
    rpc_set_error(errp, NULL);
    neverc_rpc_client_config_t effective = config
        ? *config : neverc_rpc_client_config_default();
    if (!addr || !rpc_config_valid(&effective)) {
        rpc_set_error(errp, "invalid RPC client configuration");
        return NULL;
    }

    rpc_client_transport_t transport;
    neverc_context_t *background = neverc_context_background();
    if (!background) {
        rpc_set_error(errp, "out of memory");
        return NULL;
    }
    if (rpc_dial_transport(addr, &effective, background, &transport, errp) !=
        0) {
        neverc_context_free(background);
        return NULL;
    }

    neverc_rpc_client_t *client =
        (neverc_rpc_client_t *)calloc(1, sizeof(*client));
    if (!client) goto allocation_failed;
    client->config = effective;
    client->addr = strdup(addr);
    if (effective.server_name)
        client->server_name = strdup(effective.server_name);
    if (effective.root_ca_file)
        client->root_ca_file = strdup(effective.root_ca_file);
    if (effective.client_cert_file)
        client->client_cert_file = strdup(effective.client_cert_file);
    if (effective.client_key_file)
        client->client_key_file = strdup(effective.client_key_file);
    client->config.server_name = NULL;
    client->config.root_ca_file = NULL;
    client->config.client_cert_file = NULL;
    client->config.client_key_file = NULL;
    client->tcp = transport.tcp;
    client->tls = transport.tls;
    client->quic = transport.quic;
    client->quic_stream = transport.quic_stream;
    client->next_request_id = 1;
    client->streams = (neverc_rpc_stream_t **)calloc(
        effective.max_inflight_streams, sizeof(*client->streams));
    client->send_queue = neverc_thread_channel_create(
        effective.send_queue_capacity);
    if (!client->addr ||
        (effective.server_name && !client->server_name) ||
        (effective.root_ca_file && !client->root_ca_file) ||
        (effective.client_cert_file && !client->client_cert_file) ||
        (effective.client_key_file && !client->client_key_file) ||
        !client->streams || !client->send_queue) {
        free(client->client_key_file);
        free(client->client_cert_file);
        free(client->root_ca_file);
        free(client->server_name);
        free(client->addr);
        free(client->streams);
        if (client->send_queue)
            neverc_thread_channel_free(client->send_queue);
        free(client);
        goto allocation_failed;
    }
    nc_mutex_init(&client->lifecycle_lock);
    nc_mutex_init(&client->streams_lock);
    nc_mutex_init(&client->keepalive_lock);
    client->next_ping_ms = nc_monotonic_ms() +
        (uint64_t)effective.ping_interval_ms;
    client->generation = 1;
    nc_atomic_store(&client->running, 1);
    if (nc_thread_create(&client->writer_thread, rpc_writer_main, client) !=
        0)
        goto start_failed;
    client->writer_started = 1;
    if (nc_thread_create(&client->reader_thread, rpc_reader_main, client) !=
        0)
        goto start_failed;
    client->reader_started = 1;
    if (effective.ping_interval_ms > 0 &&
        nc_thread_create(&client->keepalive_thread, rpc_keepalive_main,
                         client) != 0)
        goto start_failed;
    client->keepalive_started = effective.ping_interval_ms > 0;
    neverc_context_free(background);
    return client;

start_failed:
    nc_atomic_store(&client->running, 0);
    neverc_thread_channel_close(client->send_queue);
    if (client->tcp) {
        (void)neverc_tcp_shutdown_read(client->tcp);
        (void)neverc_tcp_shutdown_write(client->tcp);
    }
    if (client->quic)
        neverc_quic_conn_close(client->quic, 0,
                               "RPC client startup failed");
    if (client->keepalive_started) nc_thread_join(client->keepalive_thread);
    if (client->reader_started) nc_thread_join(client->reader_thread);
    if (client->writer_started) nc_thread_join(client->writer_thread);
    nc_mutex_destroy(&client->keepalive_lock);
    nc_mutex_destroy(&client->streams_lock);
    nc_mutex_destroy(&client->lifecycle_lock);
    neverc_thread_channel_free(client->send_queue);
    free(client->streams);
    free(client->client_key_file);
    free(client->client_cert_file);
    free(client->root_ca_file);
    free(client->server_name);
    free(client->addr);
    free(client);
allocation_failed:
    neverc_context_free(background);
    rpc_close_dialed_transport(&transport);
    rpc_set_error(errp, "failed to allocate RPC client");
    return NULL;
}

static void rpc_client_drain_send_queue(neverc_rpc_client_t *client) {
    if (!client->send_queue) return;
    for (;;) {
        void *value = NULL;
        int result = neverc_thread_channel_try_receive(client->send_queue,
                                                        &value);
        if (result != NEVERC_THREAD_OK) break;
        free(value);
    }
}

static void rpc_client_join_pumps(neverc_rpc_client_t *client) {
    if (client->keepalive_started) {
        nc_thread_join(client->keepalive_thread);
        client->keepalive_started = 0;
    }
    if (client->reader_started) {
        nc_thread_join(client->reader_thread);
        client->reader_started = 0;
    }
    if (client->writer_started) {
        nc_thread_join(client->writer_thread);
        client->writer_started = 0;
    }
}

static void rpc_client_release_transport(neverc_rpc_client_t *client) {
    rpc_client_drain_send_queue(client);
    if (client->tls) {
        neverc_tls_close(client->tls);
        client->tls = NULL;
    }
    if (client->tcp) {
        neverc_tcp_close(client->tcp);
        client->tcp = NULL;
    }
    if (client->quic_stream) {
        neverc_quic_stream_free(client->quic_stream);
        client->quic_stream = NULL;
    }
    if (client->quic) {
        neverc_quic_conn_free(client->quic);
        client->quic = NULL;
    }
    if (client->send_queue) {
        neverc_thread_channel_free(client->send_queue);
        client->send_queue = NULL;
    }
}

static int rpc_client_start_pumps(neverc_rpc_client_t *client) {
    nc_atomic_store(&client->running, 1);
    if (nc_thread_create(&client->writer_thread, rpc_writer_main, client) !=
        0)
        goto failed;
    client->writer_started = 1;
    if (nc_thread_create(&client->reader_thread, rpc_reader_main, client) !=
        0)
        goto failed;
    client->reader_started = 1;
    if (client->config.ping_interval_ms > 0) {
        if (nc_thread_create(&client->keepalive_thread,
                             rpc_keepalive_main, client) != 0)
            goto failed;
        client->keepalive_started = 1;
    }
    return 0;

failed:
    rpc_client_stop_transport(client, NEVERC_RPC_STATUS_UNAVAILABLE,
                              "failed to start RPC connection pumps");
    rpc_client_join_pumps(client);
    return -1;
}

static int rpc_client_backoff(neverc_rpc_client_t *client,
                              neverc_context_t *context) {
    int remaining = client->config.reconnect_backoff_ms;
    while (remaining > 0) {
        if (neverc_context_done(context)) return -1;
        int step = remaining > 25 ? 25 : remaining;
        rpc_sleep_ms((unsigned)step);
        remaining -= step;
    }
    return neverc_context_done(context) ? -1 : 0;
}

int neverc_rpc_client_reconnect(neverc_rpc_client_t *client,
                                neverc_context_t *context,
                                const char **errp) {
    rpc_set_error(errp, NULL);
    if (!client || !context || neverc_context_done(context)) {
        rpc_set_error(errp, "invalid RPC reconnect context");
        return NEVERC_RPC_IO_INVALID;
    }
    nc_mutex_lock(&client->lifecycle_lock);
    if (nc_atomic_load(&client->closing)) {
        nc_mutex_unlock(&client->lifecycle_lock);
        rpc_set_error(errp, "RPC client is closing");
        return NEVERC_RPC_IO_CLOSED;
    }
    if (nc_atomic_load(&client->running)) {
        nc_mutex_unlock(&client->lifecycle_lock);
        return NEVERC_RPC_IO_OK;
    }
    rpc_client_join_pumps(client);
    rpc_client_release_transport(client);
    if (rpc_client_backoff(client, context) != 0) {
        nc_mutex_unlock(&client->lifecycle_lock);
        rpc_set_error(errp, "RPC reconnect cancelled");
        return NEVERC_RPC_IO_CANCELLED;
    }

    neverc_rpc_client_config_t dial_config = client->config;
    dial_config.server_name = client->server_name;
    dial_config.root_ca_file = client->root_ca_file;
    dial_config.client_cert_file = client->client_cert_file;
    dial_config.client_key_file = client->client_key_file;
    rpc_client_transport_t transport;
    if (rpc_dial_transport(client->addr, &dial_config, context,
                           &transport, errp) != 0) {
        nc_mutex_unlock(&client->lifecycle_lock);
        return NEVERC_RPC_IO_CLOSED;
    }
    neverc_thread_channel_t *send_queue = neverc_thread_channel_create(
        client->config.send_queue_capacity);
    if (!send_queue) {
        rpc_close_dialed_transport(&transport);
        nc_mutex_unlock(&client->lifecycle_lock);
        rpc_set_error(errp, "failed to allocate RPC reconnect queue");
        return NEVERC_RPC_IO_NOMEM;
    }
    client->tcp = transport.tcp;
    client->tls = transport.tls;
    client->quic = transport.quic;
    client->quic_stream = transport.quic_stream;
    client->send_queue = send_queue;
    client->generation++;
    nc_mutex_lock(&client->keepalive_lock);
    client->awaiting_pong = 0;
    client->next_ping_ms = nc_monotonic_ms() +
        (uint64_t)client->config.ping_interval_ms;
    nc_mutex_unlock(&client->keepalive_lock);
    if (rpc_client_start_pumps(client) != 0) {
        rpc_client_release_transport(client);
        nc_mutex_unlock(&client->lifecycle_lock);
        rpc_set_error(errp, "failed to restart RPC connection pumps");
        return NEVERC_RPC_IO_CLOSED;
    }
    nc_mutex_unlock(&client->lifecycle_lock);
    return NEVERC_RPC_IO_OK;
}

static int rpc_open_payload_size(const char *method,
                                 const neverc_rpc_metadata_t *metadata,
                                 size_t metadata_count, size_t *size) {
    size_t method_length = method ? strlen(method) : 0;
    if (method_length == 0 || method_length > UINT16_MAX ||
        metadata_count > NEVERC_RPC_DEFAULT_MAX_METADATA_COUNT ||
        (metadata_count > 0 && !metadata))
        return -1;
    size_t required = NEVERC_RPC_OPEN_HEADER_SIZE + method_length;
    for (size_t i = 0; i < metadata_count; i++) {
        if (metadata[i].key_length > SIZE_MAX - required - 6 ||
            metadata[i].value_length >
                SIZE_MAX - required - 6 - metadata[i].key_length)
            return -1;
        required += 6 + metadata[i].key_length +
                    metadata[i].value_length;
    }
    *size = required;
    return 0;
}

neverc_rpc_stream_t *neverc_rpc_stream_open(
    neverc_rpc_client_t *client, neverc_context_t *context,
    const char *method, const neverc_rpc_metadata_t *metadata,
    size_t metadata_count, int idempotent, const char **errp) {
    return neverc_rpc_stream_open_codec(
        client, context, method, NEVERC_RPC_CODEC_RAW, metadata,
        metadata_count, idempotent, errp);
}

neverc_rpc_stream_t *neverc_rpc_stream_open_codec(
    neverc_rpc_client_t *client, neverc_context_t *context,
    const char *method, neverc_rpc_codec_t codec,
    const neverc_rpc_metadata_t *metadata, size_t metadata_count,
    int idempotent, const char **errp) {
    rpc_set_error(errp, NULL);
    if (!client || !context ||
        (codec != NEVERC_RPC_CODEC_RAW &&
         codec != NEVERC_RPC_CODEC_JSON &&
         codec != NEVERC_RPC_CODEC_PROTOBUF) ||
        (idempotent != 0 && idempotent != 1) ||
        neverc_context_done(context)) {
        rpc_set_error(errp, "invalid or closed RPC stream");
        return NULL;
    }
    if (!nc_atomic_load(&client->running)) {
        if (!client->config.reconnect_enabled) {
            rpc_set_error(errp, "RPC transport is closed");
            return NULL;
        }
        if (neverc_rpc_client_reconnect(client, context, errp) !=
            NEVERC_RPC_IO_OK)
            return NULL;
    }
    size_t payload_size = 0;
    if (rpc_open_payload_size(method, metadata, metadata_count,
                              &payload_size) != 0 ||
        payload_size > client->config.max_frame_size ||
        payload_size - NEVERC_RPC_OPEN_HEADER_SIZE - strlen(method) >
            client->config.max_metadata_size) {
        rpc_set_error(errp, "RPC OPEN payload exceeds configured limits");
        return NULL;
    }
    neverc_rpc_stream_t *stream =
        (neverc_rpc_stream_t *)calloc(1, sizeof(*stream));
    uint8_t *payload = (uint8_t *)malloc(payload_size);
    if (!stream || !payload) {
        free(stream);
        free(payload);
        rpc_set_error(errp, "out of memory");
        return NULL;
    }
    stream->client = client;
    stream->generation = client->generation;
    stream->status_code = NEVERC_RPC_STATUS_UNKNOWN;
    stream->receive_queue = neverc_thread_channel_create(
        client->config.receive_queue_capacity);
    stream->context = neverc_context_with_cancel_handle(context,
                                                         &stream->cancel);
    if (!stream->receive_queue || !stream->context || !stream->cancel) {
        if (stream->receive_queue)
            neverc_thread_channel_free(stream->receive_queue);
        if (stream->context) neverc_context_free(stream->context);
        if (stream->cancel)
            neverc_context_cancel_handle_free(stream->cancel);
        free(stream);
        free(payload);
        rpc_set_error(errp, "out of memory");
        return NULL;
    }
    nc_mutex_init(&stream->lock);

    nc_mutex_lock(&client->streams_lock);
    size_t slot = client->config.max_inflight_streams;
    for (size_t i = 0; i < client->config.max_inflight_streams; i++) {
        if (!client->streams[i]) {
            slot = i;
            break;
        }
    }
    if (slot == client->config.max_inflight_streams ||
        client->next_request_id > UINT64_MAX - 2) {
        nc_mutex_unlock(&client->streams_lock);
        rpc_set_error(errp, "RPC connection stream limit reached");
        free(payload);
        goto open_failed;
    }
    stream->request_id = client->next_request_id;
    client->next_request_id += 2;
    client->streams[slot] = stream;
    client->stream_count++;
    nc_mutex_unlock(&client->streams_lock);

    neverc_rpc_open_t open;
    memset(&open, 0, sizeof(open));
    open.method = method;
    open.method_length = strlen(method);
    open.deadline_ms = neverc_context_deadline(context);
    open.codec = codec;
    open.metadata = (neverc_rpc_metadata_t *)metadata;
    open.metadata_count = metadata_count;
    size_t encoded_size = 0;
    if (neverc_rpc_open_encode(&open, payload, payload_size,
                               &encoded_size) != 0) {
        rpc_set_error(errp, "invalid RPC method or metadata");
        goto registered_open_failed;
    }
    neverc_rpc_frame_header_t header;
    memset(&header, 0, sizeof(header));
    header.version = NEVERC_RPC_VERSION_1;
    header.type = NEVERC_RPC_FRAME_OPEN;
    header.flags = idempotent ? NEVERC_RPC_FLAG_IDEMPOTENT : 0;
    header.payload_length = (uint32_t)encoded_size;
    header.request_id = stream->request_id;
    int queued = rpc_queue_encoded(client, context, &header, payload);
    free(payload);
    if (queued != NEVERC_RPC_IO_OK) {
        rpc_set_error(errp, "RPC send queue rejected OPEN");
        goto registered_open_failed_no_payload;
    }
    return stream;

registered_open_failed:
    free(payload);
registered_open_failed_no_payload:
    nc_mutex_lock(&client->streams_lock);
    for (size_t i = 0; i < client->config.max_inflight_streams; i++)
        if (client->streams[i] == stream) {
            client->streams[i] = NULL;
            client->stream_count--;
            break;
        }
    nc_mutex_unlock(&client->streams_lock);
open_failed:
    neverc_context_cancel_handle_cancel(stream->cancel);
    neverc_context_cancel_handle_free(stream->cancel);
    neverc_context_free(stream->context);
    neverc_thread_channel_free(stream->receive_queue);
    nc_mutex_destroy(&stream->lock);
    free(stream);
    return NULL;
}

int neverc_rpc_stream_send(neverc_rpc_stream_t *stream,
                           neverc_context_t *context,
                           const void *data, size_t len) {
    if (!stream || (!data && len > 0) || !context)
        return NEVERC_RPC_IO_INVALID;
    nc_mutex_lock(&stream->lock);
    int closed = stream->send_closed || stream->ended ||
                 stream->generation != stream->client->generation ||
                 !nc_atomic_load(&stream->client->running) ||
                 neverc_context_done(stream->context);
    nc_mutex_unlock(&stream->lock);
    if (closed) return NEVERC_RPC_IO_CLOSED;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > stream->client->config.max_frame_size)
            chunk = stream->client->config.max_frame_size;
        neverc_rpc_frame_header_t header;
        memset(&header, 0, sizeof(header));
        header.version = NEVERC_RPC_VERSION_1;
        header.type = NEVERC_RPC_FRAME_DATA;
        header.payload_length = (uint32_t)chunk;
        header.request_id = stream->request_id;
        int result = rpc_queue_encoded(stream->client, context, &header,
                                       bytes + sent);
        if (result != NEVERC_RPC_IO_OK) return result;
        sent += chunk;
    }
    return NEVERC_RPC_IO_OK;
}

int neverc_rpc_stream_close_send(neverc_rpc_stream_t *stream,
                                 neverc_context_t *context) {
    if (!stream || !context) return NEVERC_RPC_IO_INVALID;
    nc_mutex_lock(&stream->lock);
    if (stream->send_closed || stream->ended ||
        stream->generation != stream->client->generation ||
        !nc_atomic_load(&stream->client->running) ||
        neverc_context_done(stream->context)) {
        nc_mutex_unlock(&stream->lock);
        return NEVERC_RPC_IO_CLOSED;
    }
    stream->send_closed = 1;
    nc_mutex_unlock(&stream->lock);
    neverc_rpc_frame_header_t header;
    memset(&header, 0, sizeof(header));
    header.version = NEVERC_RPC_VERSION_1;
    header.type = NEVERC_RPC_FRAME_DATA;
    header.flags = NEVERC_RPC_FLAG_END_STREAM;
    header.request_id = stream->request_id;
    return rpc_queue_encoded(stream->client, context, &header, NULL);
}

int neverc_rpc_stream_recv(neverc_rpc_stream_t *stream,
                           neverc_context_t *context,
                           void *buf, size_t buflen, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!stream || !context || !out_len || (!buf && buflen > 0))
        return NEVERC_RPC_IO_INVALID;
    void *value = NULL;
    int received = neverc_thread_channel_receive_context(
        stream->receive_queue, context, &value);
    if (received == NEVERC_THREAD_CANCELLED)
        return NEVERC_RPC_IO_CANCELLED;
    if (received == NEVERC_THREAD_CLOSED) return NEVERC_RPC_IO_END;
    if (received != NEVERC_THREAD_OK) return NEVERC_RPC_IO_CLOSED;
    rpc_inbound_t *inbound = (rpc_inbound_t *)value;
    if (inbound->header.type == NEVERC_RPC_FRAME_DATA) {
        if (inbound->header.payload_length > buflen) {
            free(inbound);
            (void)neverc_rpc_stream_cancel(
                stream, NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED,
                "RPC receive buffer is too small");
            return NEVERC_RPC_IO_INVALID;
        }
        if (inbound->header.payload_length > 0)
            memcpy(buf, inbound->payload, inbound->header.payload_length);
        *out_len = inbound->header.payload_length;
        free(inbound);
        return NEVERC_RPC_IO_OK;
    }
    if (inbound->header.type == NEVERC_RPC_FRAME_END ||
        inbound->header.type == NEVERC_RPC_FRAME_CANCEL) {
        int extra_frame = 0;
        for (;;) {
            void *queued = NULL;
            int drained = neverc_thread_channel_try_receive(
                stream->receive_queue, &queued);
            if (drained != NEVERC_THREAD_OK) break;
            extra_frame = 1;
            free(queued);
        }
        if (extra_frame) {
            rpc_stream_set_terminal(
                stream, NEVERC_RPC_STATUS_DATA_LOSS,
                "RPC DATA after END");
            free(inbound);
            return NEVERC_RPC_IO_PROTOCOL;
        }
        char *message = NULL;
        if (inbound->header.payload_length > 0) {
            message = (char *)malloc(
                (size_t)inbound->header.payload_length + 1);
            if (message) {
                memcpy(message, inbound->payload,
                       inbound->header.payload_length);
                message[inbound->header.payload_length] = '\0';
            }
        }
        rpc_stream_set_terminal(
            stream,
            (neverc_rpc_status_code_t)inbound->header.code,
            message ? message : "");
        free(message);
        free(inbound);
        return NEVERC_RPC_IO_END;
    }
    free(inbound);
    return NEVERC_RPC_IO_PROTOCOL;
}

int neverc_rpc_stream_cancel(neverc_rpc_stream_t *stream,
                             neverc_rpc_status_code_t code,
                             const char *message) {
    if (!stream || !neverc_rpc_status_code_valid((uint32_t)code) ||
        code == NEVERC_RPC_STATUS_OK ||
        (message && strlen(message) > stream->client->config.max_frame_size))
        return NEVERC_RPC_IO_INVALID;
    neverc_rpc_frame_header_t header;
    memset(&header, 0, sizeof(header));
    header.version = NEVERC_RPC_VERSION_1;
    header.type = NEVERC_RPC_FRAME_CANCEL;
    header.request_id = stream->request_id;
    header.code = (uint32_t)code;
    header.payload_length = message ? (uint32_t)strlen(message) : 0;
    int result = stream->generation == stream->client->generation
        ? rpc_queue_encoded(stream->client, NULL, &header, message)
        : NEVERC_RPC_IO_CLOSED;
    rpc_stream_set_terminal(stream, code, message);
    return result;
}

neverc_rpc_status_t neverc_rpc_stream_status(neverc_rpc_stream_t *stream) {
    neverc_rpc_status_t status = { NEVERC_RPC_STATUS_UNKNOWN, NULL };
    if (!stream) return status;
    nc_mutex_lock(&stream->lock);
    status.code = stream->status_code;
    status.message = stream->status_message;
    nc_mutex_unlock(&stream->lock);
    return status;
}

uint64_t neverc_rpc_stream_id(neverc_rpc_stream_t *stream) {
    return stream ? stream->request_id : 0;
}

void neverc_rpc_stream_free(neverc_rpc_stream_t *stream) {
    if (!stream) return;
    nc_mutex_lock(&stream->lock);
    int needs_cancel = !stream->ended;
    nc_mutex_unlock(&stream->lock);
    if (needs_cancel)
        (void)neverc_rpc_stream_cancel(stream, NEVERC_RPC_STATUS_CANCELLED,
                                       "RPC stream released");
    neverc_rpc_client_t *client = stream->client;
    nc_mutex_lock(&client->streams_lock);
    for (size_t i = 0; i < client->config.max_inflight_streams; i++) {
        if (client->streams[i] == stream) {
            client->streams[i] = NULL;
            client->stream_count--;
            break;
        }
    }
    neverc_thread_channel_close(stream->receive_queue);
    nc_mutex_unlock(&client->streams_lock);
    for (;;) {
        void *value = NULL;
        int result = neverc_thread_channel_try_receive(
            stream->receive_queue, &value);
        if (result != NEVERC_THREAD_OK) break;
        free(value);
    }
    neverc_context_cancel_handle_cancel(stream->cancel);
    neverc_context_cancel_handle_free(stream->cancel);
    neverc_context_free(stream->context);
    neverc_thread_channel_free(stream->receive_queue);
    nc_mutex_destroy(&stream->lock);
    free(stream->status_message);
    free(stream);
}

void neverc_rpc_client_close(neverc_rpc_client_t *client) {
    if (!client) return;
    nc_mutex_lock(&client->lifecycle_lock);
    nc_atomic_store(&client->closing, 1);
    rpc_client_stop_transport(client, NEVERC_RPC_STATUS_CANCELLED,
                              "RPC client closed");
    if (client->send_queue)
        neverc_thread_channel_close(client->send_queue);
    if (client->tcp) {
        (void)neverc_tcp_shutdown_read(client->tcp);
        (void)neverc_tcp_shutdown_write(client->tcp);
    }
    if (client->quic)
        neverc_quic_conn_close(client->quic, 0, "RPC client closed");
    rpc_client_join_pumps(client);
    rpc_client_release_transport(client);
    nc_mutex_unlock(&client->lifecycle_lock);
    nc_mutex_destroy(&client->keepalive_lock);
    nc_mutex_destroy(&client->streams_lock);
    nc_mutex_destroy(&client->lifecycle_lock);
    free(client->streams);
    free(client->client_key_file);
    free(client->client_cert_file);
    free(client->root_ca_file);
    free(client->server_name);
    free(client->addr);
    free(client);
}

int neverc_rpc_client_call(
    neverc_rpc_client_t *client, neverc_context_t *context,
    const char *method, const neverc_rpc_metadata_t *metadata,
    size_t metadata_count, const void *request, size_t request_length,
    void *response, size_t response_capacity, size_t *response_length,
    neverc_rpc_status_t *status) {
    neverc_rpc_call_options_t options = neverc_rpc_call_options_default();
    return neverc_rpc_client_call_ex(
        client, context, method, metadata, metadata_count, request,
        request_length, response, response_capacity, response_length,
        status, &options);
}

int neverc_rpc_client_call_ex(
    neverc_rpc_client_t *client, neverc_context_t *context,
    const char *method, const neverc_rpc_metadata_t *metadata,
    size_t metadata_count, const void *request, size_t request_length,
    void *response, size_t response_capacity, size_t *response_length,
    neverc_rpc_status_t *status,
    const neverc_rpc_call_options_t *options) {
    if (response_length) *response_length = 0;
    neverc_rpc_call_options_t effective = options
        ? *options : neverc_rpc_call_options_default();
    if (!client || !context || !response_length || !status ||
        (!request && request_length > 0) ||
        (!response && response_capacity > 0) ||
        (effective.codec != NEVERC_RPC_CODEC_RAW &&
         effective.codec != NEVERC_RPC_CODEC_JSON &&
         effective.codec != NEVERC_RPC_CODEC_PROTOBUF) ||
        (effective.idempotent != 0 && effective.idempotent != 1) ||
        effective.max_attempts == 0 || effective.max_attempts > 16 ||
        (!effective.idempotent && effective.max_attempts != 1))
        return NEVERC_RPC_IO_INVALID;
    status->code = NEVERC_RPC_STATUS_UNKNOWN;
    status->message = NULL;
    int final_result = NEVERC_RPC_IO_CLOSED;
    for (size_t attempt = 0; attempt < effective.max_attempts; attempt++) {
        *response_length = 0;
        const char *error = NULL;
        neverc_rpc_stream_t *stream = neverc_rpc_stream_open_codec(
            client, context, method, effective.codec, metadata,
            metadata_count, effective.idempotent, &error);
        (void)error;
        if (!stream) {
            final_result = neverc_context_done(context)
                ? NEVERC_RPC_IO_CANCELLED : NEVERC_RPC_IO_CLOSED;
        } else {
            int result = neverc_rpc_stream_send(stream, context, request,
                                                request_length);
            if (result == NEVERC_RPC_IO_OK)
                result = neverc_rpc_stream_close_send(stream, context);
            size_t total = 0;
            while (result == NEVERC_RPC_IO_OK) {
                size_t received = 0;
                void *destination = response
                    ? (uint8_t *)response + total : NULL;
                result = neverc_rpc_stream_recv(
                    stream, context, destination,
                    response_capacity - total, &received);
                total += received;
            }
            *response_length = total;
            *status = neverc_rpc_stream_status(stream);
            status->message = NULL;
            final_result = result == NEVERC_RPC_IO_END
                ? NEVERC_RPC_IO_OK : result;
            neverc_rpc_stream_free(stream);
        }
        int retryable = effective.idempotent &&
            attempt + 1 < effective.max_attempts &&
            !neverc_context_done(context) &&
            (final_result == NEVERC_RPC_IO_CLOSED ||
             status->code == NEVERC_RPC_STATUS_UNAVAILABLE);
        if (!retryable) break;
    }
    return final_result;
}
