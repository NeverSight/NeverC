#include "neverc/std/context.h"
#include "neverc/std/net/http.h"
#include "neverc/std/thread.h"
#include "neverc/std/time.h"
#include "network_test_support.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run;
static int tests_failed;
static atomic_int access_log_calls;

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
    neverc_http_mux_handle(mux, "GET /slow", http_stage5_slow);
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
    neverc_context_t *context = neverc_context_with_timeout_handle(
        neverc_context_background(), 5000, &cancel);
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

    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/chunks", port);
    memset(&sink, 0, sizeof(sink));
    cancel = NULL;
    context = neverc_context_with_timeout_handle(
        neverc_context_background(), 5000, &cancel);
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

    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/slow", port);
    cancel = NULL;
    context = neverc_context_with_timeout_handle(
        neverc_context_background(), 25, &cancel);
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
    http_stage5_plain_e2e();
    http_stage5_tls_e2e();
    printf("http stage5: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
