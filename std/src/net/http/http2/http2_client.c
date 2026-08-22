#include "neverc/std/net/http/http2.h"
#include "neverc/std/thread.h"
#include "neverc/std/net/url.h"

#include "../../_net_buffer.h"
#include "../../_net_platform.h"
#include "../../_net_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

#define H2_CLIENT_MAX_DECODED_HEADERS 128
#define H2_CLIENT_CONTEXT_POLL_MS 25
#define H2_MAX_CONTINUATION_FRAMES 128

struct neverc_h2_client_stream {
    neverc_h2_client_t *client;
    uint32_t id;
    int32_t send_window;
    int32_t recv_window;
    neverc_hpack_header_t *headers;
    size_t header_count;
    neverc_hpack_header_t *trailers;
    size_t trailer_count;
    int received_trailers;
    int received_data;
    nc_buf_t body;
    size_t received_body_size;
    size_t header_list_size;
    uint64_t content_length;
    int content_length_seen;
    int body_forbidden;
    int head_request;
    int response_started;
    int done;
    uint32_t status_code;
    uint32_t error_code;
    const char *error;
    int live;
    int local_closed;
    int linked;
    neverc_thread_channel_t *receive_queue;
    nc_cond_t changed;
    neverc_h2_client_stream_t *next;
};

typedef neverc_h2_client_stream_t h2_client_stream_t;

struct neverc_h2_client {
    neverc_h2_client_config_t config;
    neverc_tcp_conn_t *tcp;
    neverc_tls_conn_t *tls;
    char *authority;
    const char *scheme;
    neverc_hpack_encoder_t *encoder;
    neverc_hpack_decoder_t *decoder;
    neverc_h2_settings_t local_settings;
    neverc_h2_settings_t peer_settings;
    nc_mutex_t state_lock;
    nc_mutex_t write_lock;
    nc_cond_t window_changed;
    nc_thread_t reader_thread;
    int reader_started;
    volatile int running;
    volatile int closing;
    int initial_settings_received;
    int goaway_received;
    uint32_t goaway_last_stream;
    uint32_t next_stream_id;
    size_t active_streams;
    int32_t conn_send_window;
    int32_t conn_recv_window;
    h2_client_stream_t *streams;
    uint8_t *pending_headers;
    size_t pending_length;
    size_t pending_capacity;
    uint32_t pending_stream_id;
    int pending_active;
    int pending_end_stream;
    int pending_continuations;
    uint32_t settings_ack_owed;
};

static int h2_client_transport_read_all(neverc_h2_client_t *client,
                                        void *output, size_t length) {
    uint8_t *cursor = (uint8_t *)output;
    size_t offset = 0;
    while (offset < length) {
        int count = client->tls
            ? neverc_tls_read(client->tls, cursor + offset, length - offset)
            : neverc_tcp_read(client->tcp, cursor + offset, length - offset);
        if (count <= 0) return -1;
        offset += (size_t)count;
    }
    return 0;
}

static int h2_client_transport_write_all(neverc_h2_client_t *client,
                                         const void *input, size_t length) {
    const uint8_t *cursor = (const uint8_t *)input;
    size_t offset = 0;
    while (offset < length) {
        int count = client->tls
            ? neverc_tls_write(client->tls, cursor + offset, length - offset)
            : neverc_tcp_write(client->tcp, cursor + offset, length - offset);
        if (count <= 0) return -1;
        offset += (size_t)count;
    }
    return 0;
}

static void h2_client_transport_shutdown(neverc_h2_client_t *client) {
    if (client->tls) {
        (void)neverc_tls_shutdown_read(client->tls);
        (void)neverc_tls_shutdown_write(client->tls);
    } else if (client->tcp) {
        (void)neverc_tcp_shutdown_read(client->tcp);
        (void)neverc_tcp_shutdown_write(client->tcp);
    }
}

static int h2_client_write_frame_locked(neverc_h2_client_t *client,
                                        uint8_t type, uint8_t flags,
                                        uint32_t stream_id,
                                        const void *payload,
                                        uint32_t payload_length) {
    if (payload_length > 0x00ffffffu ||
        (payload_length > 0 && !payload) ||
        stream_id > 0x7fffffffu)
        return -1;
    neverc_h2_frame_header_t header = {
        .length = payload_length,
        .type = type,
        .flags = flags,
        .stream_id = stream_id,
    };
    uint8_t encoded[NC_H2_FRAME_HEADER_SIZE];
    if (neverc_h2_frame_header_write(&header, encoded) != 0 ||
        h2_client_transport_write_all(client, encoded, sizeof(encoded)) != 0 ||
        (payload_length > 0 &&
         h2_client_transport_write_all(client, payload,
                                       payload_length) != 0))
        return -1;
    return 0;
}

static int h2_client_write_frame(neverc_h2_client_t *client,
                                 uint8_t type, uint8_t flags,
                                 uint32_t stream_id, const void *payload,
                                 uint32_t payload_length) {
    nc_mutex_lock(&client->write_lock);
    int result = h2_client_write_frame_locked(
        client, type, flags, stream_id, payload, payload_length);
    nc_mutex_unlock(&client->write_lock);
    return result;
}

static int h2_client_write_u32(neverc_h2_client_t *client, uint8_t type,
                               uint32_t stream_id, uint32_t value) {
    if (type == NC_H2_FRAME_WINDOW_UPDATE) {
        /* RFC 9113 §6.9: a WINDOW_UPDATE increment of 0 is a protocol error. */
        if (value == 0) return 0;
        if (value > 0x7fffffffu) return -1;
        value &= 0x7fffffffu;
    }
    uint8_t payload[4] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value};
    return h2_client_write_frame(client, type, 0, stream_id,
                                 payload, sizeof(payload));
}

static int h2_client_write_settings(neverc_h2_client_t *client) {
    uint8_t payload[36];
    size_t offset = 0;
#define H2_CLIENT_SETTING(id, value) do { \
    uint32_t setting_value = (uint32_t)(value); \
    payload[offset++] = (uint8_t)((id) >> 8); \
    payload[offset++] = (uint8_t)(id); \
    payload[offset++] = (uint8_t)(setting_value >> 24); \
    payload[offset++] = (uint8_t)(setting_value >> 16); \
    payload[offset++] = (uint8_t)(setting_value >> 8); \
    payload[offset++] = (uint8_t)setting_value; \
} while (0)
    H2_CLIENT_SETTING(NC_H2_SETTINGS_HEADER_TABLE_SIZE,
                      client->local_settings.header_table_size);
    H2_CLIENT_SETTING(NC_H2_SETTINGS_ENABLE_PUSH, 0);
    H2_CLIENT_SETTING(NC_H2_SETTINGS_MAX_CONCURRENT_STREAMS,
                      client->local_settings.max_concurrent_streams);
    H2_CLIENT_SETTING(NC_H2_SETTINGS_INITIAL_WINDOW_SIZE,
                      client->local_settings.initial_window_size);
    H2_CLIENT_SETTING(NC_H2_SETTINGS_MAX_FRAME_SIZE,
                      client->local_settings.max_frame_size);
    H2_CLIENT_SETTING(NC_H2_SETTINGS_MAX_HEADER_LIST_SIZE,
                      client->local_settings.max_header_list_size);
#undef H2_CLIENT_SETTING
    if (h2_client_write_frame(client, NC_H2_FRAME_SETTINGS, 0, 0,
                              payload, (uint32_t)offset) != 0)
        return -1;
    client->settings_ack_owed++;
    return 0;
}

static h2_client_stream_t *h2_client_find_stream(
    neverc_h2_client_t *client, uint32_t id) {
    for (h2_client_stream_t *stream = client->streams; stream;
         stream = stream->next)
        if (stream->id == id) return stream;
    return NULL;
}

static void h2_client_free_headers(neverc_hpack_header_t *headers,
                                   size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    free(headers);
}

void neverc_h2_client_event_free(neverc_h2_client_event_t *event) {
    if (!event) return;
    h2_client_free_headers(event->headers, event->header_count);
    free(event->data);
    free(event);
}

static int h2_client_push_event(h2_client_stream_t *stream,
                                neverc_h2_client_event_t *event) {
    if (!stream->receive_queue ||
        neverc_thread_channel_try_send(stream->receive_queue, event) !=
            NEVERC_THREAD_OK) {
        neverc_h2_client_event_free(event);
        return -1;
    }
    return 0;
}

static void h2_client_push_terminal_event(h2_client_stream_t *stream,
                                          const char *error,
                                          uint32_t error_code) {
    if (!stream->live || !stream->receive_queue) return;
    neverc_h2_client_event_t *event =
        (neverc_h2_client_event_t *)calloc(1, sizeof(*event));
    if (event) {
        event->type = error ? NEVERC_H2_CLIENT_EVENT_ERROR
                            : NEVERC_H2_CLIENT_EVENT_END;
        event->error = error;
        event->error_code = error_code;
        int sent = neverc_thread_channel_try_send(
            stream->receive_queue, event);
        if (sent != NEVERC_THREAD_OK && error) {
            void *dropped = NULL;
            if (neverc_thread_channel_try_receive(
                    stream->receive_queue, &dropped) == NEVERC_THREAD_OK) {
                neverc_h2_client_event_free(
                    (neverc_h2_client_event_t *)dropped);
                sent = neverc_thread_channel_try_send(
                    stream->receive_queue, event);
            }
        }
        if (sent != NEVERC_THREAD_OK)
            neverc_h2_client_event_free(event);
    }
    (void)neverc_thread_channel_close(stream->receive_queue);
}

