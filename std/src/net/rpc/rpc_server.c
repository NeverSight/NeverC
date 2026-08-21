#include "neverc/std/net/rpc.h"

#include "neverc/std/crypto/tls.h"
#include "neverc/std/net/quic.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/thread.h"
#include "../_net_buffer.h"
#include "../_net_platform.h"
#include "../_net_thread.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define RPC_SERVER_MAX_METHODS 256
#define RPC_SERVER_MAX_INTERCEPTORS 16
#define RPC_SERVER_MAX_TENANT_KEY_SIZE 128

typedef struct rpc_server_connection rpc_server_connection_t;

typedef struct {
    char *method;
    neverc_rpc_handler_func_t handler;
    void *context;
} rpc_method_t;

typedef struct {
    neverc_rpc_interceptor_func_t function;
    void *context;
} rpc_interceptor_t;

typedef struct {
    uint64_t hash;
    uint64_t last_refill_ms;
    double tokens;
    size_t key_length;
    uint8_t key[RPC_SERVER_MAX_TENANT_KEY_SIZE];
    int occupied;
} rpc_tenant_bucket_t;

typedef struct {
    size_t length;
    uint8_t bytes[];
} rpc_server_outbound_t;

typedef struct {
    neverc_rpc_frame_header_t header;
    uint8_t payload[];
} rpc_server_inbound_t;

struct neverc_rpc_server_stream {
    rpc_server_connection_t *connection;
    uint64_t request_id;
    char *method;
    uint8_t *open_payload;
    neverc_rpc_metadata_t *metadata;
    size_t metadata_count;
    neverc_rpc_codec_t codec;
    neverc_thread_channel_t *receive_queue;
    neverc_context_t *context;
    neverc_context_cancel_handle_t *cancel;
    nc_mutex_t lock;
    int receive_closed;
    int ended;
    int peer_cancelled;
    neverc_rpc_handler_func_t handler;
    void *handler_context;
};

struct rpc_server_connection {
    neverc_rpc_server_t *server;
    neverc_tcp_conn_t *tcp;
    neverc_tls_conn_t *tls;
    neverc_quic_conn_t *quic;
    neverc_quic_stream_t *quic_stream;
    neverc_tls_config_t *tls_config;
    neverc_thread_channel_t *send_queue;
    nc_thread_t writer_thread;
    int writer_started;
    volatile int running;
    nc_mutex_t streams_lock;
    neverc_rpc_server_stream_t **streams;
    size_t stream_count;
    nc_mutex_t handlers_lock;
    nc_cond_t handlers_done;
    size_t active_handlers;
    rpc_server_connection_t *next;
    rpc_server_connection_t *prev;
};

struct neverc_rpc_server {
    neverc_rpc_server_config_t config;
    nc_mutex_t lifecycle_lock;
    volatile int serving;
    volatile int running;
    volatile int stop_requested;
    volatile int active_connections;
    volatile int bound_port;
    neverc_context_t *serve_context;
    neverc_context_cancel_handle_t *serve_cancel;
    neverc_tcp_listener_t *listener;
    neverc_quic_endpoint_t *quic_endpoint;
    neverc_thread_executor_t *connection_executor;
    neverc_thread_executor_t *handler_executor;
    neverc_tls_config_t *tls_config;
    rpc_method_t methods[RPC_SERVER_MAX_METHODS];
    size_t method_count;
    rpc_interceptor_t interceptors[RPC_SERVER_MAX_INTERCEPTORS];
    size_t interceptor_count;
    rpc_interceptor_t authenticator;
    rpc_interceptor_t authorizer;
    int has_authenticator;
    int has_authorizer;
    nc_mutex_t tenant_lock;
    rpc_tenant_bucket_t *tenant_buckets;
    size_t max_tenants;
    uint32_t tenant_requests_per_second;
    uint32_t tenant_burst;
    neverc_rpc_tenant_key_func_t tenant_key;
    void *tenant_context;
    rpc_server_connection_t *connections;
};

static neverc_rpc_status_code_t rpc_server_call_interceptor(
    const rpc_interceptor_t *interceptor,
    neverc_rpc_server_stream_t *stream) {
    neverc_rpc_status_code_t status = interceptor->function(
        stream, interceptor->context);
    return neverc_rpc_status_code_valid((uint32_t)status)
        ? status : NEVERC_RPC_STATUS_INTERNAL;
}

static uint64_t rpc_tenant_hash(const uint8_t *key, size_t length) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}

static neverc_rpc_status_code_t rpc_server_check_tenant_rate(
    neverc_rpc_server_t *server, neverc_rpc_server_stream_t *stream) {
    if (!server->tenant_key) return NEVERC_RPC_STATUS_OK;
    uint8_t key[RPC_SERVER_MAX_TENANT_KEY_SIZE];
    size_t key_length = server->tenant_key(
        stream, server->tenant_context, key, sizeof(key));
    if (key_length == 0 || key_length > sizeof(key))
        return NEVERC_RPC_STATUS_UNAUTHENTICATED;
    uint64_t hash = rpc_tenant_hash(key, key_length);
    uint64_t now = nc_monotonic_ms();
    neverc_rpc_status_code_t status = NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED;
    nc_mutex_lock(&server->tenant_lock);
    size_t start = (size_t)(hash % server->max_tenants);
    for (size_t probe = 0; probe < server->max_tenants; probe++) {
        size_t index = (start + probe) % server->max_tenants;
        rpc_tenant_bucket_t *bucket = &server->tenant_buckets[index];
        if (!bucket->occupied) {
            bucket->occupied = 1;
            bucket->hash = hash;
            bucket->key_length = key_length;
            memcpy(bucket->key, key, key_length);
            bucket->tokens = (double)server->tenant_burst;
            bucket->last_refill_ms = now;
        } else if (bucket->hash != hash ||
                   bucket->key_length != key_length ||
                   memcmp(bucket->key, key, key_length) != 0) {
            continue;
        }
        if (now > bucket->last_refill_ms) {
            double added = (double)(now - bucket->last_refill_ms) *
                (double)server->tenant_requests_per_second / 1000.0;
            bucket->tokens += added;
            if (bucket->tokens > (double)server->tenant_burst)
                bucket->tokens = (double)server->tenant_burst;
            bucket->last_refill_ms = now;
        }
        if (bucket->tokens >= 1.0) {
            bucket->tokens -= 1.0;
            status = NEVERC_RPC_STATUS_OK;
        }
        break;
    }
    nc_mutex_unlock(&server->tenant_lock);
    return status;
}

