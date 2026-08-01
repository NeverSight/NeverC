#include "neverc/std/crypto/tls.h"
#include "neverc/std/net/tcp.h"

#include <stdio.h>
#include <string.h>

static int run_client(
    const char *address, const char *root_path,
    const char *certificate_path, const char *key_path) {
    neverc_tls_config_t *config = neverc_tls_config_new();
    if (!config)
        return 10;
    neverc_tls_config_set_server_name(config, "localhost");
    if (neverc_tls_config_add_root_certificates(
            config, root_path) != 0) {
        neverc_tls_config_free(config);
        return 11;
    }
    int omit_certificate =
        strcmp(certificate_path, "-") == 0 &&
        strcmp(key_path, "-") == 0;
    if ((!omit_certificate &&
         neverc_tls_config_load_cert(
             config, certificate_path, key_path) != 0) ||
        (strcmp(certificate_path, "-") == 0) !=
            (strcmp(key_path, "-") == 0)) {
        neverc_tls_config_free(config);
        return 12;
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
    if (neverc_tls_key_update(connection, 1) != 0) {
        fprintf(stderr, "TLS client KeyUpdate failed\n");
        result = 14;
    } else if (neverc_tls_write(
            connection, request, sizeof(request) - 1) !=
        (int)(sizeof(request) - 1)) {
        fprintf(stderr, "TLS client write failed\n");
        result = 15;
    } else {
        char response[4096];
        int response_len =
            neverc_tls_read(connection, response, sizeof(response));
        if (response_len <= 0) {
            fprintf(stderr, "TLS client read failed\n");
            result = 16;
        }
    }

    neverc_tls_close(connection);
    neverc_tcp_close(tcp);
    neverc_tls_config_free(config);
    return result;
}

static int run_server(
    const char *address, const char *certificate_path,
    const char *key_path, const char *client_root_path) {
    neverc_tls_config_t *config = neverc_tls_config_new();
    if (!config)
        return 20;
    if (neverc_tls_config_load_cert(
            config, certificate_path, key_path) != 0) {
        neverc_tls_config_free(config);
        return 21;
    }
    if (neverc_tls_config_add_root_certificates(
            config, client_root_path) != 0 ||
        neverc_tls_config_set_client_auth(
            config,
            NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY) != 0) {
        neverc_tls_config_free(config);
        return 22;
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
    int result = 0;
    if (neverc_tls_key_update(connection, 1) != 0) {
        result = 24;
    } else {
        int request_len =
            neverc_tls_read(connection, request, sizeof(request));
        if (request_len <= 0)
            result = 25;
    }
    if (result == 0 && neverc_tls_write(
                   connection, "pong", 4) != 4) {
        result = 26;
    }

    neverc_tls_close(connection);
    neverc_tls_listener_close(listener);
    neverc_tls_config_free(config);
    return result;
}

static int run_resuming_client_internal(
    const char *address, const char *root_path, int require_resume) {
    neverc_tls_config_t *config = neverc_tls_config_new();
    if (!config)
        return 30;
    neverc_tls_config_set_server_name(config, "localhost");
    if (neverc_tls_config_add_root_certificates(
            config, root_path) != 0) {
        neverc_tls_config_free(config);
        return 31;
    }

    static const char request[] =
        "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    for (int round = 0; round < 2; ++round) {
        const char *error = NULL;
        neverc_tcp_conn_t *tcp =
            neverc_tcp_dial(address, &error);
        if (!tcp) {
            neverc_tls_config_free(config);
            return 32;
        }
        neverc_tls_conn_t *connection =
            neverc_tls_client(tcp, config, &error);
        if (!connection) {
            neverc_tcp_close(tcp);
            neverc_tls_config_free(config);
            return 33;
        }
#if defined(NEVERC_TLS_TESTING)
        if (require_resume &&
            neverc_tls_test_did_resume(connection) !=
                (round == 1)) {
            neverc_tls_close(connection);
            neverc_tcp_close(tcp);
            neverc_tls_config_free(config);
            return 34;
        }
#endif
        char response[4096];
        int ok =
            neverc_tls_write(
                connection, request, sizeof(request) - 1) ==
                (int)(sizeof(request) - 1) &&
            neverc_tls_read(
                connection, response, sizeof(response)) > 0;
        neverc_tls_close(connection);
        neverc_tcp_close(tcp);
        if (!ok) {
            neverc_tls_config_free(config);
            return 35;
        }
    }
    neverc_tls_config_free(config);
    return 0;
}

static int run_resuming_client(
    const char *address, const char *root_path) {
    return run_resuming_client_internal(address, root_path, 1);
}

static int run_resuming_client_lenient(
    const char *address, const char *root_path) {
    return run_resuming_client_internal(address, root_path, 0);
}

static int run_resuming_server(
    const char *address, const char *certificate_path,
    const char *key_path) {
    neverc_tls_config_t *config = neverc_tls_config_new();
    if (!config)
        return 40;
    if (neverc_tls_config_load_cert(
            config, certificate_path, key_path) != 0) {
        neverc_tls_config_free(config);
        return 41;
    }

    const char *error = NULL;
    neverc_tls_listener_t *listener =
        neverc_tls_listen(address, config, &error);
    if (!listener) {
        neverc_tls_config_free(config);
        return 42;
    }
    for (int round = 0; round < 2; ++round) {
        neverc_tls_conn_t *connection =
            neverc_tls_accept(listener, &error);
        if (!connection) {
            neverc_tls_listener_close(listener);
            neverc_tls_config_free(config);
            return 43;
        }
#if defined(NEVERC_TLS_TESTING)
        if (neverc_tls_test_did_resume(connection) !=
            (round == 1)) {
            neverc_tls_close(connection);
            neverc_tls_listener_close(listener);
            neverc_tls_config_free(config);
            return 44;
        }
#endif
        char request[256];
        int ok =
            neverc_tls_read(
                connection, request, sizeof(request)) > 0 &&
            neverc_tls_write(connection, "pong", 4) == 4;
        neverc_tls_close(connection);
        if (!ok) {
            neverc_tls_listener_close(listener);
            neverc_tls_config_free(config);
            return 45;
        }
    }
    neverc_tls_listener_close(listener);
    neverc_tls_config_free(config);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 6 && strcmp(argv[1], "client") == 0)
        return run_client(
            argv[2], argv[3], argv[4], argv[5]);
    if (argc == 6 && strcmp(argv[1], "server") == 0)
        return run_server(
            argv[2], argv[3], argv[4], argv[5]);
    if (argc == 4 && strcmp(argv[1], "client-resume") == 0)
        return run_resuming_client(argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[1], "client-resume-lenient") == 0)
        return run_resuming_client_lenient(argv[2], argv[3]);
    if (argc == 5 && strcmp(argv[1], "server-resume") == 0)
        return run_resuming_server(
            argv[2], argv[3], argv[4]);
    fprintf(stderr,
            "usage: %s client ADDRESS ROOT.pem CERT.pem KEY.pem\n"
            "       %s server ADDRESS CERT.pem KEY.pem CLIENT_ROOT.pem\n"
            "       %s client-resume ADDRESS ROOT.pem\n"
            "       %s client-resume-lenient ADDRESS ROOT.pem\n"
            "       %s server-resume ADDRESS CERT.pem KEY.pem\n",
            argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 2;
}
