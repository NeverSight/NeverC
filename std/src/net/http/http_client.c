/*
 * NeverC HTTP Client + Extended Features
 * Split from http.c to avoid large-TU compiler issue.
 */
#include "_http_internal.h"
#include "neverc/std/crypto/tls.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define sock_write_all nc_http_sock_write_all
#define strndup_safe nc_strndup_safe

neverc_http_cors_config_t g_cors_config;
int g_cors_enabled = 0;

/* ======================================================================
 * HTTP Client — Connection Pool
 * ====================================================================== */

#define POOL_MAX_HOSTS       64
#define POOL_MAX_IDLE_DEFAULT 2
#define POOL_IDLE_TIMEOUT_MS 90000
#define CLIENT_HEADER_LIMIT_DEFAULT (1024U * 1024U)
#define CLIENT_BODY_LIMIT_DEFAULT   (16U * 1024U * 1024U)

typedef struct client_connection {
    neverc_tcp_conn_t *tcp;
    neverc_tls_conn_t *tls;
} client_connection_t;

typedef struct pool_conn {
    client_connection_t  *connection;
    uint64_t              last_used;
    struct pool_conn     *next;
} pool_conn_t;

typedef struct {
    char        host[280]; /* "host:port" key */
    pool_conn_t *idle;
    int          idle_count;
} pool_host_t;

typedef struct {
    pool_host_t hosts[POOL_MAX_HOSTS];
    int          nhosts;
    int          max_idle_per_host;
    nc_mutex_t   lock;
    volatile int initialized;
} http_conn_pool_t;

struct neverc_http_client {
    neverc_http_client_config_t config;
    http_conn_pool_t            pool;
};

static neverc_http_client_t g_default_client = {
    .config = {
        .max_redirects = 10,
        .timeout_ms = 30000,
        .max_idle_per_host = POOL_MAX_IDLE_DEFAULT,
        .max_response_header_size = CLIENT_HEADER_LIMIT_DEFAULT,
        .max_response_body_size = CLIENT_BODY_LIMIT_DEFAULT,
    },
    .pool = { .max_idle_per_host = POOL_MAX_IDLE_DEFAULT },
};

static void pool_init(http_conn_pool_t *pool) {
    if (nc_atomic_load(&pool->initialized)) return;
#ifdef _WIN32
    static volatile LONG pool_lock = 0;
    while (InterlockedCompareExchange(&pool_lock, 1, 0) != 0) { Sleep(0); }
#else
    static volatile int pool_lock = 0;
    while (!__sync_bool_compare_and_swap(&pool_lock, 0, 1)) { /* spin */ }
#endif
    if (!nc_atomic_load(&pool->initialized)) {
        nc_mutex_init(&pool->lock);
        nc_atomic_store(&pool->initialized, 1);
    }
#ifdef _WIN32
    InterlockedExchange(&pool_lock, 0);
#else
    __sync_lock_release(&pool_lock);
#endif
}

static pool_host_t *pool_find_host(http_conn_pool_t *pool, const char *key) {
    for (int i = 0; i < pool->nhosts; i++) {
        if (strcmp(pool->hosts[i].host, key) == 0)
            return &pool->hosts[i];
    }
    return NULL;
}

static void client_connection_close(client_connection_t *connection) {
    if (!connection) return;
    if (connection->tls) neverc_tls_close(connection->tls);
    if (connection->tcp) neverc_tcp_close(connection->tcp);
    free(connection);
}

static client_connection_t *pool_get(neverc_http_client_t *client,
                                     const char *host_key) {
    http_conn_pool_t *pool = &client->pool;
    if (nc_atomic_load(&pool->max_idle_per_host) <= 0) return NULL;
    pool_init(pool);

    nc_mutex_lock(&pool->lock);
    pool_host_t *h = pool_find_host(pool, host_key);
    client_connection_t *result = NULL;

    if (h && h->idle) {
        uint64_t now = nc_monotonic_ms();
        /* Pop the most recently used connection (LIFO) */
        while (h->idle) {
            pool_conn_t *pc = h->idle;
            h->idle = pc->next;
            h->idle_count--;

            if (now - pc->last_used < POOL_IDLE_TIMEOUT_MS) {
                result = pc->connection;
                free(pc);
                break;
            }
            /* Expired connection */
            client_connection_close(pc->connection);
            free(pc);
        }
    }
    nc_mutex_unlock(&pool->lock);
    return result;
}

static void pool_put(neverc_http_client_t *client, const char *host_key,
                     client_connection_t *connection) {
    http_conn_pool_t *pool = &client->pool;
    if (nc_atomic_load(&pool->max_idle_per_host) <= 0 || !connection) {
        client_connection_close(connection);
        return;
    }
    pool_init(pool);

    nc_mutex_lock(&pool->lock);
    pool_host_t *h = pool_find_host(pool, host_key);
    if (!h) {
        if (pool->nhosts < POOL_MAX_HOSTS) {
            h = &pool->hosts[pool->nhosts++];
            memset(h, 0, sizeof(*h));
            snprintf(h->host, sizeof(h->host), "%s", host_key);
        }
    }

    if (h && h->idle_count < nc_atomic_load(&pool->max_idle_per_host)) {
        pool_conn_t *pc = (pool_conn_t *)calloc(1, sizeof(*pc));
        if (pc) {
            pc->connection = connection;
            pc->last_used = nc_monotonic_ms();
            pc->next = h->idle;
            h->idle = pc;
            h->idle_count++;
            nc_mutex_unlock(&pool->lock);
            return;
        }
    }
    nc_mutex_unlock(&pool->lock);
    client_connection_close(connection);
}

static void pool_close_idle(http_conn_pool_t *pool) {
    if (!nc_atomic_load(&pool->initialized)) return;
    nc_mutex_lock(&pool->lock);
    for (int i = 0; i < pool->nhosts; i++) {
        pool_conn_t *connection = pool->hosts[i].idle;
        while (connection) {
            pool_conn_t *next = connection->next;
            client_connection_close(connection->connection);
            free(connection);
            connection = next;
        }
        pool->hosts[i].idle = NULL;
        pool->hosts[i].idle_count = 0;
    }
    pool->nhosts = 0;
    nc_mutex_unlock(&pool->lock);
}

neverc_http_client_config_t neverc_http_client_config_default(void) {
    neverc_http_client_config_t config;
    config.max_redirects = 10;
    config.timeout_ms = 30000;
    config.max_idle_per_host = POOL_MAX_IDLE_DEFAULT;
    config.max_response_header_size = CLIENT_HEADER_LIMIT_DEFAULT;
    config.max_response_body_size = CLIENT_BODY_LIMIT_DEFAULT;
    return config;
}

static int client_config_valid(const neverc_http_client_config_t *config) {
    return config && config->max_redirects >= 0 && config->timeout_ms > 0 &&
           config->max_idle_per_host >= 0 &&
           config->max_response_header_size > 0 &&
           config->max_response_body_size > 0 &&
           config->max_response_header_size <= SIZE_MAX - 4 &&
           config->max_response_body_size <=
               SIZE_MAX - config->max_response_header_size - 4;
}

neverc_http_client_t *neverc_http_client_new(
    const neverc_http_client_config_t *config) {
    neverc_http_client_config_t effective = config
        ? *config : neverc_http_client_config_default();
    if (!client_config_valid(&effective)) return NULL;
    neverc_http_client_t *client =
        (neverc_http_client_t *)calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->config = effective;
    client->pool.max_idle_per_host = effective.max_idle_per_host;
    pool_init(&client->pool);
    return client;
}

void neverc_http_client_free(neverc_http_client_t *client) {
    if (!client || client == &g_default_client) return;
    pool_close_idle(&client->pool);
    if (nc_atomic_load(&client->pool.initialized))
        nc_mutex_destroy(&client->pool.lock);
    free(client);
}

/* ======================================================================
 * HTTP Client
 * ====================================================================== */

typedef struct {
    char host[256];
    char authority[280];
    uint16_t port;
    char path[2048];
    int is_https;
} parsed_url_t;

static int parse_url_port(const char *start, const char *end,
                          uint16_t *port) {
    if (!start || start == end) return -1;
    unsigned value = 0;
    for (const char *p = start; p < end; p++) {
        if (*p < '0' || *p > '9' || value > (65535U - (*p - '0')) / 10)
            return -1;
        value = value * 10 + (unsigned)(*p - '0');
    }
    if (value == 0) return -1;
    *port = (uint16_t)value;
    return 0;
}

static int parse_http_url(const char *url, parsed_url_t *out) {
    if (!url || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    out->port = 80;

    const char *p;
    if (strncmp(url, "http://", 7) == 0) {
        p = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        p = url + 8;
        out->port = 443;
        out->is_https = 1;
    } else {
        return -1;
    }

    const char *fragment = strchr(p, '#');
    if (fragment) return -1;
    const char *slash = strchr(p, '/');
    const char *question = strchr(p, '?');
    const char *authority_end = p + strlen(p);
    if (slash && slash < authority_end) authority_end = slash;
    if (question && question < authority_end) authority_end = question;
    size_t authority_length = (size_t)(authority_end - p);
    if (authority_length == 0 ||
        authority_length >= sizeof(out->authority) ||
        memchr(p, '@', authority_length))
        return -1;
    for (size_t i = 0; i < authority_length; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c <= 0x20 || c == 0x7f || c == '\\') return -1;
    }
    memcpy(out->authority, p, authority_length);
    out->authority[authority_length] = '\0';

    if (*p == '[') {
        const char *bracket = (const char *)memchr(
            p, ']', authority_length);
        if (!bracket || bracket == p + 1) return -1;
        size_t host_length = (size_t)(bracket - p - 1);
        if (host_length >= sizeof(out->host)) return -1;
        memcpy(out->host, p + 1, host_length);
        out->host[host_length] = '\0';
        if (bracket + 1 < authority_end) {
            if (bracket[1] != ':' ||
                parse_url_port(bracket + 2, authority_end, &out->port) != 0)
                return -1;
        }
    } else {
        const char *colon = (const char *)memchr(p, ':', authority_length);
        const char *host_end = colon ? colon : authority_end;
        if (colon && memchr(colon + 1, ':',
                            (size_t)(authority_end - colon - 1)))
            return -1;
        size_t host_length = (size_t)(host_end - p);
        if (host_length == 0 || host_length >= sizeof(out->host)) return -1;
        memcpy(out->host, p, host_length);
        out->host[host_length] = '\0';
        if (colon && parse_url_port(colon + 1, authority_end,
                                    &out->port) != 0)
            return -1;
    }

    const char *target = authority_end;
    size_t target_length = strlen(target);
    if (*target == '?') {
        if (target_length + 1 >= sizeof(out->path)) return -1;
        out->path[0] = '/';
        memcpy(out->path + 1, target, target_length + 1);
    } else if (*target == '/') {
        if (target_length >= sizeof(out->path)) return -1;
        memcpy(out->path, target, target_length + 1);
    } else if (*target == '\0') {
        strcpy(out->path, "/");
    } else {
        return -1;
    }
    for (size_t i = 0; out->path[i]; i++) {
        unsigned char c = (unsigned char)out->path[i];
        if (c <= 0x20 || c == 0x7f) return -1;
    }

    return 0;
}

