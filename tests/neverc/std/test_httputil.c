#include "neverc/std/net/http/httputil.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/crypto/tls.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <strings.h>
#include <unistd.h>
#endif

static int tests_run;
static int tests_failed;

#define CHECK(name, condition)                                             \
    do {                                                                   \
        tests_run++;                                                       \
        if (!(condition)) {                                                \
            tests_failed++;                                                \
            printf("  FAIL: %s\n", name);                                 \
        }                                                                  \
    } while (0)

static void check_contains(const char *name, const char *value,
                           const char *expected) {
    CHECK(name, value && expected && strstr(value, expected) != NULL);
}

static int count_occurrences(const char *value, const char *wanted) {
    if (!value || !wanted || wanted[0] == '\0') return 0;
    int count = 0;
    size_t length = strlen(wanted);
    while ((value = strstr(value, wanted)) != NULL) {
        count++;
        value += length;
    }
    return count;
}

static void test_sleep_ms(unsigned milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000U);
#endif
}

static int test_wait_finished(
    const atomic_int *finished, unsigned timeout_ms) {
    unsigned waited = 0;
    while (!atomic_load_explicit(finished, memory_order_acquire)) {
        if (waited >= timeout_ms) return -1;
        test_sleep_ms(10U);
        waited += 10U;
    }
    return 0;
}

static void test_thread_fatal(const char *message) {
    fprintf(stderr, "fatal test thread cleanup: %s\n", message);
    abort();
}

typedef struct {
    char *data;
    size_t length;
} raw_response_t;

static void raw_response_free(raw_response_t *response) {
    if (!response) return;
    free(response->data);
    response->data = NULL;
    response->length = 0;
}

static int tcp_write_all(neverc_tcp_conn_t *connection,
                         const void *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        int written = neverc_tcp_write(
            connection, (const char *)data + offset, length - offset);
        if (written <= 0 || (size_t)written > length - offset) return -1;
        offset += (size_t)written;
    }
    return 0;
}

static int raw_http_request(int port, const char *request,
                            size_t response_limit,
                            raw_response_t *response) {
    if (!request || !response || response_limit == 0) return -1;
    memset(response, 0, sizeof(*response));
    char address[64];
    int address_length = snprintf(
        address, sizeof(address), "127.0.0.1:%d", port);
    if (address_length < 0 ||
        (size_t)address_length >= sizeof(address))
        return -1;
    const char *error = NULL;
    neverc_tcp_conn_t *connection = neverc_tcp_dial(address, &error);
    if (!connection) return -1;
    if (neverc_tcp_set_timeout(connection, 5000) != 0 ||
        tcp_write_all(connection, request, strlen(request)) != 0) {
        neverc_tcp_close(connection);
        return -1;
    }

    size_t capacity = response_limit < 4096U ? response_limit : 4096U;
    response->data = (char *)malloc(capacity + 1U);
    if (!response->data) {
        neverc_tcp_close(connection);
        return -1;
    }
    for (;;) {
        if (response->length == capacity) {
            if (capacity == response_limit) {
                raw_response_free(response);
                neverc_tcp_close(connection);
                return -1;
            }
            size_t next = capacity > response_limit / 2U
                ? response_limit : capacity * 2U;
            char *grown = (char *)realloc(response->data, next + 1U);
            if (!grown) {
                raw_response_free(response);
                neverc_tcp_close(connection);
                return -1;
            }
            response->data = grown;
            capacity = next;
        }
        int received = neverc_tcp_read(
            connection, response->data + response->length,
            capacity - response->length);
        if (received == 0) break;
        if (received < 0 ||
            (size_t)received > capacity - response->length) {
            raw_response_free(response);
            neverc_tcp_close(connection);
            return -1;
        }
        response->length += (size_t)received;
    }
    response->data[response->length] = '\0';
    neverc_tcp_close(connection);
    return 0;
}

static const char *raw_response_body(const raw_response_t *response,
                                     size_t *body_length) {
    if (body_length) *body_length = 0;
    if (!response || !response->data) return NULL;
    const char *separator = strstr(response->data, "\r\n\r\n");
    if (!separator) return NULL;
    const char *body = separator + 4;
    if (body_length)
        *body_length = response->length -
            (size_t)(body - response->data);
    return body;
}

/* ---------------------------------------------------------------------- */
/* URL validation and stable legacy handlers.                             */
/* ---------------------------------------------------------------------- */

static void test_target_validation(void) {
    puts("[target_validation]");
    neverc_httputil_reverse_proxy_t *proxy =
        neverc_httputil_new_single_host_reverse_proxy(
            "https://[2001:db8::1]:8443/v2");
    CHECK("bracketed IPv6 target accepted", proxy != NULL);
    if (proxy) {
        CHECK("forwarded proto accepts https",
              neverc_httputil_proxy_set_forwarded_proto(
                  proxy, "https") == 0);
        CHECK("forwarded proto rejects arbitrary value",
              neverc_httputil_proxy_set_forwarded_proto(
                  proxy, "ftp") == -1);
        neverc_httputil_proxy_free(proxy);
    }

    static const char *invalid_targets[] = {
        "invalid",
        "http://",
        "http://:80",
        "http://host:",
        "http://host:0",
        "http://host:65536",
        "http://host:80tail",
        "http://user@host/",
        "http://host/path#fragment",
        "http://host/path?query=unsupported",
        "http://2001:db8::1/",
        "http://[2001:::1]/",
        "http://host/\r\nInjected: yes",
        "http://host/api/../admin",
        "http://host/api/..;/admin",
        "http://host/foo//bar",
    };
    for (size_t i = 0;
         i < sizeof(invalid_targets) / sizeof(invalid_targets[0]); i++) {
        proxy = neverc_httputil_new_single_host_reverse_proxy(
            invalid_targets[i]);
        CHECK("invalid target rejected", proxy == NULL);
        neverc_httputil_proxy_free(proxy);
    }

    char *long_host = (char *)malloc(320U);
    CHECK("long host allocation", long_host != NULL);
    if (long_host) {
        memcpy(long_host, "http://", 7U);
        memset(long_host + 7U, 'a', 300U);
        memcpy(long_host + 307U, ":80", 4U);
        proxy = neverc_httputil_new_single_host_reverse_proxy(long_host);
        CHECK("overlong host rejected without truncation", proxy == NULL);
        neverc_httputil_proxy_free(proxy);
        free(long_host);
    }

    char *long_path = (char *)malloc(2200U);
    CHECK("long target path allocation", long_path != NULL);
    if (long_path) {
        memcpy(long_path, "http://host/", 12U);
        memset(long_path + 12U, 'p', 2186U);
        long_path[2198] = '\0';
        proxy = neverc_httputil_new_single_host_reverse_proxy(long_path);
        CHECK("overlong target path rejected without truncation",
              proxy == NULL);
        neverc_httputil_proxy_free(proxy);
        free(long_path);
    }

    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *peer_tcp = NULL;
    CHECK("TLS context ownership pipe created",
          neverc_tcp_pipe(&client_tcp, &peer_tcp) == 0);
    neverc_tls_config_t *tls_config = neverc_tls_config_new();
    neverc_context_cancel_handle_t *cancel_handle = NULL;
    neverc_context_t *cancelled_context =
        neverc_context_with_cancel_handle(NULL, &cancel_handle);
    CHECK("TLS context test state allocated",
          client_tcp && peer_tcp && tls_config &&
          cancelled_context && cancel_handle);
    if (client_tcp && peer_tcp && tls_config &&
        cancelled_context && cancel_handle) {
        neverc_tls_config_set_server_name(
            tls_config, "context.example");
        neverc_context_cancel_handle_cancel(cancel_handle);
        const char *tls_error = NULL;
        neverc_tls_conn_t *tls = neverc_tls_client_context(
            client_tcp, tls_config, cancelled_context, &tls_error);
        CHECK("cancelled verified TLS handshake is rejected",
              tls == NULL && tls_error != NULL);
        neverc_tls_close(tls);
        CHECK("failed TLS wrapper leaves caller TCP owned",
              neverc_tcp_write(client_tcp, "x", 1U) == 1);
        char marker = '\0';
        CHECK("caller TCP remains usable after TLS cancellation",
              neverc_tcp_read(peer_tcp, &marker, 1U) == 1 &&
              marker == 'x');

        neverc_context_cancel_handle_t *timeout_handle = NULL;
        neverc_context_t *timeout_context =
            neverc_context_with_timeout_handle(
                NULL, 50, &timeout_handle);
        CHECK("TLS handshake timeout context allocated",
              timeout_context && timeout_handle);
        if (timeout_context && timeout_handle) {
            tls_error = NULL;
            tls = neverc_tls_client_context(
                client_tcp, tls_config, timeout_context, &tls_error);
            CHECK("stalled TLS handshake obeys context deadline",
                  tls == NULL &&
                  neverc_context_done(timeout_context));
            neverc_tls_close(tls);
            CHECK("timed-out TLS wrapper leaves caller TCP owned",
                  neverc_tcp_write(client_tcp, "y", 1U) == 1);
        }
        neverc_context_free(timeout_context);
        neverc_context_cancel_handle_free(timeout_handle);
    }
    neverc_context_free(cancelled_context);
    neverc_context_cancel_handle_free(cancel_handle);
    neverc_tls_config_free(tls_config);
    neverc_tcp_close(client_tcp);
    neverc_tcp_close(peer_tcp);
}

