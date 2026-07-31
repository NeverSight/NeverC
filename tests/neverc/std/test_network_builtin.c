#include "neverc/std/net.h"

#include <stdio.h>
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
    net.tcp.listener_close(tcp);

    err = NULL;
    neverc_udp_conn_t *udp = net.udp.listen("127.0.0.1:0", &err);
    CHECK(udp != NULL);
    CHECK(err == NULL);
    net.udp.close(udp);

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
#else
    CHECK(strcmp(neverc_http_status_text(200), "OK") == 0);
#endif

    puts("passed");
    return 0;
}