static neverc_http_response_t *make_error_response(const char *msg) {
    neverc_http_response_t *r =
        (neverc_http_response_t *)calloc(1, sizeof(*r));
    if (r) r->error = msg;
    return r;
}

static const char *client_context_error(neverc_context_t *context,
                                        const char *fallback) {
    const char *error = neverc_context_err(context);
    return error ? error : fallback;
}

typedef struct {
    int    status_code;
    int    keep_alive;
    int    has_content_length;
    size_t content_length;
    int    is_chunked;
} response_framing_t;

static const char *client_find_crlf(const char *start, const char *end) {
    for (const char *p = start; p + 1 < end; p++)
        if (p[0] == '\r' && p[1] == '\n') return p;
    return NULL;
}

static int client_is_tchar(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '!' || c == '#' || c == '$' ||
           c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
           c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
           c == '|' || c == '~';
}

static int client_valid_token(const char *value, size_t length) {
    if (!value || length == 0) return 0;
    for (size_t i = 0; i < length; i++)
        if (!client_is_tchar((unsigned char)value[i])) return 0;
    return 1;
}

static int client_valid_field_value(const char *value, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) return 0;
    }
    return 1;
}

static void client_trim_ows(const char **value, size_t *length) {
    while (*length > 0 && (**value == ' ' || **value == '\t')) {
        (*value)++;
        (*length)--;
    }
    while (*length > 0 && ((*value)[*length - 1] == ' ' ||
                            (*value)[*length - 1] == '\t'))
        (*length)--;
}

static int client_name_is(const char *name, size_t length,
                          const char *expected) {
    size_t expected_length = strlen(expected);
    return length == expected_length &&
           strncasecmp(name, expected, length) == 0;
}

static int client_value_has_token(const char *value, size_t length,
                                  const char *expected) {
    size_t expected_length = strlen(expected);
    size_t offset = 0;
    while (offset < length) {
        while (offset < length && (value[offset] == ' ' ||
               value[offset] == '\t' || value[offset] == ',')) offset++;
        size_t start = offset;
        while (offset < length && value[offset] != ',') offset++;
        size_t token_length = offset - start;
        while (token_length > 0 &&
               (value[start + token_length - 1] == ' ' ||
                value[start + token_length - 1] == '\t')) token_length--;
        if (token_length == expected_length &&
            strncasecmp(value + start, expected, token_length) == 0)
            return 1;
    }
    return 0;
}

static int client_parse_size(const char *value, size_t length,
                             size_t *result) {
    if (length == 0) return -1;
    size_t parsed = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < '0' || c > '9' ||
            parsed > (SIZE_MAX - (size_t)(c - '0')) / 10)
            return -1;
        parsed = parsed * 10 + (size_t)(c - '0');
    }
    *result = parsed;
    return 0;
}

static int parse_response_framing(const char *headers, size_t header_length,
                                  response_framing_t *framing) {
    memset(framing, 0, sizeof(*framing));
    const char *header_end = headers + header_length;
    const char *line_end = client_find_crlf(headers, header_end + 2);
    if (!line_end || (size_t)(line_end - headers) < 12) return -1;
    int is_http_11 = memcmp(headers, "HTTP/1.1 ", 9) == 0;
    int is_http_10 = memcmp(headers, "HTTP/1.0 ", 9) == 0;
    if ((!is_http_11 && !is_http_10) || headers[9] < '0' ||
        headers[9] > '9' || headers[10] < '0' || headers[10] > '9' ||
        headers[11] < '0' || headers[11] > '9' ||
        (line_end > headers + 12 && headers[12] != ' '))
        return -1;
    framing->status_code = (headers[9] - '0') * 100 +
                           (headers[10] - '0') * 10 + headers[11] - '0';
    framing->keep_alive = is_http_11;

    const char *cursor = line_end + 2;
    while (cursor < header_end) {
        line_end = client_find_crlf(cursor, header_end + 2);
        if (!line_end || line_end == cursor ||
            *cursor == ' ' || *cursor == '\t') return -1;
        const char *colon = (const char *)memchr(
            cursor, ':', (size_t)(line_end - cursor));
        if (!colon || !client_valid_token(
                cursor, (size_t)(colon - cursor))) return -1;
        size_t name_length = (size_t)(colon - cursor);
        const char *value = colon + 1;
        size_t value_length = (size_t)(line_end - value);
        client_trim_ows(&value, &value_length);
        if (!client_valid_field_value(value, value_length)) return -1;

        if (client_name_is(cursor, name_length, "Content-Length")) {
            if (framing->has_content_length ||
                client_parse_size(value, value_length,
                                  &framing->content_length) != 0)
                return -1;
            framing->has_content_length = 1;
        } else if (client_name_is(cursor, name_length,
                                  "Transfer-Encoding")) {
            if (framing->is_chunked || value_length != 7 ||
                strncasecmp(value, "chunked", 7) != 0)
                return -1;
            framing->is_chunked = 1;
        } else if (client_name_is(cursor, name_length, "Connection")) {
            if (client_value_has_token(value, value_length, "close"))
                framing->keep_alive = 0;
            else if (is_http_10 && client_value_has_token(
                         value, value_length, "keep-alive"))
                framing->keep_alive = 1;
        }
        cursor = line_end + 2;
    }
    if (framing->has_content_length && framing->is_chunked) return -1;
    return 0;
}

static int contains_crlf(const char *s) {
    return s && (strchr(s, '\r') != NULL || strchr(s, '\n') != NULL);
}

static int append_cstr(nc_buf_t *buf, const char *s) {
    return nc_buf_append(buf, s, strlen(s));
}

static int build_http_request(nc_buf_t *req, const char *method,
                              const parsed_url_t *url,
                              const char *content_type,
                              const void *body, size_t body_len,
                              int keep_alive) {
    if (!req)
        return -1;
    nc_buf_init(req);
    if (!method || !url || (body_len > 0 && !body) ||
        !client_valid_token(method, strlen(method)) ||
        contains_crlf(url->authority) ||
        contains_crlf(url->path) || contains_crlf(content_type))
        return -1;

    if (append_cstr(req, method) != 0 ||
        append_cstr(req, " ") != 0 ||
        append_cstr(req, url->path) != 0 ||
        append_cstr(req, " HTTP/1.1\r\nHost: ") != 0 ||
        append_cstr(req, url->authority) != 0 ||
        append_cstr(req, "\r\n") != 0)
        goto fail;

    if (content_type &&
        (append_cstr(req, "Content-Type: ") != 0 ||
         append_cstr(req, content_type) != 0 ||
         append_cstr(req, "\r\n") != 0))
        goto fail;

    if (body_len > 0) {
        char line[64];
        int n = snprintf(line, sizeof(line), "Content-Length: %zu\r\n",
                         body_len);
        if (n < 0 || (size_t)n >= sizeof(line) ||
            nc_buf_append(req, line, (size_t)n) != 0)
            goto fail;
    }

    if (append_cstr(req, keep_alive
                            ? "Connection: keep-alive\r\n\r\n"
                            : "Connection: close\r\n\r\n") != 0 ||
        (body_len > 0 && nc_buf_append(req, body, body_len) != 0))
        goto fail;

    return 0;

fail:
    nc_buf_free(req);
    return -1;
}

/* 1 = complete, 0 = incomplete, -1 = malformed or decoded body too large. */
static int scan_chunked_body(const char *source, size_t source_length,
                             size_t body_limit, size_t *wire_consumed,
                             size_t *decoded_length,
                             size_t *trailer_offset,
                             size_t *trailer_length) {
    const char *end = source + source_length;
    const char *cursor = source;
    size_t decoded = 0;
    for (;;) {
        const char *line_end = client_find_crlf(cursor, end);
        if (!line_end) return 0;
        size_t chunk_size = 0;
        size_t digits = 0;
        while (cursor + digits < line_end) {
            unsigned char c = (unsigned char)cursor[digits];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
            else break;
            if (chunk_size > (SIZE_MAX - digit) / 16) return -1;
            chunk_size = chunk_size * 16 + digit;
            digits++;
        }
        if (digits == 0 || (cursor + digits < line_end &&
                            cursor[digits] != ';') ||
            !client_valid_field_value(cursor + digits,
                                      (size_t)(line_end - cursor - digits)))
            return -1;
        cursor = line_end + 2;

        if (chunk_size == 0) {
            const char *trailers = cursor;
            for (;;) {
                line_end = client_find_crlf(cursor, end);
                if (!line_end) return 0;
                if (line_end == cursor) {
                    *wire_consumed = (size_t)(line_end + 2 - source);
                    *decoded_length = decoded;
                    *trailer_offset = (size_t)(trailers - source);
                    *trailer_length = (size_t)(cursor - trailers);
                    return 1;
                }
                if (*cursor == ' ' || *cursor == '\t') return -1;
                const char *colon = (const char *)memchr(
                    cursor, ':', (size_t)(line_end - cursor));
                if (!colon || !client_valid_token(
                        cursor, (size_t)(colon - cursor))) return -1;
                const char *value = colon + 1;
                size_t value_length = (size_t)(line_end - value);
                client_trim_ows(&value, &value_length);
                size_t name_length = (size_t)(colon - cursor);
                if (!client_valid_field_value(value, value_length) ||
                    client_name_is(cursor, name_length, "Content-Length") ||
                    client_name_is(cursor, name_length, "Transfer-Encoding") ||
                    client_name_is(cursor, name_length, "Host"))
                    return -1;
                cursor = line_end + 2;
            }
        }

        if (decoded > body_limit || chunk_size > body_limit - decoded)
            return -1;
        size_t available = (size_t)(end - cursor);
        if (chunk_size > available || available - chunk_size < 2) return 0;
        if (cursor[chunk_size] != '\r' || cursor[chunk_size + 1] != '\n')
            return -1;
        decoded += chunk_size;
        cursor += chunk_size + 2;
    }
}

