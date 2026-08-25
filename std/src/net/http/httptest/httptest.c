#include "neverc/std/net/http/httptest.h"
#include "neverc/std/net/tcp.h"
#include "../_http_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

/* ======================================================================
 * Test Server — uses memory writer to capture handler output
 * ====================================================================== */

struct neverc_httptest_server {
    neverc_tcp_listener_t      *listener;
    neverc_http_handler_func_t  handler;
    char                        url[128];
    char                        addr[64];
    uint16_t                    port;
    atomic_int                  running;
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

static int httptest_host_reg_name_byte(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z'))
        return 1;
    switch (c) {
    case '!': case '$': case '%': case '&': case '\'':
    case '(': case ')': case '*': case '+':
    case '-': case '.': case ';': case '=':
    case '_': case '~':
        return 1;
    default:
        return 0;
    }
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
            else if (!httptest_host_reg_name_byte(c))
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
        if (!httptest_host_reg_name_byte((unsigned char)value[i]))
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
    char content_type[256];
    char version[16];
    char raw_headers[8192];
    int nheaders;
    const char *query;
    const char *body;
    char *owned_body;
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
    const char *target_query = (const char *)memchr(target, '?', target_length);
    size_t path_length = target_query
        ? (size_t)(target_query - target) : target_length;
    if (path_length >= 2 &&
        neverc_url_path_n_is_protocol_relative(target, path_length))
        return -2;
    for (size_t i = 0; i < target_length; i++) {
        unsigned char c = (unsigned char)target[i];
        if (c <= 0x20 || c == 0x7f || c == '#' || c == '\\') return -2;
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
    if (strcmp(out->method, "CONNECT") == 0) return -2;
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
        } else if (httptest_name_is(cursor, name_length, "Content-Type")) {
            if (value_length >= sizeof(out->content_type))
                return -2;
            memcpy(out->content_type, value, value_length);
            out->content_type[value_length] = '\0';
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
    out->header_size = (size_t)(header_end + 4 - raw);
    if (transfer_encoding_seen) {
        const char *cursor = raw + out->header_size;
        const char *end = raw + raw_length;
        char *decoded = NULL;
        size_t decoded_len = 0;
        size_t decoded_cap = 0;
        for (;;) {
            const char *line_end = httptest_find_crlf(cursor, end);
            if (!line_end) {
                free(decoded);
                return -1;
            }
            size_t chunk = 0;
            size_t digits = 0;
            const char *p = cursor;
            if (p == line_end) {
                free(decoded);
                return -2;
            }
            /* Go net/http/internal/chunked parseHexUint: a size line with
             * no hex digits (`;ext`) is invalid, not a last-chunk of 0. */
            while (p < line_end && *p != ';') {
                unsigned char c = (unsigned char)*p++;
                unsigned digit;
                if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
                else {
                    free(decoded);
                    return -2;
                }
                if (chunk > (65535U - digit) / 16U) {
                    free(decoded);
                    return -2;
                }
                chunk = chunk * 16U + digit;
                digits++;
            }
            if (digits == 0) {
                free(decoded);
                return -2;
            }
            cursor = line_end + 2;
            if (chunk == 0) {
                line_end = httptest_find_crlf(cursor, end);
                if (!line_end) {
                    free(decoded);
                    return -1;
                }
                if (line_end != cursor) {
                    free(decoded);
                    return -2;
                }
                out->owned_body = decoded;
                out->body = decoded;
                out->body_len = decoded_len;
                return 0;
            }
            if ((size_t)(end - cursor) < chunk + 2U) {
                free(decoded);
                return -1;
            }
            if (cursor[chunk] != '\r' || cursor[chunk + 1U] != '\n') {
                free(decoded);
                return -2;
            }
            if (decoded_len > 65535U - chunk) {
                free(decoded);
                return -2;
            }
            if (decoded_len + chunk > decoded_cap) {
                size_t next = decoded_cap < 256U ? 256U : decoded_cap;
                while (next < decoded_len + chunk) {
                    if (next > 65535U / 2U) {
                        next = decoded_len + chunk;
                        break;
                    }
                    next *= 2U;
                }
                char *grown = (char *)realloc(decoded, next);
                if (!grown) {
                    free(decoded);
                    return -2;
                }
                decoded = grown;
                decoded_cap = next;
            }
            if (chunk > 0)
                memcpy(decoded + decoded_len, cursor, chunk);
            decoded_len += chunk;
            cursor += chunk + 2U;
        }
    }
    out->need = out->header_size + content_length;
    if (raw_length < out->need) return -1;
    if (content_length > 0) {
        out->body = raw + out->header_size;
        out->body_len = content_length;
    }
    return 0;
}

static int httptest_buf_append(char **buf, size_t *length, size_t *capacity,
                               const void *data, size_t data_len) {
    if (!buf || !length || !capacity || (!data && data_len != 0) ||
        *length > SIZE_MAX - data_len - 1U)
        return -1;
    size_t required = *length + data_len + 1U;
    if (required > *capacity) {
        size_t next = *capacity < 256U ? 256U : *capacity;
        while (next < required) {
            if (next > SIZE_MAX / 2U) {
                next = required;
                break;
            }
            next *= 2U;
        }
        char *grown = (char *)realloc(*buf, next);
        if (!grown) return -1;
        *buf = grown;
        *capacity = next;
    }
    if (data_len != 0)
        memcpy(*buf + *length, data, data_len);
    *length += data_len;
    (*buf)[*length] = '\0';
    return 0;
}

static int httptest_write_all(neverc_tcp_conn_t *conn, const void *data,
                              size_t length) {
    const char *bytes = (const char *)data;
    size_t offset = 0;
    while (offset < length) {
        int written = neverc_tcp_write(
            conn, bytes + offset, length - offset);
        if (written <= 0) return -1;
        offset += (size_t)written;
    }
    return 0;
}

static int httptest_emit_response(neverc_tcp_conn_t *conn,
                                  neverc_http_response_writer_t *w,
                                  const char *body, size_t body_len) {
    char *hdr = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char line[128];
    int n = snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n",
                     w->status, neverc_http_status_text(w->status));
    if (n < 0 || (size_t)n >= sizeof(line) ||
        httptest_buf_append(&hdr, &length, &capacity, line, (size_t)n) != 0)
        goto fail;

    int has_content_type = 0;
    for (int i = 0; i < w->nheaders; i++) {
        if (strcasecmp(w->header_names[i], "Content-Length") == 0 ||
            strcasecmp(w->header_names[i], "Transfer-Encoding") == 0 ||
            strcasecmp(w->header_names[i], "Connection") == 0)
            continue;
        if (httptest_buf_append(&hdr, &length, &capacity,
                                w->header_names[i],
                                strlen(w->header_names[i])) != 0 ||
            httptest_buf_append(&hdr, &length, &capacity, ": ", 2) != 0 ||
            httptest_buf_append(&hdr, &length, &capacity,
                                w->header_values[i],
                                strlen(w->header_values[i])) != 0 ||
            httptest_buf_append(&hdr, &length, &capacity, "\r\n", 2) != 0)
            goto fail;
        if (strcasecmp(w->header_names[i], "Content-Type") == 0)
            has_content_type = 1;
    }

    static const char default_ct[] =
        "Content-Type: text/plain; charset=utf-8\r\n";
    if (!has_content_type &&
        httptest_buf_append(&hdr, &length, &capacity, default_ct,
                            sizeof(default_ct) - 1U) != 0)
        goto fail;

    int emit_content_length =
        w->status >= 200 && w->status != 204 &&
        (w->has_content_length_override || w->status != 304);
    if (emit_content_length) {
        size_t content_length = w->has_content_length_override
            ? w->content_length_override : body_len;
        n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n",
                     content_length);
        if (n < 0 || (size_t)n >= sizeof(line) ||
            httptest_buf_append(&hdr, &length, &capacity, line,
                                (size_t)n) != 0)
            goto fail;
    }