static void test_legacy_handler_binding(void) {
    puts("[legacy_handler_binding]");
    neverc_httputil_reverse_proxy_t *first =
        neverc_httputil_new_single_host_reverse_proxy(
            "http://127.0.0.1:1");
    neverc_httputil_reverse_proxy_t *second =
        neverc_httputil_new_single_host_reverse_proxy(
            "http://127.0.0.1:2");
    CHECK("legacy proxies created", first && second);
    if (!first || !second) {
        neverc_httputil_proxy_free(first);
        neverc_httputil_proxy_free(second);
        return;
    }
    neverc_http_handler_func_t first_handler =
        neverc_httputil_proxy_handler(first);
    neverc_http_handler_func_t second_handler =
        neverc_httputil_proxy_handler(second);
    CHECK("legacy handlers allocated", first_handler && second_handler);
    CHECK("legacy handlers bind distinct proxies",
          first_handler != second_handler);

    neverc_httputil_proxy_free(first);
    neverc_http_request_t request;
    memset(&request, 0, sizeof(request));
    request.method = "GET";
    request.path = "/";
    neverc_http_response_writer_t *writer =
        neverc_http_memory_writer_new();
    CHECK("legacy release writer allocated", writer != NULL);
    if (first_handler && writer) {
        first_handler(&request, writer);
        char *body = NULL;
        size_t body_length = 0;
        int status = neverc_http_memory_writer_result(
            writer, &body, &body_length);
        CHECK("released handler returns 502", status == 502);
        CHECK("released handler does not switch proxy",
              body && body_length > 0 &&
              strstr(body, "Bad Gateway") != NULL);
        free(body);
    }
    neverc_http_memory_writer_free(writer);

    neverc_context_cancel_handle_t *cancel_handle = NULL;
    request.context =
        neverc_context_with_cancel_handle(NULL, &cancel_handle);
    writer = neverc_http_memory_writer_new();
    CHECK("proxy cancellation state allocated",
          request.context && cancel_handle && writer);
    if (request.context && cancel_handle && second_handler && writer) {
        neverc_context_cancel_handle_cancel(cancel_handle);
        second_handler(&request, writer);
        int status = neverc_http_memory_writer_result(
            writer, NULL, NULL);
        CHECK("cancelled proxy request uses error path",
              status == 502);
    }
    neverc_http_memory_writer_free(writer);
    neverc_context_free(request.context);
    neverc_context_cancel_handle_free(cancel_handle);
    neverc_httputil_proxy_free(second);
}

/* ---------------------------------------------------------------------- */
/* Dump API regression coverage.                                          */
/* ---------------------------------------------------------------------- */