static int decode_chunked_body(const char *source, size_t source_length,
                               size_t decoded_length, char **output) {
    char *decoded = (char *)malloc(decoded_length + 1);
    if (!decoded) return -1;
    const char *end = source + source_length;
    const char *cursor = source;
    size_t output_offset = 0;
    while (cursor < end) {
        const char *line_end = client_find_crlf(cursor, end);
        if (!line_end) goto invalid;
        size_t chunk_size = 0;
        for (const char *p = cursor; p < line_end && *p != ';'; p++) {
            unsigned char c = (unsigned char)*p;
            unsigned digit = c <= '9' ? (unsigned)(c - '0')
                : c <= 'F' ? (unsigned)(c - 'A' + 10)
                : (unsigned)(c - 'a' + 10);
            chunk_size = chunk_size * 16 + digit;
        }
        cursor = line_end + 2;
        if (chunk_size == 0) break;
        memcpy(decoded + output_offset, cursor, chunk_size);
        output_offset += chunk_size;
        cursor += chunk_size + 2;
    }
    if (output_offset != decoded_length) goto invalid;
    decoded[decoded_length] = '\0';
    *output = decoded;
    return 0;
invalid:
    free(decoded);
    return -1;
}

/* Return -1 if no bytes were written and -2 after a partial write. */
static int client_connection_write_all(client_connection_t *connection,
                                       neverc_context_t *context,
                                       const char *data, size_t length) {
    if (connection->tls) {
        if (neverc_context_done(context)) return -1;
        int written = neverc_tls_write_context(connection->tls, context,
                                               data, length);
        return written >= 0 && (size_t)written == length ? 0 : -1;
    }
    size_t written = 0;
    while (written < length) {
        neverc_net_result_t result = neverc_tcp_write_context(
            connection->tcp, context, data + written, length - written);
        written += result.transferred;
        if (result.status != NEVERC_NET_OK || result.transferred == 0)
            return written == 0 ? -1 : -2;
    }
    return 0;
}

static int client_connection_read(client_connection_t *connection,
                                  neverc_context_t *context,
                                  void *data, size_t length) {
    if (!connection->tls) {
        neverc_net_result_t result = neverc_tcp_read_context(
            connection->tcp, context, data, length);
        return result.status == NEVERC_NET_OK
            ? (int)result.transferred
            : result.status == NEVERC_NET_EOF ? 0 : -1;
    }
    if (neverc_context_done(context)) return -1;
    return neverc_tls_read_context(connection->tls, context, data, length);
}

static client_connection_t *client_connection_dial(
    const parsed_url_t *url, const char *connect_addr,
    neverc_context_t *context, const char **error) {
    neverc_tcp_conn_t *tcp = NULL;
    neverc_net_result_t dial_result = neverc_tcp_dial_context(
        connect_addr, context, &tcp);
    if (dial_result.status != NEVERC_NET_OK || !tcp) {
        if (error) *error = client_context_error(context, "connection failed");
        return NULL;
    }

    client_connection_t *connection =
        (client_connection_t *)calloc(1, sizeof(*connection));
    if (!connection) {
        neverc_tcp_close(tcp);
        if (error) *error = "out of memory";
        return NULL;
    }
    connection->tcp = tcp;
    if (!url->is_https) return connection;

    int64_t deadline = neverc_context_deadline(context);
    if (deadline > 0 &&
        (neverc_tcp_set_read_deadline(tcp, deadline) != 0 ||
         neverc_tcp_set_write_deadline(tcp, deadline) != 0)) {
        client_connection_close(connection);
        if (error) *error = "failed to configure TLS deadline";
        return NULL;
    }

    neverc_tls_config_t *tls_config = neverc_tls_config_new();
    if (!tls_config) {
        client_connection_close(connection);
        if (error) *error = "out of memory";
        return NULL;
    }
    neverc_tls_config_set_server_name(tls_config, url->host);
    const char *alpn[] = { "http/1.1" };
    neverc_tls_config_set_alpn(tls_config, alpn, 1);
    const char *tls_error = NULL;
    connection->tls = neverc_tls_client(tcp, tls_config, &tls_error);
    neverc_tls_config_free(tls_config);
    if (!connection->tls) {
        client_connection_close(connection);
        if (error) *error = tls_error ? tls_error : "TLS handshake failed";
        return NULL;
    }
    const char *negotiated = neverc_tls_alpn(connection->tls);
    if (negotiated && strcmp(negotiated, "http/1.1") != 0) {
        client_connection_close(connection);
        if (error) *error = "server selected an unsupported ALPN protocol";
        return NULL;
    }
    return connection;
}

