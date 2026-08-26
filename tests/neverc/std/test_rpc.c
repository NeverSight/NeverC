#include "neverc/std/context.h"
#include "neverc/std/net/rpc.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/thread.h"
#include "neverc/std/time.h"
#include "network_test_support.h"

#include <stdatomic.h>
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
    neverc_rpc_server_t *server;
    const neverc_network_test_files_t *files;
    const char *address;
    int mtls;
    int quic;
    int result;
    atomic_int peer_certificates_seen;
    atomic_int echo_handler_calls;
    atomic_int retry_handler_calls;
    atomic_int hook_state;
    atomic_int hook_errors;
    atomic_int last_recv_result;
    atomic_int first_short_recv_result;
    atomic_int second_short_recv_result;
} rpc_test_server_t;

static void rpc_test_echo_handler(neverc_rpc_server_stream_t *stream,
                                  void *context) {
    rpc_test_server_t *test = (rpc_test_server_t *)context;
    atomic_fetch_add_explicit(&test->echo_handler_calls, 1,
                              memory_order_relaxed);
    size_t certificate_length = 0;
    const uint8_t *certificate =
        neverc_rpc_server_stream_peer_certificate(stream,
                                                   &certificate_length);
    if (certificate && certificate_length > 0)
        atomic_fetch_add_explicit(&test->peer_certificates_seen, 1,
                                  memory_order_relaxed);
    char buffer[2048];
    for (;;) {
        size_t length = 0;
        int result = neverc_rpc_server_stream_recv(stream, buffer,
                                                    sizeof(buffer), &length);
        if (result == NEVERC_RPC_IO_END) break;
        if (result != NEVERC_RPC_IO_OK ||
            neverc_rpc_server_stream_send(stream, buffer, length) !=
                NEVERC_RPC_IO_OK) {
            (void)neverc_rpc_server_stream_end(
                stream, NEVERC_RPC_STATUS_INTERNAL, "echo failed");
            return;
        }
    }
    (void)neverc_rpc_server_stream_end(stream, NEVERC_RPC_STATUS_OK, "");
}

static void rpc_test_slow_handler(neverc_rpc_server_stream_t *stream,
                                  void *context) {
    (void)stream;
    (void)context;
    neverc_time_sleep(1000 * NEVERC_TIME_MILLISECOND);
}

static void rpc_test_short_buffer_handler(
    neverc_rpc_server_stream_t *stream, void *context) {
    rpc_test_server_t *test = (rpc_test_server_t *)context;
    while (atomic_load_explicit(&test->hook_state,
                                memory_order_acquire) == 0)
        neverc_time_sleep(1 * NEVERC_TIME_MILLISECOND);
    uint8_t byte = 0;
    size_t length = 0;
    int first = neverc_rpc_server_stream_recv(
        stream, &byte, sizeof(byte), &length);
    int second = neverc_rpc_server_stream_recv(
        stream, &byte, sizeof(byte), &length);
    atomic_store_explicit(&test->first_short_recv_result, first,
                          memory_order_relaxed);
    atomic_store_explicit(&test->second_short_recv_result, second,
                          memory_order_relaxed);
    atomic_store_explicit(&test->hook_state, 2, memory_order_release);
}

static void rpc_test_retry_handler(neverc_rpc_server_stream_t *stream,
                                   void *context) {
    rpc_test_server_t *test = (rpc_test_server_t *)context;
    char request[64];
    size_t request_length = 0;
    size_t end_length = 0;
    if (neverc_rpc_server_stream_recv(stream, request, sizeof(request),
                                      &request_length) != NEVERC_RPC_IO_OK ||
        neverc_rpc_server_stream_recv(stream, request, sizeof(request),
                                      &end_length) != NEVERC_RPC_IO_END) {
        (void)neverc_rpc_server_stream_end(
            stream, NEVERC_RPC_STATUS_INTERNAL, "retry receive failed");
        return;
    }
    int call = atomic_fetch_add_explicit(&test->retry_handler_calls, 1,
                                         memory_order_relaxed);
    if (call == 0) {
        (void)neverc_rpc_server_stream_end(
            stream, NEVERC_RPC_STATUS_UNAVAILABLE, "retry requested");
        return;
    }
    if (neverc_rpc_server_stream_send(stream, request, request_length) !=
        NEVERC_RPC_IO_OK) {
        (void)neverc_rpc_server_stream_end(
            stream, NEVERC_RPC_STATUS_INTERNAL, "retry echo failed");
        return;
    }
    (void)neverc_rpc_server_stream_end(stream, NEVERC_RPC_STATUS_OK, "");
}

static void rpc_test_cancel_handler(neverc_rpc_server_stream_t *stream,
                                    void *context) {
    rpc_test_server_t *test = (rpc_test_server_t *)context;
    char buffer[64];
    size_t length = 0;
    int first = neverc_rpc_server_stream_recv(stream, buffer, sizeof(buffer),
                                              &length);
    if (first != NEVERC_RPC_IO_OK) {
        atomic_store_explicit(&test->last_recv_result, first,
                              memory_order_relaxed);
        atomic_store_explicit(&test->hook_state, 2, memory_order_relaxed);
        (void)neverc_rpc_server_stream_end(
            stream, NEVERC_RPC_STATUS_CANCELLED, "missing request");
        return;
    }
    atomic_store_explicit(&test->hook_state, 1, memory_order_relaxed);
    int second = neverc_rpc_server_stream_recv(stream, buffer, sizeof(buffer),
                                               &length);
    atomic_store_explicit(&test->last_recv_result, second,
                          memory_order_relaxed);
    atomic_store_explicit(&test->hook_state, 2, memory_order_relaxed);
    /* A peer CANCEL must not look like a clean half-close (IO_END). */
    if (second == NEVERC_RPC_IO_END)
        (void)neverc_rpc_server_stream_end(stream, NEVERC_RPC_STATUS_OK,
                                           "cancel looked like eof");
    else
        (void)neverc_rpc_server_stream_end(
            stream, NEVERC_RPC_STATUS_CANCELLED, "peer cancelled");
}

static neverc_rpc_status_code_t rpc_test_authenticator(
    neverc_rpc_server_stream_t *stream, void *context) {
    (void)stream;
    rpc_test_server_t *test = (rpc_test_server_t *)context;
    int previous = atomic_exchange_explicit(&test->hook_state, 1,
                                            memory_order_relaxed);
    if (previous != 0)
        atomic_fetch_add_explicit(&test->hook_errors, 1,
                                  memory_order_relaxed);
    return NEVERC_RPC_STATUS_OK;
}

static neverc_rpc_status_code_t rpc_test_authorizer(
    neverc_rpc_server_stream_t *stream, void *context) {
    rpc_test_server_t *test = (rpc_test_server_t *)context;
    const char *method = neverc_rpc_server_stream_method(stream);
    int denied = method && strcmp(method, "test.Denied/Unary") == 0;
    int previous = atomic_exchange_explicit(&test->hook_state,
                                            denied ? 0 : 2,
                                            memory_order_relaxed);
    if (previous != 1)
        atomic_fetch_add_explicit(&test->hook_errors, 1,
                                  memory_order_relaxed);
    return denied ? NEVERC_RPC_STATUS_PERMISSION_DENIED
                  : NEVERC_RPC_STATUS_OK;
}

static neverc_rpc_status_code_t rpc_test_interceptor(
    neverc_rpc_server_stream_t *stream, void *context) {
    (void)stream;
    rpc_test_server_t *test = (rpc_test_server_t *)context;
    int previous = atomic_exchange_explicit(&test->hook_state, 0,
                                            memory_order_relaxed);
    if (previous != 2)
        atomic_fetch_add_explicit(&test->hook_errors, 1,
                                  memory_order_relaxed);
    return NEVERC_RPC_STATUS_OK;
}

static size_t rpc_test_tenant_key(neverc_rpc_server_stream_t *stream,
                                  void *context, void *output,
                                  size_t output_capacity) {
    (void)context;
    size_t count = 0;
    const neverc_rpc_metadata_t *metadata =
        neverc_rpc_server_stream_metadata(stream, &count);
    for (size_t i = 0; i < count; i++) {
        if (metadata[i].key_length == 9U &&
            memcmp(metadata[i].key, "tenant-id", 9U) == 0 &&
            metadata[i].value_length > 0 &&
            metadata[i].value_length <= output_capacity) {
            memcpy(output, metadata[i].value, metadata[i].value_length);
            return metadata[i].value_length;
        }
    }
    return 0;
}

