#include "neverc/std/context.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/thread.h"
#include "neverc/std/time.h"
#include "network_test_support.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static int tests_run;
static int tests_failed;
static atomic_int access_log_calls;

#define HTTP_STAGE5_STATIC_FILE "large.bin"
#define HTTP_STAGE5_STATIC_SIZE ((1024U * 1024U) + 17U)
#define HTTP_STAGE5_GZIP_FILE "compressible.txt"

static char http_stage5_static_dir[256];
static char http_stage5_static_path[320];
static char http_stage5_gzip_path[320];

#define CHECK(condition)                                                     \
    do {                                                                     \
        tests_run++;                                                         \
        if (!(condition)) {                                                  \
            tests_failed++;                                                  \
            printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #condition);  \
        }                                                                    \
    } while (0)

typedef struct {
    neverc_http_server_t *server;
    const neverc_network_test_files_t *files;
    int use_tls;
    int result;
} http_stage5_server_t;

typedef struct {
    const char *data;
    size_t length;
    size_t offset;
} http_stage5_source_t;

typedef struct {
    char data[256];
    size_t length;
} http_stage5_sink_t;

static int http_stage5_tcp_write_all(neverc_tcp_conn_t *connection,
                                     const void *data, size_t length);
static void http_stage5_remove_static_file(void);

static void http_stage5_access_log(const char *method, const char *path,
                                   int status, double duration_ms,
                                   size_t body_size) {
    (void)method;
    (void)path;
    (void)status;
    (void)duration_ms;
    (void)body_size;
    atomic_fetch_add_explicit(&access_log_calls, 1, memory_order_relaxed);
}

static void http_stage5_echo(neverc_http_request_t *request,
                             neverc_http_response_writer_t *writer) {
    neverc_http_set_header(writer, "Content-Type", "application/octet-stream");
    (void)neverc_http_write(writer, request->body, request->body_len);
}

static void http_stage5_chunks(neverc_http_request_t *request,
                               neverc_http_response_writer_t *writer) {
    (void)request;
    neverc_http_set_trailer(writer, "X-Finished", "yes");
    neverc_http_enable_chunked(writer);
    (void)neverc_http_write(writer, "alpha", 5U);
    (void)neverc_http_write(writer, "beta", 4U);
    (void)neverc_http_end_chunked(writer);
}

static void http_stage5_auto_chunks(neverc_http_request_t *request,
                                    neverc_http_response_writer_t *writer) {
    (void)request;
    neverc_http_enable_chunked(writer);
    (void)neverc_http_write(writer, "auto", 4U);
    (void)neverc_http_write(writer, "matic", 5U);
    /* The server finalizer must emit the terminating zero-size chunk. */
}

static void http_stage5_sse(neverc_http_request_t *request,
                            neverc_http_response_writer_t *writer) {
    (void)request;
    if (neverc_http_sse_begin(writer) != 0) return;
    (void)neverc_http_sse_event(
        writer, "update", "first\r\nsecond\n", "7");
    (void)neverc_http_sse_retry(writer, 1500);
    neverc_http_sse_end(writer);
}

static void http_stage5_request_framing(
    neverc_http_request_t *request,
    neverc_http_response_writer_t *writer) {
    const char *content_length =
        neverc_http_request_header(request, "Content-Length");
    if (content_length)
        (void)neverc_http_write_string(writer, content_length);
    else
        (void)neverc_http_write_string(writer, "missing");
}

static void http_stage5_slow(neverc_http_request_t *request,
                             neverc_http_response_writer_t *writer) {
    (void)request;
    neverc_time_sleep(150 * NEVERC_TIME_MILLISECOND);
    (void)neverc_http_write(writer, "late", 4U);
}

static void http_stage5_server_task(void *context) {
    http_stage5_server_t *test = (http_stage5_server_t *)context;
    if (test->use_tls) {
        test->result = neverc_http_server_listen_and_serve_tls(
            test->server, "127.0.0.1:0", test->files->server_cert,
            test->files->server_key);
    } else {
        test->result = neverc_http_server_listen_and_serve(
            test->server, "127.0.0.1:0");
    }
}

static int http_stage5_wait_for_port(neverc_http_server_t *server) {
    for (int attempt = 0; attempt < 500; attempt++) {
        int port = neverc_http_server_bound_port(server);
        if (port > 0) return port;
        neverc_time_sleep(10 * NEVERC_TIME_MILLISECOND);
    }
    return -1;
}

static int http_stage5_source(void *context, void *buffer, size_t capacity) {
    http_stage5_source_t *source = (http_stage5_source_t *)context;
    size_t remaining = source->length - source->offset;
    if (remaining == 0) return 0;
    size_t amount = remaining > 3U ? 3U : remaining;
    if (amount > capacity) amount = capacity;
    memcpy(buffer, source->data + source->offset, amount);
    source->offset += amount;
    return (int)amount;
}