static neverc_http_response_t *do_request(neverc_http_client_t *client,
                                            neverc_context_t *context,
                                            const char *method,
                                            const parsed_url_t *url,
                                            const char *content_type,
                                            const void *body,
                                            size_t body_len) {
    if (body_len > INT_MAX)
        return make_error_response("request body is too large");

    if (nc_net_init() != 0)
        return make_error_response("network initialization failed");

    char connect_addr[280];
    if (strchr(url->host, ':'))
        snprintf(connect_addr, sizeof(connect_addr), "[%s]:%u", url->host,
                 (unsigned)url->port);
    else
        snprintf(connect_addr, sizeof(connect_addr), "%s:%u", url->host,
                 (unsigned)url->port);
    char pool_key[290];
    snprintf(pool_key, sizeof(pool_key), "%s://%s",
             url->is_https ? "https" : "http", connect_addr);

    /* Try to reuse a pooled connection first */
    int from_pool = 0;
    client_connection_t *conn = pool_get(client, pool_key);
    if (conn) {
        from_pool = 1;
    } else {
        const char *dial_error = NULL;
        conn = client_connection_dial(url, connect_addr, context, &dial_error);
        if (!conn) return make_error_response(dial_error);
    }

    int use_keepalive =
        nc_atomic_load(&client->pool.max_idle_per_host) > 0;
    nc_buf_t req;
    if (build_http_request(&req, method, url, content_type, body, body_len,
                           use_keepalive) != 0 ||
        req.len > INT_MAX) {
        client_connection_close(conn);
        nc_buf_free(&req);
        return make_error_response("invalid request or out of memory");
    }

    int wr = client_connection_write_all(conn, context, req.data, req.len);
    nc_buf_free(&req);

    /* If write failed on a pooled connection (stale), retry with new conn */
    if (wr == -1 && from_pool && !url->is_https) {
        client_connection_close(conn);
        const char *dial_error = NULL;
        conn = client_connection_dial(url, connect_addr, context, &dial_error);
        if (!conn) return make_error_response(dial_error);

        if (build_http_request(&req, method, url, content_type, body,
                               body_len, use_keepalive) != 0 ||
            req.len > INT_MAX) {
            client_connection_close(conn);
            nc_buf_free(&req);
            return make_error_response("invalid request or out of memory");
        }
        wr = client_connection_write_all(conn, context, req.data, req.len);
        nc_buf_free(&req);
    }
    if (wr != 0) {
        client_connection_close(conn);
        return make_error_response(client_context_error(
            context, "request write failed"));
    }

    nc_buf_t resp_buf;
    nc_buf_init(&resp_buf);

    size_t hdr_end_offset = SIZE_MAX;
    int is_head = (strcmp(method, "HEAD") == 0);
    int response_complete = 0;
    int interim_count = 0;
    int has_extra_bytes = 0;
    response_framing_t framing;
    memset(&framing, 0, sizeof(framing));
    size_t chunk_wire_consumed = 0;
    size_t chunk_decoded_length = 0;
    size_t trailer_offset = 0;
    size_t trailer_length = 0;

    const size_t header_limit = client->config.max_response_header_size;
    const size_t body_limit = client->config.max_response_body_size;
    size_t response_limit = header_limit + 4 + body_limit;
    if (response_limit <= SIZE_MAX - header_limit)
        response_limit += header_limit;
    if (response_limit <= SIZE_MAX - 65536)
        response_limit += 65536;
    char chunk[8192];
    while (!response_complete) {
        size_t read_size = sizeof(chunk);
        if (resp_buf.len >= response_limit) {
            int extra = client_connection_read(conn, context, chunk, 1);
            if (extra == 0 && hdr_end_offset != SIZE_MAX &&
                !framing.is_chunked && !framing.has_content_length) {
                response_complete = 1;
                break;
            }
            client_connection_close(conn);
            nc_buf_free(&resp_buf);
            return make_error_response(hdr_end_offset == SIZE_MAX
                ? "response headers exceed configured limit"
                : "response body exceeds configured limit");
        }
        if (read_size > response_limit - resp_buf.len)
            read_size = response_limit - resp_buf.len;
        int rn = client_connection_read(conn, context, chunk, read_size);
        if (rn <= 0) {
            if (rn == 0 && hdr_end_offset != SIZE_MAX && !framing.is_chunked &&
                (!framing.has_content_length ||
                 resp_buf.len - hdr_end_offset - 4 ==
                     framing.content_length)) {
                response_complete = 1;
                break;
            }
            client_connection_close(conn);
            nc_buf_free(&resp_buf);
            return make_error_response(client_context_error(
                context, "incomplete HTTP response"));
        }
        if (nc_buf_append(&resp_buf, chunk, (size_t)rn) != 0) {
            client_connection_close(conn);
            nc_buf_free(&resp_buf);
            return make_error_response("out of memory");
        }

analyze_response:
        if (hdr_end_offset == SIZE_MAX) {
            char *hdr_end_ptr = strstr(resp_buf.data, "\r\n\r\n");
            if (hdr_end_ptr) {
                hdr_end_offset = (size_t)(hdr_end_ptr - resp_buf.data);
                if (hdr_end_offset > header_limit) {
                    client_connection_close(conn);
                    nc_buf_free(&resp_buf);
                    return make_error_response(
                        "response headers exceed configured limit");
                }
                if (parse_response_framing(resp_buf.data, hdr_end_offset,
                                           &framing) != 0) {
                    client_connection_close(conn);
                    nc_buf_free(&resp_buf);
                    return make_error_response("invalid HTTP response framing");
                }
                if (((framing.status_code >= 100 &&
                      framing.status_code < 200) ||
                     framing.status_code == 204) &&
                    (framing.has_content_length || framing.is_chunked)) {
                    client_connection_close(conn);
                    nc_buf_free(&resp_buf);
                    return make_error_response(
                        "body framing is forbidden for this status");
                }
                if (framing.status_code >= 100 &&
                    framing.status_code < 200 && framing.status_code != 101) {
                    if (++interim_count > 16) {
                        client_connection_close(conn);
                        nc_buf_free(&resp_buf);
                        return make_error_response("too many interim responses");
                    }
                    nc_buf_consume(&resp_buf, hdr_end_offset + 4);
                    hdr_end_offset = SIZE_MAX;
                    memset(&framing, 0, sizeof(framing));
                    if (resp_buf.len > 0) goto analyze_response;
                    continue;
                }
                if (framing.has_content_length &&
                    framing.content_length > body_limit) {
                    client_connection_close(conn);
                    nc_buf_free(&resp_buf);
                    return make_error_response(
                        "response body exceeds configured limit");
                }
            } else if (resp_buf.len > header_limit + 4) {
                client_connection_close(conn);
                nc_buf_free(&resp_buf);
                return make_error_response(
                    "response headers exceed configured limit");
            }
        }

        if (hdr_end_offset == SIZE_MAX) continue;
        size_t body_received = resp_buf.len - hdr_end_offset - 4;
        int status_has_no_body = is_head ||
            (framing.status_code >= 100 && framing.status_code < 200) ||
            framing.status_code == 204 || framing.status_code == 304;
        if (status_has_no_body) {
            if (framing.status_code == 101) framing.keep_alive = 0;
            has_extra_bytes = body_received > 0;
            response_complete = 1;
        } else if (framing.is_chunked) {
            int scan_result = scan_chunked_body(
                resp_buf.data + hdr_end_offset + 4, body_received,
                body_limit, &chunk_wire_consumed, &chunk_decoded_length,
                &trailer_offset, &trailer_length);
            if (scan_result < 0) {
                client_connection_close(conn);
                nc_buf_free(&resp_buf);
                return make_error_response("invalid chunked HTTP response");
            }
            if (scan_result > 0) {
                has_extra_bytes = body_received > chunk_wire_consumed;
                response_complete = 1;
            }
        } else if (framing.has_content_length) {
            if (body_received >= framing.content_length) {
                has_extra_bytes = body_received > framing.content_length;
                response_complete = 1;
            }
        } else {
            framing.keep_alive = 0;
            if (body_received > body_limit) {
                client_connection_close(conn);
                nc_buf_free(&resp_buf);
                return make_error_response(
                    "response body exceeds configured limit");
            }
        }
    }
    if (resp_buf.len == 0) {
        client_connection_close(conn);
        nc_buf_free(&resp_buf);
        return make_error_response("empty response");
    }

    neverc_http_response_t *r =
        (neverc_http_response_t *)calloc(1, sizeof(*r));
    if (!r) {
        client_connection_close(conn);
        nc_buf_free(&resp_buf);
        return make_error_response("out of memory");
    }

    r->status_code = framing.status_code;

    int server_keepalive = framing.keep_alive && !has_extra_bytes;
    char *hdr_end = strstr(resp_buf.data, "\r\n\r\n");
    if (hdr_end) {
        size_t hdr_size = (size_t)(hdr_end - resp_buf.data);
        r->headers = (char *)malloc(hdr_size + 1);
        if (r->headers) {
            memcpy(r->headers, resp_buf.data, hdr_size);
            r->headers[hdr_size] = '\0';
        } else {
            r->error = "out of memory";
            server_keepalive = 0;
        }

        char *body_start = hdr_end + 4;
        size_t raw_blen = resp_buf.len - (size_t)(body_start - resp_buf.data);
        int status_has_no_body = is_head ||
            (framing.status_code >= 100 && framing.status_code < 200) ||
            framing.status_code == 204 || framing.status_code == 304;

        if (status_has_no_body) {
            raw_blen = 0;
        } else if (framing.is_chunked) {
            char *decoded = NULL;
            if (decode_chunked_body(body_start, chunk_wire_consumed,
                                    chunk_decoded_length, &decoded) != 0) {
                r->error = "invalid or incomplete chunked response";
                server_keepalive = 0;
            } else {
                r->body = decoded;
                r->body_len = chunk_decoded_length;
                if (trailer_length > 0) {
                    r->trailers = (char *)malloc(trailer_length + 1);
                    if (!r->trailers) {
                        r->error = "out of memory";
                        server_keepalive = 0;
                    } else {
                        memcpy(r->trailers, body_start + trailer_offset,
                               trailer_length);
                        r->trailers[trailer_length] = '\0';
                    }
                }
            }
        } else {
            if (framing.has_content_length)
                raw_blen = framing.content_length;
            if (raw_blen > 0) {
                r->body = (char *)malloc(raw_blen + 1);
                if (r->body) {
                    memcpy(r->body, body_start, raw_blen);
                    r->body[raw_blen] = '\0';
                    r->body_len = raw_blen;
                }
            }
        }
    }

    /* Return connection to pool if both sides agreed to keep-alive */
    if (use_keepalive && server_keepalive && r->status_code > 0)
        pool_put(client, pool_key, conn);
    else
        client_connection_close(conn);

    nc_buf_free(&resp_buf);
    return r;
}

void neverc_http_client_set_max_redirects(int n) {
    if (n >= 0) nc_atomic_store(&g_default_client.config.max_redirects, n);
}

void neverc_http_client_set_timeout(int ms) {
    if (ms > 0) nc_atomic_store(&g_default_client.config.timeout_ms, ms);
}

void neverc_http_client_set_pool(int max_idle_per_host) {
    int limit = max_idle_per_host >= 0
        ? max_idle_per_host : POOL_MAX_IDLE_DEFAULT;
    http_conn_pool_t *pool = &g_default_client.pool;
    pool_init(pool);
    nc_mutex_lock(&pool->lock);
    nc_atomic_store(&pool->max_idle_per_host, limit);
    nc_atomic_store(&g_default_client.config.max_idle_per_host, limit);
    for (int i = 0; i < pool->nhosts; i++) {
        while (pool->hosts[i].idle_count > limit) {
            pool_conn_t *connection = pool->hosts[i].idle;
            if (!connection) break;
            pool->hosts[i].idle = connection->next;
            pool->hosts[i].idle_count--;
            client_connection_close(connection->connection);
            free(connection);
        }
    }
    nc_mutex_unlock(&pool->lock);
}

static const char *parse_response_header_value(const char *headers,
                                                 size_t hdr_len,
                                                 const char *name) {
    size_t namelen = strlen(name);
    const char *p = headers;
    const char *end = headers + hdr_len;
    while (p < end) {
        const char *nl = NULL;
        for (const char *q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') { nl = q; break; }
        }
        if (!nl) break;
        if ((size_t)(nl - p) > namelen + 1 &&
            strncasecmp(p, name, namelen) == 0 && p[namelen] == ':') {
            const char *v = p + namelen + 1;
            while (v < nl && *v == ' ') v++;
            size_t vlen = (size_t)(nl - v);
            char *val = (char *)malloc(vlen + 1);
            if (val) {
                memcpy(val, v, vlen);
                val[vlen] = '\0';
            }
            return val;
        }
        p = nl + 2;
    }
    return NULL;
}

static neverc_http_response_t *do_request_with_redirects(
    neverc_http_client_t *client, neverc_context_t *context,
    const char *method, const char *url,
    const char *content_type, const void *body, size_t body_len) {
    if (!url) return make_error_response("null url");

    char current_url[4096];
    if (strlen(url) >= sizeof(current_url))
        return make_error_response("url is too long");
    memcpy(current_url, url, strlen(url) + 1);
    const char *current_method = method;

    int last_status = 0;
    int max_redirects = nc_atomic_load(&client->config.max_redirects);
    for (int redirects = 0; redirects <= max_redirects; redirects++) {
        parsed_url_t pu;
        if (parse_http_url(current_url, &pu) != 0)
            return make_error_response("invalid url");

        neverc_http_response_t *resp;
        /* 301/302/303: convert POST/PUT/PATCH to GET (RFC 7231).
         * 307/308: preserve original method (RFC 7538). */
        if (redirects > 0 && last_status != 307 && last_status != 308 &&
            (strcmp(current_method, "POST") == 0 ||
             strcmp(current_method, "PUT") == 0 ||
             strcmp(current_method, "PATCH") == 0)) {
            resp = do_request(client, context, "GET", &pu, NULL, NULL, 0);
            current_method = "GET";
        } else {
            resp = do_request(client, context, current_method, &pu,
                              content_type, body, body_len);
        }

        if (!resp || resp->error) return resp;

        if (resp->status_code >= 301 && resp->status_code <= 308 &&
            resp->status_code != 304 &&
            max_redirects > 0 && resp->headers) {
            const char *loc = parse_response_header_value(
                resp->headers, strlen(resp->headers), "Location");
            if (loc) {
                last_status = resp->status_code;
                if (loc[0] == '/') {
                    int n = snprintf(current_url, sizeof(current_url),
                                     "%s://%s%s",
                                     pu.is_https ? "https" : "http",
                                     pu.authority, loc);
                    if (n < 0 || (size_t)n >= sizeof(current_url)) {
                        free((void *)loc);
                        neverc_http_response_free(resp);
                        return make_error_response("redirect url is too long");
                    }
                } else {
                    if (strlen(loc) >= sizeof(current_url)) {
                        free((void *)loc);
                        neverc_http_response_free(resp);
                        return make_error_response("redirect url is too long");
                    }
                    memcpy(current_url, loc, strlen(loc) + 1);
                }
                free((void *)loc);
                neverc_http_response_free(resp);
                continue;
            }
        }
        return resp;
    }
    return make_error_response("too many redirects");
}

