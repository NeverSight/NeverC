/* HTTP/3 server and one-shot client on top of NeverC QUIC. */

#include "neverc/std/net/http3.h"

#include "../_net_thread.h"
#include "../http/_http_internal.h"
#include "../quic/_quic_internal.h"
#include "neverc/std/net/url.h"

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef H3_FRAME_TYPES_DEFINED
#define H3_FRAME_TYPES_DEFINED
typedef struct {
    uint64_t type;
    uint64_t length;
    size_t header_size;
} h3_frame_header_t;

typedef struct {
    uint64_t qpack_max_table_capacity;
    uint64_t max_field_section_size;
    uint64_t qpack_blocked_streams;
} h3_settings_t;
#endif

extern void neverc_h3_settings_default(h3_settings_t *settings);
extern int neverc_h3_settings_encode(const h3_settings_t *settings,
                                     uint8_t *buffer, size_t capacity,
                                     size_t *written);
extern int neverc_h3_settings_decode(const uint8_t *payload, size_t length,
                                     h3_settings_t *settings);
extern int neverc_h3_write_data_frame(uint8_t *buffer, size_t capacity,
                                      const uint8_t *data, size_t length,
                                      size_t *written);
extern int neverc_h3_write_headers_frame(uint8_t *buffer, size_t capacity,
                                         const uint8_t *headers,
                                         size_t headers_length,
                                         size_t *written);
extern int neverc_h3_write_goaway_frame(uint8_t *buffer, size_t capacity,
                                        uint64_t stream_id,
                                        size_t *written);

#define H3_STREAM_TYPE_CONTROL       0x00U
#define H3_STREAM_TYPE_PUSH          0x01U
#define H3_STREAM_TYPE_QPACK_ENCODER 0x02U
#define H3_STREAM_TYPE_QPACK_DECODER 0x03U

#define H3_MAX_HEADER_SECTION (1024U * 1024U)
#define H3_MAX_CONTROL_BUFFER (65536U + 16U)
#define H3_MAX_REQUEST_BODY   (16U * 1024U * 1024U)
#define H3_MAX_RESPONSE_BODY  (64U * 1024U * 1024U)
#define H3_DATA_CHUNK         (64U * 1024U)
#define H3_MAX_CONNECTIONS    4096U
#define H3_GRACEFUL_SHUTDOWN_MS 5000U
#define H3_POST_DRAIN_GRACE_MS 250U
#define H3_QPACK_DECOMPRESSION_FAILED 0x0200U

typedef struct h3_conn h3_conn_t;

typedef struct {
    neverc_quic_stream_t *stream;
    uint8_t encoded_type[8];
    size_t encoded_length;
    size_t encoded_needed;
} h3_pending_uni_t;

struct neverc_http3_server {
    neverc_http_mux_t *mux;
    uint32_t max_concurrent_streams;
    _Atomic int running;
    _Atomic int serving;
    _Atomic int stop_requested;
    neverc_quic_endpoint_t *endpoint;
    nc_threadpool_t *request_pool;
    h3_conn_t **connections;
    size_t connection_count;
    size_t connection_capacity;
    nc_mutex_t lock;
    nc_cond_t serve_done;
};

struct h3_conn {
    neverc_http3_server_t *server;
    neverc_quic_conn_t *quic;
    neverc_qpack_encoder_t *encoder;
    neverc_qpack_decoder_t *decoder;
    h3_settings_t local_settings;
    h3_settings_t peer_settings;
    neverc_quic_stream_t *control_out;
    neverc_quic_stream_t *qpack_encoder_out;
    neverc_quic_stream_t *qpack_decoder_out;
    neverc_quic_stream_t *control_in;
    neverc_quic_stream_t *qpack_encoder_in;
    neverc_quic_stream_t *qpack_decoder_in;
    h3_pending_uni_t pending_uni[16];
    size_t pending_uni_count;
    uint8_t *control_buffer;
    size_t control_buffer_length;
    uint64_t control_skip_remaining;
    int peer_settings_received;
    int peer_control_seen;
    int peer_encoder_seen;
    int peer_decoder_seen;
    int goaway_sent;
    int goaway_received;
    uint64_t goaway_id;
    uint64_t last_request_stream_id;
    nc_mutex_t lock;
    nc_cond_t settings_cond;
    nc_thread_t thread;
    int thread_started;
    _Atomic int worker_done;
    _Atomic int closing;
    _Atomic uint32_t active_requests;
    _Atomic uint32_t task_count;
};

typedef struct {
    h3_conn_t *connection;
    neverc_quic_stream_t *stream;
} h3_stream_task_t;

typedef struct {
    char method[16];
    char path[2048];
    char authority[256];
    char scheme[8];
    char *header_names[64];
    char *header_values[64];
    int nheaders;
    uint8_t *body;
    size_t body_len;
    uint64_t content_length;
    int content_length_present;
} h3_request_t;

static int h3_write_all(neverc_quic_stream_t *stream, const void *data,
                        size_t length) {
    if (length > INT_MAX) return -1;
    return neverc_quic_stream_write(stream, data, length) == (int)length ?
        0 : -1;
}

static void h3_sleep_ms(unsigned milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000U),
                             (long)(milliseconds % 1000U) * 1000000L};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
#endif
}

/* Returns 1 for a value, 0 for clean FIN, and -1 for a truncated/error read. */
static int h3_read_varint(neverc_quic_stream_t *stream, uint64_t *value) {
    uint8_t encoded[8];
    int first = neverc_quic_stream_read(stream, encoded, 1);
    if (first == 0) return 0;
    if (first != 1) return -1;
    size_t width = (size_t)1U << (encoded[0] >> 6);
    size_t position = 1;
    while (position < width) {
        int count = neverc_quic_stream_read(stream, encoded + position,
                                            width - position);
        if (count <= 0) return -1;
        position += (size_t)count;
    }
    size_t consumed = 0;
    return neverc_quic_varint_decode(encoded, width, value, &consumed) == 0 &&
            consumed == width ? 1 : -1;
}

static int h3_read_exact(neverc_quic_stream_t *stream, uint8_t *buffer,
                         size_t length) {
    size_t position = 0;
    while (position < length) {
        int count = neverc_quic_stream_read(stream, buffer + position,
                                            length - position);
        if (count <= 0) return -1;
        position += (size_t)count;
    }
    return 0;
}

static int h3_skip_exact(neverc_quic_stream_t *stream, uint64_t length) {
    uint8_t scratch[4096];
    while (length) {
        size_t chunk = length > sizeof(scratch) ? sizeof(scratch) :
                                                  (size_t)length;
        if (h3_read_exact(stream, scratch, chunk) != 0) return -1;
        length -= chunk;
    }
    return 0;
}

static int h3_read_payload(neverc_quic_stream_t *stream, uint64_t length,
                           size_t maximum, uint8_t **payload) {
    *payload = NULL;
    if (length > maximum || length > SIZE_MAX) return -1;
    if (length == 0) return 0;
    *payload = (uint8_t *)malloc((size_t)length);
    if (!*payload) return -1;
    if (h3_read_exact(stream, *payload, (size_t)length) != 0) {
        free(*payload);
        *payload = NULL;
        return -1;
    }
    return 0;
}

static void h3_protocol_error(h3_conn_t *connection, uint64_t error_code,
                              const char *reason) {
    if (!connection) return;
    if (!atomic_exchange_explicit(&connection->closing, 1,
                                  memory_order_acq_rel))
        neverc_quic_conn_close(connection->quic, error_code, reason);
    nc_mutex_lock(&connection->lock);
    nc_cond_broadcast(&connection->settings_cond);
    nc_mutex_unlock(&connection->lock);
}

static int h3_conn_init(h3_conn_t *connection,
                        neverc_http3_server_t *server,
                        neverc_quic_conn_t *quic) {
    memset(connection, 0, sizeof(*connection));
    connection->server = server;
    connection->quic = quic;
    connection->encoder = neverc_qpack_encoder_create(0);
    connection->decoder = neverc_qpack_decoder_create(0);
    if (!connection->encoder || !connection->decoder) {
        neverc_qpack_encoder_destroy(connection->encoder);
        neverc_qpack_decoder_destroy(connection->decoder);
        connection->encoder = NULL;
        connection->decoder = NULL;
        return -1;
    }
    neverc_h3_settings_default(&connection->local_settings);
    nc_mutex_init(&connection->lock);
    nc_cond_init(&connection->settings_cond);
    return 0;
}

static void h3_conn_cleanup(h3_conn_t *connection) {
    if (!connection) return;
    neverc_quic_stream_free(connection->control_out);
    neverc_quic_stream_free(connection->qpack_encoder_out);
    neverc_quic_stream_free(connection->qpack_decoder_out);
    neverc_quic_stream_free(connection->control_in);
    neverc_quic_stream_free(connection->qpack_encoder_in);
    neverc_quic_stream_free(connection->qpack_decoder_in);
    neverc_qpack_encoder_destroy(connection->encoder);
    neverc_qpack_decoder_destroy(connection->decoder);
    free(connection->control_buffer);
    nc_mutex_destroy(&connection->lock);
    nc_cond_destroy(&connection->settings_cond);
    neverc_quic_conn_free(connection->quic);
    free(connection);
}

