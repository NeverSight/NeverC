#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t successful_allocations;
static size_t free_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    if (allocation_fails()) return NULL;
    void *result = malloc(size);
    if (result) successful_allocations++;
    return result;
}

static void *controlled_calloc(size_t count, size_t size) {
    if (allocation_fails()) return NULL;
    void *result = calloc(count, size);
    if (result) successful_allocations++;
    return result;
}

static void *controlled_realloc(void *ptr, size_t size) {
    if (allocation_fails()) return NULL;
    return realloc(ptr, size);
}

static char *controlled_strdup(const char *s) {
    if (allocation_fails()) return NULL;
    size_t length = strlen(s);
    char *copy = (char *)malloc(length + 1);
    if (copy) {
        memcpy(copy, s, length + 1);
        successful_allocations++;
    }
    return copy;
}

static void controlled_free(void *ptr) {
    if (ptr) free_count++;
    free(ptr);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#define strdup controlled_strdup
#define free controlled_free
#define neverc_http_mux_handle controlled_plain_register
#define nc_http_mux_handle_owned_context controlled_owned_register
#define nc_http_default_handle_owned_context controlled_default_owned_register
#include "../../../std/src/net/http/http_client.c"
#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef free
#undef neverc_http_mux_handle
#undef nc_http_mux_handle_owned_context
#undef nc_http_default_handle_owned_context

static int plain_registration_calls;
static int owned_registration_calls;
static int default_registration_calls;
static int registration_result;

void controlled_plain_register(neverc_http_mux_t *mux, const char *pattern,
                               neverc_http_handler_func_t handler) {
    (void)mux;
    (void)pattern;
    (void)handler;
    plain_registration_calls++;
}

int controlled_owned_register(
    neverc_http_mux_t *mux, const char *pattern,
    neverc_http_handler_context_func_t handler, void *context,
    void (*destroy_context)(void *)) {
    (void)mux;
    (void)pattern;
    (void)handler;
    owned_registration_calls++;
    if (registration_result == 0 && destroy_context)
        destroy_context(context);
    return registration_result;
}

int controlled_default_owned_register(
    const char *pattern, neverc_http_handler_context_func_t handler,
    void *context, void (*destroy_context)(void *)) {
    (void)pattern;
    (void)handler;
    default_registration_calls++;
    if (registration_result == 0 && destroy_context)
        destroy_context(context);
    return registration_result;
}

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",             \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_test(size_t failure, int register_result) {
    allocation_count = 0;
    successful_allocations = 0;
    free_count = 0;
    fail_at = failure;
    plain_registration_calls = 0;
    owned_registration_calls = 0;
    default_registration_calls = 0;
    registration_result = register_result;
}

static void inner_handler(neverc_http_request_t *request,
                          neverc_http_response_writer_t *writer) {
    (void)request;
    (void)writer;
}

int main(void) {
    neverc_http_mux_t *fake_mux = (neverc_http_mux_t *)(uintptr_t)1;

    reset_test(0, 0);
    neverc_http_strip_prefix(fake_mux, "/api", "/api/", inner_handler);
    size_t strip_allocations = allocation_count;
    CHECK(strip_allocations > 0);
    CHECK(plain_registration_calls == 0);
    CHECK(owned_registration_calls == 1);
    CHECK(default_registration_calls == 0);
    CHECK(free_count == successful_allocations);

    for (size_t failure = 1; failure <= strip_allocations; failure++) {
        reset_test(failure, 0);
        neverc_http_strip_prefix(fake_mux, "/api", "/api/", inner_handler);
        CHECK(plain_registration_calls == 0);
        CHECK(owned_registration_calls == 0);
        CHECK(default_registration_calls == 0);
        CHECK(free_count == successful_allocations);
    }

    reset_test(0, -1);
    neverc_http_strip_prefix(fake_mux, "/api", "/api/", inner_handler);
    CHECK(owned_registration_calls == 1);
    CHECK(free_count == successful_allocations);

    reset_test(0, 0);
    neverc_http_strip_prefix(NULL, "/api", "/api/", inner_handler);
    CHECK(default_registration_calls == 1);
    CHECK(free_count == successful_allocations);

    puts("passed");
    return 0;
}
