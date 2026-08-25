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

static void large_handler(neverc_http_request_t *req,
                          neverc_http_response_writer_t *w) {
    (void)req;
    char block[4096];
    memset(block, 'L', sizeof(block));
    for (int i = 0; i < 512; i++)
        if (neverc_http_write(w, block, sizeof(block)) !=
            (int)sizeof(block))
            return;
}

#ifndef _WIN32
static void content_type_handler(neverc_http_request_t *req,
                                 neverc_http_response_writer_t *w) {
    neverc_http_write_string(w, "ct=");
    neverc_http_write_string(w, req->content_type ? req->content_type : "");
}
#endif

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
        char method[16];
        check_str("echoed method header",
                  neverc_http_response_header(
                      resp, "X-Method", method, sizeof(method)),
                  "GET");
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

static void test_large_response_write_all(void) {
    printf("[large_response_write_all]\n");

    neverc_httptest_server_t *ts = neverc_httptest_new_server(large_handler);
    check_not_null("large response server", ts);
    if (!ts) return;
    neverc_http_response_t *resp = neverc_http_get(neverc_httptest_url(ts));
    check_not_null("large response", resp);
    if (resp) {
        check_true("large response is complete",
                   resp->error == NULL && resp->body &&
                   resp->body_len == 512U * 4096U &&
                   resp->body[0] == 'L' &&
                   resp->body[resp->body_len - 1U] == 'L');
        neverc_http_response_free(resp);
    }
    neverc_httptest_close(ts);
}

static void test_close_without_request(void) {
    printf("[close_without_request]\n");

    neverc_httptest_server_t *ts = neverc_httptest_new_server(hello_handler);
    check_not_null("idle server created", ts);
    check_not_null("idle url", neverc_httptest_url(ts));
    check_not_null("idle addr", neverc_httptest_addr(ts));
    neverc_httptest_close(ts);
    neverc_httptest_close(NULL);

    ts = neverc_httptest_new_server(hello_handler);
    check_not_null("second idle server created", ts);
    if (ts) {
        neverc_http_response_t *resp = neverc_http_get(neverc_httptest_url(ts));
        check_not_null("request before close", resp);
        if (resp) neverc_http_response_free(resp);
        neverc_httptest_close(ts);
    }
}

static void test_recorder(void) {
    printf("[recorder]\n");

    neverc_httptest_recorder_t *rec = neverc_httptest_new_recorder();
    check_not_null("recorder", rec);
    check_int("default status", rec->status_code, 200);

    neverc_http_response_writer_t *w = neverc_httptest_recorder_writer(rec);
    check_not_null("recorder writer", w);
    neverc_http_set_status(w, 201);
    neverc_http_set_header(w, "X-Test", "yes");
    neverc_http_write_string(w, "hello");
    neverc_httptest_recorder_flush(rec);
    check_int("captured status", rec->status_code, 201);
    check_str("captured body", rec->body, "hello");
    check_int("captured body len", (int)rec->body_len, 5);
    check_str("captured header",
              neverc_httptest_recorder_header(rec, "X-Test"), "yes");
    check_str("inferred Content-Length",
              neverc_httptest_recorder_header(rec, "Content-Length"), "5");

    neverc_httptest_recorder_free(rec);

    rec = neverc_httptest_new_recorder();
    check_not_null("length recorder", rec);
    w = neverc_httptest_recorder_writer(rec);
    check_not_null("length writer", w);
    neverc_http_set_status(w, 304);
    check_int("explicit Content-Length accepted",
              neverc_http_set_content_length(w, 1234U), 0);
    check_str("override Content-Length",
              neverc_httptest_recorder_header(rec, "Content-Length"), "1234");
    neverc_httptest_recorder_free(rec);

    rec = neverc_httptest_new_recorder();
    check_not_null("204 recorder", rec);
    w = neverc_httptest_recorder_writer(rec);
    check_not_null("204 writer", w);
    neverc_http_set_status(w, 204);
    check_int("204 Content-Length metadata accepted",
              neverc_http_set_content_length(w, 99U), 0);
    neverc_httptest_recorder_flush(rec);
    check_true("204 omits Content-Length",
               neverc_httptest_recorder_header(rec, "Content-Length") == NULL);
    neverc_httptest_recorder_free(rec);
}

#ifndef _WIN32
static int httptest_raw(const char *addr, const char *request,
                        char *response, size_t response_length) {
    const char *error = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &error);
    if (!conn) return -1;
    neverc_tcp_set_timeout(conn, 2000);
    neverc_tcp_write(conn, request, strlen(request));
    int total = 0;
    while (total < (int)response_length - 1) {
        int n = neverc_tcp_read(conn, response + total,
                                response_length - 1 - (size_t)total);
        if (n <= 0) break;
        total += n;
    }
    response[total] = '\0';
    neverc_tcp_close(conn);
    return total;
}

