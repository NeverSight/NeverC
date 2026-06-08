#include "neverc/std/net/http/httptest.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name,
               got ? got : "NULL", expected ? expected : "NULL");
    }
}

static void check_not_null(const char *name, const void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

/* ===== Test handlers ===== */

static void hello_handler(neverc_http_request_t *req,
                            neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_string(w, "hello world");
}

static void echo_handler(neverc_http_request_t *req,
                           neverc_http_response_writer_t *w) {
    neverc_http_set_header(w, "X-Method", req->method);
    if (req->body && req->body_len > 0)
        neverc_http_write(w, req->body, req->body_len);
    else
        neverc_http_write_string(w, "no body");
}

static void status_handler(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_set_status(w, 201);
    neverc_http_write_string(w, "created");
}

/* ===== Tests ===== */

static void test_new_server(void) {
    printf("[new_server]\n");

    neverc_httptest_server_t *ts = neverc_httptest_new_server(hello_handler);
    check_not_null("server created", ts);

    const char *url = neverc_httptest_url(ts);
    check_not_null("url", url);
    check_true("url starts with http://", strncmp(url, "http://", 7) == 0);
    printf("  test server url: %s\n", url);

    /* Make a request to the test server */
    neverc_http_response_t *resp = neverc_http_get(url);
    check_not_null("response", resp);
    if (resp) {
        check_int("status 200", resp->status_code, 200);
        if (resp->body) {
            check_true("body contains hello",
                        strstr(resp->body, "hello world") != NULL);
        } else {
            check_true("body not null", 0);
        }
        neverc_http_response_free(resp);
    }

    neverc_httptest_close(ts);
}

static void test_server_with_path(void) {
    printf("[server_with_path]\n");

    neverc_httptest_server_t *ts = neverc_httptest_new_server(echo_handler);
    check_not_null("server", ts);

    char url[256];
    snprintf(url, sizeof(url), "%s/test/path", neverc_httptest_url(ts));

    neverc_http_response_t *resp = neverc_http_get(url);
    check_not_null("response", resp);
    if (resp) {
        check_int("status", resp->status_code, 200);
        neverc_http_response_free(resp);
    }

    neverc_httptest_close(ts);
}

static void test_server_status_code(void) {
    printf("[server_status_code]\n");

    neverc_httptest_server_t *ts = neverc_httptest_new_server(status_handler);
    check_not_null("server", ts);

    neverc_http_response_t *resp = neverc_http_get(neverc_httptest_url(ts));
    check_not_null("response", resp);
    if (resp) {
        check_int("status 201", resp->status_code, 201);
        if (resp->body)
            check_str("body", resp->body, "created");
        neverc_http_response_free(resp);
    }

    neverc_httptest_close(ts);
}

static void test_recorder(void) {
    printf("[recorder]\n");

    neverc_httptest_recorder_t *rec = neverc_httptest_new_recorder();
    check_not_null("recorder", rec);
    check_int("default status", rec->status_code, 200);

    neverc_httptest_recorder_free(rec);
}

int main(void) {
    printf("=== NeverC httptest Tests ===\n");

    test_recorder();
    test_new_server();
    test_server_with_path();
    test_server_status_code();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
