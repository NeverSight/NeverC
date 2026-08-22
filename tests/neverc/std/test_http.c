#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got == NULL && expected == NULL) { tests_passed++; return; }
    if (got == NULL || expected == NULL) {
        tests_failed++;
        printf("  FAIL: %s: got %s, expected %s\n", name,
               got ? got : "NULL", expected ? expected : "NULL");
        return;
    }
    if (strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

static void check_not_null(const char *name, const void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

/* ===== Status text ===== */

static void test_status_text(void) {
    printf("[status_text]\n");
    check_str("100", neverc_http_status_text(100), "Continue");
    check_str("101", neverc_http_status_text(101), "Switching Protocols");
    check_str("200", neverc_http_status_text(200), "OK");
    check_str("201", neverc_http_status_text(201), "Created");
    check_str("202", neverc_http_status_text(202), "Accepted");
    check_str("204", neverc_http_status_text(204), "No Content");
    check_str("206", neverc_http_status_text(206), "Partial Content");
    check_str("301", neverc_http_status_text(301), "Moved Permanently");
    check_str("302", neverc_http_status_text(302), "Found");
    check_str("304", neverc_http_status_text(304), "Not Modified");
    check_str("307", neverc_http_status_text(307), "Temporary Redirect");
    check_str("400", neverc_http_status_text(400), "Bad Request");
    check_str("401", neverc_http_status_text(401), "Unauthorized");
    check_str("403", neverc_http_status_text(403), "Forbidden");
    check_str("404", neverc_http_status_text(404), "Not Found");
    check_str("405", neverc_http_status_text(405), "Method Not Allowed");
    check_str("408", neverc_http_status_text(408), "Request Timeout");
    check_str("409", neverc_http_status_text(409), "Conflict");
    check_str("416", neverc_http_status_text(416), "Range Not Satisfiable");
    check_str("422", neverc_http_status_text(422), "Unprocessable Entity");
    check_str("429", neverc_http_status_text(429), "Too Many Requests");
    check_str("500", neverc_http_status_text(500), "Internal Server Error");
    check_str("502", neverc_http_status_text(502), "Bad Gateway");
    check_str("503", neverc_http_status_text(503), "Service Unavailable");
    check_str("504", neverc_http_status_text(504), "Gateway Timeout");
    check_str("999", neverc_http_status_text(999), "Unknown");
}

/* ===== Query get ===== */

static void test_query_get(void) {
    printf("[query_get]\n");
    char buf[256];

    const char *v = neverc_http_query_get("name=John&age=30", "name", buf, sizeof(buf));
    check_str("query name", v, "John");

    v = neverc_http_query_get("name=John&age=30", "age", buf, sizeof(buf));
    check_str("query age", v, "30");

    v = neverc_http_query_get("name=John&age=30", "missing", buf, sizeof(buf));
    check_int("query missing", v == NULL, 1);

    v = neverc_http_query_get("key=", "key", buf, sizeof(buf));
    check_str("query empty val", v, "");

    v = neverc_http_query_get(NULL, "key", buf, sizeof(buf));
    check_int("query null", v == NULL, 1);

    v = neverc_http_query_get("a=1&b=2&c=3", "c", buf, sizeof(buf));
    check_str("query last", v, "3");

    v = neverc_http_query_get("x=hello%20world", "x", buf, sizeof(buf));
    check_str("query encoded", v, "hello%20world");

    v = neverc_http_query_get("a=1&a=2", "a", buf, sizeof(buf));
    check_str("query dup first", v, "1");

    v = neverc_http_query_get("long=aaaaaaaaaaaa&short=b", "short", buf, sizeof(buf));
    check_str("query after long", v, "b");
}

/* Go Request.ParseForm / FormValue on application/x-www-form-urlencoded. */
static void test_form_value(void) {
    printf("[form_value]\n");
    char buf[256];
    const char *v;

    v = neverc_http_form_value("name=John", 9, "name", buf, sizeof(buf));
    check_str("form name", v, "John");

    v = neverc_http_form_value("name=Hello+World!", 17, "name", buf, sizeof(buf));
    check_str("form plus", v, "Hello World!");

    v = neverc_http_form_value("x=hello%20world", 15, "x", buf, sizeof(buf));
    check_str("form pct", v, "hello world");

    v = neverc_http_form_value("na%6De=John", 11, "name", buf, sizeof(buf));
    check_str("form encoded key", v, "John");

    v = neverc_http_form_value("name", 4, "name", buf, sizeof(buf));
    check_str("form key without equals", v, "");

    v = neverc_http_form_value("name=John%00admin", 17, "name", buf, sizeof(buf));
    check_int("form encoded NUL rejected", v == NULL, 1);

    v = neverc_http_form_value("name=%%", 7, "name", buf, sizeof(buf));
    check_int("form malformed percent rejected", v == NULL, 1);

    v = neverc_http_form_value("a=1;b=2&name=ok", 15, "name", buf, sizeof(buf));
    check_str("form skips semicolon pair", v, "ok");

    v = neverc_http_form_value("a=1;b=2&name=ok", 15, "a", buf, sizeof(buf));
    check_int("form semicolon pair not a value", v == NULL, 1);

    v = neverc_http_form_value("name=John%00x&name=Jane", 23, "name", buf,
                               sizeof(buf));
    check_str("form later pair after NUL", v, "Jane");

    v = neverc_http_form_value(NULL, 0, "k", buf, sizeof(buf));
    check_int("form null body", v == NULL, 1);
}

/* ===== Mux ===== */

static void dummy_handler(neverc_http_request_t *req,
                           neverc_http_response_writer_t *w) {
    (void)req; (void)w;
}

static void test_mux(void) {
    printf("[mux]\n");
    neverc_http_mux_t *mux = neverc_http_new_mux();
    check_not_null("new_mux", mux);

    neverc_http_mux_handle(mux, "/", dummy_handler);
    neverc_http_mux_handle(mux, "/api/", dummy_handler);
    neverc_http_mux_handle(mux, "/health", dummy_handler);
    neverc_http_mux_handle(mux, "/api/v1/", dummy_handler);
    neverc_http_mux_handle(mux, "/api/v2/", dummy_handler);
    check_int("mux ok", 1, 1);

    neverc_http_mux_free(mux);

    /* Null mux safety */
    neverc_http_mux_handle(NULL, "/test", dummy_handler);
    neverc_http_mux_free(NULL);
    check_int("null mux safe", 1, 1);
}

static void mux_ctx_nop(neverc_http_request_t *req,
                        neverc_http_response_writer_t *w, void *ctx) {
    (void)req;
    (void)w;
    (void)ctx;
}

/* Go {$} registration is host-independent and must fail closed when the
 * wildcard is not at the end of the path. */
static void test_mux_dollar_pattern(void) {
    printf("[mux_dollar]\n");
    neverc_http_mux_t *mux = neverc_http_new_mux();
    check_not_null("mux", mux);
    if (!mux) return;
    check_int("{$} at end accepted",
              neverc_http_mux_handle_context(mux, "/posts/{$}",
                                             mux_ctx_nop, NULL), 0);
    check_int("/{$} accepted",
              neverc_http_mux_handle_context(mux, "/{$}",
                                             mux_ctx_nop, NULL), 0);
    check_int("{$} mid-pattern rejected",
              neverc_http_mux_handle_context(mux, "/{$}/x",
                                             mux_ctx_nop, NULL), -1);
    check_int("empty {} rejected",
              neverc_http_mux_handle_context(mux, "/users/{}",
                                             mux_ctx_nop, NULL), -1);
    check_int("empty {...} rejected",
              neverc_http_mux_handle_context(mux, "/files/{...}",
                                             mux_ctx_nop, NULL), -1);
    neverc_http_mux_free(mux);
}

/* ===== Response writer null safety ===== */

static void test_writer_null_safety(void) {
    printf("[writer_null]\n");
    neverc_http_set_status(NULL, 200);
    neverc_http_set_header(NULL, "X", "Y");
    check_int("write null", neverc_http_write(NULL, "x", 1), 0);
    check_int("write_string null", neverc_http_write_string(NULL, "x"), 0);
    check_int("writef null", neverc_http_writef(NULL, "%s", "x"), 0);
    tests_passed++; tests_run++;
}

/* ===== HTTP response free null safety ===== */

static void test_response_free_null(void) {
    printf("[response_free_null]\n");
    neverc_http_response_free(NULL);
    tests_passed++; tests_run++;
}

/* ===== HTTP Server (fork-based test) ===== */

#ifndef _WIN32

static char static_test_dir[4096];

static void hello_handler(neverc_http_request_t *req,
                           neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_set_header(w, "X-Custom", "test");
    neverc_http_write_string(w, "Hello, World!");
}

static void echo_handler(neverc_http_request_t *req,
                          neverc_http_response_writer_t *w) {
    neverc_http_set_header(w, "Content-Type", "application/json");
    neverc_http_writef(w, "{\"method\":\"%s\",\"path\":\"%s\"}", req->method, req->path);
}

static void post_handler(neverc_http_request_t *req,
                          neverc_http_response_writer_t *w) {
    neverc_http_set_status(w, 201);
    neverc_http_set_header(w, "Content-Type", "text/plain");
    if (req->body && req->body_len > 0) {
        neverc_http_writef(w, "received %zu bytes", req->body_len);
    } else {
        neverc_http_write_string(w, "no body");
    }
}

static void method_handler(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w) {
    neverc_http_writef(w, "method=%s", req->method);
}

static void delete_handler(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_set_status(w, 204);
}

static void query_handler(neverc_http_request_t *req,
                            neverc_http_response_writer_t *w) {
    char buf[256];
    const char *name = neverc_http_query_get(req->query, "name", buf, sizeof(buf));
    if (name)
        neverc_http_writef(w, "hello %s", name);
    else
        neverc_http_write_string(w, "no name");
}

static void header_handler(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w) {
    const char *ua = neverc_http_request_header(req, "User-Agent");
    if (ua)
        neverc_http_writef(w, "ua=%s", ua);
    else
        neverc_http_write_string(w, "no ua");
}

static void cl_mismatch_handler(neverc_http_request_t *req,
                                neverc_http_response_writer_t *w) {
    (void)req;
    (void)neverc_http_set_content_length(w, 3U);
    neverc_http_write_string(w, "abcdef");
}

static void cl_short_handler(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w) {
    (void)req;
    (void)neverc_http_set_content_length(w, 10U);
    neverc_http_write_string(w, "ab");
}

static void redirect_relative_handler(neverc_http_request_t *req,
                                      neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "../target?via=relative", 302);
}

static void redirect_target_handler(neverc_http_request_t *req,
                                    neverc_http_response_writer_t *w) {
    neverc_http_writef(w, "target:%s", req->query ? req->query : "");
}

static void redirect_query_handler(neverc_http_request_t *req,
                                   neverc_http_response_writer_t *w) {
    if (req->query && strcmp(req->query, "done=1") == 0)
        neverc_http_write_string(w, "query-target");
    else
        neverc_http_redirect(w, "?done=1", 302);
}

static void redirect_scheme_relative_handler(
    neverc_http_request_t *req, neverc_http_response_writer_t *w) {
    const char *host = neverc_http_request_header(req, "Host");
    char location[320];
    int length = snprintf(location, sizeof(location), "//%s/hello",
                          host ? host : "invalid");
    if (length < 0 || (size_t)length >= sizeof(location)) {
        neverc_http_error(w, "host too long", 500);
        return;
    }
    neverc_http_redirect(w, location, 302);
}

static void redirect_fragment_handler(neverc_http_request_t *req,
                                      neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "/hello#section", 302);
}

static void redirect_303_handler(neverc_http_request_t *req,
                                 neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "/method", 303);
}

static void redirect_305_handler(neverc_http_request_t *req,
                                 neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "/hello", 305);
}

static void redirect_post_first_handler(neverc_http_request_t *req,
                                        neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "/redirect/post-second", 302);
}

static void redirect_post_second_handler(neverc_http_request_t *req,
                                         neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "/redirect/inspect", 307);
}

static void redirect_307_handler(neverc_http_request_t *req,
                                 neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "/redirect/inspect", 307);
}

static void redirect_put_301_handler(neverc_http_request_t *req,
                                     neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "/redirect/inspect", 301);
}

static void redirect_inspect_handler(neverc_http_request_t *req,
                                     neverc_http_response_writer_t *w) {
    neverc_http_writef(w, "%s:%zu", req->method, req->body_len);
}

static void redirect_blank_location_handler(neverc_http_request_t *req,
                                           neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_set_status(w, 302);
    neverc_http_set_header(w, "Location", " ");
    neverc_http_write_string(w, "blank-location");
}

static int get_free_port(void) {
    const char *err = NULL;
    neverc_tcp_listener_t *probe = neverc_tcp_listen("127.0.0.1:0", &err);
    if (!probe) return -1;
    neverc_tcp_addr_t pa;
    neverc_tcp_listener_addr(probe, &pa);
    int port = pa.port;
    neverc_tcp_listener_close(probe);
    return port;
}

static int do_http_request_ex(int port, const char *request,
                               char *response, size_t resplen,
                               int timeout_ms) {
    const char *err = NULL;
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    if (!conn) return -1;

    neverc_tcp_set_timeout(conn, timeout_ms);
    neverc_tcp_write(conn, request, strlen(request));

    int total = 0;
    while (total < (int)resplen - 1) {
        int n = neverc_tcp_read(conn, response + total, resplen - 1 - (size_t)total);
        if (n <= 0) break;
        total += n;
    }
    response[total] = '\0';
    neverc_tcp_close(conn);
    return total;
}

static int do_http_request(int port, const char *request,
                            char *response, size_t resplen) {
    return do_http_request_ex(port, request, response, resplen, 2000);
}

static pid_t start_test_server(int port) {
    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/hello", hello_handler);
        neverc_http_mux_handle(mux, "/echo", echo_handler);
        neverc_http_mux_handle(mux, "/post", post_handler);
        neverc_http_mux_handle(mux, "/query", query_handler);
        neverc_http_mux_handle(mux, "/header", header_handler);
        neverc_http_mux_handle(mux, "/cl-mismatch", cl_mismatch_handler);
        neverc_http_mux_handle(mux, "/cl-short", cl_short_handler);
        neverc_http_mux_handle(mux, "/method", method_handler);
        neverc_http_mux_handle(mux, "/delete", delete_handler);
        neverc_http_mux_handle(mux, "/a/b/start", redirect_relative_handler);
        neverc_http_mux_handle(mux, "/a/target", redirect_target_handler);
        neverc_http_mux_handle(mux, "/redirect/query", redirect_query_handler);
        neverc_http_mux_handle(mux, "/redirect/scheme",
                               redirect_scheme_relative_handler);
        neverc_http_mux_handle(mux, "/redirect/fragment",
                               redirect_fragment_handler);
        neverc_http_mux_handle(mux, "/redirect/303", redirect_303_handler);
        neverc_http_mux_handle(mux, "/redirect/305", redirect_305_handler);
        neverc_http_mux_handle(mux, "/redirect/post-first",
                               redirect_post_first_handler);
        neverc_http_mux_handle(mux, "/redirect/post-second",
                               redirect_post_second_handler);
        neverc_http_mux_handle(mux, "/redirect/307", redirect_307_handler);
        neverc_http_mux_handle(mux, "/redirect/put-301",
                               redirect_put_301_handler);
        neverc_http_mux_handle(mux, "/redirect/inspect",
                               redirect_inspect_handler);
        neverc_http_mux_handle(mux, "/redirect/blank",
                               redirect_blank_location_handler);
        if (static_test_dir[0] != '\0')
            neverc_http_serve_dir(mux, "/static/", static_test_dir);

        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    /* Wait for server to be ready by probing the port */
    for (int i = 0; i < 100; i++) {
        usleep(30000);
        const char *err = NULL;
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (c) { neverc_tcp_close(c); break; }
    }
    return pid;
}