static int h3_open_typed_stream(neverc_quic_conn_t *quic, uint64_t type,
                                neverc_quic_stream_t **stream) {
    uint8_t encoded[8];
    size_t length = 0;
    *stream = neverc_quic_open_uni_stream(quic, NULL);
    if (!*stream || neverc_quic_varint_encode(type, encoded,
                                               sizeof(encoded),
                                               &length) != 0 ||
        h3_write_all(*stream, encoded, length) != 0)
        return -1;
    return 0;
}

static int h3_setup_local_streams(h3_conn_t *connection) {
    uint8_t settings[512];
    size_t settings_length = 0;
    if (h3_open_typed_stream(connection->quic, H3_STREAM_TYPE_CONTROL,
                             &connection->control_out) != 0 ||
        neverc_h3_settings_encode(&connection->local_settings, settings,
                                  sizeof(settings), &settings_length) != 0 ||
        h3_write_all(connection->control_out, settings,
                     settings_length) != 0 ||
        h3_open_typed_stream(connection->quic,
                             H3_STREAM_TYPE_QPACK_ENCODER,
                             &connection->qpack_encoder_out) != 0 ||
        h3_open_typed_stream(connection->quic,
                             H3_STREAM_TYPE_QPACK_DECODER,
                             &connection->qpack_decoder_out) != 0)
        return -1;
    return 0;
}

static int h3_append_bytes(uint8_t **buffer, size_t *length, size_t maximum,
                           const uint8_t *data, size_t count) {
    if (count > maximum - *length) return -1;
    uint8_t *grown = (uint8_t *)realloc(*buffer, *length + count + 1U);
    if (!grown) return -1;
    *buffer = grown;
    if (count) memcpy(grown + *length, data, count);
    *length += count;
    grown[*length] = 0;
    return 0;
}

static int h3_header_token_char(unsigned char character);

static int h3_ascii_lower_name(const char *name) {
    if (!name || !name[0]) return 0;
    const unsigned char *cursor = (const unsigned char *)name;
    if (*cursor == ':') cursor++;
    if (!*cursor) return 0;
    for (;
         *cursor; cursor++) {
        if (*cursor >= 'A' && *cursor <= 'Z') return 0;
        if (!h3_header_token_char(*cursor)) return 0;
    }
    return 1;
}

static int h3_header_token_char(unsigned char character) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9'))
        return 1;
    return strchr("!#$%&'*+-.^_`|~", character) != NULL;
}

static char *h3_lowercase_header_name(const char *name) {
    if (!name || !name[0] || name[0] == ':') return NULL;
    size_t length = strlen(name);
    char *lower = (char *)malloc(length + 1U);
    if (!lower) return NULL;
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)name[i];
        if (!h3_header_token_char(character)) {
            free(lower);
            return NULL;
        }
        lower[i] = character >= 'A' && character <= 'Z' ?
            (char)(character + ('a' - 'A')) : (char)character;
    }
    lower[length] = '\0';
    return lower;
}

static int h3_forbidden_request_header(const char *name, const char *value) {
    if (strcmp(name, "connection") == 0 || strcmp(name, "keep-alive") == 0 ||
        strcmp(name, "proxy-connection") == 0 ||
        strcmp(name, "transfer-encoding") == 0 ||
        strcmp(name, "upgrade") == 0)
        return 1;
    return strcmp(name, "te") == 0 && strcasecmp(value, "trailers") != 0;
}

static int h3_parse_content_length(const char *value, uint64_t *result) {
    if (!value || !value[0]) return -1;
    uint64_t parsed = 0;
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9' ||
            parsed > (UINT64_MAX - (*cursor - '0')) / 10U)
            return -1;
        parsed = parsed * 10U + (*cursor - '0');
    }
    *result = parsed;
    return 0;
}

static int h3_copy_pseudo(char *destination, size_t capacity,
                          const char *value) {
    size_t length = strlen(value);
    if (length == 0 || length >= capacity) return -1;
    memcpy(destination, value, length + 1U);
    return 0;
}

