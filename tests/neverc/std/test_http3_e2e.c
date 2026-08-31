#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "neverc/std/net/http.h"
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/http3.h"
#include "neverc/std/net/udp.h"
#include "neverc/std/thread.h"
#include "neverc/std/time.h"
#include "network_test_support.h"

#include <stdatomic.h>
#include <limits.h>
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

typedef struct {
    neverc_http3_server_t *server;
    const neverc_network_test_files_t *files;
    char address[64];
    int result;
} http3_test_server_t;

typedef struct {
    neverc_http_unified_server_t *server;
    const neverc_network_test_files_t *files;
    char address[64];
    int result;
    atomic_int completed;
} http3_test_unified_t;

typedef struct {
    char url[128];
    neverc_http3_client_config_t config;
    neverc_http_response_t *response;
} http3_test_slow_client_t;

static atomic_int http3_test_slow_entered;

static void http3_test_handler(neverc_http_request_t *request,
                               neverc_http_response_writer_t *writer) {
    neverc_http_set_header(writer, "X-NeverC", "h3");
    neverc_http_set_trailer(writer, "X-Finished", "yes");
    if (strcmp(request->path, "/slow") == 0) {
        atomic_store_explicit(&http3_test_slow_entered, 1,
                              memory_order_release);
        neverc_time_sleep(250 * NEVERC_TIME_MILLISECOND);
        (void)neverc_http_write_string(writer, "drained");
    } else if (strcmp(request->method, "POST") == 0) {
        neverc_http_set_status(writer, 201);
        (void)neverc_http_write(writer, request->body, request->body_len);
    } else {
        (void)neverc_http_write_string(writer, "hello over h3");
    }
}

static void http3_test_server_task(void *context) {
    http3_test_server_t *test = (http3_test_server_t *)context;
    test->result = neverc_http3_listen_and_serve(
        test->address, test->server, test->files->server_cert,
        test->files->server_key);
}

static void http3_test_unified_task(void *context) {
    http3_test_unified_t *test = (http3_test_unified_t *)context;
    test->result = neverc_http_unified_server_listen_and_serve(
        test->server, test->address, test->files->server_cert,
        test->files->server_key);
    atomic_store_explicit(&test->completed, 1, memory_order_release);
}

static void http3_test_slow_client_task(void *context) {
    http3_test_slow_client_t *test =
        (http3_test_slow_client_t *)context;
    test->response = neverc_http3_get_with_config(test->url, &test->config);
}

static int http3_test_free_udp_port(void) {
    const char *error = NULL;
    neverc_udp_conn_t *probe = neverc_udp_listen("127.0.0.1:0", &error);
    if (!probe) return -1;
    neverc_udp_addr_t local;
    int port = neverc_udp_local_addr(probe, &local) == 0
        ? (int)local.port : -1;
    neverc_udp_close(probe);
    return port;
}

#ifdef _WIN32
typedef SOCKET http3_test_socket_t;
#define HTTP3_TEST_INVALID_SOCKET INVALID_SOCKET
#else
typedef int http3_test_socket_t;
#define HTTP3_TEST_INVALID_SOCKET (-1)
#endif

static int http3_test_native_socket_startup(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

static void http3_test_native_socket_cleanup(void) {
#ifdef _WIN32
    (void)WSACleanup();
#endif
}

static void http3_test_native_socket_close(http3_test_socket_t handle) {
    if (handle == HTTP3_TEST_INVALID_SOCKET) return;
#ifdef _WIN32
    (void)closesocket(handle);
#else
    (void)close(handle);
#endif
}

static http3_test_socket_t http3_test_bind_exclusive(
    int socket_type, int requested_port, int *bound_port) {
    int protocol = socket_type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    http3_test_socket_t handle =
        socket(AF_INET, socket_type, protocol);
    if (handle == HTTP3_TEST_INVALID_SOCKET)
        return HTTP3_TEST_INVALID_SOCKET;

#ifdef _WIN32
    int exclusive = 1;
    if (setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   (const char *)&exclusive, (int)sizeof(exclusive)) != 0) {
        http3_test_native_socket_close(handle);
        return HTTP3_TEST_INVALID_SOCKET;
    }
#endif

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)requested_port);
    if (bind(handle, (const struct sockaddr *)&address,
             (int)sizeof(address)) != 0 ||
        (socket_type == SOCK_STREAM && listen(handle, 1) != 0)) {
        http3_test_native_socket_close(handle);
        return HTTP3_TEST_INVALID_SOCKET;
    }

    if (bound_port) {
        struct sockaddr_in actual;
        memset(&actual, 0, sizeof(actual));
#ifdef _WIN32
        int actual_length = (int)sizeof(actual);
#else
        socklen_t actual_length = (socklen_t)sizeof(actual);
#endif
        if (getsockname(handle, (struct sockaddr *)&actual,
                        &actual_length) != 0) {
            http3_test_native_socket_close(handle);
            return HTTP3_TEST_INVALID_SOCKET;
        }
        *bound_port = (int)ntohs(actual.sin_port);
    }
    return handle;
}

