#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static size_t fail_malloc_size;
static int failed_expected_allocation;

static void *controlled_malloc(size_t size) {
    if (!failed_expected_allocation && size == fail_malloc_size) {
        failed_expected_allocation = 1;
        return NULL;
    }
    return malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

static void *controlled_realloc(void *pointer, size_t size) {
    return realloc(pointer, size);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#include "../../../std/src/net/http/http_client.c"
#undef malloc
#undef calloc
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",             \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_chunk_scanner(void) {
    static const char incremental[] =
        "1\r\na\r\n1\r\nb\r\n0\r\n\r\n";
    client_chunk_scan_t state;
    memset(&state, 0, sizeof(state));
    size_t wire_consumed = 0;
    size_t trailer_length = 0;
    CHECK(scan_chunked_body(incremental, 6U, 2U, 1024U, &state,
                            &wire_consumed, &trailer_length) == 0);
    CHECK(state.cursor == 6U && state.decoded_length == 1U);
    CHECK(scan_chunked_body(incremental, 12U, 2U, 1024U, &state,
                            &wire_consumed, &trailer_length) == 0);
    CHECK(state.cursor == 12U && state.decoded_length == 2U);
    CHECK(scan_chunked_body(incremental, sizeof(incremental) - 1U,
                            2U, 1024U, &state, &wire_consumed,
                            &trailer_length) == 1);
    CHECK(wire_consumed == sizeof(incremental) - 1U);
    CHECK(trailer_length == 0U);

    char oversized_line[8195];
    memset(oversized_line, '1', 8193U);
    oversized_line[8193] = '\r';
    oversized_line[8194] = '\n';
    memset(&state, 0, sizeof(state));
    CHECK(scan_chunked_body(oversized_line, sizeof(oversized_line),
                            SIZE_MAX, 1024U, &state, &wire_consumed,
                            &trailer_length) == -1);

    static const char oversized_extension[] =
        "1;abcde\r\na\r\n0\r\n\r\n";
    memset(&state, 0, sizeof(state));
    CHECK(scan_chunked_body(oversized_extension,
                            sizeof(oversized_extension) - 1U,
                            1U, 4U, &state, &wire_consumed,
                            &trailer_length) == -1);
    nc_buf_t stream_wire;
    nc_buf_init(&stream_wire);
    CHECK(nc_buf_append(&stream_wire, oversized_extension,
                        sizeof(oversized_extension) - 1U) == 0);
    neverc_http_response_t stream_response;
    memset(&stream_response, 0, sizeof(stream_response));
    CHECK(stream_read_chunked_response(NULL, NULL, &stream_wire,
                                       &stream_response, 4U, 1U,
                                       NULL, NULL) == -1);
    nc_buf_free(&stream_wire);

    static const char oversized_trailer[] = "0\r\nX: 12345\r\n\r\n";
    memset(&state, 0, sizeof(state));
    CHECK(scan_chunked_body(oversized_trailer,
                            sizeof(oversized_trailer) - 1U,
                            0U, 4U, &state, &wire_consumed,
                            &trailer_length) == -1);
    return 0;
}

static void body_handler(neverc_http_request_t *request,
                         neverc_http_response_writer_t *writer) {
    (void)request;
    neverc_http_write(writer, "hello", 5);
}

static int free_port(void) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener = neverc_tcp_listen("127.0.0.1:0", &error);
    if (!listener) return -1;
    neverc_tcp_addr_t address;
    if (neverc_tcp_listener_addr(listener, &address) != 0) {
        neverc_tcp_listener_close(listener);
        return -1;
    }
    neverc_tcp_listener_close(listener);
    return address.port;
}

int main(void) {
    CHECK(test_chunk_scanner() == 0);

    nc_buf_t trailer_wire;
    nc_buf_init(&trailer_wire);
    CHECK(nc_buf_append(&trailer_wire,
                        "X-Finished: yes\r\n\r\n", 19) == 0);
    char *trailers = NULL;
    CHECK(stream_parse_trailers(&trailer_wire, 1024, &trailers) == 1);
    CHECK(trailers != NULL);
    CHECK(strcmp(trailers, "X-Finished: yes\r\n") == 0);
    free(trailers);
    nc_buf_free(&trailer_wire);

    nc_buf_init(&trailer_wire);
    CHECK(nc_buf_append(&trailer_wire,
                        "X-Finished: yes\r\n\r\n", 19) == 0);
    trailers = NULL;
    fail_malloc_size = 18;
    failed_expected_allocation = 0;
    CHECK(stream_parse_trailers(&trailer_wire, 1024, &trailers) == -2);
    CHECK(failed_expected_allocation);
    CHECK(trailers == NULL);
    fail_malloc_size = 0;
    failed_expected_allocation = 0;
    nc_buf_free(&trailer_wire);

    int port = free_port();
    CHECK(port > 0);
    pid_t server = fork();
    CHECK(server >= 0);
    if (server == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/body", body_handler);
        char address[64];
        snprintf(address, sizeof(address), "127.0.0.1:%d", port);
        (void)neverc_http_listen_and_serve(address, mux);
        _exit(0);
    }
    usleep(300000);

    char url[96];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/body", port);
    fail_malloc_size = 6;
    neverc_http_response_t *response = neverc_http_get(url);
    int passed = failed_expected_allocation && response &&
        response->body == NULL && response->error &&
        strcmp(response->error, "out of memory") == 0;
    neverc_http_response_free(response);

    kill(server, SIGTERM);
    waitpid(server, NULL, 0);
    CHECK(passed);
    puts("passed");
    return 0;
}
