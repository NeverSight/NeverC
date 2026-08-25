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
#include <sys/resource.h>
#include <sys/time.h>
#else
#include <windows.h>
#include <psapi.h>
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

static uint64_t process_rss_bytes(void) {
#ifdef _WIN32
    typedef BOOL(WINAPI *get_memory_info_fn)(
        HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    get_memory_info_fn get_memory_info = kernel ? (get_memory_info_fn)(
        void *)GetProcAddress(kernel, "K32GetProcessMemoryInfo") : NULL;
    PROCESS_MEMORY_COUNTERS counters;
    if (!get_memory_info ||
        !get_memory_info(GetCurrentProcess(), &counters, sizeof(counters)))
        return 0;
    return (uint64_t)counters.WorkingSetSize;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#ifdef __APPLE__
    return (uint64_t)usage.ru_maxrss;
#else
    return (uint64_t)usage.ru_maxrss * 1024U;
#endif
#endif
}

static int compare_u64(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t percentile_us(const uint64_t *sorted, size_t count,
                              unsigned percentile) {
    if (!sorted || count == 0) return 0;
    size_t rank = ((size_t)percentile * count + 99U) / 100U;
    if (rank == 0) rank = 1;
    if (rank > count) rank = count;
    return sorted[rank - 1U];
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
    neverc_http_set_idle_timeout(2000);
    neverc_http_set_shutdown_timeout(2000);

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
    uint64_t *latencies_us;
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
        uint64_t request_start = now_us();
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
        if (found_end && strstr(resp, "200 OK")) {
            if (res->latencies_us)
                res->latencies_us[res->success] = now_us() - request_start;
            res->success++;
        }
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

#if defined(_WIN32)
    int nthreads = 2;
    int requests_per_thread = 50;
#else
    int nthreads = 8;
    int requests_per_thread = 500;
#endif

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
        results[i].latencies_us = (uint64_t *)calloc(
            (size_t)requests_per_thread, sizeof(uint64_t));
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

    uint64_t *latencies = (uint64_t *)calloc(
        (size_t)(total_success > 0 ? total_success : 1), sizeof(uint64_t));
    size_t latency_count = 0;
    for (int i = 0; i < nthreads; i++) {
        if (latencies && results[i].latencies_us) {
            memcpy(latencies + latency_count, results[i].latencies_us,
                   (size_t)results[i].success * sizeof(uint64_t));
            latency_count += (size_t)results[i].success;
        }
        free(results[i].latencies_us);
    }
    if (latencies) qsort(latencies, latency_count, sizeof(uint64_t),
                         compare_u64);

    double rps = (double)total_success / ((double)elapsed / 1000000.0);
    double success_pct = 100.0 * (double)total_success / (double)total_requests;

    printf("  threads: %d, requests/thread: %d\n", nthreads, requests_per_thread);
    printf("  total requests: %d, success: %d (%.1f%%)\n",
           total_requests, total_success, success_pct);
    printf("  elapsed: %.2f ms\n", (double)elapsed / 1000.0);
    printf("  throughput: %.0f req/s\n", rps);
    printf("  latency: p50=%.3f ms p95=%.3f ms p99=%.3f ms\n",
           (double)percentile_us(latencies, latency_count, 50) / 1000.0,
           (double)percentile_us(latencies, latency_count, 95) / 1000.0,
           (double)percentile_us(latencies, latency_count, 99) / 1000.0);
    free(latencies);

#if defined(_WIN32)
    check_true("success rate > 80%", success_pct > 80.0);
    check_true("throughput > 50 req/s", rps > 50.0);
#else
    check_true("success rate > 95%", success_pct > 95.0);
    check_true("throughput > 1000 req/s", rps > 1000.0);
#endif
}

/* ===== Benchmark: Connection rate ===== */

static void test_connection_rate(void) {
    printf("[connection_rate]\n");

#if defined(_WIN32)
    int n_conns = 20;
#else
    int n_conns = 200;
#endif
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

#if defined(_WIN32)
    check_true("conn success rate > 80%",
                (double)success / (double)n_conns > 0.80);
    check_true("conn rate > 10 conn/s", cps > 10.0);
#else
    check_true("conn success rate > 95%",
                (double)success / (double)n_conns > 0.95);
    check_true("conn rate > 500 conn/s", cps > 500.0);
#endif
}

/* ===== Benchmark: Concurrent connections (many open simultaneously) ===== */

static void test_concurrent_connections(void) {
    printf("[concurrent_conns]\n");

#if defined(_WIN32)
    int max_conns = 100;
#else
    int max_conns = 500;
#endif
    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_server_port);

    neverc_tcp_conn_t **conns =
        (neverc_tcp_conn_t **)calloc((size_t)max_conns, sizeof(neverc_tcp_conn_t *));
    int open_count = 0;

    uint64_t rss_before = process_rss_bytes();
    uint64_t start = now_us();

    for (int i = 0; i < max_conns; i++) {
        const char *err = NULL;
        conns[i] = neverc_tcp_dial(addr, &err);
        if (conns[i]) {
            neverc_tcp_set_timeout(conns[i], 2000);
            open_count++;
        }
    }
    uint64_t rss_after = process_rss_bytes();

    int success_count = 0;
    for (int i = 0; i < max_conns; i++) {
        if (!conns[i]) continue;
        const char *req =
            "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        if (neverc_tcp_write(conns[i], req, strlen(req)) > 0) {
            char resp[2048];
            int n = neverc_tcp_read(conns[i], resp, sizeof(resp) - 1);
            if (n > 0) {
                resp[n] = '\0';
                if (strstr(resp, "200 OK")) success_count++;
            }
        }
        neverc_tcp_close(conns[i]);
    }

    uint64_t elapsed = now_us() - start;
    free(conns);

    printf("  opened: %d/%d, responded: %d\n",
           open_count, max_conns, success_count);
    printf("  elapsed: %.2f ms\n", (double)elapsed / 1000.0);
    uint64_t rss_growth = rss_after > rss_before ?
        rss_after - rss_before : 0;
    printf("  memory: rss=%.2f MiB growth=%.2f MiB per-connection=%.0f bytes\n",
           (double)rss_after / (1024.0 * 1024.0),
           (double)rss_growth / (1024.0 * 1024.0),
           open_count > 0 ? (double)rss_growth / open_count : 0.0);

    check_true("open rate > 90%",
                (double)open_count / (double)max_conns > 0.90);
    check_true("response rate > 80%",
                (double)success_count / (double)open_count > 0.80);
}

