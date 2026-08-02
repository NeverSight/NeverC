#ifndef NEVERC_NET_RPC_H
#define NEVERC_NET_RPC_H

/*
 * NeverC native RPC protocol.
 *
 * NRPC v1 is a multiplexed, length-prefixed protocol. Every frame has a
 * fixed-size network-byte-order header followed by a bounded payload. OPEN
 * frames carry method, metadata, and an absolute Unix-millisecond deadline;
 * DATA, END, and CANCEL frames share the request ID.
 */

#include "neverc/std/context.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_RPC_MAGIC 0x4e525043U /* "NRPC" */
#define NEVERC_RPC_VERSION_1 1
#define NEVERC_RPC_FRAME_HEADER_SIZE 24
#define NEVERC_RPC_OPEN_HEADER_SIZE 16
#define NEVERC_RPC_DEFAULT_MAX_FRAME_SIZE (4U * 1024U * 1024U)
#define NEVERC_RPC_DEFAULT_MAX_METADATA_SIZE (64U * 1024U)
#define NEVERC_RPC_DEFAULT_MAX_METADATA_COUNT 128

enum {
    NEVERC_RPC_FRAME_OPEN = 1,
    NEVERC_RPC_FRAME_DATA = 2,
    NEVERC_RPC_FRAME_END = 3,
    NEVERC_RPC_FRAME_CANCEL = 4,
    NEVERC_RPC_FRAME_PING = 5,
    NEVERC_RPC_FRAME_PONG = 6,
    NEVERC_RPC_FRAME_GOAWAY = 7
};

enum {
    NEVERC_RPC_FLAG_END_STREAM = 0x0001,
    NEVERC_RPC_FLAG_RESPONSE = 0x0002,
    NEVERC_RPC_FLAG_IDEMPOTENT = 0x0004
};

typedef enum {
    NEVERC_RPC_STATUS_OK = 0,
    NEVERC_RPC_STATUS_CANCELLED = 1,
    NEVERC_RPC_STATUS_UNKNOWN = 2,
    NEVERC_RPC_STATUS_INVALID_ARGUMENT = 3,
    NEVERC_RPC_STATUS_DEADLINE_EXCEEDED = 4,
    NEVERC_RPC_STATUS_NOT_FOUND = 5,
    NEVERC_RPC_STATUS_ALREADY_EXISTS = 6,
    NEVERC_RPC_STATUS_PERMISSION_DENIED = 7,
    NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED = 8,
    NEVERC_RPC_STATUS_FAILED_PRECONDITION = 9,
    NEVERC_RPC_STATUS_ABORTED = 10,
    NEVERC_RPC_STATUS_OUT_OF_RANGE = 11,
    NEVERC_RPC_STATUS_UNIMPLEMENTED = 12,
    NEVERC_RPC_STATUS_INTERNAL = 13,
    NEVERC_RPC_STATUS_UNAVAILABLE = 14,
    NEVERC_RPC_STATUS_DATA_LOSS = 15,
    NEVERC_RPC_STATUS_UNAUTHENTICATED = 16
} neverc_rpc_status_code_t;

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t payload_length;
    uint64_t request_id;
    uint32_t code;
} neverc_rpc_frame_header_t;

typedef struct {
    neverc_rpc_frame_header_t header;
    const uint8_t *payload; /* view into the caller-owned input buffer */
} neverc_rpc_frame_t;

typedef struct {
    const char *key;
    size_t key_length;
    const uint8_t *value;
    size_t value_length;
} neverc_rpc_metadata_t;

typedef enum {
    NEVERC_RPC_CODEC_RAW = 0,
    NEVERC_RPC_CODEC_JSON = 1,
    NEVERC_RPC_CODEC_PROTOBUF = 2 /* implemented in stage 7 */
} neverc_rpc_codec_t;