static neverc_rpc_status_code_t rpc_test_require_peer_certificate(
    neverc_rpc_server_stream_t *stream, void *context) {
    (void)context;
    size_t length = 0;
    return neverc_rpc_server_stream_peer_certificate(stream, &length) &&
                   length > 0
        ? NEVERC_RPC_STATUS_OK
        : NEVERC_RPC_STATUS_UNAUTHENTICATED;
}

static void rpc_test_server_task(void *context) {
    rpc_test_server_t *test = (rpc_test_server_t *)context;
    const char *address = test->address ? test->address : "127.0.0.1:0";
    if (test->quic) {
        test->result = neverc_rpc_server_listen_and_serve_quic(
            test->server, address, test->files->server_cert,
            test->files->server_key);
    } else if (test->mtls) {
        test->result = neverc_rpc_server_listen_and_serve_mtls(
            test->server, address, test->files->server_cert,
            test->files->server_key, test->files->ca);
    } else {
        test->result = neverc_rpc_server_listen_and_serve(
            test->server, address);
    }
}

static int rpc_test_wait_ready(neverc_rpc_server_t *server) {
    for (int attempt = 0; attempt < 500; attempt++) {
        if (neverc_rpc_server_is_running(server)) {
            int port = neverc_rpc_server_bound_port(server);
            if (port > 0) return port;
        }
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    }
    return -1;
}

/* Darwin can ECONNREFUSED the first connect() to a socket that has already
 * listen()'d if accept() has not yet run (and QUIC Initial can be dropped
 * in the same window). Retry until the transport is actually reachable. */
static neverc_rpc_client_t *rpc_test_dial(
    const char *address, const neverc_rpc_client_config_t *config,
    const char **errp) {
    neverc_rpc_client_config_t attempt_config = config
        ? *config : neverc_rpc_client_config_default();
    if (attempt_config.connect_timeout_ms > 500)
        attempt_config.connect_timeout_ms = 500;
    const char *error = NULL;
    neverc_rpc_client_t *client = NULL;
    for (int attempt = 0; attempt < 100; attempt++) {
        error = NULL;
        client = neverc_rpc_client_dial(address, &attempt_config, &error);
        if (client) break;
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    }
    if (errp) *errp = error;
    if (!client)
        printf("  dial %s failed: %s\n", address,
               error ? error : "(no error string)");
    return client;
}

static int rpc_test_stream_roundtrip(neverc_rpc_client_t *client) {
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    if (!context || !cancel) {
        neverc_context_free(context);
        neverc_context_free(background);
        return -1;
    }
    const uint8_t agent[] = "agent-1";
    neverc_rpc_metadata_t metadata = {
        "agent-id", 8U, agent, sizeof(agent) - 1U};
    const char *error = NULL;
    neverc_rpc_stream_t *stream = neverc_rpc_stream_open(
        client, context, "test.Echo/Bidi", &metadata, 1U, 1, &error);
    int result = -1;
    if (!stream) goto done;
    static const char first[] = "first";
    static const char second[] = "second";
    if (neverc_rpc_stream_send(stream, context, first,
                               sizeof(first) - 1U) != NEVERC_RPC_IO_OK ||
        neverc_rpc_stream_send(stream, context, second,
                               sizeof(second) - 1U) != NEVERC_RPC_IO_OK ||
        neverc_rpc_stream_close_send(stream, context) != NEVERC_RPC_IO_OK)
        goto stream_done;
    char response[64];
    size_t length = 0;
    if (neverc_rpc_stream_recv(stream, context, response, sizeof(response),
                               &length) != NEVERC_RPC_IO_OK ||
        length != sizeof(first) - 1U ||
        memcmp(response, first, length) != 0)
        goto stream_done;
    if (neverc_rpc_stream_recv(stream, context, response, sizeof(response),
                               &length) != NEVERC_RPC_IO_OK ||
        length != sizeof(second) - 1U ||
        memcmp(response, second, length) != 0)
        goto stream_done;
    if (neverc_rpc_stream_recv(stream, context, response, sizeof(response),
                               &length) != NEVERC_RPC_IO_END)
        goto stream_done;
    neverc_rpc_status_t stream_status = neverc_rpc_stream_status(stream);
    if (stream_status.code != NEVERC_RPC_STATUS_OK ||
        !stream_status.message || stream_status.message[0] != '\0')
        goto stream_done;
    result = 0;

stream_done:
    neverc_rpc_stream_free(stream);
done:
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(context);
    neverc_context_free(background);
    return result;
}

static int rpc_test_unary_call(
    neverc_rpc_client_t *client, const char *method,
    const neverc_rpc_metadata_t *metadata, size_t metadata_count,
    const neverc_rpc_call_options_t *options,
    neverc_rpc_status_code_t expected_status) {
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    if (!context || !cancel) {
        neverc_context_free(context);
        neverc_context_free(background);
        return -1;
    }
    static const char request[] = "unary";
    char response[64];
    size_t response_length = 0;
    neverc_rpc_status_t status;
    int result = options
        ? neverc_rpc_client_call_ex(
              client, context, method, metadata, metadata_count, request,
              sizeof(request) - 1U, response, sizeof(response),
              &response_length, &status, options)
        : neverc_rpc_client_call(
              client, context, method, metadata, metadata_count, request,
              sizeof(request) - 1U, response, sizeof(response),
              &response_length, &status);
    int valid = result == NEVERC_RPC_IO_OK &&
                status.code == expected_status &&
                status.message == NULL &&
                (expected_status != NEVERC_RPC_STATUS_OK ||
                 (response_length == sizeof(request) - 1U &&
                  memcmp(response, request, response_length) == 0));
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(context);
    neverc_context_free(background);
    return valid ? 0 : -1;
}

static int rpc_test_unary_roundtrip(neverc_rpc_client_t *client) {
    return rpc_test_unary_call(client, "test.Echo/Bidi", NULL, 0U, NULL,
                               NEVERC_RPC_STATUS_OK);
}

