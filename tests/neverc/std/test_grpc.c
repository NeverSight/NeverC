#include "neverc/std/context.h"
#include "neverc/std/net/grpc.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/thread.h"
#include "neverc/std/time.h"
#include "network_test_support.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run;
static int tests_failed;

#define CHECK(condition)                                                     \
    do {                                                                     \
        tests_run++;                                                         \
        if (!(condition)) {                                                  \
            tests_failed++;                                                  \
            printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #condition);  \
        }                                                                    \
    } while (0)

typedef struct {
    neverc_h2_server_t *server;
    char address[64];
    const neverc_network_test_files_t *files;
    int use_tls;
    int result;
} grpc_test_server_t;

static neverc_grpc_status_t grpc_test_unary_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    neverc_grpc_message_t message;
    if (neverc_grpc_server_stream_recv(stream, &message) != 1)
        return NEVERC_GRPC_INVALID_ARGUMENT;
    neverc_grpc_server_stream_set_header(stream, "x-neverc", "unary");
    neverc_grpc_server_stream_set_trailer(stream, "x-finished", "yes");
    if (neverc_grpc_server_stream_send(stream, message.data,
                                       message.length) != 0)
        return NEVERC_GRPC_INTERNAL;
    return neverc_grpc_server_stream_recv(stream, &message) == 0
        ? NEVERC_GRPC_OK : NEVERC_GRPC_INVALID_ARGUMENT;
}

static neverc_grpc_status_t grpc_test_bidi_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    neverc_grpc_message_t message;
    for (;;) {
        int received = neverc_grpc_server_stream_recv(stream, &message);
        if (received == 0) return NEVERC_GRPC_OK;
        if (received != 1 ||
            neverc_grpc_server_stream_send(stream, message.data,
                                            message.length) != 0)
            return NEVERC_GRPC_INTERNAL;
    }
}

static neverc_grpc_status_t grpc_test_server_streaming_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    neverc_grpc_message_t message;
    if (neverc_grpc_server_stream_recv(stream, &message) != 1 ||
        neverc_grpc_server_stream_recv(stream, &message) != 0)
        return NEVERC_GRPC_INVALID_ARGUMENT;
    static const char *responses[] = {"first", "second", "third"};
    for (size_t i = 0; i < sizeof(responses) / sizeof(responses[0]); i++)
        if (neverc_grpc_server_stream_send(
                stream, responses[i], strlen(responses[i])) != 0)
            return NEVERC_GRPC_INTERNAL;
    return NEVERC_GRPC_OK;
}

static neverc_grpc_status_t grpc_test_client_streaming_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    neverc_grpc_message_t message;
    size_t count = 0;
    while (neverc_grpc_server_stream_recv(stream, &message) == 1)
        count++;
    char response[32];
    int length = snprintf(response, sizeof(response), "%zu", count);
    if (length <= 0 || (size_t)length >= sizeof(response) ||
        neverc_grpc_server_stream_send(stream, response,
                                       (size_t)length) != 0)
        return NEVERC_GRPC_INTERNAL;
    return NEVERC_GRPC_OK;
}

static neverc_grpc_status_t grpc_test_bin_metadata_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    const char *value;
    neverc_grpc_message_t message;
    (void)context;
    value = neverc_grpc_server_stream_metadata(stream, "x-bin");
    if (!value || (unsigned char)value[0] != 0x01 ||
        (unsigned char)value[1] != 0x02 || value[2] != '\0')
        return NEVERC_GRPC_INVALID_ARGUMENT;
    if (neverc_grpc_server_stream_recv(stream, &message) != 1)
        return NEVERC_GRPC_INVALID_ARGUMENT;
    if (neverc_grpc_server_stream_send(stream, value, 2U) != 0)
        return NEVERC_GRPC_INTERNAL;
    return NEVERC_GRPC_OK;
}

static neverc_grpc_status_t grpc_test_reserved_metadata_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    neverc_grpc_message_t message;
    neverc_grpc_server_stream_set_header(stream, "grpc-status", "0");
    neverc_grpc_server_stream_set_header(stream, "grpc-message", "injected");
    neverc_grpc_server_stream_set_header(stream, "content-type", "text/plain");
    neverc_grpc_server_stream_set_trailer(stream, "grpc-status", "0");
    neverc_grpc_server_stream_set_trailer(stream, "grpc-message", "injected");
    neverc_grpc_server_stream_set_header(stream, "x-neverc", "reserved");
    if (neverc_grpc_server_stream_recv(stream, &message) != 1)
        return NEVERC_GRPC_INVALID_ARGUMENT;
    return NEVERC_GRPC_INTERNAL;
}

static neverc_grpc_status_t grpc_test_unary_end_early_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    neverc_grpc_message_t message;
    if (neverc_grpc_server_stream_recv(stream, &message) != 1)
        return NEVERC_GRPC_INVALID_ARGUMENT;
    if (neverc_grpc_server_stream_send(stream, "ok", 2U) != 0)
        return NEVERC_GRPC_INTERNAL;
    /* End without draining leftover request frames. Extra unary messages
     * must not be reported as OK. */
    return neverc_grpc_server_stream_end(stream, NEVERC_GRPC_OK, NULL) == 0
        ? NEVERC_GRPC_OK : NEVERC_GRPC_INTERNAL;
}

static neverc_grpc_status_t grpc_test_unary_return_ok_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    neverc_grpc_message_t message;
    if (neverc_grpc_server_stream_recv(stream, &message) != 1)
        return NEVERC_GRPC_INVALID_ARGUMENT;
    if (neverc_grpc_server_stream_send(stream, "ok", 2U) != 0)
        return NEVERC_GRPC_INTERNAL;
    /* Leave end() to the dispatcher. Extra unary DATA must still produce
     * grpc-status trailers, not RST-without-status. */
    return NEVERC_GRPC_OK;
}

static neverc_grpc_status_t grpc_test_bidi_end_early_handler(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    if (neverc_grpc_server_stream_send(stream, "hi", 2U) != 0)
        return NEVERC_GRPC_INTERNAL;
    return NEVERC_GRPC_OK;
}

static const neverc_grpc_method_t grpc_test_unary_method = {
    "/test.Echo/Unary", NEVERC_GRPC_UNARY, 1024U, 1024U,
    grpc_test_unary_handler, NULL};

static const neverc_grpc_method_t grpc_test_bidi_method = {
    "/test.Echo/Bidi", NEVERC_GRPC_BIDI_STREAMING, 1024U, 1024U,
    grpc_test_bidi_handler, NULL};

static const neverc_grpc_method_t grpc_test_server_streaming_method = {
    "/test.Echo/ServerStreaming", NEVERC_GRPC_SERVER_STREAMING, 1024U,
    1024U, grpc_test_server_streaming_handler, NULL};