typedef struct {
    const char *method;
    size_t method_length;
    int64_t deadline_ms; /* absolute Unix milliseconds; 0 means none */
    neverc_rpc_codec_t codec;
    neverc_rpc_metadata_t *metadata;
    size_t metadata_capacity;
    size_t metadata_count;
} neverc_rpc_open_t;

/* Encode/decode a complete frame. Decode returns 1 for a complete frame, 0
 * when more bytes are required, and -1 for malformed or oversized input. */
int neverc_rpc_frame_encode(const neverc_rpc_frame_t *frame,
                            void *output, size_t output_capacity,
                            size_t *output_length);
int neverc_rpc_frame_decode(const void *input, size_t input_length,
                            size_t max_payload_size,
                            neverc_rpc_frame_t *frame, size_t *consumed);

/* Encode/decode an OPEN payload. Method names use slash-separated token
 * segments (for example "game.Session/Join"). Metadata keys are lowercase
 * HTTP-token-like identifiers. Decode returns non-owning views into input. */
int neverc_rpc_open_encode(const neverc_rpc_open_t *open,
                           void *output, size_t output_capacity,
                           size_t *output_length);
int neverc_rpc_open_decode(const void *input, size_t input_length,
                           size_t max_metadata_size,
                           neverc_rpc_open_t *open);

/* Validate status codes and return their stable protocol name. */
int neverc_rpc_status_code_valid(uint32_t code);
const char *neverc_rpc_status_name(uint32_t code);

/* ======================================================================
 * Multiplexed client
 * ====================================================================== */

typedef struct neverc_rpc_client neverc_rpc_client_t;
typedef struct neverc_rpc_stream neverc_rpc_stream_t;

typedef struct {
    size_t max_frame_size;
    size_t max_metadata_size;
    size_t max_inflight_streams;
    size_t send_queue_capacity;
    size_t receive_queue_capacity;
    int connect_timeout_ms;
    int io_timeout_ms;
    int ping_interval_ms; /* 0 disables active keepalive */
    int pong_timeout_ms;  /* required when ping_interval_ms is nonzero */
    int reconnect_enabled;
    int reconnect_backoff_ms;
    int use_tls;
    int use_quic; /* mutually exclusive with use_tls; QUIC always uses TLS */
    const char *server_name; /* required for TLS when addr is not a DNS name */
    const char *root_ca_file; /* optional custom PEM roots */
    const char *client_cert_file; /* optional mTLS certificate PEM */
    const char *client_key_file;  /* required with client_cert_file */
} neverc_rpc_client_config_t;

typedef struct {
    neverc_rpc_status_code_t code;
    const char *message; /* stream-owned; valid until stream_free */
} neverc_rpc_status_t;

typedef struct {
    neverc_rpc_codec_t codec;
    int idempotent;
    size_t max_attempts; /* retries require idempotent; default 1 */
} neverc_rpc_call_options_t;

enum {
    NEVERC_RPC_IO_OK = 0,
    NEVERC_RPC_IO_END = 1,
    NEVERC_RPC_IO_CANCELLED = 2,
    NEVERC_RPC_IO_WOULD_BLOCK = 3,
    NEVERC_RPC_IO_INVALID = -1,
    NEVERC_RPC_IO_NOMEM = -2,
    NEVERC_RPC_IO_CLOSED = -3,
    NEVERC_RPC_IO_PROTOCOL = -4
};

neverc_rpc_client_config_t neverc_rpc_client_config_default(void);
neverc_rpc_call_options_t neverc_rpc_call_options_default(void);

/* Dial one multiplexed TCP, verified TLS, or verified QUIC connection and
 * start its bounded reader/writer pumps. The config strings are copied or
 * consumed during dial. */
neverc_rpc_client_t *neverc_rpc_client_dial(
    const char *addr, const neverc_rpc_client_config_t *config,
    const char **errp);

/* Re-establish a failed transport. Existing stream handles remain terminal
 * and belong to their old connection generation. */
int neverc_rpc_client_reconnect(neverc_rpc_client_t *client,
                                neverc_context_t *context,
                                const char **errp);