static void h2_client_stream_destroy(h2_client_stream_t *stream) {
    if (!stream) return;
    h2_client_free_headers(stream->headers, stream->header_count);
    h2_client_free_headers(stream->trailers, stream->trailer_count);
    if (stream->receive_queue) {
        (void)neverc_thread_channel_close(stream->receive_queue);
        void *queued = NULL;
        while (neverc_thread_channel_try_receive(
                   stream->receive_queue, &queued) == NEVERC_THREAD_OK)
            neverc_h2_client_event_free(
                (neverc_h2_client_event_t *)queued);
        neverc_thread_channel_free(stream->receive_queue);
    }
    nc_buf_free(&stream->body);
    nc_cond_destroy(&stream->changed);
    free(stream);
}

static void h2_client_remove_stream_locked(neverc_h2_client_t *client,
                                           h2_client_stream_t *stream) {
    h2_client_stream_t **link = &client->streams;
    while (*link) {
        if (*link == stream) {
            *link = stream->next;
            stream->next = NULL;
            stream->linked = 0;
            if (client->active_streams > 0) client->active_streams--;
            nc_cond_broadcast(&client->window_changed);
            return;
        }
        link = &(*link)->next;
    }
}

static void h2_client_fail_all_locked(neverc_h2_client_t *client,
                                      const char *error) {
    for (h2_client_stream_t *stream = client->streams; stream;
         stream = stream->next) {
        if (!stream->done) {
            stream->error = error;
            stream->done = 1;
            h2_client_push_terminal_event(stream, error, 0);
            nc_cond_broadcast(&stream->changed);
        }
    }
    nc_cond_broadcast(&client->window_changed);
}

static void h2_client_fail_transport(neverc_h2_client_t *client,
                                     const char *error) {
    if (!nc_atomic_cas(&client->running, 1, 0)) return;
    h2_client_transport_shutdown(client);
    nc_mutex_lock(&client->state_lock);
    h2_client_fail_all_locked(client, error);
    nc_mutex_unlock(&client->state_lock);
}

static int h2_client_append_pending(neverc_h2_client_t *client,
                                    const uint8_t *data, size_t length) {
    if (length > client->config.max_response_header_list_size ||
        client->pending_length >
            client->config.max_response_header_list_size - length)
        return -1;
    size_t needed = client->pending_length + length;
    if (needed > client->pending_capacity) {
        size_t capacity = client->pending_capacity
            ? client->pending_capacity : 4096;
        while (capacity < needed) {
            if (capacity >
                client->config.max_response_header_list_size / 2) {
                capacity = client->config.max_response_header_list_size;
                break;
            }
            capacity *= 2;
        }
        uint8_t *resized = (uint8_t *)realloc(
            client->pending_headers, capacity);
        if (!resized) return -1;
        client->pending_headers = resized;
        client->pending_capacity = capacity;
    }
    if (length > 0)
        memcpy(client->pending_headers + client->pending_length,
               data, length);
    client->pending_length += length;
    return 0;
}

static void h2_client_clear_pending(neverc_h2_client_t *client) {
    client->pending_length = 0;
    client->pending_stream_id = 0;
    client->pending_active = 0;
    client->pending_end_stream = 0;
    client->pending_continuations = 0;
}

static int h2_client_name_valid(const char *name) {
    if (!name || !name[0]) return 0;
    for (const unsigned char *cursor = (const unsigned char *)name;
         *cursor; cursor++) {
        unsigned char c = *cursor;
        if (c >= 'A' && c <= 'Z') return 0;
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') &&
            c != '!' && c != '#' && c != '$' && c != '%' && c != '&' &&
            c != '\'' && c != '*' && c != '+' && c != '-' && c != '.' &&
            c != '^' && c != '_' && c != 0x60 && c != '|' && c != '~')
            return 0;
    }
    return 1;
}

static int h2_client_value_valid(const char *value) {
    if (!value) return 0;
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor; cursor++)
        if (*cursor == '\r' || *cursor == '\n' || *cursor == 0x7f ||
            (*cursor < 0x20 && *cursor != '\t'))
            return 0;
    return 1;
}

static int h2_client_path_valid(const char *path) {
    if (!path || path[0] != '/') return 0;
    {
        const char *query = strchr(path, '?');
        size_t path_len = query ? (size_t)(query - path) : strlen(path);
        if (neverc_url_path_n_is_protocol_relative(path, path_len))
            return 0;
    }
    for (const unsigned char *cursor = (const unsigned char *)path;
         *cursor; cursor++)
        if (*cursor <= 0x20 || *cursor == 0x7f || *cursor == '#' ||
            *cursor == '\\')
            return 0;
    return 1;
}

static int h2_client_parse_content_length(const char *value,
                                          uint64_t *output) {
    if (!value || !value[0] || !output) return -1;
    uint64_t result = 0;
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9') return -1;
        uint64_t digit = (uint64_t)(*cursor - '0');
        if (result > (UINT64_MAX - digit) / 10) return -1;
        result = result * 10 + digit;
    }
    *output = result;
    return 0;
}

static int h2_client_response_length_valid(
    const h2_client_stream_t *stream) {
    if (!stream || stream->body_forbidden)
        return stream && stream->received_body_size == 0 ? 0 : -1;
    if (stream->content_length_seen &&
        stream->content_length != (uint64_t)stream->received_body_size)
        return -1;
    return 0;
}

static int h2_client_store_decoded_headers(
    neverc_h2_client_t *client, h2_client_stream_t *stream,
    neverc_hpack_header_t *decoded, int decoded_count) {
    int trailers = stream->response_started;
    int status_count = 0;
    int regular_seen = 0;
    int status_code = 0;
    int content_length_count = 0;
    uint64_t content_length = 0;
    /* RFC 9113 §6.5.2 / §4.3: MAX_HEADER_LIST_SIZE is per header block
     * (1xx, response headers, and trailers are separate lists). */
    size_t list_size = 0;
    size_t regular_count = 0;
    for (int i = 0; i < decoded_count; i++) {
        const char *name = decoded[i].name;
        const char *value = decoded[i].value;
        size_t name_length = strlen(name);
        size_t value_length = strlen(value);
        if (name_length > SIZE_MAX - value_length - 32)
            return -1;
        size_t field_size = name_length + value_length + 32;
        if (
            field_size > client->config.max_response_header_list_size ||
            list_size >
                client->config.max_response_header_list_size - field_size)
            return -1;
        list_size += field_size;
        if (name[0] == ':') {
            if (trailers || regular_seen || strcmp(name, ":status") != 0 ||
                ++status_count != 1 || strlen(value) != 3 ||
                value[0] < '1' || value[0] > '9' ||
                value[1] < '0' || value[1] > '9' ||
                value[2] < '0' || value[2] > '9')
                return -1;
            status_code = (value[0] - '0') * 100 +
                          (value[1] - '0') * 10 + value[2] - '0';
        } else {
            regular_seen = 1;
            if (!h2_client_name_valid(name) ||
                !h2_client_value_valid(value) ||
                strcmp(name, "connection") == 0 ||
                strcmp(name, "proxy-connection") == 0 ||
                strcmp(name, "keep-alive") == 0 ||
                strcmp(name, "transfer-encoding") == 0 ||
                strcmp(name, "upgrade") == 0)
                return -1;
            if (strcmp(name, "te") == 0 &&
                strcasecmp(value, "trailers") != 0)
                return -1;
            if (strcmp(name, "content-length") == 0) {
                if (trailers || ++content_length_count != 1 ||
                    h2_client_parse_content_length(
                        value, &content_length) != 0)
                    return -1;
            }
            regular_count++;
        }
    }
    if (!trailers && status_count != 1) return -1;
    neverc_hpack_header_t *stored = regular_count
        ? (neverc_hpack_header_t *)calloc(regular_count, sizeof(*stored))
        : NULL;
    if (regular_count && !stored) return -1;
    size_t output_index = 0;
    for (int i = 0; i < decoded_count; i++) {
        if (decoded[i].name[0] == ':') {
            free(decoded[i].name);
            free(decoded[i].value);
            decoded[i].name = NULL;
            decoded[i].value = NULL;
        } else {
            stored[output_index++] = decoded[i];
            decoded[i].name = NULL;
            decoded[i].value = NULL;
        }
    }
    if (!trailers && status_code < 200) {
        h2_client_free_headers(stored, regular_count);
    } else if (trailers) {
        if (stream->received_trailers) {
            h2_client_free_headers(stored, regular_count);
            return -1;
        }
        stream->trailers = stored;
        stream->trailer_count = regular_count;
        stream->received_trailers = 1;
    } else {
        if (status_code == 204 && content_length_count != 0) {
            h2_client_free_headers(stored, regular_count);
            return -1;
        }
        stream->headers = stored;
        stream->header_count = regular_count;
        stream->response_started = 1;
        stream->status_code = (uint32_t)status_code;
        stream->body_forbidden = stream->head_request ||
            status_code == 204 || status_code == 304;
        if (!stream->body_forbidden && content_length_count == 1) {
            stream->content_length = content_length;
            stream->content_length_seen = 1;
        }
    }
    stream->header_list_size = list_size;
    return 0;
}