static int rpc_server_config_valid(const neverc_rpc_server_config_t *config) {
    return config && config->max_frame_size > 0 &&
           config->max_frame_size <= INT_MAX &&
           config->max_metadata_size > 0 &&
           config->max_metadata_size <= config->max_frame_size &&
           config->max_streams_per_connection > 0 &&
           config->max_streams_per_connection <= 65536 &&
           config->send_queue_capacity > 0 &&
           config->receive_queue_capacity > 0 &&
           config->connection_workers > 0 &&
           config->connection_queue_capacity > 0 &&
           config->handler_workers > 0 &&
           config->handler_queue_capacity > 0 &&
           config->max_connections > 0 &&
           config->max_connections <= INT_MAX && config->io_timeout_ms > 0;
}

neverc_rpc_server_config_t neverc_rpc_server_config_default(void) {
    neverc_rpc_server_config_t config;
    memset(&config, 0, sizeof(config));
    config.max_frame_size = NEVERC_RPC_DEFAULT_MAX_FRAME_SIZE;
    config.max_metadata_size = NEVERC_RPC_DEFAULT_MAX_METADATA_SIZE;
    config.max_streams_per_connection = 1024;
    config.send_queue_capacity = 1024;
    config.receive_queue_capacity = 32;
    config.connection_workers = 64;
    config.connection_queue_capacity = 64;
    config.handler_workers = 8;
    config.handler_queue_capacity = 1024;
    config.max_connections = 128;
    config.io_timeout_ms = 30000;
    return config;
}

static int rpc_server_transport_read(rpc_server_connection_t *connection,
                                     void *data, size_t length) {
    if (connection->quic_stream)
        return neverc_quic_stream_read(connection->quic_stream,
                                       data, length);
    return connection->tls
        ? neverc_tls_read(connection->tls, data, length)
        : neverc_tcp_read(connection->tcp, data, length);
}

static int rpc_server_transport_write_all(
    rpc_server_connection_t *connection, const void *data, size_t length) {
    size_t written = 0;
    while (written < length) {
        size_t chunk = length - written;
        if (chunk > (size_t)INT_MAX) chunk = (size_t)INT_MAX;
        int n = connection->quic_stream
            ? neverc_quic_stream_write(connection->quic_stream,
                                       (const uint8_t *)data + written, chunk)
            : connection->tls
            ? neverc_tls_write(connection->tls,
                               (const uint8_t *)data + written, chunk)
            : neverc_tcp_write(connection->tcp,
                               (const uint8_t *)data + written, chunk);
        if (n <= 0) return -1;
        written += (size_t)n;
    }
    return 0;
}

static neverc_rpc_server_stream_t *rpc_server_find_stream_locked(
    rpc_server_connection_t *connection, uint64_t request_id) {
    size_t capacity = connection->server->config.max_streams_per_connection;
    for (size_t i = 0; i < capacity; i++) {
        neverc_rpc_server_stream_t *stream = connection->streams[i];
        if (stream && stream->request_id == request_id) return stream;
    }
    return NULL;
}

static int rpc_server_queue_frame(
    rpc_server_connection_t *connection, neverc_context_t *context,
    const neverc_rpc_frame_header_t *header, const void *payload) {
    if (!connection || !header || !nc_atomic_load(&connection->running))
        return NEVERC_RPC_IO_CLOSED;
    size_t total = NEVERC_RPC_FRAME_HEADER_SIZE +
                   (size_t)header->payload_length;
    rpc_server_outbound_t *outbound =
        (rpc_server_outbound_t *)malloc(sizeof(*outbound) + total);
    if (!outbound) return NEVERC_RPC_IO_NOMEM;
    neverc_rpc_frame_t frame;
    frame.header = *header;
    frame.payload = (const uint8_t *)payload;
    if (neverc_rpc_frame_encode(&frame, outbound->bytes, total,
                                &outbound->length) != 0) {
        free(outbound);
        return NEVERC_RPC_IO_INVALID;
    }
    int queued = context
        ? neverc_thread_channel_send_context(connection->send_queue, context,
                                             outbound)
        : neverc_thread_channel_try_send(connection->send_queue, outbound);
    if (queued != NEVERC_THREAD_OK) free(outbound);
    if (queued == NEVERC_THREAD_CANCELLED)
        return NEVERC_RPC_IO_CANCELLED;
    if (queued == NEVERC_THREAD_WOULD_BLOCK)
        return NEVERC_RPC_IO_WOULD_BLOCK;
    return queued == NEVERC_THREAD_OK ? NEVERC_RPC_IO_OK
                                     : NEVERC_RPC_IO_CLOSED;
}

static void rpc_server_cancel_streams(rpc_server_connection_t *connection) {
    nc_mutex_lock(&connection->streams_lock);
    size_t capacity = connection->server->config.max_streams_per_connection;
    for (size_t i = 0; i < capacity; i++) {
        neverc_rpc_server_stream_t *stream = connection->streams[i];
        if (!stream) continue;
        nc_mutex_lock(&stream->lock);
        stream->peer_cancelled = 1;
        stream->receive_closed = 1;
        neverc_context_cancel_handle_cancel(stream->cancel);
        neverc_thread_channel_close(stream->receive_queue);
        nc_mutex_unlock(&stream->lock);
    }
    nc_mutex_unlock(&connection->streams_lock);
}

static void rpc_server_stop_connection(rpc_server_connection_t *connection) {
    if (!nc_atomic_cas(&connection->running, 1, 0)) return;
    if (connection->tcp) {
        (void)neverc_tcp_shutdown_read(connection->tcp);
        (void)neverc_tcp_shutdown_write(connection->tcp);
    }
    if (connection->quic)
        neverc_quic_conn_close(connection->quic, 0,
                               "RPC server connection stopped");
    if (connection->send_queue)
        neverc_thread_channel_close(connection->send_queue);
    rpc_server_cancel_streams(connection);
}

static void *rpc_server_writer_main(void *arg) {
    rpc_server_connection_t *connection =
        (rpc_server_connection_t *)arg;
    for (;;) {
        void *value = NULL;
        int received = neverc_thread_channel_receive(connection->send_queue,
                                                      &value);
        if (received == NEVERC_THREAD_CLOSED) break;
        if (received != NEVERC_THREAD_OK) continue;
        rpc_server_outbound_t *outbound =
            (rpc_server_outbound_t *)value;
        if (!nc_atomic_load(&connection->running) ||
            rpc_server_transport_write_all(connection, outbound->bytes,
                                           outbound->length) != 0) {
            free(outbound);
            rpc_server_stop_connection(connection);
            break;
        }
        free(outbound);
    }
    return NULL;
}

