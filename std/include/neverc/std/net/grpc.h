#ifndef NEVERC_NET_GRPC_H
#define NEVERC_NET_GRPC_H

#include <stddef.h>
#include <stdint.h>

#include "neverc/std/context.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/http/http2.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_GRPC_OK = 0,
    NEVERC_GRPC_CANCELLED = 1,
    NEVERC_GRPC_UNKNOWN = 2,
    NEVERC_GRPC_INVALID_ARGUMENT = 3,
    NEVERC_GRPC_DEADLINE_EXCEEDED = 4,
    NEVERC_GRPC_NOT_FOUND = 5,
    NEVERC_GRPC_ALREADY_EXISTS = 6,
    NEVERC_GRPC_PERMISSION_DENIED = 7,
    NEVERC_GRPC_RESOURCE_EXHAUSTED = 8,
    NEVERC_GRPC_FAILED_PRECONDITION = 9,
    NEVERC_GRPC_ABORTED = 10,
    NEVERC_GRPC_OUT_OF_RANGE = 11,
    NEVERC_GRPC_UNIMPLEMENTED = 12,
    NEVERC_GRPC_INTERNAL = 13,
    NEVERC_GRPC_UNAVAILABLE = 14,
    NEVERC_GRPC_DATA_LOSS = 15,
    NEVERC_GRPC_UNAUTHENTICATED = 16
} neverc_grpc_status_t;

typedef enum {
    NEVERC_GRPC_UNARY,
    NEVERC_GRPC_SERVER_STREAMING,
    NEVERC_GRPC_CLIENT_STREAMING,
    NEVERC_GRPC_BIDI_STREAMING
} neverc_grpc_cardinality_t;

typedef struct {
    const uint8_t *data;
    size_t length;
} neverc_grpc_message_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    size_t max_message_size;
} neverc_grpc_frame_reader_t;

void neverc_grpc_frame_reader_init(neverc_grpc_frame_reader_t *reader,
                                    const void *data, size_t length,
                                    size_t max_message_size);
/* Return 1 for a message, 0 at end, and -1 for malformed/compressed input. */
int neverc_grpc_frame_reader_next(neverc_grpc_frame_reader_t *reader,
                                   neverc_grpc_message_t *message);
int neverc_grpc_frame_encode(const void *message, size_t message_length,
                              int compressed, void *output,
                              size_t output_capacity, size_t *output_length);

int neverc_grpc_timeout_encode(uint64_t timeout_ns, char output[10]);
int neverc_grpc_timeout_decode(const char *value, int64_t *timeout_ms);
int neverc_grpc_status_valid(uint32_t status);
const char *neverc_grpc_status_name(neverc_grpc_status_t status);

typedef struct neverc_grpc_server_stream neverc_grpc_server_stream_t;
typedef neverc_grpc_status_t (*neverc_grpc_handler_func_t)(
    neverc_grpc_server_stream_t *stream, void *context);

typedef struct {
    const char *full_method; /* /package.Service/Method */
    neverc_grpc_cardinality_t cardinality;
    size_t max_request_message_size;
    size_t max_response_message_size;
    neverc_grpc_handler_func_t handler;
    void *context;
} neverc_grpc_method_t;

/* The descriptor is borrowed and must outlive the mux. */
int neverc_grpc_server_register(neverc_http_mux_t *mux,
                                 const neverc_grpc_method_t *method);
neverc_context_t *neverc_grpc_server_stream_context(
    neverc_grpc_server_stream_t *stream);
const char *neverc_grpc_server_stream_metadata(
    neverc_grpc_server_stream_t *stream, const char *key);
int neverc_grpc_server_stream_recv(neverc_grpc_server_stream_t *stream,
                                    neverc_grpc_message_t *message);
int neverc_grpc_server_stream_send(neverc_grpc_server_stream_t *stream,
                                    const void *message,
                                    size_t message_length);
void neverc_grpc_server_stream_set_header(
    neverc_grpc_server_stream_t *stream, const char *name,
    const char *value);
void neverc_grpc_server_stream_set_trailer(
    neverc_grpc_server_stream_t *stream, const char *name,
    const char *value);
int neverc_grpc_server_stream_end(neverc_grpc_server_stream_t *stream,
                                   neverc_grpc_status_t status,
                                   const char *message);

typedef struct {
    const char *key;
    const uint8_t *value;
    size_t value_length;
} neverc_grpc_metadata_t;

typedef struct {
    neverc_grpc_status_t status;
    char *status_message;
    neverc_grpc_message_t *messages;
    size_t message_count;
    neverc_hpack_header_t *headers;
    size_t header_count;
    neverc_hpack_header_t *trailers;
    size_t trailer_count;
    const char *error;
} neverc_grpc_result_t;

typedef struct neverc_grpc_client_stream neverc_grpc_client_stream_t;

neverc_grpc_client_stream_t *neverc_grpc_client_stream_open(
    neverc_h2_client_t *client, neverc_context_t *context,
    const char *full_method, neverc_grpc_cardinality_t cardinality,
    const neverc_grpc_metadata_t *metadata, size_t metadata_count,
    size_t max_response_message_size, const char **error);
int neverc_grpc_client_stream_send(
    neverc_grpc_client_stream_t *stream, neverc_context_t *context,
    const void *message, size_t message_length);
int neverc_grpc_client_stream_close_send(
    neverc_grpc_client_stream_t *stream, neverc_context_t *context);
/* Return 1 for an owned-by-stream message view (valid until next receive),
 * 0 after valid grpc-status trailers, and -1 on protocol/transport error. */
int neverc_grpc_client_stream_receive(
    neverc_grpc_client_stream_t *stream, neverc_context_t *context,
    neverc_grpc_message_t *message);
neverc_grpc_status_t neverc_grpc_client_stream_status(
    neverc_grpc_client_stream_t *stream);
const char *neverc_grpc_client_stream_status_message(
    neverc_grpc_client_stream_t *stream);
const char *neverc_grpc_client_stream_error(
    neverc_grpc_client_stream_t *stream);
const neverc_hpack_header_t *neverc_grpc_client_stream_headers(
    neverc_grpc_client_stream_t *stream, size_t *header_count);
const neverc_hpack_header_t *neverc_grpc_client_stream_trailers(
    neverc_grpc_client_stream_t *stream, size_t *trailer_count);
void neverc_grpc_client_stream_cancel(neverc_grpc_client_stream_t *stream);
void neverc_grpc_client_stream_free(neverc_grpc_client_stream_t *stream);

neverc_grpc_result_t *neverc_grpc_client_call(
    neverc_h2_client_t *client, neverc_context_t *context,
    const char *full_method, neverc_grpc_cardinality_t cardinality,
    const neverc_grpc_metadata_t *metadata, size_t metadata_count,
    const neverc_grpc_message_t *requests, size_t request_count,
    size_t max_response_message_size);
void neverc_grpc_result_free(neverc_grpc_result_t *result);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_GRPC_H */