static int h2_client_decode_pending(neverc_h2_client_t *client,
                                    int end_stream) {
    h2_client_stream_t *stream =
        h2_client_find_stream(client, client->pending_stream_id);
    neverc_hpack_header_t decoded[H2_CLIENT_MAX_DECODED_HEADERS];
    memset(decoded, 0, sizeof(decoded));
    int count = 0;
    int decode_result = neverc_hpack_decode(
        client->decoder, client->pending_headers, client->pending_length,
        decoded, H2_CLIENT_MAX_DECODED_HEADERS, &count);
    int result = decode_result;
    int was_response_started = stream ? stream->response_started : 0;
    int already_done = stream && stream->done;
    if (result == 0 && stream && !already_done)
        result = h2_client_store_decoded_headers(
            client, stream, decoded, count);
    if (result == 0 && stream && was_response_started && !end_stream)
        result = -1;
    if (result == 0 && stream && end_stream &&
        !stream->response_started)
        result = -1;
    if (result == 0 && stream && end_stream &&
        h2_client_response_length_valid(stream) != 0)
        result = -1;
    for (int i = 0; i < count; i++) {
        free(decoded[i].name);
        free(decoded[i].value);
    }
    h2_client_clear_pending(client);
    if (decode_result < 0) return -1;
    /* RFC 9113 §5.1 / §8.1: HEADERS after remote END_STREAM is not a
     * trailer. Keep HPACK aligned and leave the existing stream error. */
    if (already_done)
        return 0;
    if (decode_result > 0) {
        if (!stream) return -1;
        stream->error = "HTTP/2 header list exceeded decoder limit";
        stream->error_code = NC_H2_PROTOCOL_ERROR;
        stream->done = 1;
        h2_client_push_terminal_event(stream, stream->error,
                                      stream->error_code);
        nc_cond_broadcast(&stream->changed);
        return 0;
    }
    if (!stream) return result == 0 ? 0 : -1;
    if (result != 0) {
        stream->error = "invalid HTTP/2 response headers";
        stream->error_code = NC_H2_PROTOCOL_ERROR;
        stream->done = 1;
        h2_client_push_terminal_event(stream, stream->error,
                                      stream->error_code);
        nc_cond_broadcast(&stream->changed);
        return 0;
    }
    if (stream->live && stream->response_started) {
        neverc_h2_client_event_t *event =
            (neverc_h2_client_event_t *)calloc(1, sizeof(*event));
        if (!event) {
            stream->error = "out of memory receiving HTTP/2 headers";
            stream->done = 1;
            h2_client_push_terminal_event(stream, stream->error,
                                          NC_H2_INTERNAL_ERROR);
            return 0;
        }
        event->type = was_response_started
            ? NEVERC_H2_CLIENT_EVENT_TRAILERS
            : NEVERC_H2_CLIENT_EVENT_HEADERS;
        event->status_code = (int)stream->status_code;
        if (was_response_started) {
            event->headers = stream->trailers;
            event->header_count = stream->trailer_count;
            stream->trailers = NULL;
            stream->trailer_count = 0;
        } else {
            event->headers = stream->headers;
            event->header_count = stream->header_count;
            stream->headers = NULL;
            stream->header_count = 0;
        }
        if (h2_client_push_event(stream, event) != 0) {
            stream->error = "HTTP/2 response consumer is too slow";
            stream->error_code = NC_H2_ENHANCE_YOUR_CALM;
            stream->done = 1;
            h2_client_push_terminal_event(stream, stream->error,
                                          stream->error_code);
            return 0;
        }
    }
    if (end_stream) {
        stream->done = 1;
        h2_client_push_terminal_event(stream, NULL, 0);
        nc_cond_broadcast(&stream->changed);
    }
    return 0;
}

static int h2_client_header_fragment(
    const neverc_h2_frame_header_t *header, const uint8_t *payload,
    const uint8_t **fragment, size_t *fragment_length) {
    if (!header || !fragment || !fragment_length ||
        (header->length != 0 && !payload))
        return -1;
    size_t offset = 0;
    size_t padding = 0;
    if (header->flags & NC_H2_FLAG_PADDED) {
        if (header->length < 1) return -1;
        padding = payload[offset++];
    }
    if (header->flags & NC_H2_FLAG_PRIORITY) {
        if (header->length - offset < 5) return -1;
        uint32_t dependency = ((uint32_t)(payload[offset] & 0x7f) << 24) |
                              ((uint32_t)payload[offset + 1] << 16) |
                              ((uint32_t)payload[offset + 2] << 8) |
                              (uint32_t)payload[offset + 3];
        /* RFC 9113 §6.2: a HEADERS priority that depends on itself is
         * a connection error of type PROTOCOL_ERROR. */
        if (dependency == header->stream_id) return -1;
        offset += 5;
    }
    if (padding > header->length - offset) return -1;
    *fragment = payload ? payload + offset : NULL;
    *fragment_length = header->length - offset - padding;
    return 0;
}

static int h2_client_data_fragment(
    const neverc_h2_frame_header_t *header, const uint8_t *payload,
    const uint8_t **data, size_t *data_length) {
    size_t offset = 0;
    size_t padding = 0;
    if (header->flags & NC_H2_FLAG_PADDED) {
        if (header->length < 1) return -1;
        padding = payload[offset++];
    }
    if (padding > header->length - offset) return -1;
    *data = payload + offset;
    *data_length = header->length - offset - padding;
    return 0;
}

static int h2_client_apply_settings(neverc_h2_client_t *client,
                                    const uint8_t *payload,
                                    uint32_t length) {
    if (length % 6 != 0) return -1;
    nc_mutex_lock(&client->state_lock);
    for (uint32_t offset = 0; offset < length; offset += 6) {
        uint16_t id = (uint16_t)((payload[offset] << 8) |
                                 payload[offset + 1]);
        uint32_t value = ((uint32_t)payload[offset + 2] << 24) |
                         ((uint32_t)payload[offset + 3] << 16) |
                         ((uint32_t)payload[offset + 4] << 8) |
                         payload[offset + 5];
        if (id == NC_H2_SETTINGS_ENABLE_PUSH && value != 0) {
            nc_mutex_unlock(&client->state_lock);
            return -1;
        }
        if (id == NC_H2_SETTINGS_INITIAL_WINDOW_SIZE) {
            if (value > INT32_MAX) {
                nc_mutex_unlock(&client->state_lock);
                return -1;
            }
            int64_t delta = (int64_t)value -
                client->peer_settings.initial_window_size;
            for (h2_client_stream_t *stream = client->streams; stream;
                 stream = stream->next) {
                int64_t updated = stream->send_window + delta;
                if (updated < INT32_MIN || updated > INT32_MAX) {
                    nc_mutex_unlock(&client->state_lock);
                    return -1;
                }
            }
            for (h2_client_stream_t *stream = client->streams; stream;
                 stream = stream->next)
                stream->send_window += (int32_t)delta;
            client->peer_settings.initial_window_size = value;
        } else if (id == NC_H2_SETTINGS_MAX_FRAME_SIZE) {
            if (value < NC_H2_DEFAULT_MAX_FRAME_SIZE ||
                value > NC_H2_MAX_FRAME_SIZE_LIMIT) {
                nc_mutex_unlock(&client->state_lock);
                return -1;
            }
            client->peer_settings.max_frame_size = value;
        } else if (id == NC_H2_SETTINGS_MAX_CONCURRENT_STREAMS) {
            client->peer_settings.max_concurrent_streams = value;
        } else if (id == NC_H2_SETTINGS_HEADER_TABLE_SIZE) {
            /* RFC 7541 §4.2: apply every HEADER_TABLE_SIZE in order so a
             * shrink-then-grow frame emits the required size-update of the
             * smallest value in the interval. Last-wins skips the shrink. */
            client->peer_settings.header_table_size = value;
            uint32_t encoder_table_size =
                value > NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE
                    ? NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE : value;
            nc_mutex_lock(&client->write_lock);
            int table_result = neverc_hpack_encoder_set_max_table_size(
                client->encoder, encoder_table_size);
            nc_mutex_unlock(&client->write_lock);
            if (table_result != 0) {
                nc_mutex_unlock(&client->state_lock);
                return -1;
            }
        } else if (id == NC_H2_SETTINGS_MAX_HEADER_LIST_SIZE) {
            client->peer_settings.max_header_list_size = value;
        }
    }
    nc_cond_broadcast(&client->window_changed);
    nc_mutex_unlock(&client->state_lock);
    return 0;
}