/* Stop pumps, cancel active streams, close the transport, and release client.
 * The caller must free all stream handles before closing the client. */
void neverc_rpc_client_close(neverc_rpc_client_t *client);

/* Open a request stream. metadata is copied into the OPEN frame. */
neverc_rpc_stream_t *neverc_rpc_stream_open(
    neverc_rpc_client_t *client, neverc_context_t *context,
    const char *method, const neverc_rpc_metadata_t *metadata,
    size_t metadata_count, int idempotent, const char **errp);
neverc_rpc_stream_t *neverc_rpc_stream_open_codec(
    neverc_rpc_client_t *client, neverc_context_t *context,
    const char *method, neverc_rpc_codec_t codec,
    const neverc_rpc_metadata_t *metadata, size_t metadata_count,
    int idempotent, const char **errp);

/* DATA send is split at max_frame_size and blocks on bounded connection
 * backpressure. close_send marks the final DATA frame END_STREAM. */
int neverc_rpc_stream_send(neverc_rpc_stream_t *stream,
                           neverc_context_t *context,
                           const void *data, size_t len);
int neverc_rpc_stream_close_send(neverc_rpc_stream_t *stream,
                                 neverc_context_t *context);

/* Receive one response DATA frame. Returns NEVERC_RPC_IO_END after END and
 * stores the final status; buflen must fit the complete next frame. */
int neverc_rpc_stream_recv(neverc_rpc_stream_t *stream,
                           neverc_context_t *context,
                           void *buf, size_t buflen, size_t *out_len);

int neverc_rpc_stream_cancel(neverc_rpc_stream_t *stream,
                             neverc_rpc_status_code_t code,
                             const char *message);
neverc_rpc_status_t neverc_rpc_stream_status(neverc_rpc_stream_t *stream);
uint64_t neverc_rpc_stream_id(neverc_rpc_stream_t *stream);
void neverc_rpc_stream_free(neverc_rpc_stream_t *stream);

/* Unary raw/JSON call primitive. The request is sent as one logical message;
 * the response must fit response_capacity. */
int neverc_rpc_client_call(
    neverc_rpc_client_t *client, neverc_context_t *context,
    const char *method, const neverc_rpc_metadata_t *metadata,
    size_t metadata_count, const void *request, size_t request_length,
    void *response, size_t response_capacity, size_t *response_length,
    neverc_rpc_status_t *status);
int neverc_rpc_client_call_ex(
    neverc_rpc_client_t *client, neverc_context_t *context,
    const char *method, const neverc_rpc_metadata_t *metadata,
    size_t metadata_count, const void *request, size_t request_length,
    void *response, size_t response_capacity, size_t *response_length,
    neverc_rpc_status_t *status,
    const neverc_rpc_call_options_t *options);

/* ======================================================================
 * Multiplexed server
 * ====================================================================== */

typedef struct neverc_rpc_server neverc_rpc_server_t;
typedef struct neverc_rpc_server_stream neverc_rpc_server_stream_t;

typedef struct {
    size_t max_frame_size;
    size_t max_metadata_size;
    size_t max_streams_per_connection;
    size_t send_queue_capacity;
    size_t receive_queue_capacity;
    size_t connection_workers;
    size_t connection_queue_capacity;
    size_t handler_workers;
    size_t handler_queue_capacity;
    size_t max_connections;
    int io_timeout_ms;
} neverc_rpc_server_config_t;

typedef void (*neverc_rpc_handler_func_t)(neverc_rpc_server_stream_t *stream,
                                          void *handler_context);

/* Return OK to continue or a non-OK status to reject before the handler.
 * Interceptors run in registration order on the handler executor. */
typedef neverc_rpc_status_code_t (*neverc_rpc_interceptor_func_t)(
    neverc_rpc_server_stream_t *stream, void *interceptor_context);

/* Copy a stable, authenticated tenant key into output and return its length.
 * Returning 0 rejects the request as unauthenticated. Keys are opaque bytes. */
