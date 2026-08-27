#include "neverc/std/net/quic.h"
#include "neverc/std/net/udp.h"
#include "neverc/std/thread.h"
#include "neverc/std/time.h"
#include "neverc/std/encoding/pem.h"
#include "network_test_support.h"
#include "../../../std/src/net/quic/_quic_internal.h"

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

static uint32_t quic_test_get_u24(const uint8_t *input) {
    return ((uint32_t)input[0] << 16) | ((uint32_t)input[1] << 8) |
           input[2];
}

static int quic_test_decode_certificate(const char *pem, uint8_t *der,
                                        size_t der_cap, size_t *der_len) {
    char label[32];
    size_t rest_offset = 0;
    return neverc_pem_decode(pem, strlen(pem), label, sizeof(label),
                            der, der_cap, der_len, &rest_offset) == 0 &&
           strcmp(label, "CERTIFICATE") == 0 && *der_len != 0 &&
           rest_offset != 0 ? 0 : -1;
}

static int quic_test_certificate_flight_has_chain(
    const uint8_t *flight, size_t flight_len,
    const uint8_t *leaf_der, size_t leaf_len,
    const uint8_t *ca_der, size_t ca_len) {
    size_t message_offset = 0;
    while (message_offset <= flight_len &&
           flight_len - message_offset >= 4U) {
        const uint8_t *message = flight + message_offset;
        size_t body_len = quic_test_get_u24(message + 1U);
        if (body_len > flight_len - message_offset - 4U)
            return -1;
        if (message[0] == 11U) {
            const uint8_t *body = message + 4U;
            if (body_len < 4U || body[0] != 0U ||
                quic_test_get_u24(body + 1U) != body_len - 4U)
                return -1;
            const uint8_t *expected_der[] = {leaf_der, ca_der};
            const size_t expected_len[] = {leaf_len, ca_len};
            size_t position = 4U;
            size_t entry = 0;
            while (position < body_len) {
                if (body_len - position < 5U || entry >= 2U)
                    return -1;
                size_t der_len = quic_test_get_u24(body + position);
                position += 3U;
                if (der_len == 0 || der_len > body_len - position - 2U ||
                    der_len != expected_len[entry] ||
                    memcmp(body + position, expected_der[entry], der_len) != 0)
                    return -1;
                position += der_len;
                size_t extensions_len =
                    ((size_t)body[position] << 8) | body[position + 1U];
                position += 2U;
                if (extensions_len != 0 ||
                    extensions_len > body_len - position)
                    return -1;
                position += extensions_len;
                entry++;
            }
            return position == body_len && entry == 2U ? 0 : -1;
        }
        message_offset += 4U + body_len;
    }
    return -1;
}