static int h2_client_reader_frame(neverc_h2_client_t *client,
                                  const neverc_h2_frame_header_t *header,
                                  uint8_t *payload) {
    if (!client->initial_settings_received &&
        (header->type != NC_H2_FRAME_SETTINGS ||
         (header->flags & NC_H2_FLAG_ACK)))
        return -1;
    if (client->pending_active &&
        header->type != NC_H2_FRAME_CONTINUATION)
        return -1;
    switch (header->type) {
    case NC_H2_FRAME_SETTINGS:
        if (header->stream_id != 0 ||
            ((header->flags & NC_H2_FLAG_ACK) && header->length != 0))
            return -1;
        if (!(header->flags & NC_H2_FLAG_ACK)) {
            if (h2_client_apply_settings(client, payload,
                                         header->length) != 0 ||
                h2_client_write_frame(client, NC_H2_FRAME_SETTINGS,
                                      NC_H2_FLAG_ACK, 0, NULL, 0) != 0)
                return -1;
            client->initial_settings_received = 1;
        } else if (client->settings_ack_owed == 0) {
            return -1;
        } else {
            client->settings_ack_owed--;
        }
        return 0;
    case NC_H2_FRAME_PING:
        if (header->stream_id != 0 || header->length != 8) return -1;
        if (!(header->flags & NC_H2_FLAG_ACK))
            return h2_client_write_frame(client, NC_H2_FRAME_PING,
                                         NC_H2_FLAG_ACK, 0,
                                         payload, 8);
        return 0;
    case NC_H2_FRAME_WINDOW_UPDATE: {
        if (header->length != 4) return -1;
        uint32_t increment = ((uint32_t)(payload[0] & 0x7f) << 24) |
                             ((uint32_t)payload[1] << 16) |
                             ((uint32_t)payload[2] << 8) | payload[3];
        if (increment == 0) {
            /* RFC 9113 §6.9: increment 0 is a connection error on stream
             * 0 and a stream error otherwise (match the server). */
            if (header->stream_id == 0) return -1;
            nc_mutex_lock(&client->state_lock);
            h2_client_stream_t *zero_stream =
                h2_client_find_stream(client, header->stream_id);
            if (!zero_stream &&
                ((header->stream_id & 1U) == 0 ||
                 header->stream_id >= client->next_stream_id)) {
                nc_mutex_unlock(&client->state_lock);
                return -1;
            }
            if (zero_stream && !zero_stream->done) {
                zero_stream->error =
                    "HTTP/2 WINDOW_UPDATE increment of 0";
                zero_stream->error_code = NC_H2_PROTOCOL_ERROR;
                zero_stream->done = 1;
                h2_client_push_terminal_event(zero_stream,
                                              zero_stream->error,
                                              zero_stream->error_code);
                nc_cond_broadcast(&zero_stream->changed);
            }
            nc_mutex_unlock(&client->state_lock);
            (void)h2_client_write_u32(client, NC_H2_FRAME_RST_STREAM,
                                      header->stream_id,
                                      NC_H2_PROTOCOL_ERROR);
            return 0;
        }
        nc_mutex_lock(&client->state_lock);
        if (header->stream_id == 0) {
            if ((int64_t)client->conn_send_window + increment > INT32_MAX) {
                nc_mutex_unlock(&client->state_lock);
                return -1;
            }
            client->conn_send_window += (int32_t)increment;
        } else {
            h2_client_stream_t *stream =
                h2_client_find_stream(client, header->stream_id);
            if (!stream &&
                ((header->stream_id & 1U) == 0 ||
                 header->stream_id >= client->next_stream_id)) {
                nc_mutex_unlock(&client->state_lock);
                return -1;
            }
            if (stream) {
                if ((int64_t)stream->send_window + increment > INT32_MAX) {
                    /* RFC 9113 §6.9.1: stream overflow is RST_STREAM, not a
                     * connection error. */
                    if (!stream->done) {
                        stream->error =
                            "HTTP/2 WINDOW_UPDATE overflow";
                        stream->error_code = NC_H2_FLOW_CONTROL_ERROR;
                        stream->done = 1;
                        h2_client_push_terminal_event(
                            stream, stream->error, stream->error_code);
                        nc_cond_broadcast(&stream->changed);
                    }
                    nc_mutex_unlock(&client->state_lock);
                    (void)h2_client_write_u32(
                        client, NC_H2_FRAME_RST_STREAM, header->stream_id,
                        NC_H2_FLOW_CONTROL_ERROR);
                    return 0;
                }
                stream->send_window += (int32_t)increment;
            }
        }
        nc_cond_broadcast(&client->window_changed);
        nc_mutex_unlock(&client->state_lock);
        return 0;
    }
    case NC_H2_FRAME_HEADERS: {
        if (header->stream_id == 0) return -1;
        const uint8_t *fragment = NULL;
        size_t fragment_length = 0;
        if (h2_client_header_fragment(header, payload, &fragment,
                                      &fragment_length) != 0)
            return -1;
        nc_mutex_lock(&client->state_lock);
        if ((header->stream_id & 1U) == 0 ||
            header->stream_id >= client->next_stream_id) {
            nc_mutex_unlock(&client->state_lock);
            return -1;
        }
        h2_client_stream_t *hdr_stream =
            h2_client_find_stream(client, header->stream_id);
        int already_done = hdr_stream && hdr_stream->done;
        if (h2_client_append_pending(client, fragment,
                                     fragment_length) != 0) {
            nc_mutex_unlock(&client->state_lock);
            return -1;
        }
        client->pending_stream_id = header->stream_id;
        client->pending_active =
            (header->flags & NC_H2_FLAG_END_HEADERS) == 0;
        client->pending_end_stream =
            (header->flags & NC_H2_FLAG_END_STREAM) != 0;
        client->pending_continuations = 0;
        int result = 0;
        if (!client->pending_active)
            result = h2_client_decode_pending(
                client, client->pending_end_stream);
        nc_mutex_unlock(&client->state_lock);
        if (already_done && result == 0) {
            (void)h2_client_write_u32(client, NC_H2_FRAME_RST_STREAM,
                                      header->stream_id,
                                      NC_H2_STREAM_CLOSED);
            return 0;
        }
        return result;
    }
    case NC_H2_FRAME_CONTINUATION: {
        /* RFC 9113 §6.10: CONTINUATION is only legal while a header block
         * is open. A leftover stream id after END_HEADERS is not enough. */
        if (!client->pending_active ||
            header->stream_id == 0 ||
            header->stream_id != client->pending_stream_id)
            return -1;
        nc_mutex_lock(&client->state_lock);
        if (h2_client_append_pending(client, payload,
                                     header->length) != 0 ||
            ++client->pending_continuations > H2_MAX_CONTINUATION_FRAMES) {
            nc_mutex_unlock(&client->state_lock);
            return -1;
        }
        int result = 0;
        int already_done = 0;
        if (header->flags & NC_H2_FLAG_END_HEADERS) {
            h2_client_stream_t *cont_stream =
                h2_client_find_stream(client, header->stream_id);
            already_done = cont_stream && cont_stream->done;
            result = h2_client_decode_pending(
                client, client->pending_end_stream);
        }
        nc_mutex_unlock(&client->state_lock);
        if (already_done && result == 0) {
            (void)h2_client_write_u32(client, NC_H2_FRAME_RST_STREAM,
                                      header->stream_id,
                                      NC_H2_STREAM_CLOSED);
            return 0;
        }
        return result;
    }
    case NC_H2_FRAME_DATA: {
        if (header->stream_id == 0) return -1;
        const uint8_t *data = NULL;
        size_t data_length = 0;
        if (h2_client_data_fragment(header, payload, &data,
                                    &data_length) != 0)
            return -1;
        nc_mutex_lock(&client->state_lock);
        if (header->length > (uint32_t)client->conn_recv_window ||
            client->conn_recv_window < 0) {
            nc_mutex_unlock(&client->state_lock);
            return -1;
        }
        h2_client_stream_t *stream =
            h2_client_find_stream(client, header->stream_id);
        if (!stream) {
            /* RFC 9113 §5.1: DATA on idle (or even/push) streams is a
             * connection error. DATA on a stream we already released is
             * treated as STREAM_CLOSED and ignored after refunding the
             * connection window. */
            if ((header->stream_id & 1U) == 0 ||
                header->stream_id >= client->next_stream_id) {
                nc_mutex_unlock(&client->state_lock);
                return -1;
            }
            client->conn_recv_window -= (int32_t)header->length;
            client->conn_recv_window += (int32_t)header->length;
            nc_mutex_unlock(&client->state_lock);
            if (header->length > 0)
                (void)h2_client_write_u32(
                    client, NC_H2_FRAME_WINDOW_UPDATE, 0, header->length);
            return 0;
        }
        if (stream->done) {
            /* RFC 9113 §5.1 / §6.1: DATA after remote END_STREAM.
             * Refund connection flow control and leave the existing error. */
            client->conn_recv_window -= (int32_t)header->length;
            client->conn_recv_window += (int32_t)header->length;
            nc_mutex_unlock(&client->state_lock);
            if (header->length > 0)
                (void)h2_client_write_u32(
                    client, NC_H2_FRAME_WINDOW_UPDATE, 0, header->length);
            (void)h2_client_write_u32(client, NC_H2_FRAME_RST_STREAM,
                                      header->stream_id,
                                      NC_H2_STREAM_CLOSED);
            return 0;
        }
        int length_invalid = stream->body_forbidden && data_length > 0;
        if (!length_invalid && stream->content_length_seen &&
            (uint64_t)data_length >
                stream->content_length -
                    (stream->received_body_size <= stream->content_length
                         ? (uint64_t)stream->received_body_size
                         : stream->content_length))
            length_invalid = 1;
        int invalid = !stream->response_started ||
            stream->recv_window < 0 ||
            header->length > (uint32_t)stream->recv_window ||
            data_length > client->config.max_response_body_size ||
            stream->received_body_size >
                client->config.max_response_body_size - data_length ||
            length_invalid;
        neverc_h2_client_event_t *data_event = NULL;
        if (!invalid && stream->live) {
            data_event = (neverc_h2_client_event_t *)calloc(
                1, sizeof(*data_event));
            if (data_event && data_length > 0) {
                data_event->data = (uint8_t *)malloc(data_length);
                if (!data_event->data) {
                    free(data_event);
                    data_event = NULL;
                }
            }
            if (!data_event) {
                invalid = 1;
            } else {
                data_event->type = NEVERC_H2_CLIENT_EVENT_DATA;
                if (data_length > 0)
                    memcpy(data_event->data, data, data_length);
                data_event->data_length = data_length;
                data_event->flow_controlled_length = data_length;
                if (h2_client_push_event(stream, data_event) != 0) {
                    data_event = NULL;
                    invalid = 1;
                }
            }
        } else if (!invalid && !stream->live && data_length > 0 &&
                   nc_buf_append(&stream->body, data, data_length) != 0) {
            invalid = 1;
        }
        if (!invalid)
            stream->received_data = 1;
        if (invalid) {
            stream->error = length_invalid
                ? "invalid HTTP/2 response content length"
                : "invalid or oversized HTTP/2 response body";
            stream->error_code = length_invalid
                ? NC_H2_PROTOCOL_ERROR : NC_H2_ENHANCE_YOUR_CALM;
            stream->done = 1;
            h2_client_push_terminal_event(
                stream, stream->error, stream->error_code);
            nc_cond_broadcast(&stream->changed);
            client->conn_recv_window -= (int32_t)header->length;
            client->conn_recv_window += (int32_t)header->length;
            nc_mutex_unlock(&client->state_lock);
            if (header->length > 0)
                (void)h2_client_write_u32(
                    client, NC_H2_FRAME_WINDOW_UPDATE, 0, header->length);
            (void)h2_client_write_u32(client, NC_H2_FRAME_RST_STREAM,
                                      header->stream_id,
                                      stream->error_code);
            return 0;
        }
        client->conn_recv_window -= (int32_t)header->length;
        stream->recv_window -= (int32_t)header->length;
        client->conn_recv_window += (int32_t)header->length;
        stream->received_body_size += data_length;
        uint32_t stream_update = header->length;
        if (stream->live) {
            stream_update = header->length - (uint32_t)data_length;
            stream->recv_window += (int32_t)stream_update;
        } else {
            stream->recv_window += (int32_t)header->length;
        }
        if (header->flags & NC_H2_FLAG_END_STREAM) {
            int length_error =
                h2_client_response_length_valid(stream) != 0;
            if (length_error) {
                stream->error =
                    "truncated HTTP/2 response content length";
                stream->error_code = NC_H2_PROTOCOL_ERROR;
            }
            stream->done = 1;
            h2_client_push_terminal_event(
                stream, stream->error, stream->error_code);
            nc_cond_broadcast(&stream->changed);
        }
        nc_mutex_unlock(&client->state_lock);
        if (header->length > 0 && h2_client_write_u32(
                client, NC_H2_FRAME_WINDOW_UPDATE, 0,
                header->length) != 0)
            return -1;
        if (stream_update > 0 && h2_client_write_u32(
                client, NC_H2_FRAME_WINDOW_UPDATE,
                header->stream_id, stream_update) != 0)
            return -1;
        return 0;
    }
    case NC_H2_FRAME_RST_STREAM: {
        if (header->stream_id == 0 || header->length != 4) return -1;
        uint32_t code = ((uint32_t)payload[0] << 24) |
                        ((uint32_t)payload[1] << 16) |
                        ((uint32_t)payload[2] << 8) | payload[3];
        nc_mutex_lock(&client->state_lock);
        h2_client_stream_t *stream =
            h2_client_find_stream(client, header->stream_id);
        /* RFC 9113 §6.4: RST_STREAM on an idle stream is a connection
         * PROTOCOL_ERROR. Even identifiers are never client-owned. */
        if (!stream &&
            ((header->stream_id & 1U) == 0 ||
             header->stream_id >= client->next_stream_id)) {
            nc_mutex_unlock(&client->state_lock);
            return -1;
        }
        if (stream && !stream->done) {
            stream->error_code = code;
            stream->error = "HTTP/2 stream reset by peer";
            stream->done = 1;
            h2_client_push_terminal_event(stream, stream->error, code);
            nc_cond_broadcast(&stream->changed);
        }
        nc_mutex_unlock(&client->state_lock);
        return 0;
    }
    case NC_H2_FRAME_GOAWAY: {
        if (header->stream_id != 0 || header->length < 8) return -1;
        uint32_t last_stream = ((uint32_t)(payload[0] & 0x7f) << 24) |
                               ((uint32_t)payload[1] << 16) |
                               ((uint32_t)payload[2] << 8) | payload[3];
        nc_mutex_lock(&client->state_lock);
        client->goaway_received = 1;
        client->goaway_last_stream = last_stream;
        for (h2_client_stream_t *stream = client->streams; stream;
             stream = stream->next) {
            if (!stream->done && stream->id > last_stream) {
                stream->error_code = NC_H2_REFUSED_STREAM;
                stream->error = "request was not processed before GOAWAY";
                stream->done = 1;
                h2_client_push_terminal_event(
                    stream, stream->error, NC_H2_REFUSED_STREAM);
                nc_cond_broadcast(&stream->changed);
            }
        }
        nc_cond_broadcast(&client->window_changed);
        nc_mutex_unlock(&client->state_lock);
        return 0;
    }
    case NC_H2_FRAME_PRIORITY: {
        if (header->stream_id == 0 || header->length != 5 || !payload)
            return -1;
        uint32_t dependency = ((uint32_t)(payload[0] & 0x7f) << 24) |
                              ((uint32_t)payload[1] << 16) |
                              ((uint32_t)payload[2] << 8) |
                              (uint32_t)payload[3];
        /* RFC 9113 §6.3: a stream cannot depend on itself. */
        return dependency == header->stream_id ? -1 : 0;
    }
    case NC_H2_FRAME_PUSH_PROMISE:
        return -1;
    default:
        return 0;
    }
}