typedef size_t (*neverc_rpc_tenant_key_func_t)(
    neverc_rpc_server_stream_t *stream, void *tenant_context,
    void *output, size_t output_capacity);

neverc_rpc_server_config_t neverc_rpc_server_config_default(void);
neverc_rpc_server_t *neverc_rpc_server_new(
    const neverc_rpc_server_config_t *config);

/* Registration is only allowed before serving starts. Names are copied. */
int neverc_rpc_server_register(neverc_rpc_server_t *server,
                               const char *method,
                               neverc_rpc_handler_func_t handler,
                               void *handler_context);
int neverc_rpc_server_add_interceptor(
    neverc_rpc_server_t *server, neverc_rpc_interceptor_func_t interceptor,
    void *interceptor_context);

/* Authentication and authorization run before custom interceptors. */
int neverc_rpc_server_set_authenticator(
    neverc_rpc_server_t *server, neverc_rpc_interceptor_func_t authenticator,
    void *authenticator_context);
int neverc_rpc_server_set_authorizer(
    neverc_rpc_server_t *server, neverc_rpc_interceptor_func_t authorizer,
    void *authorizer_context);

/* Enable a bounded per-tenant token bucket. Configuration and the tenant-key
 * callback are immutable after serving starts. */
int neverc_rpc_server_set_tenant_rate_limit(
    neverc_rpc_server_t *server, uint32_t requests_per_second,
    uint32_t burst, size_t max_tenants,
    neverc_rpc_tenant_key_func_t tenant_key, void *tenant_context);

int neverc_rpc_server_listen_and_serve(neverc_rpc_server_t *server,
                                       const char *addr);
int neverc_rpc_server_listen_and_serve_tls(
    neverc_rpc_server_t *server, const char *addr,
    const char *cert_file, const char *key_file);
/* Serve NRPC over TLS 1.3 and require a client certificate chaining to one of
 * the PEM roots in client_ca_file. */
int neverc_rpc_server_listen_and_serve_mtls(
    neverc_rpc_server_t *server, const char *addr,
    const char *cert_file, const char *key_file,
    const char *client_ca_file);
/* Serve one multiplexed NRPC connection per verified QUIC stream. */
int neverc_rpc_server_listen_and_serve_quic(
    neverc_rpc_server_t *server, const char *addr,
    const char *cert_file, const char *key_file);
void neverc_rpc_server_shutdown(neverc_rpc_server_t *server);
size_t neverc_rpc_server_active_connections(neverc_rpc_server_t *server);
int neverc_rpc_server_bound_port(neverc_rpc_server_t *server);
void neverc_rpc_server_free(neverc_rpc_server_t *server);

/* Handler-side stream access. Method and metadata are stream-owned views. */
uint64_t neverc_rpc_server_stream_id(neverc_rpc_server_stream_t *stream);
const char *neverc_rpc_server_stream_method(
    neverc_rpc_server_stream_t *stream);
neverc_context_t *neverc_rpc_server_stream_context(
    neverc_rpc_server_stream_t *stream);
const neverc_rpc_metadata_t *neverc_rpc_server_stream_metadata(
    neverc_rpc_server_stream_t *stream, size_t *count);
neverc_rpc_codec_t neverc_rpc_server_stream_codec(
    neverc_rpc_server_stream_t *stream);
/* Return the DER leaf certificate authenticated by the TLS transport. The
 * view remains valid for the lifetime of the server stream. */
const uint8_t *neverc_rpc_server_stream_peer_certificate(
    neverc_rpc_server_stream_t *stream, size_t *out_len);
int neverc_rpc_server_stream_recv(
    neverc_rpc_server_stream_t *stream, void *buf, size_t buflen,
    size_t *out_len);
int neverc_rpc_server_stream_send(
    neverc_rpc_server_stream_t *stream, const void *data, size_t len);
int neverc_rpc_server_stream_end(
    neverc_rpc_server_stream_t *stream, neverc_rpc_status_code_t code,
    const char *message);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_RPC_H */