static int h3_parse_request_headers(h3_conn_t *connection,
                                    const uint8_t *encoded, size_t length,
                                    h3_request_t *request) {
    neverc_qpack_header_t headers[64];
    int header_count = 0;
    nc_mutex_lock(&connection->lock);
    int decoded = neverc_qpack_decode(connection->decoder, encoded, length,
                                      headers, 64, &header_count);
    nc_mutex_unlock(&connection->lock);
    if (decoded != 0) return -1;
    int regular_seen = 0;
    unsigned pseudo_seen = 0;
    int result = -1;
    for (int i = 0; i < header_count; i++) {
        char *name = headers[i].name;
        char *value = headers[i].value;
        if (!h3_ascii_lower_name(name)) goto cleanup;
        if (name[0] == ':') {
            if (regular_seen) goto cleanup;
            unsigned bit = 0;
            char *destination = NULL;
            size_t capacity = 0;
            if (strcmp(name, ":method") == 0) {
                bit = 1U; destination = request->method;
                capacity = sizeof(request->method);
            } else if (strcmp(name, ":scheme") == 0) {
                bit = 2U; destination = request->scheme;
                capacity = sizeof(request->scheme);
            } else if (strcmp(name, ":authority") == 0) {
                bit = 4U; destination = request->authority;
                capacity = sizeof(request->authority);
            } else if (strcmp(name, ":path") == 0) {
                bit = 8U; destination = request->path;
                capacity = sizeof(request->path);
            } else {
                goto cleanup;
            }
            if ((pseudo_seen & bit) ||
                h3_copy_pseudo(destination, capacity, value) != 0)
                goto cleanup;
            pseudo_seen |= bit;
        } else {
            regular_seen = 1;
            if (h3_forbidden_request_header(name, value) ||
                request->nheaders == 64)
                goto cleanup;
            if (strcmp(name, "content-length") == 0) {
                uint64_t parsed;
                if (h3_parse_content_length(value, &parsed) != 0 ||
                    (request->content_length_present &&
                     request->content_length != parsed))
                    goto cleanup;
                request->content_length = parsed;
                request->content_length_present = 1;
            }
            request->header_names[request->nheaders] = name;
            request->header_values[request->nheaders] = value;
            request->nheaders++;
            headers[i].name = NULL;
            headers[i].value = NULL;
        }
    }
    if (pseudo_seen != (1U | 2U | 4U | 8U) ||
        strcmp(request->scheme, "https") != 0 || request->path[0] != '/' ||
        strcmp(request->method, "CONNECT") == 0)
        goto cleanup;
    result = 0;

cleanup:
    for (int i = 0; i < header_count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    return result;
}

static void h3_request_cleanup(h3_request_t *request) {
    for (int i = 0; i < request->nheaders; i++) {
        free(request->header_names[i]);
        free(request->header_values[i]);
    }
    free(request->body);
}

static int h3_wait_for_settings(h3_conn_t *connection) {
    nc_mutex_lock(&connection->lock);
    while (!connection->peer_settings_received &&
           !atomic_load_explicit(&connection->closing,
                                 memory_order_acquire) &&
           neverc_quic_conn_is_alive(connection->quic))
        nc_cond_wait(&connection->settings_cond, &connection->lock);
    int ready = connection->peer_settings_received;
    nc_mutex_unlock(&connection->lock);
    return ready ? 0 : -1;
}

static int h3_read_request(h3_conn_t *connection,
                           neverc_quic_stream_t *stream,
                           h3_request_t *request) {
    memset(request, 0, sizeof(*request));
    int initial_headers = 0;
    int trailers = 0;
    for (;;) {
        uint64_t type;
        int status = h3_read_varint(stream, &type);
        if (status == 0) break;
        if (status < 0) return -1;
        uint64_t length;
        if (h3_read_varint(stream, &length) != 1) return -1;
        if (type == NC_H3_FRAME_HEADERS) {
            uint8_t *payload = NULL;
            if (h3_read_payload(stream, length, H3_MAX_HEADER_SECTION,
                                &payload) != 0)
                return -1;
            if (!initial_headers) {
                int parsed = h3_parse_request_headers(
                    connection, payload, (size_t)length, request);
                free(payload);
                if (parsed != 0) return -1;
                initial_headers = 1;
            } else {
                neverc_qpack_header_t decoded[64];
                int count = 0;
                nc_mutex_lock(&connection->lock);
                int parsed = neverc_qpack_decode(
                    connection->decoder, payload, (size_t)length,
                    decoded, 64, &count);
                nc_mutex_unlock(&connection->lock);
                free(payload);
                if (parsed != 0) return -1;
                for (int i = 0; i < count; i++) {
                    if (decoded[i].name[0] == ':' ||
                        !h3_ascii_lower_name(decoded[i].name))
                        parsed = -1;
                    free(decoded[i].name);
                    free(decoded[i].value);
                }
                if (parsed != 0) return -1;
                trailers = 1;
            }
        } else if (type == NC_H3_FRAME_DATA) {
            if (!initial_headers || trailers || length > H3_MAX_REQUEST_BODY ||
                length > H3_MAX_REQUEST_BODY - request->body_len)
                return -1;
            uint8_t *payload = NULL;
            if (h3_read_payload(stream, length, H3_MAX_REQUEST_BODY,
                                &payload) != 0)
                return -1;
            int appended = h3_append_bytes(&request->body,
                                            &request->body_len,
                                            H3_MAX_REQUEST_BODY,
                                            payload, (size_t)length);
            free(payload);
            if (appended != 0) return -1;
        } else if (type == NC_H3_FRAME_SETTINGS ||
                   type == NC_H3_FRAME_GOAWAY ||
                   type == NC_H3_FRAME_MAX_PUSH_ID ||
                   type == NC_H3_FRAME_CANCEL_PUSH ||
                   type == NC_H3_FRAME_PUSH_PROMISE) {
            return -1;
        } else if (h3_skip_exact(stream, length) != 0) {
            return -1;
        }
    }
    if (!initial_headers ||
        (request->content_length_present &&
         request->content_length != request->body_len))
        return -1;
    return 0;
}

static int h3_response_header_allowed(const char *name) {
    return name && strcasecmp(name, "connection") != 0 &&
        strcasecmp(name, "keep-alive") != 0 &&
        strcasecmp(name, "proxy-connection") != 0 &&
        strcasecmp(name, "transfer-encoding") != 0 &&
        strcasecmp(name, "upgrade") != 0;
}

static int h3_send_header_section(h3_conn_t *connection,
                                  neverc_quic_stream_t *stream,
                                  neverc_qpack_header_t *headers,
                                  int header_count) {
    size_t estimate = 1024;
    for (int i = 0; i < header_count; i++) {
        size_t name_length = strlen(headers[i].name);
        size_t value_length = strlen(headers[i].value);
        if (name_length > H3_MAX_HEADER_SECTION - estimate ||
            value_length > H3_MAX_HEADER_SECTION - estimate - name_length)
            return -1;
        estimate += name_length + value_length + 16U;
    }
    if (estimate > H3_MAX_HEADER_SECTION) return -1;
    uint8_t *encoded = (uint8_t *)malloc(estimate);
    if (!encoded) return -1;
    size_t encoded_length = 0;
    nc_mutex_lock(&connection->lock);
    int result = neverc_qpack_encode(connection->encoder, headers,
                                     header_count, encoded, estimate,
                                     &encoded_length);
    nc_mutex_unlock(&connection->lock);
    if (result != 0) {
        free(encoded);
        return -1;
    }
    uint8_t *frame = (uint8_t *)malloc(encoded_length + 16U);
    if (!frame) {
        free(encoded);
        return -1;
    }
    size_t frame_length = 0;
    result = neverc_h3_write_headers_frame(frame, encoded_length + 16U,
                                           encoded, encoded_length,
                                           &frame_length);
    free(encoded);
    if (result == 0) result = h3_write_all(stream, frame, frame_length);
    free(frame);
    return result;
}

static int h3_send_response(h3_conn_t *connection,
                            neverc_quic_stream_t *stream,
                            neverc_http_response_writer_t *writer) {
    neverc_qpack_header_t headers[HTTP_MAX_HEADERS + 1];
    char *lower_names[HTTP_MAX_HEADERS];
    memset(lower_names, 0, sizeof(lower_names));
    if (writer->body.len > H3_MAX_RESPONSE_BODY) return -1;
    char status[4];
    if (writer->status < 100 || writer->status > 999) return -1;
    snprintf(status, sizeof(status), "%03d", writer->status);
    int count = 0;
    int result = 0;
    headers[count++] = (neverc_qpack_header_t){(char *)":status", status};
    for (int i = 0; i < writer->nheaders; i++) {
        if (!h3_response_header_allowed(writer->header_names[i])) continue;
        if (strcasecmp(writer->header_names[i], "content-length") == 0) {
            uint64_t content_length;
            if (h3_parse_content_length(writer->header_values[i],
                                        &content_length) != 0 ||
                content_length != writer->body.len) {
                result = -1;
                break;
            }
        }
        lower_names[i] = h3_lowercase_header_name(writer->header_names[i]);
        if (!lower_names[i]) {
            result = -1;
            break;
        }
        headers[count++] = (neverc_qpack_header_t){lower_names[i],
                                                   writer->header_values[i]};
    }
    if (result == 0)
        result = h3_send_header_section(connection, stream, headers, count);
    for (int i = 0; i < HTTP_MAX_HEADERS; i++) {
        free(lower_names[i]);
        lower_names[i] = NULL;
    }
    if (result != 0) return -1;
    if (!writer->head_request) {
        size_t position = 0;
        uint8_t *frame = (uint8_t *)malloc(H3_DATA_CHUNK + 16U);
        if (!frame) return -1;
        while (position < writer->body.len) {
            size_t chunk = writer->body.len - position;
            if (chunk > H3_DATA_CHUNK) chunk = H3_DATA_CHUNK;
            size_t frame_length = 0;
            if (neverc_h3_write_data_frame(frame, H3_DATA_CHUNK + 16U,
                                           (const uint8_t *)writer->body.data +
                                               position,
                                           chunk, &frame_length) != 0 ||
                h3_write_all(stream, frame, frame_length) != 0) {
                free(frame);
                return -1;
            }
            position += chunk;
        }
        free(frame);
    }
    if (writer->ntrailers) {
        count = 0;
        for (int i = 0; i < writer->ntrailers; i++) {
            if (!h3_response_header_allowed(writer->trailer_names[i]))
                continue;
            lower_names[i] = h3_lowercase_header_name(
                writer->trailer_names[i]);
            if (!lower_names[i]) {
                result = -1;
                break;
            }
            headers[count++] = (neverc_qpack_header_t){
                lower_names[i], writer->trailer_values[i]};
        }
        if (result == 0 && count)
            result = h3_send_header_section(connection, stream, headers,
                                             count);
        for (int i = 0; i < HTTP_MAX_HEADERS; i++) free(lower_names[i]);
        if (result != 0) return -1;
    }
    return neverc_quic_stream_close_write(stream);
}

static int h3_build_raw_headers(const h3_request_t *parsed, char **raw) {
    size_t total = 1;
    for (int i = 0; i < parsed->nheaders; i++) {
        size_t name = strlen(parsed->header_names[i]);
        size_t value = strlen(parsed->header_values[i]);
        if (name > SIZE_MAX - total - 1U ||
            value > SIZE_MAX - total - name - 2U)
            return -1;
        total += name + value + 2U;
    }
    char *buffer = (char *)malloc(total);
    if (!buffer) return -1;
    size_t position = 0;
    for (int i = 0; i < parsed->nheaders; i++) {
        size_t length = strlen(parsed->header_names[i]) + 1U;
        memcpy(buffer + position, parsed->header_names[i], length);
        position += length;
        length = strlen(parsed->header_values[i]) + 1U;
        memcpy(buffer + position, parsed->header_values[i], length);
        position += length;
    }
    buffer[position] = '\0';
    *raw = buffer;
    return 0;
}

static void h3_request_task(h3_stream_task_t *task) {
    h3_conn_t *connection = task->connection;
    neverc_quic_stream_t *stream = task->stream;
    h3_request_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    if (h3_wait_for_settings(connection) != 0 ||
        h3_read_request(connection, stream, &parsed) != 0) {
        h3_request_cleanup(&parsed);
        h3_protocol_error(connection, NC_H3_MESSAGE_ERROR,
                          "invalid HTTP/3 request");
        neverc_quic_stream_free(stream);
        return;
    }
    char *raw_headers = NULL;
    char *path = strdup(parsed.path);
    neverc_http_response_writer_t *writer = neverc_http_memory_writer_new();
    if (!path || !writer || h3_build_raw_headers(&parsed, &raw_headers) != 0) {
        free(path);
        free(raw_headers);
        neverc_http_memory_writer_free(writer);
        h3_request_cleanup(&parsed);
        h3_protocol_error(connection, NC_H3_INTERNAL_ERROR,
                          "HTTP/3 request allocation failed");
        neverc_quic_stream_free(stream);
        return;
    }
    char *query = strchr(path, '?');
    if (query) *query++ = '\0';
    const char *content_type = NULL;
    for (int i = 0; i < parsed.nheaders; i++) {
        if (strcmp(parsed.header_names[i], "content-type") == 0) {
            content_type = parsed.header_values[i];
            break;
        }
    }
    neverc_http_request_t request;
    memset(&request, 0, sizeof(request));
    request.method = parsed.method;
    request.path = path;
    request.query = query;
    request.http_version = "HTTP/3.0";
    request.host = parsed.authority;
    request.content_type = content_type;
    request.body = (const char *)parsed.body;
    request.body_len = parsed.body_len;
    request.raw_headers = raw_headers;
    request.nheaders = parsed.nheaders;
    request.context = neverc_context_background();
    writer->head_request = strcmp(parsed.method, "HEAD") == 0;
    writer->request_body_len = parsed.body_len;
    nc_http_mux_dispatch(connection->server->mux, &request, writer);
    neverc_context_free(request.context);
    request.context = NULL;
    if (h3_send_response(connection, stream, writer) != 0 &&
        neverc_quic_conn_is_alive(connection->quic))
        (void)neverc_quic_stream_reset(stream, NC_H3_INTERNAL_ERROR);
    neverc_http_memory_writer_free(writer);
    free(path);
    free(raw_headers);
    h3_request_cleanup(&parsed);
    neverc_quic_stream_free(stream);
}

static void h3_stream_task_run(void *argument) {
    h3_stream_task_t *task = (h3_stream_task_t *)argument;
    h3_conn_t *connection = task->connection;
    h3_request_task(task);
    atomic_fetch_sub_explicit(&connection->active_requests, 1,
                              memory_order_acq_rel);
    atomic_fetch_sub_explicit(&connection->task_count, 1,
                              memory_order_acq_rel);
    free(task);
}

static int h3_claim_peer_stream_type(h3_conn_t *connection, uint64_t type) {
    int *seen = NULL;
    if (type == H3_STREAM_TYPE_CONTROL)
        seen = &connection->peer_control_seen;
    else if (type == H3_STREAM_TYPE_QPACK_ENCODER)
        seen = &connection->peer_encoder_seen;
    else if (type == H3_STREAM_TYPE_QPACK_DECODER)
        seen = &connection->peer_decoder_seen;
    if (!seen) return 0;
    nc_mutex_lock(&connection->lock);
    int duplicate = *seen;
    *seen = 1;
    nc_mutex_unlock(&connection->lock);
    return duplicate ? -1 : 0;
}

static int h3_submit_stream(h3_conn_t *connection,
                            neverc_quic_stream_t *stream) {
    h3_stream_task_t *task =
        (h3_stream_task_t *)calloc(1, sizeof(*task));
    if (!task) return -1;
    task->connection = connection;
    task->stream = stream;
    atomic_fetch_add_explicit(&connection->task_count, 1,
                              memory_order_acq_rel);
    if (nc_threadpool_try_submit(connection->server->request_pool,
                                 h3_stream_task_run, task) != 0) {
        atomic_fetch_sub_explicit(&connection->task_count, 1,
                                  memory_order_acq_rel);
        free(task);
        return -1;
    }
    return 0;
}

static int h3_decode_buffer_varint(const uint8_t *buffer, size_t length,
                                   size_t *position, uint64_t *value) {
    if (*position >= length) return 0;
    size_t width = (size_t)1U << (buffer[*position] >> 6);
    if (width > length - *position) return 0;
    size_t consumed = 0;
    if (neverc_quic_varint_decode(buffer + *position, width, value,
                                  &consumed) != 0 || consumed != width)
        return -1;
    *position += width;
    return 1;
}

static void h3_consume_control_buffer(h3_conn_t *connection, size_t count) {
    if (count < connection->control_buffer_length)
        memmove(connection->control_buffer,
                connection->control_buffer + count,
                connection->control_buffer_length - count);
    connection->control_buffer_length -= count;
}

static int h3_parse_control_buffer(h3_conn_t *connection) {
    for (;;) {
        if (connection->control_skip_remaining) {
            size_t consume = connection->control_buffer_length;
            if ((uint64_t)consume > connection->control_skip_remaining)
                consume = (size_t)connection->control_skip_remaining;
            h3_consume_control_buffer(connection, consume);
            connection->control_skip_remaining -= consume;
            if (connection->control_skip_remaining) return 0;
            continue;
        }
        size_t position = 0;
        uint64_t type;
        uint64_t length;
        int decoded = h3_decode_buffer_varint(connection->control_buffer,
                                               connection->control_buffer_length,
                                               &position, &type);
        if (decoded <= 0) return decoded;
        decoded = h3_decode_buffer_varint(connection->control_buffer,
                                          connection->control_buffer_length,
                                          &position, &length);
        if (decoded <= 0) return decoded;
        if (!connection->peer_settings_received &&
            type != NC_H3_FRAME_SETTINGS) {
            h3_protocol_error(connection, NC_H3_MISSING_SETTINGS,
                              "SETTINGS is not the first control frame");
            return -1;
        }
        if (type == NC_H3_FRAME_SETTINGS) {
            if (connection->peer_settings_received || length > 65536U)
                goto invalid;
            if (length > connection->control_buffer_length - position)
                return 0;
            h3_settings_t settings;
            if (neverc_h3_settings_decode(
                    connection->control_buffer + position, (size_t)length,
                    &settings) != 0)
                goto invalid;
            nc_mutex_lock(&connection->lock);
            connection->peer_settings = settings;
            connection->peer_settings_received = 1;
            nc_cond_broadcast(&connection->settings_cond);
            nc_mutex_unlock(&connection->lock);
            h3_consume_control_buffer(connection, position + (size_t)length);
        } else if (type == NC_H3_FRAME_GOAWAY) {
            if (length == 0 || length > 8U ||
                length > connection->control_buffer_length - position)
                goto invalid;
            size_t consumed = 0;
            uint64_t identifier;
            if (neverc_quic_varint_decode(
                    connection->control_buffer + position, (size_t)length,
                    &identifier, &consumed) != 0 || consumed != length)
                goto invalid;
            nc_mutex_lock(&connection->lock);
            int increasing = connection->goaway_received &&
                             identifier > connection->goaway_id;
            connection->goaway_received = 1;
            connection->goaway_id = identifier;
            nc_mutex_unlock(&connection->lock);
            if (increasing) goto invalid;
            h3_consume_control_buffer(connection, position + (size_t)length);
        } else if (type == NC_H3_FRAME_DATA ||
                   type == NC_H3_FRAME_HEADERS ||
                   type == NC_H3_FRAME_PUSH_PROMISE) {
            goto invalid;
        } else {
            h3_consume_control_buffer(connection, position);
            connection->control_skip_remaining = length;
        }
    }

invalid:
    h3_protocol_error(connection, NC_H3_FRAME_ERROR,
                      "invalid HTTP/3 control stream");
    return -1;
}

static int h3_poll_control_stream(h3_conn_t *connection, int *worked) {
    if (!connection->control_in) return 0;
    uint8_t scratch[4096];
    for (;;) {
        int count = neverc_quic_stream_try_read(connection->control_in,
                                                scratch, sizeof(scratch));
        if (count == -2) return h3_parse_control_buffer(connection);
        if (count == 0) {
            if (!neverc_quic_conn_is_alive(connection->quic))
                return -1;
            if (connection->goaway_sent ||
                !atomic_load_explicit(&connection->server->running,
                                      memory_order_acquire))
                return 0;
            h3_protocol_error(connection, NC_H3_CLOSED_CRITICAL_STREAM,
                              "HTTP/3 control stream closed");
            return -1;
        }
        if (count < 0 || h3_append_bytes(&connection->control_buffer,
                                         &connection->control_buffer_length,
                                         H3_MAX_CONTROL_BUFFER, scratch,
                                         (size_t)count) != 0)
            return -1;
        *worked = 1;
        if (h3_parse_control_buffer(connection) != 0) return -1;
    }
}

static int h3_poll_qpack_stream(h3_conn_t *connection,
                                neverc_quic_stream_t *stream, int *worked) {
    if (!stream) return 0;
    uint8_t scratch[4096];
    for (;;) {
        int count = neverc_quic_stream_try_read(stream, scratch,
                                                sizeof(scratch));
        if (count == -2) return 0;
        if (count == 0) {
            if (!neverc_quic_conn_is_alive(connection->quic))
                return -1;
            if (connection->goaway_sent ||
                !atomic_load_explicit(&connection->server->running,
                                      memory_order_acquire))
                return 0;
            h3_protocol_error(connection, NC_H3_CLOSED_CRITICAL_STREAM,
                              "QPACK critical stream closed");
            return -1;
        }
        if (count < 0) return -1;
        *worked = 1;
        /* Dynamic QPACK instructions cannot affect static-only blocks because
         * this endpoint advertises capacity and blocked streams as zero. */
    }
}

static int h3_install_peer_stream(h3_conn_t *connection,
                                  neverc_quic_stream_t *stream,
                                  uint64_t stream_type) {
    if (h3_claim_peer_stream_type(connection, stream_type) != 0)
        return -1;
    if (stream_type == H3_STREAM_TYPE_CONTROL)
        connection->control_in = stream;
    else if (stream_type == H3_STREAM_TYPE_QPACK_ENCODER)
        connection->qpack_encoder_in = stream;
    else if (stream_type == H3_STREAM_TYPE_QPACK_DECODER)
        connection->qpack_decoder_in = stream;
    else if (stream_type == H3_STREAM_TYPE_PUSH)
        return -1;
    else {
        (void)neverc_quic_stream_stop_sending(stream, NC_H3_NO_ERROR);
        neverc_quic_stream_free(stream);
    }
    return 0;
}

static int h3_poll_pending_uni(h3_conn_t *connection, int *worked) {
    size_t index = 0;
    while (index < connection->pending_uni_count) {
        h3_pending_uni_t *pending = &connection->pending_uni[index];
        if (pending->encoded_length == 0) pending->encoded_needed = 1;
        int count = neverc_quic_stream_try_read(
            pending->stream,
            pending->encoded_type + pending->encoded_length,
            pending->encoded_needed - pending->encoded_length);
        if (count == -2) {
            index++;
            continue;
        }
        if (count <= 0) return -1;
        *worked = 1;
        pending->encoded_length += (size_t)count;
        if (pending->encoded_length == 1)
            pending->encoded_needed =
                (size_t)1U << (pending->encoded_type[0] >> 6);
        if (pending->encoded_length < pending->encoded_needed) {
            index++;
            continue;
        }
        uint64_t stream_type;
        size_t consumed = 0;
        if (neverc_quic_varint_decode(pending->encoded_type,
                                      pending->encoded_needed,
                                      &stream_type, &consumed) != 0 ||
            consumed != pending->encoded_needed ||
            h3_install_peer_stream(connection, pending->stream,
                                   stream_type) != 0)
            return -1;
        connection->pending_uni[index] =
            connection->pending_uni[connection->pending_uni_count - 1U];
        connection->pending_uni_count--;
    }
    return 0;
}

static int h3_dispatch_request_stream(h3_conn_t *connection,
                                      neverc_quic_stream_t *stream) {
    nc_mutex_lock(&connection->lock);
    if (connection->goaway_sent ||
        !atomic_load_explicit(&connection->server->running,
                              memory_order_acquire)) {
        nc_mutex_unlock(&connection->lock);
        (void)neverc_quic_stream_reset(stream, NC_H3_REQUEST_REJECTED);
        neverc_quic_stream_free(stream);
        return 0;
    }
    uint32_t active = atomic_fetch_add_explicit(
        &connection->active_requests, 1, memory_order_acq_rel) + 1U;
    if (active > connection->server->max_concurrent_streams ||
        h3_submit_stream(connection, stream) != 0) {
        atomic_fetch_sub_explicit(&connection->active_requests, 1,
                                  memory_order_acq_rel);
        nc_mutex_unlock(&connection->lock);
        (void)neverc_quic_stream_reset(stream, NC_H3_EXCESSIVE_LOAD);
        neverc_quic_stream_free(stream);
        return active > connection->server->max_concurrent_streams ? 0 : -1;
    }
    uint64_t stream_id = neverc_quic_stream_id(stream);
    if (stream_id > connection->last_request_stream_id)
        connection->last_request_stream_id = stream_id;
    nc_mutex_unlock(&connection->lock);
    return 0;
}

static int h3_connection_needs_worker(h3_conn_t *connection) {
    return atomic_load_explicit(&connection->active_requests,
                                memory_order_acquire) != 0 ||
           atomic_load_explicit(&connection->task_count,
                                memory_order_acquire) != 0 ||
           !neverc_quic_conn_send_drained(connection->quic);
}

static void *h3_connection_worker(void *argument) {
    h3_conn_t *connection = (h3_conn_t *)argument;
    while (neverc_quic_conn_is_alive(connection->quic)) {
        if (!atomic_load_explicit(&connection->server->running,
                                  memory_order_acquire) &&
            !h3_connection_needs_worker(connection))
            break;
        int worked = 0;
        if (h3_poll_pending_uni(connection, &worked) != 0 ||
            h3_poll_control_stream(connection, &worked) != 0 ||
            h3_poll_qpack_stream(connection, connection->qpack_encoder_in,
                                 &worked) != 0 ||
            h3_poll_qpack_stream(connection, connection->qpack_decoder_in,
                                 &worked) != 0) {
            if (neverc_quic_conn_is_alive(connection->quic))
                h3_protocol_error(connection, NC_H3_GENERAL_PROTOCOL_ERROR,
                                  "critical stream processing failed");
            break;
        }
        for (int accepted = 0; accepted < 32; accepted++) {
            neverc_quic_stream_t *stream = NULL;
            int status = neverc_quic_try_accept_stream(connection->quic,
                                                        &stream);
            if (status < 0) goto worker_done;
            if (status == 0) break;
            worked = 1;
            uint64_t stream_id = neverc_quic_stream_id(stream);
            if ((stream_id & 2U) != 0) {
                if (connection->pending_uni_count == 16) {
                    neverc_quic_stream_free(stream);
                    h3_protocol_error(connection, NC_H3_EXCESSIVE_LOAD,
                                      "too many incomplete stream types");
                    goto worker_done;
                }
                connection->pending_uni[connection->pending_uni_count++] =
                    (h3_pending_uni_t){.stream = stream};
            } else if (h3_dispatch_request_stream(connection, stream) != 0) {
                h3_protocol_error(connection, NC_H3_EXCESSIVE_LOAD,
                                  "HTTP/3 request queue is full");
                goto worker_done;
            }
        }
        if (!worked) h3_sleep_ms(2);
    }

worker_done:
    for (size_t i = 0; i < connection->pending_uni_count; i++)
        neverc_quic_stream_free(connection->pending_uni[i].stream);
    atomic_store_explicit(&connection->worker_done, 1,
                          memory_order_release);
    nc_mutex_lock(&connection->lock);
    nc_cond_broadcast(&connection->settings_cond);
    nc_mutex_unlock(&connection->lock);
    return NULL;
}

static int h3_server_add_connection(neverc_http3_server_t *server,
                                    h3_conn_t *connection) {
    if (server->connection_count == H3_MAX_CONNECTIONS) return -1;
    if (server->connection_count == server->connection_capacity) {
        size_t next = server->connection_capacity ?
            server->connection_capacity * 2U : 32U;
        if (next > H3_MAX_CONNECTIONS) next = H3_MAX_CONNECTIONS;
        h3_conn_t **grown = (h3_conn_t **)realloc(
            server->connections, next * sizeof(*grown));
        if (!grown) return -1;
        server->connections = grown;
        server->connection_capacity = next;
    }
    server->connections[server->connection_count++] = connection;
    return 0;
}

static void h3_server_reap_connections(neverc_http3_server_t *server) {
    size_t index = 0;
    while (index < server->connection_count) {
        h3_conn_t *connection = server->connections[index];
        if (!atomic_load_explicit(&connection->worker_done,
                                  memory_order_acquire) ||
            atomic_load_explicit(&connection->task_count,
                                 memory_order_acquire) != 0) {
            index++;
            continue;
        }
        if (connection->thread_started) {
            (void)nc_thread_join(connection->thread);
            connection->thread_started = 0;
        }
        server->connections[index] =
            server->connections[server->connection_count - 1U];
        server->connection_count--;
        h3_conn_cleanup(connection);
    }
}

static void h3_server_send_goaway(h3_conn_t *connection) {
    nc_mutex_lock(&connection->lock);
    if (connection->goaway_sent || !connection->control_out) {
        nc_mutex_unlock(&connection->lock);
        return;
    }
    uint64_t identifier = connection->last_request_stream_id + 4U;
    connection->goaway_sent = 1;
    nc_mutex_unlock(&connection->lock);
    uint8_t frame[32];
    size_t length = 0;
    if (neverc_h3_write_goaway_frame(frame, sizeof(frame), identifier,
                                     &length) == 0)
        (void)h3_write_all(connection->control_out, frame, length);
}

static void h3_server_close_connections(neverc_http3_server_t *server) {
    nc_mutex_lock(&server->lock);
    for (size_t i = 0; i < server->connection_count; i++)
        h3_server_send_goaway(server->connections[i]);

    unsigned drained_for_ms = 0;
    for (unsigned waited = 0; waited < H3_GRACEFUL_SHUTDOWN_MS; waited += 2U) {
        int drained = 1;
        for (size_t i = 0; i < server->connection_count; i++) {
            h3_conn_t *connection = server->connections[i];
            if (h3_connection_needs_worker(connection)) {
                drained = 0;
                drained_for_ms = 0;
                (void)neverc_quic_conn_flush(connection->quic);
                break;
            }
        }
        if (drained) {
            drained_for_ms += 2U;
            if (drained_for_ms >= H3_POST_DRAIN_GRACE_MS)
                break;
        }
        h3_sleep_ms(2);
    }

    for (size_t i = 0; i < server->connection_count; i++) {
        h3_conn_t *connection = server->connections[i];
        if (connection->thread_started) {
            (void)nc_thread_join(connection->thread);
            connection->thread_started = 0;
        }
    }

    for (size_t i = 0; i < server->connection_count; i++)
        h3_protocol_error(server->connections[i], NC_H3_NO_ERROR,
                          "server shutdown");
    nc_mutex_unlock(&server->lock);
}

static void h3_server_release_connections(neverc_http3_server_t *server) {
    for (size_t i = 0; i < server->connection_count; i++) {
        h3_conn_t *connection = server->connections[i];
        if (connection->thread_started)
            (void)nc_thread_join(connection->thread);
    }
    nc_threadpool_destroy(server->request_pool);
    server->request_pool = NULL;
    for (size_t i = 0; i < server->connection_count; i++)
        h3_conn_cleanup(server->connections[i]);
    free(server->connections);
    server->connections = NULL;
    server->connection_count = 0;
    server->connection_capacity = 0;
}

neverc_http3_server_t *neverc_http3_server_create(neverc_http_mux_t *mux) {
    neverc_http3_server_t *server =
        (neverc_http3_server_t *)calloc(1, sizeof(*server));
    if (!server) return NULL;
    server->mux = mux;
    server->max_concurrent_streams = 100;
    nc_mutex_init(&server->lock);
    nc_cond_init(&server->serve_done);
    return server;
}

void neverc_http3_server_destroy(neverc_http3_server_t *server) {
    if (!server) return;
    neverc_http3_server_stop(server);
    nc_mutex_lock(&server->lock);
    while (atomic_load_explicit(&server->serving, memory_order_acquire))
        nc_cond_wait(&server->serve_done, &server->lock);
    nc_mutex_unlock(&server->lock);
    nc_mutex_destroy(&server->lock);
    nc_cond_destroy(&server->serve_done);
    free(server);
}

void neverc_http3_server_set_max_streams(neverc_http3_server_t *server,
                                         uint32_t maximum) {
    if (server && maximum > 0 && maximum <= 1024 &&
        !atomic_load_explicit(&server->serving, memory_order_acquire))
        server->max_concurrent_streams = maximum;
}

void neverc_http3_server_stop(neverc_http3_server_t *server) {
    if (!server) return;
    atomic_store_explicit(&server->stop_requested, 1, memory_order_release);
    if (!atomic_exchange_explicit(&server->running, 0,
                                  memory_order_acq_rel))
        return;
    h3_server_close_connections(server);
    nc_mutex_lock(&server->lock);
    neverc_quic_endpoint_t *endpoint = server->endpoint;
    server->endpoint = NULL;
    nc_mutex_unlock(&server->lock);
    if (endpoint) neverc_quic_endpoint_close(endpoint);
}

int neverc_http3_server_is_running(const neverc_http3_server_t *server) {
    return server ? atomic_load_explicit(&server->running,
                                          memory_order_acquire) : 0;
}

uint32_t neverc_http3_server_max_streams(const neverc_http3_server_t *server) {
    return server ? server->max_concurrent_streams : 0;
}

int neverc_http3_listen_and_serve(const char *addr,
                                  neverc_http3_server_t *server,
                                  const char *cert_file,
                                  const char *key_file) {
    if (!addr || !server || !cert_file || !key_file ||
        atomic_exchange_explicit(&server->serving, 1,
                                 memory_order_acq_rel)) {
        errno = EINVAL;
        return -1;
    }
    int result = -1;
    neverc_quic_config_t config = neverc_quic_config_default();
    const char *alpn[] = {"h3", NULL};
    config.cert_file = cert_file;
    config.key_file = key_file;
    config.alpn = alpn;
    config.max_stream_data_bidi_local = H3_MAX_RESPONSE_BODY;
    config.max_stream_data_bidi_remote = H3_MAX_REQUEST_BODY;
    config.max_stream_data_uni = H3_MAX_HEADER_SECTION;
    config.max_data = H3_MAX_RESPONSE_BODY * 4U;
    config.max_streams_bidi = server->max_concurrent_streams;
    config.max_streams_uni = 16;
    server->request_pool = nc_threadpool_create(
        server->max_concurrent_streams < 32 ?
            (int)server->max_concurrent_streams : 32);
    if (!server->request_pool) goto done;
    const char *error = NULL;
    neverc_quic_endpoint_t *endpoint = neverc_quic_listen(
        addr, &config, &error);
    if (!endpoint) goto done;
    nc_mutex_lock(&server->lock);
    if (atomic_load_explicit(&server->stop_requested,
                             memory_order_acquire)) {
        nc_mutex_unlock(&server->lock);
        neverc_quic_endpoint_close(endpoint);
        result = 0;
        goto done;
    }
    server->endpoint = endpoint;
    atomic_store_explicit(&server->running, 1, memory_order_release);
    nc_mutex_unlock(&server->lock);
    result = 0;
    while (atomic_load_explicit(&server->running, memory_order_acquire)) {
        neverc_quic_conn_t *quic = neverc_quic_accept(endpoint, &error);
        if (!quic) {
            if (atomic_load_explicit(&server->running,
                                     memory_order_acquire))
                result = -1;
            break;
        }
        nc_mutex_lock(&server->lock);
        h3_server_reap_connections(server);
        nc_mutex_unlock(&server->lock);
        h3_conn_t *connection = (h3_conn_t *)calloc(1, sizeof(*connection));
        if (!connection) {
            neverc_quic_conn_free(quic);
            continue;
        }
        if (h3_conn_init(connection, server, quic) != 0) {
            neverc_quic_conn_free(quic);
            free(connection);
            continue;
        }
        if (h3_setup_local_streams(connection) != 0) {
            h3_conn_cleanup(connection);
            continue;
        }
        nc_mutex_lock(&server->lock);
        int added = h3_server_add_connection(server, connection);
        nc_mutex_unlock(&server->lock);
        if (added != 0 || nc_thread_create(&connection->thread,
                                            h3_connection_worker,
                                            connection) != 0) {
            if (added == 0) {
                nc_mutex_lock(&server->lock);
                server->connection_count--;
                nc_mutex_unlock(&server->lock);
            }
            h3_conn_cleanup(connection);
            continue;
        }
        connection->thread_started = 1;
    }
    if (atomic_exchange_explicit(&server->running, 0,
                                 memory_order_acq_rel)) {
        h3_server_close_connections(server);
        nc_mutex_lock(&server->lock);
        if (server->endpoint == endpoint) server->endpoint = NULL;
        nc_mutex_unlock(&server->lock);
        neverc_quic_endpoint_close(endpoint);
    }

done:
    h3_server_release_connections(server);
    nc_mutex_lock(&server->lock);
    server->endpoint = NULL;
    atomic_store_explicit(&server->serving, 0, memory_order_release);
    nc_cond_broadcast(&server->serve_done);
    nc_mutex_unlock(&server->lock);
    return result;
}

static neverc_http_response_t *h3_error_response(const char *error) {
    neverc_http_response_t *response =
        (neverc_http_response_t *)calloc(1, sizeof(*response));
    if (response) response->error = error;
    return response;
}

static int h3_client_receive_settings(h3_conn_t *connection) {
    for (int i = 0; i < 16; i++) {
        neverc_quic_stream_t *stream =
            neverc_quic_accept_stream(connection->quic, NULL);
        if (!stream || (neverc_quic_stream_id(stream) & 2U) == 0)
            return -1;
        uint64_t stream_type;
        if (h3_read_varint(stream, &stream_type) != 1) return -1;
        if (stream_type != H3_STREAM_TYPE_CONTROL) {
            neverc_quic_stream_free(stream);
            continue;
        }
        uint64_t frame_type;
        uint64_t frame_length;
        if (h3_read_varint(stream, &frame_type) != 1 ||
            frame_type != NC_H3_FRAME_SETTINGS ||
            h3_read_varint(stream, &frame_length) != 1)
            return -1;
        uint8_t *payload = NULL;
        if (h3_read_payload(stream, frame_length, 65536, &payload) != 0)
            return -1;
        int decoded = neverc_h3_settings_decode(payload,
                                                 (size_t)frame_length,
                                                 &connection->peer_settings);
        free(payload);
        neverc_quic_stream_free(stream);
        if (decoded != 0) return -1;
        connection->peer_settings_received = 1;
        return 0;
    }
    return -1;
}

static int h3_append_header_line(char **text, size_t *length,
                                 const char *name, const char *value) {
    size_t name_length = strlen(name);
    size_t value_length = strlen(value);
    if (name_length > H3_MAX_HEADER_SECTION - *length ||
        value_length > H3_MAX_HEADER_SECTION - *length - name_length - 4U)
        return -1;
    size_t needed = *length + name_length + value_length + 4U;
    char *grown = (char *)realloc(*text, needed + 1U);
    if (!grown) return -1;
    *text = grown;
    int written = snprintf(grown + *length, needed - *length + 1U,
                           "%s: %s\r\n", name, value);
    if (written < 0 || (size_t)written != needed - *length) return -1;
    *length = needed;
    return 0;
}

static int h3_client_parse_header_block(h3_conn_t *connection,
                                        const uint8_t *payload,
                                        size_t payload_length,
                                        int trailers,
                                        neverc_http_response_t *response,
                                        uint64_t *content_length,
                                        int *content_length_present) {
    neverc_qpack_header_t headers[64];
    int count = 0;
    if (neverc_qpack_decode(connection->decoder, payload, payload_length,
                            headers, 64, &count) != 0)
        return -1;
    int status_seen = 0;
    int regular_seen = 0;
    size_t text_length = 0;
    char **text = trailers ? &response->trailers : &response->headers;
    if (*text) text_length = strlen(*text);
    int result = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(headers[i].name, ":status") == 0 && !trailers) {
            if (regular_seen || status_seen || strlen(headers[i].value) != 3 ||
                headers[i].value[0] < '1' || headers[i].value[0] > '9' ||
                headers[i].value[1] < '0' || headers[i].value[1] > '9' ||
                headers[i].value[2] < '0' || headers[i].value[2] > '9') {
                result = -1;
            } else {
                response->status_code = atoi(headers[i].value);
                status_seen = 1;
            }
        } else if (headers[i].name[0] == ':' ||
                   !h3_ascii_lower_name(headers[i].name) ||
                   !h3_response_header_allowed(headers[i].name) ||
                   (trailers && strcmp(headers[i].name,
                                        "content-length") == 0) ||
                   h3_append_header_line(text, &text_length,
                                         headers[i].name,
                                         headers[i].value) != 0) {
            result = -1;
        } else {
            regular_seen = 1;
            if (!trailers && strcmp(headers[i].name,
                                     "content-length") == 0) {
                uint64_t parsed;
                if (h3_parse_content_length(headers[i].value, &parsed) != 0 ||
                    (*content_length_present &&
                     *content_length != parsed)) {
                    result = -1;
                } else {
                    *content_length = parsed;
                    *content_length_present = 1;
                }
            }
        }
        free(headers[i].name);
        free(headers[i].value);
    }
    if (!trailers && !status_seen) result = -1;
    return result;
}

