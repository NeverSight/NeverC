#include "neverc/std/net/quic.h"
#include "neverc/std/net/udp.h"
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
    neverc_quic_endpoint_t *endpoint;
    int result;
} quic_test_server_t;

static int quic_test_stream_read_exact(neverc_quic_stream_t *stream,
                                       void *output, size_t length) {
    size_t position = 0;
    while (position < length) {
        int count = neverc_quic_stream_read(
            stream, (uint8_t *)output + position, length - position);
        if (count <= 0) return -1;
        position += (size_t)count;
    }
    return 0;
}

static int quic_test_stream_try_read_exact(neverc_quic_stream_t *stream,
                                           void *output, size_t length) {
    size_t position = 0;
    for (int attempt = 0; attempt < 2000 && position < length; attempt++) {
        int count = neverc_quic_stream_try_read(
            stream, (uint8_t *)output + position, length - position);
        if (count == -2) {
            neverc_time_sleep(1 * NEVERC_TIME_MILLISECOND);
            continue;
        }
        if (count <= 0) return -1;
        position += (size_t)count;
    }
    return position == length ? 0 : -1;
}

static neverc_quic_stream_t *quic_test_try_accept(
    neverc_quic_conn_t *connection) {
    neverc_quic_stream_t *stream = NULL;
    for (int attempt = 0; attempt < 2000; attempt++) {
        int result = neverc_quic_try_accept_stream(connection, &stream);
        if (result == 1) return stream;
        if (result < 0) return NULL;
        neverc_time_sleep(1 * NEVERC_TIME_MILLISECOND);
    }
    return NULL;
}

static void quic_test_server_task(void *context) {
    quic_test_server_t *test = (quic_test_server_t *)context;
    test->result = -1;
    const char *error = NULL;
    neverc_quic_conn_t *connection = neverc_quic_accept(test->endpoint,
                                                          &error);
    if (!connection) return;
    neverc_quic_stream_t *stream = neverc_quic_accept_stream(connection,
                                                               &error);
    uint8_t request[4096];
    if (!stream || quic_test_stream_read_exact(stream, request,
                                                sizeof(request)) != 0 ||
        request[0] != 0U || request[4095] != (uint8_t)(4095U % 251U) ||
        neverc_quic_stream_read(stream, request, sizeof(request)) != 0 ||
        neverc_quic_stream_write(stream, request, sizeof(request)) !=
            (int)sizeof(request) ||
        neverc_quic_stream_close_write(stream) != 0)
        goto done;
    char datagram[64];
    int datagram_length = neverc_quic_recv_datagram(connection, datagram,
                                                     sizeof(datagram));
    if (datagram_length != 8 || memcmp(datagram, "datagram", 8U) != 0 ||
        neverc_quic_send_datagram(connection, "ack", 3U) != 0)
        goto done;

    neverc_quic_stream_t *server_uni = neverc_quic_open_uni_stream(
        connection, &error);
    if (!server_uni || neverc_quic_stream_write(
            server_uni, "server-uni", 10U) != 10 ||
        neverc_quic_stream_close_write(server_uni) != 0)
        goto done;
    neverc_quic_stream_free(server_uni);

    neverc_quic_stream_t *reset_uni = neverc_quic_accept_stream(
        connection, &error);
    if (!reset_uni || (neverc_quic_stream_id(reset_uni) & 2U) == 0 ||
        neverc_quic_stream_read(reset_uni, request, sizeof(request)) != -1 ||
        neverc_quic_stream_write(reset_uni, "x", 1U) != -1 ||
        neverc_quic_stream_reset(reset_uni, 9U) != -1)
        goto done;
    neverc_quic_stream_free(reset_uni);

    neverc_quic_stream_t *stopped = neverc_quic_accept_stream(
        connection, &error);
    if (!stopped || (neverc_quic_stream_id(stopped) & 2U) != 0 ||
        neverc_quic_stream_stop_sending(stopped, 77U) != 0)
        goto done;
    for (;;) {
        int read = neverc_quic_stream_read(stopped, request,
                                            sizeof(request));
        if (read < 0) break;
        if (read == 0) goto done;
    }
    neverc_quic_stream_free(stopped);
    test->result = 0;

done:
    neverc_quic_stream_free(stream);
    neverc_quic_conn_close(connection,
                           test->result == 0 ? 42U : 1U,
                           test->result == 0 ? "peer shutdown" :
                                               "test failure");
    neverc_quic_conn_free(connection);
}

static int quic_test_free_udp_port(void) {
    const char *error = NULL;
    neverc_udp_conn_t *probe = neverc_udp_listen("127.0.0.1:0", &error);
    if (!probe) return -1;
    neverc_udp_addr_t local;
    int port = neverc_udp_local_addr(probe, &local) == 0
        ? (int)local.port : -1;
    neverc_udp_close(probe);
    return port;
}

