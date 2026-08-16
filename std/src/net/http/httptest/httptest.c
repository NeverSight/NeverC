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
static int strncasecmp(const char *a, const char *b, size_t n) {
    return _strnicmp(a, b, n);
}
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

static int httptest_is_tchar(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '!' || c == '#' || c == '$' ||
           c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
           c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
           c == '|' || c == '~';
}

static int httptest_valid_token(const char *s, size_t length) {
    if (!s || length == 0) return 0;
    for (size_t i = 0; i < length; i++)
        if (!httptest_is_tchar((unsigned char)s[i])) return 0;
    return 1;
}

static int httptest_valid_field_value(const char *s, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) return 0;
    }
    return 1;
}

static int httptest_valid_port(const char *s, size_t length) {
    if (!s || length == 0) return 0;
    unsigned value = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < '0' || c > '9') return 0;
        unsigned digit = (unsigned)(c - '0');
        if (value > (65535U - digit) / 10U) return 0;
        value = value * 10U + digit;
    }
    return value > 0;
}

static int httptest_valid_host(const char *value, size_t length) {
    if (!value || length == 0) return 0;
    if (value[0] == '[') {
        const char *close = (const char *)memchr(value, ']', length);
        if (!close || close == value + 1) return 0;
        size_t inner = (size_t)(close - value - 1);
        int has_colon = 0;
        for (size_t i = 0; i < inner; i++) {
            unsigned char c = (unsigned char)value[1 + i];
            if (c == ':') has_colon = 1;
            if (c <= 0x20 || c >= 0x7f || c == '/' || c == '\\' ||
                c == '?' || c == '#' || c == '@' || c == '[' || c == ']')
                return 0;
        }
        if (!has_colon &&
            !(inner > 2 && (value[1] == 'v' || value[1] == 'V')))
            return 0;
        size_t after = length - (size_t)(close - value) - 1;
        if (after == 0) return 1;
        return close[1] == ':' && httptest_valid_port(close + 2, after - 1);
    }
    const char *colon = (const char *)memchr(value, ':', length);
    size_t host_length = colon ? (size_t)(colon - value) : length;
    if (host_length == 0) return 0;
    for (size_t i = 0; i < host_length; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c <= 0x20 || c >= 0x7f || c == '/' || c == '\\' ||
            c == '?' || c == '#' || c == '@' || c == '[' || c == ']' ||
            c == ',' || c == ':')
            return 0;
    }
    if (!colon) return 1;
    if (memchr(colon + 1, ':', length - host_length - 1)) return 0;
    return httptest_valid_port(colon + 1, length - host_length - 1);
}

static void httptest_trim_ows(const char **value, size_t *length) {
    while (*length > 0 && (**value == ' ' || **value == '\t')) {
        (*value)++;
        (*length)--;
    }
    while (*length > 0 && ((*value)[*length - 1] == ' ' ||
                            (*value)[*length - 1] == '\t'))
        (*length)--;
}

static int httptest_name_is(const char *name, size_t length,
                            const char *expected) {
    size_t expected_length = strlen(expected);
    return length == expected_length &&
           strncasecmp(name, expected, length) == 0;
}

static const char *httptest_find_crlf(const char *start, const char *end) {
    for (const char *p = start; p + 1 < end; p++)
        if (p[0] == '\r' && p[1] == '\n') return p;
    return NULL;
}

static int httptest_parse_content_length(const char *value, size_t length,
                                         size_t *result) {
    if (!value || length == 0) return -1;
    size_t parsed = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < '0' || c > '9' || parsed > (65535U - (size_t)(c - '0')) / 10U)
            return -1;
        parsed = parsed * 10U + (size_t)(c - '0');
    }
    *result = parsed;
    return 0;
}

typedef struct {
    char method[16];
    char path[2048];
    char query_buf[2048];
    char host[256];
    char version[16];
    char raw_headers[8192];
    int nheaders;
    const char *query;
    const char *body;
    size_t body_len;
    size_t header_size;
    size_t need;
} httptest_parsed_t;

