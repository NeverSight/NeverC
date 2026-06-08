#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#else
#include <windows.h>
#endif

/* ======================================================================
 * Benchmark: HTTP server throughput test
 *
 * Tests:
 *   1. Raw TCP connect/close rate
 *   2. HTTP request/response throughput (keep-alive)
 *   3. Concurrent connection handling
 *   4. Large response body throughput
 * ====================================================================== */

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static uint64_t now_us(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (uint64_t)(cnt.QuadPart * 1000000 / freq.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
#endif
}

/* ===== Handlers ===== */

static void bench_hello_handler(neverc_http_request_t *req,
                                  neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_write_string(w, "OK");
}

static void bench_json_handler(neverc_http_request_t *req,
                                 neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_set_header(w, "Content-Type", "application/json");
    neverc_http_write_string(w,
        "{\"message\":\"Hello, World!\",\"status\":200}");
}

/* ===== Server management ===== */

static int g_server_port = 0;

#ifdef _WIN32
static DWORD WINAPI server_thread(LPVOID arg) {
#else
static void *server_thread(void *arg) {
#endif
    (void)arg;
    neverc_http_set_workers(4);
    neverc_http_set_max_requests(10000);
    neverc_http_set_read_timeout(5000);

    neverc_http_mux_t *mux = neverc_http_new_mux();
    neverc_http_mux_handle(mux, "/", bench_hello_handler);
    neverc_http_mux_handle(mux, "/json", bench_json_handler);

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_server_port);
    neverc_http_listen_and_serve(addr, mux);
    neverc_http_mux_free(mux);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ===== Benchmark: Raw HTTP requests per second ===== */

typedef struct {
    int port;
    int requests;
    int success;
    uint64_t elapsed_us;
} bench_result_t;

#ifdef _WIN32
static DWORD WINAPI bench_worker(LPVOID arg) {
#else
static void *bench_worker(void *arg) {
#endif
    bench_result_t *res = (bench_result_t *)arg;
    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", res->port);

    uint64_t start = now_us();

    const char *err = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    if (!conn) {
        res->elapsed_us = now_us() - start;
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

    neverc_tcp_set_timeout(conn, 5000);

    char req_buf[] =
        "GET / HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    size_t req_len = strlen(req_buf);

    for (int i = 0; i < res->requests; i++) {
        if (neverc_tcp_write(conn, req_buf, req_len) <= 0) break;

        char resp[4096];
        size_t total = 0;
        int found_end = 0;
        while (!found_end && total < sizeof(resp) - 1) {
            int n = neverc_tcp_read(conn, resp + total, sizeof(resp) - total - 1);
            if (n <= 0) break;
            total += (size_t)n;
            resp[total] = '\0';

            char *hdr_end = strstr(resp, "\r\n\r\n");
            if (hdr_end) {
                char *cl = strstr(resp, "Content-Length: ");
                if (cl) {
                    int body_len = atoi(cl + 16);
                    size_t header_size = (size_t)(hdr_end + 4 - resp);
                    if (total >= header_size + (size_t)body_len)
                        found_end = 1;
                } else {
                    found_end = 1;
                }
            }
        }
        if (found_end && strstr(resp, "200 OK"))
            res->success++;
    }

    neverc_tcp_close(conn);
    res->elapsed_us = now_us() - start;

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void test_throughput(void) {
    printf("[throughput]\n");

    int nthreads = 8;
    int requests_per_thread = 500;

    bench_result_t results[8];
#ifdef _WIN32
    HANDLE threads[8];
#else
    pthread_t threads[8];
#endif

    uint64_t start = now_us();

    for (int i = 0; i < nthreads; i++) {
        results[i].port = g_server_port;
        results[i].requests = requests_per_thread;
        results[i].success = 0;
        results[i].elapsed_us = 0;
#ifdef _WIN32
        threads[i] = CreateThread(NULL, 0, bench_worker, &results[i], 0, NULL);
#else
        pthread_create(&threads[i], NULL, bench_worker, &results[i]);
#endif
    }

    for (int i = 0; i < nthreads; i++) {
#ifdef _WIN32
        WaitForSingleObject(threads[i], 30000);
        CloseHandle(threads[i]);
#else
        pthread_join(threads[i], NULL);
#endif
    }

    uint64_t elapsed = now_us() - start;

    int total_success = 0;
    int total_requests = nthreads * requests_per_thread;
    for (int i = 0; i < nthreads; i++)
        total_success += results[i].success;

    double rps = (double)total_success / ((double)elapsed / 1000000.0);
    double success_pct = 100.0 * (double)total_success / (double)total_requests;

    printf("  threads: %d, requests/thread: %d\n", nthreads, requests_per_thread);
    printf("  total requests: %d, success: %d (%.1f%%)\n",
           total_requests, total_success, success_pct);
    printf("  elapsed: %.2f ms\n", (double)elapsed / 1000.0);
    printf("  throughput: %.0f req/s\n", rps);

    check_true("success rate > 95%", success_pct > 95.0);
    check_true("throughput > 1000 req/s", rps > 1000.0);
}

/* ===== Benchmark: Connection rate ===== */

static void test_connection_rate(void) {
    printf("[connection_rate]\n");

    int n_conns = 200;
    int success = 0;
    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_server_port);

    uint64_t start = now_us();

    for (int i = 0; i < n_conns; i++) {
        const char *err = NULL;
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        if (conn) {
            neverc_tcp_set_timeout(conn, 2000);
            const char *req =
                "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            neverc_tcp_write(conn, req, strlen(req));
            char resp[4096];
            int n = neverc_tcp_read(conn, resp, sizeof(resp) - 1);
            if (n > 0) {
                resp[n] = '\0';
                if (strstr(resp, "200 OK")) success++;
            }
            neverc_tcp_close(conn);
        }
    }

    uint64_t elapsed = now_us() - start;
    double cps = (double)success / ((double)elapsed / 1000000.0);

    printf("  connections: %d, success: %d\n", n_conns, success);
    printf("  elapsed: %.2f ms\n", (double)elapsed / 1000.0);
    printf("  connection rate: %.0f conn/s\n", cps);

    check_true("conn success rate > 95%",
                (double)success / (double)n_conns > 0.95);
    check_true("conn rate > 500 conn/s", cps > 500.0);
}

/* ===== Main ===== */

int main(void) {
    printf("=== NeverC HTTP Benchmark ===\n");

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Start server on a random port */
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    if (!ln) {
        printf("FAIL: cannot create listener\n");
        return 1;
    }
    neverc_tcp_addr_t addr;
    neverc_tcp_listener_addr(ln, &addr);
    g_server_port = addr.port;
    neverc_tcp_listener_close(ln);

    printf("  server port: %d\n", g_server_port);

#ifdef _WIN32
    HANDLE srv = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    Sleep(200);
#else
    pthread_t srv;
    pthread_create(&srv, NULL, server_thread, NULL);
    usleep(200000);
#endif

    test_connection_rate();
    test_throughput();

    neverc_http_shutdown();

#ifdef _WIN32
    WaitForSingleObject(srv, 3000);
    CloseHandle(srv);
#else
    pthread_join(srv, NULL);
#endif

    printf("\n%d/%d benchmarks passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