static void rpc_test_roundtrip(int mtls, int quic) {
    neverc_network_test_files_t files;
    memset(&files, 0, sizeof(files));
    if (mtls || quic)
        CHECK(neverc_network_test_write_certs("rpc-e2e", &files) == 0);
    neverc_rpc_server_config_t server_config =
        neverc_rpc_server_config_default();
    server_config.connection_workers = 4U;
    server_config.connection_queue_capacity = 8U;
    server_config.handler_workers = 4U;
    server_config.handler_queue_capacity = 16U;
    server_config.max_connections = 16U;
    rpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    test.files = &files;
    test.mtls = mtls;
    test.quic = quic;
    test.result = -1;
    test.server = neverc_rpc_server_new(&server_config);
    CHECK(test.server != NULL);
    if (!test.server) return;
    CHECK(neverc_rpc_server_register(test.server, "test.Echo/Bidi",
                                     rpc_test_echo_handler, &test) ==
          NEVERC_RPC_IO_OK);
    if (!mtls && !quic) {
        CHECK(neverc_rpc_server_register(test.server, "test.Retry/Unary",
                                         rpc_test_retry_handler, &test) ==
              NEVERC_RPC_IO_OK);
        CHECK(neverc_rpc_server_register(test.server, "test.Denied/Unary",
                                         rpc_test_echo_handler, &test) ==
              NEVERC_RPC_IO_OK);
        CHECK(neverc_rpc_server_set_authenticator(
                  test.server, rpc_test_authenticator, &test) ==
              NEVERC_RPC_IO_OK);
        CHECK(neverc_rpc_server_set_authorizer(
                  test.server, rpc_test_authorizer, &test) ==
              NEVERC_RPC_IO_OK);
        CHECK(neverc_rpc_server_add_interceptor(
                  test.server, rpc_test_interceptor, &test) ==
              NEVERC_RPC_IO_OK);
    }
    if (mtls)
        CHECK(neverc_rpc_server_set_authenticator(
                  test.server, rpc_test_require_peer_certificate, &test) ==
              NEVERC_RPC_IO_OK);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) return;
    CHECK(neverc_thread_executor_submit(executor, rpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    int port = rpc_test_wait_ready(test.server);
    CHECK(port > 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);

    if (mtls) {
        neverc_rpc_client_config_t rejected =
            neverc_rpc_client_config_default();
        rejected.use_tls = 1;
        rejected.server_name = "localhost";
        rejected.root_ca_file = files.ca;
        const char *error = NULL;
        neverc_rpc_client_t *client = neverc_rpc_client_dial(
            address, &rejected, &error);
        if (client) {
            CHECK(rpc_test_unary_roundtrip(client) != 0);
            neverc_rpc_client_close(client);
        } else {
            CHECK(error != NULL);
        }
    }
    if (quic) {
        neverc_rpc_client_config_t rejected =
            neverc_rpc_client_config_default();
        rejected.use_quic = 1;
        rejected.server_name = "localhost";
        const char *error = NULL;
        neverc_rpc_client_t *client = neverc_rpc_client_dial(
            address, &rejected, &error);
        CHECK(client == NULL);
        neverc_rpc_client_close(client);
    }

    neverc_rpc_client_config_t client_config =
        neverc_rpc_client_config_default();
    if (mtls) {
        client_config.use_tls = 1;
        client_config.server_name = "localhost";
        client_config.root_ca_file = files.ca;
        client_config.client_cert_file = files.client_cert;
        client_config.client_key_file = files.client_key;
    } else if (quic) {
        client_config.use_quic = 1;
        client_config.server_name = "localhost";
        client_config.root_ca_file = files.ca;
    } else {
        client_config.ping_interval_ms = 20;
        client_config.pong_timeout_ms = 250;
        client_config.reconnect_backoff_ms = 0;
    }
    const char *error = NULL;
    neverc_rpc_client_t *client = rpc_test_dial(
        address, &client_config, &error);
    CHECK(client != NULL);
    if (client) {
        CHECK(rpc_test_stream_roundtrip(client) == 0);
        CHECK(rpc_test_unary_roundtrip(client) == 0);
        if (!mtls && !quic) {
            neverc_time_sleep(150 * NEVERC_TIME_MILLISECOND);
            CHECK(rpc_test_unary_roundtrip(client) == 0);

            int handler_calls = atomic_load_explicit(
                &test.echo_handler_calls, memory_order_relaxed);
            CHECK(rpc_test_unary_call(
                      client, "test.Denied/Unary", NULL, 0U, NULL,
                      NEVERC_RPC_STATUS_PERMISSION_DENIED) == 0);
            CHECK(atomic_load_explicit(&test.echo_handler_calls,
                                       memory_order_relaxed) == handler_calls);

            neverc_rpc_call_options_t retry =
                neverc_rpc_call_options_default();
            retry.idempotent = 1;
            retry.max_attempts = 2U;
            CHECK(rpc_test_unary_call(
                      client, "test.Retry/Unary", NULL, 0U, &retry,
                      NEVERC_RPC_STATUS_OK) == 0);
            CHECK(atomic_load_explicit(&test.retry_handler_calls,
                                       memory_order_relaxed) == 2);
        }
        neverc_rpc_client_close(client);
    }
    if (mtls)
        CHECK(atomic_load_explicit(&test.peer_certificates_seen,
                                   memory_order_relaxed) >= 2);
    if (!mtls && !quic) {
        CHECK(atomic_load_explicit(&test.hook_state,
                                   memory_order_relaxed) == 0);
        CHECK(atomic_load_explicit(&test.hook_errors,
                                   memory_order_relaxed) == 0);
    }
    neverc_rpc_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_rpc_server_free(test.server);
    if (mtls || quic) neverc_network_test_remove_certs(&files);
}

static void rpc_test_frame_codec(void) {
    static const uint8_t payload[] = {1U, 2U, 3U, 4U};
    neverc_rpc_frame_t input;
    memset(&input, 0, sizeof(input));
    input.header.version = NEVERC_RPC_VERSION_1;
    input.header.type = NEVERC_RPC_FRAME_DATA;
    input.header.flags = NEVERC_RPC_FLAG_END_STREAM;
    input.header.payload_length = sizeof(payload);
    input.header.request_id = 17U;
    input.payload = payload;
    uint8_t encoded[64];
    size_t encoded_length = 0;
    CHECK(neverc_rpc_frame_encode(&input, encoded, sizeof(encoded),
                                  &encoded_length) == 0);
    neverc_rpc_frame_t output;
    size_t consumed = 0;
    CHECK(neverc_rpc_frame_decode(encoded, encoded_length, 1024U, &output,
                                  &consumed) == 1);
    CHECK(consumed == encoded_length);
    CHECK(output.header.request_id == 17U);
    CHECK(output.header.flags == NEVERC_RPC_FLAG_END_STREAM);
    CHECK(memcmp(output.payload, payload, sizeof(payload)) == 0);
    encoded[encoded_length] = 0xffU;
    CHECK(neverc_rpc_frame_decode(encoded, encoded_length + 1U, 1024U,
                                  &output, &consumed) == 1);
    CHECK(consumed == encoded_length);
    CHECK(neverc_rpc_frame_decode(encoded, encoded_length - 1U, 1024U,
                                  &output, &consumed) == 0);
    CHECK(neverc_rpc_frame_decode(encoded, encoded_length, 3U, &output,
                                  &consumed) == -1);
    encoded[8] = 0xffU;
    encoded[9] = 0xffU;
    encoded[10] = 0xffU;
    encoded[11] = 0xffU;
    /* UINT32_MAX payload cannot overflow size_t on 64-bit hosts; the
     * frame is incomplete, not a decoded success. On 32-bit the same
     * length is rejected as an overflow. */
    CHECK(neverc_rpc_frame_decode(encoded, encoded_length, SIZE_MAX, &output,
                                  &consumed) != 1);
    encoded[8] = 0;
    encoded[9] = 0;
    encoded[10] = 0;
    encoded[11] = (uint8_t)sizeof(payload);
    encoded[4] = NEVERC_RPC_VERSION_1 + 1U;
    CHECK(neverc_rpc_frame_decode(encoded, encoded_length, 1024U, &output,
                                  &consumed) == -1);
    encoded[4] = NEVERC_RPC_VERSION_1;
    encoded[5] = 0xffU;
    CHECK(neverc_rpc_frame_decode(encoded, encoded_length, 1024U, &output,
                                  &consumed) == -1);

    neverc_rpc_frame_t cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.header.version = NEVERC_RPC_VERSION_1;
    cancel.header.type = NEVERC_RPC_FRAME_CANCEL;
    cancel.header.request_id = 1U;
    CHECK(neverc_rpc_frame_encode(&cancel, encoded, sizeof(encoded),
                                  &encoded_length) == -1);
    cancel.header.code = NEVERC_RPC_STATUS_CANCELLED;
    CHECK(neverc_rpc_frame_encode(&cancel, encoded, sizeof(encoded),
                                  &encoded_length) == 0);

    neverc_rpc_frame_t end;
    memset(&end, 0, sizeof(end));
    end.header.version = NEVERC_RPC_VERSION_1;
    end.header.type = NEVERC_RPC_FRAME_END;
    end.header.flags = NEVERC_RPC_FLAG_RESPONSE;
    end.header.request_id = 1U;
    CHECK(neverc_rpc_frame_encode(&end, encoded, sizeof(encoded),
                                  &encoded_length) == -1);
    end.header.flags = NEVERC_RPC_FLAG_END_STREAM;
    CHECK(neverc_rpc_frame_encode(&end, encoded, sizeof(encoded),
                                  &encoded_length) == -1);
    end.header.flags = NEVERC_RPC_FLAG_END_STREAM | NEVERC_RPC_FLAG_RESPONSE;
    CHECK(neverc_rpc_frame_encode(&end, encoded, sizeof(encoded),
                                  &encoded_length) == 0);
    encoded[6] = 0;
    encoded[7] = NEVERC_RPC_FLAG_END_STREAM;
    CHECK(neverc_rpc_frame_decode(encoded, encoded_length, 1024U, &output,
                                  &consumed) == -1);
    encoded[7] = NEVERC_RPC_FLAG_END_STREAM | NEVERC_RPC_FLAG_RESPONSE;

    neverc_rpc_frame_t ping;
    memset(&ping, 0, sizeof(ping));
    ping.header.version = NEVERC_RPC_VERSION_1;
    ping.header.type = NEVERC_RPC_FRAME_PING;
    ping.header.request_id = 1U;
    CHECK(neverc_rpc_frame_encode(&ping, encoded, sizeof(encoded),
                                  &encoded_length) == -1);
    ping.header.request_id = 0;
    ping.header.payload_length = 126U;
    ping.payload = payload;
    CHECK(neverc_rpc_frame_encode(&ping, encoded, sizeof(encoded),
                                  &encoded_length) == -1);

    neverc_rpc_frame_t goaway;
    memset(&goaway, 0, sizeof(goaway));
    goaway.header.version = NEVERC_RPC_VERSION_1;
    goaway.header.type = NEVERC_RPC_FRAME_GOAWAY;
    goaway.header.code = NEVERC_RPC_STATUS_UNAVAILABLE;
    CHECK(neverc_rpc_frame_encode(&goaway, encoded, sizeof(encoded),
                                  &encoded_length) == 0);
    goaway.header.request_id = 1U;
    CHECK(neverc_rpc_frame_encode(&goaway, encoded, sizeof(encoded),
                                  &encoded_length) == -1);
}

static void rpc_test_open_codec(void) {
    static const uint8_t trace[] = "abc";
    neverc_rpc_metadata_t metadata = {
        "trace-id", 8U, trace, sizeof(trace) - 1U};
    neverc_rpc_open_t open;
    memset(&open, 0, sizeof(open));
    open.method = "game.Session/Join";
    open.method_length = strlen(open.method);
    open.deadline_ms = 1700000000000;
    open.codec = NEVERC_RPC_CODEC_JSON;
    open.metadata = &metadata;
    open.metadata_count = 1U;
    uint8_t encoded[128];
    size_t encoded_length = 0;
    CHECK(neverc_rpc_open_encode(&open, encoded, sizeof(encoded),
                                 &encoded_length) == 0);
    CHECK(encoded_length > NEVERC_RPC_OPEN_HEADER_SIZE);

    neverc_rpc_metadata_t decoded_metadata[4];
    neverc_rpc_open_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.metadata = decoded_metadata;
    decoded.metadata_capacity = 4U;
    CHECK(neverc_rpc_open_decode(encoded, encoded_length, 1024U, &decoded) ==
          0);
    CHECK(decoded.method_length == open.method_length &&
          memcmp(decoded.method, open.method, open.method_length) == 0);
    CHECK(decoded.deadline_ms == open.deadline_ms);
    CHECK(decoded.codec == NEVERC_RPC_CODEC_JSON);
    CHECK(decoded.metadata_count == 1U);
    CHECK(decoded.metadata[0].key_length == 8U &&
          memcmp(decoded.metadata[0].key, "trace-id", 8U) == 0);
    CHECK(decoded.metadata[0].value_length == 3U &&
          memcmp(decoded.metadata[0].value, "abc", 3U) == 0);
    CHECK(neverc_rpc_open_decode(encoded, encoded_length, 1U, &decoded) == -1);

    /* OPEN metadata value_length of UINT32_MAX must be rejected from a
     * short buffer; do not treat it as an incomplete frame. */
    uint8_t overflow_open[26];
    memset(overflow_open, 0, sizeof(overflow_open));
    overflow_open[9] = 3U;
    overflow_open[11] = 1U;
    overflow_open[16] = 'a';
    overflow_open[17] = '/';
    overflow_open[18] = 'b';
    overflow_open[20] = 1U;
    overflow_open[21] = 0xffU;
    overflow_open[22] = 0xffU;
    overflow_open[23] = 0xffU;
    overflow_open[24] = 0xffU;
    overflow_open[25] = 'k';
    memset(&decoded, 0, sizeof(decoded));
    decoded.metadata = decoded_metadata;
    decoded.metadata_capacity = 4U;
    CHECK(neverc_rpc_open_decode(overflow_open, sizeof(overflow_open),
                                 SIZE_MAX, &decoded) == -1);
    overflow_open[21] = 0;
    overflow_open[22] = 0;
    overflow_open[23] = 0;
    overflow_open[24] = 2U;
    CHECK(neverc_rpc_open_decode(overflow_open, sizeof(overflow_open),
                                 1024U, &decoded) == -1);

    open.metadata = NULL;
    open.metadata_count = 0;
    CHECK(neverc_rpc_open_encode(&open, encoded, sizeof(encoded),
                                 &encoded_length) == 0);
    memset(&decoded, 0, sizeof(decoded));
    CHECK(neverc_rpc_open_decode(encoded, encoded_length, 0U, &decoded) == 0);
    CHECK(decoded.metadata_count == 0U);
    CHECK(decoded.codec == NEVERC_RPC_CODEC_JSON);
    encoded[encoded_length] = 0xffU;
    CHECK(neverc_rpc_open_decode(encoded, encoded_length + 1U, 1024U,
                                 &decoded) == -1);

    encoded[13] = 1U;
    CHECK(neverc_rpc_open_decode(encoded, encoded_length, 1024U, &decoded) ==
          -1);
    encoded[13] = 0;
    encoded[12] = 3U;
    CHECK(neverc_rpc_open_decode(encoded, encoded_length, 1024U, &decoded) ==
          -1);
    encoded[12] = (uint8_t)NEVERC_RPC_CODEC_JSON;

    open.method = "/game.Session/Join";
    open.method_length = strlen(open.method);
    CHECK(neverc_rpc_open_encode(&open, encoded, sizeof(encoded),
                                 &encoded_length) == -1);
    open.method = "game.Session/Join/";
    open.method_length = strlen(open.method);
    CHECK(neverc_rpc_open_encode(&open, encoded, sizeof(encoded),
                                 &encoded_length) == -1);
    open.method = "game.Session/Join";
    open.method_length = strlen(open.method);
    open.deadline_ms = -1;
    CHECK(neverc_rpc_open_encode(&open, encoded, sizeof(encoded),
                                 &encoded_length) == -1);
    open.deadline_ms = 0;
    metadata.key = "Trace-Id";
    metadata.key_length = 8U;
    open.metadata = &metadata;
    open.metadata_count = 1U;
    CHECK(neverc_rpc_open_encode(&open, encoded, sizeof(encoded),
                                 &encoded_length) == -1);

    CHECK(neverc_rpc_status_code_valid(NEVERC_RPC_STATUS_OK));
    CHECK(!neverc_rpc_status_code_valid(17U));
    CHECK(strcmp(neverc_rpc_status_name(NEVERC_RPC_STATUS_UNAUTHENTICATED),
                 "UNAUTHENTICATED") == 0);
    CHECK(strcmp(neverc_rpc_status_name(99U), "INVALID_STATUS") == 0);
}

static void rpc_test_receive_backpressure(void) {
    neverc_rpc_server_config_t server_config =
        neverc_rpc_server_config_default();
    server_config.receive_queue_capacity = 1U;
    server_config.send_queue_capacity = 8U;
    server_config.connection_workers = 1U;
    server_config.connection_queue_capacity = 2U;
    server_config.handler_workers = 1U;
    server_config.handler_queue_capacity = 2U;
    server_config.max_connections = 2U;

    rpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    test.result = -1;
    test.server = neverc_rpc_server_new(&server_config);
    CHECK(test.server != NULL);
    if (!test.server) return;
    CHECK(neverc_rpc_server_register(test.server, "test.Backpressure/Slow",
                                     rpc_test_slow_handler, NULL) ==
          NEVERC_RPC_IO_OK);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) {
        neverc_rpc_server_free(test.server);
        return;
    }
    CHECK(neverc_thread_executor_submit(executor, rpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    int port = rpc_test_wait_ready(test.server);
    CHECK(port > 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);

    neverc_rpc_client_config_t client_config =
        neverc_rpc_client_config_default();
    client_config.send_queue_capacity = 64U;
    const char *error = NULL;
    neverc_rpc_client_t *client = rpc_test_dial(
        address, &client_config, &error);
    CHECK(client != NULL);
    if (client) {
        neverc_context_cancel_handle_t *cancel = NULL;
        neverc_context_t *background = neverc_context_background();
        neverc_context_t *context = background
            ? neverc_context_with_timeout_handle(background, 5000, &cancel)
            : NULL;
        CHECK(context != NULL && cancel != NULL);
        neverc_rpc_stream_t *stream = context
            ? neverc_rpc_stream_open(client, context,
                                     "test.Backpressure/Slow", NULL, 0U, 0,
                                     &error)
            : NULL;
        CHECK(stream != NULL);
        if (stream) {
            uint8_t payload[256];
            memset(payload, 0xa5, sizeof(payload));
            int sent = 0;
            for (int index = 0; index < 32; index++) {
                if (neverc_rpc_stream_send(stream, context, payload,
                                           sizeof(payload)) !=
                    NEVERC_RPC_IO_OK)
                    break;
                sent++;
            }
            CHECK(sent >= 2);
            char response[16];
            size_t response_length = 0;
            CHECK(neverc_rpc_stream_recv(stream, context, response,
                                         sizeof(response),
                                         &response_length) ==
                  NEVERC_RPC_IO_END);
            CHECK(neverc_rpc_stream_status(stream).code ==
                  NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED);
            neverc_rpc_stream_free(stream);
        }
        if (cancel) {
            neverc_context_cancel_handle_cancel(cancel);
            neverc_context_cancel_handle_free(cancel);
        }
        neverc_context_free(context);
        neverc_context_free(background);
        neverc_rpc_client_close(client);
    }
    neverc_rpc_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_rpc_server_free(test.server);
}

static void rpc_test_server_short_buffer_is_terminal(void) {
    neverc_rpc_server_config_t server_config =
        neverc_rpc_server_config_default();
    server_config.receive_queue_capacity = 8U;
    server_config.send_queue_capacity = 8U;
    server_config.connection_workers = 1U;
    server_config.connection_queue_capacity = 2U;
    server_config.handler_workers = 1U;
    server_config.handler_queue_capacity = 2U;
    server_config.max_connections = 2U;

    rpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    test.result = -1;
    test.server = neverc_rpc_server_new(&server_config);
    CHECK(test.server != NULL);
    if (!test.server) return;
    CHECK(neverc_rpc_server_register(
              test.server, "test.Buffer/Short",
              rpc_test_short_buffer_handler, &test) ==
          NEVERC_RPC_IO_OK);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) {
        neverc_rpc_server_free(test.server);
        return;
    }
    CHECK(neverc_thread_executor_submit(executor, rpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    int port = rpc_test_wait_ready(test.server);
    CHECK(port > 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);

    neverc_rpc_client_config_t client_config =
        neverc_rpc_client_config_default();
    client_config.send_queue_capacity = 8U;
    const char *error = NULL;
    neverc_rpc_client_t *client = rpc_test_dial(
        address, &client_config, &error);
    CHECK(client != NULL);
    if (client) {
        neverc_context_cancel_handle_t *cancel = NULL;
        neverc_context_t *background = neverc_context_background();
        neverc_context_t *context = background
            ? neverc_context_with_timeout_handle(background, 5000, &cancel)
            : NULL;
        CHECK(context != NULL && cancel != NULL);
        neverc_rpc_stream_t *stream = context
            ? neverc_rpc_stream_open(client, context,
                                     "test.Buffer/Short", NULL, 0U, 0,
                                     &error)
            : NULL;
        CHECK(stream != NULL);
        if (stream) {
            static const uint8_t oversized[] = {0xa5U, 0x5aU};
            static const uint8_t following[] = {0x11U};
            CHECK(neverc_rpc_stream_send(
                      stream, context, oversized, sizeof(oversized)) ==
                  NEVERC_RPC_IO_OK);
            CHECK(neverc_rpc_stream_send(
                      stream, context, following, sizeof(following)) ==
                  NEVERC_RPC_IO_OK);
            CHECK(neverc_rpc_stream_close_send(stream, context) ==
                  NEVERC_RPC_IO_OK);
            atomic_store_explicit(&test.hook_state, 1,
                                  memory_order_release);

            uint8_t response[8];
            size_t response_length = 0;
            CHECK(neverc_rpc_stream_recv(
                      stream, context, response, sizeof(response),
                      &response_length) == NEVERC_RPC_IO_END);
            CHECK(neverc_rpc_stream_status(stream).code ==
                  NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED);
            for (int attempt = 0; attempt < 500; attempt++) {
                if (atomic_load_explicit(&test.hook_state,
                                         memory_order_acquire) == 2)
                    break;
                neverc_time_sleep(1 * NEVERC_TIME_MILLISECOND);
            }
            CHECK(atomic_load_explicit(&test.hook_state,
                                       memory_order_acquire) == 2);
            CHECK(atomic_load_explicit(&test.first_short_recv_result,
                                       memory_order_relaxed) ==
                  NEVERC_RPC_IO_INVALID);
            CHECK(atomic_load_explicit(&test.second_short_recv_result,
                                       memory_order_relaxed) ==
                  NEVERC_RPC_IO_CANCELLED);
            neverc_rpc_stream_free(stream);
        }
        if (cancel) {
            neverc_context_cancel_handle_cancel(cancel);
            neverc_context_cancel_handle_free(cancel);
        }
        neverc_context_free(context);
        neverc_context_free(background);
        neverc_rpc_client_close(client);
    }
    neverc_rpc_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_rpc_server_free(test.server);
}

static void rpc_test_invalid_mtls_config(void) {
    neverc_rpc_client_config_t config = neverc_rpc_client_config_default();
    config.client_cert_file = "client.pem";
    const char *error = NULL;
    CHECK(neverc_rpc_client_dial("127.0.0.1:1", &config, &error) == NULL);
    CHECK(error != NULL && strstr(error, "invalid") != NULL);
}

static void rpc_test_invalid_unary_status(void) {
    neverc_rpc_status_t status = {
        NEVERC_RPC_STATUS_OK, "sentinel"};
    size_t response_length = SIZE_MAX;
    CHECK(neverc_rpc_client_call_ex(
              NULL, NULL, "test.Invalid/Call", NULL, 0U, NULL, 0U,
              NULL, 0U, &response_length, &status, NULL) ==
          NEVERC_RPC_IO_INVALID);
    CHECK(response_length == 0U);
    CHECK(status.code == NEVERC_RPC_STATUS_UNKNOWN);
    CHECK(status.message == NULL);
}

static void rpc_test_tenant_rate_limit(void) {
    neverc_rpc_server_config_t server_config =
        neverc_rpc_server_config_default();
    server_config.connection_workers = 2U;
    server_config.connection_queue_capacity = 4U;
    server_config.handler_workers = 2U;
    server_config.handler_queue_capacity = 4U;

    rpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    test.result = -1;
    test.server = neverc_rpc_server_new(&server_config);
    CHECK(test.server != NULL);
    if (!test.server) return;
    CHECK(neverc_rpc_server_register(test.server, "test.Echo/Bidi",
                                     rpc_test_echo_handler, &test) ==
          NEVERC_RPC_IO_OK);
    CHECK(neverc_rpc_server_set_tenant_rate_limit(
              test.server, 1U, 1U, 8U, rpc_test_tenant_key, NULL) ==
          NEVERC_RPC_IO_OK);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) {
        neverc_rpc_server_free(test.server);
        return;
    }
    CHECK(neverc_thread_executor_submit(executor, rpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    int port = rpc_test_wait_ready(test.server);
    CHECK(port > 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);

    const char *error = NULL;
    neverc_rpc_client_config_t client_config =
        neverc_rpc_client_config_default();
    neverc_rpc_client_t *client = port > 0
        ? rpc_test_dial(address, &client_config, &error) : NULL;
    CHECK(client != NULL);
    if (client) {
        static const uint8_t tenant_a[] = "tenant-a";
        static const uint8_t tenant_b[] = "tenant-b";
        neverc_rpc_metadata_t metadata_a = {
            "tenant-id", 9U, tenant_a, sizeof(tenant_a) - 1U};
        neverc_rpc_metadata_t metadata_b = {
            "tenant-id", 9U, tenant_b, sizeof(tenant_b) - 1U};
        CHECK(rpc_test_unary_call(
                  client, "test.Echo/Bidi", &metadata_a, 1U, NULL,
                  NEVERC_RPC_STATUS_OK) == 0);
        CHECK(rpc_test_unary_call(
                  client, "test.Echo/Bidi", &metadata_a, 1U, NULL,
                  NEVERC_RPC_STATUS_RESOURCE_EXHAUSTED) == 0);
        CHECK(rpc_test_unary_call(
                  client, "test.Echo/Bidi", &metadata_b, 1U, NULL,
                  NEVERC_RPC_STATUS_OK) == 0);
        neverc_rpc_client_close(client);
    }
    CHECK(atomic_load_explicit(&test.echo_handler_calls,
                               memory_order_relaxed) == 2);
    neverc_rpc_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_rpc_server_free(test.server);
}

static void rpc_test_reconnect_roundtrip(void) {
    neverc_rpc_server_config_t server_config =
        neverc_rpc_server_config_default();
    server_config.connection_workers = 2U;
    server_config.connection_queue_capacity = 4U;
    server_config.handler_workers = 2U;
    server_config.handler_queue_capacity = 4U;

    rpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    test.result = -1;
    test.server = neverc_rpc_server_new(&server_config);
    CHECK(test.server != NULL);
    if (!test.server) return;
    CHECK(neverc_rpc_server_register(test.server, "test.Echo/Bidi",
                                     rpc_test_echo_handler, &test) ==
          NEVERC_RPC_IO_OK);

    neverc_thread_executor_t *first_executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(first_executor != NULL);
    if (!first_executor) {
        neverc_rpc_server_free(test.server);
        return;
    }
    CHECK(neverc_thread_executor_submit(first_executor, rpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    int port = rpc_test_wait_ready(test.server);
    CHECK(port > 0);
    if (port <= 0) {
        neverc_rpc_server_shutdown(test.server);
        (void)neverc_thread_executor_shutdown(first_executor);
        neverc_thread_executor_free(first_executor);
        neverc_rpc_server_free(test.server);
        return;
    }
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);

    neverc_rpc_client_config_t client_config =
        neverc_rpc_client_config_default();
    client_config.connect_timeout_ms = 200;
    client_config.io_timeout_ms = 1000;
    client_config.reconnect_backoff_ms = 0;
    const char *error = NULL;
    neverc_rpc_client_t *client = rpc_test_dial(
        address, &client_config, &error);
    CHECK(client != NULL);
    if (client) CHECK(rpc_test_unary_roundtrip(client) == 0);

    neverc_rpc_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(first_executor) ==
          NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(first_executor);
    if (client) CHECK(rpc_test_unary_roundtrip(client) != 0);

    test.address = address;
    test.result = -1;
    neverc_thread_executor_t *second_executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(second_executor != NULL);
    if (!second_executor) {
        neverc_rpc_client_close(client);
        neverc_rpc_server_free(test.server);
        return;
    }
    CHECK(neverc_thread_executor_submit(second_executor, rpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    CHECK(rpc_test_wait_ready(test.server) == port);

    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    CHECK(context != NULL && cancel != NULL);
    if (client && context) {
        int reconnect_result = NEVERC_RPC_IO_CLOSED;
        for (int attempt = 0; attempt < 100; attempt++) {
            reconnect_result = neverc_rpc_client_reconnect(
                client, context, &error);
            if (reconnect_result == NEVERC_RPC_IO_OK) break;
            neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
        }
        CHECK(reconnect_result == NEVERC_RPC_IO_OK);
        CHECK(rpc_test_unary_roundtrip(client) == 0);
    }
    if (cancel) {
        neverc_context_cancel_handle_cancel(cancel);
        neverc_context_cancel_handle_free(cancel);
    }
    neverc_context_free(context);
    neverc_context_free(background);
    neverc_rpc_client_close(client);
    neverc_rpc_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(second_executor) ==
          NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(second_executor);
    neverc_rpc_server_free(test.server);
}

static int rpc_tcp_write_all(neverc_tcp_conn_t *conn, const void *data,
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

typedef struct {
    neverc_tcp_listener_t *listener;
    int result;
} rpc_fake_peer_t;

static void rpc_fake_peer_task(void *context) {
    rpc_fake_peer_t *test = (rpc_fake_peer_t *)context;
    const char *error = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_accept(test->listener, &error);
    if (!conn) {
        test->result = -1;
        return;
    }
    (void)neverc_tcp_set_timeout(conn, 2000);
    char peek[1];
    if (neverc_tcp_read(conn, peek, 1) <= 0) {
        neverc_tcp_close(conn);
        test->result = -1;
        return;
    }

    static const uint8_t payload[] = "hello";
    static const uint8_t extra[] = "no";
    neverc_rpc_frame_t frames[3];
    memset(frames, 0, sizeof(frames));
    frames[0].header.version = NEVERC_RPC_VERSION_1;
    frames[0].header.type = NEVERC_RPC_FRAME_DATA;
    frames[0].header.flags = NEVERC_RPC_FLAG_RESPONSE;
    frames[0].header.payload_length = (uint32_t)sizeof(payload) - 1U;
    frames[0].header.request_id = 1U;
    frames[0].payload = payload;
    frames[1].header.version = NEVERC_RPC_VERSION_1;
    frames[1].header.type = NEVERC_RPC_FRAME_END;
    frames[1].header.flags =
        NEVERC_RPC_FLAG_RESPONSE | NEVERC_RPC_FLAG_END_STREAM;
    frames[1].header.request_id = 1U;
    frames[1].header.code = NEVERC_RPC_STATUS_OK;
    frames[2].header.version = NEVERC_RPC_VERSION_1;
    frames[2].header.type = NEVERC_RPC_FRAME_DATA;
    frames[2].header.flags = NEVERC_RPC_FLAG_RESPONSE;
    frames[2].header.payload_length = (uint32_t)sizeof(extra) - 1U;
    frames[2].header.request_id = 1U;
    frames[2].payload = extra;

    uint8_t encoded[256];
    size_t encoded_length = 0;
    int ok = 1;
    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        size_t frame_length = 0;
        if (neverc_rpc_frame_encode(&frames[i], encoded + encoded_length,
                                    sizeof(encoded) - encoded_length,
                                    &frame_length) != 0) {
            ok = 0;
            break;
        }
        encoded_length += frame_length;
    }
    if (!ok || rpc_tcp_write_all(conn, encoded, encoded_length) != 0) {
        neverc_tcp_close(conn);
        test->result = -1;
        return;
    }
    char ignore[64];
    while (neverc_tcp_read(conn, ignore, sizeof(ignore)) > 0) {
    }
    neverc_tcp_close(conn);
    test->result = 0;
}

static void rpc_test_data_after_end(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    if (!listener) return;
    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%u",
                   (unsigned)local.port);

    rpc_fake_peer_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.listener = listener;
    fake.result = -1;
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) {
        neverc_tcp_listener_close(listener);
        return;
    }
    CHECK(neverc_thread_executor_submit(executor, rpc_fake_peer_task,
                                         &fake) == NEVERC_THREAD_OK);

    neverc_rpc_client_config_t config = neverc_rpc_client_config_default();
    config.ping_interval_ms = 0;
    config.pong_timeout_ms = 0;
    config.reconnect_enabled = 0;
    config.connect_timeout_ms = 2000;
    config.io_timeout_ms = 2000;
    neverc_rpc_client_t *client =
        rpc_test_dial(address, &config, &error);
    CHECK(client != NULL);
    if (client) {
        neverc_context_cancel_handle_t *cancel = NULL;
        neverc_context_t *background = neverc_context_background();
        neverc_context_t *context = background
            ? neverc_context_with_timeout_handle(background, 5000, &cancel)
            : NULL;
        CHECK(context != NULL && cancel != NULL);
        neverc_rpc_stream_t *stream = context
            ? neverc_rpc_stream_open(client, context, "test.Echo/Bidi",
                                     NULL, 0U, 0, &error)
            : NULL;
        CHECK(stream != NULL);
        if (stream) {
            static const char request[] = "unary";
            CHECK(neverc_rpc_stream_send(stream, context, request,
                                         sizeof(request) - 1U) ==
                  NEVERC_RPC_IO_OK);
            CHECK(neverc_rpc_stream_close_send(stream, context) ==
                  NEVERC_RPC_IO_OK);
            neverc_time_sleep(200 * NEVERC_TIME_MILLISECOND);
            char response[64];
            size_t length = 0;
            int result = NEVERC_RPC_IO_OK;
            while (result == NEVERC_RPC_IO_OK)
                result = neverc_rpc_stream_recv(
                    stream, context, response, sizeof(response), &length);
            neverc_rpc_status_t status = neverc_rpc_stream_status(stream);
            CHECK(result != NEVERC_RPC_IO_END ||
                  status.code != NEVERC_RPC_STATUS_OK);
            neverc_rpc_stream_free(stream);
        }
        if (cancel) {
            neverc_context_cancel_handle_cancel(cancel);
            neverc_context_cancel_handle_free(cancel);
        }
        neverc_context_free(context);
        neverc_context_free(background);
        neverc_rpc_client_close(client);
    }
    neverc_tcp_listener_close(listener);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(fake.result == 0);
    neverc_thread_executor_free(executor);
}

typedef struct {
    neverc_tcp_listener_t *listener;
    int result;
} rpc_goaway_peer_t;

static void rpc_goaway_ok_task(void *context) {
    rpc_goaway_peer_t *test = (rpc_goaway_peer_t *)context;
    const char *error = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_accept(test->listener, &error);
    if (!conn) {
        test->result = -1;
        return;
    }
    (void)neverc_tcp_set_timeout(conn, 2000);
    char peek[1];
    if (neverc_tcp_read(conn, peek, 1) <= 0) {
        neverc_tcp_close(conn);
        test->result = -1;
        return;
    }
    neverc_rpc_frame_t goaway;
    memset(&goaway, 0, sizeof(goaway));
    goaway.header.version = NEVERC_RPC_VERSION_1;
    goaway.header.type = NEVERC_RPC_FRAME_GOAWAY;
    goaway.header.code = NEVERC_RPC_STATUS_OK;
    uint8_t encoded[64];
    size_t encoded_length = 0;
    if (neverc_rpc_frame_encode(&goaway, encoded, sizeof(encoded),
                                &encoded_length) != 0 ||
        rpc_tcp_write_all(conn, encoded, encoded_length) != 0) {
        neverc_tcp_close(conn);
        test->result = -1;
        return;
    }
    char ignore[64];
    while (neverc_tcp_read(conn, ignore, sizeof(ignore)) > 0) {
    }
    neverc_tcp_close(conn);
    test->result = 0;
}

static void rpc_test_goaway_ok_not_success(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    if (!listener) return;
    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%u",
                   (unsigned)local.port);

    rpc_goaway_peer_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.listener = listener;
    fake.result = -1;
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) {
        neverc_tcp_listener_close(listener);
        return;
    }
    CHECK(neverc_thread_executor_submit(executor, rpc_goaway_ok_task,
                                         &fake) == NEVERC_THREAD_OK);

    neverc_rpc_client_config_t config = neverc_rpc_client_config_default();
    config.ping_interval_ms = 0;
    config.pong_timeout_ms = 0;
    config.reconnect_enabled = 0;
    config.connect_timeout_ms = 2000;
    config.io_timeout_ms = 2000;
    neverc_rpc_client_t *client =
        rpc_test_dial(address, &config, &error);
    CHECK(client != NULL);
    if (client) {
        neverc_context_cancel_handle_t *cancel = NULL;
        neverc_context_t *background = neverc_context_background();
        neverc_context_t *context = background
            ? neverc_context_with_timeout_handle(background, 5000, &cancel)
            : NULL;
        CHECK(context != NULL && cancel != NULL);
        neverc_rpc_stream_t *stream = context
            ? neverc_rpc_stream_open(client, context, "test.Echo/Bidi",
                                     NULL, 0U, 0, &error)
            : NULL;
        CHECK(stream != NULL);
        if (stream) {
            static const char request[] = "unary";
            (void)neverc_rpc_stream_send(stream, context, request,
                                         sizeof(request) - 1U);
            (void)neverc_rpc_stream_close_send(stream, context);
            neverc_time_sleep(200 * NEVERC_TIME_MILLISECOND);
            char response[64];
            size_t length = 0;
            int result = NEVERC_RPC_IO_OK;
            while (result == NEVERC_RPC_IO_OK)
                result = neverc_rpc_stream_recv(
                    stream, context, response, sizeof(response), &length);
            neverc_rpc_status_t status = neverc_rpc_stream_status(stream);
            CHECK(result != NEVERC_RPC_IO_OK);
            CHECK(result != NEVERC_RPC_IO_END);
            CHECK(status.code != NEVERC_RPC_STATUS_OK);
            neverc_rpc_stream_free(stream);
        }
        if (cancel) {
            neverc_context_cancel_handle_cancel(cancel);
            neverc_context_cancel_handle_free(cancel);
        }
        neverc_context_free(context);
        neverc_context_free(background);
        neverc_rpc_client_close(client);
    }
    neverc_tcp_listener_close(listener);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(fake.result == 0);
    neverc_thread_executor_free(executor);
}

static void rpc_goaway_unknown_task(void *context) {
    rpc_goaway_peer_t *test = (rpc_goaway_peer_t *)context;
    const char *error = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_accept(test->listener, &error);
    if (!conn) {
        test->result = -1;
        return;
    }
    (void)neverc_tcp_set_timeout(conn, 2000);
    char peek[1];
    if (neverc_tcp_read(conn, peek, 1) <= 0) {
        neverc_tcp_close(conn);
        test->result = -1;
        return;
    }
    neverc_rpc_frame_t goaway;
    memset(&goaway, 0, sizeof(goaway));
    goaway.header.version = NEVERC_RPC_VERSION_1;
    goaway.header.type = NEVERC_RPC_FRAME_GOAWAY;
    goaway.header.code = NEVERC_RPC_STATUS_UNKNOWN;
    uint8_t encoded[64];
    size_t encoded_length = 0;
    if (neverc_rpc_frame_encode(&goaway, encoded, sizeof(encoded),
                                &encoded_length) != 0 ||
        rpc_tcp_write_all(conn, encoded, encoded_length) != 0) {
        neverc_tcp_close(conn);
        test->result = -1;
        return;
    }
    char ignore[64];
    while (neverc_tcp_read(conn, ignore, sizeof(ignore)) > 0) {
    }
    neverc_tcp_close(conn);
    test->result = 0;
}

static void rpc_test_goaway_unknown_not_success(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    if (!listener) return;
    neverc_tcp_addr_t local;
    CHECK(neverc_tcp_listener_addr(listener, &local) == 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%u",
                   (unsigned)local.port);

    rpc_goaway_peer_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.listener = listener;
    fake.result = -1;
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) {
        neverc_tcp_listener_close(listener);
        return;
    }
    CHECK(neverc_thread_executor_submit(executor, rpc_goaway_unknown_task,
                                         &fake) == NEVERC_THREAD_OK);

    neverc_rpc_client_config_t config = neverc_rpc_client_config_default();
    config.ping_interval_ms = 0;
    config.pong_timeout_ms = 0;
    config.reconnect_enabled = 0;
    config.connect_timeout_ms = 2000;
    config.io_timeout_ms = 2000;
    neverc_rpc_client_t *client =
        rpc_test_dial(address, &config, &error);
    CHECK(client != NULL);
    if (client) {
        neverc_context_cancel_handle_t *cancel = NULL;
        neverc_context_t *background = neverc_context_background();
        neverc_context_t *context = background
            ? neverc_context_with_timeout_handle(background, 5000, &cancel)
            : NULL;
        CHECK(context != NULL && cancel != NULL);
        neverc_rpc_stream_t *stream = context
            ? neverc_rpc_stream_open(client, context, "test.Echo/Bidi",
                                     NULL, 0U, 0, &error)
            : NULL;
        CHECK(stream != NULL);
        if (stream) {
            static const char request[] = "unary";
            (void)neverc_rpc_stream_send(stream, context, request,
                                         sizeof(request) - 1U);
            (void)neverc_rpc_stream_close_send(stream, context);
            neverc_time_sleep(200 * NEVERC_TIME_MILLISECOND);
            char response[64];
            size_t length = 0;
            int result = NEVERC_RPC_IO_OK;
            while (result == NEVERC_RPC_IO_OK)
                result = neverc_rpc_stream_recv(
                    stream, context, response, sizeof(response), &length);
            neverc_rpc_status_t status = neverc_rpc_stream_status(stream);
            CHECK(result != NEVERC_RPC_IO_OK);
            CHECK(result != NEVERC_RPC_IO_END);
            CHECK(status.code != NEVERC_RPC_STATUS_OK);
            CHECK(status.code != NEVERC_RPC_STATUS_UNKNOWN);
            neverc_rpc_stream_free(stream);
        }
        if (cancel) {
            neverc_context_cancel_handle_cancel(cancel);
            neverc_context_cancel_handle_free(cancel);
        }
        neverc_context_free(context);
        neverc_context_free(background);
        neverc_rpc_client_close(client);
    }
    neverc_tcp_listener_close(listener);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(fake.result == 0);
    neverc_thread_executor_free(executor);
}

static void rpc_test_peer_cancel_is_not_eof(void) {
    neverc_rpc_server_config_t server_config =
        neverc_rpc_server_config_default();
    server_config.connection_workers = 2U;
    server_config.connection_queue_capacity = 4U;
    server_config.handler_workers = 2U;
    server_config.handler_queue_capacity = 4U;

    rpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    test.result = -1;
    atomic_store_explicit(&test.last_recv_result, 99, memory_order_relaxed);
    test.server = neverc_rpc_server_new(&server_config);
    CHECK(test.server != NULL);
    if (!test.server) return;
    CHECK(neverc_rpc_server_register(test.server, "test.Cancel/Wait",
                                     rpc_test_cancel_handler, &test) ==
          NEVERC_RPC_IO_OK);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) {
        neverc_rpc_server_free(test.server);
        return;
    }
    CHECK(neverc_thread_executor_submit(executor, rpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    int port = rpc_test_wait_ready(test.server);
    CHECK(port > 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);

    neverc_rpc_client_config_t client_config =
        neverc_rpc_client_config_default();
    client_config.ping_interval_ms = 0;
    client_config.pong_timeout_ms = 0;
    const char *error = NULL;
    neverc_rpc_client_t *client = port > 0
        ? rpc_test_dial(address, &client_config, &error) : NULL;
    CHECK(client != NULL);
    if (client) {
        neverc_context_cancel_handle_t *cancel = NULL;
        neverc_context_t *background = neverc_context_background();
        neverc_context_t *context = background
            ? neverc_context_with_timeout_handle(background, 5000, &cancel)
            : NULL;
        CHECK(context != NULL && cancel != NULL);
        neverc_rpc_stream_t *stream = context
            ? neverc_rpc_stream_open(client, context, "test.Cancel/Wait",
                                     NULL, 0U, 0, &error)
            : NULL;
        CHECK(stream != NULL);
        if (stream) {
            static const char request[] = "ping";
            CHECK(neverc_rpc_stream_send(stream, context, request,
                                         sizeof(request) - 1U) ==
                  NEVERC_RPC_IO_OK);
            int ready = 0;
            for (int attempt = 0; attempt < 200; attempt++) {
                if (atomic_load_explicit(&test.hook_state,
                                         memory_order_relaxed) >= 1) {
                    ready = 1;
                    break;
                }
                neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
            }
            CHECK(ready);
            CHECK(neverc_rpc_stream_cancel(stream,
                                           NEVERC_RPC_STATUS_CANCELLED,
                                           "client cancelled") ==
                  NEVERC_RPC_IO_OK);
            for (int attempt = 0; attempt < 200; attempt++) {
                if (atomic_load_explicit(&test.hook_state,
                                         memory_order_relaxed) >= 2)
                    break;
                neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
            }
            CHECK(atomic_load_explicit(&test.hook_state,
                                       memory_order_relaxed) >= 2);
            CHECK(atomic_load_explicit(&test.last_recv_result,
                                       memory_order_relaxed) ==
                  NEVERC_RPC_IO_CANCELLED);
            neverc_rpc_stream_free(stream);
        }
        if (cancel) {
            neverc_context_cancel_handle_cancel(cancel);
            neverc_context_cancel_handle_free(cancel);
        }
        neverc_context_free(context);
        neverc_context_free(background);
        neverc_rpc_client_close(client);
    }
    neverc_rpc_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_rpc_server_free(test.server);
}

static void rpc_test_call_deadline(void) {
    neverc_rpc_server_config_t server_config =
        neverc_rpc_server_config_default();
    server_config.connection_workers = 2U;
    server_config.connection_queue_capacity = 4U;
    server_config.handler_workers = 2U;
    server_config.handler_queue_capacity = 4U;

    rpc_test_server_t test;
    memset(&test, 0, sizeof(test));
    test.result = -1;
    test.server = neverc_rpc_server_new(&server_config);
    CHECK(test.server != NULL);
    if (!test.server) return;
    CHECK(neverc_rpc_server_register(test.server, "test.Slow/Unary",
                                     rpc_test_slow_handler, NULL) ==
          NEVERC_RPC_IO_OK);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor) {
        neverc_rpc_server_free(test.server);
        return;
    }
    CHECK(neverc_thread_executor_submit(executor, rpc_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    int port = rpc_test_wait_ready(test.server);
    CHECK(port > 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);

    neverc_rpc_client_config_t client_config =
        neverc_rpc_client_config_default();
    client_config.ping_interval_ms = 0;
    client_config.pong_timeout_ms = 0;
    const char *error = NULL;
    neverc_rpc_client_t *client = port > 0
        ? rpc_test_dial(address, &client_config, &error) : NULL;
    CHECK(client != NULL);
    if (client) {
        neverc_context_t *background = neverc_context_background();
        neverc_context_cancel_handle_t *send_cancel = NULL;
        neverc_context_t *send_deadline = background
            ? neverc_context_with_timeout_handle(background, 20, &send_cancel)
            : NULL;
        CHECK(send_deadline != NULL && send_cancel != NULL);
        static const char request[] = "unary";
        const char *open_error = NULL;
        neverc_rpc_stream_t *expired_stream = send_deadline
            ? neverc_rpc_stream_open(client, send_deadline, "test.Slow/Unary",
                                     NULL, 0U, 0, &open_error)
            : NULL;
        CHECK(expired_stream != NULL);
        neverc_time_sleep(40 * NEVERC_TIME_MILLISECOND);
        int send_result = expired_stream
            ? neverc_rpc_stream_send(expired_stream, send_deadline, request,
                                     sizeof(request) - 1U)
            : NEVERC_RPC_IO_CLOSED;
        CHECK(send_result == NEVERC_RPC_IO_CANCELLED);
        if (expired_stream)
            neverc_rpc_stream_free(expired_stream);
        neverc_context_cancel_handle_cancel(send_cancel);
        neverc_context_cancel_handle_free(send_cancel);
        neverc_context_free(send_deadline);

        neverc_context_cancel_handle_t *cancel = NULL;
        neverc_context_t *deadline = background
            ? neverc_context_with_timeout_handle(background, 1, &cancel)
            : NULL;
        CHECK(deadline != NULL && cancel != NULL);
        char response[64];
        size_t response_length = 0;
        neverc_rpc_status_t status;
        int result = deadline
            ? neverc_rpc_client_call(
                  client, deadline, "test.Slow/Unary", NULL, 0U, request,
                  sizeof(request) - 1U, response, sizeof(response),
                  &response_length, &status)
            : NEVERC_RPC_IO_CLOSED;
        /* The peer reconstructs the same absolute deadline and may return its
         * terminal status just before the caller context observes expiration. */
        CHECK(result == NEVERC_RPC_IO_CANCELLED ||
              result == NEVERC_RPC_IO_OK);
        CHECK(status.code == NEVERC_RPC_STATUS_DEADLINE_EXCEEDED);
        neverc_context_cancel_handle_cancel(cancel);
        neverc_context_cancel_handle_free(cancel);
        neverc_context_free(deadline);
        neverc_context_free(background);
        neverc_rpc_client_close(client);
    }
    neverc_rpc_server_shutdown(test.server);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    neverc_thread_executor_free(executor);
    neverc_rpc_server_free(test.server);
}

int main(void) {
    printf("NRPC test suite:\n");
    rpc_test_frame_codec();
    rpc_test_open_codec();
    rpc_test_data_after_end();
    rpc_test_goaway_ok_not_success();
    rpc_test_goaway_unknown_not_success();
    rpc_test_invalid_mtls_config();
    rpc_test_invalid_unary_status();
    rpc_test_peer_cancel_is_not_eof();
    rpc_test_call_deadline();
    rpc_test_receive_backpressure();
    rpc_test_server_short_buffer_is_terminal();
    rpc_test_tenant_rate_limit();
    rpc_test_reconnect_roundtrip();
    rpc_test_roundtrip(0, 0);
    rpc_test_roundtrip(1, 0);
    rpc_test_roundtrip(0, 1);
    printf("rpc: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