static void quic_test_preserves_clienthello_parser_error(void) {
    neverc_network_test_files_t files = {0};
    int files_written = neverc_network_test_write_certs("quic-tls-error",
                                                         &files);
    CHECK(files_written == 0);
    if (files_written != 0) {
        neverc_network_test_remove_certs(&files);
        return;
    }

    quic_transport_params_t client_local;
    quic_transport_params_t client_peer;
    quic_transport_params_t server_local;
    quic_transport_params_t server_peer;
    neverc_quic_transport_params_default(&client_local);
    neverc_quic_transport_params_default(&client_peer);
    neverc_quic_transport_params_default(&server_local);
    neverc_quic_transport_params_default(&server_peer);
    neverc_quic_config_t client_config = neverc_quic_config_default();
    client_config.insecure_skip_verify = 1;
    client_config.server_name = "localhost";
    neverc_quic_config_t server_config = neverc_quic_config_default();
    server_config.cert_file = files.server_cert;
    server_config.key_file = files.server_key;
    quic_tls_t *client = neverc_quic_tls_create(0);
    quic_tls_t *server = neverc_quic_tls_create(1);
    CHECK(client != NULL && server != NULL);
    if (client && server) {
        int client_configured = neverc_quic_tls_configure(
            client, &client_config, "localhost", &client_local,
            &client_peer);
        int server_configured = neverc_quic_tls_configure(
            server, &server_config, NULL, &server_local, &server_peer);
        CHECK(client_configured == 0 && server_configured == 0);
        if (client_configured == 0 && server_configured == 0) {
            CHECK(neverc_quic_tls_start(client) == 0);
            uint64_t offset = 0;
            const uint8_t *data = NULL;
            size_t len = 0;
            CHECK(neverc_quic_tls_get_crypto_data(
                      client, QUIC_ENC_INITIAL, &offset, &data, &len) == 0);
            CHECK(offset == 0 && data != NULL && len != 0);
            CHECK(neverc_quic_tls_receive_crypto(
                      server, QUIC_ENC_INITIAL, offset, data, len) == 0);
            CHECK(neverc_quic_tls_process(server) == -1);
            const char *tls_error = neverc_quic_tls_error(server);
            CHECK(tls_error &&
                  strcmp(tls_error,
                         "invalid client QUIC transport parameters") == 0);
        }
    }
    neverc_quic_tls_destroy(client);
    neverc_quic_tls_destroy(server);
    neverc_network_test_remove_certs(&files);
}

static void quic_test_server_flight_preserves_certificate_chain(void) {
    neverc_network_test_files_t files = {0};
    int files_written = neverc_network_test_write_certs("quic-chain", &files);
    CHECK(files_written == 0);
    if (files_written != 0) {
        neverc_network_test_remove_certs(&files);
        return;
    }
    CHECK(neverc_network_test_write_file(
              files.server_cert,
              NEVERC_TEST_SERVER_CERT_PEM NEVERC_TEST_CA_CERT_PEM) == 0);

    uint8_t leaf_der[4096];
    uint8_t ca_der[4096];
    size_t leaf_len = 0;
    size_t ca_len = 0;
    CHECK(quic_test_decode_certificate(NEVERC_TEST_SERVER_CERT_PEM,
                                       leaf_der, sizeof(leaf_der),
                                       &leaf_len) == 0);
    CHECK(quic_test_decode_certificate(NEVERC_TEST_CA_CERT_PEM,
                                       ca_der, sizeof(ca_der), &ca_len) == 0);

    quic_transport_params_t client_local;
    quic_transport_params_t client_peer;
    quic_transport_params_t server_local;
    quic_transport_params_t server_peer;
    neverc_quic_transport_params_default(&client_local);
    neverc_quic_transport_params_default(&client_peer);
    neverc_quic_transport_params_default(&server_local);
    neverc_quic_transport_params_default(&server_peer);
    client_local.initial_scid[0] = 0xc1U;
    client_local.initial_scid_len = 1U;
    client_local.has_initial_scid = 1;
    server_local.original_dcid[0] = 0xd1U;
    server_local.original_dcid_len = 1U;
    server_local.has_original_dcid = 1;
    server_local.initial_scid[0] = 0x51U;
    server_local.initial_scid_len = 1U;
    server_local.has_initial_scid = 1;
    neverc_quic_config_t client_config = neverc_quic_config_default();
    client_config.insecure_skip_verify = 1;
    client_config.server_name = "localhost";
    neverc_quic_config_t server_config = neverc_quic_config_default();
    server_config.cert_file = files.server_cert;
    server_config.key_file = files.server_key;
    quic_tls_t *client = neverc_quic_tls_create(0);
    quic_tls_t *server = neverc_quic_tls_create(1);
    CHECK(client != NULL && server != NULL);
    if (client && server) {
        CHECK(neverc_quic_tls_configure(client, &client_config, "localhost",
                                        &client_local, &client_peer) == 0);
        CHECK(neverc_quic_tls_configure(server, &server_config, NULL,
                                        &server_local, &server_peer) == 0);
        CHECK(neverc_quic_tls_start(client) == 0);
        uint64_t offset = 0;
        const uint8_t *data = NULL;
        size_t len = 0;
        CHECK(neverc_quic_tls_get_crypto_data(client, QUIC_ENC_INITIAL,
                                              &offset, &data, &len) == 0);
        CHECK(offset == 0 && data != NULL && len != 0);
        CHECK(neverc_quic_tls_receive_crypto(server, QUIC_ENC_INITIAL,
                                             offset, data, len) == 0);
        int process_result = neverc_quic_tls_process(server);
        if (process_result != 0)
            fprintf(stderr, "server flight error: %s\n",
                    neverc_quic_tls_error(server));
        CHECK(process_result == 0);
        offset = 0;
        data = NULL;
        len = 0;
        CHECK(neverc_quic_tls_get_crypto_data(server, QUIC_ENC_HANDSHAKE,
                                              &offset, &data, &len) == 0);
        CHECK(offset == 0 && data != NULL && len != 0);
        CHECK(quic_test_certificate_flight_has_chain(
                  data, len, leaf_der, leaf_len, ca_der, ca_len) == 0);
    }
    neverc_quic_tls_destroy(client);
    neverc_quic_tls_destroy(server);
    neverc_network_test_remove_certs(&files);
}

