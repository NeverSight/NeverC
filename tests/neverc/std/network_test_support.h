#ifndef NEVERC_NETWORK_TEST_SUPPORT_H
#define NEVERC_NETWORK_TEST_SUPPORT_H

#include "network_test_certs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char ca[96];
    char server_cert[96];
    char server_key[96];
    char client_cert[96];
    char client_key[96];
} neverc_network_test_files_t;

static int neverc_network_test_write_file(const char *path,
                                          const char *contents) {
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    size_t length = strlen(contents);
    int result = fwrite(contents, 1U, length, file) == length ? 0 : -1;
    if (fclose(file) != 0) result = -1;
    return result;
}

static int neverc_network_test_write_certs(
    const char *prefix, neverc_network_test_files_t *files) {
    if (!prefix || !files) return -1;
    if (snprintf(files->ca, sizeof(files->ca), "%s-ca.pem", prefix) >=
            (int)sizeof(files->ca) ||
        snprintf(files->server_cert, sizeof(files->server_cert),
                 "%s-server.pem", prefix) >= (int)sizeof(files->server_cert) ||
        snprintf(files->server_key, sizeof(files->server_key),
                 "%s-server-key.pem", prefix) >=
            (int)sizeof(files->server_key) ||
        snprintf(files->client_cert, sizeof(files->client_cert),
                 "%s-client.pem", prefix) >= (int)sizeof(files->client_cert) ||
        snprintf(files->client_key, sizeof(files->client_key),
                 "%s-client-key.pem", prefix) >=
            (int)sizeof(files->client_key))
        return -1;
    if (neverc_network_test_write_file(files->ca,
                                       NEVERC_TEST_CA_CERT_PEM) != 0 ||
        neverc_network_test_write_file(files->server_cert,
                                       NEVERC_TEST_SERVER_CERT_PEM) != 0 ||
        neverc_network_test_write_file(files->server_key,
                                       NEVERC_TEST_SERVER_KEY_PEM) != 0 ||
        neverc_network_test_write_file(files->client_cert,
                                       NEVERC_TEST_CLIENT_CERT_PEM) != 0 ||
        neverc_network_test_write_file(files->client_key,
                                       NEVERC_TEST_CLIENT_KEY_PEM) != 0)
        return -1;
    return 0;
}

static void neverc_network_test_remove_certs(
    const neverc_network_test_files_t *files) {
    if (!files) return;
    (void)remove(files->ca);
    (void)remove(files->server_cert);
    (void)remove(files->server_key);
    (void)remove(files->client_cert);
    (void)remove(files->client_key);
}

#endif