static void rpc_server_stream_destroy(neverc_rpc_server_stream_t *stream) {
    rpc_server_connection_t *connection = stream->connection;
    nc_mutex_lock(&connection->streams_lock);
    size_t capacity = connection->server->config.max_streams_per_connection;
    for (size_t i = 0; i < capacity; i++) {
        if (connection->streams[i] == stream) {
            connection->streams[i] = NULL;
            connection->stream_count--;
            break;
        }
    }
    neverc_thread_channel_close(stream->receive_queue);
    nc_mutex_unlock(&connection->streams_lock);
    for (;;) {
        void *value = NULL;
        int result = neverc_thread_channel_try_receive(stream->receive_queue,
                                                        &value);
        if (result != NEVERC_THREAD_OK) break;
        free(value);
    }
    neverc_context_cancel_handle_cancel(stream->cancel);
    neverc_context_cancel_handle_free(stream->cancel);
    neverc_context_free(stream->context);
    neverc_thread_channel_free(stream->receive_queue);
    nc_mutex_destroy(&stream->lock);
    free(stream->metadata);
    free(stream->open_payload);
    free(stream->method);
    free(stream);
}

uint64_t neverc_rpc_server_stream_id(neverc_rpc_server_stream_t *stream) {
    return stream ? stream->request_id : 0;
}

const char *neverc_rpc_server_stream_method(
    neverc_rpc_server_stream_t *stream) {
    return stream ? stream->method : NULL;
}

neverc_context_t *neverc_rpc_server_stream_context(
    neverc_rpc_server_stream_t *stream) {
    return stream ? stream->context : NULL;
}

const neverc_rpc_metadata_t *neverc_rpc_server_stream_metadata(
    neverc_rpc_server_stream_t *stream, size_t *count) {
    if (count) *count = stream ? stream->metadata_count : 0;
    return stream ? stream->metadata : NULL;
}

neverc_rpc_codec_t neverc_rpc_server_stream_codec(
    neverc_rpc_server_stream_t *stream) {
    return stream ? stream->codec : NEVERC_RPC_CODEC_RAW;
}

const uint8_t *neverc_rpc_server_stream_peer_certificate(
    neverc_rpc_server_stream_t *stream, size_t *out_len) {
    if (!stream || !stream->connection || !stream->connection->tls) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    return neverc_tls_peer_certificate(stream->connection->tls, out_len);
}

int neverc_rpc_server_stream_recv(
    neverc_rpc_server_stream_t *stream, void *buf, size_t buflen,
    size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!stream || !out_len || (!buf && buflen > 0))
        return NEVERC_RPC_IO_INVALID;
    void *value = NULL;
    int received = neverc_thread_channel_receive_context(
        stream->receive_queue, stream->context, &value);
    if (received == NEVERC_THREAD_CANCELLED)
        return NEVERC_RPC_IO_CANCELLED;
    if (received == NEVERC_THREAD_CLOSED) {
        /* CANCEL (and connection teardown) close the queue. That is not a
         * clean client half-close: Go net/rpc and grpc-go Recv report the
         * cancellation error, not io.EOF, so handlers must not fail-open as
         * success. */
        nc_mutex_lock(&stream->lock);
        int cancelled = stream->peer_cancelled;
        nc_mutex_unlock(&stream->lock);
        return cancelled ? NEVERC_RPC_IO_CANCELLED : NEVERC_RPC_IO_END;
    }
    if (received != NEVERC_THREAD_OK) return NEVERC_RPC_IO_CLOSED;
    rpc_server_inbound_t *inbound = (rpc_server_inbound_t *)value;
    if (inbound->header.type != NEVERC_RPC_FRAME_DATA ||
        inbound->header.payload_length > buflen) {
        free(inbound);
        return NEVERC_RPC_IO_INVALID;
    }
    if (inbound->header.payload_length > 0)
        memcpy(buf, inbound->payload, inbound->header.payload_length);
    *out_len = inbound->header.payload_length;
    free(inbound);
    return NEVERC_RPC_IO_OK;
}

int neverc_rpc_server_stream_send(
    neverc_rpc_server_stream_t *stream, const void *data, size_t len) {
    if (!stream || (!data && len > 0)) return NEVERC_RPC_IO_INVALID;
    nc_mutex_lock(&stream->lock);
    int closed = stream->ended || stream->peer_cancelled ||
                 neverc_context_done(stream->context);
    nc_mutex_unlock(&stream->lock);
    if (closed) return NEVERC_RPC_IO_CLOSED;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > stream->connection->server->config.max_frame_size)
            chunk = stream->connection->server->config.max_frame_size;
        neverc_rpc_frame_header_t header;
        memset(&header, 0, sizeof(header));
        header.version = NEVERC_RPC_VERSION_1;
        header.type = NEVERC_RPC_FRAME_DATA;
        header.flags = NEVERC_RPC_FLAG_RESPONSE;
        header.payload_length = (uint32_t)chunk;
        header.request_id = stream->request_id;
        int result = rpc_server_queue_frame(stream->connection,
                                             stream->context, &header,
                                             (const uint8_t *)data + sent);
        if (result != NEVERC_RPC_IO_OK) return result;
        sent += chunk;
    }
    return NEVERC_RPC_IO_OK;
}

static int rpc_server_stream_end_internal(
    neverc_rpc_server_stream_t *stream, neverc_rpc_status_code_t code,
    const char *message, neverc_context_t *queue_context) {
    size_t message_length = message ? strlen(message) : 0;
    if (!stream || !neverc_rpc_status_code_valid((uint32_t)code) ||
        message_length > stream->connection->server->config.max_frame_size)
        return NEVERC_RPC_IO_INVALID;
    nc_mutex_lock(&stream->lock);
    if (stream->ended) {
        nc_mutex_unlock(&stream->lock);
        return NEVERC_RPC_IO_CLOSED;
    }
    stream->ended = 1;
    nc_mutex_unlock(&stream->lock);
    neverc_rpc_frame_header_t header;
    memset(&header, 0, sizeof(header));
    header.version = NEVERC_RPC_VERSION_1;
    header.type = NEVERC_RPC_FRAME_END;
    header.flags = NEVERC_RPC_FLAG_RESPONSE |
                   NEVERC_RPC_FLAG_END_STREAM;
    header.payload_length = (uint32_t)message_length;
    header.request_id = stream->request_id;
    header.code = (uint32_t)code;
    return rpc_server_queue_frame(stream->connection, queue_context,
                                  &header, message);
}

int neverc_rpc_server_stream_end(
    neverc_rpc_server_stream_t *stream, neverc_rpc_status_code_t code,
    const char *message) {
    return rpc_server_stream_end_internal(stream, code, message,
                                          stream ? stream->context : NULL);
}