/* 0 = complete, -1 = incomplete, -2 = invalid. */
static int httptest_parse_request(const char *raw, size_t raw_length,
                                  httptest_parsed_t *out) {
    memset(out, 0, sizeof(*out));
    out->need = 0;
    if (!raw) return -2;

    const char *end = raw + raw_length;
    const char *header_end = NULL;
    for (const char *p = raw; p + 3 < end; p++) {
        if (p[0] == '\r' && p[1] == '\n' &&
            p[2] == '\r' && p[3] == '\n') {
            header_end = p;
            break;
        }
    }
    if (!header_end) return -1;

    const char *request_line_end = httptest_find_crlf(raw, header_end + 2);
    if (!request_line_end || request_line_end == raw) return -2;
    const char *method_end = (const char *)memchr(
        raw, ' ', (size_t)(request_line_end - raw));
    if (!method_end ||
        !httptest_valid_token(raw, (size_t)(method_end - raw)) ||
        (size_t)(method_end - raw) >= sizeof(out->method))
        return -2;
    const char *target = method_end + 1;
    const char *target_end = (const char *)memchr(
        target, ' ', (size_t)(request_line_end - target));
    if (!target_end || target_end == target ||
        memchr(target_end + 1, ' ',
               (size_t)(request_line_end - target_end - 1)))
        return -2;
    size_t target_length = (size_t)(target_end - target);
    int asterisk_form = target_length == 1 && target[0] == '*';
    if (target[0] != '/' && !asterisk_form) return -2;
    for (size_t i = 0; i < target_length; i++) {
        unsigned char c = (unsigned char)target[i];
        if (c <= 0x20 || c == 0x7f || c == '#') return -2;
    }

    const char *version = target_end + 1;
    size_t version_length = (size_t)(request_line_end - version);
    int is_http_10 = version_length == 8 &&
                     memcmp(version, "HTTP/1.0", 8) == 0;
    int is_http_11 = version_length == 8 &&
                     memcmp(version, "HTTP/1.1", 8) == 0;
    if ((!is_http_10 && !is_http_11) ||
        version_length >= sizeof(out->version))
        return -2;

    memcpy(out->method, raw, (size_t)(method_end - raw));
    out->method[method_end - raw] = '\0';
    if (asterisk_form && strcmp(out->method, "OPTIONS") != 0) return -2;

    const char *query = (const char *)memchr(target, '?', target_length);
    if (query) {
        size_t path_length = (size_t)(query - target);
        size_t query_length = (size_t)(target_end - query - 1);
        if (path_length >= sizeof(out->path) ||
            query_length >= sizeof(out->query_buf))
            return -2;
        memcpy(out->path, target, path_length);
        out->path[path_length] = '\0';
        memcpy(out->query_buf, query + 1, query_length);
        out->query_buf[query_length] = '\0';
        out->query = out->query_buf;
    } else {
        if (target_length >= sizeof(out->path)) return -2;
        memcpy(out->path, target, target_length);
        out->path[target_length] = '\0';
    }
    memcpy(out->version, version, version_length);
    out->version[version_length] = '\0';

    int host_seen = 0;
    int content_length_seen = 0;
    int transfer_encoding_seen = 0;
    size_t content_length = 0;
    size_t raw_used = 0;
    const char *cursor = request_line_end + 2;
    while (cursor < header_end) {
        const char *line_end = httptest_find_crlf(cursor, header_end + 2);
        if (!line_end || line_end == cursor ||
            *cursor == ' ' || *cursor == '\t')
            return -2;
        const char *colon = (const char *)memchr(
            cursor, ':', (size_t)(line_end - cursor));
        if (!colon ||
            !httptest_valid_token(cursor, (size_t)(colon - cursor)))
            return -2;
        const char *value = colon + 1;
        size_t value_length = (size_t)(line_end - value);
        httptest_trim_ows(&value, &value_length);
        size_t name_length = (size_t)(colon - cursor);
        if (!httptest_valid_field_value(value, value_length)) return -2;

        if (httptest_name_is(cursor, name_length, "Host")) {
            if (host_seen || value_length == 0 ||
                value_length >= sizeof(out->host) ||
                !httptest_valid_host(value, value_length))
                return -2;
            host_seen = 1;
            memcpy(out->host, value, value_length);
            out->host[value_length] = '\0';
        } else if (httptest_name_is(cursor, name_length, "Content-Length")) {
            if (content_length_seen ||
                httptest_parse_content_length(
                    value, value_length, &content_length) != 0)
                return -2;
            content_length_seen = 1;
        } else if (httptest_name_is(cursor, name_length,
                                    "Transfer-Encoding")) {
            if (transfer_encoding_seen || value_length != 7 ||
                strncasecmp(value, "chunked", 7) != 0)
                return -2;
            transfer_encoding_seen = 1;
        }

        if (raw_used + name_length + value_length + 2 >=
            sizeof(out->raw_headers))
            return -2;
        memcpy(out->raw_headers + raw_used, cursor, name_length);
        raw_used += name_length;
        out->raw_headers[raw_used++] = '\0';
        memcpy(out->raw_headers + raw_used, value, value_length);
        raw_used += value_length;
        out->raw_headers[raw_used++] = '\0';
        out->nheaders++;
        cursor = line_end + 2;
    }

    if (is_http_11 && !host_seen) return -2;
    if (content_length_seen && transfer_encoding_seen) return -2;
    if (is_http_10 && transfer_encoding_seen) return -2;
    if (transfer_encoding_seen) return -2;

    out->header_size = (size_t)(header_end + 4 - raw);
    out->need = out->header_size + content_length;
    if (raw_length < out->need) return -1;
    if (content_length > 0) {
        out->body = raw + out->header_size;
        out->body_len = content_length;
    }
    return 0;
}

