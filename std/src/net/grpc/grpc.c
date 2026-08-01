#include "neverc/std/net/grpc.h"

#include "neverc/std/encoding/base64.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GRPC_DEFAULT_MAX_MESSAGE_SIZE (4U * 1024U * 1024U)
#define GRPC_MAX_STATUS_MESSAGE_SIZE 1024U
#define GRPC_MAX_CLIENT_METADATA 60U

struct neverc_grpc_server_stream {
    const neverc_grpc_method_t *method;
    neverc_http_request_t *request;
    neverc_http_response_writer_t *writer;
    neverc_grpc_frame_reader_t reader;
    neverc_context_t *context;
    neverc_context_cancel_handle_t *cancel;
    size_t received_count;
    size_t sent_count;
    uint8_t *received_message;
    int input_eof;
    int ended;
};

static int64_t grpc_now_ms(void) {
    struct timespec time_value;
    if (timespec_get(&time_value, TIME_UTC) != TIME_UTC) return 0;
    return (int64_t)time_value.tv_sec * 1000 +
           time_value.tv_nsec / 1000000;
}

int neverc_grpc_status_valid(uint32_t status) {
    return status <= NEVERC_GRPC_UNAUTHENTICATED;
}

const char *neverc_grpc_status_name(neverc_grpc_status_t status) {
    static const char *names[] = {
        "OK", "CANCELLED", "UNKNOWN", "INVALID_ARGUMENT",
        "DEADLINE_EXCEEDED", "NOT_FOUND", "ALREADY_EXISTS",
        "PERMISSION_DENIED", "RESOURCE_EXHAUSTED", "FAILED_PRECONDITION",
        "ABORTED", "OUT_OF_RANGE", "UNIMPLEMENTED", "INTERNAL",
        "UNAVAILABLE", "DATA_LOSS", "UNAUTHENTICATED"};
    return neverc_grpc_status_valid((uint32_t)status)
        ? names[(uint32_t)status] : "UNKNOWN";
}

void neverc_grpc_frame_reader_init(neverc_grpc_frame_reader_t *reader,
                                    const void *data, size_t length,
                                    size_t max_message_size) {
    if (!reader) return;
    reader->data = (const uint8_t *)data;
    reader->length = data ? length : 0;
    reader->offset = 0;
    reader->max_message_size = max_message_size
        ? max_message_size : GRPC_DEFAULT_MAX_MESSAGE_SIZE;
}

int neverc_grpc_frame_reader_next(neverc_grpc_frame_reader_t *reader,
                                   neverc_grpc_message_t *message) {
    if (!reader || !message || (!reader->data && reader->length > 0) ||
        reader->offset > reader->length)
        return -1;
    if (reader->offset == reader->length) return 0;
    if (reader->length - reader->offset < 5) return -1;
    const uint8_t *header = reader->data + reader->offset;
    if (header[0] != 0) return -1;
    uint32_t length = ((uint32_t)header[1] << 24) |
                      ((uint32_t)header[2] << 16) |
                      ((uint32_t)header[3] << 8) | header[4];
    if (length > reader->max_message_size ||
        length > reader->length - reader->offset - 5)
        return -1;
    message->data = header + 5;
    message->length = length;
    reader->offset += 5 + length;
    return 1;
}

int neverc_grpc_frame_encode(const void *message, size_t message_length,
                              int compressed, void *output,
                              size_t output_capacity, size_t *output_length) {
    if (output_length) *output_length = 0;
    if ((message_length > 0 && !message) || !output || !output_length ||
        (compressed != 0 && compressed != 1) ||
        message_length > UINT32_MAX || output_capacity < 5 ||
        message_length > output_capacity - 5)
        return -1;
    uint8_t *encoded = (uint8_t *)output;
    encoded[0] = (uint8_t)compressed;
    encoded[1] = (uint8_t)(message_length >> 24);
    encoded[2] = (uint8_t)(message_length >> 16);
    encoded[3] = (uint8_t)(message_length >> 8);
    encoded[4] = (uint8_t)message_length;
    if (message_length > 0)
        memcpy(encoded + 5, message, message_length);
    *output_length = message_length + 5;
    return 0;
}

int neverc_grpc_timeout_encode(uint64_t timeout_ns, char output[10]) {
    if (!output) return -1;
    static const struct {
        uint64_t nanoseconds;
        char suffix;
    } units[] = {
        {1, 'n'}, {1000, 'u'}, {1000000, 'm'}, {1000000000, 'S'},
        {UINT64_C(60000000000), 'M'}, {UINT64_C(3600000000000), 'H'}};
    if (timeout_ns == 0) timeout_ns = 1;
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        uint64_t unit = units[i].nanoseconds;
        uint64_t value = timeout_ns / unit +
                         (timeout_ns % unit != 0);
        if (value <= 99999999) {
            int length = snprintf(output, 10, "%llu%c",
                                  (unsigned long long)value,
                                  units[i].suffix);
            return length > 0 && length < 10 ? 0 : -1;
        }
    }
    memcpy(output, "99999999H", 10);
    return 0;
}

int neverc_grpc_timeout_decode(const char *value, int64_t *timeout_ms) {
    if (timeout_ms) *timeout_ms = 0;
    if (!value || !timeout_ms) return -1;
    size_t length = strlen(value);
    if (length < 2 || length > 9) return -1;
    uint64_t number = 0;
    for (size_t i = 0; i + 1 < length; i++) {
        if (value[i] < '0' || value[i] > '9') return -1;
        number = number * 10 + (uint64_t)(value[i] - '0');
    }
    uint64_t nanoseconds;
    switch (value[length - 1]) {
    case 'n': nanoseconds = 1; break;
    case 'u': nanoseconds = 1000; break;
    case 'm': nanoseconds = 1000000; break;
    case 'S': nanoseconds = 1000000000; break;
    case 'M': nanoseconds = UINT64_C(60000000000); break;
    case 'H': nanoseconds = UINT64_C(3600000000000); break;
    default: return -1;
    }
    if (number > UINT64_MAX / nanoseconds) return -1;
    nanoseconds *= number;
    uint64_t milliseconds = nanoseconds / 1000000 +
                            (nanoseconds % 1000000 != 0);
    if (milliseconds > INT64_MAX) return -1;
    *timeout_ms = (int64_t)milliseconds;
    return 0;
}