    static const char close_end[] = "Connection: close\r\n\r\n";
    if (httptest_buf_append(&hdr, &length, &capacity, close_end,
                            sizeof(close_end) - 1U) != 0)
        goto fail;

    if (httptest_write_all(conn, hdr, length) != 0) goto fail;
    int body_forbidden = w->head_request || w->status < 200 ||
                         w->status == 204 || w->status == 304;
    if (!body_forbidden && body && body_len > 0 &&
        httptest_write_all(conn, body, body_len) != 0)
        goto fail;
    free(hdr);
    return 0;

fail:
    free(hdr);
    return -1;
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
        (void)httptest_write_all(conn, header, (size_t)length);
}

static void handle_test_conn(neverc_tcp_conn_t *conn,
                              neverc_http_handler_func_t handler) {
    char buf[65536];
    size_t total = 0;
    httptest_parsed_t parsed;
    memset(&parsed, 0, sizeof(parsed));

    neverc_tcp_set_timeout(conn, 5000);

    int parsed_ok = 0;
    for (;;) {
        int n = neverc_tcp_read(conn, buf + total, sizeof(buf) - total - 1);
        if (n <= 0) break;
        total += (size_t)n;
        buf[total] = '\0';
        int result = httptest_parse_request(buf, total, &parsed);
        if (result == 0) {
            parsed_ok = 1;
            break;
        }
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
    if (!parsed_ok) {
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
    req.content_type = parsed.content_type[0] ? parsed.content_type : NULL;
    req.body = parsed.body;
    req.body_len = parsed.body_len;
    req.raw_headers = parsed.nheaders > 0 ? parsed.raw_headers : NULL;
    req.nheaders = parsed.nheaders;

    /* Create a memory writer for the handler to write to */
    neverc_http_response_writer_t *w = neverc_http_memory_writer_new();
    if (!w) return;
    w->head_request = strcmp(parsed.method, "HEAD") == 0;

    /* Call the actual handler */
    handler(&req, w);

    /* Extract response from memory writer */
    char *resp_body = NULL;
    size_t resp_body_len = 0;
    (void)neverc_http_memory_writer_result(w, &resp_body, &resp_body_len);
    (void)httptest_emit_response(conn, w, resp_body, resp_body_len);

    free(resp_body);
    neverc_http_memory_writer_free(w);
    free(parsed.owned_body);
}

#ifdef _WIN32
static DWORD WINAPI server_thread_func(LPVOID arg) {
#else
static void *server_thread_func(void *arg) {
#endif
    neverc_httptest_server_t *ts = (neverc_httptest_server_t *)arg;

    while (atomic_load_explicit(&ts->running, memory_order_seq_cst)) {
        const char *err = NULL;
        neverc_tcp_conn_t *conn = neverc_tcp_accept(ts->listener, &err);
        if (!conn) {
            if (!atomic_load_explicit(&ts->running, memory_order_seq_cst))
                break;
            continue;
        }
        /* Close() sets running=0 then dials a wakeup connection. Do not run
         * the user handler or wait out the request timeout on that socket. */
        if (!atomic_load_explicit(&ts->running, memory_order_seq_cst)) {
            neverc_tcp_close(conn);
            break;
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
    if (neverc_tcp_listener_addr(ln, &addr) != 0 || addr.port == 0 ||
        addr.addr[0] == '\0') {
        neverc_tcp_listener_close(ln);
        return NULL;
    }

    neverc_httptest_server_t *ts =
        (neverc_httptest_server_t *)calloc(1, sizeof(*ts));
    if (!ts) {
        neverc_tcp_listener_close(ln);
        return NULL;
    }
    ts->listener = ln;
    ts->handler = handler;
    ts->port = addr.port;
    atomic_init(&ts->running, 1);
    snprintf(ts->url, sizeof(ts->url), "http://%s", addr.addr);
    snprintf(ts->addr, sizeof(ts->addr), "%s", addr.addr);

#ifdef _WIN32
    ts->thread = CreateThread(NULL, 0, server_thread_func, ts, 0, NULL);
    if (!ts->thread) {
        neverc_tcp_listener_close(ln);
        free(ts);
        return NULL;
    }
#else
    if (pthread_create(&ts->thread, NULL, server_thread_func, ts) != 0) {
        neverc_tcp_listener_close(ln);
        free(ts);
        return NULL;
    }
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
    atomic_store_explicit(&ts->running, 0, memory_order_seq_cst);

    /* Wake the server thread's blocking accept() by making a dummy
       connection.  On Linux, close() on a listener fd does NOT reliably
       unblock another thread's accept() — this is undefined behavior in
       POSIX.  A dummy connect always works and avoids a use-after-free
       (the old code freed the listener before the thread could exit). */
    char wakeup[64];
    const char *err = NULL;
    neverc_tcp_conn_t *dummy = NULL;
    int wakeup_len = snprintf(wakeup, sizeof(wakeup), "127.0.0.1:%u",
                              (unsigned)ts->port);
    if (wakeup_len > 0 && (size_t)wakeup_len < sizeof(wakeup))
        dummy = neverc_tcp_dial(wakeup, &err);
    if (!dummy && ts->addr[0])
        dummy = neverc_tcp_dial(ts->addr, &err);
    if (dummy) neverc_tcp_close(dummy);

#ifdef _WIN32
    /* A bounded wait used to free the server while the thread still ran. */
    WaitForSingleObject(ts->thread, INFINITE);
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

typedef struct {
    neverc_httptest_recorder_t rec;
    neverc_http_response_writer_t *writer;
} httptest_recorder_box;

static httptest_recorder_box *httptest_recorder_box_from(
    neverc_httptest_recorder_t *rec) {
    return (httptest_recorder_box *)rec;
}

static void httptest_recorder_clear_headers(neverc_httptest_recorder_t *rec) {
    for (int i = 0; i < rec->nheaders; i++) {
        free(rec->header_names[i]);
        free(rec->header_values[i]);
        rec->header_names[i] = NULL;
        rec->header_values[i] = NULL;
    }
    rec->nheaders = 0;
}

static int httptest_recorder_add_header(neverc_httptest_recorder_t *rec,
                                        const char *name, const char *value) {
    if (rec->nheaders >= 64) return -1;
    char *header_name = strdup(name);
    char *header_value = strdup(value);
    if (!header_name || !header_value) {
        free(header_name);
        free(header_value);
        return -1;
    }
    rec->header_names[rec->nheaders] = header_name;
    rec->header_values[rec->nheaders] = header_value;
    rec->nheaders++;
    return 0;
}

static int httptest_recorder_has_header(const neverc_httptest_recorder_t *rec,
                                        const char *name) {
    for (int i = 0; i < rec->nheaders; i++)
        if (strcasecmp(rec->header_names[i], name) == 0)
            return 1;
    return 0;
}

/* Content-Length is writer-managed and never stored in header_names. Mirror
 * the HTTP/1 emit rule so recorder lookups see the length a client would. */
static int httptest_recorder_capture_content_length(
    neverc_httptest_recorder_t *rec, const neverc_http_response_writer_t *w) {
    if (w->chunked ||
        httptest_recorder_has_header(rec, "Content-Length"))
        return 0;
    if (w->status < 200 || w->status == 204 ||
        (!w->has_content_length_override && w->status == 304))
        return 0;
    size_t content_length = w->has_content_length_override
        ? w->content_length_override : w->body.len;
    char value[32];
    int length = snprintf(value, sizeof(value), "%zu", content_length);
    if (length < 0 || (size_t)length >= sizeof(value))
        return -1;
    return httptest_recorder_add_header(rec, "Content-Length", value);
}

static int httptest_recorder_capture(neverc_httptest_recorder_t *rec) {
    httptest_recorder_box *box = httptest_recorder_box_from(rec);
    neverc_http_response_writer_t *w = box->writer;
    if (!w) return 0;
    rec->status_code = w->status;
    free(rec->body);
    rec->body = NULL;
    rec->body_len = 0;
    if (w->body.len > 0) {
        rec->body = (char *)malloc(w->body.len + 1U);
        if (!rec->body) return -1;
        memcpy(rec->body, w->body.data, w->body.len);
        rec->body[w->body.len] = '\0';
        rec->body_len = w->body.len;
    }
    httptest_recorder_clear_headers(rec);
    for (int i = 0; i < w->nheaders && rec->nheaders < 64; i++) {
        if (httptest_recorder_add_header(
                rec, w->header_names[i], w->header_values[i]) != 0)
            return -1;
    }
    return httptest_recorder_capture_content_length(rec, w);
}

neverc_httptest_recorder_t *neverc_httptest_new_recorder(void) {
    httptest_recorder_box *box =
        (httptest_recorder_box *)calloc(1, sizeof(*box));
    if (!box) return NULL;
    box->rec.status_code = 200;
    return &box->rec;
}

neverc_http_response_writer_t *neverc_httptest_recorder_writer(
    neverc_httptest_recorder_t *rec) {
    if (!rec) return NULL;
    httptest_recorder_box *box = httptest_recorder_box_from(rec);
    if (!box->writer)
        box->writer = neverc_http_memory_writer_new();
    return box->writer;
}

void neverc_httptest_recorder_flush(neverc_httptest_recorder_t *rec) {
    if (rec) httptest_recorder_capture(rec);
}

const char *neverc_httptest_recorder_header(
    neverc_httptest_recorder_t *rec, const char *name) {
    if (!rec || !name) return NULL;
    neverc_httptest_recorder_flush(rec);
    for (int i = 0; i < rec->nheaders; i++) {
        if (strcasecmp(rec->header_names[i], name) == 0)
            return rec->header_values[i];
    }
    return NULL;
}

void neverc_httptest_recorder_free(neverc_httptest_recorder_t *rec) {
    if (!rec) return;
    httptest_recorder_box *box = httptest_recorder_box_from(rec);
    neverc_http_memory_writer_free(box->writer);
    free(rec->body);
    httptest_recorder_clear_headers(rec);
    free(box);
}