static const neverc_grpc_method_t grpc_test_client_streaming_method = {
    "/test.Echo/ClientStreaming", NEVERC_GRPC_CLIENT_STREAMING, 1024U,
    1024U, grpc_test_client_streaming_handler, NULL};

static const neverc_grpc_method_t grpc_test_unary_end_early_method = {
    "/test.Echo/UnaryEndEarly", NEVERC_GRPC_UNARY, 1024U, 1024U,
    grpc_test_unary_end_early_handler, NULL};

static const neverc_grpc_method_t grpc_test_unary_return_ok_method = {
    "/test.Echo/UnaryReturnOk", NEVERC_GRPC_UNARY, 1024U, 1024U,
    grpc_test_unary_return_ok_handler, NULL};

static const neverc_grpc_method_t grpc_test_bidi_end_early_method = {
    "/test.Echo/BidiEndEarly", NEVERC_GRPC_BIDI_STREAMING, 1024U, 1024U,
    grpc_test_bidi_end_early_handler, NULL};

static const neverc_grpc_method_t grpc_test_reserved_metadata_method = {
    "/test.Echo/Reserved", NEVERC_GRPC_UNARY, 1024U, 1024U,
    grpc_test_reserved_metadata_handler, NULL};

static const neverc_grpc_method_t grpc_test_bin_metadata_method = {
    "/test.Echo/BinMeta", NEVERC_GRPC_UNARY, 1024U, 1024U,
    grpc_test_bin_metadata_handler, NULL};

static void grpc_test_server_task(void *context) {
    grpc_test_server_t *test = (grpc_test_server_t *)context;
    if (test->use_tls) {
        test->result = neverc_h2_listen_and_serve(
            test->address, test->server, test->files->server_cert,
            test->files->server_key);
    } else {
        test->result = neverc_h2_listen_and_serve_h2c(test->address,
                                                       test->server);
    }
}

static int grpc_test_free_port(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener = neverc_tcp_listen("127.0.0.1:0",
                                                         &error);
    if (!listener) return -1;
    neverc_tcp_addr_t address;
    int result = neverc_tcp_listener_addr(listener, &address) == 0
        ? (int)address.port : -1;
    neverc_tcp_listener_close(listener);
    return result;
}

static neverc_h2_client_t *grpc_test_dial(const char *address) {
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 200;
    for (int attempt = 0; attempt < 100; attempt++) {
        const char *error = NULL;
        neverc_h2_client_t *client = neverc_h2_client_dial(
            address, "localhost", 0, &config, &error);
        if (client) return client;
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    }
    return NULL;
}

static neverc_h2_client_t *grpc_test_dial_tls(
    const char *address, const neverc_network_test_files_t *files) {
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 500;
    config.root_cert_file = files->ca;
    for (int attempt = 0; attempt < 100; attempt++) {
        const char *error = NULL;
        neverc_h2_client_t *client = neverc_h2_client_dial(
            address, "localhost", 1, &config, &error);
        if (client) return client;
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    }
    return NULL;
}

static void grpc_test_frame_and_timeout(void) {
    uint8_t encoded[64];
    size_t encoded_length = 0;
    CHECK(neverc_grpc_frame_encode("hello", 5U, 0, encoded,
                                   sizeof(encoded), &encoded_length) == 0);
    CHECK(encoded_length == 10U);
    neverc_grpc_frame_reader_t reader;
    neverc_grpc_frame_reader_init(&reader, encoded, encoded_length, 16U);
    neverc_grpc_message_t message;
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == 1);
    CHECK(message.length == 5U && memcmp(message.data, "hello", 5U) == 0);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == 0);
    CHECK(neverc_grpc_frame_encode("hello", 5U, 1, encoded,
                                   sizeof(encoded), &encoded_length) == 0);
    neverc_grpc_frame_reader_init(&reader, encoded, encoded_length, 16U);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == -1);
    CHECK(neverc_grpc_frame_encode("hello", 5U, 2, encoded,
                                   sizeof(encoded), &encoded_length) == -1);
    CHECK(neverc_grpc_frame_encode("hello", 5U, 0, encoded, 4U,
                                   &encoded_length) == -1);
    uint8_t empty_message[] = {0, 0, 0, 0, 0};
    neverc_grpc_frame_reader_init(&reader, empty_message,
                                  sizeof(empty_message), 16U);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == 1);
    CHECK(message.length == 0U);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == 0);
    uint8_t reserved_flag[] = {2, 0, 0, 0, 0};
    neverc_grpc_frame_reader_init(&reader, reserved_flag,
                                  sizeof(reserved_flag), 16U);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == -1);
    uint8_t truncated[] = {0, 0, 0, 0, 5, 'h'};
    neverc_grpc_frame_reader_init(&reader, truncated, sizeof(truncated), 16U);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == -1);
    uint8_t leftover[] = {0, 0, 0, 0, 1, 'a', 0, 0, 0};
    neverc_grpc_frame_reader_init(&reader, leftover, sizeof(leftover), 16U);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == 1);
    CHECK(message.length == 1U && message.data[0] == 'a');
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == -1);
    uint8_t oversize[] = {0, 0, 0, 0, 32, 'x'};
    neverc_grpc_frame_reader_init(&reader, oversize, sizeof(oversize), 16U);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == -1);
    uint8_t huge_length[] = {0, 0xff, 0xff, 0xff, 0xff};
    neverc_grpc_frame_reader_init(&reader, huge_length, sizeof(huge_length),
                                  SIZE_MAX);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == -1);

    char timeout[10];
    int64_t timeout_ms = 0;
    CHECK(neverc_grpc_timeout_encode(UINT64_C(1500000000), timeout) == 0);
    CHECK(neverc_grpc_timeout_decode(timeout, &timeout_ms) == 0);
    CHECK(timeout_ms >= 1500 && timeout_ms <= 1501);
    CHECK(neverc_grpc_timeout_encode(0, timeout) == 0);
    CHECK(strcmp(timeout, "0n") == 0);
    CHECK(neverc_grpc_timeout_decode("0n", &timeout_ms) == 0);
    CHECK(timeout_ms == 0);
    CHECK(neverc_grpc_timeout_decode("99999999999S", &timeout_ms) == -1);
    CHECK(neverc_grpc_timeout_decode("1", &timeout_ms) == -1);
    CHECK(neverc_grpc_timeout_decode("1x", &timeout_ms) == -1);
    CHECK(neverc_grpc_timeout_decode("5124096H", &timeout_ms) == -1);
    CHECK(neverc_grpc_status_valid(NEVERC_GRPC_OK));
    CHECK(!neverc_grpc_status_valid(17U));
    CHECK(strcmp(neverc_grpc_status_name(NEVERC_GRPC_UNAUTHENTICATED),
                 "UNAUTHENTICATED") == 0);
    CHECK(strcmp(neverc_grpc_status_name((neverc_grpc_status_t)99),
                 "UNKNOWN") == 0);

    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    const char *error = NULL;
    CHECK(neverc_h2_client_dial("invalid:1", "localhost", 2, &config,
                                &error) == NULL && error != NULL);
    config.root_cert_file = "unused.pem";
    error = NULL;
    CHECK(neverc_h2_client_dial("invalid:1", "localhost", 0, &config,
                                &error) == NULL && error != NULL);
}