static void *h2_client_reader_main(void *argument) {
    neverc_h2_client_t *client = (neverc_h2_client_t *)argument;
    while (nc_atomic_load(&client->running)) {
        uint8_t encoded_header[NC_H2_FRAME_HEADER_SIZE];
        if (h2_client_transport_read_all(client, encoded_header,
                                         sizeof(encoded_header)) != 0)
            break;
        neverc_h2_frame_header_t header;
        if (neverc_h2_frame_header_read(encoded_header,
                                        sizeof(encoded_header),
                                        &header) != 0 ||
            header.length > client->local_settings.max_frame_size)
            break;
        uint8_t *payload = header.length
            ? (uint8_t *)malloc(header.length) : NULL;
        if (header.length && (!payload ||
            h2_client_transport_read_all(client, payload,
                                         header.length) != 0)) {
            free(payload);
            break;
        }
        int result = h2_client_reader_frame(client, &header, payload);
        free(payload);
        if (result != 0) break;
    }
    h2_client_fail_transport(client, "HTTP/2 connection closed");
    return NULL;
}

static void h2_client_context_wait_tick(neverc_h2_client_t *client,
                                        h2_client_stream_t *stream) {
#ifdef _WIN32
    (void)SleepConditionVariableCS(&stream->changed, &client->state_lock,
                                   H2_CLIENT_CONTEXT_POLL_MS);
#else
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) return;
    deadline.tv_nsec += H2_CLIENT_CONTEXT_POLL_MS * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    (void)pthread_cond_timedwait(&stream->changed,
                                 &client->state_lock, &deadline);
#endif
}

static void h2_client_window_wait_tick(neverc_h2_client_t *client) {
#ifdef _WIN32
    (void)SleepConditionVariableCS(&client->window_changed,
                                   &client->state_lock,
                                   H2_CLIENT_CONTEXT_POLL_MS);
#else
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) return;
    deadline.tv_nsec += H2_CLIENT_CONTEXT_POLL_MS * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    (void)pthread_cond_timedwait(&client->window_changed,
                                 &client->state_lock, &deadline);
#endif
}

static int h2_client_send_header_block(
    neverc_h2_client_t *client, uint32_t stream_id,
    const neverc_hpack_header_t *headers, int header_count,
    int end_stream) {
    nc_mutex_lock(&client->state_lock);
    size_t capacity = client->peer_settings.max_header_list_size;
    uint32_t max_frame_size = client->peer_settings.max_frame_size;
    nc_mutex_unlock(&client->state_lock);
    if (capacity == 0 || capacity > 1024U * 1024U)
        capacity = 1024U * 1024U;
    uint8_t *block = (uint8_t *)malloc(capacity);
    if (!block) return -1;
    nc_mutex_lock(&client->write_lock);
    size_t block_length = 0;
    int result = neverc_hpack_encode(client->encoder, headers, header_count,
                                     block, capacity, &block_length);
    size_t offset = 0;
    int first = 1;
    while (result == 0 && (first || offset < block_length)) {
        size_t chunk = block_length - offset;
        if (chunk > max_frame_size)
            chunk = max_frame_size;
        uint8_t flags = offset + chunk == block_length
            ? NC_H2_FLAG_END_HEADERS : 0;
        if (first && end_stream) flags |= NC_H2_FLAG_END_STREAM;
        result = h2_client_write_frame_locked(
            client, first ? NC_H2_FRAME_HEADERS
                          : NC_H2_FRAME_CONTINUATION,
            flags, stream_id, block + offset, (uint32_t)chunk);
        first = 0;
        offset += chunk;
    }
    nc_mutex_unlock(&client->write_lock);
    free(block);
    return result;
}