static int h3_client_read_response(h3_conn_t *connection,
                                   neverc_quic_stream_t *stream,
                                   neverc_http_response_t *response) {
    int final_headers = 0;
    int trailers = 0;
    uint64_t expected_content_length = 0;
    int content_length_present = 0;
    for (;;) {
        uint64_t type;
        int status = h3_read_varint(stream, &type);
        if (status == 0) break;
        if (status < 0) return -1;
        uint64_t length;
        if (h3_read_varint(stream, &length) != 1) return -1;
        if (type == NC_H3_FRAME_HEADERS) {
            uint8_t *payload = NULL;
            if (h3_read_payload(stream, length, H3_MAX_HEADER_SECTION,
                                &payload) != 0)
                return -1;
            int parsed = h3_client_parse_header_block(
                connection, payload, (size_t)length, final_headers,
                response, &expected_content_length,
                &content_length_present);
            free(payload);
            if (parsed != 0) return -1;
            if (!final_headers && response->status_code >= 100 &&
                response->status_code < 200) {
                response->status_code = 0;
                expected_content_length = 0;
                content_length_present = 0;
                free(response->headers);
                response->headers = NULL;
            } else if (!final_headers) {
                final_headers = 1;
            } else {
                trailers = 1;
            }
        } else if (type == NC_H3_FRAME_DATA) {
            if (!final_headers || trailers || length > H3_MAX_RESPONSE_BODY ||
                length > H3_MAX_RESPONSE_BODY - response->body_len)
                return -1;
            uint8_t *payload = NULL;
            if (h3_read_payload(stream, length, H3_MAX_RESPONSE_BODY,
                                &payload) != 0)
                return -1;
            int appended = h3_append_bytes((uint8_t **)&response->body,
                                            &response->body_len,
                                            H3_MAX_RESPONSE_BODY,
                                            payload, (size_t)length);
            free(payload);
            if (appended != 0) return -1;
        } else if (type == NC_H3_FRAME_SETTINGS ||
                   type == NC_H3_FRAME_GOAWAY ||
                   type == NC_H3_FRAME_MAX_PUSH_ID ||
                   type == NC_H3_FRAME_CANCEL_PUSH) {
            return -1;
        } else if (h3_skip_exact(stream, length) != 0) {
            return -1;
        }
    }
    return final_headers &&
            (!content_length_present ||
             expected_content_length == response->body_len) ? 0 : -1;
}