static int http3_test_free_dual_protocol_port(void) {
    for (int attempt = 0; attempt < 64; attempt++) {
        int port = -1;
        http3_test_socket_t tcp =
            http3_test_bind_exclusive(SOCK_STREAM, 0, &port);
        if (tcp == HTTP3_TEST_INVALID_SOCKET) continue;
        http3_test_socket_t udp =
            http3_test_bind_exclusive(SOCK_DGRAM, port, NULL);
        if (udp != HTTP3_TEST_INVALID_SOCKET) {
            http3_test_native_socket_close(udp);
            http3_test_native_socket_close(tcp);
            return port;
        }
        http3_test_native_socket_close(tcp);
    }
    return -1;
}

static int http3_test_wait_unified_running(
    neverc_http_unified_server_t *server, http3_test_unified_t *test) {
    for (int attempt = 0; attempt < 500; attempt++) {
        if (neverc_http_unified_server_is_running(server)) return 0;
        if (atomic_load_explicit(&test->completed, memory_order_acquire))
            return -1;
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    }
    return -1;
}

static int http3_test_wait_unified_completed(
    neverc_http_unified_server_t *server, http3_test_unified_t *test,
    int *saw_running) {
    for (int attempt = 0; attempt < 500; attempt++) {
        if (neverc_http_unified_server_is_running(server))
            *saw_running = 1;
        if (atomic_load_explicit(&test->completed, memory_order_acquire))
            return 0;
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    }
    if (neverc_http_unified_server_is_running(server))
        *saw_running = 1;
    return atomic_load_explicit(&test->completed, memory_order_acquire)
        ? 0 : -1;
}

static int http3_test_wait_running(neverc_http3_server_t *server) {
    for (int attempt = 0; attempt < 500; attempt++) {
        if (neverc_http3_server_is_running(server)) return 0;
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    }
    return -1;
}

static void http3_test_end_to_end(void) {
    neverc_network_test_files_t files;
    CHECK(neverc_network_test_write_certs("http3-e2e", &files) == 0);
    int port = http3_test_free_udp_port();
    CHECK(port > 0);
    neverc_http_mux_t *mux = neverc_http_new_mux();
    CHECK(mux != NULL);
    neverc_http_mux_handle(mux, "/echo", http3_test_handler);
    http3_test_server_t test;
    memset(&test, 0, sizeof(test));
    test.files = &files;
    test.result = -1;
    (void)snprintf(test.address, sizeof(test.address), "127.0.0.1:%d", port);
    test.server = neverc_http3_server_create(mux);
    CHECK(test.server != NULL);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    CHECK(neverc_thread_executor_submit(executor, http3_test_server_task,
                                         &test) == NEVERC_THREAD_OK);
    CHECK(http3_test_wait_running(test.server) == 0);

    char url[128];
    (void)snprintf(url, sizeof(url), "https://127.0.0.1:%d/echo", port);
    neverc_http3_client_config_t config =
        neverc_http3_client_config_default();
    config.server_name = "localhost";
    config.root_cert_file = files.ca;
    neverc_http_response_t *get = neverc_http3_get_with_config(url, &config);
    if (get && get->error) printf("  GET error: %s\n", get->error);
    CHECK(get != NULL);
    CHECK(get && get->error == NULL);
    CHECK(get && get->status_code == 200);
    CHECK(get && get->body_len == 13U &&
          memcmp(get->body, "hello over h3", 13U) == 0);
    char header[64];
    CHECK(get && neverc_http_response_header(get, "X-NeverC", header,
                                              sizeof(header)) != NULL &&
          strcmp(header, "h3") == 0);
    CHECK(get && get->trailers && strstr(get->trailers, "x-finished: yes"));
    neverc_http_response_free(get);

    static const char body[] = "signed telemetry";
    neverc_http_response_t *post = neverc_http3_post_with_config(
        url, "application/octet-stream", body, sizeof(body) - 1U, &config);
    if (post && post->error) printf("  POST error: %s\n", post->error);
    CHECK(post != NULL);
    CHECK(post && post->error == NULL);
    CHECK(post && post->status_code == 201);
    CHECK(post && post->body_len == sizeof(body) - 1U &&
          memcmp(post->body, body, sizeof(body) - 1U) == 0);
    neverc_http_response_free(post);

    config.server_name = "wrong.example";
    neverc_http_response_t *rejected = neverc_http3_get_with_config(url,
                                                                      &config);
    CHECK(rejected != NULL && rejected->error != NULL);
    neverc_http_response_free(rejected);

    neverc_http3_server_stop(test.server);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_http3_server_destroy(test.server);
    neverc_http_mux_free(mux);
    neverc_network_test_remove_certs(&files);
}

