#include "neverc/std/net/http.h"
#include "neverc/std/net/http3.h"
#include "network_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void http3_interop_handler(neverc_http_request_t *request,
                                  neverc_http_response_writer_t *writer) {
    (void)request;
    neverc_http_set_header(writer, "X-NeverC-Interop", "quiche");
    (void)neverc_http_write_string(writer, "NeverC HTTP/3 interop");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    long port = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || port <= 0 || port > 65535) return 2;

    neverc_network_test_files_t files;
    if (neverc_network_test_write_certs("http3-interop", &files) != 0)
        return 1;
    neverc_http_mux_t *mux = neverc_http_new_mux();
    neverc_http3_server_t *server = neverc_http3_server_create(mux);
    if (!mux || !server) return 1;
    neverc_http_mux_handle(mux, "/interop", http3_interop_handler);

    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%ld", port);
    printf("NeverC HTTP/3 interop server listening on %s\n", address);
    fflush(stdout);
    int result = neverc_http3_listen_and_serve(
        address, server, files.server_cert, files.server_key);
    neverc_http3_server_destroy(server);
    neverc_http_mux_free(mux);
    neverc_network_test_remove_certs(&files);
    return result == 0 ? 0 : 1;
}