static int http_stage5_sink(void *context, const void *data, size_t length) {
    http_stage5_sink_t *sink = (http_stage5_sink_t *)context;
    if (length > sizeof(sink->data) - sink->length) return -1;
    memcpy(sink->data + sink->length, data, length);
    sink->length += length;
    return 0;
}

static int http_stage5_write_static_file(void) {
    char directory_template[] = "neverc-http-stage5-static-XXXXXX";
#ifdef _WIN32
    if (_mktemp_s(directory_template, sizeof(directory_template)) != 0 ||
        _mkdir(directory_template) != 0)
        return -1;
#else
    if (!mkdtemp(directory_template)) return -1;
#endif
    if (snprintf(http_stage5_static_dir, sizeof(http_stage5_static_dir),
                 "%s", directory_template) < 0 ||
        snprintf(http_stage5_static_path, sizeof(http_stage5_static_path),
                 "%s/%s", directory_template,
                 HTTP_STAGE5_STATIC_FILE) < 0 ||
        snprintf(http_stage5_gzip_path, sizeof(http_stage5_gzip_path),
                 "%s/%s", directory_template,
                 HTTP_STAGE5_GZIP_FILE) < 0) {
        http_stage5_remove_static_file();
        return -1;
    }
    FILE *file = fopen(http_stage5_static_path, "wb");
    if (!file) {
        http_stage5_remove_static_file();
        return -1;
    }
    uint8_t block[4096];
    for (size_t i = 0; i < sizeof(block); i++)
        block[i] = (uint8_t)(i & 0xffU);
    size_t remaining = HTTP_STAGE5_STATIC_SIZE;
    int result = 0;
    while (remaining > 0) {
        size_t amount = remaining < sizeof(block) ? remaining : sizeof(block);
        if (fwrite(block, 1U, amount, file) != amount) {
            result = -1;
            break;
        }
        remaining -= amount;
    }
    if (fclose(file) != 0) result = -1;
    if (result == 0) {
        file = fopen(http_stage5_gzip_path, "wb");
        if (!file) {
            result = -1;
        } else {
            uint8_t compressible[1024];
            memset(compressible, 'A', sizeof(compressible));
            size_t written = fwrite(
                compressible, 1U, sizeof(compressible), file);
            int close_result = fclose(file);
            if (written != sizeof(compressible) || close_result != 0)
                result = -1;
        }
    }
    if (result != 0) {
        http_stage5_remove_static_file();
    }
    return result;
}

static void http_stage5_remove_static_file(void) {
    if (http_stage5_static_path[0] != '\0')
        (void)remove(http_stage5_static_path);
    if (http_stage5_gzip_path[0] != '\0')
        (void)remove(http_stage5_gzip_path);
#ifdef _WIN32
    if (http_stage5_static_dir[0] != '\0')
        (void)_rmdir(http_stage5_static_dir);
#else
    if (http_stage5_static_dir[0] != '\0')
        (void)rmdir(http_stage5_static_dir);
#endif
    http_stage5_static_dir[0] = '\0';
    http_stage5_static_path[0] = '\0';
    http_stage5_gzip_path[0] = '\0';
}

static void http_stage5_check_static_head(neverc_http_client_t *client,
                                          const char *scheme, int port) {
    char url[160];
    const char *host = strcmp(scheme, "https") == 0
        ? "localhost" : "127.0.0.1";
    (void)snprintf(url, sizeof(url), "%s://%s:%d/static/%s",
                   scheme, host, port, HTTP_STAGE5_STATIC_FILE);
    neverc_http_response_t *response = neverc_http_client_do(
        client, "HEAD", url, NULL, NULL, 0U);
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        char expected[64];
        (void)snprintf(expected, sizeof(expected), "Content-Length: %u",
                       (unsigned)HTTP_STAGE5_STATIC_SIZE);
        CHECK(response->status_code == 200);
        CHECK(response->body == NULL && response->body_len == 0U);
        CHECK(response->headers && strstr(response->headers, expected));
        neverc_http_response_free(response);
    }

    (void)snprintf(url, sizeof(url), "%s://%s:%d/echo",
                   scheme, host, port);
    response = neverc_http_client_do(
        client, "POST", url, "text/plain", "after-head", 10U);
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        CHECK(response->status_code == 200 && response->body_len == 10U &&
              response->body &&
              memcmp(response->body, "after-head", 10U) == 0);
        neverc_http_response_free(response);
    }
}

