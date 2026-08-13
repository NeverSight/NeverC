#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    return allocation_fails() ? NULL : calloc(count, size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return allocation_fails() ? NULL : realloc(ptr, size);
}

static char *controlled_strdup(const char *s) {
    if (allocation_fails()) return NULL;
    size_t length = strlen(s);
    char *copy = (char *)malloc(length + 1);
    if (copy) memcpy(copy, s, length + 1);
    return copy;
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#define strdup controlled_strdup
#include "../../../std/src/net/http/http.c"
#undef malloc
#undef calloc
#undef realloc
#undef strdup

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",             \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

static void plain_handler(neverc_http_request_t *request,
                          neverc_http_response_writer_t *writer) {
    (void)request;
    (void)writer;
}

static void context_handler(neverc_http_request_t *request,
                            neverc_http_response_writer_t *writer,
                            void *context) {
    (void)request;
    (void)writer;
    (void)context;
}

static neverc_http_mux_t *new_mux(void) {
    reset_allocator(0);
    return neverc_http_new_mux();
}

static int transport_result;
static int transport_calls;

static int controlled_transport_write(void *context, const void *data,
                                      size_t length, int timeout_ms) {
    (void)context;
    (void)data;
    (void)length;
    (void)timeout_ms;
    transport_calls++;
    return transport_result;
}

static void init_test_writer(neverc_http_response_writer_t *writer) {
    memset(writer, 0, sizeof(*writer));
    writer->fd = NC_INVALID_SOCK;
    writer->status = 200;
    writer->keep_alive = 1;
    writer->transport_write = controlled_transport_write;
    nc_buf_init(&writer->body);
}

static void free_test_writer(neverc_http_response_writer_t *writer) {
    for (int i = 0; i < writer->nheaders; i++) {
        free(writer->header_names[i]);
        free(writer->header_values[i]);
    }
    for (int i = 0; i < writer->ntrailers; i++) {
        free(writer->trailer_names[i]);
        free(writer->trailer_values[i]);
    }
    nc_buf_free(&writer->body);
}

static int test_response_header_failures(void) {
    neverc_http_response_writer_t writer;

    init_test_writer(&writer);
    reset_allocator(1);
    transport_calls = 0;
    CHECK(rw_flush(&writer) == -1);
    CHECK(writer.aborted && !writer.headers_sent);
    CHECK(transport_calls == 0);
    free_test_writer(&writer);

    init_test_writer(&writer);
    writer.chunked = 1;
    reset_allocator(1);
    transport_calls = 0;
    CHECK(neverc_http_flush_chunk(&writer) == -1);
    CHECK(writer.aborted && !writer.headers_sent);
    CHECK(transport_calls == 0);
    free_test_writer(&writer);

    init_test_writer(&writer);
    writer.chunked = 1;
    reset_allocator(0);
    transport_result = -1;
    transport_calls = 0;
    CHECK(neverc_http_flush_chunk(&writer) == -1);
    CHECK(writer.aborted && !writer.headers_sent);
    CHECK(transport_calls == 1);
    free_test_writer(&writer);

    init_test_writer(&writer);
    reset_allocator(0);
    transport_result = -1;
    transport_calls = 0;
    CHECK(neverc_http_sse_begin(&writer) == -1);
    CHECK(writer.aborted && !writer.headers_sent);
    CHECK(transport_calls == 1);
    free_test_writer(&writer);
    return 0;
}

int main(void) {
    CHECK(test_response_header_failures() == 0);

    neverc_http_mux_t *mux = new_mux();
    CHECK(mux != NULL);
    reset_allocator(0);
    CHECK(neverc_http_mux_handle_context(
              mux, "GET /probe", context_handler, NULL) == 0);
    size_t route_allocations = allocation_count;
    CHECK(route_allocations > 0);
    neverc_http_mux_free(mux);

    for (size_t failure = 1; failure <= route_allocations; failure++) {
        mux = new_mux();
        CHECK(mux != NULL);
        reset_allocator(failure);
        neverc_http_mux_handle(mux, "GET /plain", plain_handler);
        CHECK(mux->nroutes == 0);
        reset_allocator(0);
        neverc_http_mux_handle(mux, "GET /plain", plain_handler);
        CHECK(mux->nroutes == 1);
        neverc_http_mux_free(mux);

        mux = new_mux();
        CHECK(mux != NULL);
        reset_allocator(failure);
        CHECK(neverc_http_mux_handle_context(
                  mux, "GET /context", context_handler, NULL) == -1);
        CHECK(mux->nroutes == 0);
        reset_allocator(0);
        CHECK(neverc_http_mux_handle_context(
                  mux, "GET /context", context_handler, NULL) == 0);
        neverc_http_mux_free(mux);

        mux = new_mux();
        CHECK(mux != NULL);
        reset_allocator(failure);
        CHECK(neverc_http_mux_handle_stream_context(
                  mux, "GET /stream", context_handler, NULL) == -1);
        CHECK(mux->nroutes == 0);
        reset_allocator(0);
        CHECK(neverc_http_mux_handle_stream_context(
                  mux, "GET /stream", context_handler, NULL) == 0);
        neverc_http_mux_free(mux);
    }

    puts("passed");
    return 0;
}
