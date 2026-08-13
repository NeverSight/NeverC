#include "neverc/std/context.h"
#include "neverc/std/net/grpc.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/thread.h"
#include "neverc/std/time.h"
#include "network_test_support.h"

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
    encoded[0] = 1U;
    neverc_grpc_frame_reader_init(&reader, encoded, encoded_length, 16U);
    CHECK(neverc_grpc_frame_reader_next(&reader, &message) == -1);

    char timeout[10];
    int64_t timeout_ms = 0;
    CHECK(neverc_grpc_timeout_encode(UINT64_C(1500000000), timeout) == 0);
    CHECK(neverc_grpc_timeout_decode(timeout, &timeout_ms) == 0);
    CHECK(timeout_ms >= 1500 && timeout_ms <= 1501);
    CHECK(neverc_grpc_timeout_decode("99999999999S", &timeout_ms) == -1);
    CHECK(strcmp(neverc_grpc_status_name(NEVERC_GRPC_UNAUTHENTICATED),
                 "UNAUTHENTICATED") == 0);

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
    CHECK(result && result->error != NULL);
    neverc_grpc_result_free(result);
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

static int grpc_test_register_methods(neverc_http_mux_t *mux) {
    return neverc_grpc_server_register(mux, &grpc_test_unary_method) == 0 &&
           neverc_grpc_server_register(mux, &grpc_test_bidi_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_server_streaming_method) == 0 &&
           neverc_grpc_server_register(
               mux, &grpc_test_client_streaming_method) == 0;
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

int main(void) {
    printf("gRPC test suite:\n");
    grpc_test_frame_and_timeout();
    grpc_test_metadata_limits();
    grpc_test_h2c_end_to_end();
    grpc_test_tls_end_to_end();
    printf("grpc: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