typedef struct {
    neverc_quic_endpoint_t *endpoint;
    neverc_quic_conn_t *accepted;
    neverc_thread_channel_t *ready;
    neverc_thread_channel_t *release;
} quic_amp_server_t;

static void quic_amp_server_task(void *context) {
    quic_amp_server_t *test = (quic_amp_server_t *)context;
    const char *error = NULL;
    test->accepted = neverc_quic_accept(test->endpoint, &error);
    (void)neverc_thread_channel_send(test->ready, test);
    void *ignored = NULL;
    (void)neverc_thread_channel_receive(test->release, &ignored);
    if (test->accepted) {
        neverc_quic_conn_close(test->accepted, 0U, "amplification test done");
        neverc_quic_conn_free(test->accepted);
    }
}

/* Drains whatever the server sent to the candidate address and returns the
 * total byte count plus the largest single datagram. */
static size_t quic_amp_drain(neverc_udp_conn_t *victim, size_t *largest_out) {
    uint8_t buffer[2048];
    size_t total = 0;
    *largest_out = 0;
    for (;;) {
        neverc_udp_addr_t from;
        int n = neverc_udp_read_from(victim, buffer, sizeof(buffer), &from);
        if (n <= 0) break;
        total += (size_t)n;
        if ((size_t)n > *largest_out) *largest_out = (size_t)n;
    }
    return total;
}

/* RFC 9000 §8 / §9.3.1: the three-times budget is per path. A connection
 * validated on its original path must not fund PATH_CHALLENGE datagrams to a
 * spoofed migration candidate, or a 29-byte forged packet turns the server
 * into a 41x reflector. */
