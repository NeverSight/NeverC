#include "neverc/std/net/http/httptest.h"
#include "neverc/std/net/tcp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#include <strings.h>
#else
#include <windows.h>
static int strcasecmp(const char *a, const char *b) { return _stricmp(a, b); }
#endif

/* ======================================================================
 * Test Server — uses memory writer to capture handler output
 * ====================================================================== */

struct neverc_httptest_server {
    neverc_tcp_listener_t      *listener;
    neverc_http_handler_func_t  handler;
    char                        url[128];
    char                        addr[64];
    volatile int                running;
#ifdef _WIN32
    HANDLE                      thread;
#else
    pthread_t                   thread;
#endif
};

static void handle_test_conn(neverc_tcp_conn_t *conn,
                              neverc_http_handler_func_t handler) {
    char buf[65536];
    size_t total = 0;

    neverc_tcp_set_timeout(conn, 5000);

    for (;;) {
        int n = neverc_tcp_read(conn, buf + total, sizeof(buf) - total - 1);
        if (n <= 0) break;
        total += (size_t)n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
        if (total >= sizeof(buf) - 1) break;
    }
    if (total == 0) return;

    /* Parse request line */
    char method[16] = {0}, raw_path[2048] = {0};
    sscanf(buf, "%15s %2047s", method, raw_path);

    char path[2048];
    strncpy(path, raw_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    char *query = strchr(path, '?');
    if (query) *query++ = '\0';

    char *body_start = strstr(buf, "\r\n\r\n");
    const char *body = NULL;
    size_t body_len = 0;
    if (body_start) {
        body_start += 4;
        body = body_start;
        body_len = total - (size_t)(body_start - buf);
    }

    neverc_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = method;
    req.path = path;
    req.query = query;
    req.http_version = "HTTP/1.1";
    req.body = body;
    req.body_len = body_len;

    /* Create a memory writer for the handler to write to */
    neverc_http_response_writer_t *w = neverc_http_memory_writer_new();
    if (!w) return;

    /* Call the actual handler */
    handler(&req, w);

    /* Extract response from memory writer */
    char *resp_body = NULL;
    size_t resp_body_len = 0;
    int status = neverc_http_memory_writer_result(w, &resp_body, &resp_body_len);

    /* Build raw HTTP response */
    char resp_hdr[4096];
    int rlen = snprintf(resp_hdr, sizeof(resp_hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, neverc_http_status_text(status), resp_body_len);

    neverc_tcp_write(conn, resp_hdr, (size_t)rlen);
    if (resp_body && resp_body_len > 0)
        neverc_tcp_write(conn, resp_body, resp_body_len);

    free(resp_body);
    neverc_http_memory_writer_free(w);
}

#ifdef _WIN32
static DWORD WINAPI server_thread_func(LPVOID arg) {
#else
static void *server_thread_func(void *arg) {
#endif
    neverc_httptest_server_t *ts = (neverc_httptest_server_t *)arg;

    while (ts->running) {
        const char *err = NULL;
        neverc_tcp_conn_t *conn = neverc_tcp_accept(ts->listener, &err);
        if (!conn) {
            if (!ts->running) break;
            continue;
        }
        handle_test_conn(conn, ts->handler);
        neverc_tcp_close(conn);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

neverc_httptest_server_t *neverc_httptest_new_server(
    neverc_http_handler_func_t handler) {
    if (!handler) return NULL;

    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    if (!ln) return NULL;

    neverc_tcp_addr_t addr;
    neverc_tcp_listener_addr(ln, &addr);

    neverc_httptest_server_t *ts =
        (neverc_httptest_server_t *)calloc(1, sizeof(*ts));
    if (!ts) {
        neverc_tcp_listener_close(ln);
        return NULL;
    }
    ts->listener = ln;
    ts->handler = handler;
    ts->running = 1;
    snprintf(ts->url, sizeof(ts->url), "http://%s", addr.addr);
    snprintf(ts->addr, sizeof(ts->addr), "%s", addr.addr);

#ifdef _WIN32
    ts->thread = CreateThread(NULL, 0, server_thread_func, ts, 0, NULL);
#else
    pthread_create(&ts->thread, NULL, server_thread_func, ts);
#endif

#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);
#endif

    return ts;
}

const char *neverc_httptest_url(neverc_httptest_server_t *ts) {
    return ts ? ts->url : NULL;
}

const char *neverc_httptest_addr(neverc_httptest_server_t *ts) {
    return ts ? ts->addr : NULL;
}

void neverc_httptest_close(neverc_httptest_server_t *ts) {
    if (!ts) return;
    ts->running = 0;

    /* Wake the server thread's blocking accept() by making a dummy
       connection.  On Linux, close() on a listener fd does NOT reliably
       unblock another thread's accept() — this is undefined behavior in
       POSIX.  A dummy connect always works and avoids a use-after-free
       (the old code freed the listener before the thread could exit). */
    const char *err = NULL;
    neverc_tcp_conn_t *dummy = neverc_tcp_dial(ts->addr, &err);
    if (dummy) neverc_tcp_close(dummy);

#ifdef _WIN32
    WaitForSingleObject(ts->thread, 3000);
    CloseHandle(ts->thread);
#else
    pthread_join(ts->thread, NULL);
#endif

    neverc_tcp_listener_close(ts->listener);
    free(ts);
}

/* ======================================================================
 * Response Recorder
 * ====================================================================== */

neverc_httptest_recorder_t *neverc_httptest_new_recorder(void) {
    neverc_httptest_recorder_t *rec =
        (neverc_httptest_recorder_t *)calloc(1, sizeof(*rec));
    if (rec) rec->status_code = 200;
    return rec;
}

neverc_http_response_writer_t *neverc_httptest_recorder_writer(
    neverc_httptest_recorder_t *rec) {
    (void)rec;
    return neverc_http_memory_writer_new();
}

const char *neverc_httptest_recorder_header(
    neverc_httptest_recorder_t *rec, const char *name) {
    if (!rec || !name) return NULL;
    for (int i = 0; i < rec->nheaders; i++) {
        if (strcasecmp(rec->header_names[i], name) == 0)
            return rec->header_values[i];
    }
    return NULL;
}

void neverc_httptest_recorder_free(neverc_httptest_recorder_t *rec) {
    if (!rec) return;
    free(rec->body);
    for (int i = 0; i < rec->nheaders; i++) {
        free(rec->header_names[i]);
        free(rec->header_values[i]);
    }
    free(rec);
}