static void test_dump_apis(void) {
    puts("[dump_apis]");
    neverc_http_request_t request;
    memset(&request, 0, sizeof(request));
    request.method = "POST";
    request.path = "/api/users";
    request.query = "page=1";
    request.http_version = "HTTP/1.1";
    request.host = "example.test";
    request.body = "payload";
    request.body_len = 7U;

    char *dump = neverc_httputil_dump_request(&request, 1);
    CHECK("request dump allocated", dump != NULL);
    if (dump) {
        check_contains("request dump path", dump,
                       "POST /api/users?page=1 HTTP/1.1");
        check_contains("request dump host", dump,
                       "Host: example.test");
        check_contains("request dump content length", dump,
                       "Content-Length: 7\r\n");
        check_contains("request dump body", dump, "payload");
        CHECK("request dump has one Content-Length",
              count_occurrences(dump, "Content-Length:") == 1);
        free(dump);
    }

    static const char raw_host_headers[] =
        "Host\0only.example\0Accept\0*/*\0";
    request.host = NULL;
    request.raw_headers = raw_host_headers;
    request.nheaders = 2;
    request.body = NULL;
    request.body_len = 0;
    dump = neverc_httputil_dump_request(&request, 0);
    CHECK("raw-header Host dump allocated", dump != NULL);
    if (dump) {
        check_contains("Host taken from raw headers", dump,
                       "Host: only.example");
        check_contains("ordinary raw header kept", dump, "Accept: */*");
        free(dump);
    }
    request.host = "example.test";
    request.raw_headers = NULL;
    request.nheaders = 0;
    request.body = "payload";
    request.body_len = 7U;

    request.path = "/";
    request.query = NULL;
    request.body = NULL;
    request.body_len = 0;
    request.host = "::1";
    dump = neverc_httputil_dump_request(&request, 0);
    CHECK("IPv6 request dump allocated", dump != NULL);
    if (dump) {
        check_contains("IPv6 Host is bracketed", dump, "Host: [::1]\r\n");
        CHECK("IPv6 Host is not emitted unbracketed",
              strstr(dump, "Host: ::1\r\n") == NULL);
        free(dump);
    }
    request.host = "[::1]";
    dump = neverc_httputil_dump_request(&request, 0);
    CHECK("bracketed IPv6 dump allocated", dump != NULL);
    if (dump) {
        check_contains("already-bracketed IPv6 Host kept", dump,
                       "Host: [::1]\r\n");
        CHECK("already-bracketed IPv6 Host is not wrapped twice",
              strstr(dump, "Host: [[::1]]") == NULL);
        free(dump);
    }
    request.host = "192.168.1.1";
    dump = neverc_httputil_dump_request(&request, 0);
    CHECK("IPv4 request dump allocated", dump != NULL);
    if (dump) {
        check_contains("IPv4 Host is not bracketed", dump,
                       "Host: 192.168.1.1\r\n");
        free(dump);
    }
    request.host = "example.test";
    request.body = "payload";
    request.body_len = 7U;

    char *long_path = (char *)malloc(9002U);
    CHECK("long dump path allocated", long_path != NULL);
    if (long_path) {
        long_path[0] = '/';
        memset(long_path + 1U, 'p', 9000U);
        long_path[9001] = '\0';
        request.method = "GET";
        request.path = long_path;
        request.query = NULL;
        request.body = NULL;
        request.body_len = 0;
        dump = neverc_httputil_dump_request(&request, 0);
        CHECK("request dump grows dynamically",
              dump && strstr(dump, long_path) != NULL);
        free(dump);
        free(long_path);
    }

    dump = neverc_httputil_dump_request_out(
        "POST", "/data", "Content-Type: text/plain\r\n",
        "hello", 5U);
    CHECK("outbound dump allocated", dump != NULL);
    if (dump) {
        check_contains("outbound dump content length",
                       dump, "Content-Length: 5\r\n");
        check_contains("outbound dump body", dump, "hello");
        CHECK("outbound dump synthesizes one Content-Length",
              count_occurrences(dump, "Content-Length:") == 1);
        free(dump);
    }
    dump = neverc_httputil_dump_request_out(
        "POST", "/data", "Content-Type: text/plain\r\n",
        NULL, 0U);
    CHECK("empty POST outbound dump allocated", dump != NULL);
    if (dump) {
        check_contains("empty POST dump Content-Length 0",
                       dump, "Content-Length: 0\r\n");
        free(dump);
    }
    dump = neverc_httputil_dump_request_out(
        "GET", "/", "Content-Type: text/plain\r\n",
        NULL, 0U);
    CHECK("GET outbound dump allocated", dump != NULL);
    if (dump) {
        CHECK("GET+Content-Type dump omits Content-Length",
              strstr(dump, "Content-Length:") == NULL);
        free(dump);
    }
    dump = neverc_httputil_dump_request_out(
        "POST", "/data", "Transfer-Encoding: chunked\r\n",
        "5\r\nhello\r\n0\r\n\r\n", 16U);
    CHECK("outbound dump with TE allocated", dump != NULL);
    if (dump) {
        CHECK("outbound dump does not add Content-Length under TE",
              strstr(dump, "Content-Length:") == NULL);
        check_contains("outbound dump keeps Transfer-Encoding", dump,
                       "Transfer-Encoding: chunked");
        free(dump);
    }
    {
        neverc_http_request_t chunked_req;
        memset(&chunked_req, 0, sizeof(chunked_req));
        chunked_req.method = "POST";
        chunked_req.path = "/data";
        chunked_req.http_version = "HTTP/1.1";
        chunked_req.host = "example.test";
        chunked_req.body = "hello";
        chunked_req.body_len = 5U;
        static const char te_headers[] = "Transfer-Encoding\0chunked\0";
        chunked_req.raw_headers = te_headers;
        chunked_req.nheaders = 1;
        dump = neverc_httputil_dump_request(&chunked_req, 1);
        CHECK("chunked request dump allocated", dump != NULL);
        if (dump) {
            CHECK("chunked request dump does not add Content-Length",
                  strstr(dump, "Content-Length:") == NULL);
            check_contains("chunked request dump keeps TE", dump,
                           "Transfer-Encoding: chunked");
            check_contains("chunked request dump frames decoded body", dump,
                           "5\r\nhello\r\n0\r\n\r\n");
            CHECK("chunked request dump does not emit identity body",
                  strstr(dump, "chunked\r\n\r\nhello") == NULL);
            free(dump);
        }
    }
    dump = neverc_httputil_dump_request_out(
        "POST", "/data", "Content-Length: 5\r\nAccept: */*\r\n",
        "hello", 5U);
    CHECK("outbound dump with existing length allocated", dump != NULL);
    if (dump) {
        CHECK("outbound dump does not duplicate Content-Length",
              count_occurrences(dump, "Content-Length:") == 1);
        check_contains("outbound dump keeps Accept", dump, "Accept: */*");
        check_contains("outbound dump keeps body", dump, "hello");
        free(dump);
    }
    CHECK("outbound dump rejects missing body",
          neverc_httputil_dump_request_out(
              "POST", "/", NULL, NULL, 1U) == NULL);
    request.path = "/";
    request.nheaders = 1;
    request.raw_headers = NULL;
    CHECK("request dump rejects headers without storage",
          neverc_httputil_dump_request(&request, 0) == NULL);
    request.path = "/x\r\nHost: evil";
    request.nheaders = 0;
    request.raw_headers = NULL;
    CHECK("request dump rejects CRLF in path",
          neverc_httputil_dump_request(&request, 0) == NULL);
    request.path = "/";
    request.http_version = "HTTP/1.1 extra";
    CHECK("request dump rejects SP in http_version",
          neverc_httputil_dump_request(&request, 0) == NULL);
    request.http_version = "HTTP/1.1";
    CHECK("outbound dump rejects CRLF in URL",
          neverc_httputil_dump_request_out(
              "GET", "/x\r\nHost: evil", NULL, NULL, 0) == NULL);
    CHECK("outbound dump rejects SP in method",
          neverc_httputil_dump_request_out(
              "GET /evil HTTP/1.1", "/", NULL, NULL, 0) == NULL);
    CHECK("outbound dump rejects SP in URL",
          neverc_httputil_dump_request_out(
              "GET", "/evil HTTP/1.1 /", NULL, NULL, 0) == NULL);
    CHECK("outbound dump rejects HTAB in URL",
          neverc_httputil_dump_request_out(
              "GET", "/evil\tHTTP/1.1", NULL, NULL, 0) == NULL);
    CHECK("outbound dump rejects header smuggling",
          neverc_httputil_dump_request_out(
              "GET", "/", "X-A: 1\r\n\r\nGET /x HTTP/1.1\r\n",
              NULL, 0) == NULL);
    CHECK("outbound dump rejects bare LF in headers",
          neverc_httputil_dump_request_out(
              "GET", "/", "X-A: 1\nX-B: 2\r\n", NULL, 0) == NULL);
    dump = neverc_httputil_dump_request_out(
        "GET", "/", "Accept: */*", NULL, 0);
    CHECK("outbound dump without trailing CRLF allocated", dump != NULL);
    if (dump) {
        check_contains("outbound dump terminates the last header line",
                       dump, "Accept: */*\r\n\r\n");
        free(dump);
    }
    request.nheaders = 0;

    neverc_http_response_writer_t *writer =
        neverc_http_memory_writer_new();
    CHECK("memory writer allocated", writer != NULL);
    if (writer) {
        neverc_http_set_status(writer, 201);
        CHECK("duplicate-safe header append succeeds",
              neverc_http_add_header(
                  writer, "Set-Cookie", "one=1") == 0 &&
              neverc_http_add_header(
                  writer, "Set-Cookie", "two=2") == 0);
        CHECK("writer-managed content length rejects raw append",
              neverc_http_add_header(
                  writer, "Content-Length", "5") == -1);
        CHECK("content length metadata accepted",
              neverc_http_set_content_length(writer, 5U) == 0);
        (void)neverc_http_write_string(writer, "stale");
        CHECK("unsent response resets",
              neverc_http_reset_response(writer) == 0);
        neverc_http_set_status(writer, 598);
        (void)neverc_http_write_string(writer, "fresh");
        char *body = NULL;
        size_t body_length = 0;
        int status = neverc_http_memory_writer_result(
            writer, &body, &body_length);
        CHECK("reset removes backend status and body",
              status == 598 && body && body_length == 5U &&
              memcmp(body, "fresh", 5U) == 0);
        free(body);
        neverc_http_memory_writer_free(writer);
    }
}

/* ---------------------------------------------------------------------- */
/* Independent HTTP server harness.                                       */
/* ---------------------------------------------------------------------- */

typedef struct {
    neverc_http_server_t *server;
    int result;
    int thread_started;
    atomic_int thread_finished;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} test_server_t;

