#include "neverc/std/net/quic.h"
#include "neverc/std/net/udp.h"
#include "neverc/std/thread.h"
#include "neverc/std/time.h"
#include "network_test_support.h"

#include <stdatomic.h>
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
    neverc_udp_conn_t *front;
    neverc_udp_conn_t *rebound;
    neverc_udp_addr_t target;
    neverc_udp_addr_t client;
    int have_client;
    atomic_int running;
    int result;
    int client_packets;
    int dropped;
    int duplicated;
    int delayed;
    int rebound_packets;
    uint8_t held[NEVERC_UDP_MAX_DATAGRAM_SIZE];
    size_t held_length;
    int hold_ticks;
} quic_sim_proxy_t;

typedef struct {
    neverc_quic_endpoint_t *endpoint;
    uint16_t rebound_port;
    int result;
    int migration_verified;
} quic_sim_server_t;

static int quic_sim_read_exact(neverc_quic_stream_t *stream, void *output,
                               size_t length) {
    size_t position = 0;
    while (position < length) {
        int count = neverc_quic_stream_read(
            stream, (uint8_t *)output + position, length - position);
        if (count <= 0) return -1;
        position += (size_t)count;
    }
    return 0;
}

static void quic_sim_forward_held(quic_sim_proxy_t *proxy) {
    if (proxy->held_length == 0) return;
    if (neverc_udp_write_to(proxy->front, proxy->held,
                            proxy->held_length, &proxy->target) >= 0)
        proxy->delayed++;
    proxy->held_length = 0;
    proxy->hold_ticks = 0;
}

static void quic_sim_front_packet(quic_sim_proxy_t *proxy,
                                  const uint8_t *packet, size_t length,
                                  const neverc_udp_addr_t *source) {
    if (source->port == proxy->target.port) {
        if (proxy->have_client)
            (void)neverc_udp_write_to(proxy->front, packet, length,
                                      &proxy->client);
        return;
    }

    proxy->client = *source;
    proxy->have_client = 1;
    proxy->client_packets++;
    if (proxy->client_packets == 1) {
        proxy->dropped++;
        return;
    }
    if (proxy->client_packets == 3 && length <= sizeof(proxy->held)) {
        memcpy(proxy->held, packet, length);
        proxy->held_length = length;
        proxy->hold_ticks = 3;
        return;
    }

    neverc_udp_conn_t *egress = proxy->client_packets >= 4
        ? proxy->rebound : proxy->front;
    if (egress == proxy->rebound) proxy->rebound_packets++;
    (void)neverc_udp_write_to(egress, packet, length, &proxy->target);
    if (proxy->client_packets == 2) {
        (void)neverc_udp_write_to(egress, packet, length, &proxy->target);
        proxy->duplicated++;
    }
    /* A following packet overtakes the held packet; otherwise the timer
     * releases it after a bounded delay. */
    if (proxy->held_length != 0) quic_sim_forward_held(proxy);
}

static void quic_sim_proxy_task(void *context) {
    quic_sim_proxy_t *proxy = (quic_sim_proxy_t *)context;
    uint8_t packet[NEVERC_UDP_MAX_DATAGRAM_SIZE];
    proxy->result = -1;
    while (atomic_load_explicit(&proxy->running, memory_order_acquire)) {
        neverc_udp_packet_info_t info;
        int count = neverc_udp_read_packet(proxy->front, packet,
                                            sizeof(packet), &info);
        if (count >= 0)
            quic_sim_front_packet(proxy, packet, (size_t)count,
                                  &info.source);

        count = neverc_udp_read_packet(proxy->rebound, packet,
                                       sizeof(packet), &info);
        if (count >= 0 && proxy->have_client &&
            info.source.port == proxy->target.port)
            (void)neverc_udp_write_to(proxy->front, packet, (size_t)count,
                                      &proxy->client);

        if (proxy->held_length != 0 && --proxy->hold_ticks <= 0)
            quic_sim_forward_held(proxy);
    }
    quic_sim_forward_held(proxy);
    proxy->result = 0;
}

static void quic_sim_server_task(void *context) {
    quic_sim_server_t *test = (quic_sim_server_t *)context;
    const char *error = NULL;
    test->result = -1;
    neverc_quic_conn_t *connection = neverc_quic_accept(test->endpoint,
                                                          &error);
    if (!connection) return;
    neverc_quic_stream_t *stream = neverc_quic_accept_stream(connection,
                                                               &error);
    char request[4];
    if (!stream || quic_sim_read_exact(stream, request, sizeof(request)) != 0 ||
        memcmp(request, "ping", sizeof(request)) != 0 ||
        neverc_quic_stream_read(stream, request, sizeof(request)) != 0 ||
        neverc_quic_stream_write(stream, "pong", 4U) != 4 ||
        neverc_quic_stream_close_write(stream) != 0)
        goto done;
    char datagram[16];
    int length = neverc_quic_recv_datagram(connection, datagram,
                                            sizeof(datagram));
    if (length != 4 || memcmp(datagram, "dgram", 4U) != 0 ||
        neverc_quic_send_datagram(connection, "ack", 3U) != 0)
        goto done;
    neverc_time_sleep(500 * NEVERC_TIME_MILLISECOND);
    const char *remote = neverc_quic_conn_remote_addr(connection);
    char expected[16];
    (void)snprintf(expected, sizeof(expected), ":%u",
                   (unsigned)test->rebound_port);
    test->migration_verified = remote && strstr(remote, expected) != NULL;
    test->result = 0;

done:
    neverc_quic_stream_free(stream);
    neverc_quic_conn_close(connection, 0U, "simulation complete");
    neverc_quic_conn_free(connection);
}