static int http_stage5_fetch_headers(int port, const char *request,
                                     char *headers, size_t capacity) {
    if (!headers || capacity == 0) return -1;
    headers[0] = '\0';
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);
    const char *error = NULL;
    neverc_tcp_conn_t *connection = neverc_tcp_dial(address, &error);
    if (!connection) return -1;
    neverc_tcp_set_timeout(connection, 5000);
    if (http_stage5_tcp_write_all(
            connection, request, strlen(request)) != 0) {
        neverc_tcp_close(connection);
        return -1;
    }
    size_t length = 0;
    while (length + 1U < capacity) {
        int received = neverc_tcp_read(connection, headers + length, 1U);
        if (received <= 0) break;
        length += (size_t)received;
        headers[length] = '\0';
        if (strstr(headers, "\r\n\r\n")) {
            neverc_tcp_close(connection);
            return 0;
        }
    }
    neverc_tcp_close(connection);
    return -1;
}

static size_t http_stage5_content_length(const char *headers) {
    const char *value = strstr(headers, "Content-Length: ");
    if (!value) return SIZE_MAX;
    value += strlen("Content-Length: ");
    char *end = NULL;
    unsigned long long length = strtoull(value, &end, 10);
    size_t result = (size_t)length;
    if (end == value || (unsigned long long)result != length) return SIZE_MAX;
    return result;
}

static void http_stage5_check_gzip_head(int port) {
    static const char head_request[] =
        "HEAD /static/" HTTP_STAGE5_GZIP_FILE " HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Accept-Encoding: gzip\r\n"
        "Connection: close\r\n\r\n";
    static const char get_request[] =
        "GET /static/" HTTP_STAGE5_GZIP_FILE " HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Accept-Encoding: gzip\r\n"
        "Connection: close\r\n\r\n";
    char head_headers[4096];
    char get_headers[4096];
    int head_result = http_stage5_fetch_headers(
        port, head_request, head_headers, sizeof(head_headers));
    int get_result = http_stage5_fetch_headers(
        port, get_request, get_headers, sizeof(get_headers));
    CHECK(head_result == 0);
    CHECK(get_result == 0);
    if (head_result != 0 || get_result != 0) return;
    CHECK(strstr(head_headers, "Content-Encoding: gzip\r\n") != NULL);
    CHECK(strstr(get_headers, "Content-Encoding: gzip\r\n") != NULL);
    size_t head_length = http_stage5_content_length(head_headers);
    size_t get_length = http_stage5_content_length(get_headers);
    CHECK(head_length != SIZE_MAX && head_length < 1024U);
    CHECK(head_length == get_length);
}

static void http_stage5_check_plain_static_head_raw(int port) {
    char address[64];
    (void)snprintf(address, sizeof(address), "127.0.0.1:%d", port);
    const char *error = NULL;
    neverc_tcp_conn_t *connection = neverc_tcp_dial(address, &error);
    CHECK(connection != NULL);
    if (!connection) return;
    neverc_tcp_set_timeout(connection, 5000);

    static const char head_request[] =
        "HEAD /static/" HTTP_STAGE5_STATIC_FILE " HTTP/1.1\r\n"
        "Host: localhost\r\n\r\n";
    CHECK(http_stage5_tcp_write_all(
              connection, head_request, sizeof(head_request) - 1U) == 0);
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
    CHECK(strncmp(headers, "HTTP/1.1 200", 12U) == 0);
    CHECK(strstr(headers, "Content-Length: 1048593") != NULL);

    static const char post_request[] =
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 10\r\n"
        "Connection: close\r\n\r\n"
        "after-head";
    CHECK(http_stage5_tcp_write_all(
              connection, post_request, sizeof(post_request) - 1U) == 0);
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
    CHECK(strncmp(response, "HTTP/1.1 200", 12U) == 0);
    CHECK(strstr(response, "after-head") != NULL);
    neverc_tcp_close(connection);
}

static neverc_thread_executor_t *http_stage5_start_server(
    http_stage5_server_t *test) {
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    if (!executor) return NULL;
    if (neverc_thread_executor_submit(executor, http_stage5_server_task,
                                       test) != NEVERC_THREAD_OK) {
        (void)neverc_thread_executor_shutdown(executor);
        neverc_thread_executor_free(executor);
        return NULL;
    }
    return executor;
}

static neverc_http_mux_t *http_stage5_mux(void) {
    neverc_http_mux_t *mux = neverc_http_new_mux();
    if (!mux) return NULL;
    neverc_http_mux_handle(mux, "POST /echo", http_stage5_echo);
    neverc_http_mux_handle(mux, "GET /chunks", http_stage5_chunks);
    neverc_http_mux_handle(mux, "GET /auto-chunks", http_stage5_auto_chunks);
    neverc_http_mux_handle(mux, "GET /events", http_stage5_sse);
    neverc_http_mux_handle(
        mux, "POST /request-framing", http_stage5_request_framing);
    neverc_http_mux_handle(mux, "GET /slow", http_stage5_slow);
    if (http_stage5_static_dir[0] != '\0')
        neverc_http_serve_dir(mux, "/static/", http_stage5_static_dir);
    return mux;
}