static neverc_http_response_t *execute_client_request(
    neverc_http_client_t *client, neverc_context_t *parent,
    const char *method, const char *url, const char *content_type,
    const void *body, size_t body_len) {
    neverc_context_cancel_handle_t *cancel_handle = NULL;
    neverc_context_t *context = neverc_context_with_timeout_handle(
        parent ? parent : neverc_context_background(),
        nc_atomic_load(&client->config.timeout_ms), &cancel_handle);
    if (!context) return make_error_response("out of memory");
    neverc_http_response_t *response = do_request_with_redirects(
        client, context, method, url, content_type, body, body_len);
    neverc_context_cancel_handle_cancel(cancel_handle);
    neverc_context_cancel_handle_free(cancel_handle);
    neverc_context_free(context);
    return response;
}

neverc_http_response_t *neverc_http_get(const char *url) {
    return execute_client_request(&g_default_client, NULL, "GET", url,
                                  NULL, NULL, 0);
}

neverc_http_response_t *neverc_http_post(const char *url,
                                          const char *content_type,
                                          const void *body,
                                          size_t body_len) {
    return execute_client_request(&g_default_client, NULL, "POST", url,
                                  content_type, body, body_len);
}

neverc_http_response_t *neverc_http_head(const char *url) {
    return execute_client_request(&g_default_client, NULL, "HEAD", url,
                                  NULL, NULL, 0);
}

neverc_http_response_t *neverc_http_put(const char *url,
                                         const char *content_type,
                                         const void *body,
                                         size_t body_len) {
    return execute_client_request(&g_default_client, NULL, "PUT", url,
                                  content_type, body, body_len);
}

neverc_http_response_t *neverc_http_delete(const char *url) {
    return execute_client_request(&g_default_client, NULL, "DELETE", url,
                                  NULL, NULL, 0);
}

neverc_http_response_t *neverc_http_patch(const char *url,
                                           const char *content_type,
                                           const void *body,
                                           size_t body_len) {
    return execute_client_request(&g_default_client, NULL, "PATCH", url,
                                  content_type, body, body_len);
}

neverc_http_response_t *neverc_http_do(const char *method, const char *url,
                                        const char *content_type,
                                        const void *body, size_t body_len) {
    if (!method) return make_error_response("null method");
    return execute_client_request(&g_default_client, NULL, method, url,
                                  content_type, body, body_len);
}

neverc_http_response_t *neverc_http_client_do(
    neverc_http_client_t *client, const char *method, const char *url,
    const char *content_type, const void *body, size_t body_len) {
    if (!client) return make_error_response("null client");
    if (!method) return make_error_response("null method");
    return execute_client_request(client, NULL, method, url, content_type,
                                  body, body_len);
}

neverc_http_response_t *neverc_http_client_do_context(
    neverc_http_client_t *client, neverc_context_t *context,
    const char *method, const char *url, const char *content_type,
    const void *body, size_t body_len) {
    if (!client) return make_error_response("null client");
    if (!context) return make_error_response("null context");
    if (!method) return make_error_response("null method");
    return execute_client_request(client, context, method, url,
                                  content_type, body, body_len);
}

void neverc_http_response_free(neverc_http_response_t *resp) {
    if (!resp) return;
    free(resp->body);
    free(resp->headers);
    free(resp->trailers);
    free(resp);
}

/* ======================================================================
 * Cookie API
 * ====================================================================== */

void neverc_http_set_cookie(neverc_http_response_writer_t *w,
                              const neverc_http_cookie_t *c) {
    if (!w || !c || !c->name || !c->value) return;

    char buf[2048];
    int n = snprintf(buf, sizeof(buf), "%s=%s", c->name, c->value);

    if (c->path && c->path[0])
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; Path=%s", c->path);
    if (c->domain && c->domain[0])
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; Domain=%s", c->domain);
    if (c->max_age != 0)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; Max-Age=%d", c->max_age);
    if (c->secure)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; Secure");
    if (c->http_only)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; HttpOnly");
    if (c->same_site == 1)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; SameSite=Lax");
    else if (c->same_site == 2)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; SameSite=Strict");
    else if (c->same_site == 3)
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "; SameSite=None");

    neverc_http_set_header(w, "Set-Cookie", buf);
}

const char *neverc_http_get_cookie(const neverc_http_request_t *req,
                                     const char *name,
                                     char *buf, size_t buflen) {
    if (!req || !name || !buf || buflen == 0) return NULL;

    const char *cookie_hdr = neverc_http_request_header(req, "Cookie");
    if (!cookie_hdr) return NULL;

    size_t nlen = strlen(name);
    const char *p = cookie_hdr;

    while (*p) {
        while (*p == ' ' || *p == ';') p++;
        if (!*p) break;

        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            const char *val = p + nlen + 1;
            size_t i = 0;
            while (val[i] && val[i] != ';' && val[i] != ' ' && i < buflen - 1) {
                buf[i] = val[i];
                i++;
            }
            buf[i] = '\0';
            return buf;
        }

        while (*p && *p != ';') p++;
    }
    return NULL;
}

/* ======================================================================
 * Server-Sent Events (SSE)
 * ====================================================================== */

int neverc_http_sse_begin(neverc_http_response_writer_t *w) {
    if (!w || w->fd == NC_INVALID_SOCK) return -1;
    w->headers_sent = 1;
    w->keep_alive = 0;

    const char *hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n";
    return nc_http_sock_write_all_timeout(w->fd, hdr, strlen(hdr),
                                           w->write_timeout_ms);
}

int neverc_http_sse_event(neverc_http_response_writer_t *w,
                            const char *event, const char *data,
                            const char *id) {
    if (!w || !data || w->fd == NC_INVALID_SOCK) return -1;

    nc_buf_t buf;
    nc_buf_init(&buf);

    if (id) {
        nc_buf_append(&buf, "id: ", 4);
        nc_buf_append(&buf, id, strlen(id));
        nc_buf_append(&buf, "\n", 1);
    }
    if (event) {
        nc_buf_append(&buf, "event: ", 7);
        nc_buf_append(&buf, event, strlen(event));
        nc_buf_append(&buf, "\n", 1);
    }

    const char *p = data;
    while (*p) {
        const char *nl = strchr(p, '\n');
        if (nl) {
            nc_buf_append(&buf, "data: ", 6);
            nc_buf_append(&buf, p, (size_t)(nl - p));
            nc_buf_append(&buf, "\n", 1);
            p = nl + 1;
        } else {
            nc_buf_append(&buf, "data: ", 6);
            nc_buf_append(&buf, p, strlen(p));
            nc_buf_append(&buf, "\n", 1);
            break;
        }
    }
    nc_buf_append(&buf, "\n", 1);

    int rc = nc_http_sock_write_all_timeout(
        w->fd, buf.data, buf.len, w->write_timeout_ms);
    nc_buf_free(&buf);
    return rc;
}

int neverc_http_sse_retry(neverc_http_response_writer_t *w, int ms) {
    if (!w || w->fd == NC_INVALID_SOCK) return -1;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "retry: %d\n\n", ms);
    return nc_http_sock_write_all_timeout(
        w->fd, buf, (size_t)n, w->write_timeout_ms);
}

void neverc_http_sse_end(neverc_http_response_writer_t *w) {
    if (w) w->keep_alive = 0;
}

/* ======================================================================
 * Multipart Form Parsing (RFC 2046)
 * ====================================================================== */

#define MULTIPART_MAX_PARTS 256

struct neverc_http_multipart {
    neverc_http_multipart_part_t *parts;
    int nparts;
    char *storage;
};

static const char *find_boundary(const char *content_type) {
    if (!content_type) return NULL;
    const char *bp = strstr(content_type, "boundary=");
    if (!bp) return NULL;
    bp += 9;
    if (*bp == '"') {
        bp++;
        const char *end = strchr(bp, '"');
        if (!end) return NULL;
        return bp;
    }
    return bp;
}

static size_t boundary_len(const char *boundary) {
    size_t len = 0;
    while (boundary[len] && boundary[len] != '"' &&
           boundary[len] != ';' && boundary[len] != ' ' &&
           boundary[len] != '\r' && boundary[len] != '\n')
        len++;
    return len;
}

static void parse_part_headers(const char *hdr, size_t hdrlen,
                                neverc_http_multipart_part_t *part) {
    const char *end = hdr + hdrlen;
    const char *p = hdr;
    while (p < end) {
        const char *line_end = NULL;
        for (const char *q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') { line_end = q; break; }
        }
        if (!line_end) break;
        if (line_end == p) { p = line_end + 2; break; }

        size_t llen = (size_t)(line_end - p);
        if (llen > 20 && strncasecmp(p, "Content-Disposition:", 20) == 0) {
            const char *cd = p + 20;
            while (cd < line_end && *cd == ' ') cd++;

            const char *nm = strstr(cd, "name=\"");
            if (nm && nm < line_end) {
                nm += 6;
                const char *ne = strchr(nm, '"');
                if (ne && ne <= line_end) {
                    char *s = strndup_safe(nm, (size_t)(ne - nm));
                    part->name = s;
                }
            }
            const char *fn = strstr(cd, "filename=\"");
            if (fn && fn < line_end) {
                fn += 10;
                const char *fe = strchr(fn, '"');
                if (fe && fe <= line_end) {
                    char *s = strndup_safe(fn, (size_t)(fe - fn));
                    part->filename = s;
                }
            }
        }
        if (llen > 13 && strncasecmp(p, "Content-Type:", 13) == 0) {
            const char *ct = p + 13;
            while (ct < line_end && *ct == ' ') ct++;
            char *s = strndup_safe(ct, (size_t)(line_end - ct));
            part->content_type = s;
        }
        p = line_end + 2;
    }
}