static void grpc_test_metadata_limits(void) {
    uint8_t byte = 0;
    neverc_grpc_metadata_t metadata = {
        "oversized-bin", &byte, SIZE_MAX};
    neverc_grpc_message_t request = {NULL, 0};
    unsigned char client_storage = 0;
    neverc_grpc_result_t *result = neverc_grpc_client_call(
        (neverc_h2_client_t *)(void *)&client_storage, NULL,
        "/test.Echo/Unary", NEVERC_GRPC_UNARY, &metadata, 1U,
        &request, 1U, 1024U);
    CHECK(result != NULL);
    CHECK(result && result->error &&
          strcmp(result->error, "invalid gRPC metadata") == 0);
    neverc_grpc_result_free(result);

    metadata.value_length = 8193U;
    result = neverc_grpc_client_call(
        (neverc_h2_client_t *)(void *)&client_storage, NULL,
        "/test.Echo/Unary", NEVERC_GRPC_UNARY, &metadata, 1U,
        &request, 1U, 1024U);
    CHECK(result != NULL);
    CHECK(result && result->error &&
          strcmp(result->error, "invalid gRPC metadata") == 0);
    neverc_grpc_result_free(result);

    neverc_grpc_metadata_t reserved = {
        "grpc-status", &byte, 1U};
    result = neverc_grpc_client_call(
        (neverc_h2_client_t *)(void *)&client_storage, NULL,
        "/test.Echo/Unary", NEVERC_GRPC_UNARY, &reserved, 1U,
        &request, 1U, 1024U);
    CHECK(result != NULL);
    CHECK(result && result->error &&
          strcmp(result->error, "invalid gRPC metadata") == 0);
    neverc_grpc_result_free(result);

    result = neverc_grpc_client_call(
        (neverc_h2_client_t *)(void *)&client_storage, NULL,
        "/evil\r\n:status", NEVERC_GRPC_UNARY, NULL, 0U,
        &request, 1U, 1024U);
    CHECK(result != NULL);
    CHECK(result && result->error &&
          strcmp(result->error, "invalid gRPC call") == 0);
    neverc_grpc_result_free(result);

    result = neverc_grpc_client_call(
        (neverc_h2_client_t *)(void *)&client_storage, NULL,
        "/noservice", NEVERC_GRPC_UNARY, NULL, 0U,
        &request, 1U, 1024U);
    CHECK(result != NULL);
    CHECK(result && result->error &&
          strcmp(result->error, "invalid gRPC call") == 0);
    neverc_grpc_result_free(result);

    const char *error = NULL;
    CHECK(neverc_grpc_client_stream_open(
              (neverc_h2_client_t *)(void *)&client_storage, NULL,
              "/evil\r\nfoo", NEVERC_GRPC_UNARY, NULL, 0U, 1024U,
              &error) == NULL);
    CHECK(error && strcmp(error, "invalid gRPC stream") == 0);
}

static void grpc_test_unary(neverc_h2_client_t *client) {
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    static const uint8_t metadata_value[] = "value";
    neverc_grpc_metadata_t metadata = {
        "x-client", metadata_value, sizeof(metadata_value) - 1U};
    neverc_grpc_message_t request = {
        (const uint8_t *)"unary-message", 13U};
    neverc_grpc_result_t *result = neverc_grpc_client_call(
        client, context, "/test.Echo/Unary", NEVERC_GRPC_UNARY, &metadata,
        1U, &request, 1U, 1024U);
    CHECK(result != NULL);
    CHECK(result && result->error == NULL);
    CHECK(result && result->status == NEVERC_GRPC_OK);
    CHECK(result && result->message_count == 1U);
    CHECK(result && result->messages[0].length == request.length &&
          memcmp(result->messages[0].data, request.data, request.length) == 0);
    int header_seen = 0;
    int trailer_seen = 0;
    if (result) {
        for (size_t i = 0; i < result->header_count; i++)
            if (strcmp(result->headers[i].name, "x-neverc") == 0 &&
                strcmp(result->headers[i].value, "unary") == 0)
                header_seen = 1;
        for (size_t i = 0; i < result->trailer_count; i++)
            if (strcmp(result->trailers[i].name, "x-finished") == 0 &&
                strcmp(result->trailers[i].value, "yes") == 0)
                trailer_seen = 1;
    }
    CHECK(header_seen);
    CHECK(trailer_seen);
    neverc_grpc_result_free(result);
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(context);
    neverc_context_free(background);
}

static void grpc_test_bidi(neverc_h2_client_t *client) {
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    const char *error = NULL;
    neverc_grpc_client_stream_t *stream = neverc_grpc_client_stream_open(
        client, context, "/test.Echo/Bidi", NEVERC_GRPC_BIDI_STREAMING,
        NULL, 0U, 1024U, &error);
    CHECK(stream != NULL);
    if (stream) {
        CHECK(neverc_grpc_client_stream_send(stream, context, "one", 3U) == 0);
        CHECK(neverc_grpc_client_stream_send(stream, context, "two", 3U) == 0);
        CHECK(neverc_grpc_client_stream_close_send(stream, context) == 0);
        neverc_grpc_message_t message;
        CHECK(neverc_grpc_client_stream_receive(stream, context, &message) ==
              1);
        CHECK(message.length == 3U && memcmp(message.data, "one", 3U) == 0);
        CHECK(neverc_grpc_client_stream_receive(stream, context, &message) ==
              1);
        CHECK(message.length == 3U && memcmp(message.data, "two", 3U) == 0);
        CHECK(neverc_grpc_client_stream_receive(stream, context, &message) ==
              0);
        CHECK(neverc_grpc_client_stream_status(stream) == NEVERC_GRPC_OK);
        neverc_grpc_client_stream_free(stream);
    }
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(context);
    neverc_context_free(background);
}