static void http_stage5_plain_e2e(void) {
    neverc_http_mux_t *mux = http_stage5_mux();
    CHECK(mux != NULL);
    if (!mux) return;
    neverc_http_server_config_t server_config =
        neverc_http_server_config_default();
    server_config.workers = 2;
    server_config.max_connections = 8;
    server_config.gzip_enabled = 1;
    server_config.gzip_level = 6;
    server_config.gzip_min_size = 1U;
    server_config.access_log_enabled = 1;
    server_config.access_log = http_stage5_access_log;
    http_stage5_server_t test;
    memset(&test, 0, sizeof(test));
    test.result = -1;
    test.server = neverc_http_server_new(mux, &server_config);
    CHECK(test.server != NULL);
    if (!test.server) {
        neverc_http_mux_free(mux);
        return;
    }
    neverc_thread_executor_t *executor = http_stage5_start_server(&test);
    CHECK(executor != NULL);
    int port = executor ? http_stage5_wait_for_port(test.server) : -1;
    CHECK(port > 0);

    neverc_http_client_config_t client_config =
        neverc_http_client_config_default();
    client_config.max_idle_per_host = 2;
    neverc_http_client_t *client = neverc_http_client_new(&client_config);
    CHECK(client != NULL);
    char url[128];
    http_stage5_check_plain_static_head_raw(port);
    http_stage5_check_gzip_head(port);
    if (client)
        http_stage5_check_static_head(client, "http", port);
    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/echo", port);
    static const char body[] = "buffered body";
    neverc_http_response_t *response = client
        ? neverc_http_client_do(client, "POST", url,
                                "application/octet-stream", body,
                                sizeof(body) - 1U) : NULL;
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        CHECK(response->status_code == 200);
        CHECK(response->body_len == sizeof(body) - 1U && response->body &&
              memcmp(response->body, body, sizeof(body) - 1U) == 0);
        neverc_http_response_free(response);
    }

    static const char streamed[] = "streamed request and response";
    http_stage5_source_t source = {
        streamed, sizeof(streamed) - 1U, 0U};
    http_stage5_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = NULL;
    neverc_context_t *context = NULL;
    background = neverc_context_background();
    context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    response = client && context
        ? neverc_http_client_do_stream_context(
              client, context, "POST", url, "application/octet-stream",
              (int64_t)(sizeof(streamed) - 1U), http_stage5_source, &source,
              http_stage5_sink, &sink) : NULL;
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        CHECK(response->body == NULL &&
              response->body_len == sizeof(streamed) - 1U);
        CHECK(sink.length == sizeof(streamed) - 1U &&
              memcmp(sink.data, streamed, sink.length) == 0);
        neverc_http_response_free(response);
    }
    if (cancel) {
        neverc_context_cancel_handle_cancel(cancel);
        neverc_context_cancel_handle_free(cancel);
    }
    neverc_context_free(context);
    neverc_context_free(background);

    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunks", port);
    memset(&sink, 0, sizeof(sink));
    cancel = NULL;
    background = neverc_context_background();
    context = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    response = client && context
        ? neverc_http_client_do_stream_context(
              client, context, "GET", url, NULL, 0, NULL, NULL,
              http_stage5_sink, &sink) : NULL;
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        CHECK(sink.length == 9U && memcmp(sink.data, "alphabeta", 9U) == 0);
        CHECK(response->trailers &&
              strstr(response->trailers, "X-Finished: yes") != NULL);
        neverc_http_response_free(response);
    }
    if (cancel) {
        neverc_context_cancel_handle_cancel(cancel);
        neverc_context_cancel_handle_free(cancel);
    }
    neverc_context_free(context);
    neverc_context_free(background);

    (void)snprintf(
        url, sizeof(url), "http://127.0.0.1:%d/auto-chunks", port);
    response = client
        ? neverc_http_client_do(client, "GET", url, NULL, NULL, 0U) : NULL;
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        CHECK(response->body_len == 9U && response->body &&
              memcmp(response->body, "automatic", 9U) == 0);
        neverc_http_response_free(response);
    }

    (void)snprintf(
        url, sizeof(url), "http://127.0.0.1:%d/request-framing", port);
    response = client
        ? neverc_http_client_do(client, "POST", url, NULL, NULL, 0U) : NULL;
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        CHECK(response->body_len == 1U && response->body &&
              response->body[0] == '0');
        neverc_http_response_free(response);
    }

    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/slow", port);
    cancel = NULL;
    background = neverc_context_background();
    context = background
        ? neverc_context_with_timeout_handle(background, 25, &cancel)
        : NULL;
    response = client && context
        ? neverc_http_client_do_context(client, context, "GET", url, NULL,
                                         NULL, 0U) : NULL;
    CHECK(response != NULL && response->error != NULL);
    neverc_http_response_free(response);
    if (cancel) {
        neverc_context_cancel_handle_cancel(cancel);
        neverc_context_cancel_handle_free(cancel);
    }
    neverc_context_free(context);
    neverc_context_free(background);

    neverc_http_client_free(client);
    neverc_http_server_shutdown(test.server);
    if (executor)
        CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    CHECK(neverc_http_server_active_connections(test.server) == 0);
    CHECK(atomic_load_explicit(&access_log_calls, memory_order_relaxed) >= 3);
    neverc_thread_executor_free(executor);
    neverc_http_server_free(test.server);
    neverc_http_mux_free(mux);
}