static void rpc_server_handler_task(void *arg) {
    neverc_rpc_server_stream_t *stream =
        (neverc_rpc_server_stream_t *)arg;
    neverc_rpc_server_t *server = stream->connection->server;
    neverc_rpc_status_code_t rejected = NEVERC_RPC_STATUS_OK;
    if (neverc_context_done(stream->context)) {
        const char *error = neverc_context_err(stream->context);
        rejected = error && strcmp(error, "context deadline exceeded") == 0
            ? NEVERC_RPC_STATUS_DEADLINE_EXCEEDED
            : NEVERC_RPC_STATUS_CANCELLED;
    }
    if (rejected == NEVERC_RPC_STATUS_OK && server->has_authenticator)
        rejected = rpc_server_call_interceptor(&server->authenticator,
                                               stream);
    if (rejected == NEVERC_RPC_STATUS_OK && server->has_authorizer)
        rejected = rpc_server_call_interceptor(&server->authorizer, stream);
    if (rejected == NEVERC_RPC_STATUS_OK)
        rejected = rpc_server_check_tenant_rate(server, stream);
    for (size_t i = 0;
         rejected == NEVERC_RPC_STATUS_OK && i < server->interceptor_count;
         i++) {
        rejected = rpc_server_call_interceptor(&server->interceptors[i],
                                               stream);
    }
    if (rejected != NEVERC_RPC_STATUS_OK) {
        (void)neverc_rpc_server_stream_end(stream, rejected,
                                           "RPC request rejected");
    } else {
        stream->handler(stream, stream->handler_context);
        nc_mutex_lock(&stream->lock);
        int ended = stream->ended;
        int peer_cancelled = stream->peer_cancelled;
        nc_mutex_unlock(&stream->lock);
        if (!ended && !peer_cancelled) {
            neverc_rpc_status_code_t code = NEVERC_RPC_STATUS_OK;
            if (neverc_context_done(stream->context)) {
                const char *error = neverc_context_err(stream->context);
                code = error && strcmp(error,
                                       "context deadline exceeded") == 0
                    ? NEVERC_RPC_STATUS_DEADLINE_EXCEEDED
                    : NEVERC_RPC_STATUS_CANCELLED;
            }
            (void)neverc_rpc_server_stream_end(stream, code,
                                               code == NEVERC_RPC_STATUS_OK
                                                   ? NULL
                                                   : "RPC request cancelled");
        }
    }
    rpc_server_connection_t *connection = stream->connection;
    rpc_server_stream_destroy(stream);
    nc_mutex_lock(&connection->handlers_lock);
    connection->active_handlers--;
    if (connection->active_handlers == 0)
        nc_cond_broadcast(&connection->handlers_done);
    nc_mutex_unlock(&connection->handlers_lock);
}

static rpc_method_t *rpc_server_find_method(neverc_rpc_server_t *server,
                                            const char *method,
                                            size_t method_length) {
    for (size_t i = 0; i < server->method_count; i++) {
        if (strlen(server->methods[i].method) == method_length &&
            memcmp(server->methods[i].method, method, method_length) == 0)
            return &server->methods[i];
    }
    return NULL;
}

static int rpc_server_dispatch_open(rpc_server_connection_t *connection,
                                    const neverc_rpc_frame_t *frame) {
    neverc_rpc_server_t *server = connection->server;
    if ((frame->header.request_id & 1U) == 0 ||
        (frame->header.flags & NEVERC_RPC_FLAG_RESPONSE) != 0)
        return -1;
    neverc_rpc_server_stream_t *stream =
        (neverc_rpc_server_stream_t *)calloc(1, sizeof(*stream));
    if (!stream) return -1;
    stream->connection = connection;
    stream->request_id = frame->header.request_id;
    stream->open_payload = (uint8_t *)malloc(frame->header.payload_length);
    stream->metadata = (neverc_rpc_metadata_t *)calloc(
        NEVERC_RPC_DEFAULT_MAX_METADATA_COUNT, sizeof(*stream->metadata));
    stream->receive_queue = neverc_thread_channel_create(
        server->config.receive_queue_capacity);
    if (!stream->open_payload || !stream->metadata ||
        !stream->receive_queue) {
        if (stream->receive_queue)
            neverc_thread_channel_free(stream->receive_queue);
        free(stream->metadata);
        free(stream->open_payload);
        free(stream);
        return -1;
    }
    memcpy(stream->open_payload, frame->payload,
           frame->header.payload_length);
    neverc_rpc_open_t open;
    memset(&open, 0, sizeof(open));
    open.metadata = stream->metadata;
    open.metadata_capacity = NEVERC_RPC_DEFAULT_MAX_METADATA_COUNT;
    if (neverc_rpc_open_decode(stream->open_payload,
                               frame->header.payload_length,
                               server->config.max_metadata_size,
                               &open) != 0) {
        neverc_thread_channel_free(stream->receive_queue);
        free(stream->metadata);
        free(stream->open_payload);
        free(stream);
        return -1;
    }
    stream->method = (char *)malloc(open.method_length + 1);
    if (!stream->method) {
        neverc_thread_channel_free(stream->receive_queue);
        free(stream->metadata);
        free(stream->open_payload);
        free(stream);
        return -1;
    }
    memcpy(stream->method, open.method, open.method_length);
    stream->method[open.method_length] = '\0';
    stream->metadata_count = open.metadata_count;
    stream->codec = open.codec;
    if (open.deadline_ms > 0) {
        stream->context = neverc_context_with_deadline_handle(
            server->serve_context, open.deadline_ms, &stream->cancel);
    } else {
        stream->context = neverc_context_with_cancel_handle(
            server->serve_context, &stream->cancel);
    }
    if (!stream->context || !stream->cancel) {
        if (stream->context) neverc_context_free(stream->context);
        if (stream->cancel)
            neverc_context_cancel_handle_free(stream->cancel);
        neverc_thread_channel_free(stream->receive_queue);
        free(stream->method);
        free(stream->metadata);
        free(stream->open_payload);
        free(stream);
        return -1;
    }
    nc_mutex_init(&stream->lock);
    rpc_method_t *method = rpc_server_find_method(
        server, stream->method, strlen(stream->method));
    if (method) {
        stream->handler = method->handler;
        stream->handler_context = method->context;
    }

    nc_mutex_lock(&connection->streams_lock);
    size_t capacity = server->config.max_streams_per_connection;
    size_t slot = capacity;
    if (!rpc_server_find_stream_locked(connection, stream->request_id)) {
        for (size_t i = 0; i < capacity; i++)
            if (!connection->streams[i]) {
                slot = i;
                break;
            }
    }
    if (slot == capacity) {
        nc_mutex_unlock(&connection->streams_lock);
        rpc_server_stream_destroy(stream);
        return -1;
    }
    connection->streams[slot] = stream;
    connection->stream_count++;
    if ((frame->header.flags & NEVERC_RPC_FLAG_END_STREAM) != 0) {
        stream->receive_closed = 1;
        neverc_thread_channel_close(stream->receive_queue);
    }
    nc_mutex_unlock(&connection->streams_lock);

    if (!method || neverc_context_done(stream->context)) {
        neverc_rpc_status_code_t status = !method
            ? NEVERC_RPC_STATUS_NOT_FOUND
            : NEVERC_RPC_STATUS_DEADLINE_EXCEEDED;
        (void)rpc_server_stream_end_internal(
            stream, status,
            !method ? "RPC method not found" : "RPC deadline expired",
            NULL);
        rpc_server_stream_destroy(stream);
        return 0;
    }

    nc_mutex_lock(&connection->handlers_lock);
    connection->active_handlers++;
    nc_mutex_unlock(&connection->handlers_lock);
    int submitted = neverc_thread_executor_try_submit(
        server->handler_executor, rpc_server_handler_task, stream);
    if (submitted != NEVERC_THREAD_OK) {
        nc_mutex_lock(&connection->handlers_lock);
        connection->active_handlers--;
        if (connection->active_handlers == 0)
            nc_cond_broadcast(&connection->handlers_done);
        nc_mutex_unlock(&connection->handlers_lock);
        (void)rpc_server_stream_end_internal(
            stream, NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED,
            "RPC handler queue is full", NULL);
        rpc_server_stream_destroy(stream);
    }
    return 0;
}