neverc_http_multipart_t *neverc_http_multipart_parse(
    const char *content_type, const char *body, size_t body_len) {
    const char *boundary = find_boundary(content_type);
    if (!boundary || !body || body_len == 0) return NULL;

    size_t blen = boundary_len(boundary);
    if (blen == 0 || blen > 200) return NULL;

    char delim[256];
    delim[0] = '-';
    delim[1] = '-';
    memcpy(delim + 2, boundary, blen);
    size_t dlen = blen + 2;

    neverc_http_multipart_t *mp =
        (neverc_http_multipart_t *)calloc(1, sizeof(*mp));
    if (!mp) return NULL;
    mp->parts = (neverc_http_multipart_part_t *)calloc(
        MULTIPART_MAX_PARTS, sizeof(neverc_http_multipart_part_t));
    if (!mp->parts) { free(mp); return NULL; }

    const char *end = body + body_len;
    const char *pos = body;

    /* Skip preamble: find first boundary */
    const char *first = NULL;
    for (const char *s = pos; s + dlen <= end; s++) {
        if (memcmp(s, delim, dlen) == 0) { first = s; break; }
    }
    if (!first) { free(mp->parts); free(mp); return NULL; }

    pos = first + dlen;
    if (pos + 2 <= end && pos[0] == '-' && pos[1] == '-') {
        /* No parts, just closing delimiter */
        free(mp->parts);
        free(mp);
        return NULL;
    }
    while (pos < end && (*pos == '\r' || *pos == '\n')) pos++;

    while (pos < end && mp->nparts < MULTIPART_MAX_PARTS) {
        /* Find next boundary */
        const char *next_bound = NULL;
        for (const char *s = pos; s + dlen <= end; s++) {
            if (s[0] == '\r' && s[1] == '\n' &&
                memcmp(s + 2, delim, dlen) == 0) {
                next_bound = s;
                break;
            }
        }
        if (!next_bound) break;

        /* Part content is from pos to next_bound */
        const char *part_data = pos;
        size_t part_len = (size_t)(next_bound - pos);

        /* Find headers/body separator within part */
        const char *hdr_end = NULL;
        for (size_t i = 0; i + 3 < part_len; i++) {
            if (part_data[i] == '\r' && part_data[i+1] == '\n' &&
                part_data[i+2] == '\r' && part_data[i+3] == '\n') {
                hdr_end = part_data + i;
                break;
            }
        }

        neverc_http_multipart_part_t *p = &mp->parts[mp->nparts];
        if (hdr_end) {
            parse_part_headers(part_data, (size_t)(hdr_end + 2 - part_data), p);
            p->data = hdr_end + 4;
            p->data_len = part_len - (size_t)(hdr_end + 4 - part_data);
        } else {
            p->data = part_data;
            p->data_len = part_len;
        }
        mp->nparts++;

        /* Move past boundary */
        pos = next_bound + 2 + dlen;
        if (pos + 2 <= end && pos[0] == '-' && pos[1] == '-')
            break; /* closing delimiter */
        while (pos < end && (*pos == '\r' || *pos == '\n')) pos++;
    }

    return mp;
}

int neverc_http_multipart_count(const neverc_http_multipart_t *mp) {
    return mp ? mp->nparts : 0;
}

const neverc_http_multipart_part_t *neverc_http_multipart_get(
    const neverc_http_multipart_t *mp, int index) {
    if (!mp || index < 0 || index >= mp->nparts) return NULL;
    return &mp->parts[index];
}

const neverc_http_multipart_part_t *neverc_http_multipart_field(
    const neverc_http_multipart_t *mp, const char *name) {
    if (!mp || !name) return NULL;
    for (int i = 0; i < mp->nparts; i++) {
        if (mp->parts[i].name && strcmp(mp->parts[i].name, name) == 0)
            return &mp->parts[i];
    }
    return NULL;
}

void neverc_http_multipart_free(neverc_http_multipart_t *mp) {
    if (!mp) return;
    for (int i = 0; i < mp->nparts; i++) {
        free((void *)mp->parts[i].name);
        free((void *)mp->parts[i].filename);
        free((void *)mp->parts[i].content_type);
    }
    free(mp->parts);
    free(mp->storage);
    free(mp);
}

/* ======================================================================
 * Content Type Detection — WHATWG MIME Sniffing Standard
 * https://mimesniff.spec.whatwg.org/
 * ====================================================================== */

#define SNIFF_LEN 512

static int sniff_is_ws(unsigned char b) {
    return b == '\t' || b == '\n' || b == '\x0c' || b == '\r' || b == ' ';
}

static int sniff_is_tt(unsigned char b) {
    return b == ' ' || b == '>';
}

static const char *sniff_exact(const unsigned char *data, size_t dlen,
                                const unsigned char *sig, size_t slen,
                                const char *ct) {
    if (dlen < slen) return NULL;
    if (memcmp(data, sig, slen) == 0) return ct;
    return NULL;
}

static const char *sniff_masked(const unsigned char *data, size_t dlen,
                                 const unsigned char *mask,
                                 const unsigned char *pat, size_t plen,
                                 int skip_ws, int first_nws,
                                 const char *ct) {
    const unsigned char *d = data;
    size_t dl = dlen;
    if (skip_ws) {
        if ((size_t)first_nws > dl) return NULL;
        d += first_nws;
        dl -= (size_t)first_nws;
    }
    if (dl < plen) return NULL;
    for (size_t i = 0; i < plen; i++) {
        if ((d[i] & mask[i]) != pat[i]) return NULL;
    }
    return ct;
}

static const char *sniff_html(const unsigned char *data, size_t dlen,
                               int first_nws, const char *tag, size_t tlen) {
    const unsigned char *d = data + first_nws;
    size_t dl = dlen - (size_t)first_nws;
    if (dl < tlen + 1) return NULL;
    for (size_t i = 0; i < tlen; i++) {
        unsigned char b = (unsigned char)tag[i];
        unsigned char db = d[i];
        if (b >= 'A' && b <= 'Z') db &= 0xDF;
        if (b != db) return NULL;
    }
    if (!sniff_is_tt(d[tlen])) return NULL;
    return "text/html; charset=utf-8";
}

static const char *sniff_mp4(const unsigned char *data, size_t dlen) {
    if (dlen < 12) return NULL;
    uint32_t box_size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                        ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    if (dlen < box_size || box_size % 4 != 0) return NULL;
    if (memcmp(data + 4, "ftyp", 4) != 0) return NULL;
    for (uint32_t st = 8; st < box_size; st += 4) {
        if (st == 12) continue;
        if (memcmp(data + st, "mp4", 3) == 0) return "video/mp4";
    }
    return NULL;
}

static const char *sniff_text(const unsigned char *data, size_t dlen,
                               int first_nws) {
    for (size_t i = (size_t)first_nws; i < dlen; i++) {
        unsigned char b = data[i];
        if (b <= 0x08 || b == 0x0B ||
            (b >= 0x0E && b <= 0x1A) ||
            (b >= 0x1C && b <= 0x1F))
            return NULL;
    }
    return "text/plain; charset=utf-8";
}