static neverc_http_response_t *h3_client_request(
    const char *method, const char *url, const char *content_type,
    const void *body, size_t body_length,
    const neverc_http3_client_config_t *client_config) {
    if (!method || !url || (!body && body_length != 0) ||
        body_length > H3_MAX_REQUEST_BODY ||
        (client_config && client_config->insecure_skip_verify != 0 &&
         client_config->insecure_skip_verify != 1))
        return h3_error_response("invalid HTTP/3 request");
    neverc_url_t parsed;
    if (neverc_url_parse(&parsed, url) != 0 ||
        strcmp(parsed.scheme, "https") != 0 || !parsed.host[0] ||
        parsed.user[0] || parsed.password[0])
        return h3_error_response("HTTP/3 requires an absolute https URL");
    const char *port = parsed.port[0] ? parsed.port : "443";
    char address[320];
    char authority[320];
    if (strchr(parsed.host, ':')) {
        if (snprintf(address, sizeof(address), "[%s]:%s", parsed.host,
                     port) >= (int)sizeof(address) ||
            snprintf(authority, sizeof(authority), "[%s]%s%s", parsed.host,
                     strcmp(port, "443") == 0 ? "" : ":",
                     strcmp(port, "443") == 0 ? "" : port) >=
                (int)sizeof(authority))
            return h3_error_response("HTTP/3 URL authority is too long");
    } else {
        if (snprintf(address, sizeof(address), "%s:%s", parsed.host, port) >=
                (int)sizeof(address) ||
            snprintf(authority, sizeof(authority), "%s%s%s", parsed.host,
                     strcmp(port, "443") == 0 ? "" : ":",
                     strcmp(port, "443") == 0 ? "" : port) >=
                (int)sizeof(authority))
            return h3_error_response("HTTP/3 URL authority is too long");
    }
    char request_uri[3072];
    int request_uri_length = neverc_url_request_uri(
        &parsed, request_uri, sizeof(request_uri));
    if (request_uri_length < 0 ||
        (size_t)request_uri_length >= sizeof(request_uri))
        return h3_error_response("HTTP/3 request URI is too long");
    neverc_quic_config_t config = neverc_quic_config_default();
    const char *alpn[] = {"h3", NULL};
    config.alpn = alpn;
    config.server_name = client_config && client_config->server_name
        ? client_config->server_name : parsed.host;
    config.root_cert_file = client_config
        ? client_config->root_cert_file : NULL;
    config.insecure_skip_verify = client_config
        ? client_config->insecure_skip_verify : 0;
    config.max_stream_data_bidi_local = H3_MAX_REQUEST_BODY;
    config.max_stream_data_bidi_remote = H3_MAX_RESPONSE_BODY;
    config.max_stream_data_uni = H3_MAX_HEADER_SECTION;
    config.max_data = H3_MAX_RESPONSE_BODY * 2U;
    config.max_streams_uni = 16;
    neverc_quic_conn_t *quic = neverc_quic_dial(address, &config, NULL);
    if (!quic) return h3_error_response("HTTP/3 QUIC handshake failed");
    const char *negotiated_alpn = neverc_quic_conn_alpn(quic);
    if (!negotiated_alpn || strcmp(negotiated_alpn, "h3") != 0) {
        neverc_quic_conn_free(quic);
        return h3_error_response("HTTP/3 peer did not negotiate h3");
    }
    h3_conn_t connection;
    if (h3_conn_init(&connection, NULL, quic) != 0 ||
        h3_setup_local_streams(&connection) != 0 ||
        h3_client_receive_settings(&connection) != 0) {
        if (connection.encoder && connection.decoder) {
            neverc_qpack_encoder_destroy(connection.encoder);
            neverc_qpack_decoder_destroy(connection.decoder);
            nc_mutex_destroy(&connection.lock);
            nc_cond_destroy(&connection.settings_cond);
        }
        neverc_quic_conn_free(quic);
        return h3_error_response("HTTP/3 peer SETTINGS failed");
    }
    neverc_quic_stream_t *stream = neverc_quic_open_stream(quic, NULL);
    neverc_http_response_t *response = NULL;
    const char *request_error = "HTTP/3 request stream open failed";
    if (!stream) goto client_failed;
    neverc_qpack_header_t headers[8];
    int count = 0;
    headers[count++] = (neverc_qpack_header_t){(char *)":method",
                                               (char *)method};
    headers[count++] = (neverc_qpack_header_t){(char *)":scheme",
                                               (char *)"https"};
    headers[count++] = (neverc_qpack_header_t){(char *)":authority",
                                               authority};
    headers[count++] = (neverc_qpack_header_t){(char *)":path", request_uri};
    char content_length[32];
    if (content_type)
        headers[count++] = (neverc_qpack_header_t){(char *)"content-type",
                                                   (char *)content_type};
    if (body_length) {
        snprintf(content_length, sizeof(content_length), "%zu", body_length);
        headers[count++] = (neverc_qpack_header_t){(char *)"content-length",
                                                   content_length};
    }
    request_error = "HTTP/3 request headers failed";
    if (h3_send_header_section(&connection, stream, headers, count) != 0)
        goto client_failed;
    if (body_length) {
        request_error = "HTTP/3 request body failed";
        uint8_t *frame = (uint8_t *)malloc(H3_DATA_CHUNK + 16U);
        if (!frame) goto client_failed;
        size_t position = 0;
        while (position < body_length) {
            size_t chunk = body_length - position;
            if (chunk > H3_DATA_CHUNK) chunk = H3_DATA_CHUNK;
            size_t frame_length = 0;
            if (neverc_h3_write_data_frame(
                    frame, H3_DATA_CHUNK + 16U,
                    (const uint8_t *)body + position, chunk,
                    &frame_length) != 0 ||
                h3_write_all(stream, frame, frame_length) != 0) {
                free(frame);
                goto client_failed;
            }
            position += chunk;
        }
        free(frame);
    }
    request_error = "HTTP/3 request stream close failed";
    if (neverc_quic_stream_close_write(stream) != 0) goto client_failed;
    request_error = "HTTP/3 response allocation failed";
    response = (neverc_http_response_t *)calloc(1, sizeof(*response));
    if (!response) goto client_failed;
    request_error = "HTTP/3 response is invalid";
    if (h3_client_read_response(&connection, stream, response) != 0) {
        neverc_http_response_free(response);
        response = NULL;
        goto client_failed;
    }
    neverc_quic_stream_free(stream);
    neverc_qpack_encoder_destroy(connection.encoder);
    neverc_qpack_decoder_destroy(connection.decoder);
    nc_mutex_destroy(&connection.lock);
    nc_cond_destroy(&connection.settings_cond);
    neverc_quic_conn_free(quic);
    return response;

client_failed:
    neverc_quic_stream_free(stream);
    neverc_qpack_encoder_destroy(connection.encoder);
    neverc_qpack_decoder_destroy(connection.decoder);
    nc_mutex_destroy(&connection.lock);
    nc_cond_destroy(&connection.settings_cond);
    neverc_quic_conn_free(quic);
    return h3_error_response(request_error);
}