static int rpc_server_dispatch_request_frame(
    rpc_server_connection_t *connection, const neverc_rpc_frame_t *frame) {
    if ((frame->header.flags & NEVERC_RPC_FLAG_RESPONSE) != 0) return -1;
    if (frame->header.type == NEVERC_RPC_FRAME_OPEN)
        return rpc_server_dispatch_open(connection, frame);
    if (frame->header.type != NEVERC_RPC_FRAME_DATA &&
        frame->header.type != NEVERC_RPC_FRAME_CANCEL)
        return -1;

    int terminal_only =
        frame->header.type == NEVERC_RPC_FRAME_DATA &&
        frame->header.payload_length == 0 &&
        (frame->header.flags & NEVERC_RPC_FLAG_END_STREAM) != 0;
    rpc_server_inbound_t *inbound = NULL;
    if (frame->header.type == NEVERC_RPC_FRAME_DATA && !terminal_only) {
        inbound = (rpc_server_inbound_t *)malloc(
            sizeof(*inbound) + frame->header.payload_length);
        if (!inbound) return -1;
        inbound->header = frame->header;
        if (frame->header.payload_length > 0)
            memcpy(inbound->payload, frame->payload,
                   frame->header.payload_length);
    }

    nc_mutex_lock(&connection->streams_lock);
    neverc_rpc_server_stream_t *stream = rpc_server_find_stream_locked(
        connection, frame->header.request_id);
    if (!stream) {
        nc_mutex_unlock(&connection->streams_lock);
        free(inbound);
        return 0;
    }
    nc_mutex_lock(&stream->lock);
    int receive_closed = stream->receive_closed;
    nc_mutex_unlock(&stream->lock);
    if (receive_closed && frame->header.type == NEVERC_RPC_FRAME_DATA) {
        free(inbound);
        /* Hold streams_lock across END so destroy cannot free `stream`
         * after we drop the table lock. DATA after END is a stream error,
         * not a connection kill, so a bidi handler can still finish. */
        (void)rpc_server_stream_end_internal(
            stream, NEVERC_RPC_STATUS_DATA_LOSS,
            "RPC DATA after end of stream", NULL);
        nc_mutex_unlock(&connection->streams_lock);
        return 0;
    }
    if (frame->header.type == NEVERC_RPC_FRAME_CANCEL) {
        nc_mutex_lock(&stream->lock);
        stream->peer_cancelled = 1;
        stream->receive_closed = 1;
        neverc_context_cancel_handle_cancel(stream->cancel);
        neverc_thread_channel_close(stream->receive_queue);
        nc_mutex_unlock(&stream->lock);
    } else {
        if (!terminal_only) {
            int queued = neverc_thread_channel_try_send(
                stream->receive_queue, inbound);
            if (queued != NEVERC_THREAD_OK) {
                free(inbound);
                inbound = NULL;
                if (queued == NEVERC_THREAD_WOULD_BLOCK) {
                    (void)rpc_server_stream_end_internal(
                        stream, NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED,
                        "RPC request receive queue is full", NULL);
                    neverc_context_cancel_handle_cancel(stream->cancel);
                    neverc_thread_channel_close(stream->receive_queue);
                }
            }
        }
        if ((frame->header.flags & NEVERC_RPC_FLAG_END_STREAM) != 0) {
            nc_mutex_lock(&stream->lock);
            stream->receive_closed = 1;
            neverc_thread_channel_close(stream->receive_queue);
            nc_mutex_unlock(&stream->lock);
        }
    }
    nc_mutex_unlock(&connection->streams_lock);
    return 0;
}

static int rpc_server_dispatch_frame(rpc_server_connection_t *connection,
                                     const neverc_rpc_frame_t *frame) {
    if (frame->header.type == NEVERC_RPC_FRAME_PING) {
        neverc_rpc_frame_header_t header;
        memset(&header, 0, sizeof(header));
        header.version = NEVERC_RPC_VERSION_1;
        header.type = NEVERC_RPC_FRAME_PONG;
        header.payload_length = frame->header.payload_length;
        return rpc_server_queue_frame(connection, NULL, &header,
                                      frame->payload) == NEVERC_RPC_IO_OK
                   ? 0 : -1;
    }
    if (frame->header.type == NEVERC_RPC_FRAME_PONG) return 0;
    if (frame->header.type == NEVERC_RPC_FRAME_GOAWAY) return -1;
    return rpc_server_dispatch_request_frame(connection, frame);
}

static void rpc_server_remove_connection(
    rpc_server_connection_t *connection) {
    neverc_rpc_server_t *server = connection->server;
    nc_mutex_lock(&server->lifecycle_lock);
    if (connection->prev)
        connection->prev->next = connection->next;
    else if (server->connections == connection)
        server->connections = connection->next;
    if (connection->next) connection->next->prev = connection->prev;
    connection->next = NULL;
    connection->prev = NULL;
    nc_atomic_dec(&server->active_connections);
    nc_mutex_unlock(&server->lifecycle_lock);
}

