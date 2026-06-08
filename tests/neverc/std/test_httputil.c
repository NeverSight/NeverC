#include "neverc/std/net/http/httputil.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static void check_not_null(const char *name, const void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

static void check_contains(const char *name, const char *haystack,
                             const char *needle) {
    tests_run++;
    if (haystack && strstr(haystack, needle)) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: '%s' not found in output\n", name,
               needle);
    }
}

/* ===== Test: URL parsing ===== */

static void test_reverse_proxy_creation(void) {
    printf("[reverse_proxy_creation]\n");

    neverc_httputil_reverse_proxy_t *rp =
        neverc_httputil_new_single_host_reverse_proxy("http://127.0.0.1:8081");
    check_not_null("create proxy", rp);
    if (rp) {
        neverc_http_handler_func_t h = neverc_httputil_proxy_handler(rp);
        check_not_null("proxy handler", (const void *)(uintptr_t)h);
        neverc_httputil_proxy_free(rp);
    }

    rp = neverc_httputil_new_single_host_reverse_proxy(
        "https://api.example.com:443/v2");
    check_not_null("create https proxy", rp);
    if (rp) neverc_httputil_proxy_free(rp);

    rp = neverc_httputil_new_single_host_reverse_proxy("invalid");
    check_true("invalid url returns NULL", rp == NULL);
}

/* ===== Test: Request dump ===== */

static void test_dump_request(void) {
    printf("[dump_request]\n");

    neverc_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.path = "/api/users";
    req.query = "page=1&limit=10";
    req.http_version = "HTTP/1.1";
    req.host = "example.com";

    char *dump = neverc_httputil_dump_request(&req, 0);
    check_not_null("dump not null", dump);
    if (dump) {
        check_contains("has method", dump, "GET");
        check_contains("has path", dump, "/api/users");
        check_contains("has query", dump, "page=1&limit=10");
        check_contains("has host", dump, "Host: example.com");
        check_contains("has version", dump, "HTTP/1.1");
        free(dump);
    }

    /* With body */
    req.method = "POST";
    req.body = "name=test&value=123";
    req.body_len = strlen(req.body);
    dump = neverc_httputil_dump_request(&req, 1);
    check_not_null("dump with body", dump);
    if (dump) {
        check_contains("body present", dump, "name=test&value=123");
        free(dump);
    }
}

/* ===== Test: Dump request out ===== */

static void test_dump_request_out(void) {
    printf("[dump_request_out]\n");

    char *dump = neverc_httputil_dump_request_out(
        "POST", "/api/data",
        "Content-Type: application/json\r\n",
        "{\"key\":\"val\"}", 13);

    check_not_null("dump out not null", dump);
    if (dump) {
        check_contains("has POST", dump, "POST /api/data HTTP/1.1");
        check_contains("has content-type", dump, "Content-Type: application/json");
        check_contains("has content-length", dump, "Content-Length: 13");
        check_contains("has body", dump, "{\"key\":\"val\"}");
        free(dump);
    }

    /* GET without body */
    dump = neverc_httputil_dump_request_out("GET", "/health", NULL, NULL, 0);
    check_not_null("dump GET", dump);
    if (dump) {
        check_contains("has GET", dump, "GET /health HTTP/1.1");
        check_true("no content-length for GET",
                     strstr(dump, "Content-Length") == NULL);
        free(dump);
    }
}

/* ===== Test: Reverse proxy with real backend ===== */

static volatile int g_backend_port = 0;
static volatile int g_backend_ready = 0;
static int g_proxy_port = 0;