static int quic_sim_bound_udp(neverc_udp_conn_t **connection,
                              neverc_udp_addr_t *address) {
    const char *error = NULL;
    *connection = neverc_udp_listen("127.0.0.1:0", &error);
    if (!*connection || neverc_udp_local_addr(*connection, address) != 0)
        return -1;
    (void)neverc_udp_set_read_timeout(*connection, 10);
    return 0;
}

static void quic_sim_run(void) {
    neverc_network_test_files_t files;
    CHECK(neverc_network_test_write_certs("quic-network-sim", &files) == 0);

    neverc_udp_conn_t *server_probe = NULL;
    neverc_udp_addr_t server_address;
    CHECK(quic_sim_bound_udp(&server_probe, &server_address) == 0);
    neverc_udp_close(server_probe);
    char listen_address[64];
    (void)snprintf(listen_address, sizeof(listen_address), "127.0.0.1:%u",
                   (unsigned)server_address.port);

    const char *alpn[] = {"neverc-quic-sim/1", NULL};
    neverc_quic_config_t server_config = neverc_quic_config_default();
    server_config.cert_file = files.server_cert;
    server_config.key_file = files.server_key;
    server_config.alpn = alpn;
    server_config.max_idle_timeout_ms = 8000U;
    const char *error = NULL;
    neverc_quic_endpoint_t *endpoint = neverc_quic_listen(
        listen_address, &server_config, &error);
    CHECK(endpoint != NULL);
    if (!endpoint) goto cleanup_files;

    quic_sim_proxy_t proxy;
    memset(&proxy, 0, sizeof(proxy));
    CHECK(quic_sim_bound_udp(&proxy.front, &proxy.client) == 0);
    neverc_udp_addr_t front_address = proxy.client;
    neverc_udp_addr_t rebound_address;
    CHECK(quic_sim_bound_udp(&proxy.rebound, &rebound_address) == 0);
    CHECK(neverc_udp_resolve_addr(listen_address, &proxy.target) == 0);
    atomic_init(&proxy.running, 1);

    quic_sim_server_t server = {endpoint, rebound_address.port, -1, 0};
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(2U, 2U);
    CHECK(executor != NULL);
    if (!executor) goto cleanup_proxy;
    CHECK(neverc_thread_executor_submit(executor, quic_sim_proxy_task,
                                         &proxy) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_submit(executor, quic_sim_server_task,
                                         &server) == NEVERC_THREAD_OK);

    char proxy_address[64];
    (void)snprintf(proxy_address, sizeof(proxy_address), "127.0.0.1:%u",
                   (unsigned)front_address.port);
    neverc_quic_config_t client_config = neverc_quic_config_default();
    client_config.alpn = alpn;
    client_config.server_name = "localhost";
    client_config.root_cert_file = files.ca;
    client_config.max_idle_timeout_ms = 8000U;
    neverc_quic_conn_t *client = neverc_quic_dial(proxy_address,
                                                    &client_config, &error);
    CHECK(client != NULL);
    if (client) {
        neverc_quic_stream_t *stream = neverc_quic_open_stream(client,
                                                                 &error);
        CHECK(stream != NULL);
        if (stream) {
            char response[4];
            CHECK(neverc_quic_stream_write(stream, "ping", 4U) == 4);
            CHECK(neverc_quic_stream_close_write(stream) == 0);
            CHECK(quic_sim_read_exact(stream, response, sizeof(response)) == 0);
            CHECK(memcmp(response, "pong", sizeof(response)) == 0);
            neverc_quic_stream_free(stream);
        }
        CHECK(neverc_quic_send_datagram(client, "dgram", 4U) == 0);
        char response[8];
        int length = neverc_quic_recv_datagram(client, response,
                                                sizeof(response));
        CHECK(length == 3 && memcmp(response, "ack", 3U) == 0);
        neverc_quic_conn_close(client, 0U, "simulation complete");
        neverc_quic_conn_free(client);
    }

    neverc_quic_endpoint_close(endpoint);
    atomic_store_explicit(&proxy.running, 0, memory_order_release);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(proxy.result == 0);
    CHECK(server.result == 0);
    CHECK(proxy.dropped == 1);
    CHECK(proxy.duplicated == 1);
    CHECK(proxy.delayed >= 1);
    CHECK(proxy.rebound_packets >= 1);
    CHECK(server.migration_verified == 1);
    neverc_thread_executor_free(executor);

cleanup_proxy:
    neverc_udp_close(proxy.front);
    neverc_udp_close(proxy.rebound);
cleanup_files:
    neverc_network_test_remove_certs(&files);
}

int main(void) {
    printf("QUIC network simulation test suite:\n");
    quic_sim_run();
    printf("quic-network-sim: %d checks, %d failed\n", tests_run,
           tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
