#include "neverc/std/crypto/tls.h"
#include "neverc/std/net/tcp.h"

#include <stdio.h>
#include <string.h>

static int run_client(const char *address, const char *root_path) {
    neverc_tls_config_t *config = neverc_tls_config_new();
    if (!config)
        return 10;
    neverc_tls_config_set_server_name(config, "localhost");
    if (neverc_tls_config_add_root_certificates(
            config, root_path) != 0) {
        neverc_tls_config_free(config);
        return 11;
    }

    const char *error = NULL;
    neverc_tcp_conn_t *tcp = neverc_tcp_dial(address, &error);
    if (!tcp) {
        fprintf(stderr, "TCP dial failed: %s\n",
                error ? error : "unknown error");
        neverc_tls_config_free(config);
        return 12;
    }
    neverc_tls_conn_t *connection =
        neverc_tls_client(tcp, config, &error);
    if (!connection) {
        fprintf(stderr, "TLS client handshake failed: %s\n",
                error ? error : "unknown error");
        neverc_tcp_close(tcp);
        neverc_tls_config_free(config);
        return 13;
    }

    static const char request[] =
        "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    int result = 0;
    if (neverc_tls_write(
            connection, request, sizeof(request) - 1) !=
        (int)(sizeof(request) - 1)) {
        result = 14;
    } else {
        char response[4096];
        int response_len =
            neverc_tls_read(connection, response, sizeof(response));
        if (response_len <= 0)
            result = 15;
    }

    neverc_tls_close(connection);
    neverc_tcp_close(tcp);
    neverc_tls_config_free(config);
    return result;
}

static int run_server(
    const char *address, const char *certificate_path,
    const char *key_path) {
    neverc_tls_config_t *config = neverc_tls_config_new();
    if (!config)
        return 20;
    if (neverc_tls_config_load_cert(
            config, certificate_path, key_path) != 0) {
        neverc_tls_config_free(config);
        return 21;
    }

    const char *error = NULL;
    neverc_tls_listener_t *listener =
        neverc_tls_listen(address, config, &error);
    if (!listener) {
        fprintf(stderr, "TLS listen failed: %s\n",
                error ? error : "unknown error");
        neverc_tls_config_free(config);
        return 22;
    }
    neverc_tls_conn_t *connection =
        neverc_tls_accept(listener, &error);
    if (!connection) {
        fprintf(stderr, "TLS server handshake failed: %s\n",
                error ? error : "unknown error");
        neverc_tls_listener_close(listener);
        neverc_tls_config_free(config);
        return 23;
    }

    char request[256];
    int request_len =
        neverc_tls_read(connection, request, sizeof(request));
    int result = 0;
    if (request_len <= 0) {
        result = 24;
    } else if (neverc_tls_write(
                   connection, "pong", 4) != 4) {
        result = 25;
    }

    neverc_tls_close(connection);
    neverc_tls_listener_close(listener);
    neverc_tls_config_free(config);
    return result;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "client") == 0)
        return run_client(argv[2], argv[3]);
    if (argc == 5 && strcmp(argv[1], "server") == 0)
        return run_server(argv[2], argv[3], argv[4]);
    fprintf(stderr,
            "usage: %s client ADDRESS ROOT.pem\n"
            "       %s server ADDRESS CERT.pem KEY.pem\n",
            argv[0], argv[0]);
    return 2;
}