static void quic_test_migration_respects_anti_amplification(void) {
    neverc_network_test_files_t files;
    CHECK(neverc_network_test_write_certs("quic-amp", &files) == 0);
    int port = quic_test_free_udp_port();
    CHECK(port > 0);
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);
    const char *alpn[] = {"neverc-quic-amp/1", NULL};
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

    quic_amp_server_t test = {endpoint, NULL,
                              neverc_thread_channel_create(1),
                              neverc_thread_channel_create(1)};
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(test.ready != NULL);
    CHECK(test.release != NULL);
    CHECK(executor != NULL);
    CHECK(neverc_thread_executor_submit(executor, quic_amp_server_task,
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

    void *ignored = NULL;
    CHECK(neverc_thread_channel_receive(test.ready, &ignored) ==
          NEVERC_THREAD_OK);
    struct neverc_quic_conn *server = test.accepted;
    CHECK(server != NULL);

    neverc_udp_conn_t *victim = neverc_udp_listen("127.0.0.1:0", &error);
    neverc_udp_addr_t victim_addr;
    CHECK(victim != NULL);
    if (server && victim &&
        neverc_udp_local_addr(victim, &victim_addr) == 0) {
        (void)neverc_udp_set_read_timeout(victim, 200);
        CHECK(server->address_validated == 1);

        /* A forged packet from the victim address opens a candidate path
         * whose own budget is still zero. */
        server->candidate_addr = victim_addr;
        server->candidate_bytes_received = 0;
        server->candidate_bytes_sent = 0;
        server->path_validation_pending = 1;
        server->path_challenge_pending = 1;
        (void)neverc_quic_conn_flush(server);
        size_t largest = 0;
        CHECK(quic_amp_drain(victim, &largest) == 0);

        /* With 29 bytes credited the challenge may go out, but unexpanded:
         * 3 * 29 = 87 bytes, far below the 1200-byte expansion. */
        server->candidate_bytes_received = 29;
        server->path_challenge_pending = 1;
        (void)neverc_quic_conn_flush(server);
        size_t sent = quic_amp_drain(victim, &largest);
        CHECK(sent <= 87U);
        CHECK(largest < 1200U);

        /* RFC 9000 §8.2.1 expansion still happens once the path has paid
         * for it. */
        server->candidate_bytes_received = 4096;
        server->candidate_bytes_sent = 0;
        server->path_challenge_pending = 1;
        (void)neverc_quic_conn_flush(server);
        (void)quic_amp_drain(victim, &largest);
        CHECK(largest == 1200U);

        server->path_validation_pending = 0;
        server->path_challenge_pending = 0;
    }
    if (victim) neverc_udp_close(victim);

    CHECK(neverc_thread_channel_send(test.release, &test) ==
          NEVERC_THREAD_OK);
    if (client) {
        neverc_quic_conn_close(client, 0U, "amplification test done");
        neverc_quic_conn_free(client);
    }
    if (executor) {
        (void)neverc_thread_executor_shutdown(executor);
        neverc_thread_executor_free(executor);
    }
    neverc_thread_channel_free(test.ready);
    neverc_thread_channel_free(test.release);
    neverc_quic_endpoint_close(endpoint);
    neverc_network_test_remove_certs(&files);
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
    if (!client)
        printf("  dial error: %s\n", error ? error : "(null)");
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

static void quic_test_clienthello_legacy_session_id_empty(void) {
    neverc_quic_config_t config = neverc_quic_config_default();
    config.insecure_skip_verify = 1;
    config.server_name = "localhost";
    quic_transport_params_t local;
    quic_transport_params_t peer;
    neverc_quic_transport_params_default(&local);
    neverc_quic_transport_params_default(&peer);
    quic_tls_t *tls = neverc_quic_tls_create(0);
    CHECK(tls != NULL);
    if (!tls)
        return;
    CHECK(neverc_quic_tls_configure(tls, &config, "localhost",
                                    &local, &peer) == 0);
    CHECK(neverc_quic_tls_start(tls) == 0);
    uint64_t offset = 0;
    const uint8_t *data = NULL;
    size_t len = 0;
    CHECK(neverc_quic_tls_get_crypto_data(tls, QUIC_ENC_INITIAL,
                                          &offset, &data, &len) == 0);
    CHECK(data != NULL && len >= 39);
    if (data && len >= 39) {
        CHECK(data[0] == 1); /* ClientHello */
        CHECK(data[4 + 2 + 32] == 0); /* empty legacy_session_id */
    }
    neverc_quic_tls_destroy(tls);
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
    quic_test_clienthello_legacy_session_id_empty();
    quic_test_preserves_clienthello_parser_error();
    quic_test_server_flight_preserves_certificate_chain();
    quic_test_roundtrip();
    quic_test_migration_respects_anti_amplification();
    printf("quic-e2e: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