static int h2_client_send_body(neverc_h2_client_t *client,
                               h2_client_stream_t *stream,
                               neverc_context_t *context,
                               const uint8_t *body, size_t length,
                               int end_stream) {
    size_t offset = 0;
    while (offset < length) {
        nc_mutex_lock(&client->state_lock);
        while (nc_atomic_load(&client->running) && !stream->error &&
               !neverc_context_done(context) &&
               (client->conn_send_window <= 0 ||
                stream->send_window <= 0))
            h2_client_window_wait_tick(client);
        if (!nc_atomic_load(&client->running) || stream->error ||
            neverc_context_done(context)) {
            nc_mutex_unlock(&client->state_lock);
            return -1;
        }
        size_t chunk = length - offset;
        if (chunk > client->peer_settings.max_frame_size)
            chunk = client->peer_settings.max_frame_size;
        /* SETTINGS can shrink a stream window below zero. A size_t cast
         * of that negative value would wrap and ignore flow control. */
        if (client->conn_send_window <= 0 || stream->send_window <= 0) {
            nc_mutex_unlock(&client->state_lock);
            continue;
        }
        if (chunk > (size_t)client->conn_send_window)
            chunk = (size_t)client->conn_send_window;
        if (chunk > (size_t)stream->send_window)
            chunk = (size_t)stream->send_window;
        client->conn_send_window -= (int32_t)chunk;
        stream->send_window -= (int32_t)chunk;
        nc_mutex_unlock(&client->state_lock);
        uint8_t flags = end_stream && offset + chunk == length
            ? NC_H2_FLAG_END_STREAM : 0;
        if (h2_client_write_frame(client, NC_H2_FRAME_DATA, flags,
                                  stream->id, body + offset,
                                  (uint32_t)chunk) != 0)
            return -1;
        offset += chunk;
    }
    return 0;
}

static neverc_context_t *h2_client_operation_context(
    neverc_h2_client_t *client, neverc_context_t *parent,
    neverc_context_t **owned_background_out) {
    neverc_context_t *owned_background = NULL;
    if (!parent) {
        parent = owned_background = neverc_context_background();
        if (!parent)
            return NULL;
    }
    neverc_context_t *context = neverc_context_with_timeout(
        parent, client->config.timeout_ms, NULL);
    if (!context) {
        neverc_context_free(owned_background);
        return NULL;
    }
    if (owned_background_out)
        *owned_background_out = owned_background;
    return context;
}

neverc_h2_client_stream_t *neverc_h2_client_stream_open(
    neverc_h2_client_t *client, neverc_context_t *parent_context,
    const char *method, const char *path,
    const neverc_hpack_header_t *extra_headers, size_t extra_count,
    int end_stream, const char **error) {
    if (error) *error = NULL;
    if (!client || !method || !method[0] || !h2_client_path_valid(path) ||
        extra_count > 64 || (extra_count && !extra_headers) ||
        (end_stream != 0 && end_stream != 1)) {
        if (error) *error = "invalid HTTP/2 stream request";
        return NULL;
    }
    for (const unsigned char *cursor = (const unsigned char *)method;
         *cursor; cursor++) {
        if (*cursor <= 0x20 || *cursor >= 0x7f) {
            if (error) *error = "invalid HTTP method";
            return NULL;
        }
    }
    for (size_t i = 0; i < extra_count; i++) {
        const char *name = extra_headers[i].name;
        const char *value = extra_headers[i].value;
        if (!h2_client_name_valid(name) || !h2_client_value_valid(value) ||
            name[0] == ':' || strcmp(name, "connection") == 0 ||
            strcmp(name, "proxy-connection") == 0 ||
            strcmp(name, "keep-alive") == 0 ||
            strcmp(name, "transfer-encoding") == 0 ||
            strcmp(name, "upgrade") == 0 ||
            (strcmp(name, "te") == 0 &&
             strcasecmp(value, "trailers") != 0)) {
            if (error) *error = "invalid HTTP/2 header";
            return NULL;
        }
    }
    neverc_context_t *owned_background = NULL;
    neverc_context_t *context = h2_client_operation_context(
        client, parent_context, &owned_background);
    if (!context) {
        if (error) *error = "out of memory";
        return NULL;
    }
    h2_client_stream_t *stream =
        (h2_client_stream_t *)calloc(1, sizeof(*stream));
    if (!stream) {
        neverc_context_free(context);
        neverc_context_free(owned_background);
        if (error) *error = "out of memory";
        return NULL;
    }
    stream->client = client;
    stream->live = 1;
    stream->head_request = strcmp(method, "HEAD") == 0;
    stream->local_closed = end_stream;
    stream->receive_queue = neverc_thread_channel_create(8);
    nc_buf_init(&stream->body);
    nc_cond_init(&stream->changed);
    stream->head_request = strcmp(method, "HEAD") == 0;
    if (!stream->receive_queue) {
        h2_client_stream_destroy(stream);
        neverc_context_free(context);
        neverc_context_free(owned_background);
        if (error) *error = "out of memory";
        return NULL;
    }

    nc_mutex_lock(&client->state_lock);
    while (nc_atomic_load(&client->running) &&
           !client->goaway_received &&
           client->active_streams >=
               client->peer_settings.max_concurrent_streams &&
           !neverc_context_done(context))
        h2_client_window_wait_tick(client);
    if (!nc_atomic_load(&client->running) || client->goaway_received ||
        neverc_context_done(context) ||
        client->next_stream_id > 0x7fffffffU) {
        nc_mutex_unlock(&client->state_lock);
        h2_client_stream_destroy(stream);
        neverc_context_free(context);
        neverc_context_free(owned_background);
        if (error) *error = "HTTP/2 connection cannot accept a stream";
        return NULL;
    }
    stream->id = client->next_stream_id;
    client->next_stream_id += 2;
    stream->send_window =
        (int32_t)client->peer_settings.initial_window_size;
    stream->recv_window =
        (int32_t)client->local_settings.initial_window_size;
    stream->next = client->streams;
    client->streams = stream;
    stream->linked = 1;
    client->active_streams++;
    nc_mutex_unlock(&client->state_lock);

    neverc_hpack_header_t request_headers[4 + 64];
    int request_count = 0;
    request_headers[request_count++] = (neverc_hpack_header_t){
        .name = ":method", .value = (char *)method};
    request_headers[request_count++] = (neverc_hpack_header_t){
        .name = ":scheme", .value = (char *)client->scheme};
    request_headers[request_count++] = (neverc_hpack_header_t){
        .name = ":path", .value = (char *)path};
    request_headers[request_count++] = (neverc_hpack_header_t){
        .name = ":authority", .value = client->authority};
    for (size_t i = 0; i < extra_count; i++)
        request_headers[request_count++] = extra_headers[i];
    int result = h2_client_send_header_block(
        client, stream->id, request_headers, request_count, end_stream);
    neverc_context_free(context);
    neverc_context_free(owned_background);
    if (result == 0) return stream;

    nc_mutex_lock(&client->state_lock);
    h2_client_remove_stream_locked(client, stream);
    nc_mutex_unlock(&client->state_lock);
    h2_client_stream_destroy(stream);
    h2_client_fail_transport(client, "HTTP/2 stream header write failed");
    if (error) *error = "HTTP/2 stream header write failed";
    return NULL;
}

int neverc_h2_client_stream_send(
    neverc_h2_client_stream_t *stream, neverc_context_t *parent_context,
    const void *data, size_t length, int end_stream) {
    if (!stream || !stream->client || (length > 0 && !data) ||
        (end_stream != 0 && end_stream != 1))
        return -1;
    neverc_h2_client_t *client = stream->client;
    neverc_context_t *owned_background = NULL;
    neverc_context_t *context = h2_client_operation_context(
        client, parent_context, &owned_background);
    if (!context) return -1;
    nc_mutex_lock(&client->state_lock);
    int permitted = stream->linked && !stream->local_closed &&
                    !stream->error &&
                    nc_atomic_load(&client->running);
    nc_mutex_unlock(&client->state_lock);
    if (!permitted) {
        neverc_context_free(context);
        neverc_context_free(owned_background);
        return -1;
    }
    int result = length > 0
        ? h2_client_send_body(client, stream, context,
                              (const uint8_t *)data, length, end_stream)
        : (end_stream
            ? h2_client_write_frame(client, NC_H2_FRAME_DATA,
                                    NC_H2_FLAG_END_STREAM, stream->id,
                                    NULL, 0)
            : 0);
    if (result == 0 && end_stream) {
        nc_mutex_lock(&client->state_lock);
        stream->local_closed = 1;
        nc_mutex_unlock(&client->state_lock);
    }
    neverc_context_free(context);
    neverc_context_free(owned_background);
    if (result != 0)
        neverc_h2_client_stream_cancel(stream, NC_H2_CANCEL);
    return result;
}

int neverc_h2_client_stream_receive(
    neverc_h2_client_stream_t *stream, neverc_context_t *context,
    neverc_h2_client_event_t **event) {
    if (event) *event = NULL;
    if (!stream || !stream->client || !stream->receive_queue || !event)
        return -1;
    void *value = NULL;
    int result = context
        ? neverc_thread_channel_receive_context(
              stream->receive_queue, context, &value)
        : neverc_thread_channel_receive(stream->receive_queue, &value);
    if (result == NEVERC_THREAD_CLOSED) return 0;
    if (result != NEVERC_THREAD_OK || !value) return -1;
    neverc_h2_client_event_t *received =
        (neverc_h2_client_event_t *)value;
    if (received->type == NEVERC_H2_CLIENT_EVENT_DATA &&
        received->flow_controlled_length > 0) {
        neverc_h2_client_t *client = stream->client;
        size_t increment = received->flow_controlled_length;
        if (increment > (size_t)INT32_MAX) {
            neverc_h2_client_event_free(received);
            return -1;
        }
        int32_t signed_increment = (int32_t)increment;
        nc_mutex_lock(&client->state_lock);
        if (stream->recv_window > INT32_MAX - signed_increment) {
            nc_mutex_unlock(&client->state_lock);
            neverc_h2_client_event_free(received);
            return -1;
        }
        stream->recv_window += signed_increment;
        nc_mutex_unlock(&client->state_lock);
        if (h2_client_write_u32(client, NC_H2_FRAME_WINDOW_UPDATE,
                                stream->id, (uint32_t)increment) != 0) {
            neverc_h2_client_event_free(received);
            return -1;
        }
        received->flow_controlled_length = 0;
    }
    *event = received;
    return 1;
}

