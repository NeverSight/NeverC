#include "neverc/std/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
#ifdef __neverc__
    CHECK(strcmp(net.http.status_text(200), "OK") == 0);

    char canonical[32];
    CHECK(net.http.canonical_header_key("content-type", canonical,
                                         sizeof(canonical)) == canonical);
    CHECK(strcmp(canonical, "Content-Type") == 0);

    const char *err = NULL;
    neverc_tcp_listener_t *tcp =
        net.tcp.listen("127.0.0.1:0", &err);
    CHECK(tcp != NULL);
    CHECK(err == NULL);
    neverc_tcp_conn_t *pending = NULL;
    neverc_net_result_t tcp_result =
        net.tcp.try_accept(tcp, &pending);
    CHECK(tcp_result.status == NEVERC_NET_WOULD_BLOCK);
    CHECK(pending == NULL);
    CHECK(net.tcp.listener_handle(tcp) != NEVERC_NET_INVALID_HANDLE);
    net.tcp.listener_close(tcp);

    err = NULL;
    neverc_udp_conn_t *udp = net.udp.listen("127.0.0.1:0", &err);
    CHECK(udp != NULL);
    CHECK(err == NULL);
    CHECK(net.udp.conn_handle(udp) != NEVERC_NET_INVALID_HANDLE);
    CHECK(net.udp.set_read_timeout(udp, 100) == 0);
    CHECK(net.udp.set_write_timeout(udp, 100) == 0);
    CHECK(net.udp.set_read_deadline(udp, 0) == 0);
    CHECK(net.udp.set_write_deadline(udp, 0) == 0);
    neverc_udp_mtu_info_t mtu;
    CHECK(net.udp.get_mtu_info(udp, &mtu) == 0);
    CHECK(mtu.protocol_max_payload > 0);
    char packet_buf[8];
    neverc_udp_packet_info_t packet_info;
    neverc_net_result_t udp_result =
        net.udp.try_read_packet(udp, packet_buf,
                                sizeof(packet_buf), &packet_info);
    CHECK(udp_result.status == NEVERC_NET_WOULD_BLOCK);
    neverc_udp_queue_t *queue = net.udp.queue_create(2, 16);
    CHECK(queue != NULL);
    CHECK(net.udp.queue_capacity(queue) == 2);
    CHECK(net.udp.queue_length(queue) == 0);
    net.udp.queue_free(queue);
    net.udp.close(udp);

    err = NULL;
    CHECK(net.websocket.dial("not-a-websocket-url", NULL, &err) == NULL);
    CHECK(err != NULL);

    neverc_quic_config_t quic_cfg = net.quic.config_default();
    CHECK(quic_cfg.max_udp_payload_size == 1200);
    err = NULL;
    CHECK(net.quic.listen("127.0.0.1:0", &quic_cfg, &err) == NULL);
    CHECK(err != NULL);

    neverc_http3_server_t *http3 = net.http3.server_create(NULL);
    CHECK(http3 != NULL);
    CHECK(net.http3.listen_and_serve("127.0.0.1:0", http3,
                                      "cert.pem", "key.pem") == -1);
    net.http3.server_destroy(http3);

    neverc_http_unified_server_t *unified =
        net.http3.unified_server_create(NULL);
    CHECK(unified != NULL);
    CHECK(net.http3.unified_server_is_running(unified) == 0);
    CHECK(net.http3.unified_server_bound_port(unified) == -1);
    CHECK(net.http3.unified_server_listen_and_serve(
              unified, "127.0.0.1:0", "cert.pem", "key.pem") == -1);
    net.http3.unified_server_shutdown(unified);
    neverc_qpack_encoder_t *qpack_encoder =
        net.http3.qpack_encoder_create(0);
    neverc_qpack_decoder_t *qpack_decoder =
        net.http3.qpack_decoder_create(0);
    CHECK(qpack_encoder != NULL);
    CHECK(qpack_decoder != NULL);
    neverc_qpack_header_t source_header;
    source_header.name = ":method";
    source_header.value = "GET";
    uint8_t encoded_header[64];
    size_t encoded_length = 0;
    CHECK(net.http3.qpack_encode(qpack_encoder, &source_header, 1,
                                 encoded_header, sizeof(encoded_header),
                                 &encoded_length) == 0);
    CHECK(encoded_length > 0);
    neverc_qpack_header_t decoded_header;
    int decoded_count = 0;
    CHECK(net.http3.qpack_decode(qpack_decoder, encoded_header,
                                 encoded_length, &decoded_header, 1,
                                 &decoded_count) == 0);
    CHECK(decoded_count == 1);
    CHECK(strcmp(decoded_header.name, ":method") == 0);
    CHECK(strcmp(decoded_header.value, "GET") == 0);
    free(decoded_header.name);
    free(decoded_header.value);
    net.http3.qpack_encoder_destroy(qpack_encoder);
    net.http3.qpack_decoder_destroy(qpack_decoder);
    net.http3.unified_server_destroy(unified);
    CHECK(net.http3.serve_all("127.0.0.1:0", NULL,
                              "cert.pem", "key.pem") == -1);
#else
    CHECK(strcmp(neverc_http_status_text(200), "OK") == 0);
#endif

    puts("passed");
    return 0;
}