const char *neverc_http_detect_content_type(const void *data, size_t len) {
    if (!data || len == 0) return "application/octet-stream";

    const unsigned char *d = (const unsigned char *)data;
    size_t dlen = len > SNIFF_LEN ? SNIFF_LEN : len;

    int first_nws = 0;
    while ((size_t)first_nws < dlen && sniff_is_ws(d[first_nws]))
        first_nws++;

    const char *ct;

    /* HTML signatures */
    static const char *html_tags[] = {
        "<!DOCTYPE HTML", "<HTML", "<HEAD", "<SCRIPT", "<IFRAME",
        "<H1", "<DIV", "<FONT", "<TABLE", "<A", "<STYLE", "<TITLE",
        "<B", "<BODY", "<BR", "<P", "<!--", NULL
    };
    for (int i = 0; html_tags[i]; i++) {
        ct = sniff_html(d, dlen, first_nws, html_tags[i], strlen(html_tags[i]));
        if (ct) return ct;
    }

    /* XML */
    {
        static const unsigned char xml_mask[] = {0xFF,0xFF,0xFF,0xFF,0xFF};
        static const unsigned char xml_pat[] = "<?xml";
        ct = sniff_masked(d, dlen, xml_mask, xml_pat, 5, 1, first_nws,
                           "text/xml; charset=utf-8");
        if (ct) return ct;
    }

    /* PDF / PostScript */
    ct = sniff_exact(d, dlen, (const unsigned char *)"%PDF-", 5, "application/pdf");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"%!PS-Adobe-", 11, "application/postscript");
    if (ct) return ct;

    /* UTF BOMs */
    if (dlen >= 2 && d[0] == 0xFE && d[1] == 0xFF) return "text/plain; charset=utf-16be";
    if (dlen >= 2 && d[0] == 0xFF && d[1] == 0xFE) return "text/plain; charset=utf-16le";
    if (dlen >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) return "text/plain; charset=utf-8";

    /* Image types */
    if (dlen >= 4 && d[0]==0 && d[1]==0 && d[2]==1 && d[3]==0) return "image/x-icon";
    if (dlen >= 4 && d[0]==0 && d[1]==0 && d[2]==2 && d[3]==0) return "image/x-icon";
    ct = sniff_exact(d, dlen, (const unsigned char *)"BM", 2, "image/bmp");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"GIF87a", 6, "image/gif");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"GIF89a", 6, "image/gif");
    if (ct) return ct;
    if (dlen >= 14 && memcmp(d, "RIFF", 4) == 0 && memcmp(d+8, "WEBPVP", 6) == 0)
        return "image/webp";
    {
        static const unsigned char png_sig[] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
        ct = sniff_exact(d, dlen, png_sig, 8, "image/png");
        if (ct) return ct;
    }
    if (dlen >= 3 && d[0]==0xFF && d[1]==0xD8 && d[2]==0xFF) return "image/jpeg";

    /* Audio/Video */
    if (dlen >= 12 && memcmp(d, "FORM", 4) == 0 && memcmp(d+8, "AIFF", 4) == 0)
        return "audio/aiff";
    ct = sniff_exact(d, dlen, (const unsigned char *)"ID3", 3, "audio/mpeg");
    if (ct) return ct;
    {
        static const unsigned char ogg_sig[] = {0x4F,0x67,0x67,0x53,0x00};
        ct = sniff_exact(d, dlen, ogg_sig, 5, "application/ogg");
        if (ct) return ct;
    }
    {
        static const unsigned char midi_sig[] = {0x4D,0x54,0x68,0x64,0x00,0x00,0x00,0x06};
        ct = sniff_exact(d, dlen, midi_sig, 8, "audio/midi");
        if (ct) return ct;
    }
    if (dlen >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d+8, "AVI ", 4) == 0)
        return "video/avi";
    if (dlen >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d+8, "WAVE", 4) == 0)
        return "audio/wave";

    ct = sniff_mp4(d, dlen);
    if (ct) return ct;

    {
        static const unsigned char webm_sig[] = {0x1A,0x45,0xDF,0xA3};
        ct = sniff_exact(d, dlen, webm_sig, 4, "video/webm");
        if (ct) return ct;
    }

    /* Font types */
    if (dlen >= 4 && d[0]==0 && d[1]==1 && d[2]==0 && d[3]==0) return "font/ttf";
    ct = sniff_exact(d, dlen, (const unsigned char *)"OTTO", 4, "font/otf");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"ttcf", 4, "font/collection");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"wOFF", 4, "font/woff");
    if (ct) return ct;
    ct = sniff_exact(d, dlen, (const unsigned char *)"wOF2", 4, "font/woff2");
    if (ct) return ct;

    /* Archive types */
    {
        static const unsigned char gz_sig[] = {0x1F,0x8B,0x08};
        ct = sniff_exact(d, dlen, gz_sig, 3, "application/x-gzip");
        if (ct) return ct;
    }
    {
        static const unsigned char zip_sig[] = {0x50,0x4B,0x03,0x04};
        ct = sniff_exact(d, dlen, zip_sig, 4, "application/zip");
        if (ct) return ct;
    }
    {
        static const unsigned char rar4_sig[] = {0x52,0x61,0x72,0x21,0x1A,0x07,0x00};
        ct = sniff_exact(d, dlen, rar4_sig, 7, "application/x-rar-compressed");
        if (ct) return ct;
    }
    {
        static const unsigned char rar5_sig[] = {0x52,0x61,0x72,0x21,0x1A,0x07,0x01,0x00};
        ct = sniff_exact(d, dlen, rar5_sig, 8, "application/x-rar-compressed");
        if (ct) return ct;
    }

    /* WebAssembly */
    {
        static const unsigned char wasm_sig[] = {0x00,0x61,0x73,0x6D};
        ct = sniff_exact(d, dlen, wasm_sig, 4, "application/wasm");
        if (ct) return ct;
    }

    /* Text fallback */
    ct = sniff_text(d, dlen, first_nws);
    if (ct) return ct;

    return "application/octet-stream";
}

/* ======================================================================
 * Handler Wrappers
 * ====================================================================== */

void neverc_http_not_found(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w) {
    (void)req;
    neverc_http_error(w, "404 page not found", 404);
}

typedef struct {
    char *prefix;
    size_t prefix_len;
    neverc_http_handler_func_t inner;
} strip_prefix_ctx_t;

#define MAX_STRIP_PREFIX 16
static strip_prefix_ctx_t g_strip_prefixes[MAX_STRIP_PREFIX];
static int g_strip_prefix_count = 0;

static void strip_prefix_handler_fn(neverc_http_request_t *req,
                                      neverc_http_response_writer_t *w) {
    for (int i = 0; i < g_strip_prefix_count; i++) {
        strip_prefix_ctx_t *ctx = &g_strip_prefixes[i];
        if (req->path && strncmp(req->path, ctx->prefix, ctx->prefix_len) == 0) {
            neverc_http_request_t stripped = *req;
            stripped.path = req->path + ctx->prefix_len;
            if (stripped.path[0] == '\0') stripped.path = "/";
            ctx->inner(&stripped, w);
            return;
        }
    }
    neverc_http_not_found(req, w);
}

void neverc_http_strip_prefix(neverc_http_mux_t *mux, const char *prefix,
                                const char *pattern,
                                neverc_http_handler_func_t handler) {
    if (!prefix || !handler || g_strip_prefix_count >= MAX_STRIP_PREFIX)
        return;

    strip_prefix_ctx_t *ctx = &g_strip_prefixes[g_strip_prefix_count++];
    ctx->prefix = strdup(prefix);
    ctx->prefix_len = strlen(prefix);
    ctx->inner = handler;

    if (mux)
        neverc_http_mux_handle(mux, pattern ? pattern : prefix,
                                strip_prefix_handler_fn);
    else
        neverc_http_handle_func(pattern ? pattern : prefix,
                                 strip_prefix_handler_fn);
}

/* ======================================================================
 * Serve File — with Content-Type detection, Range, If-Modified-Since
 * ====================================================================== */

void neverc_http_serve_file(neverc_http_response_writer_t *w,
                              neverc_http_request_t *req,
                              const char *filepath) {
    if (!w || !filepath) return;

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        neverc_http_set_status(w, 404);
        neverc_http_write_string(w, "404 page not found\n");
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        fclose(f);
        neverc_http_set_status(w, 500);
        neverc_http_write_string(w, "internal error\n");
        return;
    }

    /* Detect content type from first 512 bytes */
    unsigned char sniff_buf[512];
    size_t sniff_n = fread(sniff_buf, 1, sizeof(sniff_buf), f);
    fseek(f, 0, SEEK_SET);

    const char *ct = neverc_http_detect_content_type(sniff_buf, sniff_n);

    /* Also try file extension for common types */
    const char *ext = strrchr(filepath, '.');
    if (ext) {
        if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
            ct = "text/html; charset=utf-8";
        else if (strcasecmp(ext, ".css") == 0) ct = "text/css; charset=utf-8";
        else if (strcasecmp(ext, ".js") == 0) ct = "application/javascript";
        else if (strcasecmp(ext, ".json") == 0) ct = "application/json";
        else if (strcasecmp(ext, ".svg") == 0) ct = "image/svg+xml";
        else if (strcasecmp(ext, ".xml") == 0) ct = "text/xml; charset=utf-8";
        else if (strcasecmp(ext, ".txt") == 0) ct = "text/plain; charset=utf-8";
        else if (strcasecmp(ext, ".wasm") == 0) ct = "application/wasm";
    }

    neverc_http_set_header(w, "Content-Type", ct);
    neverc_http_set_header(w, "Accept-Ranges", "bytes");

    char cl_buf[32];
    snprintf(cl_buf, sizeof(cl_buf), "%ld", fsize);
    neverc_http_set_header(w, "Content-Length", cl_buf);

    /* HEAD request: headers only */
    if (req && req->method && strcmp(req->method, "HEAD") == 0) {
        fclose(f);
        return;
    }

    /* Read and write the file */
    char buf[8192];
    size_t total = 0;
    while (total < (size_t)fsize) {
        size_t to_read = sizeof(buf);
        if (total + to_read > (size_t)fsize)
            to_read = (size_t)fsize - total;
        size_t n = fread(buf, 1, to_read, f);
        if (n == 0) break;
        neverc_http_write(w, buf, n);
        total += n;
    }

    fclose(f);
}

/* ======================================================================
 * Header Utilities
 * ====================================================================== */