static int grpc_content_type_valid(const char *content_type) {
    static const char prefix[] = "application/grpc";
    if (!content_type ||
        strncmp(content_type, prefix, sizeof(prefix) - 1) != 0)
        return 0;
    char next = content_type[sizeof(prefix) - 1];
    return next == '\0' || next == '+' || next == ';';
}

static int grpc_cardinality_valid(neverc_grpc_cardinality_t cardinality) {
    return cardinality >= NEVERC_GRPC_UNARY &&
           cardinality <= NEVERC_GRPC_BIDI_STREAMING;
}

static int grpc_method_valid(const neverc_grpc_method_t *method) {
    if (!method || !method->full_method || method->full_method[0] != '/' ||
        !method->handler || !grpc_cardinality_valid(method->cardinality))
        return 0;
    const char *separator = strchr(method->full_method + 1, '/');
    return separator && separator[1] && !strchr(separator + 1, '/') &&
           !strchr(method->full_method, ' ') &&
           !strchr(method->full_method, '\r') &&
           !strchr(method->full_method, '\n');
}

static neverc_grpc_status_t grpc_context_status(neverc_context_t *context) {
    const char *error = neverc_context_err(context);
    return error && strcmp(error, "context deadline exceeded") == 0
        ? NEVERC_GRPC_DEADLINE_EXCEEDED : NEVERC_GRPC_CANCELLED;
}

static char *grpc_percent_encode(const char *message) {
    if (!message || !message[0]) return NULL;
    size_t input_length = strlen(message);
    if (input_length > GRPC_MAX_STATUS_MESSAGE_SIZE)
        input_length = GRPC_MAX_STATUS_MESSAGE_SIZE;
    if (input_length > (SIZE_MAX - 1) / 3) return NULL;
    char *encoded = (char *)malloc(input_length * 3 + 1);
    if (!encoded) return NULL;
    static const char hex[] = "0123456789ABCDEF";
    size_t output = 0;
    for (size_t i = 0; i < input_length; i++) {
        unsigned char c = (unsigned char)message[i];
        if (c >= 0x20 && c <= 0x7e && c != '%') {
            encoded[output++] = (char)c;
        } else {
            encoded[output++] = '%';
            encoded[output++] = hex[c >> 4];
            encoded[output++] = hex[c & 15];
        }
    }
    encoded[output] = '\0';
    return encoded;
}