neverc_http3_client_config_t neverc_http3_client_config_default(void) {
    neverc_http3_client_config_t config;
    memset(&config, 0, sizeof(config));
    return config;
}

neverc_http_response_t *neverc_http3_get_with_config(
    const char *url, const neverc_http3_client_config_t *config) {
    return h3_client_request("GET", url, NULL, NULL, 0, config);
}

neverc_http_response_t *neverc_http3_get(const char *url) {
    return neverc_http3_get_with_config(url, NULL);
}

neverc_http_response_t *neverc_http3_post(const char *url,
                                          const char *content_type,
                                          const void *body,
                                          size_t body_length) {
    return neverc_http3_post_with_config(url, content_type, body,
                                          body_length, NULL);
}

neverc_http_response_t *neverc_http3_post_with_config(
    const char *url, const char *content_type,
    const void *body, size_t body_length,
    const neverc_http3_client_config_t *config) {
    return h3_client_request("POST", url, content_type, body, body_length,
                             config);
}

struct neverc_http_unified_server {
    neverc_http_mux_t *mux;
    neverc_http_server_t *http;
    neverc_http3_server_t *http3;
    const char *addr;
    const char *cert_file;
    const char *key_file;
    nc_thread_t tcp_thread;
    int tcp_thread_started;
    int tcp_result;
    _Atomic int bound_port;
    _Atomic int serving;
    _Atomic int running;
    _Atomic int stop_requested;
    nc_mutex_t lock;
    nc_cond_t serve_done;
};