char *neverc_http_canonical_header_key(const char *key, char *buf,
                                         size_t buflen) {
    if (!key || !buf || buflen == 0) return buf;

    int upper = 1;
    size_t i;
    for (i = 0; key[i] && i < buflen - 1; i++) {
        unsigned char c = (unsigned char)key[i];
        if (c == '-') {
            buf[i] = '-';
            upper = 1;
        } else if (upper) {
            buf[i] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
            upper = 0;
        } else {
            buf[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
    }
    buf[i] = '\0';
    return buf;
}

const char *neverc_http_response_header(const neverc_http_response_t *resp,
                                          const char *name,
                                          char *buf, size_t buflen) {
    if (!resp || !resp->headers || !name || !buf || buflen == 0) return NULL;

    size_t nlen = strlen(name);
    const char *p = resp->headers;

    while (*p) {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end) line_end = p + strlen(p);

        const char *colon = strchr(p, ':');
        if (colon && colon < line_end) {
            size_t klen = (size_t)(colon - p);
            if (klen == nlen && strncasecmp(p, name, nlen) == 0) {
                const char *val = colon + 1;
                while (val < line_end && *val == ' ') val++;
                size_t vlen = (size_t)(line_end - val);
                if (vlen >= buflen) vlen = buflen - 1;
                memcpy(buf, val, vlen);
                buf[vlen] = '\0';
                return buf;
            }
        }

        if (*line_end == '\0') break;
        p = line_end + 2;
    }

    return NULL;
}

/* ======================================================================
 * CORS — Cross-Origin Resource Sharing
 * ====================================================================== */

void neverc_http_cors_headers(neverc_http_response_writer_t *w,
                                const neverc_http_cors_config_t *cfg,
                                const char *origin) {
    if (!w || !cfg) return;

    const char *ao = cfg->allowed_origins ? cfg->allowed_origins : "*";

    if (strcmp(ao, "*") == 0) {
        neverc_http_set_header(w, "Access-Control-Allow-Origin", "*");
    } else if (origin && strstr(ao, origin)) {
        neverc_http_set_header(w, "Access-Control-Allow-Origin", origin);
        neverc_http_set_header(w, "Vary", "Origin");
    }

    if (cfg->allow_credentials)
        neverc_http_set_header(w, "Access-Control-Allow-Credentials", "true");

    if (cfg->exposed_headers)
        neverc_http_set_header(w, "Access-Control-Expose-Headers",
                                cfg->exposed_headers);
}

static void cors_preflight_handler(neverc_http_request_t *req,
                                     neverc_http_response_writer_t *w) {
    if (!req || !w) return;

    const char *origin = neverc_http_request_header(req, "Origin");
    neverc_http_cors_headers(w, &g_cors_config, origin);

    const char *methods = g_cors_config.allowed_methods
        ? g_cors_config.allowed_methods
        : "GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD";
    neverc_http_set_header(w, "Access-Control-Allow-Methods", methods);

    const char *headers = g_cors_config.allowed_headers
        ? g_cors_config.allowed_headers
        : "Content-Type, Authorization, Accept, X-Requested-With";
    neverc_http_set_header(w, "Access-Control-Allow-Headers", headers);

    char max_age_buf[16];
    int ma = g_cors_config.max_age > 0 ? g_cors_config.max_age : 86400;
    snprintf(max_age_buf, sizeof(max_age_buf), "%d", ma);
    neverc_http_set_header(w, "Access-Control-Max-Age", max_age_buf);

    neverc_http_set_status(w, 204);
}

void neverc_http_enable_cors(neverc_http_mux_t *mux,
                               const neverc_http_cors_config_t *config) {
    if (config) {
        g_cors_config = *config;
    } else {
        memset(&g_cors_config, 0, sizeof(g_cors_config));
        g_cors_config.allowed_origins = "*";
        g_cors_config.allowed_methods = "GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD";
        g_cors_config.allowed_headers = "Content-Type, Authorization, Accept, X-Requested-With";
        g_cors_config.max_age = 86400;
    }
    g_cors_enabled = 1;

    /* Register an OPTIONS handler for preflight requests */
    if (mux)
        neverc_http_mux_handle(mux, "OPTIONS /", cors_preflight_handler);
    else
        neverc_http_handle_func("OPTIONS /", cors_preflight_handler);
}

/* ======================================================================
 * JSON Request Helpers
 * ====================================================================== */

const char *neverc_http_json_get(const neverc_http_request_t *req,
                                   const char *key, char *buf, size_t buflen) {
    if (!req || !req->body || req->body_len == 0 || !key || !buf || buflen == 0)
        return NULL;

    size_t klen = strlen(key);
    const char *p = req->body;
    const char *end = req->body + req->body_len;

    /* Search for "key": or "key" : */
    while (p < end) {
        const char *quote = memchr(p, '"', (size_t)(end - p));
        if (!quote) break;

        const char *kstart = quote + 1;
        if (kstart + klen >= end) break;

        if (memcmp(kstart, key, klen) == 0 && kstart[klen] == '"') {
            /* Found key. Skip to colon and value. */
            const char *after_key = kstart + klen + 1;
            while (after_key < end && (*after_key == ' ' || *after_key == ':'))
                after_key++;

            if (after_key >= end) break;

            if (*after_key == '"') {
                /* String value */
                const char *vstart = after_key + 1;
                const char *vend = vstart;
                while (vend < end && *vend != '"') {
                    if (*vend == '\\' && vend + 1 < end) vend++; /* skip escape */
                    vend++;
                }
                size_t vlen = (size_t)(vend - vstart);
                if (vlen >= buflen) vlen = buflen - 1;
                memcpy(buf, vstart, vlen);
                buf[vlen] = '\0';
                return buf;
            }

            /* Number, boolean, null */
            const char *vstart = after_key;
            const char *vend = vstart;
            while (vend < end && *vend != ',' && *vend != '}' &&
                   *vend != ' ' && *vend != '\n' && *vend != '\r')
                vend++;
            size_t vlen = (size_t)(vend - vstart);
            if (vlen >= buflen) vlen = buflen - 1;
            memcpy(buf, vstart, vlen);
            buf[vlen] = '\0';
            return buf;
        }

        p = kstart + 1;
    }
    return NULL;
}

int neverc_http_json_error(neverc_http_response_writer_t *w,
                             int code, const char *message) {
    if (!w) return 0;
    neverc_http_set_status(w, code);
    neverc_http_set_header(w, "Content-Type", "application/json; charset=utf-8");
    return neverc_http_writef(w, "{\"error\":\"%s\",\"code\":%d}",
                              message ? message : "error", code);
}

/* ======================================================================
 * Server-Sent Events (SSE) Implementation
 *
 * SSE (RFC 8895 / W3C EventSource) enables server-to-client push over
 * a long-lived HTTP connection. Protocol:
 *   - Response: Content-Type: text/event-stream
 *   - Events: "event: <type>\ndata: <payload>\nid: <id>\n\n"
 *   - Keep-alive: ": comment\n\n"
 *   - Retry: "retry: <ms>\n\n"
 * ====================================================================== */

struct neverc_sse {
    neverc_tcp_conn_t *connection;
    int                closed;
};

static int sse_tcp_write_all(neverc_tcp_conn_t *connection,
                             const char *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        int n = neverc_tcp_write(connection, data + offset, length - offset);
        if (n <= 0) return -1;
        offset += (size_t)n;
    }
    return 0;
}

neverc_sse_t *neverc_sse_start(neverc_http_response_writer_t *w) {
    if (!w) return NULL;

    neverc_http_set_status(w, 200);
    neverc_http_set_header(w, "Content-Type", "text/event-stream");
    neverc_http_set_header(w, "Cache-Control", "no-cache");
    neverc_http_set_header(w, "Connection", "keep-alive");
    neverc_http_set_header(w, "X-Accel-Buffering", "no");

    neverc_tcp_conn_t *connection = neverc_http_hijack(w);
    if (!connection) return NULL;
    (void)neverc_tcp_set_write_timeout(connection, w->write_timeout_ms);

    const char *headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "Transfer-Encoding: chunked\r\n\r\n";
    int write_result = sse_tcp_write_all(
        connection, headers, strlen(headers));
    if (write_result != 0) {
        neverc_tcp_close(connection);
        return NULL;
    }

    neverc_sse_t *sse = (neverc_sse_t *)malloc(sizeof(*sse));
    if (!sse) {
        neverc_tcp_close(connection);
        return NULL;
    }
    sse->connection = connection;
    sse->closed = 0;
    return sse;
}

static int sse_write_chunk(neverc_sse_t *sse, const char *data, size_t len) {
    if (!sse || sse->closed || !sse->connection) return -1;

    char size_buf[32];
    int slen = snprintf(size_buf, sizeof(size_buf), "%zx\r\n", len);
    if (sse_tcp_write_all(sse->connection, size_buf, (size_t)slen) != 0) {
        sse->closed = 1;
        return -1;
    }
    if (sse_tcp_write_all(sse->connection, data, len) != 0) {
        sse->closed = 1;
        return -1;
    }
    if (sse_tcp_write_all(sse->connection, "\r\n", 2) != 0) {
        sse->closed = 1;
        return -1;
    }
    return 0;
}

int neverc_sse_send(neverc_sse_t *sse, const char *event_type,
                     const char *data, const char *id) {
    if (!sse || sse->closed) return -1;
    if ((event_type && contains_crlf(event_type)) ||
        (id && contains_crlf(id))) return -1;

    nc_buf_t ev;
    nc_buf_init(&ev);

    if (id && id[0]) {
        nc_buf_append(&ev, "id: ", 4);
        nc_buf_append(&ev, id, strlen(id));
        nc_buf_append(&ev, "\n", 1);
    }
    if (event_type && event_type[0]) {
        nc_buf_append(&ev, "event: ", 7);
        nc_buf_append(&ev, event_type, strlen(event_type));
        nc_buf_append(&ev, "\n", 1);
    }
    if (data) {
        const char *p = data;
        while (*p) {
            const char *nl = strchr(p, '\n');
            nc_buf_append(&ev, "data: ", 6);
            if (nl) {
                nc_buf_append(&ev, p, (size_t)(nl - p));
                nc_buf_append(&ev, "\n", 1);
                p = nl + 1;
            } else {
                nc_buf_append(&ev, p, strlen(p));
                nc_buf_append(&ev, "\n", 1);
                break;
            }
        }
    } else {
        nc_buf_append(&ev, "data: \n", 7);
    }
    nc_buf_append(&ev, "\n", 1);

    int rc = sse_write_chunk(sse, ev.data, ev.len);
    nc_buf_free(&ev);
    return rc;
}

int neverc_sse_send_id(neverc_sse_t *sse, const char *event_type,
                        const char *data, const char *id) {
    return neverc_sse_send(sse, event_type, data, id);
}

int neverc_sse_retry(neverc_sse_t *sse, int retry_ms) {
    if (!sse || sse->closed) return -1;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "retry: %d\n\n", retry_ms);
    return sse_write_chunk(sse, buf, (size_t)n);
}

int neverc_sse_comment(neverc_sse_t *sse, const char *text) {
    if (!sse || sse->closed) return -1;

    nc_buf_t ev;
    nc_buf_init(&ev);
    nc_buf_append(&ev, ": ", 2);
    if (text) nc_buf_append(&ev, text, strlen(text));
    nc_buf_append(&ev, "\n\n", 2);

    int rc = sse_write_chunk(sse, ev.data, ev.len);
    nc_buf_free(&ev);
    return rc;
}

void neverc_sse_close(neverc_sse_t *sse) {
    if (!sse) return;
    if (!sse->closed && sse->connection) {
        (void)sse_tcp_write_all(sse->connection, "0\r\n\r\n", 5);
    }
    if (sse->connection) neverc_tcp_close(sse->connection);
    sse->connection = NULL;
    sse->closed = 1;
    free(sse);
}
