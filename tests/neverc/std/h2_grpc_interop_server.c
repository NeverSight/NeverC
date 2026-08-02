#include "neverc/std/net/grpc.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/http/http2.h"

#include <stdio.h>
#include <stdlib.h>

static neverc_grpc_status_t grpc_echo(
    neverc_grpc_server_stream_t *stream, void *context) {
    (void)context;
    neverc_grpc_message_t message;
    if (neverc_grpc_server_stream_recv(stream, &message) != 1)
        return NEVERC_GRPC_INVALID_ARGUMENT;
    if (neverc_grpc_server_stream_send(stream, message.data,
                                       message.length) != 0)
        return NEVERC_GRPC_INTERNAL;
    return neverc_grpc_server_stream_recv(stream, &message) == 0
        ? NEVERC_GRPC_OK : NEVERC_GRPC_INVALID_ARGUMENT;
}

static const neverc_grpc_method_t grpc_echo_method = {
    "/test.Echo/Unary", NEVERC_GRPC_UNARY, 1024U * 1024U,
    1024U * 1024U, grpc_echo, NULL};

static void http_root(neverc_http_request_t *request,
                      neverc_http_response_writer_t *writer) {
    (void)request;
    neverc_http_set_header(writer, "X-NeverC-HTTP2", "yes");
    (void)neverc_http_write_string(writer, "NeverC HTTP/2 interop");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    long port = strtol(argv[1], &end, 10);
    if (!end || *end || port < 1 || port > 65535) return 2;
    neverc_http_mux_t *mux = neverc_http_new_mux();
    if (!mux) return 1;
    neverc_http_mux_handle(mux, "/", http_root);
    if (neverc_grpc_server_register(mux, &grpc_echo_method) != 0) {
        neverc_http_mux_free(mux);
        return 1;
    }
    neverc_h2_server_t *server = neverc_h2_server_create(mux);
    if (!server) {
        neverc_http_mux_free(mux);
        return 1;
    }
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%ld", port);
    int result = neverc_h2_listen_and_serve_h2c(address, server);
    neverc_h2_server_destroy(server);
    neverc_http_mux_free(mux);
    return result == 0 ? 0 : 1;
}