static void grpc_test_bidi_trailers_without_halfclose(neverc_h2_client_t *client) {
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    const char *error = NULL;
    neverc_grpc_client_stream_t *stream = neverc_grpc_client_stream_open(
        client, context, "/test.Echo/BidiEndEarly", NEVERC_GRPC_BIDI_STREAMING,
        NULL, 0U, 1024U, &error);
    CHECK(stream != NULL);
    if (stream) {
        neverc_grpc_message_t message;
        CHECK(neverc_grpc_client_stream_send(stream, context, "x", 1U) == 0);
        CHECK(neverc_grpc_client_stream_receive(stream, context, &message) ==
              1);
        CHECK(message.length == 2U && memcmp(message.data, "hi", 2U) == 0);
        CHECK(neverc_grpc_client_stream_receive(stream, context, &message) ==
              0);
        CHECK(neverc_grpc_client_stream_status(stream) == NEVERC_GRPC_OK);
        neverc_grpc_client_stream_free(stream);
    }
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(context);
    neverc_context_free(background);
}

static void grpc_test_directional_streaming(neverc_h2_client_t *client) {
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    neverc_grpc_message_t seed = {(const uint8_t *)"seed", 4U};
    neverc_grpc_result_t *result = neverc_grpc_client_call(
        client, context, "/test.Echo/ServerStreaming",
        NEVERC_GRPC_SERVER_STREAMING, NULL, 0U, &seed, 1U, 1024U);
    CHECK(result != NULL && result->error == NULL &&
          result->status == NEVERC_GRPC_OK);
    CHECK(result && result->message_count == 3U);
    if (result && result->message_count == 3U) {
        CHECK(result->messages[0].length == 5U &&
              memcmp(result->messages[0].data, "first", 5U) == 0);
        CHECK(result->messages[2].length == 5U &&
              memcmp(result->messages[2].data, "third", 5U) == 0);
    }
    neverc_grpc_result_free(result);

    neverc_grpc_message_t requests[] = {
        {(const uint8_t *)"one", 3U},
        {(const uint8_t *)"two", 3U},
        {(const uint8_t *)"three", 5U}};
    result = neverc_grpc_client_call(
        client, context, "/test.Echo/ClientStreaming",
        NEVERC_GRPC_CLIENT_STREAMING, NULL, 0U, requests,
        sizeof(requests) / sizeof(requests[0]), 1024U);
    CHECK(result != NULL && result->error == NULL &&
          result->status == NEVERC_GRPC_OK);
    CHECK(result && result->message_count == 1U &&
          result->messages[0].length == 1U &&
          result->messages[0].data[0] == '3');
    neverc_grpc_result_free(result);
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(context);
    neverc_context_free(background);
}

static const char *grpc_response_status(neverc_h2_response_t *response) {
    if (!response) return NULL;
    size_t i;
    for (i = 0; i < response->trailer_count; i++)
        if (response->trailers[i].name &&
            strcmp(response->trailers[i].name, "grpc-status") == 0)
            return response->trailers[i].value;
    for (i = 0; i < response->header_count; i++)
        if (response->headers[i].name &&
            strcmp(response->headers[i].name, "grpc-status") == 0)
            return response->headers[i].value;
    return NULL;
}

static void grpc_test_unary_extra_frames(neverc_h2_client_t *client) {
    uint8_t body[64];
    size_t first = 0;
    size_t second = 0;
    CHECK(neverc_grpc_frame_encode("a", 1U, 0, body, sizeof(body),
                                   &first) == 0);
    CHECK(neverc_grpc_frame_encode("b", 1U, 0, body + first,
                                   sizeof(body) - first, &second) == 0);
    neverc_hpack_header_t headers[] = {
        {.name = "content-type", .value = "application/grpc"},
        {.name = "te", .value = "trailers"}};
    neverc_h2_response_t *two = neverc_h2_client_do_context(
        client, NULL, "POST", "/test.Echo/UnaryEndEarly", headers, 2U,
        body, first + second);
    CHECK(two != NULL);
    CHECK(two && two->error == NULL);
    const char *status = grpc_response_status(two);
    CHECK(status && strcmp(status, "3") == 0);
    neverc_h2_response_free(two);

    neverc_h2_response_t *one = neverc_h2_client_do_context(
        client, NULL, "POST", "/test.Echo/UnaryEndEarly", headers, 2U,
        body, first);
    CHECK(one != NULL);
    CHECK(one && one->error == NULL);
    status = grpc_response_status(one);
    CHECK(status && strcmp(status, "0") == 0);
    neverc_h2_response_free(one);

    uint8_t incomplete[64];
    memcpy(incomplete, body, first);
    incomplete[first] = 0;
    incomplete[first + 1] = 0;
    incomplete[first + 2] = 0;
    neverc_h2_response_t *partial = neverc_h2_client_do_context(
        client, NULL, "POST", "/test.Echo/UnaryEndEarly", headers, 2U,
        incomplete, first + 3U);
    CHECK(partial != NULL);
    CHECK(partial && partial->error == NULL);
    status = grpc_response_status(partial);
    CHECK(status && strcmp(status, "3") == 0);
    neverc_h2_response_free(partial);

    /* Dispatcher drain (handler returns OK, does not call end()). */
    neverc_h2_response_t *drain = neverc_h2_client_do_context(
        client, NULL, "POST", "/test.Echo/UnaryReturnOk", headers, 2U,
        body, first + second);
    CHECK(drain != NULL);
    CHECK(drain && drain->error == NULL);
    status = grpc_response_status(drain);
    CHECK(status && strcmp(status, "3") == 0);
    neverc_h2_response_free(drain);
}

static void grpc_test_reserved_metadata(neverc_h2_client_t *client) {
    neverc_grpc_message_t request = {(const uint8_t *)"x", 1U};
    neverc_grpc_result_t *result = neverc_grpc_client_call(
        client, NULL, "/test.Echo/Reserved", NEVERC_GRPC_UNARY, NULL, 0U,
        &request, 1U, 1024U);
    CHECK(result != NULL);
    CHECK(result && result->error == NULL);
    CHECK(result && result->status == NEVERC_GRPC_INTERNAL);
    CHECK(result && (result->status_message == NULL ||
                     strcmp(result->status_message, "injected") != 0));
    int grpc_status_in_headers = 0;
    int content_type_ok = 0;
    int custom_header = 0;
    if (result) {
        for (size_t i = 0; i < result->header_count; i++) {
            if (strcmp(result->headers[i].name, "grpc-status") == 0)
                grpc_status_in_headers = 1;
            if (strcmp(result->headers[i].name, "content-type") == 0 &&
                result->headers[i].value &&
                strncmp(result->headers[i].value, "application/grpc", 16U) ==
                    0)
                content_type_ok = 1;
            if (strcmp(result->headers[i].name, "x-neverc") == 0 &&
                result->headers[i].value &&
                strcmp(result->headers[i].value, "reserved") == 0)
                custom_header = 1;
        }
    }
    CHECK(!grpc_status_in_headers);
    CHECK(content_type_ok);
    CHECK(custom_header);
    neverc_grpc_result_free(result);
}