typedef struct {
    neverc_tcp_listener_t *listener;
    int result;
} http_stage5_raw_server_t;

static int http_stage5_tcp_write_all(neverc_tcp_conn_t *connection,
                                     const void *data, size_t length) {
    const char *bytes = (const char *)data;
    size_t offset = 0;
    while (offset < length) {
        int written =
            neverc_tcp_write(connection, bytes + offset, length - offset);
        if (written <= 0) return -1;
        offset += (size_t)written;
    }
    return 0;
}

static void http_stage5_304_task(void *context) {
    http_stage5_raw_server_t *server =
        (http_stage5_raw_server_t *)context;
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *deadline = background
        ? neverc_context_with_timeout_handle(background, 5000, &cancel)
        : NULL;
    neverc_tcp_conn_t *connection = NULL;
    neverc_net_result_t accepted = deadline
        ? neverc_tcp_accept_context(server->listener, deadline, &connection)
        : (neverc_net_result_t){NEVERC_NET_NOMEM, 0, "accept", 0U};
    if (cancel) {
        neverc_context_cancel_handle_cancel(cancel);
        neverc_context_cancel_handle_free(cancel);
    }
    neverc_context_free(deadline);
    neverc_context_free(background);
    if (accepted.status != NEVERC_NET_OK)
        return;
    if (!connection) return;
    neverc_tcp_set_timeout(connection, 5000);
    char request[1024];
    if (neverc_tcp_read(connection, request, sizeof(request)) <= 0) {
        neverc_tcp_close(connection);
        return;
    }
    static const char response[] =
        "HTTP/1.1 304 Not Modified\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n\r\n";
    server->result = http_stage5_tcp_write_all(
        connection, response, sizeof(response) - 1U);
    neverc_tcp_close(connection);
}

static void http_stage5_accept_304_transfer_encoding(int streaming) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    CHECK(listener != NULL);
    if (!listener) return;
    neverc_tcp_addr_t address;
    int address_ok = neverc_tcp_listener_addr(listener, &address) == 0;
    CHECK(address_ok);
    if (!address_ok) {
        neverc_tcp_listener_close(listener);
        return;
    }

    http_stage5_raw_server_t server = {listener, -1};
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    CHECK(executor != NULL);
    if (!executor ||
        neverc_thread_executor_submit(
            executor, http_stage5_304_task,
            &server) != NEVERC_THREAD_OK) {
        neverc_tcp_listener_close(listener);
        if (executor) neverc_thread_executor_free(executor);
        return;
    }

    neverc_http_client_config_t config =
        neverc_http_client_config_default();
    config.timeout_ms = 5000;
    config.max_idle_per_host = 1;
    neverc_http_client_t *client = neverc_http_client_new(&config);
    CHECK(client != NULL);
    char url[128];
    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/not-modified",
                   address.port);

    neverc_context_t *background =
        streaming ? neverc_context_background() : NULL;
    if (!client || (streaming && !background)) {
        neverc_context_free(background);
        neverc_http_client_free(client);
        (void)neverc_thread_executor_shutdown(executor);
        neverc_thread_executor_free(executor);
        neverc_tcp_listener_close(listener);
        return;
    }
    http_stage5_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    neverc_http_response_t *response =
        streaming
            ? neverc_http_client_do_stream_context(
                  client, background, "GET", url, NULL, 0,
                  NULL, NULL, http_stage5_sink, &sink)
            : neverc_http_client_do(
                  client, "GET", url, NULL, NULL, 0U);
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        CHECK(response->status_code == 304);
        CHECK(response->body == NULL && response->body_len == 0U);
    }
    CHECK(!streaming || sink.length == 0U);
    neverc_http_response_free(response);
    neverc_context_free(background);
    neverc_http_client_free(client);

    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(server.result == 0);
    neverc_thread_executor_free(executor);
    neverc_tcp_listener_close(listener);
}

static void http_stage5_response_framing(void) {
    puts("[response framing]");
    http_stage5_accept_304_transfer_encoding(0);
    http_stage5_accept_304_transfer_encoding(1);
}

