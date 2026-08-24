#include "neverc/std/net/http3.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run;
static int tests_failed;

#define CHECK(condition)                                                     \
    do {                                                                     \
        tests_run++;                                                         \
        if (!(condition)) {                                                  \
            tests_failed++;                                                  \
            printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #condition);  \
        }                                                                    \
    } while (0)

static void test_server_lifecycle(void) {
    neverc_http3_server_t *server = neverc_http3_server_create(NULL);
    CHECK(server != NULL);
    CHECK(neverc_http3_server_max_streams(server) == 100U);
    CHECK(neverc_http3_server_is_running(server) == 0);
    neverc_http3_server_set_max_streams(server, 200U);
    CHECK(neverc_http3_server_max_streams(server) == 200U);
    neverc_http3_server_set_max_streams(server, 0U);
    CHECK(neverc_http3_server_max_streams(server) == 200U);
    neverc_http3_server_stop(server);
    CHECK(neverc_http3_server_is_running(server) == 0);
    neverc_http3_server_destroy(server);
    neverc_http3_server_destroy(NULL);
}

static void test_public_error_codes(void) {
    CHECK(NC_H3_QPACK_DECOMPRESSION_FAILED == 0x0200);
    CHECK(NC_H3_QPACK_ENCODER_STREAM_ERROR == 0x0201);
    CHECK(NC_H3_QPACK_DECODER_STREAM_ERROR == 0x0202);
    CHECK(NC_H3_GENERAL_PROTOCOL_ERROR == 0x0101);
    CHECK(NC_H3_STREAM_CREATION_ERROR == 0x0103);
    CHECK(NC_H3_CLOSED_CRITICAL_STREAM == 0x0104);
    CHECK(NC_H3_FRAME_UNEXPECTED == 0x0105);
    CHECK(NC_H3_FRAME_ERROR == 0x0106);
    CHECK(NC_H3_EXCESSIVE_LOAD == 0x0107);
    CHECK(NC_H3_MESSAGE_ERROR == 0x010E);
}

static void test_invalid_server_inputs(void) {
    neverc_http3_server_t *server = neverc_http3_server_create(NULL);
    CHECK(server != NULL);
    errno = 0;
    CHECK(neverc_http3_listen_and_serve(NULL, server, "cert", "key") == -1);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(neverc_http3_listen_and_serve("127.0.0.1:443", NULL, "cert",
                                        "key") == -1);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(neverc_http_serve_all("127.0.0.1:0", NULL, "cert", "key") == -1);
    CHECK(errno == EINVAL);
    neverc_http3_server_destroy(server);
}

static void test_invalid_client_inputs(void) {
    neverc_http_response_t *response = neverc_http3_get("http://example.com/");
    CHECK(response != NULL && response->error != NULL);
    CHECK(response && strstr(response->error, "absolute https") != NULL);
    neverc_http_response_free(response);
    response = neverc_http3_post("https://example.com/", "text/plain", NULL,
                                  1U);
    CHECK(response != NULL && response->error != NULL);
    neverc_http_response_free(response);
}

int main(void) {
    printf("HTTP/3 public server test suite:\n");
    test_server_lifecycle();
    test_public_error_codes();
    test_invalid_server_inputs();
    test_invalid_client_inputs();
    printf("http3-server: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