static void grpc_test_bin_metadata(neverc_h2_client_t *client) {
    static const uint8_t raw[] = {0x01, 0x02};
    neverc_grpc_metadata_t metadata = {"x-bin", raw, sizeof(raw)};
    neverc_grpc_message_t request = {(const uint8_t *)"x", 1U};
    neverc_grpc_result_t *result = neverc_grpc_client_call(
        client, NULL, "/test.Echo/BinMeta", NEVERC_GRPC_UNARY, &metadata, 1U,
        &request, 1U, 1024U);
    CHECK(result != NULL);
    CHECK(result && result->error == NULL);
    CHECK(result && result->status == NEVERC_GRPC_OK);
    CHECK(result && result->message_count == 1U);
    CHECK(result && result->messages[0].length == 2U &&
          result->messages[0].data[0] == 0x01 &&
          result->messages[0].data[1] == 0x02);
    neverc_grpc_result_free(result);
}

static void grpc_test_max_request_message_size(neverc_h2_client_t *client) {
    uint8_t oversized[1025];
    memset(oversized, 'x', sizeof(oversized));
    neverc_grpc_message_t request = {oversized, sizeof(oversized)};
    neverc_grpc_result_t *result = neverc_grpc_client_call(
        client, NULL, "/test.Echo/Unary", NEVERC_GRPC_UNARY, NULL, 0U,
        &request, 1U, 4096U);
    CHECK(result != NULL);
    CHECK(result && result->error == NULL);
    CHECK(result && result->status == NEVERC_GRPC_INVALID_ARGUMENT);
    neverc_grpc_result_free(result);
}

static int grpc_test_register_methods(neverc_http_mux_t *mux) {
    return neverc_grpc_server_register(mux, &grpc_test_unary_method) == 0 &&
           neverc_grpc_server_register(mux, &grpc_test_bidi_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_server_streaming_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_client_streaming_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_unary_end_early_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_unary_return_ok_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_bidi_end_early_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_reserved_metadata_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_bin_metadata_method) == 0;
}