static void http3_test_config_validation(void) {
    neverc_http3_client_config_t config =
        neverc_http3_client_config_default();
    CHECK(config.server_name == NULL);
    CHECK(config.root_cert_file == NULL);
    CHECK(config.insecure_skip_verify == 0);
    config.insecure_skip_verify = 2;
    neverc_http_response_t *response = neverc_http3_get_with_config(
        "https://127.0.0.1:1/", &config);
    CHECK(response != NULL && response->error != NULL &&
          strstr(response->error, "invalid") != NULL);
    neverc_http_response_free(response);
}

static void http3_test_unified_occupied_port(int occupied_socket_type) {
    const char *kind = occupied_socket_type == SOCK_STREAM ? "tcp" : "udp";
    int port = -1;
    http3_test_socket_t holder = http3_test_bind_exclusive(
        occupied_socket_type, 0, &port);
    CHECK(holder != HTTP3_TEST_INVALID_SOCKET);
    CHECK(port > 0);
    if (holder == HTTP3_TEST_INVALID_SOCKET || port <= 0) {
        http3_test_native_socket_close(holder);
        return;
    }

    int peer_socket_type = occupied_socket_type == SOCK_STREAM
        ? SOCK_DGRAM : SOCK_STREAM;
    http3_test_socket_t peer =
        http3_test_bind_exclusive(peer_socket_type, port, NULL);
    CHECK(peer != HTTP3_TEST_INVALID_SOCKET);
    http3_test_native_socket_close(peer);
    if (peer == HTTP3_TEST_INVALID_SOCKET) {
        http3_test_native_socket_close(holder);
        return;
    }

    neverc_network_test_files_t files;
    memset(&files, 0, sizeof(files));
    const char *prefix = occupied_socket_type == SOCK_STREAM
        ? "http-unified-tcp-occupied" : "http-unified-udp-occupied";
    int wrote_certs = neverc_network_test_write_certs(prefix, &files);
    CHECK(wrote_certs == 0);
    if (wrote_certs != 0) {
        http3_test_native_socket_close(holder);
        return;
    }

    neverc_http_mux_t *mux = neverc_http_new_mux();
    CHECK(mux != NULL);
    neverc_http_unified_server_t *server = mux
        ? neverc_http_unified_server_create(mux) : NULL;
    CHECK(server != NULL);
    neverc_thread_executor_t *executor = server
        ? neverc_thread_executor_create(1U, 1U) : NULL;
    CHECK(executor != NULL);

    http3_test_unified_t test;
    memset(&test, 0, sizeof(test));
    test.server = server;
    test.files = &files;
    test.result = -1;
    atomic_init(&test.completed, 0);
    (void)snprintf(test.address, sizeof(test.address),
                   "127.0.0.1:%d", port);

    int submitted = 0;
    if (executor) {
        int submit_result = neverc_thread_executor_submit(
            executor, http3_test_unified_task, &test);
        CHECK(submit_result == NEVERC_THREAD_OK);
        submitted = submit_result == NEVERC_THREAD_OK;
    }
    int saw_running = 0;
    int completed_in_time = submitted &&
        http3_test_wait_unified_completed(server, &test, &saw_running) == 0;
    CHECK(completed_in_time);
    if (submitted && !completed_in_time) {
        printf("occupied=%s completed=0 result=%d running=%d "
               "saw_running=%d bound=%d\n", kind, INT_MIN,
               server ? neverc_http_unified_server_is_running(server) : 0,
               saw_running,
               server ? neverc_http_unified_server_bound_port(server) : -1);
        fflush(stdout);
        http3_test_native_socket_close(holder);
        _Exit(1);
    }
    if (executor)
        CHECK(neverc_thread_executor_shutdown(executor) ==
              NEVERC_THREAD_OK);

    http3_test_socket_t released_peer =
        http3_test_bind_exclusive(peer_socket_type, port, NULL);
    CHECK(released_peer != HTTP3_TEST_INVALID_SOCKET);
    http3_test_native_socket_close(released_peer);

    int completed = submitted && atomic_load_explicit(
        &test.completed, memory_order_acquire);
    int running = server && neverc_http_unified_server_is_running(server);
    int bound_port = server
        ? neverc_http_unified_server_bound_port(server) : -1;
    printf("occupied=%s completed=%d result=%d running=%d "
           "saw_running=%d bound=%d\n",
           kind, completed, completed ? test.result : INT_MIN,
           running, saw_running, bound_port);
    fflush(stdout);
    CHECK(completed);
    CHECK(test.result == -1);
    CHECK(server && !running);
    CHECK(!saw_running);
    CHECK(server && bound_port == -1);

    http3_test_native_socket_close(holder);
    neverc_thread_executor_free(executor);
    neverc_http_unified_server_destroy(server);
    neverc_http_mux_free(mux);
    neverc_network_test_remove_certs(&files);
}