static int grpc_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static char *grpc_percent_decode(const char *message) {
    if (!message) return NULL;
    size_t length = strlen(message);
    char *decoded = (char *)malloc(length + 1);
    if (!decoded) return NULL;
    size_t output = 0;
    for (size_t i = 0; i < length; i++) {
        if (message[i] == '%' && i + 2 < length) {
            int high = grpc_hex_value(message[i + 1]);
            int low = grpc_hex_value(message[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded[output++] = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        decoded[output++] = message[i];
    }
    decoded[output] = '\0';
    return decoded;
}

neverc_context_t *neverc_grpc_server_stream_context(
    neverc_grpc_server_stream_t *stream) {
    return stream ? stream->context : NULL;
}

const char *neverc_grpc_server_stream_metadata(
    neverc_grpc_server_stream_t *stream, const char *key) {
    return stream ? neverc_http_request_header(stream->request, key) : NULL;
}

static int grpc_h2_read_exact(neverc_grpc_server_stream_t *stream,
                              void *output, size_t length,
                              int allow_clean_eof) {
    uint8_t *cursor = (uint8_t *)output;
    size_t offset = 0;
    while (offset < length) {
        int count = neverc_h2_request_stream_read(
            stream->request->protocol_stream, stream->context,
            cursor + offset, length - offset);
        if (count == 0)
            return allow_clean_eof && offset == 0 ? 0 : -1;
        if (count < 0) return -1;
        offset += (size_t)count;
    }
    return 1;
}

static int grpc_server_stream_recv_internal(
    neverc_grpc_server_stream_t *stream, neverc_grpc_message_t *message) {
    if (!stream || !message || neverc_context_done(stream->context))
        return -1;
    free(stream->received_message);
    stream->received_message = NULL;
    int result;
    if (stream->request->protocol_stream) {
        uint8_t header[5];
        result = grpc_h2_read_exact(stream, header, sizeof(header), 1);
        if (result == 0) {
            stream->input_eof = 1;
            return 0;
        }
        if (result < 0 || header[0] != 0) return -1;
        uint32_t length = ((uint32_t)header[1] << 24) |
                          ((uint32_t)header[2] << 16) |
                          ((uint32_t)header[3] << 8) | header[4];
        size_t max_message_size = stream->method->max_request_message_size
            ? stream->method->max_request_message_size
            : GRPC_DEFAULT_MAX_MESSAGE_SIZE;
        if (length > max_message_size) return -1;
        stream->received_message = length
            ? (uint8_t *)malloc(length) : NULL;
        if (length && !stream->received_message) return -1;
        if (length && grpc_h2_read_exact(
                stream, stream->received_message, length, 0) != 1)
            return -1;
        message->data = stream->received_message;
        message->length = length;
        result = 1;
    } else {
        result = neverc_grpc_frame_reader_next(&stream->reader, message);
        if (result == 0) stream->input_eof = 1;
    }
    if (result == 1) {
        stream->received_count++;
        if ((stream->method->cardinality == NEVERC_GRPC_UNARY ||
             stream->method->cardinality == NEVERC_GRPC_SERVER_STREAMING) &&
            stream->received_count > 1)
            return -1;
    }
    return result;
}

int neverc_grpc_server_stream_recv(neverc_grpc_server_stream_t *stream,
                                    neverc_grpc_message_t *message) {
    if (!stream || stream->ended) return -1;
    return grpc_server_stream_recv_internal(stream, message);
}

int neverc_grpc_server_stream_send(neverc_grpc_server_stream_t *stream,
                                    const void *message,
                                    size_t message_length) {
    if (!stream || stream->ended || (message_length > 0 && !message) ||
        neverc_context_done(stream->context))
        return -1;
    size_t max_message_size = stream->method->max_response_message_size
        ? stream->method->max_response_message_size
        : GRPC_DEFAULT_MAX_MESSAGE_SIZE;
    if (message_length > max_message_size || message_length > INT_MAX - 5)
        return -1;
    if ((stream->method->cardinality == NEVERC_GRPC_UNARY ||
         stream->method->cardinality == NEVERC_GRPC_CLIENT_STREAMING) &&
        stream->sent_count != 0)
        return -1;
    if (message_length > SIZE_MAX - 5) return -1;
    uint8_t *framed = (uint8_t *)malloc(message_length + 5);
    if (!framed) return -1;
    size_t framed_length = 0;
    int result = neverc_grpc_frame_encode(
        message, message_length, 0, framed, message_length + 5,
        &framed_length);
    if (result == 0 &&
        neverc_http_write(stream->writer, framed, framed_length) !=
            (int)framed_length)
        result = -1;
    if (result == 0)
        result = neverc_http_flush_chunk(stream->writer);
    free(framed);
    if (result == 0) stream->sent_count++;
    return result;
}

void neverc_grpc_server_stream_set_header(
    neverc_grpc_server_stream_t *stream, const char *name,
    const char *value) {
    if (stream && !stream->ended)
        neverc_http_set_header(stream->writer, name, value);
}

void neverc_grpc_server_stream_set_trailer(
    neverc_grpc_server_stream_t *stream, const char *name,
    const char *value) {
    if (stream && !stream->ended)
        neverc_http_set_trailer(stream->writer, name, value);
}

int neverc_grpc_server_stream_end(neverc_grpc_server_stream_t *stream,
                                   neverc_grpc_status_t status,
                                   const char *message) {
    if (!stream || stream->ended ||
        !neverc_grpc_status_valid((uint32_t)status))
        return -1;
    if (neverc_context_done(stream->context))
        status = grpc_context_status(stream->context);
    char status_value[3];
    if (snprintf(status_value, sizeof(status_value), "%u",
                 (unsigned)status) <= 0)
        return -1;
    neverc_http_set_trailer(stream->writer, "grpc-status", status_value);
    char *encoded_message = grpc_percent_encode(message);
    if (encoded_message) {
        neverc_http_set_trailer(stream->writer, "grpc-message",
                                encoded_message);
        free(encoded_message);
    }
    int result = neverc_http_end_chunked(stream->writer);
    stream->ended = result == 0;
    return result;
}

static void grpc_server_dispatch(neverc_http_request_t *request,
                                 neverc_http_response_writer_t *writer,
                                 void *context) {
    const neverc_grpc_method_t *method =
        (const neverc_grpc_method_t *)context;
    if (!grpc_content_type_valid(request->content_type)) {
        neverc_http_set_status(writer, 415);
        return;
    }
    const char *te = neverc_http_request_header(request, "te");
    const char *encoding = neverc_http_request_header(
        request, "grpc-encoding");
    if (!te || strcmp(te, "trailers") != 0 ||
        (encoding && strcmp(encoding, "identity") != 0)) {
        neverc_http_set_status(writer, 200);
        neverc_http_set_header(writer, "content-type", "application/grpc");
        neverc_http_enable_chunked(writer);
        neverc_grpc_server_stream_t rejected = {
            .method = method, .request = request, .writer = writer,
            .context = request->context};
        (void)neverc_grpc_server_stream_end(
            &rejected, encoding ? NEVERC_GRPC_UNIMPLEMENTED
                                : NEVERC_GRPC_INVALID_ARGUMENT,
            encoding ? "unsupported grpc-encoding"
                     : "te: trailers is required");
        return;
    }

    neverc_context_t *call_context = request->context;
    neverc_context_cancel_handle_t *call_cancel = NULL;
    int owns_context = 0;
    const char *timeout = neverc_http_request_header(
        request, "grpc-timeout");
    if (timeout) {
        int64_t timeout_ms = 0;
        if (neverc_grpc_timeout_decode(timeout, &timeout_ms) != 0) {
            neverc_http_set_status(writer, 200);
            neverc_http_set_header(writer, "content-type",
                                   "application/grpc");
            neverc_http_enable_chunked(writer);
            neverc_grpc_server_stream_t rejected = {
                .method = method, .request = request, .writer = writer,
                .context = request->context};
            (void)neverc_grpc_server_stream_end(
                &rejected, NEVERC_GRPC_INVALID_ARGUMENT,
                "invalid grpc-timeout");
            return;
        }
        call_context = neverc_context_with_timeout_handle(
            request->context, timeout_ms, &call_cancel);
        owns_context = call_context != NULL && call_cancel != NULL;
        if (!owns_context) call_context = request->context;
    }

    size_t request_count = 0;
    int framing_valid = 1;
    if (!request->protocol_stream) {
        neverc_grpc_frame_reader_t validator;
        neverc_grpc_frame_reader_init(
            &validator, request->body, request->body_len,
            method->max_request_message_size);
        for (;;) {
            neverc_grpc_message_t ignored;
            int next = neverc_grpc_frame_reader_next(&validator, &ignored);
            if (next == 0) break;
            if (next < 0) {
                framing_valid = 0;
                break;
            }
            request_count++;
        }
        if ((method->cardinality == NEVERC_GRPC_UNARY ||
             method->cardinality == NEVERC_GRPC_SERVER_STREAMING) &&
            request_count != 1)
            framing_valid = 0;
    }

    neverc_http_set_status(writer, 200);
    neverc_http_set_header(writer, "content-type", "application/grpc");
    neverc_http_set_header(writer, "grpc-encoding", "identity");
    neverc_http_enable_chunked(writer);
    neverc_grpc_server_stream_t stream;
    memset(&stream, 0, sizeof(stream));
    stream.method = method;
    stream.request = request;
    stream.writer = writer;
    stream.context = call_context;
    stream.cancel = call_cancel;
    neverc_grpc_frame_reader_init(
        &stream.reader, request->body, request->body_len,
        method->max_request_message_size);

    neverc_grpc_status_t status = framing_valid
        ? method->handler(&stream, method->context)
        : NEVERC_GRPC_INVALID_ARGUMENT;
    if (request->protocol_stream && framing_valid) {
        for (;;) {
            neverc_grpc_message_t ignored;
            int next = grpc_server_stream_recv_internal(&stream, &ignored);
            if (next == 0) break;
            if (next < 0) {
                framing_valid = 0;
                break;
            }
        }
        if ((method->cardinality == NEVERC_GRPC_UNARY ||
             method->cardinality == NEVERC_GRPC_SERVER_STREAMING) &&
            stream.received_count != 1)
            framing_valid = 0;
        if (!framing_valid) {
            status = NEVERC_GRPC_INVALID_ARGUMENT;
            if (!stream.input_eof)
                neverc_h2_request_stream_cancel(
                    request->protocol_stream, NC_H2_PROTOCOL_ERROR);
        }
    }
    if (!neverc_grpc_status_valid((uint32_t)status))
        status = NEVERC_GRPC_INTERNAL;
    if (!stream.ended)
        (void)neverc_grpc_server_stream_end(
            &stream, status, framing_valid ? NULL : "invalid request framing");
    free(stream.received_message);
    if (owns_context) {
        neverc_context_cancel_handle_cancel(call_cancel);
        neverc_context_cancel_handle_free(call_cancel);
        neverc_context_free(call_context);
    }
}

int neverc_grpc_server_register(neverc_http_mux_t *mux,
                                 const neverc_grpc_method_t *method) {
    if (!mux || !grpc_method_valid(method)) return -1;
    size_t length = strlen(method->full_method);
    if (length > SIZE_MAX - 6) return -1;
    char *pattern = (char *)malloc(length + 6);
    if (!pattern) return -1;
    memcpy(pattern, "POST ", 5);
    memcpy(pattern + 5, method->full_method, length + 1);
    int result = neverc_http_mux_handle_stream_context(
        mux, pattern, grpc_server_dispatch, (void *)method);
    free(pattern);
    return result;
}

static int grpc_metadata_key_valid(const char *key) {
    if (!key || !key[0] || key[0] == ':') return 0;
    for (const unsigned char *cursor = (const unsigned char *)key;
         *cursor; cursor++)
        if (!(isalnum(*cursor) && !isupper(*cursor)) &&
            *cursor != '-' && *cursor != '_' && *cursor != '.')
            return 0;
    return strcmp(key, "content-type") != 0 &&
           strcmp(key, "te") != 0 &&
           strcmp(key, "grpc-timeout") != 0 &&
           strcmp(key, "grpc-encoding") != 0;
}

static int grpc_binary_key(const char *key) {
    size_t length = strlen(key);
    return length >= 4 && strcmp(key + length - 4, "-bin") == 0;
}

static const char *grpc_find_header(neverc_hpack_header_t *headers,
                                    size_t count, const char *name,
                                    size_t *matches) {
    const char *value = NULL;
    *matches = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(headers[i].name, name) == 0) {
            value = headers[i].value;
            (*matches)++;
        }
    }
    return value;
}

static int grpc_parse_status(const char *value,
                             neverc_grpc_status_t *status) {
    if (!value || !status || !value[0]) return -1;
    uint32_t number = 0;
    size_t length = strlen(value);
    if (length > 2) return -1;
    for (size_t i = 0; i < length; i++) {
        if (value[i] < '0' || value[i] > '9') return -1;
        number = number * 10 + (uint32_t)(value[i] - '0');
    }
    if (!neverc_grpc_status_valid(number)) return -1;
    *status = (neverc_grpc_status_t)number;
    return 0;
}

static int grpc_parse_messages(const uint8_t *body, size_t body_length,
                               size_t max_message_size,
                               neverc_grpc_result_t *result) {
    neverc_grpc_frame_reader_t reader;
    neverc_grpc_frame_reader_init(
        &reader, body, body_length, max_message_size);
    size_t count = 0;
    for (;;) {
        neverc_grpc_message_t message;
        int next = neverc_grpc_frame_reader_next(&reader, &message);
        if (next == 0) break;
        if (next < 0) return -1;
        count++;
    }
    neverc_grpc_message_t *messages = count
        ? (neverc_grpc_message_t *)calloc(count, sizeof(*messages)) : NULL;
    if (count && !messages) return -1;
    neverc_grpc_frame_reader_init(
        &reader, body, body_length, max_message_size);
    for (size_t i = 0; i < count; i++) {
        neverc_grpc_message_t view;
        if (neverc_grpc_frame_reader_next(&reader, &view) != 1) {
            free(messages);
            return -1;
        }
        uint8_t *copy = view.length ? (uint8_t *)malloc(view.length) : NULL;
        if (view.length && !copy) {
            for (size_t j = 0; j < i; j++)
                free((void *)messages[j].data);
            free(messages);
            return -1;
        }
        if (view.length) memcpy(copy, view.data, view.length);
        messages[i].data = copy;
        messages[i].length = view.length;
    }
    result->messages = messages;
    result->message_count = count;
    return 0;
}

struct neverc_grpc_client_stream {
    neverc_h2_client_stream_t *transport;
    neverc_grpc_cardinality_t cardinality;
    size_t max_response_message_size;
    size_t sent_count;
    size_t received_count;
    uint8_t *buffer;
    size_t buffer_length;
    size_t buffer_capacity;
    size_t buffer_offset;
    uint8_t *received_message;
    neverc_grpc_status_t status;
    char *status_message;
    const char *error;
    neverc_hpack_header_t *headers;
    size_t header_count;
    neverc_hpack_header_t *trailers;
    size_t trailer_count;
    int headers_received;
    int trailers_received;
    int send_closed;
    int receive_closed;
};

typedef struct {
    neverc_hpack_header_t headers[GRPC_MAX_CLIENT_METADATA + 3];
    char *owned_values[GRPC_MAX_CLIENT_METADATA + 1];
    size_t count;
} grpc_client_header_block_t;

static void grpc_client_header_block_free(
    grpc_client_header_block_t *block) {
    for (size_t i = 0; i < GRPC_MAX_CLIENT_METADATA + 1; i++)
        free(block->owned_values[i]);
}

static int grpc_client_header_block_build(
    grpc_client_header_block_t *block, neverc_context_t *context,
    const neverc_grpc_metadata_t *metadata, size_t metadata_count) {
    memset(block, 0, sizeof(*block));
    if (metadata_count > GRPC_MAX_CLIENT_METADATA ||
        (metadata_count && !metadata))
        return -1;
    block->headers[block->count++] = (neverc_hpack_header_t){
        .name = "content-type", .value = "application/grpc+proto"};
    block->headers[block->count++] = (neverc_hpack_header_t){
        .name = "te", .value = "trailers"};
    for (size_t i = 0; i < metadata_count; i++) {
        if (!grpc_metadata_key_valid(metadata[i].key) ||
            (metadata[i].value_length && !metadata[i].value) ||
            metadata[i].value_length > 8192)
            return -1;
        size_t capacity;
        size_t length;
        if (grpc_binary_key(metadata[i].key)) {
            capacity = neverc_base64_encoded_len(metadata[i].value_length);
            if (capacity > 16384) return -1;
            block->owned_values[i] = (char *)malloc(capacity + 1);
            if (!block->owned_values[i]) return -1;
            length = neverc_base64_encode(
                block->owned_values[i], metadata[i].value,
                metadata[i].value_length);
        } else {
            capacity = metadata[i].value_length;
            block->owned_values[i] = (char *)malloc(capacity + 1);
            if (!block->owned_values[i]) return -1;
            memcpy(block->owned_values[i], metadata[i].value, capacity);
            length = capacity;
            for (size_t j = 0; j < length; j++) {
                unsigned char c = (unsigned char)block->owned_values[i][j];
                if (c < 0x20 || c == 0x7f) return -1;
            }
        }
        block->owned_values[i][length] = '\0';
        block->headers[block->count++] = (neverc_hpack_header_t){
            .name = (char *)metadata[i].key,
            .value = block->owned_values[i]};
    }
    int64_t deadline = context ? neverc_context_deadline(context) : 0;
    if (deadline > 0) {
        int64_t remaining_ms = deadline - grpc_now_ms();
        uint64_t timeout_ns = remaining_ms <= 0 ? 1 :
            (uint64_t)remaining_ms > UINT64_MAX / 1000000
                ? UINT64_MAX : (uint64_t)remaining_ms * 1000000;
        char encoded_timeout[10];
        if (neverc_grpc_timeout_encode(
                timeout_ns, encoded_timeout) != 0)
            return -1;
        size_t slot = metadata_count;
        block->owned_values[slot] = strdup(encoded_timeout);
        if (!block->owned_values[slot]) return -1;
        block->headers[block->count++] = (neverc_hpack_header_t){
            .name = "grpc-timeout",
            .value = block->owned_values[slot]};
    }
    return 0;
}

neverc_grpc_client_stream_t *neverc_grpc_client_stream_open(
    neverc_h2_client_t *client, neverc_context_t *context,
    const char *full_method, neverc_grpc_cardinality_t cardinality,
    const neverc_grpc_metadata_t *metadata, size_t metadata_count,
    size_t max_response_message_size, const char **error) {
    if (error) *error = NULL;
    if (!client || !full_method || full_method[0] != '/' ||
        !grpc_cardinality_valid(cardinality)) {
        if (error) *error = "invalid gRPC stream";
        return NULL;
    }
    grpc_client_header_block_t header_block;
    if (grpc_client_header_block_build(
            &header_block, context, metadata, metadata_count) != 0) {
        grpc_client_header_block_free(&header_block);
        if (error) *error = "invalid gRPC metadata";
        return NULL;
    }
    neverc_grpc_client_stream_t *stream =
        (neverc_grpc_client_stream_t *)calloc(1, sizeof(*stream));
    if (!stream) {
        grpc_client_header_block_free(&header_block);
        if (error) *error = "out of memory";
        return NULL;
    }
    stream->cardinality = cardinality;
    stream->max_response_message_size = max_response_message_size
        ? max_response_message_size : GRPC_DEFAULT_MAX_MESSAGE_SIZE;
    stream->status = NEVERC_GRPC_UNKNOWN;
    stream->transport = neverc_h2_client_stream_open(
        client, context, "POST", full_method, header_block.headers,
        header_block.count, 0, error);
    grpc_client_header_block_free(&header_block);
    if (!stream->transport) {
        free(stream);
        return NULL;
    }
    return stream;
}

int neverc_grpc_client_stream_send(
    neverc_grpc_client_stream_t *stream, neverc_context_t *context,
    const void *message, size_t message_length) {
    if (!stream || !stream->transport || stream->send_closed ||
        stream->error || (message_length && !message) ||
        message_length > GRPC_DEFAULT_MAX_MESSAGE_SIZE ||
        message_length > SIZE_MAX - 5)
        return -1;
    if ((stream->cardinality == NEVERC_GRPC_UNARY ||
         stream->cardinality == NEVERC_GRPC_SERVER_STREAMING) &&
        stream->sent_count != 0)
        return -1;
    uint8_t *framed = (uint8_t *)malloc(message_length + 5);
    if (!framed) return -1;
    size_t framed_length = 0;
    int result = neverc_grpc_frame_encode(
        message, message_length, 0, framed, message_length + 5,
        &framed_length);
    if (result == 0)
        result = neverc_h2_client_stream_send(
            stream->transport, context, framed, framed_length, 0);
    free(framed);
    if (result == 0) {
        stream->sent_count++;
    } else {
        stream->error = "gRPC stream send failed";
    }
    return result;
}

int neverc_grpc_client_stream_close_send(
    neverc_grpc_client_stream_t *stream, neverc_context_t *context) {
    if (!stream || !stream->transport || stream->send_closed)
        return -1;
    if ((stream->cardinality == NEVERC_GRPC_UNARY ||
         stream->cardinality == NEVERC_GRPC_SERVER_STREAMING) &&
        stream->sent_count != 1) {
        stream->error = "gRPC unary request count is not one";
        neverc_h2_client_stream_cancel(stream->transport, NC_H2_CANCEL);
        return -1;
    }
    int result = neverc_h2_client_stream_send(
        stream->transport, context, NULL, 0, 1);
    if (result == 0) {
        stream->send_closed = 1;
    } else {
        stream->error = "gRPC stream half-close failed";
    }
    return result;
}

static int grpc_client_stream_append(neverc_grpc_client_stream_t *stream,
                                     const uint8_t *data, size_t length) {
    if (stream->buffer_offset > 0) {
        size_t remaining = stream->buffer_length - stream->buffer_offset;
        if (remaining)
            memmove(stream->buffer, stream->buffer + stream->buffer_offset,
                    remaining);
        stream->buffer_length = remaining;
        stream->buffer_offset = 0;
    }
    size_t maximum = stream->max_response_message_size;
    if (maximum > SIZE_MAX - NC_H2_DEFAULT_MAX_FRAME_SIZE - 5)
        maximum = SIZE_MAX;
    else
        maximum += NC_H2_DEFAULT_MAX_FRAME_SIZE + 5;
    if (length > maximum || stream->buffer_length > maximum - length)
        return -1;
    size_t needed = stream->buffer_length + length;
    if (needed > stream->buffer_capacity) {
        size_t capacity = stream->buffer_capacity
            ? stream->buffer_capacity : NC_H2_DEFAULT_MAX_FRAME_SIZE;
        while (capacity < needed) {
            if (capacity > maximum / 2) {
                capacity = maximum;
                break;
            }
            capacity *= 2;
        }
        uint8_t *resized = (uint8_t *)realloc(stream->buffer, capacity);
        if (!resized) return -1;
        stream->buffer = resized;
        stream->buffer_capacity = capacity;
    }
    if (length)
        memcpy(stream->buffer + stream->buffer_length, data, length);
    stream->buffer_length += length;
    return 0;
}

static int grpc_client_stream_next_buffered(
    neverc_grpc_client_stream_t *stream, neverc_grpc_message_t *message) {
    size_t available = stream->buffer_length - stream->buffer_offset;
    if (available < 5) return 0;
    const uint8_t *header = stream->buffer + stream->buffer_offset;
    if (header[0] != 0) return -1;
    uint32_t length = ((uint32_t)header[1] << 24) |
                      ((uint32_t)header[2] << 16) |
                      ((uint32_t)header[3] << 8) | header[4];
    if (length > stream->max_response_message_size) return -1;
    if (available - 5 < length) return 0;
    free(stream->received_message);
    stream->received_message = length
        ? (uint8_t *)malloc(length) : NULL;
    if (length && !stream->received_message) return -1;
    if (length)
        memcpy(stream->received_message, header + 5, length);
    stream->buffer_offset += length + 5;
    message->data = stream->received_message;
    message->length = length;
    stream->received_count++;
    if ((stream->cardinality == NEVERC_GRPC_UNARY ||
         stream->cardinality == NEVERC_GRPC_CLIENT_STREAMING) &&
        stream->received_count > 1)
        return -1;
    return 1;
}

static int grpc_client_stream_parse_trailers(
    neverc_grpc_client_stream_t *stream,
    neverc_h2_client_event_t *event) {
    size_t status_matches = 0;
    const char *status_value = grpc_find_header(
        event->headers, event->header_count,
        "grpc-status", &status_matches);
    if (status_matches != 1 ||
        grpc_parse_status(status_value, &stream->status) != 0)
        return -1;
    size_t message_matches = 0;
    const char *message = grpc_find_header(
        event->headers, event->header_count,
        "grpc-message", &message_matches);
    if (message_matches > 1) return -1;
    if (message_matches == 1) {
        stream->status_message = grpc_percent_decode(message);
        if (!stream->status_message) return -1;
    }
    stream->trailers_received = 1;
    return 0;
}

int neverc_grpc_client_stream_receive(
    neverc_grpc_client_stream_t *stream, neverc_context_t *context,
    neverc_grpc_message_t *message) {
    if (!stream || !stream->transport || !message || stream->error)
        return -1;
    if (stream->receive_closed) return 0;
    int buffered = grpc_client_stream_next_buffered(stream, message);
    if (buffered != 0) {
        if (buffered < 0)
            stream->error = "malformed gRPC response message";
        return buffered;
    }
    for (;;) {
        neverc_h2_client_event_t *event = NULL;
        int received = neverc_h2_client_stream_receive(
            stream->transport, context, &event);
        if (received <= 0) {
            if (received < 0)
                stream->error = "gRPC response receive cancelled";
            else if (!stream->receive_closed)
                stream->error = "gRPC response closed without END";
            return received < 0 ? -1 : 0;
        }
        int result = 0;
        if (event->type == NEVERC_H2_CLIENT_EVENT_HEADERS) {
            size_t matches = 0;
            const char *content_type = grpc_find_header(
                event->headers, event->header_count,
                "content-type", &matches);
            if (stream->headers_received || event->status_code != 200 ||
                matches != 1 || !grpc_content_type_valid(content_type)) {
                stream->error = "invalid gRPC response headers";
                result = -1;
            } else {
                stream->headers_received = 1;
                stream->headers = event->headers;
                stream->header_count = event->header_count;
                event->headers = NULL;
                event->header_count = 0;
            }
        } else if (event->type == NEVERC_H2_CLIENT_EVENT_DATA) {
            if (!stream->headers_received || stream->trailers_received ||
                grpc_client_stream_append(
                    stream, event->data, event->data_length) != 0) {
                stream->error = "invalid or oversized gRPC response DATA";
                result = -1;
            }
        } else if (event->type == NEVERC_H2_CLIENT_EVENT_TRAILERS) {
            if (!stream->headers_received || stream->trailers_received ||
                grpc_client_stream_parse_trailers(stream, event) != 0) {
                stream->error = "invalid gRPC response trailers";
                result = -1;
            } else {
                stream->trailers = event->headers;
                stream->trailer_count = event->header_count;
                event->headers = NULL;
                event->header_count = 0;
            }
        } else if (event->type == NEVERC_H2_CLIENT_EVENT_ERROR) {
            stream->error = event->error
                ? event->error : "HTTP/2 stream failed";
            result = -1;
        } else if (event->type == NEVERC_H2_CLIENT_EVENT_END) {
            size_t remaining = stream->buffer_length -
                               stream->buffer_offset;
            if (!stream->trailers_received || remaining != 0 ||
                ((stream->cardinality == NEVERC_GRPC_UNARY ||
                  stream->cardinality == NEVERC_GRPC_CLIENT_STREAMING) &&
                 stream->status == NEVERC_GRPC_OK &&
                 stream->received_count != 1)) {
                stream->error = "incomplete gRPC response";
                result = -1;
            } else {
                stream->receive_closed = 1;
                result = 2;
            }
        }
        neverc_h2_client_event_free(event);
        if (result < 0) return -1;
        buffered = grpc_client_stream_next_buffered(stream, message);
        if (buffered != 0) {
            if (buffered < 0)
                stream->error = "malformed gRPC response message";
            return buffered;
        }
        if (result == 2) return 0;
    }
}

neverc_grpc_status_t neverc_grpc_client_stream_status(
    neverc_grpc_client_stream_t *stream) {
    return stream ? stream->status : NEVERC_GRPC_UNKNOWN;
}

const char *neverc_grpc_client_stream_status_message(
    neverc_grpc_client_stream_t *stream) {
    return stream ? stream->status_message : NULL;
}

const char *neverc_grpc_client_stream_error(
    neverc_grpc_client_stream_t *stream) {
    return stream ? stream->error : "invalid gRPC stream";
}

const neverc_hpack_header_t *neverc_grpc_client_stream_headers(
    neverc_grpc_client_stream_t *stream, size_t *header_count) {
    if (header_count) *header_count = stream ? stream->header_count : 0;
    return stream ? stream->headers : NULL;
}

const neverc_hpack_header_t *neverc_grpc_client_stream_trailers(
    neverc_grpc_client_stream_t *stream, size_t *trailer_count) {
    if (trailer_count) *trailer_count = stream ? stream->trailer_count : 0;
    return stream ? stream->trailers : NULL;
}

void neverc_grpc_client_stream_cancel(
    neverc_grpc_client_stream_t *stream) {
    if (!stream || !stream->transport) return;
    stream->error = "gRPC stream cancelled";
    neverc_h2_client_stream_cancel(stream->transport, NC_H2_CANCEL);
}

void neverc_grpc_client_stream_free(
    neverc_grpc_client_stream_t *stream) {
    if (!stream) return;
    if (stream->transport)
        neverc_h2_client_stream_free(stream->transport);
    free(stream->buffer);
    free(stream->received_message);
    free(stream->status_message);
    for (size_t i = 0; i < stream->header_count; i++) {
        free(stream->headers[i].name);
        free(stream->headers[i].value);
    }
    for (size_t i = 0; i < stream->trailer_count; i++) {
        free(stream->trailers[i].name);
        free(stream->trailers[i].value);
    }
    free(stream->headers);
    free(stream->trailers);
    free(stream);
}

neverc_grpc_result_t *neverc_grpc_client_call(
    neverc_h2_client_t *client, neverc_context_t *context,
    const char *full_method, neverc_grpc_cardinality_t cardinality,
    const neverc_grpc_metadata_t *metadata, size_t metadata_count,
    const neverc_grpc_message_t *requests, size_t request_count,
    size_t max_response_message_size) {
    neverc_grpc_result_t *result =
        (neverc_grpc_result_t *)calloc(1, sizeof(*result));
    if (!result) return NULL;
    result->status = NEVERC_GRPC_UNKNOWN;
    if (!client || !full_method || full_method[0] != '/' ||
        !grpc_cardinality_valid(cardinality) ||
        metadata_count > GRPC_MAX_CLIENT_METADATA ||
        (metadata_count && !metadata) || (request_count && !requests) ||
        ((cardinality == NEVERC_GRPC_UNARY ||
          cardinality == NEVERC_GRPC_SERVER_STREAMING) &&
         request_count != 1)) {
        result->error = "invalid gRPC call";
        return result;
    }
    if (max_response_message_size == 0)
        max_response_message_size = GRPC_DEFAULT_MAX_MESSAGE_SIZE;
    size_t body_length = 0;
    for (size_t i = 0; i < request_count; i++) {
        if ((requests[i].length > 0 && !requests[i].data) ||
            requests[i].length > GRPC_DEFAULT_MAX_MESSAGE_SIZE ||
            requests[i].length > SIZE_MAX - 5 ||
            body_length > SIZE_MAX - requests[i].length - 5) {
            result->error = "gRPC request message is too large";
            return result;
        }
        body_length += requests[i].length + 5;
    }
    uint8_t *body = body_length ? (uint8_t *)malloc(body_length) : NULL;
    if (body_length && !body) {
        result->error = "out of memory";
        return result;
    }
    size_t body_offset = 0;
    for (size_t i = 0; i < request_count; i++) {
        size_t encoded_length = 0;
        if (neverc_grpc_frame_encode(
                requests[i].data, requests[i].length, 0,
                body + body_offset, body_length - body_offset,
                &encoded_length) != 0) {
            free(body);
            result->error = "failed to frame gRPC request";
            return result;
        }
        body_offset += encoded_length;
    }

    neverc_hpack_header_t headers[GRPC_MAX_CLIENT_METADATA + 3];
    char *owned_values[GRPC_MAX_CLIENT_METADATA + 1];
    memset(owned_values, 0, sizeof(owned_values));
    size_t header_count = 0;
    headers[header_count++] = (neverc_hpack_header_t){
        .name = "content-type", .value = "application/grpc+proto"};
    headers[header_count++] = (neverc_hpack_header_t){
        .name = "te", .value = "trailers"};
    int metadata_valid = 1;
    for (size_t i = 0; i < metadata_count; i++) {
        if (!grpc_metadata_key_valid(metadata[i].key) ||
            (metadata[i].value_length > 0 && !metadata[i].value)) {
            metadata_valid = 0;
            break;
        }
        size_t value_capacity;
        size_t value_length;
        if (grpc_binary_key(metadata[i].key)) {
            value_capacity = neverc_base64_encoded_len(
                metadata[i].value_length);
            owned_values[i] = (char *)malloc(value_capacity + 1);
            if (!owned_values[i]) {
                metadata_valid = 0;
                break;
            }
            value_length = neverc_base64_encode(
                owned_values[i], metadata[i].value,
                metadata[i].value_length);
        } else {
            value_capacity = metadata[i].value_length;
            owned_values[i] = (char *)malloc(value_capacity + 1);
            if (!owned_values[i]) {
                metadata_valid = 0;
                break;
            }
            memcpy(owned_values[i], metadata[i].value, value_capacity);
            value_length = value_capacity;
            for (size_t j = 0; j < value_length; j++)
                if ((unsigned char)owned_values[i][j] < 0x20 ||
                    (unsigned char)owned_values[i][j] == 0x7f) {
                    metadata_valid = 0;
                    break;
                }
        }
        owned_values[i][value_length] = '\0';
        headers[header_count++] = (neverc_hpack_header_t){
            .name = (char *)metadata[i].key,
            .value = owned_values[i],
            .sensitive = 0};
        if (!metadata_valid) break;
    }
    char timeout_value[10];
    int64_t deadline = context ? neverc_context_deadline(context) : 0;
    if (metadata_valid && deadline > 0) {
        int64_t remaining_ms = deadline - grpc_now_ms();
        uint64_t timeout_ns = remaining_ms <= 0 ? 1 :
            (uint64_t)remaining_ms > UINT64_MAX / 1000000
                ? UINT64_MAX : (uint64_t)remaining_ms * 1000000;
        if (neverc_grpc_timeout_encode(timeout_ns, timeout_value) != 0) {
            metadata_valid = 0;
        } else {
            headers[header_count++] = (neverc_hpack_header_t){
                .name = "grpc-timeout", .value = timeout_value};
        }
    }
    if (!metadata_valid) {
        for (size_t i = 0; i < metadata_count; i++)
            free(owned_values[i]);
        free(body);
        result->error = "invalid gRPC metadata";
        return result;
    }

    neverc_h2_response_t *response = neverc_h2_client_do_context(
        client, context, "POST", full_method, headers, header_count,
        body, body_length);
    for (size_t i = 0; i < metadata_count; i++)
        free(owned_values[i]);
    free(body);
    if (!response) {
        result->error = "out of memory";
        return result;
    }
    if (response->error) {
        result->error = response->error;
        neverc_h2_response_free(response);
        return result;
    }
    result->headers = response->headers;
    result->header_count = response->header_count;
    result->trailers = response->trailers;
    result->trailer_count = response->trailer_count;
    response->headers = NULL;
    response->header_count = 0;
    response->trailers = NULL;
    response->trailer_count = 0;

    size_t status_matches = 0;
    const char *status_value = grpc_find_header(
        result->trailers, result->trailer_count,
        "grpc-status", &status_matches);
    size_t content_type_matches = 0;
    const char *content_type = grpc_find_header(
        result->headers, result->header_count,
        "content-type", &content_type_matches);
    if (response->status_code != 200 || content_type_matches != 1 ||
        !grpc_content_type_valid(content_type) || status_matches != 1 ||
        grpc_parse_status(status_value, &result->status) != 0) {
        result->error = "invalid or missing gRPC status";
        result->status = NEVERC_GRPC_UNKNOWN;
    }
    size_t message_matches = 0;
    const char *message_value = grpc_find_header(
        result->trailers, result->trailer_count,
        "grpc-message", &message_matches);
    if (message_matches == 1)
        result->status_message = grpc_percent_decode(message_value);
    if (grpc_parse_messages(
            response->body, response->body_length,
            max_response_message_size, result) != 0) {
        result->error = "malformed gRPC response framing";
        result->status = NEVERC_GRPC_DATA_LOSS;
    }
    if ((cardinality == NEVERC_GRPC_UNARY ||
         cardinality == NEVERC_GRPC_CLIENT_STREAMING) &&
        result->status == NEVERC_GRPC_OK && result->message_count != 1) {
        result->error = "gRPC unary response count is not one";
        result->status = NEVERC_GRPC_DATA_LOSS;
    }
    neverc_h2_response_free(response);
    return result;
}

void neverc_grpc_result_free(neverc_grpc_result_t *result) {
    if (!result) return;
    for (size_t i = 0; i < result->message_count; i++)
        free((void *)result->messages[i].data);
    free(result->messages);
    free(result->status_message);
    for (size_t i = 0; i < result->header_count; i++) {
        free(result->headers[i].name);
        free(result->headers[i].value);
    }
    for (size_t i = 0; i < result->trailer_count; i++) {
        free(result->trailers[i].name);
        free(result->trailers[i].value);
    }
    free(result->headers);
    free(result->trailers);
    free(result);
}