static void grpc_test_h2c_end_to_end(void) {
    int port = grpc_test_free_port();
    CHECK(port > 0);
    neverc_http_mux_t *mux = neverc_http_new_mux();
    CHECK(mux != NULL);
    CHECK(grpc_test_register_methods(mux));
    grpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    (void)snprintf(test.address, sizeof(test.address), "127.0.0.1:%d", port);
    test.server = neverc_h2_server_create(mux);
    test.result = -1;
    CHECK(test.server != NULL);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    CHECK(neverc_thread_executor_submit(executor, grpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    neverc_h2_client_t *client = grpc_test_dial(test.address);
    CHECK(client != NULL);
    if (client) {
        grpc_test_unary(client);
        grpc_test_directional_streaming(client);
        grpc_test_bidi(client);
        grpc_test_bidi_trailers_without_halfclose(client);
        grpc_test_unary_extra_frames(client);
        grpc_test_reserved_metadata(client);
        grpc_test_bin_metadata(client);
        grpc_test_max_request_message_size(client);
        neverc_h2_client_close(client);
        neverc_h2_client_free(client);
    }
    neverc_h2_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_h2_server_destroy(test.server);
    neverc_http_mux_free(mux);
}

static void grpc_test_tls_end_to_end(void) {
    neverc_network_test_files_t files;
    memset(&files, 0, sizeof(files));
    CHECK(neverc_network_test_write_certs("grpc-h2-tls", &files) == 0);
    int port = grpc_test_free_port();
    CHECK(port > 0);
    neverc_http_mux_t *mux = neverc_http_new_mux();
    CHECK(mux != NULL && grpc_test_register_methods(mux));
    grpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    (void)snprintf(test.address, sizeof(test.address), "127.0.0.1:%d", port);
    test.server = mux ? neverc_h2_server_create(mux) : NULL;
    test.files = &files;
    test.use_tls = 1;
    test.result = -1;
    CHECK(test.server != NULL);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (executor && test.server)
        CHECK(neverc_thread_executor_submit(executor, grpc_test_server_task,
                                             &test) == NEVERC_THREAD_OK);
    neverc_h2_client_t *client = executor
        ? grpc_test_dial_tls(test.address, &files) : NULL;
    CHECK(client != NULL);
    if (client) {
        grpc_test_unary(client);
        neverc_h2_client_free(client);
    }

    neverc_h2_client_config_t rejected = neverc_h2_client_config_default();
    rejected.timeout_ms = 1000;
    const char *error = NULL;
    client = neverc_h2_client_dial(test.address, "localhost", 1, &rejected,
                                   &error);
    CHECK(client == NULL && error != NULL);
    neverc_h2_client_free(client);
    rejected.client_cert_file = files.client_cert;
    CHECK(neverc_h2_client_dial(test.address, "localhost", 1, &rejected,
                                &error) == NULL);

    if (test.server) neverc_h2_server_shutdown(test.server);
    if (executor)
        CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_h2_server_destroy(test.server);
    neverc_http_mux_free(mux);
    neverc_network_test_remove_certs(&files);
}

static int grpc_tcp_write_all(neverc_tcp_conn_t *conn, const void *data,
                              size_t length) {
    const uint8_t *cursor = (const uint8_t *)data;
    size_t offset = 0;
    while (offset < length) {
        int count = neverc_tcp_write(conn, cursor + offset, length - offset);
        if (count <= 0) return -1;
        offset += (size_t)count;
    }
    return 0;
}

static int grpc_tcp_read_all(neverc_tcp_conn_t *conn, void *data,
                             size_t length) {
    uint8_t *cursor = (uint8_t *)data;
    size_t offset = 0;
    while (offset < length) {
        int count = neverc_tcp_read(conn, cursor + offset, length - offset);
        if (count <= 0) return -1;
        offset += (size_t)count;
    }
    return 0;
}

static int grpc_h2_write_frame(neverc_tcp_conn_t *conn, uint8_t type,
                               uint8_t flags, uint32_t stream_id,
                               const void *payload, uint32_t length) {
    neverc_h2_frame_header_t header = {
        .length = length, .type = type, .flags = flags,
        .stream_id = stream_id};
    uint8_t encoded[NC_H2_FRAME_HEADER_SIZE];
    if (neverc_h2_frame_header_write(&header, encoded) != 0) return -1;
    if (grpc_tcp_write_all(conn, encoded, sizeof(encoded)) != 0) return -1;
    return length == 0
        ? 0 : grpc_tcp_write_all(conn, payload, length);
}

static int grpc_h2_read_frame(neverc_tcp_conn_t *conn,
                              neverc_h2_frame_header_t *header,
                              uint8_t *payload, size_t capacity) {
    uint8_t encoded[NC_H2_FRAME_HEADER_SIZE];
    if (grpc_tcp_read_all(conn, encoded, sizeof(encoded)) != 0) return -1;
    if (neverc_h2_frame_header_read(encoded, sizeof(encoded), header) != 0)
        return -1;
    if (header->length > capacity) return -1;
    return header->length == 0
        ? 0 : grpc_tcp_read_all(conn, payload, header->length);
}

typedef struct {
    neverc_tcp_listener_t *listener;
    int kind;
    int result;
    char captured_bin[16];
} grpc_fake_h2_t;

static void grpc_fake_capture_bin(grpc_fake_h2_t *test, const uint8_t *payload,
                                  size_t length) {
    neverc_hpack_decoder_t *decoder;
    neverc_hpack_header_t headers[32];
    int nheaders = 0;
    if (!test || !payload)
        return;
    decoder = neverc_hpack_decoder_create(4096);
    if (!decoder ||
        neverc_hpack_decode(decoder, payload, length, headers, 32,
                            &nheaders) < 0) {
        neverc_hpack_decoder_destroy(decoder);
        return;
    }
    for (int i = 0; i < nheaders; i++) {
        if (headers[i].name && headers[i].value &&
            strcmp(headers[i].name, "x-bin") == 0) {
            size_t n = strlen(headers[i].value);
            if (n >= sizeof(test->captured_bin))
                n = sizeof(test->captured_bin) - 1;
            memcpy(test->captured_bin, headers[i].value, n);
            test->captured_bin[n] = '\0';
        }
        free(headers[i].name);
        free(headers[i].value);
    }
    neverc_hpack_decoder_destroy(decoder);
}

static void grpc_fake_h2_task(void *context) {
    grpc_fake_h2_t *test = (grpc_fake_h2_t *)context;
    const char *error = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_accept(test->listener, &error);
    if (!conn) {
        test->result = -1;
        return;
    }
    char preface[NC_H2_CLIENT_PREFACE_LEN];
    neverc_h2_frame_header_t header;
    uint8_t payload[4096];
    if (grpc_tcp_read_all(conn, preface, sizeof(preface)) != 0 ||
        memcmp(preface, NC_H2_CLIENT_PREFACE, sizeof(preface)) != 0 ||
        grpc_h2_read_frame(conn, &header, payload, sizeof(payload)) != 0 ||
        header.type != NC_H2_FRAME_SETTINGS ||
        grpc_h2_write_frame(conn, NC_H2_FRAME_SETTINGS, 0, 0, NULL, 0) != 0 ||
        grpc_h2_write_frame(conn, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK,
                            0, NULL, 0) != 0) {
        neverc_tcp_close(conn);
        test->result = -1;
        return;
    }
    for (;;) {
        if (grpc_h2_read_frame(conn, &header, payload, sizeof(payload)) != 0) {
            neverc_tcp_close(conn);
            test->result = -1;
            return;
        }
        if (header.type == NC_H2_FRAME_HEADERS && header.stream_id == 1U)
            break;
    }
    grpc_fake_capture_bin(test, payload, header.length);
    neverc_hpack_encoder_t *encoder = neverc_hpack_encoder_create(4096);
    neverc_hpack_header_t headers[4];
    int header_count = 0;
    if (test->kind == 0) {
        headers[header_count++] = (neverc_hpack_header_t){
            .name = ":status", .value = "200"};
        headers[header_count++] = (neverc_hpack_header_t){
            .name = "content-type", .value = "application/grpc"};
        headers[header_count++] = (neverc_hpack_header_t){
            .name = "grpc-status", .value = "5"};
        headers[header_count++] = (neverc_hpack_header_t){
            .name = "grpc-message", .value = "missing"};
    } else if (test->kind == 1) {
        /* Non-200 without grpc-status: HTTP mapping (503 → UNAVAILABLE). */
        headers[header_count++] = (neverc_hpack_header_t){
            .name = ":status", .value = "503"};
        headers[header_count++] = (neverc_hpack_header_t){
            .name = "content-type", .value = "text/plain"};
    } else if (test->kind == 2 || test->kind == 3) {
        headers[header_count++] = (neverc_hpack_header_t){
            .name = ":status", .value = "503"};
        headers[header_count++] = (neverc_hpack_header_t){
            .name = "content-type", .value = "application/grpc"};
    } else if (test->kind == 4 || test->kind == 5 || test->kind == 6 ||
               test->kind == 7 || test->kind == 9) {
        /* kind 7 is Trailers-Only OK. kinds 4–6 put grpc-status on
         * Response-Headers, which is valid only when there is no DATA and
         * no later trailer block. */
        headers[header_count++] = (neverc_hpack_header_t){
            .name = ":status", .value = "200"};
        headers[header_count++] = (neverc_hpack_header_t){
            .name = "content-type", .value = "application/grpc"};
        headers[header_count++] = (neverc_hpack_header_t){
            .name = "grpc-status", .value = "0"};
    } else if (test->kind == 8) {
        /* Unary success: headers without grpc-status, one DATA message,
         * trailers with grpc-status 0. */
        headers[header_count++] = (neverc_hpack_header_t){
            .name = ":status", .value = "200"};
        headers[header_count++] = (neverc_hpack_header_t){
            .name = "content-type", .value = "application/grpc"};
    }
    uint8_t block[256];
    size_t block_length = 0;
    uint8_t trailer_block[256];
    size_t trailer_length = 0;
    int encoded = encoder && neverc_hpack_encode(
        encoder, headers, header_count, block, sizeof(block),
        &block_length) == 0 && block_length > 0 && block_length <= 0xffffffU;
    if (encoded && (test->kind == 3 || test->kind == 4 || test->kind == 5 ||
                    test->kind == 6 || test->kind == 8 || test->kind == 9)) {
        neverc_hpack_header_t trailers[1];
        if (test->kind == 6) {
            trailers[0] = (neverc_hpack_header_t){
                .name = "grpc-status", .value = "7"};
        } else if (test->kind == 8) {
            trailers[0] = (neverc_hpack_header_t){
                .name = "grpc-status", .value = "0"};
        } else {
            trailers[0] = (neverc_hpack_header_t){
                .name = "x-unused", .value = "1"};
        }
        if (test->kind == 9) {
            encoded = neverc_hpack_encode(
                encoder, trailers, 0, trailer_block, sizeof(trailer_block),
                &trailer_length) == 0 && trailer_length <= 0xffffffU;
        } else {
            encoded = neverc_hpack_encode(
                encoder, trailers, 1, trailer_block, sizeof(trailer_block),
                &trailer_length) == 0 && trailer_length > 0 &&
                trailer_length <= 0xffffffU;
        }
    }
    neverc_hpack_encoder_destroy(encoder);
    uint8_t header_flags = NC_H2_FLAG_END_HEADERS;
    if (test->kind == 0 || test->kind == 1 || test->kind == 7)
        header_flags = (uint8_t)(header_flags | NC_H2_FLAG_END_STREAM);
    if (!encoded ||
        grpc_h2_write_frame(
            conn, NC_H2_FRAME_HEADERS, header_flags,
            1U, block, (uint32_t)block_length) != 0) {
        neverc_tcp_close(conn);
        test->result = -1;
        return;
    }
    if (test->kind == 2) {
        uint8_t garbage[] = {0, 0, 0, 0, 50, 'x'};
        if (grpc_h2_write_frame(
                conn, NC_H2_FRAME_DATA, NC_H2_FLAG_END_STREAM, 1U,
                garbage, (uint32_t)sizeof(garbage)) != 0) {
            neverc_tcp_close(conn);
            test->result = -1;
            return;
        }
    } else if (test->kind == 3 || test->kind == 4 || test->kind == 5 ||
               test->kind == 6 || test->kind == 8 || test->kind == 9) {
        uint8_t grpc_ok[] = {0, 0, 0, 0, 2, 'o', 'k'};
        if ((test->kind == 4 || test->kind == 8) &&
            grpc_h2_write_frame(conn, NC_H2_FRAME_DATA, 0, 1U, grpc_ok,
                                (uint32_t)sizeof(grpc_ok)) != 0) {
            neverc_tcp_close(conn);
            test->result = -1;
            return;
        }
        if (grpc_h2_write_frame(
                conn, NC_H2_FRAME_HEADERS,
                (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM),
                1U, trailer_block, (uint32_t)trailer_length) != 0) {
            neverc_tcp_close(conn);
            test->result = -1;
            return;
        }
    }
    while (grpc_h2_read_frame(conn, &header, payload, sizeof(payload)) == 0) {
    }
    neverc_tcp_close(conn);
    test->result = 0;
}

static neverc_h2_client_t *grpc_start_fake_h2(
    grpc_fake_h2_t *fake, neverc_thread_executor_t **executor, int kind) {
    if (executor) *executor = NULL;
    if (fake) memset(fake, 0, sizeof(*fake));
    if (!fake || !executor) return NULL;
    int port = grpc_test_free_port();
    if (port <= 0) return NULL;
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);
    const char *error = NULL;
    fake->kind = kind;
    fake->result = -1;
    fake->listener = neverc_tcp_listen(address, &error);
    if (!fake->listener) return NULL;
    *executor = neverc_thread_executor_create(1U, 1U);
    if (!*executor ||
        neverc_thread_executor_submit(*executor, grpc_fake_h2_task,
                                      fake) != NEVERC_THREAD_OK) {
        neverc_thread_executor_free(*executor);
        *executor = NULL;
        neverc_tcp_listener_close(fake->listener);
        fake->listener = NULL;
        return NULL;
    }
    return grpc_test_dial(address);
}

static void grpc_stop_fake_h2(grpc_fake_h2_t *fake,
                              neverc_thread_executor_t *executor,
                              neverc_h2_client_t *client) {
    if (client) {
        neverc_h2_client_close(client);
        neverc_h2_client_free(client);
    }
    if (fake && fake->listener) neverc_tcp_listener_close(fake->listener);
    if (executor) {
        (void)neverc_thread_executor_shutdown(executor);
        neverc_thread_executor_free(executor);
    }
}

static neverc_grpc_result_t *grpc_call_fake_h2(int kind) {
    grpc_fake_h2_t fake;
    neverc_thread_executor_t *executor = NULL;
    neverc_h2_client_t *client = grpc_start_fake_h2(&fake, &executor, kind);
    neverc_grpc_message_t request = {(const uint8_t *)"x", 1U};
    neverc_grpc_result_t *result = client
        ? neverc_grpc_client_call(
              client, NULL, "/test.Echo/Unary", NEVERC_GRPC_UNARY,
              NULL, 0U, &request, 1U, 1024U)
        : NULL;
    grpc_stop_fake_h2(&fake, executor, client);
    return result;
}

static int grpc_stream_fake_h2(int kind, neverc_grpc_status_t *status,
                               const char **error_out) {
    if (status) *status = NEVERC_GRPC_UNKNOWN;
    if (error_out) *error_out = "fake h2 stream failed";
    grpc_fake_h2_t fake;
    neverc_thread_executor_t *executor = NULL;
    neverc_h2_client_t *client = grpc_start_fake_h2(&fake, &executor, kind);
    const char *error = NULL;
    neverc_grpc_client_stream_t *stream = client
        ? neverc_grpc_client_stream_open(
              client, NULL, "/test.Echo/Unary", NEVERC_GRPC_UNARY,
              NULL, 0U, 1024U, &error)
        : NULL;
    int receive = -1;
    if (stream &&
        neverc_grpc_client_stream_send(stream, NULL, "x", 1U) == 0 &&
        neverc_grpc_client_stream_close_send(stream, NULL) == 0) {
        neverc_grpc_message_t message;
        receive = neverc_grpc_client_stream_receive(stream, NULL, &message);
        if (status) *status = neverc_grpc_client_stream_status(stream);
        if (error_out) *error_out = neverc_grpc_client_stream_error(stream);
    }
    neverc_grpc_client_stream_free(stream);
    grpc_stop_fake_h2(&fake, executor, client);
    return receive;
}

static void grpc_test_status_mapping(void) {
    neverc_grpc_result_t *trailers_only = grpc_call_fake_h2(0);
    CHECK(trailers_only != NULL);
    CHECK(trailers_only && trailers_only->error == NULL);
    CHECK(trailers_only && trailers_only->status == NEVERC_GRPC_NOT_FOUND);
    CHECK(trailers_only && trailers_only->status_message &&
          strcmp(trailers_only->status_message, "missing") == 0);
    neverc_grpc_result_free(trailers_only);

    neverc_grpc_result_t *http_error = grpc_call_fake_h2(1);
    CHECK(http_error != NULL);
    CHECK(http_error && http_error->error == NULL);
    CHECK(http_error && http_error->status == NEVERC_GRPC_UNAVAILABLE);
    neverc_grpc_result_free(http_error);

    neverc_grpc_result_t *http_framing = grpc_call_fake_h2(2);
    CHECK(http_framing != NULL);
    CHECK(http_framing && http_framing->error == NULL);
    CHECK(http_framing && http_framing->status == NEVERC_GRPC_UNAVAILABLE);
    neverc_grpc_result_free(http_framing);

    neverc_grpc_result_t *http_trailers = grpc_call_fake_h2(3);
    CHECK(http_trailers != NULL);
    CHECK(http_trailers && http_trailers->error == NULL);
    CHECK(http_trailers && http_trailers->status == NEVERC_GRPC_UNAVAILABLE);
    neverc_grpc_result_free(http_trailers);

    neverc_grpc_result_t *status_in_headers = grpc_call_fake_h2(4);
    CHECK(status_in_headers != NULL);
    CHECK(status_in_headers && status_in_headers->error != NULL);
    CHECK(status_in_headers &&
          status_in_headers->status != NEVERC_GRPC_OK);
    CHECK(status_in_headers && status_in_headers->message_count == 0U);
    neverc_grpc_result_free(status_in_headers);

    neverc_grpc_result_t *status_then_trailers = grpc_call_fake_h2(5);
    CHECK(status_then_trailers != NULL);
    CHECK(status_then_trailers && status_then_trailers->error != NULL);
    CHECK(status_then_trailers &&
          status_then_trailers->status != NEVERC_GRPC_OK);
    neverc_grpc_result_free(status_then_trailers);

    neverc_grpc_result_t *conflicting_status = grpc_call_fake_h2(6);
    CHECK(conflicting_status != NULL);
    CHECK(conflicting_status && conflicting_status->error != NULL);
    CHECK(conflicting_status &&
          conflicting_status->status != NEVERC_GRPC_OK &&
          conflicting_status->status != NEVERC_GRPC_PERMISSION_DENIED);
    neverc_grpc_result_free(conflicting_status);

    neverc_grpc_result_t *empty_trailers = grpc_call_fake_h2(9);
    CHECK(empty_trailers != NULL);
    CHECK(empty_trailers && empty_trailers->error != NULL);
    CHECK(empty_trailers && empty_trailers->status != NEVERC_GRPC_OK);
    neverc_grpc_result_free(empty_trailers);

    neverc_grpc_status_t stream_status = NEVERC_GRPC_UNKNOWN;
    const char *stream_error = "unset";
    CHECK(grpc_stream_fake_h2(0, &stream_status, &stream_error) == 0);
    CHECK(stream_status == NEVERC_GRPC_NOT_FOUND);
    CHECK(stream_error == NULL);

    stream_status = NEVERC_GRPC_UNKNOWN;
    stream_error = "unset";
    CHECK(grpc_stream_fake_h2(1, &stream_status, &stream_error) == 0);
    CHECK(stream_status == NEVERC_GRPC_UNAVAILABLE);
    CHECK(stream_error == NULL);

    stream_status = NEVERC_GRPC_UNKNOWN;
    stream_error = "unset";
    CHECK(grpc_stream_fake_h2(2, &stream_status, &stream_error) == 0);
    CHECK(stream_status == NEVERC_GRPC_UNAVAILABLE);
    CHECK(stream_error == NULL);

    stream_status = NEVERC_GRPC_UNKNOWN;
    stream_error = "unset";
    CHECK(grpc_stream_fake_h2(3, &stream_status, &stream_error) == 0);
    CHECK(stream_status == NEVERC_GRPC_UNAVAILABLE);
    CHECK(stream_error == NULL);

    stream_status = NEVERC_GRPC_UNKNOWN;
    stream_error = "unset";
    CHECK(grpc_stream_fake_h2(4, &stream_status, &stream_error) == -1);
    CHECK(stream_error != NULL);
    CHECK(stream_status != NEVERC_GRPC_OK);

    stream_status = NEVERC_GRPC_UNKNOWN;
    stream_error = "unset";
    CHECK(grpc_stream_fake_h2(5, &stream_status, &stream_error) == -1);
    CHECK(stream_error != NULL);
    CHECK(stream_status != NEVERC_GRPC_OK);

    stream_status = NEVERC_GRPC_UNKNOWN;
    stream_error = "unset";
    CHECK(grpc_stream_fake_h2(6, &stream_status, &stream_error) == -1);
    CHECK(stream_error != NULL);
    CHECK(stream_status != NEVERC_GRPC_OK);
    CHECK(stream_status != NEVERC_GRPC_PERMISSION_DENIED);

    stream_status = NEVERC_GRPC_UNKNOWN;
    stream_error = "unset";
    CHECK(grpc_stream_fake_h2(9, &stream_status, &stream_error) == -1);
    CHECK(stream_error != NULL);
    CHECK(stream_status != NEVERC_GRPC_OK);
}

static void grpc_test_binary_metadata_unpadded(void) {
    static const uint8_t raw[] = {0x00};
    neverc_grpc_metadata_t metadata = {"x-bin", raw, sizeof(raw)};
    neverc_grpc_message_t request = {(const uint8_t *)"x", 1U};
    grpc_fake_h2_t fake;
    neverc_thread_executor_t *executor = NULL;
    neverc_h2_client_t *client = grpc_start_fake_h2(&fake, &executor, 8);
    neverc_grpc_result_t *result = client
        ? neverc_grpc_client_call(
              client, NULL, "/test.Echo/Unary", NEVERC_GRPC_UNARY,
              &metadata, 1U, &request, 1U, 1024U)
        : NULL;
    CHECK(result != NULL);
    CHECK(result && result->error == NULL);
    CHECK(result && result->status == NEVERC_GRPC_OK);
    CHECK(strcmp(fake.captured_bin, "AA") == 0);
    neverc_grpc_result_free(result);
    grpc_stop_fake_h2(&fake, executor, client);

    executor = NULL;
    client = grpc_start_fake_h2(&fake, &executor, 8);
    const char *error = NULL;
    neverc_grpc_client_stream_t *stream = client
        ? neverc_grpc_client_stream_open(
              client, NULL, "/test.Echo/Unary", NEVERC_GRPC_UNARY,
              &metadata, 1U, 1024U, &error)
        : NULL;
    if (stream &&
        neverc_grpc_client_stream_send(stream, NULL, "x", 1U) == 0 &&
        neverc_grpc_client_stream_close_send(stream, NULL) == 0) {
        neverc_grpc_message_t message;
        (void)neverc_grpc_client_stream_receive(stream, NULL, &message);
    }
    CHECK(stream != NULL);
    CHECK(strcmp(fake.captured_bin, "AA") == 0);
    neverc_grpc_client_stream_free(stream);
    grpc_stop_fake_h2(&fake, executor, client);
}

int main(void) {
    printf("gRPC test suite:\n");
    grpc_test_frame_and_timeout();
    grpc_test_metadata_limits();
    grpc_test_status_mapping();
    grpc_test_binary_metadata_unpadded();
    grpc_test_h2c_end_to_end();
    grpc_test_tls_end_to_end();
    printf("grpc: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
