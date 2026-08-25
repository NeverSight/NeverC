#include "neverc/std/net/http/http2.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int status_code;
    neverc_hpack_header_t *headers;
    size_t header_count;
    neverc_hpack_header_t *trailers;
    size_t trailer_count;
    uint8_t *body;
    size_t body_length;
    uint32_t stream_error;
    const char *error;
} v3389_h2_response_t;

#define ABI_FIELD_EQ(field)                                                 \
    _Static_assert(offsetof(neverc_h2_response_t, field) ==                 \
                       offsetof(v3389_h2_response_t, field),                \
                   "v3389.1.4 HTTP/2 response field offset changed")

_Static_assert(sizeof(neverc_h2_response_t) == sizeof(v3389_h2_response_t),
               "v3389.1.4 HTTP/2 response size ABI changed");
_Static_assert(_Alignof(neverc_h2_response_t) ==
                   _Alignof(v3389_h2_response_t),
               "v3389.1.4 HTTP/2 response alignment ABI changed");
ABI_FIELD_EQ(status_code);
ABI_FIELD_EQ(headers);
ABI_FIELD_EQ(header_count);
ABI_FIELD_EQ(trailers);
ABI_FIELD_EQ(trailer_count);
ABI_FIELD_EQ(body);
ABI_FIELD_EQ(body_length);
ABI_FIELD_EQ(stream_error);
ABI_FIELD_EQ(error);

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(neverc_h2_response_t) == 72,
               "v3389.1.4 HTTP/2 response ABI changed");
_Static_assert(_Alignof(neverc_h2_response_t) == 8,
               "v3389.1.4 HTTP/2 response alignment changed");
_Static_assert(offsetof(neverc_h2_response_t, status_code) == 0,
               "HTTP/2 status offset changed");
_Static_assert(offsetof(neverc_h2_response_t, headers) == 8,
               "HTTP/2 headers offset changed");
_Static_assert(offsetof(neverc_h2_response_t, header_count) == 16,
               "HTTP/2 header_count offset changed");
_Static_assert(offsetof(neverc_h2_response_t, trailers) == 24,
               "HTTP/2 trailers offset changed");
_Static_assert(offsetof(neverc_h2_response_t, trailer_count) == 32,
               "HTTP/2 trailer_count offset changed");
_Static_assert(offsetof(neverc_h2_response_t, body) == 40,
               "HTTP/2 body offset changed");
_Static_assert(offsetof(neverc_h2_response_t, body_length) == 48,
               "HTTP/2 body_length offset changed");
_Static_assert(offsetof(neverc_h2_response_t, stream_error) == 56,
               "HTTP/2 stream_error offset changed");
_Static_assert(offsetof(neverc_h2_response_t, error) == 64,
               "HTTP/2 error offset changed");
#endif

static int failures;

#define CHECK(expr) do {                                                     \
    if (!(expr)) {                                                           \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);    \
        failures++;                                                          \
    }                                                                        \
} while (0)

static void test_external_response_access_is_bounded(void) {
    struct {
        neverc_h2_response_t response;
        unsigned char canary[32];
    } external;
    memset(&external, 0, sizeof(external));
    memset(external.canary, 0x5a, sizeof(external.canary));
    CHECK(neverc_h2_response_received_trailers(&external.response) == 0);
    CHECK(neverc_h2_response_received_data(&external.response) == 0);
    neverc_h2_response_free(&external.response);
    for (size_t i = 0; i < sizeof(external.canary); i++)
        CHECK(external.canary[i] == 0x5aU);
}

static void test_error_response_uses_owned_wrapper(void) {
    neverc_h2_response_t *response = neverc_h2_client_do(
        NULL, "GET", "/", NULL, 0U, NULL, 0U);
    CHECK(response != NULL);
    if (!response) return;
    CHECK(response->error != NULL);
    CHECK(neverc_h2_response_received_trailers(response) == 0);
    CHECK(neverc_h2_response_received_data(response) == 0);
    neverc_h2_response_free(response);
}

int main(void) {
    CHECK(neverc_h2_response_received_trailers(NULL) == 0);
    CHECK(neverc_h2_response_received_data(NULL) == 0);
    test_external_response_access_is_bounded();
    test_error_response_uses_owned_wrapper();
    if (failures == 0) printf("HTTP/2 response ABI tests passed\n");
    return failures == 0 ? 0 : 1;
}