static void rpc_server_connection_destroy(
    rpc_server_connection_t *connection) {
    if (connection->writer_started)
        nc_thread_join(connection->writer_thread);
    if (connection->send_queue) {
        for (;;) {
            void *value = NULL;
            int result = neverc_thread_channel_try_receive(
                connection->send_queue, &value);
            if (result != NEVERC_THREAD_OK) break;
            free(value);
        }
    }
    if (connection->tls) neverc_tls_close(connection->tls);
    if (connection->tcp) neverc_tcp_close(connection->tcp);
    if (connection->quic_stream)
        neverc_quic_stream_free(connection->quic_stream);
    if (connection->quic) neverc_quic_conn_free(connection->quic);
    if (connection->send_queue)
        neverc_thread_channel_free(connection->send_queue);
    nc_cond_destroy(&connection->handlers_done);
    nc_mutex_destroy(&connection->handlers_lock);
    nc_mutex_destroy(&connection->streams_lock);
    free(connection->streams);
    free(connection);
}

static void rpc_server_connection_task(void *arg) {
    rpc_server_connection_t *connection =
        (rpc_server_connection_t *)arg;
    if (connection->quic && !connection->quic_stream) {
        const char *stream_error = NULL;
        connection->quic_stream = neverc_quic_accept_stream(
            connection->quic, &stream_error);
        (void)stream_error;
        if (!connection->quic_stream) goto finished;
    }
    if (connection->tls_config) {
        const char *tls_error = NULL;
        connection->tls = neverc_tls_server(
            connection->tcp, connection->tls_config, &tls_error);
        (void)tls_error;
        if (!connection->tls) goto finished;
        const char *alpn = neverc_tls_alpn(connection->tls);
        if (!alpn || strcmp(alpn, "nrpc/1") != 0) goto finished;
    }
    if (nc_thread_create(&connection->writer_thread,
                         rpc_server_writer_main, connection) != 0)
        goto finished;
    connection->writer_started = 1;

    nc_buf_t input;
    nc_buf_init(&input);
    char chunk[16384];
    while (nc_atomic_load(&connection->running)) {
        int n = rpc_server_transport_read(connection, chunk, sizeof(chunk));
        if (n <= 0) break;
        size_t input_limit = connection->server->config.max_frame_size +
                             NEVERC_RPC_FRAME_HEADER_SIZE + sizeof(chunk);
        if (input.len > input_limit ||
            (size_t)n > input_limit - input.len ||
            nc_buf_append(&input, chunk, (size_t)n) != 0)
            break;
        while (input.len > 0) {
            neverc_rpc_frame_t frame;
            size_t consumed = 0;
            int decoded = neverc_rpc_frame_decode(
                input.data, input.len,
                connection->server->config.max_frame_size,
                &frame, &consumed);
            if (decoded == 0) break;
            if (decoded < 0 ||
                rpc_server_dispatch_frame(connection, &frame) != 0)
                goto reader_finished;
            nc_buf_consume(&input, consumed);
        }
    }

reader_finished:
    nc_buf_free(&input);
finished:
    rpc_server_stop_connection(connection);
    nc_mutex_lock(&connection->handlers_lock);
    while (connection->active_handlers > 0)
        nc_cond_wait(&connection->handlers_done,
                     &connection->handlers_lock);
    nc_mutex_unlock(&connection->handlers_lock);
    rpc_server_remove_connection(connection);
    rpc_server_connection_destroy(connection);
}

static rpc_server_connection_t *rpc_server_connection_create(
    neverc_rpc_server_t *server, neverc_tcp_conn_t *tcp,
    neverc_quic_conn_t *quic) {
    rpc_server_connection_t *connection =
        (rpc_server_connection_t *)calloc(1, sizeof(*connection));
    if (!connection) return NULL;
    connection->server = server;
    connection->tcp = tcp;
    connection->quic = quic;
    connection->tls_config = server->tls_config;
    connection->send_queue = neverc_thread_channel_create(
        server->config.send_queue_capacity);
    connection->streams = (neverc_rpc_server_stream_t **)calloc(
        server->config.max_streams_per_connection,
        sizeof(*connection->streams));
    if (!connection->send_queue || !connection->streams) {
        if (connection->send_queue)
            neverc_thread_channel_free(connection->send_queue);
        free(connection->streams);
        free(connection);
        return NULL;
    }
    nc_mutex_init(&connection->streams_lock);
    nc_mutex_init(&connection->handlers_lock);
    nc_cond_init(&connection->handlers_done);
    nc_atomic_store(&connection->running, 1);
    return connection;
}

neverc_rpc_server_t *neverc_rpc_server_new(
    const neverc_rpc_server_config_t *config) {
    neverc_rpc_server_config_t effective = config
        ? *config : neverc_rpc_server_config_default();
    if (!rpc_server_config_valid(&effective)) return NULL;
    neverc_rpc_server_t *server =
        (neverc_rpc_server_t *)calloc(1, sizeof(*server));
    if (!server) return NULL;
    server->config = effective;
    nc_mutex_init(&server->lifecycle_lock);
    nc_mutex_init(&server->tenant_lock);
    return server;
}

static int rpc_validate_method_name(const char *method) {
    if (!method) return 0;
    size_t length = strlen(method);
    if (length == 0 || length > UINT16_MAX ||
        length > SIZE_MAX - NEVERC_RPC_OPEN_HEADER_SIZE)
        return 0;
    uint8_t *buffer = (uint8_t *)malloc(
        length + NEVERC_RPC_OPEN_HEADER_SIZE);
    if (!buffer) return 0;
    neverc_rpc_open_t open;
    memset(&open, 0, sizeof(open));
    open.method = method;
    open.method_length = length;
    size_t encoded = 0;
    int valid = neverc_rpc_open_encode(
        &open, buffer, length + NEVERC_RPC_OPEN_HEADER_SIZE,
        &encoded) == 0;
    free(buffer);
    return valid;
}

int neverc_rpc_server_register(neverc_rpc_server_t *server,
                               const char *method,
                               neverc_rpc_handler_func_t handler,
                               void *handler_context) {
    if (!server || !handler || !rpc_validate_method_name(method))
        return NEVERC_RPC_IO_INVALID;
    nc_mutex_lock(&server->lifecycle_lock);
    if (nc_atomic_load(&server->serving) ||
        server->method_count == RPC_SERVER_MAX_METHODS) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return NEVERC_RPC_IO_CLOSED;
    }
    if (rpc_server_find_method(server, method, strlen(method))) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return NEVERC_RPC_IO_INVALID;
    }
    char *copy = strdup(method);
    if (!copy) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return NEVERC_RPC_IO_NOMEM;
    }
    rpc_method_t *entry = &server->methods[server->method_count++];
    entry->method = copy;
    entry->handler = handler;
    entry->context = handler_context;
    nc_mutex_unlock(&server->lifecycle_lock);
    return NEVERC_RPC_IO_OK;
}