void neverc_h2_client_stream_cancel(neverc_h2_client_stream_t *stream,
                                     uint32_t error_code) {
    if (!stream || !stream->client) return;
    neverc_h2_client_t *client = stream->client;
    uint32_t code = error_code ? error_code : NC_H2_CANCEL;
    nc_mutex_lock(&client->state_lock);
    int was_linked = stream->linked;
    if (was_linked)
        h2_client_remove_stream_locked(client, stream);
    if (!stream->done) {
        stream->error = "HTTP/2 stream cancelled";
        stream->error_code = code;
        stream->done = 1;
        h2_client_push_terminal_event(stream, stream->error, code);
    }
    stream->local_closed = 1;
    nc_mutex_unlock(&client->state_lock);
    if (was_linked && nc_atomic_load(&client->running))
        (void)h2_client_write_u32(client, NC_H2_FRAME_RST_STREAM,
                                  stream->id, code);
}

void neverc_h2_client_stream_free(neverc_h2_client_stream_t *stream) {
    if (!stream) return;
    neverc_h2_client_t *client = stream->client;
    if (client) {
        nc_mutex_lock(&client->state_lock);
        int linked = stream->linked;
        int complete = stream->done && stream->local_closed;
        if (linked && complete)
            h2_client_remove_stream_locked(client, stream);
        nc_mutex_unlock(&client->state_lock);
        if (linked && !complete)
            neverc_h2_client_stream_cancel(stream, NC_H2_CANCEL);
    }
    h2_client_stream_destroy(stream);
}

neverc_h2_client_config_t neverc_h2_client_config_default(void) {
    neverc_h2_client_config_t config;
    config.timeout_ms = 30000;
    config.max_concurrent_streams = 100;
    config.initial_window_size = NC_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    config.max_response_header_list_size =
        NC_H2_DEFAULT_MAX_HEADER_LIST_SIZE;
    config.max_response_body_size = 16U * 1024U * 1024U;
    config.root_cert_file = NULL;
    config.client_cert_file = NULL;
    config.client_key_file = NULL;
    config.insecure_skip_verify = 0;
    return config;
}

static int h2_client_config_valid(
    const neverc_h2_client_config_t *config) {
    return config && config->timeout_ms > 0 &&
           config->max_concurrent_streams > 0 &&
           config->initial_window_size <= INT32_MAX &&
           config->max_response_header_list_size > 0 &&
           config->max_response_header_list_size <= UINT32_MAX &&
           config->max_response_body_size > 0 &&
           (config->insecure_skip_verify == 0 ||
            config->insecure_skip_verify == 1) &&
           ((config->client_cert_file == NULL) ==
            (config->client_key_file == NULL));
}

neverc_h2_client_t *neverc_h2_client_dial_context(
    const char *addr, const char *server_name, int use_tls,
    const neverc_h2_client_config_t *input_config,
    neverc_context_t *parent_context, const char **error) {
    if (error) *error = NULL;
    neverc_h2_client_config_t config = input_config
        ? *input_config : neverc_h2_client_config_default();
    if (!addr || !server_name || !server_name[0] ||
        (use_tls != 0 && use_tls != 1) ||
        (!use_tls && (config.root_cert_file || config.client_cert_file ||
                      config.client_key_file ||
                      config.insecure_skip_verify)) ||
        !h2_client_config_valid(&config)) {
        if (error) *error = "invalid HTTP/2 client configuration";
        return NULL;
    }
    neverc_context_t *owned_background = NULL;
    neverc_context_t *parent = parent_context;
    if (!parent) {
        parent = owned_background = neverc_context_background();
        if (!parent) {
            if (error) *error = "out of memory";
            return NULL;
        }
    }
    neverc_context_t *context = neverc_context_with_timeout(
        parent, config.timeout_ms, NULL);
    if (!context) {
        neverc_context_free(owned_background);
        if (error) *error = "out of memory";
        return NULL;
    }
    neverc_tcp_conn_t *tcp = NULL;
    neverc_net_result_t dialed =
        neverc_tcp_dial_context(addr, context, &tcp);
    if (dialed.status != NEVERC_NET_OK || !tcp) {
        neverc_context_free(context);
        neverc_context_free(owned_background);
        if (error) *error = "HTTP/2 TCP dial failed";
        return NULL;
    }
    int64_t deadline = neverc_context_deadline(context);
    if (deadline > 0) {
        (void)neverc_tcp_set_read_deadline(tcp, deadline);
        (void)neverc_tcp_set_write_deadline(tcp, deadline);
    }

    neverc_tls_conn_t *tls = NULL;
    if (use_tls) {
        neverc_tls_config_t *tls_config = neverc_tls_config_new();
        if (!tls_config) {
            neverc_tcp_close(tcp);
            neverc_context_free(context);
            neverc_context_free(owned_background);
            if (error) *error = "out of memory";
            return NULL;
        }
        if ((config.root_cert_file &&
             neverc_tls_config_add_root_certificates(
                 tls_config, config.root_cert_file) != 0) ||
            (config.client_cert_file &&
             neverc_tls_config_load_cert(tls_config,
                                         config.client_cert_file,
                                         config.client_key_file) != 0)) {
            neverc_tls_config_free(tls_config);
            neverc_tcp_close(tcp);
            neverc_context_free(context);
            neverc_context_free(owned_background);
            if (error) *error = "invalid HTTP/2 TLS configuration";
            return NULL;
        }
        if (config.insecure_skip_verify)
            neverc_tls_config_insecure_skip_verify(tls_config);
        neverc_tls_config_set_server_name(tls_config, server_name);
        const char *alpn[] = {"h2"};
        neverc_tls_config_set_alpn(tls_config, alpn, 1);
        const char *tls_error = NULL;
        tls = neverc_tls_client(tcp, tls_config, &tls_error);
        neverc_tls_config_free(tls_config);
        if (!tls || !neverc_tls_alpn(tls) ||
            strcmp(neverc_tls_alpn(tls), "h2") != 0) {
            if (tls) neverc_tls_close(tls);
            neverc_tcp_close(tcp);
            neverc_context_free(context);
            neverc_context_free(owned_background);
            if (error) *error = tls_error
                ? tls_error : "HTTP/2 ALPN negotiation failed";
            return NULL;
        }
    }

    neverc_h2_client_t *client =
        (neverc_h2_client_t *)calloc(1, sizeof(*client));
    if (!client) {
        if (tls) neverc_tls_close(tls);
        neverc_tcp_close(tcp);
        neverc_context_free(context);
        neverc_context_free(owned_background);
        if (error) *error = "out of memory";
        return NULL;
    }
    client->config = config;
    client->config.root_cert_file = NULL;
    client->config.client_cert_file = NULL;
    client->config.client_key_file = NULL;
    client->tcp = tcp;
    client->tls = tls;
    client->authority = strdup(server_name);
    client->scheme = use_tls ? "https" : "http";
    client->next_stream_id = 1;
    client->conn_send_window = NC_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    client->conn_recv_window = NC_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    neverc_h2_settings_init(&client->local_settings);
    neverc_h2_settings_init(&client->peer_settings);
    client->local_settings.enable_push = 0;
    client->local_settings.max_concurrent_streams =
        config.max_concurrent_streams;
    client->local_settings.initial_window_size =
        config.initial_window_size;
    client->local_settings.max_header_list_size =
        (uint32_t)config.max_response_header_list_size;
    client->encoder = neverc_hpack_encoder_create(
        NC_H2_DEFAULT_HEADER_TABLE_SIZE);
    client->decoder = neverc_hpack_decoder_create(
        client->local_settings.header_table_size);
    nc_mutex_init(&client->state_lock);
    nc_mutex_init(&client->write_lock);
    nc_cond_init(&client->window_changed);
    if (!client->authority || !client->encoder || !client->decoder) {
        if (error) *error = "out of memory";
        neverc_context_free(context);
        neverc_context_free(owned_background);
        neverc_h2_client_free(client);
        return NULL;
    }
    nc_atomic_store(&client->running, 1);
    if (h2_client_transport_write_all(
            client, NC_H2_CLIENT_PREFACE,
            NC_H2_CLIENT_PREFACE_LEN) != 0 ||
        h2_client_write_settings(client) != 0) {
        if (error) *error = "failed to start HTTP/2 connection";
        neverc_context_free(context);
        neverc_context_free(owned_background);
        neverc_h2_client_free(client);
        return NULL;
    }
    if (deadline > 0) {
        (void)neverc_tcp_set_read_deadline(tcp, 0);
        (void)neverc_tcp_set_write_deadline(tcp, 0);
    }
    if (nc_thread_create(&client->reader_thread,
                         h2_client_reader_main, client) != 0) {
        if (error) *error = "failed to start HTTP/2 connection";
        neverc_context_free(context);
        neverc_context_free(owned_background);
        neverc_h2_client_free(client);
        return NULL;
    }
    client->reader_started = 1;
    neverc_context_free(context);
    neverc_context_free(owned_background);
    return client;
}