static void test_httptest_strict_parser(void) {
    printf("[strict_parser]\n");

    neverc_httptest_server_t *ts = neverc_httptest_new_server(echo_handler);
    check_not_null("strict server", ts);
    if (!ts) return;

    const char *addr = neverc_httptest_addr(ts);
    char buf[4096];

    int n = httptest_raw(addr,
        "GET / HTTP/1.1\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("no host 400",
              n > 0 && strstr(buf, "400 Bad Request") != NULL, 1);

    n = httptest_raw(addr,
        "GET / HTTP/1.1\r\nHost: example.com/foo\r\n"
        "Connection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("invalid host 400",
              n > 0 && strstr(buf, "400 Bad Request") != NULL, 1);

    n = httptest_raw(addr,
        "GET / HTTP/1.1\r\nHost: localhost\r\nX-Foo: bar\r\n"
        " baz\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("fold 400",
              n > 0 && strstr(buf, "400 Bad Request") != NULL, 1);

    n = httptest_raw(addr,
        "GET / HTTP/1.1\r\nHost: [::1,evil]\r\n"
        "Connection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("ipv6 comma host 400",
              n > 0 && strstr(buf, "400 Bad Request") != NULL, 1);

    n = httptest_raw(addr,
        "POST / HTTP/1.1\r\nHost: localhost\r\n"
        "Connection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("post without cl is empty body",
              n > 0 && strstr(buf, "400 Bad Request") == NULL &&
              strstr(buf, "no body") != NULL, 1);

    n = httptest_raw(addr,
        "POST / HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Length: 4\r\nConnection: close\r\n\r\n"
        "ABCDXXXX",
        buf, sizeof(buf));
    check_int("cl honored", n > 0 && strstr(buf, "ABCD") != NULL, 1);
    check_int("cl extra ignored", strstr(buf, "XXXX") == NULL, 1);

    neverc_httptest_close(ts);

    ts = neverc_httptest_new_server(hello_handler);
    check_not_null("head server", ts);
    if (ts) {
        n = httptest_raw(neverc_httptest_addr(ts),
            "HEAD / HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_true("head has content-length 11",
                   n > 0 && strstr(buf, "Content-Length: 11") != NULL);
        {
            const char *hdr_end = n > 0 ? strstr(buf, "\r\n\r\n") : NULL;
            check_true("head has no body",
                       hdr_end && strstr(hdr_end + 4, "hello world") == NULL);
        }
        neverc_httptest_close(ts);
    }

    ts = neverc_httptest_new_server(content_type_handler);
    check_not_null("content-type server", ts);
    if (ts) {
        n = httptest_raw(neverc_httptest_addr(ts),
            "POST / HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Type: text/plain\r\nContent-Length: 0\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        {
            const char *hdr_end = n > 0 ? strstr(buf, "\r\n\r\n") : NULL;
            check_true("content-type echoed",
                       hdr_end && strstr(hdr_end + 4, "ct=text/plain") != NULL);
        }
        neverc_httptest_close(ts);
    }

    ts = neverc_httptest_new_server(echo_handler);
    check_not_null("chunked server", ts);
    if (ts) {
        n = httptest_raw(neverc_httptest_addr(ts),
            "POST / HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: close\r\n\r\n"
            "5\r\nhello\r\n0\r\n\r\n",
            buf, sizeof(buf));
        {
            const char *hdr_end = n > 0 ? strstr(buf, "\r\n\r\n") : NULL;
            check_true("chunked POST is not 400",
                       n > 0 && strstr(buf, "400") == NULL);
            check_true("chunked POST echoes body",
                       hdr_end && strstr(hdr_end + 4, "hello") != NULL);
        }

        n = httptest_raw(neverc_httptest_addr(ts),
            "POST / HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: close\r\n\r\n"
            "0;ext\r\n\r\n",
            buf, sizeof(buf));
        check_true("chunked last-chunk ext is not 400",
                   n > 0 && strstr(buf, "400") == NULL);

        n = httptest_raw(neverc_httptest_addr(ts),
            "POST / HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: close\r\n\r\n"
            ";ignored\r\n\r\n",
            buf, sizeof(buf));
        check_true("chunked size without digits is 400",
                   n > 0 && strstr(buf, "400") != NULL);
        neverc_httptest_close(ts);
    }
}
#endif

int main(void) {
    printf("=== NeverC httptest Tests ===\n");

    test_recorder();
    test_close_without_request();
    test_new_server();
    test_server_with_path();
    test_server_status_code();
    test_large_response_write_all();
#ifndef _WIN32
    test_httptest_strict_parser();
#endif

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