int neverc_rpc_server_add_interceptor(
    neverc_rpc_server_t *server, neverc_rpc_interceptor_func_t interceptor,
    void *interceptor_context) {
    if (!server || !interceptor) return NEVERC_RPC_IO_INVALID;
    nc_mutex_lock(&server->lifecycle_lock);
    if (nc_atomic_load(&server->serving) ||
        server->interceptor_count == RPC_SERVER_MAX_INTERCEPTORS) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return NEVERC_RPC_IO_CLOSED;
    }
    rpc_interceptor_t *entry =
        &server->interceptors[server->interceptor_count++];
    entry->function = interceptor;
    entry->context = interceptor_context;
    nc_mutex_unlock(&server->lifecycle_lock);
    return NEVERC_RPC_IO_OK;
}

static int rpc_server_set_auth_hook(
    neverc_rpc_server_t *server, rpc_interceptor_t *destination,
    int *enabled, neverc_rpc_interceptor_func_t function, void *context) {
    if (!server || !function) return NEVERC_RPC_IO_INVALID;
    nc_mutex_lock(&server->lifecycle_lock);
    if (nc_atomic_load(&server->serving)) {
        nc_mutex_unlock(&server->lifecycle_lock);
        return NEVERC_RPC_IO_CLOSED;
    }
    destination->function = function;
    destination->context = context;
    *enabled = 1;
    nc_mutex_unlock(&server->lifecycle_lock);
    return NEVERC_RPC_IO_OK;
}

int neverc_rpc_server_set_authenticator(
    neverc_rpc_server_t *server, neverc_rpc_interceptor_func_t authenticator,
    void *authenticator_context) {
    return rpc_server_set_auth_hook(server, server ? &server->authenticator
                                                    : NULL,
                                    server ? &server->has_authenticator : NULL,
                                    authenticator, authenticator_context);
}

int neverc_rpc_server_set_authorizer(
    neverc_rpc_server_t *server, neverc_rpc_interceptor_func_t authorizer,
    void *authorizer_context) {
    return rpc_server_set_auth_hook(server, server ? &server->authorizer
                                                    : NULL,
                                    server ? &server->has_authorizer : NULL,
                                    authorizer, authorizer_context);
}

int neverc_rpc_server_set_tenant_rate_limit(
    neverc_rpc_server_t *server, uint32_t requests_per_second,
    uint32_t burst, size_t max_tenants,
    neverc_rpc_tenant_key_func_t tenant_key, void *tenant_context) {
    if (!server || !tenant_key || requests_per_second == 0 || burst == 0 ||
        max_tenants == 0 ||
        max_tenants > SIZE_MAX / sizeof(rpc_tenant_bucket_t))
        return NEVERC_RPC_IO_INVALID;
    rpc_tenant_bucket_t *buckets = (rpc_tenant_bucket_t *)calloc(
        max_tenants, sizeof(*buckets));
    if (!buckets) return NEVERC_RPC_IO_NOMEM;
    nc_mutex_lock(&server->lifecycle_lock);
    if (nc_atomic_load(&server->serving)) {
        nc_mutex_unlock(&server->lifecycle_lock);
        free(buckets);
        return NEVERC_RPC_IO_CLOSED;
    }
    free(server->tenant_buckets);
    server->tenant_buckets = buckets;
    server->max_tenants = max_tenants;
    server->tenant_requests_per_second = requests_per_second;
    server->tenant_burst = burst;
    server->tenant_key = tenant_key;
    server->tenant_context = tenant_context;
    nc_mutex_unlock(&server->lifecycle_lock);
    return NEVERC_RPC_IO_OK;
}

void neverc_rpc_server_shutdown(neverc_rpc_server_t *server) {
    if (!server) return;
    nc_atomic_store(&server->stop_requested, 1);
    nc_atomic_store(&server->running, 0);
    nc_atomic_store(&server->bound_port, 0);
    nc_mutex_lock(&server->lifecycle_lock);
    neverc_quic_endpoint_t *quic_endpoint = server->quic_endpoint;
    server->quic_endpoint = NULL;
    if (server->serve_cancel)
        neverc_context_cancel_handle_cancel(server->serve_cancel);
    for (rpc_server_connection_t *connection = server->connections;
         connection; connection = connection->next)
        rpc_server_stop_connection(connection);
    nc_mutex_unlock(&server->lifecycle_lock);
    if (quic_endpoint) neverc_quic_endpoint_close(quic_endpoint);
}