neverc_h2_client_t *neverc_h2_client_dial(
    const char *addr, const char *server_name, int use_tls,
    const neverc_h2_client_config_t *config, const char **error) {
    return neverc_h2_client_dial_context(
        addr, server_name, use_tls, config, NULL, error);
}

static neverc_h2_response_t *h2_client_error_response(
    const char *error, uint32_t code) {
    neverc_h2_response_t *response =
        (neverc_h2_response_t *)calloc(1, sizeof(*response));
    if (response) {
        response->error = error;
        response->stream_error = code;
    }
    return response;
}

neverc_h2_response_t *neverc_h2_client_do_context(
    neverc_h2_client_t *client, neverc_context_t *parent_context,
    const char *method, const char *path,
    const neverc_hpack_header_t *extra_headers, size_t extra_count,
    const void *body, size_t body_length) {
    if (!client || !method || !method[0] || !h2_client_path_valid(path) ||
        (body_length > 0 && !body) || extra_count > 64 ||
        (extra_count > 0 && !extra_headers))
        return h2_client_error_response("invalid HTTP/2 request", 0);
    for (const unsigned char *cursor = (const unsigned char *)method;
         *cursor; cursor++)
        if (*cursor <= 0x20 || *cursor >= 0x7f)
            return h2_client_error_response("invalid HTTP method", 0);
    for (size_t i = 0; i < extra_count; i++) {
        const char *name = extra_headers[i].name;
        const char *value = extra_headers[i].value;
        if (!h2_client_name_valid(name) || !h2_client_value_valid(value) ||
            name[0] == ':' || strcmp(name, "connection") == 0 ||
            strcmp(name, "proxy-connection") == 0 ||
            strcmp(name, "keep-alive") == 0 ||
            strcmp(name, "transfer-encoding") == 0 ||
            strcmp(name, "upgrade") == 0 ||
            strcmp(name, "content-length") == 0 ||
            (strcmp(name, "te") == 0 &&
             strcasecmp(value, "trailers") != 0))
            return h2_client_error_response("invalid HTTP/2 header", 0);
    }
    neverc_context_t *owned_background = NULL;
    neverc_context_t *context = h2_client_operation_context(
        client, parent_context, &owned_background);
    if (!context)
        return h2_client_error_response("out of memory", 0);
    h2_client_stream_t *stream =
        (h2_client_stream_t *)calloc(1, sizeof(*stream));
    if (!stream) {
        neverc_context_free(context);
        neverc_context_free(owned_background);
        return h2_client_error_response("out of memory", 0);
    }
    nc_buf_init(&stream->body);
    nc_cond_init(&stream->changed);

    nc_mutex_lock(&client->state_lock);
    while (nc_atomic_load(&client->running) &&
           !client->goaway_received &&
           client->active_streams >=
               client->peer_settings.max_concurrent_streams &&
           !neverc_context_done(context))
        h2_client_window_wait_tick(client);
    if (!nc_atomic_load(&client->running) || client->goaway_received ||
        neverc_context_done(context) ||
        client->next_stream_id > 0x7fffffffU) {
        int draining = client->goaway_received;
        nc_mutex_unlock(&client->state_lock);
        h2_client_stream_destroy(stream);
        neverc_context_free(context);
        neverc_context_free(owned_background);
        return h2_client_error_response(
            draining ? "HTTP/2 connection is draining" :
            "HTTP/2 request cancelled before send", NC_H2_REFUSED_STREAM);
    }
    stream->id = client->next_stream_id;
    client->next_stream_id += 2;
    stream->send_window =
        (int32_t)client->peer_settings.initial_window_size;
    stream->recv_window =
        (int32_t)client->local_settings.initial_window_size;
    stream->next = client->streams;
    client->streams = stream;
    stream->linked = 1;
    client->active_streams++;
    nc_mutex_unlock(&client->state_lock);

    char content_length[32];
    int content_length_size = snprintf(
        content_length, sizeof(content_length), "%zu", body_length);
    neverc_hpack_header_t request_headers[4 + 64 + 1];
    int request_count = 0;
    request_headers[request_count++] = (neverc_hpack_header_t){
        .name = ":method", .value = (char *)method};
    request_headers[request_count++] = (neverc_hpack_header_t){
        .name = ":scheme", .value = (char *)client->scheme};
    request_headers[request_count++] = (neverc_hpack_header_t){
        .name = ":path", .value = (char *)path};
    request_headers[request_count++] = (neverc_hpack_header_t){
        .name = ":authority", .value = client->authority};
    for (size_t i = 0; i < extra_count; i++)
        request_headers[request_count++] = extra_headers[i];
    if (body_length > 0 && content_length_size > 0)
        request_headers[request_count++] = (neverc_hpack_header_t){
            .name = "content-length", .value = content_length};

    int sent = h2_client_send_header_block(
        client, stream->id, request_headers, request_count,
        body_length == 0);
    if (sent == 0 && body_length > 0)
        sent = h2_client_send_body(client, stream, context,
                                  (const uint8_t *)body, body_length, 1);
    if (sent != 0) {
        (void)h2_client_write_u32(client, NC_H2_FRAME_RST_STREAM,
                                  stream->id, NC_H2_CANCEL);
        nc_mutex_lock(&client->state_lock);
        if (!stream->done) {
            stream->error = neverc_context_done(context)
                ? "HTTP/2 request send cancelled"
                : "HTTP/2 request write failed";
            stream->error_code = NC_H2_CANCEL;
            stream->done = 1;
        }
        nc_mutex_unlock(&client->state_lock);
    }

    nc_mutex_lock(&client->state_lock);
    while (!stream->done && nc_atomic_load(&client->running) &&
           !neverc_context_done(context))
        h2_client_context_wait_tick(client, stream);
    int cancelled = !stream->done && neverc_context_done(context);
    if (cancelled) {
        stream->error = "HTTP/2 request cancelled";
        stream->error_code = NC_H2_CANCEL;
        stream->done = 1;
    } else if (!stream->done) {
        stream->error = "HTTP/2 connection closed";
        stream->done = 1;
    }
    h2_client_remove_stream_locked(client, stream);
    nc_mutex_unlock(&client->state_lock);
    if (cancelled)
        (void)h2_client_write_u32(client, NC_H2_FRAME_RST_STREAM,
                                  stream->id, NC_H2_CANCEL);

    neverc_h2_response_t *response =
        (neverc_h2_response_t *)calloc(1, sizeof(*response));
    if (response) {
        response->status_code = (int)stream->status_code;
        response->headers = stream->headers;
        response->header_count = stream->header_count;
        response->trailers = stream->trailers;
        response->trailer_count = stream->trailer_count;
        response->received_trailers = stream->received_trailers;
        response->received_data = stream->received_data;
        response->body = (uint8_t *)stream->body.data;
        response->body_length = stream->body.len;
        response->stream_error =
            stream->error ? stream->error_code : 0;
        response->error = stream->error;
        stream->headers = NULL;
        stream->header_count = 0;
        stream->trailers = NULL;
        stream->trailer_count = 0;
        stream->body.data = NULL;
        stream->body.len = 0;
        stream->body.cap = 0;
    }
    h2_client_stream_destroy(stream);
    neverc_context_free(context);
    neverc_context_free(owned_background);
    return response;
}

neverc_h2_response_t *neverc_h2_client_do(
    neverc_h2_client_t *client, const char *method, const char *path,
    const neverc_hpack_header_t *headers, size_t header_count,
    const void *body, size_t body_length) {
    return neverc_h2_client_do_context(
        client, NULL, method, path, headers, header_count,
        body, body_length);
}

void neverc_h2_response_free(neverc_h2_response_t *response) {
    if (!response) return;
    h2_client_free_headers(response->headers, response->header_count);
    h2_client_free_headers(response->trailers, response->trailer_count);
    free(response->body);
    free(response);
}

void neverc_h2_client_close(neverc_h2_client_t *client) {
    if (!client || !nc_atomic_cas(&client->closing, 0, 1)) return;
    if (nc_atomic_load(&client->running)) {
        uint32_t last_stream = client->next_stream_id > 1
            ? client->next_stream_id - 2 : 0;
        uint8_t goaway[8] = {
            (uint8_t)((last_stream >> 24) & 0x7f),
            (uint8_t)(last_stream >> 16),
            (uint8_t)(last_stream >> 8), (uint8_t)last_stream,
            0, 0, 0, 0};
        (void)h2_client_write_frame(client, NC_H2_FRAME_GOAWAY, 0, 0,
                                    goaway, sizeof(goaway));
    }
    h2_client_fail_transport(client, "HTTP/2 client closed");
    if (client->reader_started) {
        (void)nc_thread_join(client->reader_thread);
        client->reader_started = 0;
    }
}

void neverc_h2_client_free(neverc_h2_client_t *client) {
    if (!client) return;
    neverc_h2_client_close(client);
    while (client->streams) {
        h2_client_stream_t *next = client->streams->next;
        h2_client_stream_destroy(client->streams);
        client->streams = next;
    }
    if (client->tls) neverc_tls_close(client->tls);
    if (client->tcp) neverc_tcp_close(client->tcp);
    if (client->encoder) neverc_hpack_encoder_destroy(client->encoder);
    if (client->decoder) neverc_hpack_decoder_destroy(client->decoder);
    free(client->pending_headers);
    free(client->authority);
    nc_cond_destroy(&client->window_changed);
    nc_mutex_destroy(&client->write_lock);
    nc_mutex_destroy(&client->state_lock);
    free(client);
}