static void quic_test_roundtrip(void) {
    neverc_network_test_files_t files;
    CHECK(neverc_network_test_write_certs("quic-e2e", &files) == 0);
    int port = quic_test_free_udp_port();
    CHECK(port > 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);
    const char *alpn[] = {"neverc-quic-test/1", NULL};
    neverc_quic_config_t server_config = neverc_quic_config_default();
    server_config.cert_file = files.server_cert;
    server_config.key_file = files.server_key;
    server_config.alpn = alpn;
    server_config.max_idle_timeout_ms = 10000U;
    server_config.max_udp_payload_size = 1200U;
    const char *error = NULL;
    neverc_quic_endpoint_t *endpoint = neverc_quic_listen(
        address, &server_config, &error);
    CHECK(endpoint != NULL);
    if (!endpoint) {
        neverc_network_test_remove_certs(&files);
        return;
    }
    quic_test_server_t test = {endpoint, -1};
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    CHECK(neverc_thread_executor_submit(executor, quic_test_server_task,
                                         &test) == NEVERC_THREAD_OK);

    neverc_quic_config_t client_config = neverc_quic_config_default();
    client_config.alpn = alpn;
    client_config.server_name = "localhost";
    client_config.root_cert_file = files.ca;
    client_config.max_idle_timeout_ms = 10000U;
    client_config.max_udp_payload_size = 1200U;
    neverc_quic_conn_t *client = neverc_quic_dial(address, &client_config,
                                                    &error);
    CHECK(client != NULL);
    if (client) {
        CHECK(strcmp(neverc_quic_conn_alpn(client),
                     "neverc-quic-test/1") == 0);
        neverc_quic_stream_t *peer_stream = NULL;
        CHECK(neverc_quic_try_accept_stream(client, &peer_stream) == 0 &&
              peer_stream == NULL);
        neverc_quic_stream_t *stream = neverc_quic_open_stream(client,
                                                                 &error);
        CHECK(stream != NULL);
        if (stream) {
            uint8_t request[4096];
            for (size_t i = 0; i < sizeof(request); i++)
                request[i] = (uint8_t)(i % 251U);
            CHECK(neverc_quic_stream_try_read(stream, request,
                                               sizeof(request)) == -2);
            CHECK(neverc_quic_stream_write(stream, request,
                                            sizeof(request)) ==
                  (int)sizeof(request));
            CHECK(neverc_quic_stream_close_write(stream) == 0);
            uint8_t response[4096];
            CHECK(quic_test_stream_read_exact(stream, response,
                                               sizeof(response)) == 0);
            CHECK(memcmp(response, request, sizeof(response)) == 0);
            CHECK(neverc_quic_stream_read(stream, response,
                                          sizeof(response)) == 0);
            neverc_quic_stream_free(stream);
        }
        CHECK(neverc_quic_send_datagram(client, "datagram", 8U) == 0);
        char response[16];
        int response_length = neverc_quic_recv_datagram(client, response,
                                                         sizeof(response));
        CHECK(response_length == 3 && memcmp(response, "ack", 3U) == 0);

        peer_stream = quic_test_try_accept(client);
        CHECK(peer_stream != NULL &&
              (neverc_quic_stream_id(peer_stream) & 2U) != 0);
        if (peer_stream) {
            char notice[10];
            CHECK(quic_test_stream_try_read_exact(peer_stream, notice,
                                                   sizeof(notice)) == 0);
            CHECK(memcmp(notice, "server-uni", sizeof(notice)) == 0);
            CHECK(neverc_quic_stream_write(peer_stream, "x", 1U) == -1);
            CHECK(neverc_quic_stream_stop_sending(peer_stream, 3U) == 0);
            neverc_quic_stream_free(peer_stream);
        }

        neverc_quic_stream_t *local_uni = neverc_quic_open_uni_stream(
            client, &error);
        CHECK(local_uni != NULL);
        if (local_uni) {
            char byte;
            CHECK(neverc_quic_stream_try_read(local_uni, &byte, 1U) == -1);
            CHECK(neverc_quic_stream_stop_sending(local_uni, 3U) == -1);
            CHECK(neverc_quic_stream_reset(local_uni, 9U) == 0);
            neverc_quic_stream_free(local_uni);
        }

        neverc_quic_stream_t *stopped = neverc_quic_open_stream(client,
                                                                  &error);
        CHECK(stopped != NULL);
        if (stopped)
            CHECK(neverc_quic_stream_write(stopped, "cancel-me", 9U) == 9);
        for (int attempt = 0; attempt < 4000 &&
             neverc_quic_conn_is_alive(client); attempt++)
            neverc_time_sleep(1 * NEVERC_TIME_MILLISECOND);
        CHECK(!neverc_quic_conn_is_alive(client));
        neverc_quic_close_info_t close_info;
        memset(&close_info, 0, sizeof(close_info));
        CHECK(neverc_quic_conn_close_info(client, &close_info) == 0);
        CHECK(close_info.error_code == 42U && close_info.is_app == 1 &&
              close_info.reason &&
              strcmp(close_info.reason, "peer shutdown") == 0);
        neverc_quic_stream_free(stopped);
        neverc_quic_conn_free(client);
    }
    neverc_quic_endpoint_close(endpoint);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_network_test_remove_certs(&files);
}

static void quic_test_rejects_unimplemented_options(void) {
    const char *error = NULL;
    neverc_quic_config_t config = neverc_quic_config_default();
    config.enable_0rtt = 1;
    CHECK(neverc_quic_dial("127.0.0.1:1", &config, &error) == NULL);
    CHECK(error != NULL && strstr(error, "configuration") != NULL);
    config.enable_0rtt = 0;
    config.congestion_algorithm = 1;
    error = NULL;
    CHECK(neverc_quic_dial("127.0.0.1:1", &config, &error) == NULL);
    CHECK(error != NULL && strstr(error, "configuration") != NULL);
    config.congestion_algorithm = 0;
    config.insecure_skip_verify = 2;
    error = NULL;
    CHECK(neverc_quic_dial("127.0.0.1:1", &config, &error) == NULL);
    CHECK(error != NULL && strstr(error, "configuration") != NULL);
    config.insecure_skip_verify = 0;
    config.disable_migration = 2;
    error = NULL;
    CHECK(neverc_quic_dial("127.0.0.1:1", &config, &error) == NULL);
    CHECK(error != NULL && strstr(error, "configuration") != NULL);
}

int main(void) {
    printf("QUIC end-to-end test suite:\n");
    quic_test_rejects_unimplemented_options();
    quic_test_roundtrip();
    printf("quic-e2e: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
