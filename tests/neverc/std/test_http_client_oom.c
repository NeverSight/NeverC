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