static void stop_test_server(pid_t pid) {
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

static void test_http_server(void) {
    printf("[http_server]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char buf[4096];

    /* Test 1: GET /hello */
    {
        int n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("hello resp", n > 0, 1);
        check_int("hello 200", strstr(buf, "200 OK") != NULL, 1);
        check_int("hello body", strstr(buf, "Hello, World!") != NULL, 1);
        check_int("hello custom", strstr(buf, "X-Custom: test") != NULL, 1);
    }

    /* Test 2: GET /echo */
    {
        int n = do_http_request(port,
            "GET /echo HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("echo resp", n > 0, 1);
        check_int("echo 200", strstr(buf, "200 OK") != NULL, 1);
        check_int("echo json", strstr(buf, "\"method\":\"GET\"") != NULL, 1);
        check_int("echo path", strstr(buf, "\"path\":\"/echo\"") != NULL, 1);
    }

    /* Test 3: GET /nonexistent -> 404 */
    {
        int n = do_http_request(port,
            "GET /nonexistent HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("404 resp", n > 0, 1);
        check_int("404 status", strstr(buf, "404") != NULL, 1);
    }

    /* Test 4: POST with body */
    {
        int n = do_http_request(port,
            "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Length: 11\r\nConnection: close\r\n\r\nhello world",
            buf, sizeof(buf));
        check_int("post resp", n > 0, 1);
        check_int("post 201", strstr(buf, "201") != NULL, 1);
        check_int("post body", strstr(buf, "received 11 bytes") != NULL, 1);
    }

    /* Test 5: Query parameters */
    {
        int n = do_http_request(port,
            "GET /query?name=NeverC HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("query resp", n > 0, 1);
        check_int("query body", strstr(buf, "hello NeverC") != NULL, 1);
    }

    /* Test 6: Request headers */
    {
        int n = do_http_request(port,
            "GET /header HTTP/1.1\r\nHost: localhost\r\n"
            "User-Agent: NeverC/1.0\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("header resp", n > 0, 1);
        check_int("header ua", strstr(buf, "ua=NeverC/1.0") != NULL, 1);
    }

    /* Test 7: Concurrent requests */
    {
        int ok_count = 0;
        for (int i = 0; i < 3; i++) {
            pid_t cpid = fork();
            if (cpid == 0) {
                char cbuf[4096];
                int cn = do_http_request(port,
                    "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
                    cbuf, sizeof(cbuf));
                _exit(cn > 0 && strstr(cbuf, "Hello, World!") ? 0 : 1);
            }
        }
        for (int i = 0; i < 3; i++) {
            int st;
            wait(&st);
            if (WIFEXITED(st) && WEXITSTATUS(st) == 0) ok_count++;
        }
        check_int("concurrent 3/3", ok_count, 3);
    }

    /* Test 8: Keep-alive (multiple requests on same connection) */
    {
        const char *err = NULL;
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        check_not_null("keepalive conn", conn);
        if (conn) {
            neverc_tcp_set_timeout(conn, 2000);

            const char *req1 = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
            neverc_tcp_write(conn, req1, strlen(req1));
            usleep(100000);
            char kbuf[4096];
            int kn = neverc_tcp_read(conn, kbuf, sizeof(kbuf) - 1);
            kbuf[kn > 0 ? kn : 0] = '\0';
            check_int("ka req1 ok", strstr(kbuf, "Hello, World!") != NULL, 1);

            const char *req2 = "GET /echo HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
            neverc_tcp_write(conn, req2, strlen(req2));
            usleep(100000);
            kn = neverc_tcp_read(conn, kbuf, sizeof(kbuf) - 1);
            kbuf[kn > 0 ? kn : 0] = '\0';
            check_int("ka req2 ok", strstr(kbuf, "\"method\":\"GET\"") != NULL, 1);

            neverc_tcp_close(conn);
        }
    }

    stop_test_server(server_pid);
}

/* ===== HTTP Client API test ===== */

static void test_http_client(void) {
    printf("[http_client]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    /* Test GET */
    {
        char url[64];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/hello", port);
        neverc_http_response_t *resp = neverc_http_get(url);
        check_not_null("client get resp", resp);
        if (resp) {
            check_int("client get status", resp->status_code, 200);
            check_int("client get body",
                       resp->body && strstr(resp->body, "Hello, World!") != NULL, 1);
            neverc_http_response_free(resp);
        }
    }

    /* Fragments must be stripped, not rejected: they are never sent. */
    {
        char url[80];
        snprintf(url, sizeof(url),
                 "http://127.0.0.1:%d/hello#ignored", port);
        neverc_http_response_t *resp = neverc_http_get(url);
        check_not_null("client fragment resp", resp);
        if (resp) {
            check_int("client fragment status",
                      resp->error == NULL && resp->status_code == 200, 1);
            check_int("client fragment body",
                      resp->body && strstr(resp->body, "Hello, World!") != NULL,
                      1);
            neverc_http_response_free(resp);
        }
    }

    /* Test POST */
    {
        char url[64];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/post", port);
        neverc_http_response_t *resp = neverc_http_post(url, "text/plain",
                                                         "test data", 9);
        check_not_null("client post resp", resp);
        if (resp) {
            check_int("client post status", resp->status_code, 201);
            check_int("client post body",
                       resp->body && strstr(resp->body, "received 9 bytes") != NULL, 1);
            neverc_http_response_free(resp);
        }
    }

    /* Test 404 */
    {
        char url[64];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/nope", port);
        neverc_http_response_t *resp = neverc_http_get(url);
        check_not_null("client 404 resp", resp);
        if (resp) {
            check_int("client 404 status", resp->status_code, 404);
            neverc_http_response_free(resp);
        }
    }

    /* Test error: null url */
    {
        neverc_http_response_t *resp = neverc_http_get(NULL);
        check_not_null("client null url resp", resp);
        if (resp) {
            check_not_null("client null url error", resp->error);
            neverc_http_response_free(resp);
        }
    }

    /* HTTPS must never be sent as cleartext to a TCP endpoint. */
    {
        char url[64];
        snprintf(url, sizeof(url), "https://127.0.0.1:%d/hello", port);
        neverc_http_response_t *resp = neverc_http_get(url);
        check_not_null("client https fail-closed resp", resp);
        if (resp) {
            check_not_null("client https fail-closed error", resp->error);
            check_int("client https no cleartext status", resp->status_code, 0);
            neverc_http_response_free(resp);
        }
    }

    /* Request construction must not read past its formatting scratch space,
     * and caller-controlled fields must not inject extra header lines. */
    {
        char url[64];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/post", port);

        char *long_type = (char *)malloc(5001);
        check_not_null("client long content type allocation", long_type);
        if (long_type) {
            memset(long_type, 'a', 5000);
            long_type[5000] = '\0';
            neverc_http_response_t *resp =
                neverc_http_post(url, long_type, "x", 1);
            check_not_null("client long content type resp", resp);
            if (resp) {
                check_int("client long content type status",
                          resp->status_code, 201);
                neverc_http_response_free(resp);
            }
            free(long_type);
        }

        neverc_http_response_t *resp =
            neverc_http_do("GET\r\nX-Injected: yes", url, NULL, NULL, 0);
        check_not_null("client injected method resp", resp);
        if (resp) {
            check_not_null("client injected method error", resp->error);
            check_int("client injected method no status",
                      resp->status_code, 0);
            neverc_http_response_free(resp);
        }

        resp = neverc_http_post(url, "text/plain\r\nX-Injected: yes",
                                 "x", 1);
        check_not_null("client injected content type resp", resp);
        if (resp) {
            check_not_null("client injected content type error", resp->error);
            check_int("client injected content type no status",
                      resp->status_code, 0);
            neverc_http_response_free(resp);
        }

        resp = neverc_http_post(url, "application/octet-stream",
                                 "x", SIZE_MAX);
        check_not_null("client oversized body resp", resp);
        if (resp) {
            check_not_null("client oversized body error", resp->error);
            check_int("client oversized body no status",
                      resp->status_code, 0);
            neverc_http_response_free(resp);
        }
    }

    stop_test_server(server_pid);
}

static void test_http_client_redirects(void) {
    printf("[http_client_redirects]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }
    pid_t server_pid = start_test_server(port);
    char url[128];

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/a/b/start", port);
    neverc_http_response_t *resp = neverc_http_get(url);
    check_int("relative redirect succeeds",
              resp && !resp->error && resp->status_code == 200, 1);
    check_int("relative redirect resolves dot segments",
              resp && resp->body &&
                  strcmp(resp->body, "target:via=relative") == 0, 1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/query", port);
    resp = neverc_http_get(url);
    check_int("query-only redirect succeeds",
              resp && !resp->error && resp->status_code == 200, 1);
    check_int("query-only redirect keeps path",
              resp && resp->body && strcmp(resp->body, "query-target") == 0, 1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/scheme", port);
    resp = neverc_http_get(url);
    check_int("scheme-relative redirect succeeds",
              resp && !resp->error && resp->status_code == 200, 1);
    check_int("scheme-relative redirect changes authority",
              resp && resp->body && strstr(resp->body, "Hello, World!") != NULL,
              1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/fragment", port);
    resp = neverc_http_get(url);
    check_int("redirect fragment is not sent",
              resp && !resp->error && resp->status_code == 200, 1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/303", port);
    resp = neverc_http_do("DELETE", url, NULL, NULL, 0);
    check_int("303 redirect succeeds",
              resp && !resp->error && resp->status_code == 200, 1);
    check_int("303 changes DELETE to GET",
              resp && resp->body && strcmp(resp->body, "method=GET") == 0, 1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/305", port);
    resp = neverc_http_get(url);
    check_int("305 is not followed",
              resp && !resp->error && resp->status_code == 305, 1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/post-first", port);
    resp = neverc_http_post(url, "text/plain", "secret", 6);
    check_int("rewritten GET survives later 307",
              resp && !resp->error && resp->body &&
                  strcmp(resp->body, "GET:0") == 0, 1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/307", port);
    resp = neverc_http_post(url, "text/plain", "data", 4);
    check_int("307 preserves method and body",
              resp && !resp->error && resp->body &&
                  strcmp(resp->body, "POST:4") == 0, 1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/put-301", port);
    resp = neverc_http_do("PUT", url, "text/plain", "secret", 6);
    check_int("301 rewrites PUT to GET without body",
              resp && !resp->error && resp->body &&
                  strcmp(resp->body, "GET:0") == 0, 1);
    neverc_http_response_free(resp);

    snprintf(url, sizeof(url),
             "http://127.0.0.1:%d/redirect/blank", port);
    resp = neverc_http_get(url);
    check_int("whitespace Location is not followed",
              resp && !resp->error && resp->status_code == 302, 1);
    neverc_http_response_free(resp);

    stop_test_server(server_pid);
}

/* ===== Stress test: concurrent requests ===== */

static void test_stress(void) {
    printf("[stress]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    #define STRESS_N 20
    int ok_count = 0;
    for (int i = 0; i < STRESS_N; i++) {
        pid_t cpid = fork();
        if (cpid == 0) {
            char cbuf[4096];
            int cn = do_http_request(port,
                "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
                cbuf, sizeof(cbuf));
            _exit(cn > 0 && strstr(cbuf, "Hello, World!") ? 0 : 1);
        }
    }
    for (int i = 0; i < STRESS_N; i++) {
        int st;
        wait(&st);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) ok_count++;
    }
    check_int("stress 20/20", ok_count, STRESS_N);
    #undef STRESS_N

    stop_test_server(server_pid);
}

/* ===== Large POST body ===== */

static void test_large_post(void) {
    printf("[large_post]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    #define LARGE_BODY_SZ 65536
    char *body = (char *)malloc(LARGE_BODY_SZ);
    for (int i = 0; i < LARGE_BODY_SZ; i++) body[i] = 'A' + (i % 26);

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/post", port);
    neverc_http_response_t *resp = neverc_http_post(url, "application/octet-stream",
                                                     body, LARGE_BODY_SZ);
    check_not_null("large post resp", resp);
    if (resp) {
        check_int("large post status", resp->status_code, 201);
        char expected[64];
        snprintf(expected, sizeof(expected), "received %d bytes", LARGE_BODY_SZ);
        check_int("large post body",
                   resp->body && strstr(resp->body, expected) != NULL, 1);
        neverc_http_response_free(resp);
    }
    free(body);

    stop_test_server(server_pid);
    #undef LARGE_BODY_SZ
}

/* ===== HTTP methods (HEAD/PUT/DELETE/PATCH) ===== */

static void test_http_methods(void) {
    printf("[http_methods]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char buf[4096];

    /* HEAD request */
    {
        int n = do_http_request(port,
            "HEAD /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("head resp", n > 0, 1);
        check_int("head 200", strstr(buf, "200 OK") != NULL, 1);
    }

    /* PUT request */
    {
        int n = do_http_request(port,
            "PUT /method HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n"
            "Content-Length: 4\r\n\r\ndata",
            buf, sizeof(buf));
        check_int("put resp", n > 0, 1);
        check_int("put 200", strstr(buf, "200 OK") != NULL, 1);
        check_int("put method", strstr(buf, "method=PUT") != NULL, 1);
    }

    /* DELETE request */
    {
        int n = do_http_request(port,
            "DELETE /delete HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("delete resp", n > 0, 1);
        check_int("delete 204", strstr(buf, "204") != NULL, 1);
    }

    /* PATCH request */
    {
        int n = do_http_request(port,
            "PATCH /method HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n"
            "Content-Length: 5\r\n\r\npatch",
            buf, sizeof(buf));
        check_int("patch resp", n > 0, 1);
        check_int("patch method", strstr(buf, "method=PATCH") != NULL, 1);
    }

    stop_test_server(server_pid);
}

static void test_large_static_head(void) {
    printf("[large_static_head]\n");
    static const size_t file_size = (1024U * 1024U) + 17U;
    char directory_template[] = "/tmp/neverc_http_static_XXXXXX";
    char *directory = mkdtemp(directory_template);
    if (!directory) {
        printf("  SKIP: cannot create static test directory\n");
        return;
    }
    snprintf(static_test_dir, sizeof(static_test_dir), "%s", directory);
    char path[4096];
    int path_length = snprintf(
        path, sizeof(path), "%s/large.bin", static_test_dir);
    if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
        static_test_dir[0] = '\0';
        rmdir(directory);
        return;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        printf("  SKIP: cannot create static test file\n");
        static_test_dir[0] = '\0';
        rmdir(directory);
        return;
    }
    uint8_t block[4096];
    memset(block, 0x5a, sizeof(block));
    size_t remaining = file_size;
    while (remaining > 0) {
        size_t amount = remaining < sizeof(block) ? remaining : sizeof(block);
        if (fwrite(block, 1U, amount, file) != amount) break;
        remaining -= amount;
    }
    int close_result = fclose(file);
    int file_ok = remaining == 0 && close_result == 0;
    check_int("static test file", file_ok, 1);
    if (!file_ok) {
        unlink(path);
        static_test_dir[0] = '\0';
        rmdir(directory);
        return;
    }

    int port = get_free_port();
    if (port < 0) {
        printf("  SKIP: cannot find free port\n");
        unlink(path);
        static_test_dir[0] = '\0';
        rmdir(directory);
        return;
    }
    pid_t server_pid = start_test_server(port);
    const char *error = NULL;
    char address[64];
    snprintf(address, sizeof(address), "127.0.0.1:%d", port);
    neverc_tcp_conn_t *connection = neverc_tcp_dial(address, &error);
    check_not_null("static HEAD connection", connection);
    if (connection) {
        neverc_tcp_set_timeout(connection, 5000);
        static const char head_request[] =
            "HEAD /static/large.bin HTTP/1.1\r\n"
            "Host: localhost\r\n\r\n";
        check_int("static HEAD write",
                  neverc_tcp_write(connection, head_request,
                                   sizeof(head_request) - 1U) > 0,
                  1);

        char headers[4096];
        size_t header_length = 0;
        headers[0] = '\0';
        while (header_length + 1U < sizeof(headers)) {
            int received = neverc_tcp_read(
                connection, headers + header_length, 1U);
            if (received <= 0) break;
            header_length += (size_t)received;
            headers[header_length] = '\0';
            if (strstr(headers, "\r\n\r\n")) break;
        }
        check_int("static HEAD status",
                  strncmp(headers, "HTTP/1.1 200", 12U) == 0, 1);
        check_int("static HEAD length",
                  strstr(headers, "Content-Length: 1048593") != NULL, 1);

        static const char get_request[] =
            "GET /hello HTTP/1.1\r\n"
            "Host: localhost\r\nConnection: close\r\n\r\n";
        check_int("request after static HEAD",
                  neverc_tcp_write(connection, get_request,
                                   sizeof(get_request) - 1U) > 0,
                  1);
        char response[4096];
        int total = 0;
        while (total < (int)sizeof(response) - 1) {
            int received = neverc_tcp_read(
                connection, response + total,
                sizeof(response) - 1U - (size_t)total);
            if (received <= 0) break;
            total += received;
        }
        response[total] = '\0';
        check_int("no HEAD body before next response",
                  strncmp(response, "HTTP/1.1 200", 12U) == 0, 1);
        check_int("response after static HEAD",
                  strstr(response, "Hello, World!") != NULL, 1);
        neverc_tcp_close(connection);
    }
    stop_test_server(server_pid);
    unlink(path);
    static_test_dir[0] = '\0';
    rmdir(directory);
}

/* ===== Keep-alive connections ===== */

static void test_keep_alive(void) {
    printf("[keep_alive]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    const char *err = NULL;
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    check_not_null("ka connect", conn);

    if (conn) {
        neverc_tcp_set_timeout(conn, 5000);

        /* First request on keep-alive connection */
        const char *req1 =
            "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
        neverc_tcp_write(conn, req1, strlen(req1));

        char buf[4096];
        int total = 0;
        int found_end = 0;
        while (total < (int)sizeof(buf) - 1 && !found_end) {
            int n = neverc_tcp_read(conn, buf + total, sizeof(buf) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            buf[total] = '\0';
            if (strstr(buf, "Hello, World!")) found_end = 1;
        }
        check_int("ka first 200", strstr(buf, "200 OK") != NULL, 1);
        check_int("ka first body", strstr(buf, "Hello, World!") != NULL, 1);

        /* Second request on same connection */
        const char *req2 =
            "GET /echo HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        neverc_tcp_write(conn, req2, strlen(req2));

        char buf2[4096];
        total = 0;
        while (total < (int)sizeof(buf2) - 1) {
            int n = neverc_tcp_read(conn, buf2 + total, sizeof(buf2) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
        }
        buf2[total] = '\0';
        check_int("ka second 200", strstr(buf2, "200 OK") != NULL, 1);

        neverc_tcp_close(conn);
    }

    stop_test_server(server_pid);
}

/* ===== Concurrent stress test with threads ===== */

typedef struct {
    int port;
    int success;
} thread_test_ctx_t;

static void *stress_thread_func(void *arg) {
    thread_test_ctx_t *ctx = (thread_test_ctx_t *)arg;
    char buf[4096];
    for (int i = 0; i < 5; i++) {
        int n = do_http_request(ctx->port,
            "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        if (n > 0 && strstr(buf, "Hello, World!")) {
            ctx->success++;
        }
    }
    return NULL;
}

static void test_thread_stress(void) {
    printf("[thread_stress]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    #define NUM_THREADS 8
    #define REQS_PER_THREAD 5
    pthread_t threads[NUM_THREADS];
    thread_test_ctx_t ctxs[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        ctxs[i].port = port;
        ctxs[i].success = 0;
        pthread_create(&threads[i], NULL, stress_thread_func, &ctxs[i]);
    }

    int total_success = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_success += ctxs[i].success;
    }

    int expected = NUM_THREADS * REQS_PER_THREAD;
    check_int("thread stress all ok", total_success, expected);
    #undef NUM_THREADS
    #undef REQS_PER_THREAD

    stop_test_server(server_pid);
}

/* ===== 404 Not Found ===== */

static void test_404(void) {
    printf("[test_404]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char buf[4096];

    int n = do_http_request(port,
        "GET /nonexistent HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("404 resp", n > 0, 1);
    check_int("404 status", strstr(buf, "404") != NULL, 1);
    check_int("404 body", strstr(buf, "not found") != NULL, 1);

    stop_test_server(server_pid);
}

/* ===== HTTP client new methods ===== */

static void test_http_client_methods(void) {
    printf("[http_client_methods]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char url[64];

    /* HEAD */
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/hello", port);
    neverc_http_response_t *resp = neverc_http_head(url);
    check_not_null("head resp", resp);
    if (resp) {
        check_int("head status", resp->status_code, 200);
        neverc_http_response_free(resp);
    }

    /* PUT */
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/method", port);
    resp = neverc_http_put(url, "text/plain", "data", 4);
    check_not_null("put resp", resp);
    if (resp) {
        check_int("put status", resp->status_code, 200);
        check_int("put body", resp->body && strstr(resp->body, "method=PUT") != NULL, 1);
        neverc_http_response_free(resp);
    }

    /* DELETE */
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/delete", port);
    resp = neverc_http_delete(url);
    check_not_null("delete resp", resp);
    if (resp) {
        check_int("delete status", resp->status_code, 204);
        neverc_http_response_free(resp);
    }

    /* PATCH */
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/method", port);
    resp = neverc_http_patch(url, "text/plain", "patch", 5);
    check_not_null("patch resp", resp);
    if (resp) {
        check_int("patch status", resp->status_code, 200);
        check_int("patch body", resp->body && strstr(resp->body, "method=PATCH") != NULL, 1);
        neverc_http_response_free(resp);
    }

    /* Generic DO */
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/method", port);
    resp = neverc_http_do("OPTIONS", url, NULL, NULL, 0);
    check_not_null("do resp", resp);
    if (resp) {
        check_int("do status", resp->status_code, 200);
        neverc_http_response_free(resp);
    }

    /* Error cases */
    resp = neverc_http_get(NULL);
    check_not_null("null url", resp);
    if (resp) {
        check_int("null url error", resp->error != NULL, 1);
        neverc_http_response_free(resp);
    }

    resp = neverc_http_do(NULL, "http://localhost:1", NULL, NULL, 0);
    check_not_null("null method", resp);
    if (resp) {
        check_int("null method error", resp->error != NULL, 1);
        neverc_http_response_free(resp);
    }

    stop_test_server(server_pid);
}

/* ===== Edge case: malformed HTTP request ===== */

static void test_malformed_request(void) {
    printf("[malformed_request]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char buf[4096];

    /* Completely garbage data */
    {
        int n = do_http_request_ex(port, "this is not http\r\n\r\n",
                                   buf, sizeof(buf), 500);
        check_int("garbage handled", n >= 0, 1);
    }

    /* Empty request */
    {
        const char *err = NULL;
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        if (conn) {
            neverc_tcp_set_timeout(conn, 500);
            neverc_tcp_write(conn, "\r\n\r\n", 4);
            int n = neverc_tcp_read(conn, buf, sizeof(buf) - 1);
            buf[n > 0 ? n : 0] = '\0';
            neverc_tcp_close(conn);
        }
        tests_passed++; tests_run++;
    }

    /* Incomplete request line */
    {
        int n = do_http_request_ex(port, "GET /hello\r\n\r\n",
                                   buf, sizeof(buf), 500);
        check_int("incomplete line handled", n >= 0, 1);
    }

    /* RFC 9112: HTTP/1.1 without Host is 400. */
    {
        int n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("no host resp", n > 0, 1);
        check_int("no host 400", strstr(buf, "400 Bad Request") != NULL, 1);
    }

    /* Very long URL */
    {
        char long_req[8192];
        int off = snprintf(long_req, sizeof(long_req), "GET /");
        for (int i = 0; i < 4000; i++)
            long_req[off++] = 'a';
        off += snprintf(long_req + off, sizeof(long_req) - (size_t)off,
                        " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        int n = do_http_request(port, long_req, buf, sizeof(buf));
        check_int("long url handled", n >= 0, 1);
    }

    /* HTTP/1.0 (should close after response) */
    {
        int n = do_http_request(port,
            "GET /hello HTTP/1.0\r\nHost: localhost\r\n\r\n",
            buf, sizeof(buf));
        check_int("http10 resp", n > 0, 1);
        check_int("http10 body", strstr(buf, "Hello, World!") != NULL, 1);
    }

    /* RFC 9112: invalid Host field values are 400. */
    {
        int n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: example.com/foo\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host path 400", strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: user@localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host userinfo 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: localhost:99999\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host port 400", strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: [::1\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host ipv6 400", strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: localhost extra\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host space 400", strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: [::1]\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host ipv6 ok", strstr(buf, "Hello, World!") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: [::1,evil]\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host ipv6 comma 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: example.com<script>\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host html 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: example.com\">\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("host quote 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
    }

    /* Duplicate Host / Content-Length, CL+TE, and obs-fold are 400. */
    {
        int n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: localhost\r\nHost: evil\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("dup host 400", strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Length: 0\r\nContent-Length: 0\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("dup cl 400", strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Length: 0\r\nTransfer-Encoding: chunked\r\n"
            "Connection: close\r\n\r\n0\r\n\r\n",
            buf, sizeof(buf));
        check_int("cl+te 400", strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello HTTP/1.1\r\nHost: localhost\r\n"
            "X-Foo: bar\r\n baz\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("header fold 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            "Content-Length: 99999999999999999999\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("cl overflow 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
    }

    /* asterisk-form is only valid for OPTIONS. */
    {
        int n = do_http_request(port,
            "GET * HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("get star 400", strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "OPTIONS * HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("options star not 400",
                  strstr(buf, "400 Bad Request") == NULL, 1);
    }

    /* RFC 9112: absolute-form is for proxies. Origin-form "//host" is the
     * leftover scheme-relative form; CONNECT is HTTP/2-rejected here too. */
    {
        int n = do_http_request(port,
            "GET //evil.example/ HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("scheme-relative target 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /%2f/evil.example/ HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("percent-encoded scheme-relative 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /%00//evil.example/ HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("nul-encoded scheme-relative 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /foo%2fbar HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("encoded slash in later segment not 400",
                  strstr(buf, "400 Bad Request") == NULL, 1);
        n = do_http_request(port,
            "GET /foo//bar HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("empty path segment not 400",
                  strstr(buf, "400 Bad Request") == NULL, 1);
        n = do_http_request(port,
            "GET /hello\\x HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("backslash target 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET http://evil.example/hello HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("absolute-form 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "CONNECT /hello HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("origin-form CONNECT 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "CONNECT example.com:443 HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("authority-form CONNECT 400",
                  strstr(buf, "400 Bad Request") != NULL, 1);
        n = do_http_request(port,
            "GET /hello?next=https://example.com HTTP/1.1\r\n"
            "Host: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("query double-slash ok",
                  strstr(buf, "Hello, World!") != NULL, 1);
    }

    /* Chunk trailers must not be promoted into request headers. */
    {
        int n = do_http_request(port,
            "POST /header HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
            "0\r\nUser-Agent: injected\r\n\r\n",
            buf, sizeof(buf));
        check_int("trailer not header", strstr(buf, "no ua") != NULL, 1);
        check_int("trailer not injected",
                  strstr(buf, "ua=injected") == NULL, 1);
        (void)n;
    }

    /* Chunk-size lines and trailers share the streaming 8KiB / 128 caps. */
    {
        int n = do_http_request(port,
            "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
            "1;foo\r\nx\r\n0\r\n\r\n",
            buf, sizeof(buf));
        check_int("short chunk-ext ok",
                  n > 0 && strstr(buf, "201") != NULL, 1);
        check_int("short chunk-ext body",
                  n > 0 && strstr(buf, "received 1") != NULL, 1);

        char long_req[16384];
        int off = snprintf(long_req, sizeof(long_req),
            "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
            "1;");
        for (int i = 0; i < 8200; i++)
            long_req[off++] = 'a';
        off += snprintf(long_req + off, sizeof(long_req) - (size_t)off,
                        "\r\nx\r\n0\r\n\r\n");
        n = do_http_request(port, long_req, buf, sizeof(buf));
        check_int("long chunk-ext 400",
                  n > 0 && strstr(buf, "400 Bad Request") != NULL, 1);

        off = snprintf(long_req, sizeof(long_req),
            "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
            "0\r\n");
        for (int i = 0; i < 200; i++)
            off += snprintf(long_req + off, sizeof(long_req) - (size_t)off,
                            "X-%03d: a\r\n", i);
        snprintf(long_req + off, sizeof(long_req) - (size_t)off, "\r\n");
        n = do_http_request(port, long_req, buf, sizeof(buf));
        check_int("too many trailers 400",
                  n > 0 && strstr(buf, "400 Bad Request") != NULL, 1);
    }

    /* Content-Length smaller than the buffered body must not emit extra
     * bytes that a downstream parser would treat as the next response. */
    {
        int n = do_http_request(port,
            "GET /cl-mismatch HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("cl mismatch no leak",
                  n >= 0 && strstr(buf, "abcdef") == NULL, 1);
    }

    /* Content-Length larger than the buffered body must not advertise a
     * length a keep-alive peer would read into the next request. */
    {
        int n = do_http_request(port,
            "GET /cl-short HTTP/1.1\r\nHost: localhost\r\n"
            "Connection: keep-alive\r\n\r\n",
            buf, sizeof(buf));
        check_int("cl short no advertised length",
                  n >= 0 && strstr(buf, "Content-Length: 10") == NULL, 1);
    }

    stop_test_server(server_pid);
}

/* ===== Edge case: POST with no Content-Length ===== */

static void test_post_no_content_length(void) {
    printf("[post_no_content_length]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char buf[4096];

    int n = do_http_request(port,
        "POST /post HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("post no cl resp", n > 0, 1);
    check_int("post no cl rejected", strstr(buf, "400 Bad Request") != NULL, 1);

    stop_test_server(server_pid);
}

static void test_post_no_content_length_keepalive(void) {
    printf("[post_no_content_length_keepalive]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char buf[4096];

    int n = do_http_request(port,
        "POST /post HTTP/1.1\r\nHost: localhost\r\n\r\n",
        buf, sizeof(buf));
    check_int("post keepalive no cl resp", n > 0, 1);
    check_int("post keepalive no cl rejected",
              strstr(buf, "400 Bad Request") != NULL, 1);

    stop_test_server(server_pid);
}

static void test_post_http10_keepalive_no_cl(void) {
    printf("[post_http10_keepalive_no_cl]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char buf[4096];

    int n = do_http_request(port,
        "POST /post HTTP/1.0\r\nHost: localhost\r\n"
        "Connection: keep-alive\r\n\r\n",
        buf, sizeof(buf));
    check_int("http10 ka post resp", n > 0, 1);
    check_int("http10 ka post rejected",
              strstr(buf, "400 Bad Request") != NULL, 1);

    n = do_http_request(port,
        "POST /post HTTP/1.0\r\nHost: localhost\r\n"
        "Connection: close\r\n\r\nhello",
        buf, sizeof(buf));
    check_int("http10 close post resp", n > 0, 1);
    check_int("http10 close post rejected",
              strstr(buf, "400 Bad Request") != NULL, 1);
    check_int("http10 close post not silent empty body",
              strstr(buf, "no body") == NULL &&
              strstr(buf, "201") == NULL, 1);

    n = do_http_request(port,
        "POST /post HTTP/1.0\r\nHost: localhost\r\n\r\nhello",
        buf, sizeof(buf));
    check_int("http10 default close post resp", n > 0, 1);
    check_int("http10 default close post rejected",
              strstr(buf, "400 Bad Request") != NULL, 1);
    check_int("http10 default close post not silent empty body",
              strstr(buf, "no body") == NULL &&
              strstr(buf, "201") == NULL, 1);

    stop_test_server(server_pid);
}

/* RFC 9110: 100 Continue must be sent after headers, before the body. */
static void test_expect_continue(void) {
    printf("[expect_continue]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    const char *err = NULL;
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    check_not_null("expect dial", conn);
    if (!conn) {
        stop_test_server(server_pid);
        return;
    }
    neverc_tcp_set_timeout(conn, 2000);

    const char *headers =
        "POST /post HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 4\r\n"
        "Expect: 100-continue\r\n"
        "\r\n";
    neverc_tcp_write(conn, headers, strlen(headers));

    char buf[4096];
    int total = 0;
    int saw_100 = 0;
    buf[0] = '\0';
    while (total < (int)sizeof(buf) - 1) {
        int n = neverc_tcp_read(conn, buf + total,
                                sizeof(buf) - 1 - (size_t)total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "HTTP/1.1 100 Continue") != NULL) {
            saw_100 = 1;
            break;
        }
        if (strstr(buf, "HTTP/1.1 201") != NULL)
            break;
    }
    check_int("expect 100 continue", saw_100, 1);

    if (saw_100) {
        neverc_tcp_write(conn, "data", 4);
        while (total < (int)sizeof(buf) - 1) {
            int n = neverc_tcp_read(conn, buf + total,
                                    sizeof(buf) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            buf[total] = '\0';
            if (strstr(buf, "HTTP/1.1 201") != NULL)
                break;
        }
    }
    check_int("expect 201 after body", strstr(buf, "HTTP/1.1 201") != NULL, 1);
    check_int("expect body received",
              strstr(buf, "received 4 bytes") != NULL, 1);

    /* Keep-alive: continue_sent must not leak onto the next request. */
    const char *get =
        "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    neverc_tcp_write(conn, get, strlen(get));
    while (total < (int)sizeof(buf) - 1) {
        int n = neverc_tcp_read(conn, buf + total,
                                sizeof(buf) - 1 - (size_t)total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "Hello, World!") != NULL)
            break;
    }
    check_int("expect keepalive get",
              strstr(buf, "Hello, World!") != NULL, 1);

    neverc_tcp_close(conn);
    stop_test_server(server_pid);
}

/* ===== Edge case: multiple headers with same name ===== */

static void test_duplicate_headers(void) {
    printf("[duplicate_headers]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);
    char buf[4096];

    int n = do_http_request(port,
        "GET /header HTTP/1.1\r\nHost: localhost\r\n"
        "User-Agent: first\r\nUser-Agent: second\r\n"
        "Connection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("dup headers resp", n > 0, 1);
    check_int("dup headers ua", strstr(buf, "ua=") != NULL, 1);

    stop_test_server(server_pid);
}

/* ===== Stress test: high concurrency with threads ===== */

typedef struct {
    int port;
    int total_requests;
    int success;
} heavy_stress_ctx_t;

static void *heavy_stress_thread(void *arg) {
    heavy_stress_ctx_t *ctx = (heavy_stress_ctx_t *)arg;
    char buf[4096];
    for (int i = 0; i < ctx->total_requests; i++) {
        int n = do_http_request(ctx->port,
            "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        if (n > 0 && strstr(buf, "Hello, World!"))
            ctx->success++;
    }
    return NULL;
}

static void test_heavy_stress(void) {
    printf("[heavy_stress]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    #define HEAVY_THREADS 16
    #define HEAVY_REQS    10
    pthread_t threads[HEAVY_THREADS];
    heavy_stress_ctx_t ctxs[HEAVY_THREADS];

    for (int i = 0; i < HEAVY_THREADS; i++) {
        ctxs[i].port = port;
        ctxs[i].total_requests = HEAVY_REQS;
        ctxs[i].success = 0;
        pthread_create(&threads[i], NULL, heavy_stress_thread, &ctxs[i]);
    }

    int total_ok = 0;
    for (int i = 0; i < HEAVY_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_ok += ctxs[i].success;
    }

    int expected = HEAVY_THREADS * HEAVY_REQS;
    check_int("heavy stress all ok", total_ok, expected);
    #undef HEAVY_THREADS
    #undef HEAVY_REQS

    stop_test_server(server_pid);
}

/* ===== Stress test: keep-alive pipelining ===== */

static void test_pipelining(void) {
    printf("[pipelining]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    const char *err = NULL;
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    check_not_null("pipeline conn", conn);

    if (conn) {
        neverc_tcp_set_timeout(conn, 5000);

        /* Send 5 pipelined requests at once */
        const char *reqs =
            "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n"
            "GET /echo HTTP/1.1\r\nHost: localhost\r\n\r\n"
            "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n"
            "GET /echo HTTP/1.1\r\nHost: localhost\r\n\r\n"
            "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        neverc_tcp_write(conn, reqs, strlen(reqs));

        char buf[16384];
        int total = 0;
        while (total < (int)sizeof(buf) - 1) {
            int n = neverc_tcp_read(conn, buf + total,
                                     sizeof(buf) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
        }
        buf[total] = '\0';

        /* Count "200 OK" occurrences */
        int ok_count = 0;
        const char *p = buf;
        while ((p = strstr(p, "200 OK")) != NULL) {
            ok_count++;
            p += 6;
        }
        check_int("pipeline 5 responses", ok_count >= 5, 1);

        neverc_tcp_close(conn);
    }

    stop_test_server(server_pid);
}

/* ===== Test: Connection close after error ===== */

static void test_connection_close_on_error(void) {
    printf("[conn_close_error]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    /* Send request with bad Content-Length (negative) */
    char buf[4096];
    int n = do_http_request(port,
        "POST /post HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Length: -1\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("bad cl handled", n >= 0, 1);

    /* Send request with Content-Length: 0 */
    n = do_http_request(port,
        "POST /post HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Length: 0\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("cl0 resp", n > 0, 1);
    check_int("cl0 body", strstr(buf, "no body") != NULL, 1);

    stop_test_server(server_pid);
}

/* ===== Test: Rapid connect/disconnect (connection churn) ===== */

static void test_connection_churn(void) {
    printf("[conn_churn]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_test_server(port);

    int success = 0;
    for (int i = 0; i < 50; i++) {
        const char *err = NULL;
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        if (conn) {
            neverc_tcp_close(conn);
            success++;
        }
    }
    check_int("churn 50 ok", success, 50);

    /* Verify server still works after churn */
    char buf[4096];
    int n = do_http_request(port,
        "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("post-churn ok", n > 0 && strstr(buf, "Hello, World!") != NULL, 1);

    stop_test_server(server_pid);
}

/* ===== Test: Chunked transfer encoding ===== */

static void chunked_handler(neverc_http_request_t *req,
                              neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_set_header(w, "Content-Type", "text/plain");
    neverc_http_enable_chunked(w);

    neverc_http_write_string(w, "chunk1");
    neverc_http_flush_chunk(w);

    neverc_http_write_string(w, "chunk2");
    neverc_http_flush_chunk(w);

    neverc_http_write_string(w, "chunk3");
    neverc_http_end_chunked(w);
}

static void host_trailer_handler(neverc_http_request_t *req,
                                 neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_enable_chunked(w);
    neverc_http_set_trailer(w, "Host", "evil.com");
    neverc_http_set_trailer(w, "Connection", "close");
    neverc_http_set_trailer(w, "X-Ok", "1");
    neverc_http_write_string(w, "ok");
    neverc_http_end_chunked(w);
}

static pid_t start_chunked_server(int port) {
    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/chunked", chunked_handler);
        neverc_http_mux_handle(mux, "/trailer-host", host_trailer_handler);

        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);
    return pid;
}

static void test_chunked_encoding(void) {
    printf("[chunked_encoding]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: cannot find free port\n"); return; }

    pid_t server_pid = start_chunked_server(port);
    char buf[4096];

    int n = do_http_request(port,
        "GET /chunked HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("chunked resp", n > 0, 1);
    check_int("chunked has TE", strstr(buf, "chunked") != NULL, 1);
    check_int("chunked has chunk1", strstr(buf, "chunk1") != NULL, 1);
    check_int("chunked has chunk2", strstr(buf, "chunk2") != NULL, 1);
    check_int("chunked has chunk3", strstr(buf, "chunk3") != NULL, 1);

    n = do_http_request(port,
        "GET /trailer-host HTTP/1.1\r\nHost: localhost\r\n"
        "Connection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("trailer-host resp", n > 0, 1);
    check_int("trailer-host keeps x-ok",
              n > 0 && strstr(buf, "X-Ok: 1") != NULL, 1);
    check_int("trailer-host lists X-Ok",
              n > 0 && strstr(buf, "Trailer: X-Ok") != NULL, 1);
    check_int("trailer-host drops Host",
              n > 0 && strstr(buf, "Host: evil.com") == NULL, 1);
    check_int("trailer-host omits Host from Trailer",
              n > 0 && strstr(buf, "Trailer: Host") == NULL, 1);

    stop_test_server(server_pid);
}

/* ===== Test: new config APIs ===== */

static void test_config_apis(void) {
    printf("[config_apis]\n");

    neverc_http_set_workers(4);
    neverc_http_set_max_requests(500);
    neverc_http_set_read_timeout(30000);
    neverc_http_set_max_connections(10000);
    neverc_http_set_max_header_size(65536);
    neverc_http_set_max_body_size(1048576);
    neverc_http_set_shutdown_timeout(3000);
    neverc_http_client_set_max_redirects(5);
    neverc_http_client_set_timeout(10000);
    tests_passed++; tests_run++;

    /* Reset to defaults for other tests */
    neverc_http_set_workers(0);
    neverc_http_set_max_requests(1000);
    neverc_http_set_read_timeout(60000);
    neverc_http_set_max_connections(0);
    neverc_http_set_max_header_size(0);
    neverc_http_set_max_body_size(0);
    neverc_http_set_shutdown_timeout(5000);
    neverc_http_client_set_max_redirects(10);
    neverc_http_client_set_timeout(30000);
}

/* ===== Test: client receives chunked response correctly ===== */

static void chunked_body_handler(neverc_http_request_t *req,
                                  neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_enable_chunked(w);
    neverc_http_set_header(w, "Content-Type", "text/plain");

    neverc_http_write_string(w, "Hello ");
    neverc_http_flush_chunk(w);
    neverc_http_write_string(w, "Chunked ");
    neverc_http_flush_chunk(w);
    neverc_http_write_string(w, "World");
    neverc_http_end_chunked(w);
}

static void large_chunked_body_handler(neverc_http_request_t *req,
                                        neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_enable_chunked(w);
    neverc_http_set_header(w, "Content-Type", "application/octet-stream");

    char chunk[4096];
    for (int block = 0; block < 32; block++) {
        for (size_t i = 0; i < sizeof(chunk); i++)
            chunk[i] = (char)('a' + ((block * (int)sizeof(chunk) +
                                      (int)i) % 26));
        neverc_http_write(w, chunk, sizeof(chunk));
        neverc_http_flush_chunk(w);
    }
    neverc_http_end_chunked(w);
}

static pid_t start_chunked_body_server(int port) {
    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/chunked_body", chunked_body_handler);
        neverc_http_mux_handle(mux, "/chunked_large",
                                large_chunked_body_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);
    return pid;
}

static void test_client_chunked_response(void) {
    printf("[client_chunked_response]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t srv = start_chunked_body_server(port);

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunked_body", port);
    neverc_http_response_t *resp = neverc_http_get(url);
    check_not_null("chunked client resp", resp);
    if (resp) {
        check_int("chunked client status", resp->status_code, 200);
        check_not_null("chunked client body", resp->body);
        if (resp->body) {
            check_int("chunked client body content",
                       strstr(resp->body, "Hello") != NULL &&
                       strstr(resp->body, "Chunked") != NULL &&
                       strstr(resp->body, "World") != NULL, 1);
        }
        neverc_http_response_free(resp);
    }

    snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunked_large", port);
    neverc_http_client_set_timeout(1000);
    resp = neverc_http_get(url);
    neverc_http_client_set_timeout(30000);
    check_not_null("large chunked client resp", resp);
    if (resp) {
        check_int("large chunked client status", resp->status_code, 200);
        check_int("large chunked client body length",
                  (int)resp->body_len, 32 * 4096);
        if (resp->body && resp->body_len == 32 * 4096) {
            check_int("large chunked first byte", resp->body[0], 'a');
            check_int("large chunked last byte",
                      resp->body[resp->body_len - 1],
                      'a' + (((32 * 4096) - 1) % 26));
        }
        neverc_http_response_free(resp);
    }

    stop_test_server(srv);
}

static int raw_write_all(neverc_tcp_conn_t *connection,
                         const void *data, size_t length) {
    const char *bytes = (const char *)data;
    size_t offset = 0;
    while (offset < length) {
        int written = neverc_tcp_write(connection, bytes + offset,
                                       length - offset);
        if (written <= 0) return -1;
        offset += (size_t)written;
    }
    return 0;
}

static void test_client_many_tiny_chunks(void) {
    printf("[client_many_tiny_chunks]\n");

    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    check_not_null("tiny chunk listener", listener);
    if (!listener) return;

    neverc_tcp_addr_t address;
    if (neverc_tcp_listener_addr(listener, &address) != 0) {
        neverc_tcp_listener_close(listener);
        check_int("tiny chunk listener address", 0, 1);
        return;
    }

    pid_t server = fork();
    if (server == 0) {
        neverc_tcp_conn_t *connection = neverc_tcp_accept(listener, &error);
        if (!connection) _exit(1);
        char request[1024];
        if (neverc_tcp_read(connection, request, sizeof(request)) <= 0)
            _exit(1);
        static const char headers[] =
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: close\r\n\r\n";
        char chunks[6000];
        for (size_t i = 0; i < 1000U; i++)
            memcpy(chunks + i * 6U, "1\r\na\r\n", 6U);
        if (raw_write_all(connection, headers, sizeof(headers) - 1U) != 0)
            _exit(1);
        for (int batch = 0; batch < 20; batch++) {
            if (raw_write_all(connection, chunks, sizeof(chunks)) != 0)
                _exit(1);
        }
        (void)raw_write_all(connection, "0\r\n\r\n", 5U);
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        _exit(0);
    }
    neverc_tcp_listener_close(listener);
    if (server < 0) {
        check_int("tiny chunk server fork", 0, 1);
        return;
    }

    neverc_http_client_config_t config =
        neverc_http_client_config_default();
    config.timeout_ms = 5000;
    config.max_idle_per_host = 0;
    config.max_response_header_size = 1024U;
    config.max_response_body_size = 20000U;
    neverc_http_client_t *client = neverc_http_client_new(&config);
    check_not_null("tiny chunk client", client);
    if (!client) {
        kill(server, SIGTERM);
        waitpid(server, NULL, 0);
        return;
    }
    char url[96];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/tiny", address.port);
    neverc_http_response_t *response =
        neverc_http_client_do(client, "GET", url, NULL, NULL, 0U);
    if (response && response->error)
        printf("  tiny chunk error: %s\n", response->error);
    check_int("tiny chunk response success",
              response != NULL && response->error == NULL, 1);
    check_int("tiny chunk decoded length",
              response != NULL && response->body_len == 20000U, 1);
    int body_ok = response && response->body;
    for (size_t i = 0; body_ok && i < response->body_len; i++)
        body_ok = response->body[i] == 'a';
    check_int("tiny chunk decoded body", body_ok, 1);

    neverc_http_response_free(response);
    neverc_http_client_free(client);
    int status = 0;
    waitpid(server, &status, 0);
    check_int("tiny chunk server status",
              WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);
}

static void test_client_304_chunked_not_pooled(void) {
    printf("[client_304_chunked_not_pooled]\n");

    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    check_not_null("304 chunked listener", listener);
    if (!listener) return;

    neverc_tcp_addr_t address;
    if (neverc_tcp_listener_addr(listener, &address) != 0) {
        neverc_tcp_listener_close(listener);
        check_int("304 chunked listener address", 0, 1);
        return;
    }

    pid_t server = fork();
    if (server == 0) {
        neverc_tcp_conn_t *first = neverc_tcp_accept(listener, &error);
        if (!first) _exit(1);
        char request[1024];
        if (neverc_tcp_read(first, request, sizeof(request)) <= 0)
            _exit(1);
        static const char not_modified[] =
            "HTTP/1.1 304 Not Modified\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\nhello\r\n0\r\n\r\n";
        if (raw_write_all(first, not_modified, sizeof(not_modified) - 1U) != 0)
            _exit(1);
        alarm(3);
        neverc_tcp_conn_t *second = neverc_tcp_accept(listener, &error);
        alarm(0);
        if (!second) _exit(2);
        if (neverc_tcp_read(second, request, sizeof(request)) <= 0)
            _exit(1);
        static const char ok[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 6\r\n"
            "Connection: close\r\n"
            "\r\n"
            "second";
        if (raw_write_all(second, ok, sizeof(ok) - 1U) != 0)
            _exit(1);
        neverc_tcp_close(first);
        neverc_tcp_close(second);
        neverc_tcp_listener_close(listener);
        _exit(0);
    }
    neverc_tcp_listener_close(listener);
    if (server < 0) {
        check_int("304 chunked server fork", 0, 1);
        return;
    }

    neverc_http_client_config_t config =
        neverc_http_client_config_default();
    config.timeout_ms = 4000;
    config.max_idle_per_host = 4;
    neverc_http_client_t *client = neverc_http_client_new(&config);
    check_not_null("304 chunked client", client);
    if (!client) {
        kill(server, SIGTERM);
        waitpid(server, NULL, 0);
        return;
    }
    char url[96];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/x", address.port);
    neverc_http_response_t *first =
        neverc_http_client_do(client, "GET", url, NULL, NULL, 0U);
    check_int("304+TE accepted",
              first != NULL && first->error == NULL &&
                  first->status_code == 304, 1);
    check_int("304+TE has no body",
              first == NULL || first->body_len == 0, 1);
    neverc_http_response_t *second =
        neverc_http_client_do(client, "GET", url, NULL, NULL, 0U);
    check_int("304+TE does not pool leftover",
              second != NULL && second->error == NULL &&
                  second->status_code == 200 &&
                  second->body_len == 6 &&
                  second->body && memcmp(second->body, "second", 6) == 0, 1);

    neverc_http_response_free(first);
    neverc_http_response_free(second);
    neverc_http_client_free(client);
    int status = 0;
    waitpid(server, &status, 0);
    check_int("304 chunked server status",
              WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);
}

static void test_client_rejects_bare_lf_status(void) {
    printf("[client_rejects_bare_lf_status]\n");

    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    check_not_null("lf status listener", listener);
    if (!listener) return;

    neverc_tcp_addr_t address;
    if (neverc_tcp_listener_addr(listener, &address) != 0) {
        neverc_tcp_listener_close(listener);
        check_int("lf status listener address", 0, 1);
        return;
    }

    pid_t server = fork();
    if (server == 0) {
        neverc_tcp_conn_t *connection = neverc_tcp_accept(listener, &error);
        if (!connection) _exit(1);
        char request[1024];
        if (neverc_tcp_read(connection, request, sizeof(request)) <= 0)
            _exit(1);
        static const char response[] =
            "HTTP/1.1 200 OK\nTransfer-Encoding: chunked\r\n"
            "Content-Length: 5\r\n\r\nHELLO";
        if (raw_write_all(connection, response, sizeof(response) - 1U) != 0)
            _exit(1);
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        _exit(0);
    }
    neverc_tcp_listener_close(listener);
    if (server < 0) {
        check_int("lf status server fork", 0, 1);
        return;
    }

    neverc_http_client_config_t config =
        neverc_http_client_config_default();
    config.timeout_ms = 5000;
    config.max_idle_per_host = 0;
    neverc_http_client_t *client = neverc_http_client_new(&config);
    check_not_null("lf status client", client);
    if (!client) {
        kill(server, SIGTERM);
        waitpid(server, NULL, 0);
        return;
    }
    char url[96];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/lf", address.port);
    neverc_http_response_t *response =
        neverc_http_client_do(client, "GET", url, NULL, NULL, 0U);
    check_int("lf status framing rejected",
              response != NULL && response->error != NULL, 1);
    check_int("lf status did not take Content-Length body",
              response == NULL || response->body == NULL ||
              response->body_len != 5U, 1);

    neverc_http_response_free(response);
    neverc_http_client_free(client);
    int status = 0;
    waitpid(server, &status, 0);
    check_int("lf status server status",
              WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);
}

/* ===== Test: concurrent connection limit ===== */

static void slow_handler(neverc_http_request_t *req,
                           neverc_http_response_writer_t *w) {
    (void)req;
    usleep(200000);
    neverc_http_write_string(w, "ok");
}

static pid_t start_limited_server(int port) {
    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_set_max_connections(3);
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/slow", slow_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);
    return pid;
}

static void *limited_client_thread(void *arg) {
    int port = *(int *)arg;
    char req_buf[256];
    snprintf(req_buf, sizeof(req_buf),
             "GET /slow HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    char resp[4096];
    do_http_request(port, req_buf, resp, sizeof(resp));
    return NULL;
}

static void test_connection_limit(void) {
    printf("[connection_limit]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t srv = start_limited_server(port);

    pthread_t threads[6];
    for (int i = 0; i < 6; i++)
        pthread_create(&threads[i], NULL, limited_client_thread, &port);
    for (int i = 0; i < 6; i++)
        pthread_join(threads[i], NULL);

    tests_passed++; tests_run++;

    stop_test_server(srv);

    neverc_http_set_max_connections(0);
}

static pid_t start_max_body_server(int port) {
    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_set_max_body_size(16);
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/post", post_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    for (int i = 0; i < 100; i++) {
        usleep(30000);
        const char *err = NULL;
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (c) { neverc_tcp_close(c); break; }
    }
    return pid;
}

static void test_max_body_size_buffered(void) {
    printf("[max_body_size_buffered]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t srv = start_max_body_server(port);
    char buf[4096];

    int n = do_http_request(port,
        "POST /post HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Length: 8\r\nConnection: close\r\n\r\n"
        "12345678",
        buf, sizeof(buf));
    check_int("small body accepted",
              n > 0 && strstr(buf, "201") != NULL, 1);
    check_int("small body echoed",
              n > 0 && strstr(buf, "received 8") != NULL, 1);

    n = do_http_request(port,
        "POST /post HTTP/1.1\r\nHost: localhost\r\n"
        "Content-Length: 32\r\nConnection: close\r\n\r\n"
        "0123456789abcdef0123456789abcdef",
        buf, sizeof(buf));
    check_int("oversize body is 413",
              n > 0 && strstr(buf, "413") != NULL, 1);
    check_int("oversize body not handled",
              n > 0 && strstr(buf, "received") == NULL, 1);

    n = do_http_request(port,
        "POST /post HTTP/1.1\r\nHost: localhost\r\n"
        "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
        "20\r\n0123456789abcdef0123456789abcdef\r\n0\r\n\r\n",
        buf, sizeof(buf));
    check_int("oversize chunked is 413",
              n > 0 && strstr(buf, "413") != NULL, 1);

    stop_test_server(srv);
    neverc_http_set_max_body_size(0);
}

/* ===== Test: rapid server start/stop cycle (regression test) ===== */

static void noop_handler(neverc_http_request_t *req,
                           neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_string(w, "ok");
}

static void test_server_lifecycle(void) {
    printf("[server_lifecycle]\n");

    for (int cycle = 0; cycle < 3; cycle++) {
        int port = get_free_port();
        if (port < 0) { printf("  SKIP: no free port\n"); return; }

        pid_t pid = fork();
        if (pid == 0) {
            neverc_http_mux_t *mux = neverc_http_new_mux();
            neverc_http_mux_handle(mux, "/ping", noop_handler);
            char addr[32];
            snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
            neverc_http_listen_and_serve(addr, mux);
            _exit(0);
        }
        usleep(300000);

        char buf[4096];
        int n = do_http_request(port,
            "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("lifecycle resp > 0", n > 0, 1);
        check_int("lifecycle 200", strstr(buf, "200 OK") != NULL, 1);

        stop_test_server(pid);
    }
}

/* ===== Test: convenience APIs ===== */

static void redirect_handler(neverc_http_request_t *req,
                               neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_redirect(w, "/hello", 302);
}

static void error_handler(neverc_http_request_t *req,
                            neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_error(w, "something went wrong", 500);
}

static void form_handler(neverc_http_request_t *req,
                           neverc_http_response_writer_t *w) {
    char buf[256];
    const char *name = neverc_http_form_value(req->body, req->body_len,
                                                "name", buf, sizeof(buf));
    if (name)
        neverc_http_writef(w, "hello %s", name);
    else
        neverc_http_write_string(w, "no name");
}

static void json_handler(neverc_http_request_t *req,
                           neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_json(w, "{\"status\":\"ok\",\"code\":200}");
}

static pid_t start_conv_server(int port) {
    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/redir", redirect_handler);
        neverc_http_mux_handle(mux, "/err", error_handler);
        neverc_http_mux_handle(mux, "/form", form_handler);
        neverc_http_mux_handle(mux, "/json", json_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);
    return pid;
}

static void test_convenience_apis(void) {
    printf("[convenience_apis]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t srv = start_conv_server(port);
    char buf[4096];

    /* Redirect */
    {
        neverc_http_client_set_max_redirects(0);
        int n = do_http_request(port,
            "GET /redir HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("redir resp", n > 0, 1);
        check_int("redir 302", strstr(buf, "302") != NULL, 1);
        check_int("redir loc", strstr(buf, "Location: /hello") != NULL, 1);
        neverc_http_client_set_max_redirects(10);
    }

    /* Error */
    {
        int n = do_http_request(port,
            "GET /err HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("error resp", n > 0, 1);
        check_int("error 500", strstr(buf, "500") != NULL, 1);
        check_int("error msg", strstr(buf, "something went wrong") != NULL, 1);
        check_int("error nosniff",
                   strstr(buf, "X-Content-Type-Options: nosniff") != NULL, 1);
    }

    /* Form data */
    {
        int n = do_http_request(port,
            "POST /form HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: 9\r\n"
            "Connection: close\r\n\r\n"
            "name=John",
            buf, sizeof(buf));
        check_int("form resp", n > 0, 1);
        check_int("form body", strstr(buf, "hello John") != NULL, 1);
    }

    /* Form data with URL encoding */
    {
        int n = do_http_request(port,
            "POST /form HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: 17\r\n"
            "Connection: close\r\n\r\n"
            "name=Hello+World!",
            buf, sizeof(buf));
        check_int("form enc resp", n > 0, 1);
        check_int("form enc body", strstr(buf, "hello Hello World!") != NULL, 1);
    }

    /* JSON response */
    {
        int n = do_http_request(port,
            "GET /json HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        check_int("json resp", n > 0, 1);
        check_int("json ct",
                   strstr(buf, "application/json") != NULL, 1);
        check_int("json body",
                   strstr(buf, "\"status\":\"ok\"") != NULL, 1);
    }

    /* Null safety */
    neverc_http_redirect(NULL, "/x", 302);
    neverc_http_error(NULL, "x", 500);
    check_int("conv null safe", 1, 1);
    tests_run++;
    tests_passed++;

    /* form_value edge cases */
    {
        char fb[64];
        const char *v;
        v = neverc_http_form_value(NULL, 0, "k", fb, sizeof(fb));
        check_int("form null body", v == NULL, 1);
        v = neverc_http_form_value("k=v", 3, NULL, fb, sizeof(fb));
        check_int("form null key", v == NULL, 1);
        v = neverc_http_form_value("a=1&b=2", 7, "b", fb, sizeof(fb));
        check_str("form val b", v, "2");
        v = neverc_http_form_value("x=hello%20world", 15, "x", fb, sizeof(fb));
        check_str("form pct", v, "hello world");
    }

    stop_test_server(srv);
}

/* ===== Test: throughput benchmark ===== */

typedef struct {
    int port;
    int requests;
    int success;
} bench_ctx_t;

static void *bench_thread(void *arg) {
    bench_ctx_t *ctx = (bench_ctx_t *)arg;
    char buf[4096];
    for (int i = 0; i < ctx->requests; i++) {
        int n = do_http_request(ctx->port,
            "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            buf, sizeof(buf));
        if (n > 0 && strstr(buf, "Hello, World!"))
            ctx->success++;
    }
    return NULL;
}

static void test_benchmark(void) {
    printf("[benchmark]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t srv = start_test_server(port);

    #define BENCH_THREADS 8
    #define BENCH_REQS    50
    pthread_t threads[BENCH_THREADS];
    bench_ctx_t ctxs[BENCH_THREADS];

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < BENCH_THREADS; i++) {
        ctxs[i].port = port;
        ctxs[i].requests = BENCH_REQS;
        ctxs[i].success = 0;
        pthread_create(&threads[i], NULL, bench_thread, &ctxs[i]);
    }

    int total_ok = 0;
    for (int i = 0; i < BENCH_THREADS; i++) {
        pthread_join(threads[i], NULL);
        total_ok += ctxs[i].success;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec)
                   + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    int expected = BENCH_THREADS * BENCH_REQS;
    double qps = (double)total_ok / elapsed;

    printf("    %d/%d requests in %.2f s = %.0f req/s\n",
           total_ok, expected, elapsed, qps);

    check_int("bench all ok", total_ok, expected);
    check_int("bench qps > 100", qps > 100.0, 1);

    #undef BENCH_THREADS
    #undef BENCH_REQS

    stop_test_server(srv);
}

/* ===== Test: connection pool reuse ===== */

static void pool_handler(neverc_http_request_t *req,
                           neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_string(w, "pooled");
}

static pid_t start_pool_server(int port) {
    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/pool", pool_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);
    return pid;
}

static void test_connection_pool(void) {
    printf("[connection_pool]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t srv = start_pool_server(port);

    neverc_http_client_set_pool(2);

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/pool", port);

    for (int i = 0; i < 5; i++) {
        neverc_http_response_t *resp = neverc_http_get(url);
        check_not_null("pool resp", resp);
        if (resp) {
            check_int("pool status", resp->status_code, 200);
            check_not_null("pool body", resp->body);
            if (resp->body)
                check_int("pool content", strstr(resp->body, "pooled") != NULL, 1);
            neverc_http_response_free(resp);
        }
    }

    neverc_http_client_set_pool(2);

    stop_test_server(srv);
}

/* ===== Test: Cookie API ===== */

static char long_cookie_value[4097];

static void cookie_handler(neverc_http_request_t *req,
                              neverc_http_response_writer_t *w) {
    neverc_http_cookie_t c = {0};
    c.name = "session";
    c.value = "abc123";
    c.path = "/";
    c.max_age = 3600;
    c.http_only = 1;
    c.same_site = 1; /* Lax */
    neverc_http_set_cookie(w, &c);

    neverc_http_cookie_t large = {0};
    large.name = "large";
    large.value = long_cookie_value;
    large.path = "/long-cookie-tail";
    neverc_http_set_cookie(w, &large);

    neverc_http_cookie_t injected = {0};
    injected.name = "inject";
    injected.value = "x; Domain=evil.com";
    neverc_http_set_cookie(w, &injected);
    injected.name = "ok";
    injected.value = "safe";
    injected.path = "/; Domain=evil.com";
    neverc_http_set_cookie(w, &injected);

    /* Check incoming cookie */
    char buf[128];
    const char *v = neverc_http_get_cookie(req, "test_cookie", buf, sizeof(buf));
    if (v)
        neverc_http_writef(w, "cookie=%s", v);
    else
        neverc_http_write_string(w, "no-cookie");
}

static void test_cookies(void) {
    printf("[cookies]\n");
    memset(long_cookie_value, 'x', sizeof(long_cookie_value) - 1U);
    long_cookie_value[sizeof(long_cookie_value) - 1U] = '\0';

    /* Test cookie parsing from request header */
    neverc_http_request_t req;
    memset(&req, 0, sizeof(req));
    char hdr_data[256];
    const char *hname = "Cookie";
    const char *hval = "session=abc123; theme=dark; lang=en";
    size_t pos = 0;
    memcpy(hdr_data + pos, hname, strlen(hname) + 1); pos += strlen(hname) + 1;
    memcpy(hdr_data + pos, hval, strlen(hval) + 1); pos += strlen(hval) + 1;
    req.raw_headers = hdr_data;
    req.nheaders = 1;

    char buf[128];
    const char *v = neverc_http_get_cookie(&req, "session", buf, sizeof(buf));
    check_not_null("get session cookie", v);
    if (v) check_int("session value", strcmp(v, "abc123") == 0, 1);

    v = neverc_http_get_cookie(&req, "theme", buf, sizeof(buf));
    check_not_null("get theme cookie", v);
    if (v) check_int("theme value", strcmp(v, "dark") == 0, 1);

    v = neverc_http_get_cookie(&req, "lang", buf, sizeof(buf));
    check_not_null("get lang cookie", v);
    if (v) check_int("lang value", strcmp(v, "en") == 0, 1);

    v = neverc_http_get_cookie(&req, "nonexistent", buf, sizeof(buf));
    check_int("nonexistent cookie", v == NULL, 1);

    /* RFC 6265: Cookie values may be DQUOTE-wrapped. */
    {
        neverc_http_request_t quoted;
        memset(&quoted, 0, sizeof(quoted));
        char quoted_hdr[256];
        const char *qhname = "Cookie";
        const char *qhval = "session=\"abc123\"; empty=\"\"; bare=xyz";
        size_t qpos = 0;
        memcpy(quoted_hdr + qpos, qhname, strlen(qhname) + 1);
        qpos += strlen(qhname) + 1;
        memcpy(quoted_hdr + qpos, qhval, strlen(qhval) + 1);
        quoted.raw_headers = quoted_hdr;
        quoted.nheaders = 1;

        v = neverc_http_get_cookie(&quoted, "session", buf, sizeof(buf));
        check_not_null("quoted session cookie", v);
        if (v) check_str("quoted session value", v, "abc123");

        v = neverc_http_get_cookie(&quoted, "empty", buf, sizeof(buf));
        check_not_null("quoted empty cookie", v);
        if (v) check_str("quoted empty value", v, "");

        v = neverc_http_get_cookie(&quoted, "bare", buf, sizeof(buf));
        check_not_null("bare cookie after quoted", v);
        if (v) check_str("bare cookie value", v, "xyz");
    }

    /* Go parseCookieValue allows SP; NeverC used to truncate at the first one. */
    {
        neverc_http_request_t spaced;
        memset(&spaced, 0, sizeof(spaced));
        char spaced_hdr[256];
        size_t spos = 0;
        const char *shname = "Cookie";
        const char *shval = "q=foo bar; r=\"foo bar\"";
        memcpy(spaced_hdr + spos, shname, strlen(shname) + 1);
        spos += strlen(shname) + 1;
        memcpy(spaced_hdr + spos, shval, strlen(shval) + 1);
        spaced.raw_headers = spaced_hdr;
        spaced.nheaders = 1;

        v = neverc_http_get_cookie(&spaced, "q", buf, sizeof(buf));
        check_not_null("spaced cookie", v);
        if (v) check_str("spaced cookie value", v, "foo bar");

        v = neverc_http_get_cookie(&spaced, "r", buf, sizeof(buf));
        check_not_null("quoted spaced cookie", v);
        if (v) check_str("quoted spaced cookie value", v, "foo bar");
    }

    /* Multiple Cookie headers must all be searched (RFC 6265 / Go r.Cookie). */
    {
        neverc_http_request_t multi;
        memset(&multi, 0, sizeof(multi));
        char multi_hdr[256];
        size_t mpos = 0;
        memcpy(multi_hdr + mpos, "Cookie", 7); mpos += 7;
        memcpy(multi_hdr + mpos, "first=a", 8); mpos += 8;
        memcpy(multi_hdr + mpos, "Cookie", 7); mpos += 7;
        memcpy(multi_hdr + mpos, "second=b", 9);
        multi.raw_headers = multi_hdr;
        multi.nheaders = 2;

        v = neverc_http_get_cookie(&multi, "second", buf, sizeof(buf));
        check_not_null("second cookie header", v);
        if (v) check_str("second cookie value", v, "b");

        v = neverc_http_get_cookie(&multi, "first", buf, sizeof(buf));
        check_not_null("first cookie header", v);
        if (v) check_str("first cookie value", v, "a");
    }

    /* Test Set-Cookie via real server */
    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t srv = fork();
    if (srv == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/cookie", cookie_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);

    /* Send request with Cookie header, verify Set-Cookie in response */
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    const char *err = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    check_not_null("cookie conn", conn);
    if (conn) {
        neverc_tcp_set_timeout(conn, 3000);
        const char *req_str =
            "GET /cookie HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Cookie: test_cookie=hello_world\r\n"
            "Connection: close\r\n\r\n";
        neverc_tcp_write(conn, req_str, strlen(req_str));

        char resp[16384];
        size_t total = 0;
        int n;
        while ((n = neverc_tcp_read(conn, resp + total,
                                      sizeof(resp) - total - 1)) > 0)
            total += (size_t)n;
        resp[total] = '\0';
        neverc_tcp_close(conn);

        check_int("has Set-Cookie header",
                     strstr(resp, "Set-Cookie: session=abc123") != NULL, 1);
        check_int("has HttpOnly",
                     strstr(resp, "HttpOnly") != NULL, 1);
        check_int("has SameSite=Lax",
                     strstr(resp, "SameSite=Lax") != NULL, 1);
        check_int("long Set-Cookie remains complete",
                     strstr(resp, "; Path=/long-cookie-tail") != NULL, 1);
        check_int("Set-Cookie rejects value attribute injection",
                     strstr(resp, "Domain=evil.com") == NULL, 1);
        check_int("Set-Cookie rejects path attribute injection",
                     strstr(resp, "inject=") == NULL, 1);
        check_int("body has cookie value",
                     strstr(resp, "cookie=hello_world") != NULL, 1);
    }

    stop_test_server(srv);
}

/* ===== Multipart form parsing ===== */

static void test_multipart_parsing(void) {
    printf("[multipart_parsing]\n");

    const char *ct = "multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW";
    const char *body =
        "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        "Content-Disposition: form-data; name=\"username\"\r\n"
        "\r\n"
        "john_doe\r\n"
        "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        "Content-Disposition: form-data; name=\"avatar\"; filename=\"photo.png\"\r\n"
        "Content-Type: image/png\r\n"
        "\r\n"
        "PNG_DATA_HERE\r\n"
        "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n"
        "Content-Disposition: form-data; name=\"bio\"\r\n"
        "\r\n"
        "Hello\nWorld\r\n"
        "------WebKitFormBoundary7MA4YWxkTrZu0gW--\r\n";

    neverc_http_multipart_t *mp = neverc_http_multipart_parse(ct, body, strlen(body));
    check_not_null("multipart parse", mp);

    if (mp) {
        check_int("multipart count", neverc_http_multipart_count(mp), 3);

        const neverc_http_multipart_part_t *p0 = neverc_http_multipart_get(mp, 0);
        check_not_null("part 0", p0);
        if (p0) {
            check_str("part 0 name", p0->name, "username");
            check_int("part 0 not file", p0->filename == NULL, 1);
            check_int("part 0 data len", (int)p0->data_len, 8);
        }

        const neverc_http_multipart_part_t *p1 = neverc_http_multipart_get(mp, 1);
        check_not_null("part 1", p1);
        if (p1) {
            check_str("part 1 name", p1->name, "avatar");
            check_str("part 1 filename", p1->filename, "photo.png");
            check_str("part 1 content_type", p1->content_type, "image/png");
            check_int("part 1 data len", (int)p1->data_len, 13);
        }

        const neverc_http_multipart_part_t *bio = neverc_http_multipart_field(mp, "bio");
        check_not_null("field bio", bio);
        if (bio) {
            check_str("bio name", bio->name, "bio");
            check_int("bio data len", (int)bio->data_len, 11);
        }

        check_int("field missing", neverc_http_multipart_field(mp, "missing") == NULL, 1);
        check_int("get out of bounds", neverc_http_multipart_get(mp, 99) == NULL, 1);

        neverc_http_multipart_free(mp);
    }

    check_int("null content_type", neverc_http_multipart_parse(NULL, body, strlen(body)) == NULL, 1);
    check_int("null body", neverc_http_multipart_parse(ct, NULL, 0) == NULL, 1);
    check_int("no boundary", neverc_http_multipart_parse("text/plain", body, strlen(body)) == NULL, 1);

    const char *short_ct = "multipart/form-data; boundary=b";
    const char *boundary_like_data =
        "--b\r\n"
        "Content-Disposition: form-data; name=\"value\"\r\n"
        "\r\n"
        "alpha\r\n--bX\r\nomega\r\n"
        "--b--\r\n";
    mp = neverc_http_multipart_parse(
        short_ct, boundary_like_data, strlen(boundary_like_data));
    check_not_null("boundary prefix in payload is not a delimiter", mp);
    if (mp) {
        const neverc_http_multipart_part_t *part =
            neverc_http_multipart_get(mp, 0);
        const char *expected = "alpha\r\n--bX\r\nomega";
        check_int("boundary-prefix body preserved",
                  part && part->data_len == strlen(expected) &&
                      memcmp(part->data, expected, strlen(expected)) == 0,
                  1);
        neverc_http_multipart_free(mp);
    }

    const char *filename_only =
        "--b\r\n"
        "Content-Disposition: form-data; filename=\"file.txt\"\r\n"
        "\r\n"
        "x\r\n"
        "--b--\r\n";
    mp = neverc_http_multipart_parse(
        short_ct, filename_only, strlen(filename_only));
    check_not_null("filename-only part parses", mp);
    if (mp) {
        const neverc_http_multipart_part_t *part =
            neverc_http_multipart_get(mp, 0);
        check_int("filename is not mistaken for field name",
                  part && part->name == NULL, 1);
        check_str("filename-only value", part ? part->filename : NULL,
                  "file.txt");
        neverc_http_multipart_free(mp);
    }

    const char *unterminated = "--b\r\nx\r\n--";
    check_int("truncated delimiter rejected",
              neverc_http_multipart_parse(
                  short_ct, unterminated, strlen(unterminated)) == NULL,
              1);

    const char *no_parameters =
        "--b\r\n"
        "Content-Disposition: form-data\r\n"
        "\r\n"
        "x\r\n"
        "--b--\r\n";
    size_t no_parameters_len = strlen(no_parameters);
    char *bounded_body = (char *)malloc(no_parameters_len);
    if (bounded_body) memcpy(bounded_body, no_parameters, no_parameters_len);
    mp = bounded_body ? neverc_http_multipart_parse(
                            short_ct, bounded_body, no_parameters_len)
                      : NULL;
    check_not_null("non-NUL-terminated multipart body", mp);
    neverc_http_multipart_free(mp);
    free(bounded_body);

    {
        const char *smuggle_ct =
            "multipart/form-data; notboundary=SMUGGLE; boundary=REAL";
        const char *real_body =
            "--REAL\r\n"
            "Content-Disposition: form-data; name=\"ok\"\r\n"
            "\r\n"
            "yes\r\n"
            "--REAL--\r\n";
        neverc_http_multipart_t *parsed = neverc_http_multipart_parse(
            smuggle_ct, real_body, strlen(real_body));
        const neverc_http_multipart_part_t *part =
            parsed ? neverc_http_multipart_field(parsed, "ok") : NULL;
        check_not_null("boundary is not stolen by notboundary", parsed);
        check_int("real boundary field",
                  part && part->data_len == 3 &&
                      memcmp(part->data, "yes", 3) == 0,
                  1);
        neverc_http_multipart_free(parsed);
    }
}

/* ===== SSE (Server-Sent Events) ===== */

static void sse_handler(neverc_http_request_t *req,
                         neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_sse_begin(w);
    neverc_http_sse_event(w, NULL, "hello", NULL);
    neverc_http_sse_event(w, "update", "data1", "1");
    neverc_http_sse_event(w, "chat", "line1\nline2", "2");
    neverc_http_sse_retry(w, 3000);
    neverc_http_sse_end(w);
}

static void test_sse(void) {
    printf("[sse]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t srv = fork();
    if (srv == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/events", sse_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);

    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    const char *err = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    check_not_null("sse conn", conn);

    if (conn) {
        neverc_tcp_set_timeout(conn, 3000);
        const char *req_str =
            "GET /events HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Accept: text/event-stream\r\n"
            "Connection: close\r\n\r\n";
        neverc_tcp_write(conn, req_str, strlen(req_str));

        char resp[8192];
        size_t total = 0;
        int n;
        while ((n = neverc_tcp_read(conn, resp + total,
                                      sizeof(resp) - total - 1)) > 0)
            total += (size_t)n;
        resp[total] = '\0';
        neverc_tcp_close(conn);

        check_int("sse content-type", strstr(resp, "text/event-stream") != NULL, 1);
        check_int("sse default event", strstr(resp, "data: hello\n") != NULL, 1);
        check_int("sse named event", strstr(resp, "event: update\n") != NULL, 1);
        check_int("sse event data", strstr(resp, "data: data1\n") != NULL, 1);
        check_int("sse event id", strstr(resp, "id: 1\n") != NULL, 1);
        check_int("sse multiline data1", strstr(resp, "data: line1\n") != NULL, 1);
        check_int("sse multiline data2", strstr(resp, "data: line2\n") != NULL, 1);
        check_int("sse retry", strstr(resp, "retry: 3000\n") != NULL, 1);
    }

    stop_test_server(srv);
}

#endif /* _WIN32 */

/* ===== DetectContentType (like Go http.DetectContentType) ===== */

static void test_detect_content_type(void) {
    printf("[detect_content_type]\n");

    /* HTML */
    check_str("html doctype",
              neverc_http_detect_content_type("<!DOCTYPE HTML><html>", 21),
              "text/html; charset=utf-8");
    check_str("html tag",
              neverc_http_detect_content_type("<HTML>", 6),
              "text/html; charset=utf-8");
    check_str("html head",
              neverc_http_detect_content_type("<HEAD>", 6),
              "text/html; charset=utf-8");
    check_str("html with ws",
              neverc_http_detect_content_type("  \t<HTML>", 9),
              "text/html; charset=utf-8");
    check_str("html comment",
              neverc_http_detect_content_type("<!-- comment -->", 16),
              "text/html; charset=utf-8");

    /* XML */
    check_str("xml",
              neverc_http_detect_content_type("<?xml version=\"1.0\"?>", 21),
              "text/xml; charset=utf-8");

    /* PDF */
    check_str("pdf",
              neverc_http_detect_content_type("%PDF-1.4", 8),
              "application/pdf");

    /* Images */
    {
        unsigned char png[] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00};
        check_str("png", neverc_http_detect_content_type(png, 8), "image/png");
    }
    {
        unsigned char jpg[] = {0xFF,0xD8,0xFF,0xE0};
        check_str("jpeg", neverc_http_detect_content_type(jpg, 4), "image/jpeg");
    }
    check_str("gif87",
              neverc_http_detect_content_type("GIF87a", 6), "image/gif");
    check_str("gif89",
              neverc_http_detect_content_type("GIF89a", 6), "image/gif");
    check_str("bmp",
              neverc_http_detect_content_type("BM\x00\x00", 4), "image/bmp");

    /* Audio/Video */
    check_str("mp3/id3",
              neverc_http_detect_content_type("ID3\x03\x00", 5), "audio/mpeg");
    {
        unsigned char ogg[] = {0x4F,0x67,0x67,0x53,0x00};
        check_str("ogg", neverc_http_detect_content_type(ogg, 5), "application/ogg");
    }
    {
        unsigned char webm[] = {0x1A,0x45,0xDF,0xA3};
        check_str("webm", neverc_http_detect_content_type(webm, 4), "video/webm");
    }

    /* Archives */
    {
        unsigned char gz[] = {0x1F,0x8B,0x08};
        check_str("gzip", neverc_http_detect_content_type(gz, 3), "application/x-gzip");
    }
    {
        unsigned char zip[] = {0x50,0x4B,0x03,0x04};
        check_str("zip", neverc_http_detect_content_type(zip, 4), "application/zip");
    }

    /* Fonts */
    check_str("woff",
              neverc_http_detect_content_type("wOFF\x00", 5), "font/woff");
    check_str("woff2",
              neverc_http_detect_content_type("wOF2\x00", 5), "font/woff2");
    check_str("otf",
              neverc_http_detect_content_type("OTTO\x00", 5), "font/otf");

    /* WebAssembly */
    {
        unsigned char wasm[] = {0x00,0x61,0x73,0x6D};
        check_str("wasm", neverc_http_detect_content_type(wasm, 4), "application/wasm");
    }

    /* Plain text */
    check_str("plain text",
              neverc_http_detect_content_type("Hello, world!", 13),
              "text/plain; charset=utf-8");

    /* Binary data → octet-stream */
    {
        unsigned char bin[] = {0x01,0x02,0x03,0x04};
        check_str("binary", neverc_http_detect_content_type(bin, 4),
                  "application/octet-stream");
    }

    /* Edge cases */
    check_str("null data",
              neverc_http_detect_content_type(NULL, 0),
              "application/octet-stream");
    check_str("empty data",
              neverc_http_detect_content_type("", 0),
              "application/octet-stream");
}

/* ===== CanonicalHeaderKey ===== */

static void test_canonical_header_key(void) {
    printf("[canonical_header_key]\n");
    char buf[128];

    neverc_http_canonical_header_key("accept-encoding", buf, sizeof(buf));
    check_str("accept-encoding", buf, "Accept-Encoding");

    neverc_http_canonical_header_key("content-type", buf, sizeof(buf));
    check_str("content-type", buf, "Content-Type");

    neverc_http_canonical_header_key("x-custom-header", buf, sizeof(buf));
    check_str("x-custom-header", buf, "X-Custom-Header");

    neverc_http_canonical_header_key("HOST", buf, sizeof(buf));
    check_str("HOST -> Host", buf, "Host");

    neverc_http_canonical_header_key("x", buf, sizeof(buf));
    check_str("single char", buf, "X");

    neverc_http_canonical_header_key("", buf, sizeof(buf));
    check_str("empty key", buf, "");

    neverc_http_canonical_header_key("already-Canonical", buf, sizeof(buf));
    check_str("mixed case", buf, "Already-Canonical");
}

/* ===== NotFound handler ===== */

static void test_not_found_handler(void) {
    printf("[not_found_handler]\n");

    neverc_http_response_writer_t *w = neverc_http_memory_writer_new();
    check_not_null("mem writer", w);
    if (!w) return;

    neverc_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.path = "/nonexistent";

    neverc_http_not_found(&req, w);

    char *data = NULL;
    size_t data_len = 0;
    int status = neverc_http_memory_writer_result(w, &data, &data_len);

    check_int("not_found status", status, 404);
    check_int("not_found body", data && strstr(data, "404 page not found") != NULL, 1);

    free(data);
    neverc_http_memory_writer_free(w);
}

static void test_redirect_html_escape(void) {
    printf("[redirect_html_escape]\n");

    neverc_http_response_writer_t *w = neverc_http_memory_writer_new();
    check_not_null("redirect writer", w);
    if (!w) return;

    neverc_http_redirect(w, "\"><svg/onload=1>", 302);
    char *data = NULL;
    size_t data_len = 0;
    int status = neverc_http_memory_writer_result(w, &data, &data_len);
    check_int("redirect status", status, 302);
    check_int("redirect body present", data != NULL, 1);
    if (data) {
        check_int("redirect escapes quote", strstr(data, "&quot;") != NULL, 1);
        check_int("redirect escapes lt", strstr(data, "&lt;svg") != NULL, 1);
        check_int("redirect no raw breakout",
                  strstr(data, "\"><svg") == NULL, 1);
    }
    free(data);
    neverc_http_memory_writer_free(w);
}

#ifndef _WIN32
/* ===== ServeFile test ===== */

static const char *g_serve_file_path;

static void serve_file_live_handler(neverc_http_request_t *req,
                                    neverc_http_response_writer_t *w) {
    neverc_http_serve_file(w, req, g_serve_file_path);
}

static void test_serve_file(void) {
    printf("[serve_file]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    /* Create a temp file to serve */
    const char *tmppath = "/tmp/neverc_serve_test.txt";
    FILE *f = fopen(tmppath, "w");
    if (!f) { printf("  SKIP: cannot create temp file\n"); return; }
    fprintf(f, "Hello from served file!");
    fclose(f);
    g_serve_file_path = tmppath;

    /* Memory writer test for ServeFile */
    neverc_http_response_writer_t *w = neverc_http_memory_writer_new();
    check_not_null("serve_file mem writer", w);
    if (w) {
        neverc_http_request_t req;
        memset(&req, 0, sizeof(req));
        req.method = "GET";
        req.path = "/test.txt";

        neverc_http_serve_file(w, &req, tmppath);

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);

        check_int("serve_file status", status, 200);
        check_int("serve_file body",
                   data && strstr(data, "Hello from served file!") != NULL, 1);

        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* Range: bytes=0-4 → 206 and the first five bytes. */
    w = neverc_http_memory_writer_new();
    if (w) {
        neverc_http_request_t req;
        memset(&req, 0, sizeof(req));
        req.method = "GET";
        req.path = "/test.txt";
        char range_hdr[64];
        size_t rpos = 0;
        memcpy(range_hdr + rpos, "Range", 6); rpos += 6;
        memcpy(range_hdr + rpos, "bytes=0-4", 10);
        req.raw_headers = range_hdr;
        req.nheaders = 1;

        neverc_http_serve_file(w, &req, tmppath);

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);
        check_int("serve_file range status", status, 206);
        check_int("serve_file range length", (int)data_len, 5);
        check_int("serve_file range body",
                  data && memcmp(data, "Hello", 5) == 0, 1);
        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* Unsatisfiable Range → 416. */
    w = neverc_http_memory_writer_new();
    if (w) {
        neverc_http_request_t req;
        memset(&req, 0, sizeof(req));
        req.method = "GET";
        req.path = "/test.txt";
        char range_hdr[64];
        size_t rpos = 0;
        memcpy(range_hdr + rpos, "Range", 6); rpos += 6;
        memcpy(range_hdr + rpos, "bytes=100-200", 14);
        req.raw_headers = range_hdr;
        req.nheaders = 1;

        neverc_http_serve_file(w, &req, tmppath);

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);
        check_int("serve_file unsatisfiable range", status, 416);
        check_int("serve_file 416 has no file body",
                  data == NULL || strstr(data, "Hello") == NULL, 1);
        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* If-Modified-Since in the future → 304. */
    w = neverc_http_memory_writer_new();
    if (w) {
        neverc_http_request_t req;
        memset(&req, 0, sizeof(req));
        req.method = "GET";
        req.path = "/test.txt";
        char ims_hdr[80];
        size_t ipos = 0;
        memcpy(ims_hdr + ipos, "If-Modified-Since", 18); ipos += 18;
        memcpy(ims_hdr + ipos, "Thu, 01 Jan 2099 00:00:00 GMT", 30);
        req.raw_headers = ims_hdr;
        req.nheaders = 1;

        neverc_http_serve_file(w, &req, tmppath);

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);
        check_int("serve_file IMS 304 status", status, 304);
        check_int("serve_file IMS 304 no body", data == NULL || data_len == 0, 1);
        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* Go ServeFile rejects ".." in the request path even for a safe file. */
    w = neverc_http_memory_writer_new();
    if (w) {
        neverc_http_request_t req;
        memset(&req, 0, sizeof(req));
        req.method = "GET";
        req.path = "/foo/../secret.txt";

        neverc_http_serve_file(w, &req, tmppath);

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);
        check_int("serve_file dotdot status", status, 400);
        check_int("serve_file dotdot no leak",
                  data == NULL || strstr(data, "Hello from served file!") == NULL,
                  1);
        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* Go unescapes URL.Path before the ".." check; %2e%2e is the same hole. */
    w = neverc_http_memory_writer_new();
    if (w) {
        neverc_http_request_t req;
        memset(&req, 0, sizeof(req));
        req.method = "GET";
        req.path = "/foo/%2e%2e/secret.txt";

        neverc_http_serve_file(w, &req, tmppath);

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);
        check_int("serve_file encoded dotdot status", status, 400);
        check_int("serve_file encoded dotdot no leak",
                  data == NULL || strstr(data, "Hello from served file!") == NULL,
                  1);
        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* Test non-existent file */
    w = neverc_http_memory_writer_new();
    if (w) {
        neverc_http_request_t req;
        memset(&req, 0, sizeof(req));
        req.method = "GET";
        req.path = "/nope.txt";

        neverc_http_serve_file(w, &req, "/tmp/neverc_nonexistent_file.txt");

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);

        check_int("serve_file 404 status", status, 404);

        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* A directory can be opened as a FILE on some POSIX systems but cannot be
     * read as a regular file. It must not be reported as an empty 200 response. */
    w = neverc_http_memory_writer_new();
    if (w) {
        neverc_http_request_t req;
        memset(&req, 0, sizeof(req));
        req.method = "GET";
        req.path = "/directory";

        neverc_http_serve_file(w, &req, "/tmp");

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);
        check_int("serve_file unreadable input status", status, 500);
        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* neverc_http_set_header silently drops Content-Length; ServeFile must
     * use set_content_length so GET/HEAD emit the file size. */
    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/file", serve_file_live_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    for (int i = 0; i < 100; i++) {
        usleep(30000);
        const char *err = NULL;
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (c) { neverc_tcp_close(c); break; }
    }

    char buf[4096];
    int n = do_http_request(port,
        "GET /file HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("serve_file live resp", n > 0, 1);
    check_int("serve_file live 200", strstr(buf, "200 OK") != NULL, 1);
    check_int("serve_file live length",
              strstr(buf, "Content-Length: 23") != NULL, 1);
    check_int("serve_file live body",
              strstr(buf, "Hello from served file!") != NULL, 1);
    check_int("serve_file live ranges",
              strstr(buf, "Accept-Ranges: bytes") != NULL, 1);
    check_int("serve_file live last-modified",
              strstr(buf, "Last-Modified: ") != NULL, 1);

    {
        const char *lm = strstr(buf, "Last-Modified: ");
        if (lm) {
            lm += 15;
            const char *lm_end = strstr(lm, "\r\n");
            size_t lmlen = lm_end ? (size_t)(lm_end - lm) : 0;
            check_int("serve_file last-modified value",
                      lmlen > 0 && lmlen < 80, 1);
            if (lmlen > 0 && lmlen < 80) {
                char ims[80];
                char ims_req[512];
                memcpy(ims, lm, lmlen);
                ims[lmlen] = '\0';
                snprintf(ims_req, sizeof(ims_req),
                         "GET /file HTTP/1.1\r\nHost: localhost\r\n"
                         "If-Modified-Since: %s\r\n"
                         "Connection: close\r\n\r\n", ims);
                n = do_http_request(port, ims_req, buf, sizeof(buf));
                check_int("serve_file live IMS 304",
                          strstr(buf, "304 Not Modified") != NULL, 1);
                check_int("serve_file live IMS no body",
                          strstr(buf, "Hello from served file!") == NULL, 1);
            }
        }
    }

    n = do_http_request(port,
        "GET /file HTTP/1.1\r\nHost: localhost\r\n"
        "Range: bytes=0-4\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("serve_file live 206", strstr(buf, "206 Partial Content") != NULL, 1);
    check_int("serve_file live content-range",
              strstr(buf, "Content-Range: bytes 0-4/23") != NULL, 1);
    check_int("serve_file live range length",
              strstr(buf, "Content-Length: 5\r\n") != NULL, 1);
    check_int("serve_file live range body", strstr(buf, "Hello") != NULL, 1);

    n = do_http_request(port,
        "GET /file HTTP/1.1\r\nHost: localhost\r\n"
        "Range: bytes=100-200\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("serve_file live 416",
              strstr(buf, "416 Range Not Satisfiable") != NULL, 1);
    check_int("serve_file live 416 content-range",
              strstr(buf, "Content-Range: bytes */23") != NULL, 1);

    n = do_http_request(port,
        "HEAD /file HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("serve_file HEAD resp", n > 0, 1);
    check_int("serve_file HEAD 200", strstr(buf, "200 OK") != NULL, 1);
    check_int("serve_file HEAD length",
              strstr(buf, "Content-Length: 23") != NULL, 1);
    check_int("serve_file HEAD no body",
              strstr(buf, "Hello from served file!") == NULL, 1);

    n = do_http_request(port,
        "HEAD /file HTTP/1.1\r\nHost: localhost\r\n"
        "Range: bytes=0-4\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("serve_file HEAD range 206",
              strstr(buf, "206 Partial Content") != NULL, 1);
    check_int("serve_file HEAD range length",
              strstr(buf, "Content-Length: 5\r\n") != NULL, 1);
    check_int("serve_file HEAD range no body",
              strstr(buf, "Hello") == NULL, 1);

    stop_test_server(pid);
    unlink(tmppath);
}

/* ===== StripPrefix via server test ===== */

static void strip_inner_handler(neverc_http_request_t *req,
                                  neverc_http_response_writer_t *w) {
    neverc_http_writef(w, "stripped_path=%s", req->path);
}

static void strip_version_handler(neverc_http_request_t *req,
                                  neverc_http_response_writer_t *w) {
    neverc_http_writef(w, "version_path=%s", req->path);
}

static void test_strip_prefix(void) {
    printf("[strip_prefix]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_strip_prefix(mux, "/api", "/api/",
                                  strip_inner_handler);
        neverc_http_strip_prefix(mux, "/api/v2", "/api/v2/",
                                 strip_version_handler);
        /* Catch-all so "/apifoo" reaches the strip handler instead of
         * dying as an unmatched mux prefix. */
        neverc_http_strip_prefix(mux, "/api", "/",
                                  strip_inner_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);

    char buf[4096];
    int n = do_http_request(port,
        "GET /api/users HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("strip resp", n > 0, 1);
    check_int("strip body", strstr(buf, "stripped_path=/users") != NULL, 1);

    n = do_http_request(port,
        "GET /api/v2/users HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("overlapping strip resp", n > 0, 1);
    check_int("overlapping strip handler",
              strstr(buf, "version_path=/users") != NULL, 1);

    n = do_http_request(port,
        "GET /apifoo HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("strip prefix is a path-segment boundary",
              n > 0 && strstr(buf, "404") != NULL, 1);
    check_int("strip prefix does not leak sibling path",
              strstr(buf, "stripped_path=") == NULL, 1);

    n = do_http_request(port,
        "GET /api/../secret HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("strip prefix rejects dot-dot remainder",
              n > 0 && strstr(buf, "404") != NULL, 1);
    check_int("strip prefix does not leak dot-dot path",
              strstr(buf, "stripped_path=") == NULL, 1);

    n = do_http_request(port,
        "GET /api/%2e%2e/secret HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("strip prefix rejects encoded dot-dot remainder",
              n > 0 && strstr(buf, "404") != NULL, 1);
    check_int("strip prefix does not leak encoded dot-dot path",
              strstr(buf, "stripped_path=") == NULL, 1);

    n = do_http_request(port,
        "GET /api//evil.example/ HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("strip prefix rejects scheme-relative remainder",
              n > 0 && strstr(buf, "404") != NULL, 1);
    check_int("strip prefix does not leak scheme-relative path",
              strstr(buf, "stripped_path=") == NULL, 1);

    stop_test_server(pid);
}

/* ===== Path params test ===== */

static void path_param_handler(neverc_http_request_t *req,
                                 neverc_http_response_writer_t *w) {
    const char *id = neverc_http_path_value(req, "id");
    const char *action = neverc_http_path_value(req, "action");
    const char *rest = neverc_http_path_value(req, "path");
    neverc_http_writef(w, "id=%s,action=%s,path=%s",
                        id ? id : "NULL", action ? action : "NULL",
                        rest ? rest : "NULL");
}

static void mux_home_handler(neverc_http_request_t *req,
                              neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_string(w, "home");
}

static void mux_api_prefix_handler(neverc_http_request_t *req,
                                    neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_string(w, "api_prefix");
}

static void mux_posts_exact_handler(neverc_http_request_t *req,
                                     neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_string(w, "posts_exact");
}

static void test_path_params(void) {
    printf("[path_params]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        /* Go: most-specific path wins; method is only a tie-break. */
        neverc_http_mux_handle(mux, "GET /", mux_home_handler);
        neverc_http_mux_handle(mux, "GET /api/", mux_api_prefix_handler);
        neverc_http_mux_handle(mux, "/api/users/{id}", path_param_handler);
        neverc_http_mux_handle(mux, "GET /users/{id}", path_param_handler);
        neverc_http_mux_handle(mux, "HEAD /items/{id}/{action}",
                                path_param_handler);
        neverc_http_mux_handle(mux, "GET /items/{id}/{action}",
                                path_param_handler);
        neverc_http_mux_handle(mux, "GET /files/{path...}",
                                path_param_handler);
        neverc_http_mux_handle(mux, "GET /wild/{path...}/x",
                                path_param_handler);
        neverc_http_mux_handle(mux, "GET /posts/{$}",
                                mux_posts_exact_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);

    char buf[4096];

    /* Single param */
    int n = do_http_request(port,
        "GET /users/42 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("path_params resp", n > 0, 1);
    check_int("path_params id=42",
               strstr(buf, "id=42") != NULL, 1);
    check_int("GET / does not steal /users/{id}",
               strstr(buf, "home") == NULL, 1);

    /* Go 1.22: GET-only patterns also serve HEAD. */
    n = do_http_request(port,
        "HEAD /users/42 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("path_params HEAD via GET",
               n > 0 && strstr(buf, "200") != NULL, 1);
    check_int("path_params HEAD via GET no body",
               strstr(buf, "id=42") == NULL, 1);

    /* Two params */
    n = do_http_request(port,
        "GET /items/99/edit HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("path_params two resp", n > 0, 1);
    check_int("path_params id=99",
               strstr(buf, "id=99") != NULL, 1);
    check_int("path_params action=edit",
               strstr(buf, "action=edit") != NULL, 1);

    n = do_http_request(port,
        "HEAD /items/99/edit HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("path_params dedicated HEAD",
               n > 0 && strstr(buf, "200") != NULL, 1);

    /* No match → 404 */
    n = do_http_request(port,
        "GET /nonexistent HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("path_params 404 resp", n > 0, 1);
    check_int("path_params 404",
               strstr(buf, "404") != NULL, 1);

    n = do_http_request(port,
        "GET /files/a/b/c HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("wildcard resp", n > 0, 1);
    check_int("wildcard captures remainder",
               strstr(buf, "path=a/b/c") != NULL, 1);

    n = do_http_request(port,
        "GET /files HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("wildcard empty remainder /files",
               n > 0 && strstr(buf, "200") != NULL &&
               strstr(buf, "path=") != NULL &&
               strstr(buf, "path=NULL") == NULL, 1);

    n = do_http_request(port,
        "GET /files/ HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("wildcard empty remainder /files/",
               n > 0 && strstr(buf, "200") != NULL &&
               strstr(buf, "path=") != NULL &&
               strstr(buf, "path=NULL") == NULL, 1);

    n = do_http_request(port,
        "GET /api/users/7 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("method-less /api/users/{id} beats GET /api/",
               n > 0 && strstr(buf, "id=7") != NULL &&
               strstr(buf, "api_prefix") == NULL, 1);

    n = do_http_request(port,
        "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("GET / still serves the root",
               n > 0 && strstr(buf, "home") != NULL, 1);

    n = do_http_request(port,
        "GET /wild/foo/x HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("mid-pattern wildcard rejected",
               n > 0 && strstr(buf, "404") != NULL, 1);

    n = do_http_request(port,
        "GET /posts/ HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("{$} matches /posts/ only",
               n > 0 && strstr(buf, "posts_exact") != NULL &&
               strstr(buf, "home") == NULL, 1);

    n = do_http_request(port,
        "GET /posts/234 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("{$} does not capture leftover /posts/234",
               n > 0 && strstr(buf, "posts_exact") == NULL &&
               strstr(buf, "home") != NULL, 1);

    n = do_http_request(port,
        "GET /posts HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("{$} does not match /posts without slash",
               n > 0 && strstr(buf, "posts_exact") == NULL, 1);

    /* neverc_http_path_value null safety */
    check_int("path_value null req",
               neverc_http_path_value(NULL, "x") == NULL, 1);
    {
        neverc_http_request_t empty_req;
        memset(&empty_req, 0, sizeof(empty_req));
        check_int("path_value no params",
                   neverc_http_path_value(&empty_req, "x") == NULL, 1);
    }

    stop_test_server(pid);
}

/* ===== Rate limiter test ===== */

static void test_rate_limiter(void) {
    printf("[rate_limiter]\n");

    /* Null safety */
    check_int("rl allow null", neverc_http_rate_limiter_allow(NULL), 1);
    neverc_http_rate_limiter_free(NULL);

    /* Create rate limiter with burst=3 */
    neverc_http_rate_limiter_t *rl = neverc_http_rate_limiter_new(10.0, 3);
    check_not_null("rl created", rl);

    /* Burst allows first 3 requests immediately */
    check_int("rl allow 1", neverc_http_rate_limiter_allow(rl), 1);
    check_int("rl allow 2", neverc_http_rate_limiter_allow(rl), 1);
    check_int("rl allow 3", neverc_http_rate_limiter_allow(rl), 1);

    /* 4th request should be denied (burst exhausted, no time to refill) */
    check_int("rl deny 4", neverc_http_rate_limiter_allow(rl), 0);

    /* After a brief sleep, tokens should refill (rate=10/s → ~1 token per 100ms) */
    usleep(250000);
    check_int("rl allow after refill", neverc_http_rate_limiter_allow(rl), 1);

    neverc_http_rate_limiter_free(rl);
}

/* ===== CORS test ===== */

static void cors_test_handler(neverc_http_request_t *req,
                                neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_string(w, "cors_ok");
}

static void test_cors(void) {
    printf("[cors]\n");

    int port = get_free_port();
    if (port < 0) { printf("  SKIP: no free port\n"); return; }

    pid_t pid = fork();
    if (pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/test", cors_test_handler);

        neverc_http_cors_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.allowed_origins =
            "http://example.com, https://admin.example.com";
        cfg.allowed_methods = "GET, POST";
        cfg.allowed_headers = "Content-Type";
        cfg.allow_credentials = 1;
        cfg.max_age = 3600;
        neverc_http_enable_cors(mux, &cfg);

        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);

    char buf[4096];

    /* OPTIONS preflight should get 204 with CORS headers */
    int n = do_http_request(port,
        "OPTIONS / HTTP/1.1\r\nHost: localhost\r\n"
        "Origin: http://example.com\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("cors preflight resp", n > 0, 1);
    check_int("cors allow-origin",
               strstr(buf, "Access-Control-Allow-Origin: "
                           "http://example.com\r\n") != NULL, 1);
    check_int("cors allow-credentials",
               strstr(buf, "Access-Control-Allow-Credentials: true\r\n") !=
                   NULL, 1);
    check_int("cors allow-methods",
               strstr(buf, "Access-Control-Allow-Methods") != NULL, 1);

    /* Normal request should also get CORS headers */
    n = do_http_request(port,
        "GET /test HTTP/1.1\r\nHost: localhost\r\n"
        "Origin: http://example.com\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("cors normal resp", n > 0, 1);
    check_int("cors normal allow-origin",
               strstr(buf, "Access-Control-Allow-Origin: "
                           "http://example.com\r\n") != NULL, 1);
    check_int("cors normal body",
               strstr(buf, "cors_ok") != NULL, 1);

    /* A textual prefix is not an allowed origin. */
    n = do_http_request(port,
        "GET /test HTTP/1.1\r\nHost: localhost\r\n"
        "Origin: http://example\r\nConnection: close\r\n\r\n",
        buf, sizeof(buf));
    check_int("cors prefix resp", n > 0, 1);
    check_int("cors reject prefix origin",
               strstr(buf, "Access-Control-Allow-Origin") == NULL, 1);
    check_int("cors omit credentials for rejected origin",
               strstr(buf, "Access-Control-Allow-Credentials") == NULL, 1);

    stop_test_server(pid);
}

/* ===== JSON helpers test ===== */

static void test_json_helpers(void) {
    printf("[json_helpers]\n");

    /* neverc_http_json_get — extract values from JSON request body */
    neverc_http_request_t req;
    memset(&req, 0, sizeof(req));

    const char *json_body = "{\"name\": \"Alice\", \"age\": 30, \"active\": true}";
    req.body = json_body;
    req.body_len = strlen(json_body);

    char vbuf[128];

    /* String value */
    const char *v = neverc_http_json_get(&req, "name", vbuf, sizeof(vbuf));
    check_not_null("json get name", v);
    if (v) check_str("json name val", v, "Alice");

    /* Number value */
    v = neverc_http_json_get(&req, "age", vbuf, sizeof(vbuf));
    check_not_null("json get age", v);
    if (v) check_str("json age val", v, "30");

    /* Boolean value */
    v = neverc_http_json_get(&req, "active", vbuf, sizeof(vbuf));
    check_not_null("json get active", v);
    if (v) check_str("json active val", v, "true");

    /* Missing key */
    v = neverc_http_json_get(&req, "missing", vbuf, sizeof(vbuf));
    check_int("json missing key", v == NULL, 1);

    /* Nested objects must not steal a later top-level field. */
    const char *nested =
        "{\"user\":{\"role\":\"admin\"},\"role\":\"user\"}";
    req.body = nested;
    req.body_len = strlen(nested);
    v = neverc_http_json_get(&req, "role", vbuf, sizeof(vbuf));
    check_not_null("json top-level role", v);
    if (v) check_str("json top-level role val", v, "user");

    /* Quote-scan confusion is not a key. */
    const char *confused = "{\"x\":\"role\":\"admin\"}";
    req.body = confused;
    req.body_len = strlen(confused);
    v = neverc_http_json_get(&req, "role", vbuf, sizeof(vbuf));
    check_int("json reject quote-scan role", v == NULL, 1);

    req.body = json_body;
    req.body_len = strlen(json_body);

    /* Null safety */
    check_int("json get null req",
               neverc_http_json_get(NULL, "x", vbuf, sizeof(vbuf)) == NULL, 1);
    check_int("json get null key",
               neverc_http_json_get(&req, NULL, vbuf, sizeof(vbuf)) == NULL, 1);

    /* Empty body */
    neverc_http_request_t empty_req;
    memset(&empty_req, 0, sizeof(empty_req));
    check_int("json empty body",
               neverc_http_json_get(&empty_req, "x", vbuf, sizeof(vbuf)) == NULL, 1);

    /* neverc_http_json_error — write JSON error response */
    neverc_http_response_writer_t *w = neverc_http_memory_writer_new();
    check_not_null("json_error writer", w);
    if (w) {
        neverc_http_json_error(w, 400, "bad request");

        char *data = NULL;
        size_t data_len = 0;
        int status = neverc_http_memory_writer_result(w, &data, &data_len);

        check_int("json_error status", status, 400);
        check_int("json_error body has error",
                   data && strstr(data, "bad request") != NULL, 1);
        check_int("json_error body has code",
                   data && strstr(data, "400") != NULL, 1);

        free(data);
        neverc_http_memory_writer_free(w);
    }

    w = neverc_http_memory_writer_new();
    check_not_null("json_error quote writer", w);
    if (w) {
        neverc_http_json_error(w, 400, "a\"b\\c");
        char *data = NULL;
        size_t data_len = 0;
        (void)neverc_http_memory_writer_result(w, &data, &data_len);
        check_int("json_error escapes quote",
                  data && strstr(data, "\"a\\\"b\\\\c\"") != NULL, 1);
        check_int("json_error does not split object",
                  data && strstr(data, "\"a\"b") == NULL, 1);
        free(data);
        neverc_http_memory_writer_free(w);
    }

    /* json_error null safety */
    check_int("json_error null w", neverc_http_json_error(NULL, 500, "err"), 0);
}

#endif /* _WIN32 */

/* ===== ResponseHeader test ===== */

static void test_response_header(void) {
    printf("[response_header]\n");

    neverc_http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.headers = strdup("Content-Type: application/json\r\n"
                          "X-Custom: test-value\r\n"
                          "Cache-Control: no-cache\r\n");

    char buf[128];
    const char *v;

    v = neverc_http_response_header(&resp, "Content-Type", buf, sizeof(buf));
    check_not_null("resp hdr content-type", v);
    if (v) check_str("resp hdr ct val", v, "application/json");

    v = neverc_http_response_header(&resp, "X-Custom", buf, sizeof(buf));
    check_not_null("resp hdr x-custom", v);
    if (v) check_str("resp hdr custom val", v, "test-value");

    v = neverc_http_response_header(&resp, "Cache-Control", buf, sizeof(buf));
    check_not_null("resp hdr cache-control", v);
    if (v) check_str("resp hdr cache val", v, "no-cache");

    v = neverc_http_response_header(&resp, "Missing", buf, sizeof(buf));
    check_int("resp hdr missing", v == NULL, 1);

    /* Null safety */
    v = neverc_http_response_header(NULL, "X", buf, sizeof(buf));
    check_int("resp hdr null resp", v == NULL, 1);
    v = neverc_http_response_header(&resp, NULL, buf, sizeof(buf));
    check_int("resp hdr null name", v == NULL, 1);

    free(resp.headers);
}

int main(void) {
    test_status_text();
    test_query_get();
    test_form_value();
    test_mux();
    test_mux_dollar_pattern();
    test_writer_null_safety();
    test_response_free_null();
    test_detect_content_type();
    test_canonical_header_key();
    test_not_found_handler();
    test_redirect_html_escape();
    test_response_header();
#ifndef _WIN32
    test_http_server();
    test_http_client();
    test_http_client_redirects();
    test_stress();
    test_large_post();
    test_http_methods();
    test_large_static_head();
    test_keep_alive();
    test_thread_stress();
    test_404();
    test_http_client_methods();
    test_malformed_request();
    test_post_no_content_length();
    test_post_no_content_length_keepalive();
    test_post_http10_keepalive_no_cl();
    test_expect_continue();
    test_duplicate_headers();
    test_heavy_stress();
    test_pipelining();
    test_connection_close_on_error();
    test_connection_churn();
    test_chunked_encoding();
    test_config_apis();
    test_client_chunked_response();
    test_client_many_tiny_chunks();
    test_client_304_chunked_not_pooled();
    test_client_rejects_bare_lf_status();
    test_connection_limit();
    test_max_body_size_buffered();
    test_server_lifecycle();
    test_connection_pool();
    test_convenience_apis();
    test_benchmark();
    test_cookies();
    test_multipart_parsing();
    test_sse();
    test_serve_file();
    test_strip_prefix();
    test_path_params();
    test_rate_limiter();
    test_cors();
    test_json_helpers();
#endif

    printf("\n--- net/http: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