static int rpc_server_serve(neverc_rpc_server_t *server, const char *addr,
                            const char *cert_file, const char *key_file,
                            const char *client_ca_file, int use_quic) {
    if (!server || !addr || ((cert_file == NULL) != (key_file == NULL)))
        return -1;
    if ((client_ca_file && !cert_file) ||
        (use_quic && (!cert_file || !key_file || client_ca_file)))
        return -1;
    nc_atomic_store(&server->stop_requested, 0);
    if (!nc_atomic_cas(&server->serving, 0, 1)) return -1;
    int result = -1;
    neverc_context_t *serve_background = NULL;

    serve_background = neverc_context_background();
    if (!serve_background) goto cleanup;
    server->serve_context = neverc_context_with_cancel_handle(
        serve_background, &server->serve_cancel);
    if (!server->serve_context || !server->serve_cancel) goto cleanup;
    server->connection_executor = neverc_thread_executor_create(
        server->config.connection_workers,
        server->config.connection_queue_capacity);
    server->handler_executor = neverc_thread_executor_create(
        server->config.handler_workers,
        server->config.handler_queue_capacity);
    if (!server->connection_executor || !server->handler_executor)
        goto cleanup;

    if (cert_file && !use_quic) {
        server->tls_config = neverc_tls_config_new();
        if (!server->tls_config ||
            neverc_tls_config_load_cert(server->tls_config, cert_file,
                                        key_file) != 0 ||
            (client_ca_file &&
             (neverc_tls_config_add_root_certificates(
                  server->tls_config, client_ca_file) != 0 ||
              neverc_tls_config_set_client_auth(
                  server->tls_config,
                  NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY) != 0)))
            goto cleanup;
        const char *alpn[] = { "nrpc/1" };
        neverc_tls_config_set_alpn(server->tls_config, alpn, 1);
    }
    const char *listen_error = NULL;
    neverc_quic_endpoint_t *quic_endpoint = NULL;
    int bound_port = 0;
    if (use_quic) {
        const char *alpn[] = { "nrpc/1", NULL };
        neverc_quic_config_t quic_config = neverc_quic_config_default();
        quic_config.cert_file = cert_file;
        quic_config.key_file = key_file;
        quic_config.alpn = alpn;
        quic_config.max_idle_timeout_ms =
            (uint64_t)server->config.io_timeout_ms;
        quic_endpoint = neverc_quic_listen(addr, &quic_config,
                                            &listen_error);
        if (!quic_endpoint) goto cleanup;
        nc_mutex_lock(&server->lifecycle_lock);
        server->quic_endpoint = quic_endpoint;
        nc_mutex_unlock(&server->lifecycle_lock);
        bound_port = neverc_quic_endpoint_bound_port(quic_endpoint);
        if (bound_port <= 0) goto cleanup;
    } else {
        server->listener = neverc_tcp_listen(addr, &listen_error);
        if (!server->listener) goto cleanup;
        neverc_tcp_addr_t local_addr;
        if (neverc_tcp_listener_addr(server->listener, &local_addr) != 0)
            goto cleanup;
        bound_port = local_addr.port;
        if (bound_port <= 0) goto cleanup;
    }
    if (nc_atomic_load(&server->stop_requested)) {
        result = 0;
        goto cleanup;
    }
    /* Publish running before bound_port so waiters that see a port also
     * observe an accept loop that is about to run. listen()/quic_listen()
     * already bound the socket; Darwin still refuses a connect() that
     * races the first accept, which tests handle with a ready-dial wait. */
    nc_atomic_store(&server->running, 1);
    nc_atomic_store(&server->bound_port, bound_port);
    if (nc_atomic_load(&server->stop_requested))
        neverc_rpc_server_shutdown(server);

    while (nc_atomic_load(&server->running)) {
        neverc_tcp_conn_t *tcp = NULL;
        neverc_quic_conn_t *quic = NULL;
        if (use_quic) {
            quic = neverc_quic_accept(quic_endpoint, &listen_error);
            if (!quic) {
                if (!nc_atomic_load(&server->running)) break;
                result = -1;
                goto cleanup;
            }
        } else {
            neverc_net_result_t accepted = neverc_tcp_accept_context(
                server->listener, server->serve_context, &tcp);
            if (accepted.status != NEVERC_NET_OK || !tcp) {
                if (neverc_context_done(server->serve_context) ||
                    !nc_atomic_load(&server->running))
                    break;
                result = -1;
                goto cleanup;
            }
        }
        if ((size_t)nc_atomic_load(&server->active_connections) >=
            server->config.max_connections) {
            if (quic) neverc_quic_conn_free(quic);
            if (tcp) neverc_tcp_close(tcp);
            continue;
        }
        if (tcp)
            (void)neverc_tcp_set_timeout(tcp,
                                         server->config.io_timeout_ms);
        rpc_server_connection_t *connection =
            rpc_server_connection_create(server, tcp, quic);
        if (!connection) {
            if (quic) neverc_quic_conn_free(quic);
            if (tcp) neverc_tcp_close(tcp);
            continue;
        }

        nc_mutex_lock(&server->lifecycle_lock);
        if (!nc_atomic_load(&server->running)) {
            nc_mutex_unlock(&server->lifecycle_lock);
            rpc_server_stop_connection(connection);
            rpc_server_connection_destroy(connection);
            break;
        }
        connection->next = server->connections;
        if (server->connections) server->connections->prev = connection;
        server->connections = connection;
        nc_atomic_inc(&server->active_connections);
        nc_mutex_unlock(&server->lifecycle_lock);

        int submitted = neverc_thread_executor_try_submit(
            server->connection_executor, rpc_server_connection_task,
            connection);
        if (submitted != NEVERC_THREAD_OK) {
            rpc_server_stop_connection(connection);
            rpc_server_remove_connection(connection);
            rpc_server_connection_destroy(connection);
        }
    }
    result = 0;

cleanup:
    neverc_rpc_server_shutdown(server);
    if (server->connection_executor) {
        (void)neverc_thread_executor_shutdown(
            server->connection_executor);
        neverc_thread_executor_free(server->connection_executor);
        server->connection_executor = NULL;
    }
    if (server->handler_executor) {
        (void)neverc_thread_executor_shutdown(server->handler_executor);
        neverc_thread_executor_free(server->handler_executor);
        server->handler_executor = NULL;
    }
    if (server->listener) {
        neverc_tcp_listener_close(server->listener);
        server->listener = NULL;
    }
    neverc_tls_config_free(server->tls_config);
    server->tls_config = NULL;
    if (server->serve_cancel) {
        neverc_context_cancel_handle_cancel(server->serve_cancel);
        neverc_context_cancel_handle_free(server->serve_cancel);
        server->serve_cancel = NULL;
    }
    if (server->serve_context) {
        neverc_context_free(server->serve_context);
        server->serve_context = NULL;
    }
    neverc_context_free(serve_background);
    nc_atomic_store(&server->running, 0);
    nc_atomic_store(&server->serving, 0);
    return result;
}

int neverc_rpc_server_listen_and_serve(neverc_rpc_server_t *server,
                                       const char *addr) {
    return rpc_server_serve(server, addr, NULL, NULL, NULL, 0);
}

int neverc_rpc_server_listen_and_serve_tls(
    neverc_rpc_server_t *server, const char *addr,
    const char *cert_file, const char *key_file) {
    if (!cert_file || !key_file) return -1;
    return rpc_server_serve(server, addr, cert_file, key_file, NULL, 0);
}

int neverc_rpc_server_listen_and_serve_mtls(
    neverc_rpc_server_t *server, const char *addr,
    const char *cert_file, const char *key_file,
    const char *client_ca_file) {
    if (!cert_file || !key_file || !client_ca_file) return -1;
    return rpc_server_serve(server, addr, cert_file, key_file,
                            client_ca_file, 0);
}

int neverc_rpc_server_listen_and_serve_quic(
    neverc_rpc_server_t *server, const char *addr,
    const char *cert_file, const char *key_file) {
    if (!cert_file || !key_file) return -1;
    return rpc_server_serve(server, addr, cert_file, key_file, NULL, 1);
}

size_t neverc_rpc_server_active_connections(neverc_rpc_server_t *server) {
    return server ? (size_t)nc_atomic_load(&server->active_connections) : 0;
}

int neverc_rpc_server_is_running(neverc_rpc_server_t *server) {
    return server ? nc_atomic_load(&server->running) : 0;
}

int neverc_rpc_server_bound_port(neverc_rpc_server_t *server) {
    return server ? nc_atomic_load(&server->bound_port) : 0;
}

void neverc_rpc_server_free(neverc_rpc_server_t *server) {
    if (!server || nc_atomic_load(&server->serving)) return;
    for (size_t i = 0; i < server->method_count; i++)
        free(server->methods[i].method);
    free(server->tenant_buckets);
    nc_mutex_destroy(&server->tenant_lock);
    nc_mutex_destroy(&server->lifecycle_lock);
    free(server);
}
