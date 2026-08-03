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

int main(void) {
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