typedef struct {
    neverc_tcp_listener_t *listener;
    const char *response;
    size_t response_len;
    int max_accepts;
    char request[2048];
    int requests;
    int result;
} http_stage5_scripted_t;

static int http_stage5_read_request(neverc_tcp_conn_t *connection,
                                    char *buf, size_t capacity) {
    if (!buf || capacity == 0) return -1;
    size_t length = 0;
    buf[0] = '\0';
    while (length + 1U < capacity) {
        int received = neverc_tcp_read(connection, buf + length, 1U);
        if (received <= 0) break;
        length += (size_t)received;
        buf[length] = '\0';
        if (strstr(buf, "\r\n\r\n")) return 0;
    }
    return -1;
}

static void http_stage5_scripted_task(void *context) {
    http_stage5_scripted_t *server = (http_stage5_scripted_t *)context;
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *deadline = background
        ? neverc_context_with_timeout_handle(background, 8000, &cancel)
        : NULL;
    int ok = 0;
    for (int i = 0; i < server->max_accepts; i++) {
        neverc_tcp_conn_t *connection = NULL;
        neverc_net_result_t accepted = deadline
            ? neverc_tcp_accept_context(server->listener, deadline, &connection)
            : (neverc_net_result_t){NEVERC_NET_NOMEM, 0, "accept", 0U};
        if (accepted.status != NEVERC_NET_OK || !connection)
            break;
        neverc_tcp_set_timeout(connection, 5000);
        if (http_stage5_read_request(
                connection, server->request, sizeof(server->request)) == 0)
            server->requests++;
        if (http_stage5_tcp_write_all(
                connection, server->response, server->response_len) != 0) {
            neverc_tcp_close(connection);
            break;
        }
        neverc_tcp_close(connection);
        ok = 1;
    }
    if (cancel) {
        neverc_context_cancel_handle_cancel(cancel);
        neverc_context_cancel_handle_free(cancel);
    }
    neverc_context_free(deadline);
    neverc_context_free(background);
    server->result = ok ? 0 : -1;
}

static neverc_tcp_listener_t *http_stage5_listen_port(int *port) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    if (!listener) return NULL;
    neverc_tcp_addr_t address;
    if (neverc_tcp_listener_addr(listener, &address) != 0) {
        neverc_tcp_listener_close(listener);
        return NULL;
    }
    *port = address.port;
    return listener;
}

static neverc_http_client_t *http_stage5_security_client(int max_redirects) {
    neverc_http_client_config_t config =
        neverc_http_client_config_default();
    config.timeout_ms = 5000;
    config.max_idle_per_host = 0;
    config.max_redirects = max_redirects;
    return neverc_http_client_new(&config);
}

static neverc_thread_executor_t *http_stage5_start_scripted(
    http_stage5_scripted_t *server) {
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1U, 1U);
    if (!executor) return NULL;
    if (neverc_thread_executor_submit(
            executor, http_stage5_scripted_task, server) != NEVERC_THREAD_OK) {
        neverc_thread_executor_free(executor);
        return NULL;
    }
    return executor;
}

static void http_stage5_stop_scripted(neverc_thread_executor_t *executor,
                                      neverc_tcp_listener_t *listener) {
    if (listener) neverc_tcp_listener_close(listener);
    if (executor) {
        CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
        neverc_thread_executor_free(executor);
    }
}

static void http_stage5_client_one_shot(const char *response,
                                        neverc_http_response_t **out) {
    *out = NULL;
    int port = 0;
    neverc_tcp_listener_t *listener = http_stage5_listen_port(&port);
    CHECK(listener != NULL);
    if (!listener) return;
    http_stage5_scripted_t server;
    memset(&server, 0, sizeof(server));
    server.listener = listener;
    server.response = response;
    server.response_len = strlen(response);
    server.max_accepts = 1;
    neverc_thread_executor_t *executor = http_stage5_start_scripted(&server);
    CHECK(executor != NULL);
    neverc_http_client_t *client = http_stage5_security_client(0);
    CHECK(client != NULL);
    if (!executor || !client) {
        neverc_http_client_free(client);
        http_stage5_stop_scripted(executor, listener);
        return;
    }
    char url[128];
    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/x", port);
    *out = neverc_http_client_do(client, "GET", url, NULL, NULL, 0U);
    neverc_http_client_free(client);
    http_stage5_stop_scripted(executor, listener);
}