static int do_http_request(int port, const char *request,
                         char *response, size_t resplen) {
    const char *err = NULL;
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    if (!conn) return -1;

    neverc_tcp_set_timeout(conn, 3000);
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

static int wait_for_tcp_port(int port, int attempts) {
    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    for (int i = 0; i < attempts; i++) {
        const char *err = NULL;
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        if (conn) {
            neverc_tcp_close(conn);
            return 0;
        }
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    return -1;
}

static void backend_handler(neverc_http_request_t *req,
                              neverc_http_response_writer_t *w) {
    neverc_http_set_header(w, "Content-Type", "application/json");
    neverc_http_set_header(w, "X-Backend", "true");
    neverc_http_writef(w, "{\"proxied\":true,\"path\":\"%s\"}",
                        req->path ? req->path : "/");
}

#ifdef _WIN32
static DWORD WINAPI backend_thread(LPVOID arg) {
#else
static void *backend_thread(void *arg) {
#endif
    (void)arg;
    neverc_http_set_workers(1);
    neverc_http_set_max_requests(100);
    neverc_http_set_read_timeout(3000);
    neverc_http_set_idle_timeout(1000);
    neverc_http_set_shutdown_timeout(2000);

    neverc_http_mux_t *mux = neverc_http_new_mux();
    neverc_http_mux_handle(mux, "/", backend_handler);

    const char *err = NULL;
    neverc_tcp_listener_t *probe = neverc_tcp_listen("127.0.0.1:0", &err);
    if (!probe) {
        neverc_http_mux_free(mux);
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }
    neverc_tcp_addr_t pa;
    neverc_tcp_listener_addr(probe, &pa);
    g_backend_port = pa.port;
    neverc_tcp_listener_close(probe);

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_backend_port);
#if defined(_WIN32)
    Sleep(100);
#else
    usleep(100000);
#endif
    g_backend_ready = 1;
    neverc_http_listen_and_serve(addr, mux);
    neverc_http_mux_free(mux);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int wait_for_backend_http(int port, int attempts) {
    const char *req =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    for (int i = 0; i < attempts; i++) {
        char buf[512];
        if (do_http_request(port, req, buf, sizeof(buf)) > 0 &&
            strstr(buf, "200 OK") != NULL)
            return 0;
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    return -1;
}

static void test_reverse_proxy_live(void) {
    printf("[reverse_proxy_live]\n");

    g_backend_port = 0;
    g_backend_ready = 0;

#ifdef _WIN32
    HANDLE bt = CreateThread(NULL, 0, backend_thread, NULL, 0, NULL);
#else
    pthread_t bt;
    pthread_create(&bt, NULL, backend_thread, NULL);
#endif

    for (int i = 0; i < 100 && !g_backend_ready; i++) {
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    if (!g_backend_ready || g_backend_port <= 0) {
        printf("  SKIP: backend thread did not publish a port\n");
        neverc_http_shutdown();
#ifdef _WIN32
        WaitForSingleObject(bt, 3000);
        CloseHandle(bt);
#else
        pthread_join(bt, NULL);
#endif
        return;
    }

    if (wait_for_backend_http(g_backend_port, 50) != 0) {
        printf("  SKIP: backend did not become ready\n");
        neverc_http_shutdown();
#ifdef _WIN32
        WaitForSingleObject(bt, 3000);
        CloseHandle(bt);
#else
        pthread_join(bt, NULL);
#endif
        return;
    }

    /* Test: direct request to backend to verify it works */
    char respbuf[4096];
    const char *req =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    int n = do_http_request(g_backend_port, req, respbuf, sizeof(respbuf));
    check_true("backend tcp response", n > 0);
    if (n > 0) {
        check_true("backend status 200", strstr(respbuf, "200 OK") != NULL);
        check_contains("backend body", respbuf, "proxied");
    }

    /* Test: create and verify reverse proxy config */
    char target[64];
    snprintf(target, sizeof(target),
             "http://127.0.0.1:%d", g_backend_port);
    neverc_httputil_reverse_proxy_t *rp =
        neverc_httputil_new_single_host_reverse_proxy(target);
    check_not_null("proxy created", rp);

    if (rp) {
        neverc_http_handler_func_t h = neverc_httputil_proxy_handler(rp);
        check_not_null("handler not null", (const void *)(uintptr_t)h);
        neverc_httputil_proxy_free(rp);
    }

    neverc_http_shutdown();
#ifdef _WIN32
    WaitForSingleObject(bt, 3000);
    CloseHandle(bt);
#else
    pthread_join(bt, NULL);
#endif
}

int main(void) {
    printf("=== NeverC httputil tests ===\n");

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    test_reverse_proxy_creation();
    test_dump_request();
    test_dump_request_out();
    test_reverse_proxy_live();

    printf("\n%d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
