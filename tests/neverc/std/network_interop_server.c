#include "neverc/std/net/http.h"
#include "neverc/std/net/websocket.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTEROP_MAX_MESSAGE (16U * 1024U * 1024U)

static void websocket_echo(neverc_http_request_t *request,
                           neverc_http_response_writer_t *writer) {
    neverc_ws_conn_t *websocket = neverc_ws_upgrade_http(request, writer);
    if (!websocket) return;
    void *message = malloc(INTEROP_MAX_MESSAGE);
    if (!message) {
        (void)neverc_ws_send_close(websocket, 1011, "allocation failed");
        neverc_ws_conn_free(websocket);
        return;
    }
    (void)neverc_ws_set_read_limit(websocket, INTEROP_MAX_MESSAGE);
    for (;;) {
        int opcode = 0;
        size_t message_length = 0;
        if (neverc_ws_read_data_message(websocket, &opcode, message,
                                        INTEROP_MAX_MESSAGE,
                                        &message_length) != 0)
            break;
        int sent = opcode == NC_WS_OPCODE_TEXT
            ? neverc_ws_write_text(websocket, message, message_length)
            : neverc_ws_write_binary(websocket, message, message_length);
        if (sent != 0) break;
    }
    free(message);
    neverc_ws_conn_free(websocket);
}

static void http_echo(neverc_http_request_t *request,
                      neverc_http_response_writer_t *writer) {
    neverc_http_set_header(writer, "X-NeverC-Interop", "yes");
    if (strcmp(request->method, "POST") == 0) {
        (void)neverc_http_write(writer, request->body, request->body_len);
    } else {
        (void)neverc_http_write_string(writer, "NeverC interop GET");
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 2;
    }
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    char *end = NULL;
    long port = strtol(argv[1], &end, 10);
    if (!end || *end || port < 1 || port > 65535) return 2;

    neverc_http_mux_t *mux = neverc_http_new_mux();
    if (!mux) return 1;
    neverc_http_mux_handle(mux, "/ws", websocket_echo);
    neverc_http_mux_handle(mux, "/echo", http_echo);
    neverc_http_server_config_t config = neverc_http_server_config_default();
    config.workers = 4;
    config.max_body_size = 1024 * 1024;
    config.read_timeout_ms = 10000;
    config.write_timeout_ms = 10000;
    neverc_http_server_t *server = neverc_http_server_new(mux, &config);
    if (!server) {
        neverc_http_mux_free(mux);
        return 1;
    }
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%ld", port);
    int result = neverc_http_server_listen_and_serve(server, address);
    neverc_http_server_free(server);
    neverc_http_mux_free(mux);
    return result == 0 ? 0 : 1;
}