static void http_stage5_client_security(void) {
    puts("[client security]");

    neverc_http_response_t *resp =
        neverc_http_get("http://127.0.0.1,example.com/");
    CHECK(resp != NULL && resp->error != NULL);
    neverc_http_response_free(resp);

    resp = neverc_http_get("http://user@127.0.0.1/");
    CHECK(resp != NULL && resp->error != NULL);
    neverc_http_response_free(resp);

    static const char both_framing[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n\r\n"
        "hello";
    http_stage5_client_one_shot(both_framing, &resp);
    CHECK(resp != NULL && resp->error != NULL);
    neverc_http_response_free(resp);

    /* HTTP/1.0 has no Transfer-Encoding. Accepting chunked here used to
     * decode the body while a 1.0 hop treated the same bytes as identity. */
    static const char http10_chunked[] =
        "HTTP/1.0 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n\r\n"
        "5\r\nhello\r\n0\r\n\r\n";
    http_stage5_client_one_shot(http10_chunked, &resp);
    CHECK(resp != NULL && resp->error != NULL);
    neverc_http_response_free(resp);

    /* A bare LF in the status line used to swallow Transfer-Encoding into the
     * reason phrase, so Content-Length won and keep-alive framing desynced. */
    static const char lf_status[] =
        "HTTP/1.1 200 OK\n"
        "Transfer-Encoding: chunked\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n\r\n"
        "hello";
    http_stage5_client_one_shot(lf_status, &resp);
    CHECK(resp != NULL && resp->error != NULL);
    neverc_http_response_free(resp);

    static const char smuggled_location[] =
        "HTTP/1.1 302 Found\r\n"
        "Location: /next\nHost: evil.example\r\n"
        "Connection: close\r\n\r\n";
    http_stage5_client_one_shot(smuggled_location, &resp);
    CHECK(resp != NULL && resp->error != NULL);
    neverc_http_response_free(resp);

    int loop_port = 0;
    neverc_tcp_listener_t *loop_listener =
        http_stage5_listen_port(&loop_port);
    CHECK(loop_listener != NULL);
    if (loop_listener) {
        static const char loop_response[] =
            "HTTP/1.1 302 Found\r\n"
            "Location: /loop\r\n"
            "Connection: close\r\n\r\n";
        http_stage5_scripted_t loop_server;
        memset(&loop_server, 0, sizeof(loop_server));
        loop_server.listener = loop_listener;
        loop_server.response = loop_response;
        loop_server.response_len = sizeof(loop_response) - 1U;
        loop_server.max_accepts = 8;
        neverc_thread_executor_t *loop_executor =
            http_stage5_start_scripted(&loop_server);
        CHECK(loop_executor != NULL);
        neverc_http_client_t *client = http_stage5_security_client(2);
        CHECK(client != NULL);
        char url[128];
        (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/loop",
                       loop_port);
        resp = client
            ? neverc_http_client_do(client, "GET", url, NULL, NULL, 0U)
            : NULL;
        CHECK(resp != NULL && resp->error != NULL &&
              strstr(resp->error, "too many redirects") != NULL);
        neverc_http_response_free(resp);
        neverc_http_client_free(client);
        http_stage5_stop_scripted(loop_executor, loop_listener);
    }

    int first_port = 0;
    int second_port = 0;
    neverc_tcp_listener_t *first_listener =
        http_stage5_listen_port(&first_port);
    neverc_tcp_listener_t *second_listener =
        http_stage5_listen_port(&second_port);
    CHECK(first_listener != NULL && second_listener != NULL);
    if (first_listener && second_listener) {
        char location_response[256];
        int location_len = snprintf(
            location_response, sizeof(location_response),
            "HTTP/1.1 302 Found\r\n"
            "Location: http://127.0.0.1:%d/end\r\n"
            "Set-Cookie: session=leaked\r\n"
            "Connection: close\r\n\r\n",
            second_port);
        CHECK(location_len > 0 &&
              (size_t)location_len < sizeof(location_response));
        static const char final_response[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n\r\n"
            "ok";
        http_stage5_scripted_t first_server;
        http_stage5_scripted_t second_server;
        memset(&first_server, 0, sizeof(first_server));
        memset(&second_server, 0, sizeof(second_server));
        first_server.listener = first_listener;
        first_server.response = location_response;
        first_server.response_len = (size_t)location_len;
        first_server.max_accepts = 1;
        second_server.listener = second_listener;
        second_server.response = final_response;
        second_server.response_len = sizeof(final_response) - 1U;
        second_server.max_accepts = 1;
        neverc_thread_executor_t *first_executor =
            http_stage5_start_scripted(&first_server);
        neverc_thread_executor_t *second_executor =
            http_stage5_start_scripted(&second_server);
        CHECK(first_executor != NULL && second_executor != NULL);
        neverc_http_client_t *client = http_stage5_security_client(5);
        CHECK(client != NULL);
        char url[128];
        (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/start",
                       first_port);
        resp = client
            ? neverc_http_client_do(client, "GET", url, NULL, NULL, 0U)
            : NULL;
        CHECK(resp != NULL && resp->error == NULL &&
              resp->status_code == 200);
        char expected_host[80];
        (void)snprintf(expected_host, sizeof(expected_host),
                       "Host: 127.0.0.1:%d\r\n", second_port);
        char original_host[80];
        (void)snprintf(original_host, sizeof(original_host),
                       "Host: 127.0.0.1:%d\r\n", first_port);
        CHECK(strstr(second_server.request, expected_host) != NULL);
        CHECK(strstr(second_server.request, original_host) == NULL);
        CHECK(strstr(second_server.request, "Cookie:") == NULL);
        CHECK(strstr(second_server.request, "session=leaked") == NULL);
        neverc_http_response_free(resp);
        neverc_http_client_free(client);
        http_stage5_stop_scripted(first_executor, first_listener);
        http_stage5_stop_scripted(second_executor, second_listener);
    }
}

static void http_stage5_tls_e2e(void) {
    neverc_network_test_files_t files;
    memset(&files, 0, sizeof(files));
    CHECK(neverc_network_test_write_certs("http-stage5", &files) == 0);
    neverc_http_mux_t *mux = http_stage5_mux();
    CHECK(mux != NULL);
    if (!mux) {
        neverc_network_test_remove_certs(&files);
        return;
    }
    neverc_http_server_config_t server_config =
        neverc_http_server_config_default();
    server_config.workers = 2;
    http_stage5_server_t test;
    memset(&test, 0, sizeof(test));
    test.files = &files;
    test.use_tls = 1;
    test.result = -1;
    test.server = neverc_http_server_new(mux, &server_config);
    CHECK(test.server != NULL);
    neverc_thread_executor_t *executor = test.server
        ? http_stage5_start_server(&test) : NULL;
    CHECK(executor != NULL);
    int port = executor ? http_stage5_wait_for_port(test.server) : -1;
    CHECK(port > 0);
    char url[128];
    (void)snprintf(url, sizeof(url), "https://localhost:%d/echo", port);

    neverc_http_client_config_t trusted_config =
        neverc_http_client_config_default();
    trusted_config.root_cert_file = files.ca;
    trusted_config.client_cert_file = files.client_cert;
    trusted_config.client_key_file = files.client_key;
    neverc_http_client_t *trusted = neverc_http_client_new(&trusted_config);
    CHECK(trusted != NULL);
    if (trusted)
        http_stage5_check_static_head(trusted, "https", port);
    neverc_http_response_t *response = trusted
        ? neverc_http_client_do(trusted, "POST", url, "text/plain", "tls",
                                3U) : NULL;
    if (response && response->error)
        printf("  trusted HTTPS error: %s\n", response->error);
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        CHECK(response->status_code == 200 && response->body_len == 3U &&
              response->body && memcmp(response->body, "tls", 3U) == 0);
        neverc_http_response_free(response);
    }

    (void)snprintf(url, sizeof(url), "https://localhost:%d/events", port);
    response = trusted
        ? neverc_http_client_do(trusted, "GET", url, NULL, NULL, 0U) : NULL;
    CHECK(response != NULL && response->error == NULL);
    if (response) {
        static const char expected[] =
            "id: 7\n"
            "event: update\n"
            "data: first\n"
            "data: second\n"
            "data: \n"
            "\n"
            "retry: 1500\n\n";
        CHECK(response->body_len == sizeof(expected) - 1U &&
              response->body &&
              memcmp(response->body, expected, sizeof(expected) - 1U) == 0);
        neverc_http_response_free(response);
    }
    neverc_http_client_free(trusted);

    neverc_http_client_config_t rejected_config =
        neverc_http_client_config_default();
    neverc_http_client_t *rejected =
        neverc_http_client_new(&rejected_config);
    response = rejected
        ? neverc_http_client_do(rejected, "POST", url, "text/plain", "tls",
                                3U) : NULL;
    CHECK(response != NULL && response->error != NULL);
    neverc_http_response_free(response);
    neverc_http_client_free(rejected);

    trusted_config.client_key_file = NULL;
    CHECK(neverc_http_client_new(&trusted_config) == NULL);

    neverc_http_server_shutdown(test.server);
    if (executor)
        CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(test.result == 0);
    neverc_thread_executor_free(executor);
    neverc_http_server_free(test.server);
    neverc_http_mux_free(mux);
    neverc_network_test_remove_certs(&files);
}

int main(void) {
    puts("HTTP stage 5 production API test suite:");
    int fixture_result = http_stage5_write_static_file();
    CHECK(fixture_result == 0);
    if (fixture_result != 0) {
        http_stage5_remove_static_file();
        printf("http stage5: %d checks, %d failed\n",
               tests_run, tests_failed);
        return 1;
    }
    http_stage5_plain_e2e();
    http_stage5_response_framing();
    http_stage5_client_security();
    http_stage5_tls_e2e();
    http_stage5_remove_static_file();
    printf("http stage5: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