static void httptest_write_error(neverc_tcp_conn_t *conn, int status,
                                 const char *text) {
    char header[128];
    int length = snprintf(header, sizeof(header),
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Length: 0\r\n"
                          "Connection: close\r\n\r\n",
                          status, text);
    if (length > 0 && (size_t)length < sizeof(header))
        neverc_tcp_write(conn, header, (size_t)length);
}

static void handle_test_conn(neverc_tcp_conn_t *conn,
                              neverc_http_handler_func_t handler) {
    char buf[65536];
    size_t total = 0;
    httptest_parsed_t parsed;
    memset(&parsed, 0, sizeof(parsed));

    neverc_tcp_set_timeout(conn, 5000);

    for (;;) {
        int n = neverc_tcp_read(conn, buf + total, sizeof(buf) - total - 1);
        if (n <= 0) break;
        total += (size_t)n;
        buf[total] = '\0';
        int result = httptest_parse_request(buf, total, &parsed);
        if (result == 0) break;
        if (result == -2) {
            httptest_write_error(conn, 400, "Bad Request");
            return;
        }
        if (total >= sizeof(buf) - 1) {
            httptest_write_error(conn, 413, "Payload Too Large");
            return;
        }
    }
    if (total == 0) return;
    if (httptest_parse_request(buf, total, &parsed) != 0) {
        httptest_write_error(conn, 400, "Bad Request");
        return;
    }

    neverc_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = parsed.method;
    req.path = parsed.path;
    req.query = parsed.query;
    req.http_version = parsed.version;
    req.host = parsed.host[0] ? parsed.host : NULL;
    req.body = parsed.body;
    req.body_len = parsed.body_len;
    req.raw_headers = parsed.nheaders > 0 ? parsed.raw_headers : NULL;
    req.nheaders = parsed.nheaders;

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