#if defined(_WIN32)
static DWORD WINAPI test_server_thread(LPVOID argument) {
#else
static void *test_server_thread(void *argument) {
#endif
    test_server_t *server = (test_server_t *)argument;
    server->result = neverc_http_server_listen_and_serve(
        server->server, "127.0.0.1:0");
    atomic_store_explicit(
        &server->thread_finished, 1, memory_order_release);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int test_server_join(test_server_t *test, unsigned timeout_ms) {
    if (!test || !test->thread_started) return 0;
#if defined(_WIN32)
    DWORD wait_result = WaitForSingleObject(test->thread, timeout_ms);
    if (wait_result != WAIT_OBJECT_0) return -1;
    if (!CloseHandle(test->thread)) return -1;
#else
    /* POSIX has no portable timed join. Wait on the completion flag with a
     * bound, then join only after the server thread has reached its return. */
    if (test_wait_finished(&test->thread_finished, timeout_ms) != 0)
        return -1;
    if (pthread_join(test->thread, NULL) != 0) return -1;
#endif
    test->thread_started = 0;
    return 0;
}

static int test_server_start(test_server_t *test,
                             neverc_http_mux_t *mux) {
    if (!test || !mux) return -1;
    memset(test, 0, sizeof(*test));
    test->result = -1;
    atomic_init(&test->thread_finished, 0);
    neverc_http_server_config_t config =
        neverc_http_server_config_default();
    config.workers = 2;
    config.max_connections = 32;
    config.read_timeout_ms = 5000;
    config.write_timeout_ms = 5000;
    config.idle_timeout_ms = 1000;
    config.shutdown_timeout_ms = 3000;
    test->server = neverc_http_server_new(mux, &config);
    if (!test->server) return -1;
#if defined(_WIN32)
    test->thread = CreateThread(
        NULL, 0, test_server_thread, test, 0, NULL);
    if (!test->thread) {
        neverc_http_server_free(test->server);
        test->server = NULL;
        return -1;
    }
#else
    if (pthread_create(
            &test->thread, NULL, test_server_thread, test) != 0) {
        neverc_http_server_free(test->server);
        test->server = NULL;
        return -1;
    }
#endif
    test->thread_started = 1;
    for (int attempt = 0; attempt < 500; attempt++) {
        int port = neverc_http_server_bound_port(test->server);
        if (port > 0) return port;
        if (atomic_load_explicit(
                &test->thread_finished, memory_order_acquire))
            break;
        test_sleep_ms(10U);
    }
    neverc_http_server_shutdown(test->server);
    if (test_server_join(test, 10000U) != 0)
        test_thread_fatal("server startup thread did not stop");
    neverc_http_server_free(test->server);
    test->server = NULL;
    return -1;
}

static void test_server_stop(test_server_t *test) {
    if (!test || !test->server) return;
    neverc_http_server_shutdown(test->server);
    if (test_server_join(test, 35000U) != 0)
        test_thread_fatal("server shutdown thread did not stop");
    neverc_http_server_free(test->server);
    test->server = NULL;
}

typedef struct {
    char id;
    const char *large_body;
    size_t large_body_length;
} backend_state_t;

static int request_header_count(const neverc_http_request_t *request,
                                const char *wanted) {
    int count = 0;
    const char *cursor = request->raw_headers;
    for (int i = 0; i < request->nheaders; i++) {
        const char *name = cursor;
        cursor += strlen(cursor) + 1U;
        const char *value = cursor;
        cursor += strlen(cursor) + 1U;
        (void)value;
#if defined(_WIN32)
        if (_stricmp(name, wanted) == 0) count++;
#else
        if (strcasecmp(name, wanted) == 0) count++;
#endif
    }
    return count;
}

static void backend_handler(neverc_http_request_t *request,
                            neverc_http_response_writer_t *writer,
                            void *context) {
    backend_state_t *state = (backend_state_t *)context;
    neverc_http_set_header(writer, "X-Backend", "yes");
    if (strcmp(request->path, "/chunked") == 0) {
        neverc_http_set_trailer(writer, "X-Chunk-End", "yes");
        neverc_http_enable_chunked(writer);
        (void)neverc_http_write(writer, "alpha", 5U);
        (void)neverc_http_write(writer, "-beta", 5U);
        (void)neverc_http_end_chunked(writer);
        return;
    }
    if (strcmp(request->path, "/large") == 0) {
        neverc_http_set_header(
            writer, "Content-Type", "application/octet-stream");
        (void)neverc_http_write(
            writer, state->large_body, state->large_body_length);
        return;
    }
    if (strcmp(request->path, "/cookies") == 0) {
        (void)neverc_http_add_header(
            writer, "Set-Cookie", "first=one; Path=/");
        (void)neverc_http_add_header(
            writer, "Set-Cookie", "second=two; Path=/");
        (void)neverc_http_write_string(writer, "cookies");
        return;
    }
    if (strcmp(request->path, "/not-modified") == 0) {
        neverc_http_set_status(writer, 304);
        (void)neverc_http_set_content_length(writer, 1234U);
        return;
    }
    if (strcmp(request->path, "/no-content") == 0) {
        neverc_http_set_status(writer, 204);
        (void)neverc_http_set_content_length(writer, 99U);
        return;
    }
    if (strcmp(request->path, "/informational") == 0) {
        neverc_http_set_status(writer, 103);
        (void)neverc_http_set_content_length(writer, 99U);
        return;
    }
    if (strcmp(request->path, "/upgrade") == 0) {
        neverc_http_set_status(writer, 101);
        neverc_http_set_header(writer, "Upgrade", "websocket");
        neverc_http_set_header(writer, "X-Leaked-Backend", "must-reset");
        return;
    }
    if (strcmp(request->path, "/framing") == 0) {
        const char *proto =
            neverc_http_request_header(request, "X-Forwarded-Proto");
        const char *forwarded =
            neverc_http_request_header(request, "Forwarded");
        const char *forwarded_for =
            neverc_http_request_header(request, "X-Forwarded-For");
        const char *forwarded_host =
            neverc_http_request_header(request, "X-Forwarded-Host");
        const char *real_ip =
            neverc_http_request_header(request, "X-Real-IP");
        const char *proxy_authorization =
            neverc_http_request_header(request, "Proxy-Authorization");
        const char *keep_alive =
            neverc_http_request_header(request, "Keep-Alive");
        const char *drop =
            neverc_http_request_header(request, "X-Drop");
        const char *keep =
            neverc_http_request_header(request, "X-Keep");
        (void)neverc_http_writef(
            writer,
            "%c cl=%d te=%d forwarded=%d xff=%d xrealip=%d "
            "proxyauth=%d keepalive=%d xdrop=%d "
            "xkeep=%d proto=%s xhost=%s body=%.*s",
            state->id,
            request_header_count(request, "Content-Length"),
            request_header_count(request, "Transfer-Encoding"),
            forwarded != NULL, forwarded_for != NULL, real_ip != NULL,
            proxy_authorization != NULL, keep_alive != NULL,
            drop != NULL,
            keep != NULL, proto ? proto : "missing",
            forwarded_host ? forwarded_host : "missing",
            (int)request->body_len,
            request->body ? request->body : "");
        return;
    }
    (void)neverc_http_writef(
        writer, "%c:%s", state->id,
        request->path ? request->path : "/");
}

static const char rewritten_framing_headers[] =
    "Host\0" "attacker.invalid\0"
    "Content-Length\0" "999\0"
    "Content-Length\0" "4\0"
    "Transfer-Encoding\0" "chunked\0"
    "Forwarded\0" "for=attacker\0"
    "X-Forwarded-Proto\0" "https\0"
    "X-Forwarded-For\0" "203.0.113.9\0"
    "X-Real-IP\0" "198.51.100.7\0"
    "Keep-Alive\0" "timeout=5\0"
    "Proxy-Authorization\0" "Basic dXNlcjpwYXNz\0"
    "TE\0" "trailers\0"
    "Connection\0" "X-Drop\0"
    "X-Drop\0" "secret\0"
    "X-Keep\0" "yes\0";

static atomic_int rewrite_calls;

static int rewrite_request(const neverc_http_request_t *input,
                           neverc_http_request_t *output,
                           void *context) {
    (void)context;
    atomic_fetch_add_explicit(
        &rewrite_calls, 1, memory_order_relaxed);
    if (input->path && strcmp(input->path, "/rewrite") == 0) {
        output->path = "/rewritten";
    } else if (input->path &&
               strcmp(input->path, "/inject") == 0) {
        output->path = "/safe\r\nX-Injected: yes";
    } else if (input->path &&
               strcmp(input->path, "/framing") == 0) {
        output->raw_headers = rewritten_framing_headers;
        output->nheaders = 14;
    } else if (input->path &&
               (strcmp(input->path, "/options-star") == 0 ||
                strcmp(input->path, "/base-star") == 0)) {
        output->method = "OPTIONS";
        output->path = "*";
        output->query = NULL;
    } else if (input->path &&
               strcmp(input->path, "/bad-star") == 0) {
        output->path = "*";
        output->query = NULL;
    } else if (input->path &&
               strcmp(input->path, "/star-query") == 0) {
        output->method = "OPTIONS";
        output->path = "*";
        output->query = "forbidden=1";
    } else if (input->path &&
               strcmp(input->path, "/dotdot") == 0) {
        output->path = "/../admin";
    } else if (input->path &&
               strcmp(input->path, "/dotdot-encoded") == 0) {
        output->path = "/%2e%2e/admin";
    } else if (input->path &&
               strcmp(input->path, "/slash-slash") == 0) {
        output->path = "//evil.example/";
    } else if (input->path &&
               strcmp(input->path, "/dotdot-semi") == 0) {
        output->path = "/..;/admin";
    } else if (input->path &&
               strcmp(input->path, "/dotdot-pct3b") == 0) {
        output->path = "/%2e%2e%3b/admin";
    } else if (input->path &&
               strcmp(input->path, "/mid-slash-slash") == 0) {
        output->path = "/foo//bar";
    } else if (input->path &&
               strcmp(input->path, "/connect") == 0) {
        output->method = "CONNECT";
    }
    return 0;
}

typedef struct {
    atomic_int calls;
} proxy_error_state_t;

static void proxy_error_handler(
    neverc_http_response_writer_t *writer,
    const neverc_http_request_t *request,
    const char *error_message, void *context) {
    proxy_error_state_t *state = (proxy_error_state_t *)context;
    atomic_fetch_add_explicit(
        &state->calls, 1, memory_order_relaxed);
    neverc_http_set_status(writer, 598);
    neverc_http_set_header(writer, "X-Proxy-Error", "yes");
    (void)neverc_http_writef(
        writer, "proxy-error:%s:%s",
        request && request->path ? request->path : "unknown",
        error_message ? error_message : "unknown");
}

/* A plaintext peer captures the first bytes emitted by an https target.
 * The proxy must send a TLS ClientHello, never an HTTP request line. */
typedef struct {
    neverc_tcp_listener_t *listener;
    int port;
    const char *reply;
    size_t reply_length;
    unsigned char first_bytes[16];
    size_t first_length;
    int result;
    int thread_started;
    atomic_int thread_finished;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} tls_probe_t;

#if defined(_WIN32)
static DWORD WINAPI tls_probe_thread(LPVOID argument) {
#else
static void *tls_probe_thread(void *argument) {
#endif
    tls_probe_t *probe = (tls_probe_t *)argument;
    const char *error = NULL;
    neverc_tcp_conn_t *connection =
        neverc_tcp_accept(probe->listener, &error);
    if (connection) {
        (void)neverc_tcp_set_timeout(connection, 3000);
        unsigned char request_bytes[4096];
        size_t request_length = 0;
        int complete_request_headers = probe->reply == NULL;
        do {
            size_t capacity = probe->reply
                ? sizeof(request_bytes) - request_length
                : sizeof(probe->first_bytes) - probe->first_length;
            void *destination = probe->reply
                ? (void *)(request_bytes + request_length)
                : (void *)(probe->first_bytes + probe->first_length);
            int received = neverc_tcp_read(
                connection, destination, capacity);
            if (received <= 0) break;
            if (probe->reply) {
                size_t old_length = request_length;
                request_length += (size_t)received;
                size_t capture = request_length;
                if (capture > sizeof(probe->first_bytes))
                    capture = sizeof(probe->first_bytes);
                memcpy(probe->first_bytes, request_bytes, capture);
                probe->first_length = capture;
                size_t scan = old_length > 3U ? old_length - 3U : 0U;
                for (; scan + 3U < request_length; scan++) {
                    if (memcmp(
                            request_bytes + scan, "\r\n\r\n", 4U) == 0) {
                        complete_request_headers = 1;
                        break;
                    }
                }
            } else {
                probe->first_length += (size_t)received;
            }
        } while ((probe->reply && !complete_request_headers &&
                  request_length < sizeof(request_bytes)) ||
                 (!probe->reply && probe->first_length < 4U));
        if (probe->first_length > 0 && complete_request_headers) {
            probe->result = 0;
            if (probe->reply && probe->reply_length > 0 &&
                tcp_write_all(
                    connection, probe->reply,
                    probe->reply_length) != 0)
                probe->result = -1;
        }
        neverc_tcp_close(connection);
    }
    atomic_store_explicit(
        &probe->thread_finished, 1, memory_order_release);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int tls_probe_start(tls_probe_t *probe, const char *reply) {
    if (!probe) return -1;
    memset(probe, 0, sizeof(*probe));
    probe->result = -1;
    atomic_init(&probe->thread_finished, 0);
    probe->reply = reply;
    probe->reply_length = reply ? strlen(reply) : 0;
    const char *error = NULL;
    probe->listener = neverc_tcp_listen(
        "127.0.0.1:0", &error);
    if (!probe->listener) return -1;
    neverc_tcp_addr_t address;
    if (neverc_tcp_listener_addr(probe->listener, &address) != 0) {
        neverc_tcp_listener_close(probe->listener);
        probe->listener = NULL;
        return -1;
    }
    probe->port = address.port;
#if defined(_WIN32)
    probe->thread = CreateThread(
        NULL, 0, tls_probe_thread, probe, 0, NULL);
    if (!probe->thread) {
        neverc_tcp_listener_close(probe->listener);
        probe->listener = NULL;
        return -1;
    }
#else
    if (pthread_create(
            &probe->thread, NULL, tls_probe_thread, probe) != 0) {
        neverc_tcp_listener_close(probe->listener);
        probe->listener = NULL;
        return -1;
    }
#endif
    probe->thread_started = 1;
    return probe->port;
}

static void tls_probe_stop(tls_probe_t *probe) {
    if (!probe || !probe->listener) return;
    if (probe->thread_started &&
        !atomic_load_explicit(
            &probe->thread_finished, memory_order_acquire)) {
        char address[64];
        (void)snprintf(
            address, sizeof(address), "127.0.0.1:%d", probe->port);
        const char *error = NULL;
        neverc_tcp_conn_t *connection =
            neverc_tcp_dial(address, &error);
        if (connection) {
            (void)neverc_tcp_write(connection, "x", 1U);
            neverc_tcp_close(connection);
        }
    }
    if (probe->thread_started) {
#if defined(_WIN32)
        DWORD wait_result = WaitForSingleObject(probe->thread, 5000U);
        if (wait_result != WAIT_OBJECT_0 ||
            !CloseHandle(probe->thread))
            test_thread_fatal("probe thread did not stop");
#else
        if (test_wait_finished(
                &probe->thread_finished, 5000U) != 0 ||
            pthread_join(probe->thread, NULL) != 0)
            test_thread_fatal("probe thread did not stop");
#endif
        probe->thread_started = 0;
    }
    neverc_tcp_listener_close(probe->listener);
    probe->listener = NULL;
}

static void check_small_proxy_request(int proxy_port, const char *request,
                                      const char *expected) {
    raw_response_t response;
    int result = raw_http_request(
        proxy_port, request, 256U * 1024U, &response);
    CHECK("proxy request completed", result == 0);
    if (result == 0)
        check_contains("proxy response contains expected value",
                       response.data, expected);
    raw_response_free(&response);
}

typedef struct {
    neverc_http_handler_func_t handler;
} prefilled_proxy_state_t;

static void prefilled_proxy_handler(
    neverc_http_request_t *request,
    neverc_http_response_writer_t *writer, void *context) {
    prefilled_proxy_state_t *state =
        (prefilled_proxy_state_t *)context;
    (void)neverc_http_add_header(
        writer, "X-Preexisting", "must-reset");
    if (state && state->handler)
        state->handler(request, writer);
}

static void test_live_reverse_proxy(void) {
    puts("[live_reverse_proxy]");
    neverc_http_mux_t *backend_a_mux = NULL;
    neverc_http_mux_t *backend_b_mux = NULL;
    neverc_http_mux_t *proxy_mux = NULL;
    test_server_t backend_a_server;
    test_server_t backend_b_server;
    test_server_t proxy_server;
    memset(&backend_a_server, 0, sizeof(backend_a_server));
    memset(&backend_b_server, 0, sizeof(backend_b_server));
    memset(&proxy_server, 0, sizeof(proxy_server));
    neverc_httputil_reverse_proxy_t *proxy_a = NULL;
    neverc_httputil_reverse_proxy_t *proxy_b = NULL;
    neverc_httputil_reverse_proxy_t *proxy_base = NULL;
    neverc_httputil_reverse_proxy_t *proxy_error = NULL;
    neverc_httputil_reverse_proxy_t *proxy_overflow = NULL;
    neverc_httputil_reverse_proxy_t *proxy_tls = NULL;
    neverc_httputil_reverse_proxy_t *proxy_http10 = NULL;
    neverc_httputil_reverse_proxy_t *proxy_hops = NULL;
    neverc_httputil_reverse_proxy_t *proxy_dupcl = NULL;
    tls_probe_t error_probe;
    tls_probe_t overflow_probe;
    tls_probe_t tls_probe;
    tls_probe_t http10_probe;
    tls_probe_t hop_probe;
    tls_probe_t dupcl_probe;
    memset(&error_probe, 0, sizeof(error_probe));
    memset(&overflow_probe, 0, sizeof(overflow_probe));
    memset(&tls_probe, 0, sizeof(tls_probe));
    memset(&http10_probe, 0, sizeof(http10_probe));
    memset(&hop_probe, 0, sizeof(hop_probe));
    memset(&dupcl_probe, 0, sizeof(dupcl_probe));
    char *large_body = NULL;

    const size_t large_body_length = 131089U;
    large_body = (char *)malloc(large_body_length);
    CHECK("large backend body allocated", large_body != NULL);
    if (!large_body) goto cleanup;
    memset(large_body, 'L', large_body_length);
    memcpy(large_body, "BEGIN", 5U);
    memcpy(large_body + large_body_length - 4U, "END!", 4U);

    backend_state_t backend_a = {
        'A', large_body, large_body_length};
    backend_state_t backend_b = {
        'B', large_body, large_body_length};
    backend_a_mux = neverc_http_new_mux();
    backend_b_mux = neverc_http_new_mux();
    CHECK("backend muxes allocated", backend_a_mux && backend_b_mux);
    if (!backend_a_mux || !backend_b_mux) goto cleanup;
    CHECK("backend A route registered",
          neverc_http_mux_handle_context(
              backend_a_mux, "/", backend_handler, &backend_a) == 0);
    CHECK("backend A asterisk route registered",
          neverc_http_mux_handle_context(
              backend_a_mux, "*", backend_handler, &backend_a) == 0);
    CHECK("backend B route registered",
          neverc_http_mux_handle_context(
              backend_b_mux, "/", backend_handler, &backend_b) == 0);

    int backend_a_port =
        test_server_start(&backend_a_server, backend_a_mux);
    int backend_b_port =
        test_server_start(&backend_b_server, backend_b_mux);
    CHECK("backend A started", backend_a_port > 0);
    CHECK("backend B started", backend_b_port > 0);
    if (backend_a_port <= 0 || backend_b_port <= 0) goto cleanup;

    char target_a[96];
    char target_b[96];
    (void)snprintf(target_a, sizeof(target_a),
                   "http://127.0.0.1:%d", backend_a_port);
    (void)snprintf(target_b, sizeof(target_b),
                   "http://127.0.0.1:%d", backend_b_port);
    proxy_a = neverc_httputil_new_single_host_reverse_proxy(target_a);
    proxy_b = neverc_httputil_new_single_host_reverse_proxy(target_b);
    char target_base[112];
    (void)snprintf(target_base, sizeof(target_base),
                   "http://127.0.0.1:%d/base", backend_a_port);
    proxy_base = neverc_httputil_new_single_host_reverse_proxy(
        target_base);
    static const char malformed_response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n\r\n"
        "0\r\n\r\n";
    int unavailable_port =
        tls_probe_start(&error_probe, malformed_response);
    CHECK("malformed HTTP peer started", unavailable_port > 0);
    char error_target[96];
    (void)snprintf(error_target, sizeof(error_target),
                   "http://127.0.0.1:%d", unavailable_port);
    proxy_error = neverc_httputil_new_single_host_reverse_proxy(
        error_target);

    static const char http10_chunked_response[] =
        "HTTP/1.0 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n\r\n"
        "5\r\nhello\r\n0\r\n\r\n";
    int http10_port =
        tls_probe_start(&http10_probe, http10_chunked_response);
    CHECK("HTTP/1.0 chunked peer started", http10_port > 0);
    char http10_target[96];
    (void)snprintf(http10_target, sizeof(http10_target),
                   "http://127.0.0.1:%d", http10_port);
    proxy_http10 = neverc_httputil_new_single_host_reverse_proxy(
        http10_target);

    static const char hop_response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "Connection: close, X-Hop\r\n"
        "Keep-Alive: timeout=5\r\n"
        "Proxy-Connection: keep-alive\r\n"
        "Proxy-Authenticate: Basic realm=\"backend\"\r\n"
        "TE: trailers\r\n"
        "HTTP2-Settings: AAEAAQAAAAIAAAAAAAIAAAAA\r\n"
        "X-Hop: secret\r\n"
        "X-Keep: yes\r\n"
        "\r\n"
        "OK";
    int hop_port = tls_probe_start(&hop_probe, hop_response);
    CHECK("hop-by-hop peer started", hop_port > 0);
    char hop_target[96];
    (void)snprintf(hop_target, sizeof(hop_target),
                   "http://127.0.0.1:%d", hop_port);
    proxy_hops = neverc_httputil_new_single_host_reverse_proxy(
        hop_target);

    static const char dupcl_response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n\r\n"
        "OK";
    int dupcl_port = tls_probe_start(&dupcl_probe, dupcl_response);
    CHECK("duplicate Content-Length peer started", dupcl_port > 0);
    char dupcl_target[96];
    (void)snprintf(dupcl_target, sizeof(dupcl_target),
                   "http://127.0.0.1:%d", dupcl_port);
    proxy_dupcl = neverc_httputil_new_single_host_reverse_proxy(
        dupcl_target);

    char overflow_response[4096];
    int overflow_size = snprintf(
        overflow_response, sizeof(overflow_response),
        "HTTP/1.1 200 OK\r\n");
    int overflow_built =
        overflow_size > 0 &&
        (size_t)overflow_size < sizeof(overflow_response);
    for (int i = 0; overflow_built && i < 64; i++) {
        int written = snprintf(
            overflow_response + overflow_size,
            sizeof(overflow_response) - (size_t)overflow_size,
            "X-Raw-%02d: value\r\n", i);
        if (written < 0 ||
            (size_t)written >=
                sizeof(overflow_response) - (size_t)overflow_size) {
            overflow_built = 0;
        } else {
            overflow_size += written;
        }
    }
    if (overflow_built) {
        int written = snprintf(
            overflow_response + overflow_size,
            sizeof(overflow_response) - (size_t)overflow_size,
            "\r\noverflow");
        if (written < 0 ||
            (size_t)written >=
                sizeof(overflow_response) - (size_t)overflow_size) {
            overflow_built = 0;
        }
    }
    CHECK("header overflow response built", overflow_built);
    int overflow_port = overflow_built
        ? tls_probe_start(&overflow_probe, overflow_response) : -1;
    CHECK("header overflow peer started", overflow_port > 0);
    char overflow_target[96];
    (void)snprintf(overflow_target, sizeof(overflow_target),
                   "http://127.0.0.1:%d", overflow_port);
    proxy_overflow = overflow_port > 0
        ? neverc_httputil_new_single_host_reverse_proxy(
              overflow_target)
        : NULL;

    int tls_probe_port = tls_probe_start(&tls_probe, NULL);
    CHECK("TLS plaintext probe started", tls_probe_port > 0);
    char tls_target[96];
    (void)snprintf(tls_target, sizeof(tls_target),
                   "https://127.0.0.1:%d", tls_probe_port);
    proxy_tls = neverc_httputil_new_single_host_reverse_proxy(tls_target);
    CHECK("all proxy instances allocated",
          proxy_a && proxy_b && proxy_base && proxy_error &&
          proxy_overflow && proxy_tls && proxy_http10 && proxy_hops &&
          proxy_dupcl);
    if (!proxy_a || !proxy_b || !proxy_base ||
        !proxy_error || !proxy_overflow || !proxy_tls ||
        !proxy_http10 || !proxy_hops || !proxy_dupcl)
        goto cleanup;

    atomic_store_explicit(&rewrite_calls, 0, memory_order_relaxed);
    neverc_httputil_proxy_set_rewrite(
        proxy_a, rewrite_request, NULL);
    neverc_httputil_proxy_set_rewrite(
        proxy_base, rewrite_request, NULL);
    CHECK("live forwarded proto configured",
          neverc_httputil_proxy_set_forwarded_proto(
              proxy_a, "https") == 0);
    proxy_error_state_t error_state;
    atomic_init(&error_state.calls, 0);
    neverc_httputil_proxy_set_error_handler(
        proxy_a, proxy_error_handler, &error_state);
    neverc_httputil_proxy_set_error_handler(
        proxy_base, proxy_error_handler, &error_state);
    neverc_httputil_proxy_set_error_handler(
        proxy_error, proxy_error_handler, &error_state);
    neverc_httputil_proxy_set_error_handler(
        proxy_overflow, proxy_error_handler, &error_state);
    neverc_httputil_proxy_set_error_handler(
        proxy_tls, proxy_error_handler, &error_state);
    neverc_httputil_proxy_set_error_handler(
        proxy_http10, proxy_error_handler, &error_state);
    neverc_httputil_proxy_set_error_handler(
        proxy_hops, proxy_error_handler, &error_state);
    neverc_httputil_proxy_set_error_handler(
        proxy_dupcl, proxy_error_handler, &error_state);

    proxy_mux = neverc_http_new_mux();
    CHECK("proxy mux allocated", proxy_mux != NULL);
    if (!proxy_mux) goto cleanup;
    CHECK("proxy A route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/one", proxy_a) == 0);
    CHECK("proxy B route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/two", proxy_b) == 0);
    CHECK("rewrite route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/rewrite", proxy_a) == 0);
    CHECK("framing route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/framing", proxy_a) == 0);
    CHECK("injection route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/inject", proxy_a) == 0);
    CHECK("dot-dot route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/dotdot", proxy_a) == 0);
    CHECK("encoded dot-dot route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/dotdot-encoded", proxy_a) == 0);
    CHECK("scheme-relative path route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/slash-slash", proxy_a) == 0);
    CHECK("matrix-parameter traversal route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/dotdot-semi", proxy_a) == 0);
    CHECK("encoded matrix-parameter traversal route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/dotdot-pct3b", proxy_a) == 0);
    CHECK("middle double-slash route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/mid-slash-slash", proxy_a) == 0);
    CHECK("CONNECT rewrite route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/connect", proxy_a) == 0);
    CHECK("chunked route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/chunked", proxy_a) == 0);
    CHECK("large route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/large", proxy_a) == 0);
    CHECK("HEAD route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/head", proxy_a) == 0);
    CHECK("cookie route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/cookies", proxy_a) == 0);
    CHECK("304 route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/not-modified", proxy_a) == 0);
    CHECK("204 route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/no-content", proxy_a) == 0);
    CHECK("101 rejection route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/upgrade", proxy_a) == 0);
    CHECK("OPTIONS asterisk route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/options-star", proxy_a) == 0);
    CHECK("invalid asterisk route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/bad-star", proxy_a) == 0);
    CHECK("asterisk query route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/star-query", proxy_a) == 0);
    CHECK("base path asterisk route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/base-star", proxy_base) == 0);
    CHECK("error route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/error", proxy_error) == 0);
    CHECK("HTTP/1.0 chunked route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/http10-chunked", proxy_http10) == 0);
    CHECK("hop-by-hop route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/hops", proxy_hops) == 0);
    CHECK("duplicate Content-Length route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/dup-cl", proxy_dupcl) == 0);
    CHECK("TLS route registered",
          neverc_httputil_proxy_register(
              proxy_mux, "/tls", proxy_tls) == 0);
    neverc_http_handler_func_t legacy_a =
        neverc_httputil_proxy_handler(proxy_a);
    neverc_http_handler_func_t legacy_b =
        neverc_httputil_proxy_handler(proxy_b);
    prefilled_proxy_state_t overflow_state = {
        neverc_httputil_proxy_handler(proxy_overflow)};
    CHECK("live legacy handlers allocated", legacy_a && legacy_b);
    CHECK("live legacy handlers remain instance-bound",
          legacy_a != legacy_b);
    if (legacy_a)
        neverc_http_mux_handle(proxy_mux, "/legacy-a", legacy_a);
    if (legacy_b)
        neverc_http_mux_handle(proxy_mux, "/legacy-b", legacy_b);
    CHECK("prefilled overflow handler allocated",
          overflow_state.handler != NULL);
    if (overflow_state.handler) {
        CHECK("header overflow route registered",
              neverc_http_mux_handle_context(
                  proxy_mux, "/header-overflow",
                  prefilled_proxy_handler,
                  &overflow_state) == 0);
    }

    int proxy_port = test_server_start(&proxy_server, proxy_mux);
    CHECK("proxy server started", proxy_port > 0);
    if (proxy_port <= 0) goto cleanup;

    check_small_proxy_request(
        proxy_port,
        "GET /one HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        "A:/one");
    check_small_proxy_request(
        proxy_port,
        "GET /two HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        "B:/two");
    check_small_proxy_request(
        proxy_port,
        "GET /rewrite HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        "A:/rewritten");
    check_small_proxy_request(
        proxy_port,
        "GET /legacy-a HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        "A:/legacy-a");
    check_small_proxy_request(
        proxy_port,
        "GET /legacy-b HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        "B:/legacy-b");
    CHECK("rewrite callback was called",
          atomic_load_explicit(
              &rewrite_calls, memory_order_relaxed) >= 2);

    raw_response_t response;
    int request_result = raw_http_request(
        proxy_port,
        "POST /framing HTTP/1.1\r\n"
        "Host: client.example\r\n"
        "Content-Length: 4\r\n"
        "Forwarded: for=198.51.100.7\r\n"
        "X-Forwarded-Proto: https\r\n"
        "Connection: close\r\n\r\nDATA",
        256U * 1024U, &response);
    CHECK("framing proxy request completed", request_result == 0);
    if (request_result == 0) {
        check_contains("one canonical content length",
                       response.data, "cl=1");
        check_contains("transfer encoding removed",
                       response.data, "te=0");
        check_contains("Forwarded input removed",
                       response.data, "forwarded=0");
        check_contains("X-Forwarded-For input removed",
                       response.data, "xff=0");
        check_contains("X-Real-IP spoof removed",
                       response.data, "xrealip=0");
        check_contains("Proxy-Authorization hop header removed",
                       response.data, "proxyauth=0");
        check_contains("Keep-Alive hop header removed",
                       response.data, "keepalive=0");
        check_contains("Connection nominated header removed",
                       response.data, "xdrop=0");
        check_contains("ordinary header retained",
                       response.data, "xkeep=1");
        check_contains("configured inbound proto reaches backend",
                       response.data, "proto=https");
        check_contains("trusted forwarded host generated",
                       response.data, "xhost=client.example");
        check_contains("request body forwarded",
                       response.data, "body=DATA");
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /framing HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("GET framing proxy request completed", request_result == 0);
    if (request_result == 0) {
        check_contains("proxied GET omits Content-Length",
                       response.data, "cl=0");
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /inject HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("injection rejection response completed",
          request_result == 0);
    if (request_result == 0) {
        check_contains("CRLF injection rejected via error handler",
                       response.data, "proxy-error:/inject:");
        CHECK("CRLF injection returned failure status",
              strstr(response.data, "HTTP/1.1 598") != NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /dotdot HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("dot-dot rejection completed", request_result == 0);
    if (request_result == 0) {
        check_contains("dot-dot path rejected via error handler",
                       response.data, "proxy-error:/dotdot:");
        CHECK("dot-dot path returned failure status",
              strstr(response.data, "HTTP/1.1 598") != NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /dotdot-encoded HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("encoded dot-dot rejection completed", request_result == 0);
    if (request_result == 0) {
        check_contains("encoded dot-dot path rejected",
                       response.data, "proxy-error:/dotdot-encoded:");
        CHECK("encoded dot-dot returned failure status",
              strstr(response.data, "HTTP/1.1 598") != NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /slash-slash HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("scheme-relative path rejection completed", request_result == 0);
    if (request_result == 0) {
        check_contains("scheme-relative path rejected",
                       response.data, "proxy-error:/slash-slash:");
        CHECK("scheme-relative path returned failure status",
              strstr(response.data, "HTTP/1.1 598") != NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /dotdot-semi HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("matrix-parameter traversal rejection completed",
          request_result == 0);
    if (request_result == 0) {
        check_contains("matrix-parameter path rejected",
                       response.data, "proxy-error:/dotdot-semi:");
        CHECK("matrix-parameter path returned failure status",
              strstr(response.data, "HTTP/1.1 598") != NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /dotdot-pct3b HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("encoded matrix-parameter rejection completed",
          request_result == 0);
    if (request_result == 0) {
        check_contains("encoded matrix-parameter path rejected",
                       response.data, "proxy-error:/dotdot-pct3b:");
        CHECK("encoded matrix-parameter returned failure status",
              strstr(response.data, "HTTP/1.1 598") != NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /mid-slash-slash HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("middle double-slash rejection completed", request_result == 0);
    if (request_result == 0) {
        check_contains("middle double-slash path rejected",
                       response.data, "proxy-error:/mid-slash-slash:");
        CHECK("middle double-slash returned failure status",
              strstr(response.data, "HTTP/1.1 598") != NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /connect HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("CONNECT rewrite rejection completed", request_result == 0);
    if (request_result == 0) {
        check_contains("CONNECT rewrite rejected",
                       response.data, "proxy-error:/connect:");
        CHECK("CONNECT rewrite returned failure status",
              strstr(response.data, "HTTP/1.1 598") != NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /chunked HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("chunked proxy request completed", request_result == 0);
    if (request_result == 0) {
        size_t body_length = 0;
        const char *body = raw_response_body(
            &response, &body_length);
        CHECK("chunked body decoded",
              body && body_length == 10U &&
              memcmp(body, "alpha-beta", 10U) == 0);
        CHECK("chunked transfer header stripped",
              strstr(response.data, "Transfer-Encoding:") == NULL);
        CHECK("chunk markers not forwarded",
              strstr(response.data, "\r\n5\r\nalpha") == NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /large HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        512U * 1024U, &response);
    CHECK("large proxy request completed", request_result == 0);
    if (request_result == 0) {
        size_t body_length = 0;
        const char *body = raw_response_body(
            &response, &body_length);
        CHECK("large response exceeds old 64 KiB limit",
              body_length > 65536U);
        CHECK("large response is not truncated",
              body && body_length == large_body_length &&
              memcmp(body, "BEGIN", 5U) == 0 &&
              memcmp(body + body_length - 4U, "END!", 4U) == 0);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "HEAD /head HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("HEAD proxy request completed", request_result == 0);
    if (request_result == 0) {
        size_t body_length = 0;
        check_contains("HEAD preserves 200 status",
                       response.data, "HTTP/1.1 200");
        check_contains("HEAD preserves backend header",
                       response.data, "X-Backend: yes");
        check_contains("HEAD preserves representation length",
                       response.data, "Content-Length: 7\r\n");
        CHECK("HEAD response has no body",
              raw_response_body(&response, &body_length) != NULL &&
              body_length == 0);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /cookies HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("duplicate response header request completed",
          request_result == 0);
    if (request_result == 0) {
        CHECK("both Set-Cookie headers preserved",
              count_occurrences(response.data, "Set-Cookie:") == 2);
        check_contains("first Set-Cookie preserved",
                       response.data, "Set-Cookie: first=one; Path=/");
        check_contains("second Set-Cookie preserved",
                       response.data, "Set-Cookie: second=two; Path=/");
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /not-modified HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("304 proxy request completed", request_result == 0);
    if (request_result == 0) {
        size_t body_length = 0;
        check_contains("304 status preserved",
                       response.data, "HTTP/1.1 304");
        check_contains("304 backend header preserved",
                       response.data, "X-Backend: yes");
        check_contains("304 representation length preserved",
                       response.data, "Content-Length: 1234\r\n");
        CHECK("304 response has no body",
              raw_response_body(&response, &body_length) != NULL &&
              body_length == 0);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /no-content HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("204 proxy request completed", request_result == 0);
    if (request_result == 0) {
        size_t body_length = 0;
        check_contains("204 status preserved",
                       response.data, "HTTP/1.1 204");
        CHECK("204 omits Content-Length",
              strstr(response.data, "Content-Length:") == NULL);
        CHECK("204 response has no body",
              raw_response_body(&response, &body_length) != NULL &&
              body_length == 0);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        backend_a_port,
        "GET /informational HTTP/1.1\r\n"
        "Host: backend.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("1xx writer response completed", request_result == 0);
    if (request_result == 0) {
        check_contains("1xx status written",
                       response.data, "HTTP/1.1 103");
        CHECK("1xx omits Content-Length",
              strstr(response.data, "Content-Length:") == NULL);
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /upgrade HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("101 rejection response completed", request_result == 0);
    if (request_result == 0) {
        check_contains("101 rejected through error handler",
                       response.data, "HTTP/1.1 598");
        check_contains("clean error header written",
                       response.data, "X-Proxy-Error: yes");
        CHECK("backend upgrade header was not inherited",
              strstr(response.data, "Upgrade:") == NULL);
        CHECK("backend ordinary header was not inherited",
              strstr(response.data, "X-Leaked-Backend:") == NULL);
        check_contains("101 rejection error body written",
                       response.data, "proxy-error:/upgrade:");
    }
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /header-overflow HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("response header failure request completed",
          request_result == 0);
    if (request_result == 0) {
        check_contains("header append failure uses error handler",
                       response.data, "HTTP/1.1 598");
        check_contains("header append failure has clean error header",
                       response.data, "X-Proxy-Error: yes");
        CHECK("partial backend headers were reset",
              strstr(response.data, "X-Raw-00:") == NULL);
        CHECK("preexisting response headers were reset",
              strstr(response.data, "X-Preexisting:") == NULL);
    }
    raw_response_free(&response);
    tls_probe_stop(&overflow_probe);
    CHECK("header overflow peer saw request",
          overflow_probe.result == 0);

    check_small_proxy_request(
        proxy_port,
        "GET /options-star HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        "A:*");

    request_result = raw_http_request(
        proxy_port,
        "GET /bad-star HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("non-OPTIONS asterisk rejection completed",
          request_result == 0);
    if (request_result == 0)
        check_contains("non-OPTIONS asterisk rejected",
                       response.data, "HTTP/1.1 598");
    raw_response_free(&response);

    request_result = raw_http_request(
        proxy_port,
        "GET /star-query HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("asterisk query rejection completed", request_result == 0);
    if (request_result == 0)
        check_contains("asterisk query rejected",
                       response.data, "HTTP/1.1 598");
    raw_response_free(&response);

    check_small_proxy_request(
        proxy_port,
        "GET /base-star HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        "A:*");

    request_result = raw_http_request(
        proxy_port,
        "GET /error HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("backend failure response completed", request_result == 0);
    if (request_result == 0) {
        check_contains("custom error status used",
                       response.data, "HTTP/1.1 598");
        check_contains("custom error handler body used",
                       response.data, "proxy-error:/error:");
    }
    raw_response_free(&response);
    tls_probe_stop(&error_probe);
    CHECK("HTTP failure peer saw request", error_probe.result == 0);
    CHECK("HTTP target used plaintext TCP",
          error_probe.first_length >= 4U &&
          memcmp(error_probe.first_bytes, "GET ", 4U) == 0);

    request_result = raw_http_request(
        proxy_port,
        "GET /http10-chunked HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("HTTP/1.0 chunked rejection completed", request_result == 0);
    if (request_result == 0) {
        check_contains("HTTP/1.0 chunked rejected via error handler",
                       response.data, "HTTP/1.1 598");
        CHECK("HTTP/1.0 chunked body was not forwarded",
              strstr(response.data, "hello") == NULL);
    }
    raw_response_free(&response);
    tls_probe_stop(&http10_probe);

    request_result = raw_http_request(
        proxy_port,
        "GET /hops HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("hop-by-hop proxy request completed", request_result == 0);
    if (request_result == 0) {
        check_contains("hop-by-hop body forwarded",
                       response.data, "OK");
        check_contains("ordinary response header retained",
                       response.data, "X-Keep: yes");
        CHECK("Keep-Alive hop header stripped",
              strstr(response.data, "Keep-Alive:") == NULL);
        CHECK("Proxy-Connection hop header stripped",
              strstr(response.data, "Proxy-Connection:") == NULL);
        CHECK("Proxy-Authenticate hop header stripped",
              strstr(response.data, "Proxy-Authenticate:") == NULL);
        CHECK("TE hop header stripped",
              strstr(response.data, "TE:") == NULL);
        CHECK("HTTP2-Settings hop header stripped",
              strstr(response.data, "HTTP2-Settings:") == NULL);
        CHECK("Connection-nominated response header stripped",
              strstr(response.data, "X-Hop:") == NULL);
    }
    raw_response_free(&response);
    tls_probe_stop(&hop_probe);

    request_result = raw_http_request(
        proxy_port,
        "GET /dup-cl HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("duplicate Content-Length rejection completed",
          request_result == 0);
    if (request_result == 0) {
        check_contains("duplicate Content-Length rejected via error handler",
                       response.data, "HTTP/1.1 598");
        CHECK("duplicate Content-Length body was not forwarded",
              strstr(response.data, "\r\n\r\nOK") == NULL);
    }
    raw_response_free(&response);
    tls_probe_stop(&dupcl_probe);

    request_result = raw_http_request(
        proxy_port,
        "GET /tls HTTP/1.1\r\n"
        "Host: client.example\r\nConnection: close\r\n\r\n",
        256U * 1024U, &response);
    CHECK("HTTPS backend failure response completed",
          request_result == 0);
    if (request_result == 0)
        check_contains("HTTPS failure uses error handler",
                       response.data, "proxy-error:/tls:");
    raw_response_free(&response);
    tls_probe_stop(&tls_probe);
    CHECK("HTTPS branch connected to probe", tls_probe.result == 0);
    CHECK("HTTPS branch emitted TLS record",
          tls_probe.first_length >= 3U &&
          tls_probe.first_bytes[0] == 0x16U &&
          tls_probe.first_bytes[1] == 0x03U);
    CHECK("HTTPS branch did not emit plaintext HTTP",
          tls_probe.first_length < 4U ||
          memcmp(tls_probe.first_bytes, "GET ", 4U) != 0);
    CHECK("error handler observed backend failures",
          atomic_load_explicit(
              &error_state.calls, memory_order_relaxed) >= 5);

cleanup:
    tls_probe_stop(&error_probe);
    tls_probe_stop(&overflow_probe);
    tls_probe_stop(&tls_probe);
    tls_probe_stop(&http10_probe);
    tls_probe_stop(&hop_probe);
    tls_probe_stop(&dupcl_probe);
    test_server_stop(&proxy_server);
    if (proxy_mux) neverc_http_mux_free(proxy_mux);
    neverc_httputil_proxy_free(proxy_a);
    neverc_httputil_proxy_free(proxy_b);
    neverc_httputil_proxy_free(proxy_base);
    neverc_httputil_proxy_free(proxy_error);
    neverc_httputil_proxy_free(proxy_overflow);
    neverc_httputil_proxy_free(proxy_tls);
    neverc_httputil_proxy_free(proxy_http10);
    neverc_httputil_proxy_free(proxy_hops);
    neverc_httputil_proxy_free(proxy_dupcl);
    test_server_stop(&backend_a_server);
    test_server_stop(&backend_b_server);
    if (backend_a_mux) neverc_http_mux_free(backend_a_mux);
    if (backend_b_mux) neverc_http_mux_free(backend_b_mux);
    free(large_body);
}

int main(void) {
    puts("=== NeverC httputil tests ===");
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif
    test_target_validation();
    test_legacy_handler_binding();
    test_dump_apis();
    test_live_reverse_proxy();
    printf("%d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