static void http3_test_unified_end_to_end(void) {
    neverc_network_test_files_t files;
    CHECK(neverc_network_test_write_certs("http-unified", &files) == 0);
    int port = http3_test_free_dual_protocol_port();
    CHECK(port > 0);
    neverc_http_mux_t *mux = neverc_http_new_mux();
    CHECK(mux != NULL);
    if (mux) {
        neverc_http_mux_handle(mux, "/echo", http3_test_handler);
        neverc_http_mux_handle(mux, "/slow", http3_test_handler);
    }
    neverc_http_unified_server_t *server =
        neverc_http_unified_server_create(mux);
    CHECK(server != NULL);
    http3_test_unified_t test;
    memset(&test, 0, sizeof(test));
    test.server = server;
    test.files = &files;
    test.result = -1;
    atomic_init(&test.completed, 0);
    (void)snprintf(test.address, sizeof(test.address), "127.0.0.1:%d", port);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (executor && server)
        CHECK(neverc_thread_executor_submit(executor,
                                             http3_test_unified_task,
                                             &test) == NEVERC_THREAD_OK);
    CHECK(server && http3_test_wait_unified_running(server, &test) == 0);
    CHECK(server && neverc_http_unified_server_bound_port(server) == port);

    char url[128];
    (void)snprintf(url, sizeof(url), "https://localhost:%d/echo", port);
    neverc_http_client_config_t http_config =
        neverc_http_client_config_default();
    http_config.root_cert_file = files.ca;
    http_config.timeout_ms = 5000;
    neverc_http_client_t *http_client = neverc_http_client_new(&http_config);
    neverc_http_response_t *response = http_client
        ? neverc_http_client_do(http_client, "GET", url, NULL, NULL, 0U)
        : NULL;
    CHECK(response != NULL && response->error == NULL &&
          response->status_code == 200 && response->body_len == 13U);
    char alt_svc[128];
    CHECK(response && neverc_http_response_header(
              response, "Alt-Svc", alt_svc, sizeof(alt_svc)) != NULL &&
          strstr(alt_svc, "h3=\":") != NULL);
    neverc_http_response_free(response);
    neverc_http_client_free(http_client);

    neverc_h2_client_config_t h2_config = neverc_h2_client_config_default();
    h2_config.root_cert_file = files.ca;
    h2_config.timeout_ms = 5000;
    const char *h2_error = NULL;
    neverc_h2_client_t *h2 = neverc_h2_client_dial(
        test.address, "localhost", 1, &h2_config, &h2_error);
    if (!h2 && h2_error) printf("  unified h2 dial error: %s\n", h2_error);
    CHECK(h2 != NULL);
    neverc_h2_response_t *h2_response = h2
        ? neverc_h2_client_do(h2, "GET", "/echo", NULL, 0U, NULL, 0U)
        : NULL;
    CHECK(h2_response != NULL && h2_response->error == NULL &&
          h2_response->status_code == 200 &&
          h2_response->body_length == 13U);
    int h2_alt_svc = 0;
    if (h2_response)
        for (size_t i = 0; i < h2_response->header_count; i++)
            if (strcmp(h2_response->headers[i].name, "alt-svc") == 0 &&
                strstr(h2_response->headers[i].value, "h3=\":") != NULL)
                h2_alt_svc = 1;
    CHECK(h2_alt_svc);
    neverc_h2_response_free(h2_response);
    neverc_h2_client_free(h2);

    neverc_http3_client_config_t h3_config =
        neverc_http3_client_config_default();
    h3_config.server_name = "localhost";
    h3_config.root_cert_file = files.ca;
    (void)snprintf(url, sizeof(url), "https://127.0.0.1:%d/echo", port);
    response = neverc_http3_get_with_config(url, &h3_config);
    if (response && response->error)
        printf("  unified h3 GET error: %s\n", response->error);
    CHECK(response != NULL && response->error == NULL &&
          response->status_code == 200 && response->body_len == 13U);
    neverc_http_response_free(response);

    http3_test_slow_client_t slow;
    memset(&slow, 0, sizeof(slow));
    slow.config = h3_config;
    (void)snprintf(slow.url, sizeof(slow.url),
                   "https://127.0.0.1:%d/slow", port);
    atomic_store_explicit(&http3_test_slow_entered, 0,
                          memory_order_release);
    neverc_thread_executor_t *slow_executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(slow_executor != NULL);
    if (slow_executor)
        CHECK(neverc_thread_executor_submit(slow_executor,
                                             http3_test_slow_client_task,
                                             &slow) == NEVERC_THREAD_OK);
    for (int attempt = 0; attempt < 2000 &&
         !atomic_load_explicit(&http3_test_slow_entered,
                               memory_order_acquire); attempt++)
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    CHECK(atomic_load_explicit(&http3_test_slow_entered,
                               memory_order_acquire));
    neverc_http_unified_server_shutdown(server);
    if (slow_executor)
        CHECK(neverc_thread_executor_shutdown(slow_executor) ==
              NEVERC_THREAD_OK);
    if (slow.response && slow.response->error)
        printf("  slow drain error: %s\n", slow.response->error);
    else if (slow.response &&
             (slow.response->status_code != 200 ||
              slow.response->body_len != 7U))
        printf("  slow drain response: status=%d body_len=%zu\n",
               slow.response->status_code, slow.response->body_len);
    CHECK(slow.response != NULL && slow.response->error == NULL &&
          slow.response->status_code == 200 && slow.response->body_len == 7U &&
          memcmp(slow.response->body, "drained", 7U) == 0);
    neverc_http_response_free(slow.response);
    neverc_thread_executor_free(slow_executor);
    if (executor)
        CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    CHECK(server && !neverc_http_unified_server_is_running(server));
    neverc_thread_executor_free(executor);
    neverc_http_unified_server_destroy(server);
    neverc_http_mux_free(mux);
    neverc_network_test_remove_certs(&files);
}

int main(void) {
    printf("HTTP/3 end-to-end test suite:\n");
    int native_socket_ready = http3_test_native_socket_startup() == 0;
    CHECK(native_socket_ready);
    http3_test_config_validation();
    http3_test_end_to_end();
    http3_test_unified_end_to_end();
    http3_test_unified_occupied_port(SOCK_DGRAM);
    http3_test_unified_occupied_port(SOCK_STREAM);
    if (native_socket_ready) http3_test_native_socket_cleanup();
    printf("http3-e2e: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