static void *h3_unified_tcp_worker(void *argument) {
    neverc_http_unified_server_t *server =
        (neverc_http_unified_server_t *)argument;
    server->tcp_result = neverc_http_server_listen_and_serve_tls(
        server->http, server->addr, server->cert_file,
        server->key_file);
    neverc_http3_server_stop(server->http3);
    return NULL;
}

neverc_http_unified_server_t *neverc_http_unified_server_create(
    neverc_http_mux_t *mux) {
    neverc_http_unified_server_t *server =
        (neverc_http_unified_server_t *)calloc(1, sizeof(*server));
    if (!server) return NULL;
    server->mux = mux;
    atomic_store_explicit(&server->bound_port, -1, memory_order_release);
    nc_mutex_init(&server->lock);
    nc_cond_init(&server->serve_done);
    return server;
}

void neverc_http_unified_server_shutdown(
    neverc_http_unified_server_t *server) {
    if (!server || !atomic_load_explicit(&server->serving,
                                         memory_order_acquire))
        return;
    atomic_store_explicit(&server->stop_requested, 1,
                          memory_order_release);
    nc_mutex_lock(&server->lock);
    if (server->http3) neverc_http3_server_stop(server->http3);
    if (server->http) neverc_http_server_shutdown(server->http);
    nc_mutex_unlock(&server->lock);
}