static int bench_single_close_request(int port) {
    char address[32];
    snprintf(address, sizeof(address), "127.0.0.1:%d", port);
    const char *error = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_dial(address, &error);
    if (!conn) return -1;
    neverc_tcp_set_timeout(conn, 2000);
    const char request[] =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    int result = -1;
    if (neverc_tcp_write(conn, request, sizeof(request) - 1U) > 0) {
        char response[2048];
        int count = neverc_tcp_read(conn, response, sizeof(response) - 1U);
        if (count > 0) {
            response[count] = '\0';
            result = strstr(response, "200 OK") ? 0 : -1;
        }
    }
    neverc_tcp_close(conn);
    return result;
}

static void test_disconnect_recovery(void) {
    printf("[disconnect_recovery]\n");
    check_true("initial close request succeeds",
               bench_single_close_request(g_server_port) == 0);
    uint64_t start = now_us();
    int recovered = bench_single_close_request(g_server_port) == 0;
    uint64_t recovery_us = now_us() - start;
    printf("  recovery: %.3f ms\n", (double)recovery_us / 1000.0);
    check_true("request recovers on a new connection", recovered);
    check_true("disconnect recovery < 2 seconds", recovery_us < 2000000U);
}

/* ===== Benchmark: Pipelining throughput ===== */

static void test_pipelining_bench(void) {
    printf("[pipelining]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_server_port);

    const char *err = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
    if (!conn) { printf("  SKIP: cannot connect\n"); return; }

    neverc_tcp_set_timeout(conn, 5000);

    int batch_size = 50;
    int batches = 10;
    int success = 0;

    uint64_t start = now_us();

    for (int b = 0; b < batches; b++) {
        char pipeline[8192];
        int off = 0;
        for (int i = 0; i < batch_size; i++) {
            off += snprintf(pipeline + off, sizeof(pipeline) - (size_t)off,
                "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        }
        neverc_tcp_write(conn, pipeline, (size_t)off);

        char resp[65536];
        size_t total = 0;
        int responses_found = 0;
        while (responses_found < batch_size && total < sizeof(resp) - 1) {
            int n = neverc_tcp_read(conn, resp + total,
                                    sizeof(resp) - total - 1);
            if (n <= 0) break;
            total += (size_t)n;
            resp[total] = '\0';

            char *scan = resp;
            int complete_responses = 0;
            while ((scan = strstr(scan, "200 OK")) != NULL) {
                complete_responses++;
                scan += 6;
            }
            responses_found = complete_responses;
        }
        success += responses_found;
    }

    neverc_tcp_close(conn);
    uint64_t elapsed = now_us() - start;

    int total_requests = batch_size * batches;
    double rps = (double)success / ((double)elapsed / 1000000.0);

    printf("  pipelined: %d requests in %d batches\n",
           total_requests, batches);
    printf("  success: %d, elapsed: %.2f ms\n",
           success, (double)elapsed / 1000.0);
    printf("  pipelining throughput: %.0f req/s\n", rps);

    check_true("pipeline success is 100%", success == total_requests);
}

/* ===== Helpers ===== */

static int wait_for_http_ready(int port, int attempts) {
    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    for (int i = 0; i < attempts; i++) {
        const char *err = NULL;
        neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &err);
        if (conn) {
            neverc_tcp_set_timeout(conn, 1000);
            const char *req =
                "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            neverc_tcp_write(conn, req, strlen(req));
            char resp[512];
            int n = neverc_tcp_read(conn, resp, sizeof(resp) - 1);
            neverc_tcp_close(conn);
            if (n > 0) {
                resp[n] = '\0';
                if (strstr(resp, "200 OK")) return 0;
            }
        }
#ifdef _WIN32
        Sleep(200);
#else
        usleep(200000);
#endif
    }
    return -1;
}

/* ===== Main ===== */

int main(void) {
    printf("=== NeverC HTTP Benchmark ===\n");

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

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
    Sleep(500);
#else
    pthread_t srv;
    pthread_create(&srv, NULL, server_thread, NULL);
#endif

    if (wait_for_http_ready(g_server_port, 30) != 0) {
        printf("  FAIL: server did not become ready\n");
        printf("0/0 benchmarks passed, startup FAILED\n");
        neverc_http_shutdown();
#ifdef _WIN32
        WaitForSingleObject(srv, 3000);
        CloseHandle(srv);
#else
        pthread_join(srv, NULL);
#endif
        return 1;
    }

    test_connection_rate();
    test_throughput();
    test_disconnect_recovery();
#ifndef _WIN32
    test_concurrent_connections();
    test_pipelining_bench();
#endif

    neverc_http_shutdown();

#ifdef _WIN32
    WaitForSingleObject(srv, 5000);
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