void neverc_http_unified_server_destroy(
    neverc_http_unified_server_t *server) {
    if (!server) return;
    neverc_http_unified_server_shutdown(server);
    nc_mutex_lock(&server->lock);
    while (atomic_load_explicit(&server->serving, memory_order_acquire))
        nc_cond_wait(&server->serve_done, &server->lock);
    nc_mutex_unlock(&server->lock);
    nc_cond_destroy(&server->serve_done);
    nc_mutex_destroy(&server->lock);
    free(server);
}

int neverc_http_unified_server_is_running(
    const neverc_http_unified_server_t *server) {
    return server ? atomic_load_explicit(&server->running,
                                          memory_order_acquire) : 0;
}

int neverc_http_unified_server_bound_port(
    const neverc_http_unified_server_t *server) {
    return server ? atomic_load_explicit(&server->bound_port,
                                          memory_order_acquire) : -1;
}

int neverc_http_unified_server_listen_and_serve(
    neverc_http_unified_server_t *server, const char *addr,
    const char *cert_file, const char *key_file) {
    if (!server || !addr || !cert_file || !key_file ||
        atomic_exchange_explicit(&server->serving, 1,
                                 memory_order_acq_rel)) {
        errno = EINVAL;
        return -1;
    }
    atomic_store_explicit(&server->stop_requested, 0, memory_order_release);
    int result = -1;
    char host[256];
    uint16_t port = 0;
    if (nc_parse_addr(addr, host, sizeof(host), &port) != 0 || port == 0) {
        errno = EINVAL;
        goto done;
    }
    char alt_svc[64];
    if (snprintf(alt_svc, sizeof(alt_svc), "h3=\":%u\"; ma=86400",
                 (unsigned)port) >= (int)sizeof(alt_svc))
        goto done;
    neverc_http_server_config_t config = neverc_http_server_config_default();
    config.alt_svc = alt_svc;
    neverc_http_server_t *http = neverc_http_server_new(server->mux, &config);
    neverc_http3_server_t *http3 = neverc_http3_server_create(server->mux);
    if (!http || !http3) {
        neverc_http_server_free(http);
        neverc_http3_server_destroy(http3);
        goto done;
    }

    nc_mutex_lock(&server->lock);
    server->http = http;
    server->http3 = http3;
    server->addr = addr;
    server->cert_file = cert_file;
    server->key_file = key_file;
    server->tcp_result = -1;
    atomic_store_explicit(&server->bound_port, (int)port,
                          memory_order_release);
    atomic_store_explicit(&server->running, 1, memory_order_release);
    nc_mutex_unlock(&server->lock);

    if (nc_thread_create(&server->tcp_thread, h3_unified_tcp_worker,
                         server) != 0) {
        atomic_store_explicit(&server->running, 0, memory_order_release);
        goto release_servers;
    }
    server->tcp_thread_started = 1;
    if (atomic_load_explicit(&server->stop_requested, memory_order_acquire))
        neverc_http_unified_server_shutdown(server);
    int h3_result = neverc_http3_listen_and_serve(
        addr, http3, cert_file, key_file);
    neverc_http_server_shutdown(http);
    (void)nc_thread_join(server->tcp_thread);
    server->tcp_thread_started = 0;
    atomic_store_explicit(&server->running, 0, memory_order_release);
    result = h3_result == 0 && server->tcp_result == 0 ? 0 : -1;

release_servers:
    nc_mutex_lock(&server->lock);
    server->http = NULL;
    server->http3 = NULL;
    server->addr = NULL;
    server->cert_file = NULL;
    server->key_file = NULL;
    nc_mutex_unlock(&server->lock);
    neverc_http3_server_destroy(http3);
    neverc_http_server_free(http);

done:
    atomic_store_explicit(&server->running, 0, memory_order_release);
    atomic_store_explicit(&server->serving, 0, memory_order_release);
    nc_mutex_lock(&server->lock);
    nc_cond_broadcast(&server->serve_done);
    nc_mutex_unlock(&server->lock);
    return result;
}

int neverc_http_serve_all(const char *addr, neverc_http_mux_t *mux,
                          const char *cert_file, const char *key_file) {
    neverc_http_unified_server_t *server =
        neverc_http_unified_server_create(mux);
    if (!server) return -1;
    int result = neverc_http_unified_server_listen_and_serve(
        server, addr, cert_file, key_file);
    neverc_http_unified_server_destroy(server);
    return result;
}

neverc_http_unified_server_t *neverc_http3_unified_server_create(
    neverc_http_mux_t *mux) {
    return neverc_http_unified_server_create(mux);
}

void neverc_http3_unified_server_destroy(
    neverc_http_unified_server_t *server) {
    neverc_http_unified_server_destroy(server);
}

int neverc_http3_unified_server_listen_and_serve(
    neverc_http_unified_server_t *server, const char *addr,
    const char *cert_file, const char *key_file) {
    return neverc_http_unified_server_listen_and_serve(
        server, addr, cert_file, key_file);
}

void neverc_http3_unified_server_shutdown(
    neverc_http_unified_server_t *server) {
    neverc_http_unified_server_shutdown(server);
}

int neverc_http3_unified_server_is_running(
    const neverc_http_unified_server_t *server) {
    return neverc_http_unified_server_is_running(server);
}

int neverc_http3_unified_server_bound_port(
    const neverc_http_unified_server_t *server) {
    return neverc_http_unified_server_bound_port(server);
}

int neverc_http3_serve_all(const char *addr, neverc_http_mux_t *mux,
                           const char *cert_file, const char *key_file) {
    return neverc_http_serve_all(addr, mux, cert_file, key_file);
}
