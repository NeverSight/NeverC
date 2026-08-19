/*
 * NeverC HTTP Client + Extended Features
 * Split from http.c to avoid large-TU compiler issue.
 */
#include "_http_internal.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/time.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif

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
    char                       *root_cert_file;
    char                       *client_cert_file;
    char                       *client_key_file;
};

static neverc_http_client_t g_default_client = {
    .config = {
        .max_redirects = 10,
        .timeout_ms = 30000,
        .max_idle_per_host = POOL_MAX_IDLE_DEFAULT,
        .max_response_header_size = CLIENT_HEADER_LIMIT_DEFAULT,
        .max_response_body_size = CLIENT_BODY_LIMIT_DEFAULT,
        .root_cert_file = NULL,
        .client_cert_file = NULL,
        .client_key_file = NULL,
        .insecure_skip_verify = 0,
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
    config.root_cert_file = NULL;
    config.client_cert_file = NULL;
    config.client_key_file = NULL;
    config.insecure_skip_verify = 0;
    return config;
}

static int client_config_valid(const neverc_http_client_config_t *config) {
    return config && config->max_redirects >= 0 && config->timeout_ms > 0 &&
           config->max_idle_per_host >= 0 &&
           (config->insecure_skip_verify == 0 ||
            config->insecure_skip_verify == 1) &&
           ((config->client_cert_file == NULL) ==
            (config->client_key_file == NULL)) &&
           config->max_response_header_size > 0 &&
           config->max_response_body_size > 0 &&
           config->max_response_header_size <= SIZE_MAX - 4 &&
           config->max_response_body_size <=
               SIZE_MAX - config->max_response_header_size - 4;
}

static size_t client_saturating_add_size(size_t left, size_t right) {
    return left > SIZE_MAX - right ? SIZE_MAX : left + right;
}

static size_t client_saturating_mul_size(size_t value, size_t multiplier) {
    return value != 0 && multiplier > SIZE_MAX / value
        ? SIZE_MAX : value * multiplier;
}

/* One-byte chunks need at most five framing bytes per body byte. The second
 * header allowance is shared by chunk extensions and trailers. */
static size_t client_response_wire_limit(size_t header_limit,
                                         size_t body_limit) {
    size_t limit = client_saturating_add_size(header_limit, 4U);
    limit = client_saturating_add_size(
        limit, client_saturating_mul_size(body_limit, 6U));
    limit = client_saturating_add_size(limit, header_limit);
    return client_saturating_add_size(limit, 5U);
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
    if (effective.root_cert_file)
        client->root_cert_file = strndup_safe(
            effective.root_cert_file, strlen(effective.root_cert_file));
    if (effective.client_cert_file)
        client->client_cert_file = strndup_safe(
            effective.client_cert_file, strlen(effective.client_cert_file));
    if (effective.client_key_file)
        client->client_key_file = strndup_safe(
            effective.client_key_file, strlen(effective.client_key_file));
    if ((effective.root_cert_file && !client->root_cert_file) ||
        (effective.client_cert_file && !client->client_cert_file) ||
        (effective.client_key_file && !client->client_key_file)) {
        free(client->client_key_file);
        free(client->client_cert_file);
        free(client->root_cert_file);
        free(client);
        return NULL;
    }
    client->config.root_cert_file = client->root_cert_file;
    client->config.client_cert_file = client->client_cert_file;
    client->config.client_key_file = client->client_key_file;
    client->pool.max_idle_per_host = effective.max_idle_per_host;
    pool_init(&client->pool);
    return client;
}

void neverc_http_client_free(neverc_http_client_t *client) {
    if (!client || client == &g_default_client) return;
    pool_close_idle(&client->pool);
    if (nc_atomic_load(&client->pool.initialized))
        nc_mutex_destroy(&client->pool.lock);
    free(client->client_key_file);
    free(client->client_cert_file);
    free(client->root_cert_file);
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
        unsigned char c = (unsigned char)*p;
        if (c < '0' || c > '9') return -1;
        unsigned digit = (unsigned)(c - '0');
        if (value > (65535U - digit) / 10U) return -1;
        value = value * 10U + digit;
    }
    if (value == 0) return -1;
    *port = (uint16_t)value;
    return 0;
}

static int client_valid_port(const char *s, size_t length) {
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

/* Same Host byte allowlist as the HTTP/1 server (Go ValidHostHeader without
 * comma). Rejecting only CTL/comma left '<' '>' '"' in Host, which XSS dumps
 * and reflected Host HTML the same way the unpatched server did. */
static int client_host_reg_name_byte(unsigned char c) {
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

static int client_valid_host(const char *value, size_t length) {
    if (!value || length == 0) return 0;
    if (value[0] == '[') {
        const char *close = (const char *)memchr(value, ']', length);
        if (!close || close == value + 1) return 0;
        size_t inner = (size_t)(close - value - 1);
        int has_colon = 0;
        for (size_t i = 0; i < inner; i++) {
            unsigned char c = (unsigned char)value[1 + i];
            if (c == ':') has_colon = 1;
            else if (!client_host_reg_name_byte(c))
                return 0;
        }
        if (!has_colon &&
            !(inner > 2 && (value[1] == 'v' || value[1] == 'V')))
            return 0;
        size_t after = length - (size_t)(close - value) - 1;
        if (after == 0) return 1;
        return close[1] == ':' && client_valid_port(close + 2, after - 1);
    }

    const char *colon = (const char *)memchr(value, ':', length);
    size_t host_length = colon ? (size_t)(colon - value) : length;
    if (host_length == 0) return 0;
    for (size_t i = 0; i < host_length; i++) {
        if (!client_host_reg_name_byte((unsigned char)value[i]))
            return 0;
    }
    if (!colon) return 1;
    if (memchr(colon + 1, ':', length - host_length - 1)) return 0;
    return client_valid_port(colon + 1, length - host_length - 1);
}

static int parse_http_url(const char *url, parsed_url_t *out) {
    if (!url || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    out->port = 80;

    const char *p;
    if (strncasecmp(url, "http://", 7) == 0) {
        p = url + 7;
    } else if (strncasecmp(url, "https://", 8) == 0) {
        p = url + 8;
        out->port = 443;
        out->is_https = 1;
    } else {
        return -1;
    }

    /* Fragments are client-side only: strip '#' and everything after it. */
    const char *url_end = p + strlen(p);
    const char *fragment = memchr(p, '#', (size_t)(url_end - p));
    if (fragment) url_end = fragment;
    const char *slash = memchr(p, '/', (size_t)(url_end - p));
    const char *question = memchr(p, '?', (size_t)(url_end - p));
    const char *authority_end = url_end;
    if (slash && slash < authority_end) authority_end = slash;
    if (question && question < authority_end) authority_end = question;
    size_t authority_length = (size_t)(authority_end - p);
    if (authority_length == 0 ||
        authority_length >= sizeof(out->authority) ||
        memchr(p, '@', authority_length))
        return -1;
    /* Comma is a Host-list separator in some intermediaries; a URL such as
     * http://evil.example,victim.example/ would otherwise send
     * Host: evil.example,victim.example and override the origin. */
    for (size_t i = 0; i < authority_length; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c <= 0x20 || c == 0x7f || c == '\\' || c == ',') return -1;
    }
    memcpy(out->authority, p, authority_length);
    out->authority[authority_length] = '\0';
    if (!client_valid_host(out->authority, authority_length))
        return -1;

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
    size_t target_length = (size_t)(url_end - target);
    if (target_length > 0 && *target == '?') {
        if (target_length + 1 >= sizeof(out->path)) return -1;
        out->path[0] = '/';
        memcpy(out->path + 1, target, target_length);
        out->path[target_length + 1] = '\0';
    } else if (target_length > 0 && *target == '/') {
        if (target_length >= sizeof(out->path)) return -1;
        memcpy(out->path, target, target_length);
        out->path[target_length] = '\0';
    } else if (target_length == 0) {
        strcpy(out->path, "/");
    } else {
        return -1;
    }
    /* Origin-form leftover: `http://host//evil` would send GET //evil with
     * Host: host. Same open-redirect / XSS case the HTTP/1 server already
     * rejects. Empty path segments (`/foo//bar`) are still allowed. */
    const char *path_query = strchr(out->path, '?');
    size_t path_length = path_query
        ? (size_t)(path_query - out->path) : strlen(out->path);
    if (path_length >= 2 && out->path[0] == '/' && out->path[1] == '/')
        return -1;
    for (size_t i = 0; out->path[i]; i++) {
        unsigned char c = (unsigned char)out->path[i];
        if (c <= 0x20 || c == 0x7f || c == '\\') return -1;
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
    /* Bare CR/LF in the status line would swallow later fields, including
     * Transfer-Encoding / Content-Length, and desynchronize keep-alive. */
    if (memchr(headers, '\n', (size_t)(line_end - headers)) ||
        memchr(headers, '\r', (size_t)(line_end - headers)))
        return -1;
    int is_http_11 = memcmp(headers, "HTTP/1.1 ", 9) == 0;
    int is_http_10 = memcmp(headers, "HTTP/1.0 ", 9) == 0;
    if ((!is_http_11 && !is_http_10) || headers[9] < '0' ||
        headers[9] > '9' || headers[10] < '0' || headers[10] > '9' ||
        headers[11] < '0' || headers[11] > '9' ||
        (line_end > headers + 12 && headers[12] != ' '))
        return -1;
    framing->status_code = (headers[9] - '0') * 100 +
                           (headers[10] - '0') * 10 + headers[11] - '0';
    /* Go statusCodeValid: reject 000-099 so a bogus status cannot fail-open
     * as a final response with a body. */
    if (framing->status_code < 100) return -1;
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
    /* RFC 9112: Transfer-Encoding is not defined for HTTP/1.0. Treating a
     * 1.0 response as chunked desynchronizes intermediaries that read the
     * body as identity (the same hole the request parser already closes). */
    if (is_http_10 && framing->is_chunked) return -1;
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
        strcmp(method, "CONNECT") == 0 ||
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

    int send_content_length =
        body_len > 0 || body != NULL || content_type != NULL ||
        strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 ||
        strcmp(method, "PATCH") == 0;
    if (send_content_length) {
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

static int build_http_stream_request_headers(
    nc_buf_t *req, const char *method, const parsed_url_t *url,
    const char *content_type, int64_t content_length, int keep_alive) {
    if (!req) return -1;
    nc_buf_init(req);
    if (!method || !url || content_length < -1 ||
        !client_valid_token(method, strlen(method)) ||
        strcmp(method, "CONNECT") == 0 ||
        contains_crlf(url->authority) || contains_crlf(url->path) ||
        contains_crlf(content_type))
        return -1;

    if (append_cstr(req, method) != 0 || append_cstr(req, " ") != 0 ||
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

    if (content_length >= 0) {
        char line[64];
        int n = snprintf(line, sizeof(line), "Content-Length: %llu\r\n",
                         (unsigned long long)content_length);
        if (n < 0 || (size_t)n >= sizeof(line) ||
            nc_buf_append(req, line, (size_t)n) != 0)
            goto fail;
    } else if (append_cstr(req, "Transfer-Encoding: chunked\r\n") != 0) {
        goto fail;
    }

    if (append_cstr(req, keep_alive
                            ? "Connection: keep-alive\r\n\r\n"
                            : "Connection: close\r\n\r\n") != 0)
        goto fail;
    return 0;

fail:
    nc_buf_free(req);
    return -1;
}

typedef struct {
    size_t cursor;
    size_t decoded_length;
    size_t trailer_offset;
    size_t auxiliary_length;
    int reading_trailers;
} client_chunk_scan_t;

/* 1 = complete, 0 = incomplete, -1 = malformed or decoded body too large. */
static int scan_chunked_body(const char *source, size_t source_length,
                             size_t body_limit, size_t trailer_limit,
                             client_chunk_scan_t *state,
                             size_t *wire_consumed,
                             size_t *trailer_length) {
    const char *end = source + source_length;
    if (state->cursor > source_length) return -1;
    const char *cursor = source + state->cursor;
    for (;;) {
        if (state->reading_trailers) {
            const char *trailers = source + state->trailer_offset;
            size_t trailer_budget = trailer_limit - state->auxiliary_length;
            const char *line_end = client_find_crlf(cursor, end);
            if (!line_end) {
                if ((size_t)(end - trailers) > trailer_budget + 2U)
                    return -1;
                return 0;
            }
            if (line_end == cursor) {
                if ((size_t)(cursor - trailers) > trailer_budget)
                    return -1;
                *wire_consumed = (size_t)(line_end + 2 - source);
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
            state->cursor = (size_t)(line_end + 2 - source);
            if (state->cursor - state->trailer_offset > trailer_budget + 2U)
                return -1;
            cursor = source + state->cursor;
            continue;
        }

        const char *line_end = client_find_crlf(cursor, end);
        if (!line_end) {
            if ((size_t)(end - cursor) > 8194U) return -1;
            return 0;
        }
        size_t line_length = (size_t)(line_end - cursor);
        if (line_length > 8192U) return -1;
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
        if (digits == 0) return -1;
        if (cursor + digits < line_end) {
            if (cursor[digits] != ';' || cursor + digits + 1 >= line_end ||
                !client_is_tchar((unsigned char)cursor[digits + 1]) ||
                !client_valid_field_value(
                    cursor + digits, (size_t)(line_end - (cursor + digits))))
                return -1;
        }
        size_t auxiliary_increment = line_length - 1U;
        if (state->auxiliary_length > trailer_limit ||
            auxiliary_increment > trailer_limit - state->auxiliary_length)
            return -1;
        cursor = line_end + 2;

        if (chunk_size == 0) {
            state->auxiliary_length += auxiliary_increment;
            state->reading_trailers = 1;
            state->trailer_offset = (size_t)(cursor - source);
            state->cursor = state->trailer_offset;
            continue;
        }

        if (state->decoded_length > body_limit ||
            chunk_size > body_limit - state->decoded_length)
            return -1;
        size_t available = (size_t)(end - cursor);
        if (chunk_size > available || available - chunk_size < 2) return 0;
        if (cursor[chunk_size] != '\r' || cursor[chunk_size + 1] != '\n')
            return -1;
        state->auxiliary_length += auxiliary_increment;
        state->decoded_length += chunk_size;
        cursor += chunk_size + 2;
        state->cursor = (size_t)(cursor - source);
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
    neverc_http_client_t *client, const parsed_url_t *url,
    const char *connect_addr,
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
    if ((client->config.root_cert_file &&
         neverc_tls_config_add_root_certificates(
             tls_config, client->config.root_cert_file) != 0) ||
        (client->config.client_cert_file &&
         neverc_tls_config_load_cert(tls_config,
                                     client->config.client_cert_file,
                                     client->config.client_key_file) != 0)) {
        neverc_tls_config_free(tls_config);
        client_connection_close(connection);
        if (error) *error = "failed to configure HTTP client TLS identity";
        return NULL;
    }
    if (client->config.insecure_skip_verify)
        neverc_tls_config_insecure_skip_verify(tls_config);
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
        conn = client_connection_dial(client, url, connect_addr, context,
                                      &dial_error);
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

    /* If write failed on a pooled connection (stale), retry with a new conn.
     * HTTP and HTTPS both do this: an idle peer may have already closed. */
    if (wr == -1 && from_pool) {
        client_connection_close(conn);
        const char *dial_error = NULL;
        conn = client_connection_dial(client, url, connect_addr, context,
                                      &dial_error);
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
    client_chunk_scan_t chunk_scan;
    memset(&chunk_scan, 0, sizeof(chunk_scan));
    size_t trailer_length = 0;

    const size_t header_limit = client->config.max_response_header_size;
    const size_t body_limit = client->config.max_response_body_size;
    size_t response_limit = client_response_wire_limit(header_limit,
                                                       body_limit);
    char chunk[8192];
    while (!response_complete) {
        size_t read_size = sizeof(chunk);
        if (resp_buf.len >= response_limit) {
            int extra = client_connection_read(conn, context, chunk, 1);
            if (extra == 0 && hdr_end_offset != SIZE_MAX &&
                !framing.is_chunked && !framing.has_content_length) {
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
                if ((((framing.status_code >= 100 &&
                       framing.status_code < 200) ||
                      framing.status_code == 204) &&
                     (framing.has_content_length || framing.is_chunked))) {
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
            /* RFC 9112: TE still frames a body. 304/HEAD+chunked leftover
             * must not return the socket to the idle pool. */
            has_extra_bytes = body_received > 0 || framing.is_chunked;
            response_complete = 1;
        } else if (framing.is_chunked) {
            int scan_result = scan_chunked_body(
                resp_buf.data + hdr_end_offset + 4, body_received,
                body_limit, header_limit, &chunk_scan,
                &chunk_wire_consumed, &trailer_length);
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

        if (!status_has_no_body && framing.is_chunked) {
            char *decoded = NULL;
            if (decode_chunked_body(body_start, chunk_wire_consumed,
                                    chunk_scan.decoded_length, &decoded) != 0) {
                r->error = "invalid or incomplete chunked response";
                server_keepalive = 0;
            } else {
                r->body = decoded;
                r->body_len = chunk_scan.decoded_length;
                if (trailer_length > 0) {
                    r->trailers = (char *)malloc(trailer_length + 1);
                    if (!r->trailers) {
                        r->error = "out of memory";
                        server_keepalive = 0;
                    } else {
                        memcpy(r->trailers,
                               body_start + chunk_scan.trailer_offset,
                               trailer_length);
                        r->trailers[trailer_length] = '\0';
                    }
                }
            }
        } else if (!status_has_no_body) {
            if (framing.has_content_length)
                raw_blen = framing.content_length;
            if (raw_blen > 0) {
                r->body = (char *)malloc(raw_blen + 1);
                if (r->body) {
                    memcpy(r->body, body_start, raw_blen);
                    r->body[raw_blen] = '\0';
                    r->body_len = raw_blen;
                } else {
                    r->error = "out of memory";
                    server_keepalive = 0;
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

static int stream_write_request_body(
    client_connection_t *connection, neverc_context_t *context,
    int64_t content_length, neverc_http_body_source_func_t source,
    void *source_context) {
    if (content_length == 0) return 0;
    if (!source) return -1;

    char data[8192];
    if (content_length > 0) {
        uint64_t remaining = (uint64_t)content_length;
        while (remaining > 0) {
            size_t capacity = sizeof(data);
            if ((uint64_t)capacity > remaining) capacity = (size_t)remaining;
            int produced = source(source_context, data, capacity);
            if (produced <= 0 || (size_t)produced > capacity ||
                client_connection_write_all(connection, context, data,
                                            (size_t)produced) != 0)
                return -1;
            remaining -= (uint64_t)produced;
        }
        return 0;
    }

    for (;;) {
        int produced = source(source_context, data, sizeof(data));
        if (produced < 0 || (size_t)produced > sizeof(data)) return -1;
        if (produced == 0)
            return client_connection_write_all(
                connection, context, "0\r\n\r\n", 5);

        char prefix[32];
        int prefix_length = snprintf(prefix, sizeof(prefix), "%x\r\n",
                                     (unsigned)produced);
        if (prefix_length <= 0 || (size_t)prefix_length >= sizeof(prefix) ||
            client_connection_write_all(connection, context, prefix,
                                        (size_t)prefix_length) != 0 ||
            client_connection_write_all(connection, context, data,
                                        (size_t)produced) != 0 ||
            client_connection_write_all(connection, context, "\r\n", 2) != 0)
            return -1;
    }
}

static int stream_deliver_body(
    neverc_http_response_t *response, size_t body_limit,
    neverc_http_body_sink_func_t sink, void *sink_context,
    const void *data, size_t length) {
    if (length == 0) return 0;
    if (response->body_len > body_limit ||
        length > body_limit - response->body_len)
        return -2;
    if (sink && sink(sink_context, data, length) != 0) return -1;
    response->body_len += length;
    return 0;
}

static int stream_chunk_size(const char *line, size_t length,
                             size_t *chunk_size) {
    size_t value = 0;
    size_t digits = 0;
    while (digits < length) {
        unsigned char c = (unsigned char)line[digits];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else break;
        if (value > (SIZE_MAX - digit) / 16) return -1;
        value = value * 16 + digit;
        digits++;
    }
    if (digits == 0) return -1;
    if (digits < length) {
        if (line[digits] != ';' || digits + 1 >= length ||
            !client_is_tchar((unsigned char)line[digits + 1]) ||
            !client_valid_field_value(line + digits, length - digits))
            return -1;
    }
    *chunk_size = value;
    return 0;
}

/* 1 = complete, 0 = incomplete, -1 = malformed, -2 = out of memory. */
static int stream_parse_trailers(nc_buf_t *wire, size_t trailer_limit,
                                 char **trailers) {
    size_t cursor = 0;
    while (cursor < wire->len) {
        const char *line = wire->data + cursor;
        const char *line_end = client_find_crlf(
            line, wire->data + wire->len);
        if (!line_end) {
            if (wire->len > trailer_limit + 2) return -1;
            return 0;
        }
        size_t line_length = (size_t)(line_end - line);
        if (line_length == 0) {
            if (cursor > trailer_limit) return -1;
            if (cursor > 0) {
                size_t copy_length = cursor;
                char *copy = (char *)malloc(copy_length + 1);
                if (!copy) return -2;
                if (copy_length > 0)
                    memcpy(copy, wire->data, copy_length);
                copy[copy_length] = '\0';
                *trailers = copy;
            }
            nc_buf_consume(wire, cursor + 2);
            return 1;
        }
        if (*line == ' ' || *line == '\t') return -1;
        const char *colon = (const char *)memchr(line, ':', line_length);
        if (!colon || !client_valid_token(line, (size_t)(colon - line)))
            return -1;
        const char *value = colon + 1;
        size_t value_length = (size_t)(line_end - value);
        client_trim_ows(&value, &value_length);
        size_t name_length = (size_t)(colon - line);
        if (!client_valid_field_value(value, value_length) ||
            client_name_is(line, name_length, "Content-Length") ||
            client_name_is(line, name_length, "Transfer-Encoding") ||
            client_name_is(line, name_length, "Host"))
            return -1;
        cursor = (size_t)(line_end + 2 - wire->data);
        if (cursor > trailer_limit + 2) return -1;
    }
    return 0;
}

static int stream_read_chunked_response(
    client_connection_t *connection, neverc_context_t *context,
    nc_buf_t *wire, neverc_http_response_t *response,
    size_t header_limit, size_t body_limit,
    neverc_http_body_sink_func_t sink, void *sink_context) {
    enum { STREAM_CHUNK_SIZE, STREAM_CHUNK_DATA,
           STREAM_CHUNK_DATA_CRLF, STREAM_CHUNK_TRAILERS } state =
        STREAM_CHUNK_SIZE;
    size_t remaining = 0;
    size_t auxiliary_length = 0;
    char input[8192];

    for (;;) {
        int made_progress = 0;
        if (state == STREAM_CHUNK_SIZE) {
            const char *line_end = wire->len >= 2
                ? client_find_crlf(wire->data, wire->data + wire->len)
                : NULL;
            if (line_end) {
                size_t line_length = (size_t)(line_end - wire->data);
                if (line_length > 8192 ||
                    stream_chunk_size(wire->data, line_length,
                                      &remaining) != 0)
                    return -1;
                size_t auxiliary_increment = line_length - 1U;
                if (auxiliary_length > header_limit ||
                    auxiliary_increment > header_limit - auxiliary_length)
                    return -1;
                auxiliary_length += auxiliary_increment;
                nc_buf_consume(wire, line_length + 2);
                state = remaining == 0 ? STREAM_CHUNK_TRAILERS
                                       : STREAM_CHUNK_DATA;
                made_progress = 1;
            } else if (wire->len > 8194) {
                return -1;
            }
        } else if (state == STREAM_CHUNK_DATA) {
            size_t available = wire->len;
            if (available > remaining) available = remaining;
            if (available > 0) {
                int delivered = stream_deliver_body(
                    response, body_limit, sink, sink_context,
                    wire->data, available);
                if (delivered != 0) return delivered == -2 ? -2 : -3;
                nc_buf_consume(wire, available);
                remaining -= available;
                made_progress = 1;
            }
            if (remaining == 0) state = STREAM_CHUNK_DATA_CRLF;
        } else if (state == STREAM_CHUNK_DATA_CRLF) {
            if (wire->len >= 2) {
                if (wire->data[0] != '\r' || wire->data[1] != '\n')
                    return -1;
                nc_buf_consume(wire, 2);
                state = STREAM_CHUNK_SIZE;
                made_progress = 1;
            }
        } else {
            int trailer_result = stream_parse_trailers(
                wire, header_limit - auxiliary_length,
                &response->trailers);
            if (trailer_result == -2) return -4;
            if (trailer_result < 0) return -1;
            if (trailer_result > 0) return 0;
        }

        if (made_progress) continue;
        int received = client_connection_read(
            connection, context, input, sizeof(input));
        if (received <= 0) return -1;
        if (nc_buf_append(wire, input, (size_t)received) != 0) return -4;
    }
}

static neverc_http_response_t *stream_response_error(
    client_connection_t *connection, nc_buf_t *wire,
    neverc_http_response_t *response, const char *error) {
    client_connection_close(connection);
    nc_buf_free(wire);
    if (!response) return make_error_response(error);
    response->error = error;
    return response;
}

static neverc_http_response_t *do_stream_request(
    neverc_http_client_t *client, neverc_context_t *context,
    const char *method, const parsed_url_t *url, const char *content_type,
    int64_t content_length, neverc_http_body_source_func_t source,
    void *source_context, neverc_http_body_sink_func_t sink,
    void *sink_context) {
    if (content_length < -1 || (content_length != 0 && !source))
        return make_error_response("invalid streaming request body");
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

    int use_keepalive =
        nc_atomic_load(&client->pool.max_idle_per_host) > 0;
    client_connection_t *connection = pool_get(client, pool_key);
    if (!connection) {
        const char *dial_error = NULL;
        connection = client_connection_dial(
            client, url, connect_addr, context, &dial_error);
        if (!connection) return make_error_response(dial_error);
    }

    nc_buf_t request_headers;
    if (build_http_stream_request_headers(
            &request_headers, method, url, content_type, content_length,
            use_keepalive) != 0) {
        client_connection_close(connection);
        return make_error_response("invalid request or out of memory");
    }
    int write_result = client_connection_write_all(
        connection, context, request_headers.data, request_headers.len);
    nc_buf_free(&request_headers);
    if (write_result != 0 || stream_write_request_body(
            connection, context, content_length, source,
            source_context) != 0) {
        client_connection_close(connection);
        return make_error_response(client_context_error(
            context, "streaming request write failed"));
    }

    nc_buf_t wire;
    nc_buf_init(&wire);
    response_framing_t framing;
    memset(&framing, 0, sizeof(framing));
    int interim_count = 0;
    size_t final_header_length = 0;
    char input[8192];
    for (;;) {
        char *header_end = wire.data ? strstr(wire.data, "\r\n\r\n") : NULL;
        if (!header_end) {
            if (wire.len > client->config.max_response_header_size + 4)
                return stream_response_error(
                    connection, &wire, NULL,
                    "response headers exceed configured limit");
            int received = client_connection_read(
                connection, context, input, sizeof(input));
            if (received <= 0 ||
                nc_buf_append(&wire, input, (size_t)received) != 0)
                return stream_response_error(
                    connection, &wire, NULL,
                    received < 0 ? client_context_error(
                        context, "response header read failed")
                                 : "incomplete HTTP response headers");
            continue;
        }
        final_header_length = (size_t)(header_end - wire.data);
        if (final_header_length > client->config.max_response_header_size ||
            parse_response_framing(wire.data, final_header_length,
                                   &framing) != 0)
            return stream_response_error(
                connection, &wire, NULL, "invalid HTTP response framing");
        if ((((framing.status_code >= 100 && framing.status_code < 200) ||
              framing.status_code == 204) &&
             (framing.has_content_length || framing.is_chunked)))
            return stream_response_error(
                connection, &wire, NULL,
                "body framing is forbidden for this status");
        if (framing.status_code >= 100 && framing.status_code < 200 &&
            framing.status_code != 101) {
            if (++interim_count > 16)
                return stream_response_error(
                    connection, &wire, NULL, "too many interim responses");
            nc_buf_consume(&wire, final_header_length + 4);
            memset(&framing, 0, sizeof(framing));
            continue;
        }
        break;
    }

    neverc_http_response_t *response =
        (neverc_http_response_t *)calloc(1, sizeof(*response));
    if (!response)
        return stream_response_error(
            connection, &wire, NULL, "out of memory");
    response->status_code = framing.status_code;
    response->headers = (char *)malloc(final_header_length + 1);
    if (!response->headers)
        return stream_response_error(
            connection, &wire, response, "out of memory");
    memcpy(response->headers, wire.data, final_header_length);
    response->headers[final_header_length] = '\0';
    nc_buf_consume(&wire, final_header_length + 4);

    int is_head = strcmp(method, "HEAD") == 0;
    int status_has_no_body = is_head ||
        (framing.status_code >= 100 && framing.status_code < 200) ||
        framing.status_code == 204 || framing.status_code == 304;
    int keepalive = framing.keep_alive;
    if (status_has_no_body) {
        if (framing.status_code == 101) keepalive = 0;
        if (wire.len > 0 || framing.is_chunked) keepalive = 0;
    } else if (framing.is_chunked) {
        int chunk_result = stream_read_chunked_response(
            connection, context, &wire, response,
            client->config.max_response_header_size,
            client->config.max_response_body_size,
            sink, sink_context);
        if (chunk_result != 0)
            return stream_response_error(
                connection, &wire, response,
                chunk_result == -2 ? "response body exceeds configured limit"
                : chunk_result == -3 ? "response body sink failed"
                : chunk_result == -4 ? "out of memory"
                                     : "invalid or incomplete chunked response");
        if (wire.len > 0) keepalive = 0;
    } else if (framing.has_content_length) {
        if (framing.content_length > client->config.max_response_body_size)
            return stream_response_error(
                connection, &wire, response,
                "response body exceeds configured limit");
        size_t remaining = framing.content_length;
        if (wire.len > remaining)
            return stream_response_error(
                connection, &wire, response,
                "response contains bytes beyond Content-Length");
        if (wire.len > 0) {
            int delivered = stream_deliver_body(
                response, client->config.max_response_body_size,
                sink, sink_context, wire.data, wire.len);
            if (delivered != 0)
                return stream_response_error(
                    connection, &wire, response,
                    delivered == -2
                        ? "response body exceeds configured limit"
                        : "response body sink failed");
            remaining -= wire.len;
            nc_buf_reset(&wire);
        }
        while (remaining > 0) {
            size_t capacity = sizeof(input);
            if (capacity > remaining) capacity = remaining;
            int received = client_connection_read(
                connection, context, input, capacity);
            if (received <= 0)
                return stream_response_error(
                    connection, &wire, response,
                    "incomplete Content-Length response");
            int delivered = stream_deliver_body(
                response, client->config.max_response_body_size,
                sink, sink_context, input, (size_t)received);
            if (delivered != 0)
                return stream_response_error(
                    connection, &wire, response,
                    delivered == -2
                        ? "response body exceeds configured limit"
                        : "response body sink failed");
            remaining -= (size_t)received;
        }
    } else {
        keepalive = 0;
        if (wire.len > 0) {
            int delivered = stream_deliver_body(
                response, client->config.max_response_body_size,
                sink, sink_context, wire.data, wire.len);
            if (delivered != 0)
                return stream_response_error(
                    connection, &wire, response,
                    delivered == -2
                        ? "response body exceeds configured limit"
                        : "response body sink failed");
            nc_buf_reset(&wire);
        }
        for (;;) {
            int received = client_connection_read(
                connection, context, input, sizeof(input));
            if (received == 0) break;
            if (received < 0)
                return stream_response_error(
                    connection, &wire, response,
                    client_context_error(context, "response body read failed"));
            int delivered = stream_deliver_body(
                response, client->config.max_response_body_size,
                sink, sink_context, input, (size_t)received);
            if (delivered != 0)
                return stream_response_error(
                    connection, &wire, response,
                    delivered == -2
                        ? "response body exceeds configured limit"
                        : "response body sink failed");
        }
    }

    nc_buf_free(&wire);
    if (use_keepalive && keepalive)
        pool_put(client, pool_key, connection);
    else
        client_connection_close(connection);
    return response;
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

static int copy_response_header_value(const char *headers, size_t hdr_len,
                                      const char *name, char **value) {
    if (!headers || !name || !value) return -1;
    *value = NULL;
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
            while (v < nl && (*v == ' ' || *v == '\t')) v++;
            size_t vlen = (size_t)(nl - v);
            while (vlen > 0 && (v[vlen - 1] == ' ' ||
                                v[vlen - 1] == '\t'))
                vlen--;
            char *val = (char *)malloc(vlen + 1);
            if (!val) return -1;
            memcpy(val, v, vlen);
            val[vlen] = '\0';
            *value = val;
            return 1;
        }
        p = nl + 2;
    }
    return 0;
}

static int redirect_status(int status) {
    return status == 301 || status == 302 || status == 303 ||
        status == 307 || status == 308;
}

static int redirect_prefix(const char *input, size_t length,
                           const char *prefix) {
    size_t prefix_length = strlen(prefix);
    return length >= prefix_length &&
        memcmp(input, prefix, prefix_length) == 0;
}

static void redirect_remove_last_segment(char *output, size_t *length) {
    while (*length > 0 && output[*length - 1] != '/') (*length)--;
    if (*length > 0) (*length)--;
}

/* RFC 3986 section 5.2.4 dot-segment removal for an absolute path. */
static int normalize_redirect_target(const char *target, char *output,
                                     size_t capacity) {
    const char *query = strchr(target, '?');
    size_t path_length = query ? (size_t)(query - target) : strlen(target);
    size_t input = 0;
    size_t written = 0;

    while (input < path_length) {
        size_t remaining = path_length - input;
        const char *current = target + input;
        if (redirect_prefix(current, remaining, "../")) {
            input += 3;
        } else if (redirect_prefix(current, remaining, "./")) {
            input += 2;
        } else if (redirect_prefix(current, remaining, "/./")) {
            input += 2;
        } else if (remaining == 2 && memcmp(current, "/.", 2) == 0) {
            if (written == 0 || output[written - 1] != '/') {
                if (written >= capacity - 1) return -1;
                output[written++] = '/';
            }
            input = path_length;
        } else if (redirect_prefix(current, remaining, "/../")) {
            input += 3;
            redirect_remove_last_segment(output, &written);
        } else if (remaining == 3 && memcmp(current, "/..", 3) == 0) {
            redirect_remove_last_segment(output, &written);
            if (written == 0 || output[written - 1] != '/') {
                if (written >= capacity - 1) return -1;
                output[written++] = '/';
            }
            input = path_length;
        } else if ((remaining == 1 && current[0] == '.') ||
                   (remaining == 2 && memcmp(current, "..", 2) == 0)) {
            input = path_length;
        } else {
            size_t end = input;
            if (target[end] == '/') end++;
            while (end < path_length && target[end] != '/') end++;
            size_t segment_length = end - input;
            if (segment_length >= capacity - written) return -1;
            memcpy(output + written, target + input, segment_length);
            written += segment_length;
            input = end;
        }
    }

    if (written == 0) {
        if (capacity < 2) return -1;
        output[written++] = '/';
    }
    if (query) {
        size_t query_length = strlen(query);
        if (query_length >= capacity - written) return -1;
        memcpy(output + written, query, query_length);
        written += query_length;
    }
    output[written] = '\0';
    return 0;
}

static int resolve_redirect_url(const parsed_url_t *base,
                                const char *location,
                                char *output, size_t capacity) {
    if (!base || !location || !output || capacity == 0) return -1;

    size_t reference_length = strlen(location);
    const char *fragment = strchr(location, '#');
    if (fragment) reference_length = (size_t)(fragment - location);
    if (reference_length >= capacity) return -1;

    char reference[4096];
    if (reference_length >= sizeof(reference)) return -1;
    memcpy(reference, location, reference_length);
    reference[reference_length] = '\0';

    const char *scheme = base->is_https ? "https" : "http";
    if (strncasecmp(reference, "http://", 7) == 0 ||
        strncasecmp(reference, "https://", 8) == 0) {
        memcpy(output, reference, reference_length + 1);
        return 0;
    }
    const char *reference_colon = strchr(reference, ':');
    const char *reference_delimiter = strpbrk(reference, "/?");
    if (reference_colon &&
        (!reference_delimiter || reference_colon < reference_delimiter))
        return -1;
    if (reference[0] == '/' && reference[1] == '/') {
        int length = snprintf(output, capacity, "%s:%s", scheme, reference);
        return length < 0 || (size_t)length >= capacity ? -1 : 0;
    }

    char target[4096];
    if (reference[0] == '\0') {
        size_t base_length = strlen(base->path);
        if (base_length >= sizeof(target)) return -1;
        memcpy(target, base->path, base_length + 1);
    } else if (reference[0] == '?') {
        const char *base_query = strchr(base->path, '?');
        size_t base_path_length = base_query
            ? (size_t)(base_query - base->path) : strlen(base->path);
        if (base_path_length + reference_length >= sizeof(target)) return -1;
        memcpy(target, base->path, base_path_length);
        memcpy(target + base_path_length, reference, reference_length + 1);
    } else if (reference[0] == '/') {
        memcpy(target, reference, reference_length + 1);
    } else {
        const char *base_query = strchr(base->path, '?');
        size_t base_path_length = base_query
            ? (size_t)(base_query - base->path) : strlen(base->path);
        size_t directory_length = base_path_length;
        while (directory_length > 0 &&
               base->path[directory_length - 1] != '/')
            directory_length--;
        if (directory_length + reference_length >= sizeof(target)) return -1;
        memcpy(target, base->path, directory_length);
        memcpy(target + directory_length, reference, reference_length + 1);
    }

    char normalized[4096];
    if (normalize_redirect_target(target, normalized, sizeof(normalized)) != 0)
        return -1;
    int length = snprintf(output, capacity, "%s://%s%s",
                          scheme, base->authority, normalized);
    return length < 0 || (size_t)length >= capacity ? -1 : 0;
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
    const char *current_content_type = content_type;
    const void *current_body = body;
    size_t current_body_len = body_len;

    int last_status = 0;
    int max_redirects = nc_atomic_load(&client->config.max_redirects);
    for (int redirects = 0; ; redirects++) {
        parsed_url_t pu;
        if (parse_http_url(current_url, &pu) != 0)
            return make_error_response("invalid url");

        /* 303 changes every method except HEAD to GET. For compatibility,
         * 301/302 change POST to GET; 307/308 preserve method and body. */
        if (redirects > 0 &&
            ((last_status == 303 && strcmp(current_method, "HEAD") != 0) ||
             ((last_status == 301 || last_status == 302) &&
              strcmp(current_method, "POST") == 0))) {
            current_method = "GET";
            current_content_type = NULL;
            current_body = NULL;
            current_body_len = 0;
        }
        neverc_http_response_t *resp = do_request(
            client, context, current_method, &pu, current_content_type,
            current_body, current_body_len);

        if (!resp || resp->error) return resp;

        if (redirect_status(resp->status_code) &&
            max_redirects > 0 && resp->headers) {
            char *location = NULL;
            int location_result = copy_response_header_value(
                resp->headers, strlen(resp->headers), "Location", &location);
            if (location_result < 0) {
                neverc_http_response_free(resp);
                return make_error_response("out of memory");
            }
            if (location_result > 0) {
                if (redirects >= max_redirects) {
                    free(location);
                    neverc_http_response_free(resp);
                    return make_error_response("too many redirects");
                }
                last_status = resp->status_code;
                if (resolve_redirect_url(
                        &pu, location, current_url, sizeof(current_url)) != 0) {
                    free(location);
                    neverc_http_response_free(resp);
                    return make_error_response("invalid redirect url");
                }
                free(location);
                neverc_http_response_free(resp);
                continue;
            }
        }
        return resp;
    }
}

static neverc_http_response_t *execute_client_request(
    neverc_http_client_t *client, neverc_context_t *parent,
    const char *method, const char *url, const char *content_type,
    const void *body, size_t body_len) {
    neverc_context_t *owned_background = NULL;
    if (!parent) {
        parent = owned_background = neverc_context_background();
        if (!parent)
            return make_error_response("out of memory");
    }
    neverc_context_cancel_handle_t *cancel_handle = NULL;
    neverc_context_t *context = neverc_context_with_timeout_handle(
        parent, nc_atomic_load(&client->config.timeout_ms), &cancel_handle);
    if (!context) {
        neverc_context_free(owned_background);
        return make_error_response("out of memory");
    }
    neverc_http_response_t *response = do_request_with_redirects(
        client, context, method, url, content_type, body, body_len);
    neverc_context_cancel_handle_cancel(cancel_handle);
    neverc_context_cancel_handle_free(cancel_handle);
    neverc_context_free(context);
    neverc_context_free(owned_background);
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

neverc_http_response_t *neverc_http_client_do_stream_context(
    neverc_http_client_t *client, neverc_context_t *parent,
    const char *method, const char *url, const char *content_type,
    int64_t content_length, neverc_http_body_source_func_t source,
    void *source_context, neverc_http_body_sink_func_t sink,
    void *sink_context) {
    if (!client) return make_error_response("null client");
    if (!parent) return make_error_response("null context");
    if (!method) return make_error_response("null method");
    if (!url) return make_error_response("null url");

    parsed_url_t parsed;
    if (parse_http_url(url, &parsed) != 0)
        return make_error_response("invalid url");
    neverc_context_cancel_handle_t *cancel_handle = NULL;
    neverc_context_t *context = neverc_context_with_timeout_handle(
        parent, nc_atomic_load(&client->config.timeout_ms), &cancel_handle);
    if (!context || !cancel_handle) {
        if (context) neverc_context_free(context);
        if (cancel_handle)
            neverc_context_cancel_handle_free(cancel_handle);
        return make_error_response("out of memory");
    }
    neverc_http_response_t *response = do_stream_request(
        client, context, method, &parsed, content_type, content_length,
        source, source_context, sink, sink_context);
    neverc_context_cancel_handle_cancel(cancel_handle);
    neverc_context_cancel_handle_free(cancel_handle);
    neverc_context_free(context);
    return response;
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

static int http_cookie_name_ok(const char *name) {
    static const char separators[] = "()<>@,;:\\\"/[]?={} \t";
    if (!name || !name[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p <= 0x20 || *p >= 0x7f || strchr(separators, *p))
            return 0;
    }
    return 1;
}

static int http_cookie_value_ok(const char *value) {
    if (!value) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p != 0x21 && !(*p >= 0x23 && *p <= 0x2b) &&
            !(*p >= 0x2d && *p <= 0x3a) &&
            !(*p >= 0x3c && *p <= 0x5b) &&
            !(*p >= 0x5d && *p <= 0x7e))
            return 0;
    }
    return 1;
}

static int http_cookie_path_ok(const char *path) {
    if (!path || path[0] != '/') return 0;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        if (*p < 0x20 || *p == 0x7f || *p == ';') return 0;
    }
    return 1;
}

static int http_cookie_domain_ok(const char *domain) {
    if (!domain || !domain[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)domain; *p; p++) {
        if (*p <= 0x20 || *p >= 0x7f || *p == '/' || *p == '\\' ||
            *p == ':' || *p == ';')
            return 0;
    }
    return 1;
}

void neverc_http_set_cookie(neverc_http_response_writer_t *w,
                              const neverc_http_cookie_t *c) {
    if (!w || !c || !c->name || !c->value ||
        !http_cookie_name_ok(c->name) || !http_cookie_value_ok(c->value))
        return;
    if (c->path && c->path[0] && !http_cookie_path_ok(c->path)) return;
    if (c->domain && c->domain[0] && !http_cookie_domain_ok(c->domain))
        return;

    nc_buf_t value;
    nc_buf_init(&value);
    int failed = nc_buf_append(&value, c->name, strlen(c->name)) != 0 ||
                 nc_buf_append(&value, "=", 1) != 0 ||
                 nc_buf_append(&value, c->value, strlen(c->value)) != 0;
    if (!failed && c->path && c->path[0])
        failed = nc_buf_append(&value, "; Path=", 7) != 0 ||
                 nc_buf_append(&value, c->path, strlen(c->path)) != 0;
    if (!failed && c->domain && c->domain[0])
        failed = nc_buf_append(&value, "; Domain=", 9) != 0 ||
                 nc_buf_append(&value, c->domain, strlen(c->domain)) != 0;
    if (!failed && c->max_age != 0) {
        char max_age[32];
        int length = snprintf(max_age, sizeof(max_age), "; Max-Age=%d",
                              c->max_age);
        failed = length < 0 || (size_t)length >= sizeof(max_age) ||
                 nc_buf_append(&value, max_age, (size_t)length) != 0;
    }
    if (!failed && c->secure)
        failed = nc_buf_append(&value, "; Secure", 8) != 0;
    if (!failed && c->http_only)
        failed = nc_buf_append(&value, "; HttpOnly", 10) != 0;
    if (!failed && c->same_site == 1)
        failed = nc_buf_append(&value, "; SameSite=Lax", 14) != 0;
    else if (!failed && c->same_site == 2)
        failed = nc_buf_append(&value, "; SameSite=Strict", 17) != 0;
    else if (!failed && c->same_site == 3)
        failed = nc_buf_append(&value, "; SameSite=None", 15) != 0;

    if (failed ||
        nc_http_writer_add_header(w, "Set-Cookie", value.data) != 0)
        w->aborted = 1;
    nc_buf_free(&value);
}

/* Go validCookieValueByte: SP/comma allowed; DQUOTE, ';', '\\', CTL, DEL not. */
static int http_cookie_value_byte_ok(unsigned char c) {
    return c >= 0x20 && c < 0x7f && c != '"' && c != ';' && c != '\\';
}

static const char *http_cookie_lookup(const char *cookie_hdr, const char *name,
                                      char *buf, size_t buflen) {
    size_t nlen = strlen(name);
    const char *p = cookie_hdr;

    while (*p) {
        while (*p == ' ' || *p == ';') p++;
        if (!*p) break;

        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            const char *val = p + nlen + 1;
            size_t raw_len = 0;
            while (val[raw_len] && val[raw_len] != ';')
                raw_len++;
            /* Go parseCookieValue: unwrap a matching DQUOTE pair, then
             * reject leftover '"' / '\\' / CTL. Truncating into buf used
             * to return a prefix of `"secret"` as a successful value. */
            const char *inner = val;
            size_t inner_len = raw_len;
            if (raw_len >= 2 && val[0] == '"' && val[raw_len - 1] == '"') {
                inner = val + 1;
                inner_len = raw_len - 2;
            }
            int valid = inner_len < buflen;
            for (size_t i = 0; valid && i < inner_len; i++)
                valid = http_cookie_value_byte_ok((unsigned char)inner[i]);
            if (valid) {
                if (inner_len > 0)
                    memcpy(buf, inner, inner_len);
                buf[inner_len] = '\0';
                return buf;
            }
        }

        while (*p && *p != ';') p++;
    }
    return NULL;
}

const char *neverc_http_get_cookie(const neverc_http_request_t *req,
                                     const char *name,
                                     char *buf, size_t buflen) {
    if (!req || !req->raw_headers || !name || !name[0] || !buf || buflen == 0)
        return NULL;

    /* Cookie is a well-known exception to combined-header rules: walk every
     * Cookie field rather than stopping at the first. */
    const char *p = req->raw_headers;
    for (int i = 0; i < req->nheaders; i++) {
        const char *hname = p;
        while (*p) p++;
        p++;
        const char *hval = p;
        while (*p) p++;
        p++;
        if (strcasecmp(hname, "Cookie") != 0) continue;
        const char *found = http_cookie_lookup(hval, name, buf, buflen);
        if (found) return found;
    }
    return NULL;
}

/* ======================================================================
 * Server-Sent Events (SSE)
 * ====================================================================== */

int neverc_http_sse_begin(neverc_http_response_writer_t *w) {
    if (!w || w->headers_sent) return -1;
    w->keep_alive = 0;
    neverc_http_set_status(w, 200);
    neverc_http_set_header(w, "Content-Type", "text/event-stream");
    neverc_http_set_header(w, "Cache-Control", "no-cache");
    neverc_http_set_header(w, "X-Accel-Buffering", "no");
    neverc_http_enable_chunked(w);
    return neverc_http_flush_chunk(w);
}

int neverc_http_sse_event(neverc_http_response_writer_t *w,
                            const char *event, const char *data,
                            const char *id) {
    if (!w || !data || !w->chunked || w->chunked_ended ||
        contains_crlf(event) || contains_crlf(id))
        return -1;

    nc_buf_t buf;
    nc_buf_init(&buf);

    if (id) {
        if (nc_buf_append(&buf, "id: ", 4) != 0 ||
            nc_buf_append(&buf, id, strlen(id)) != 0 ||
            nc_buf_append(&buf, "\n", 1) != 0)
            goto fail;
    }
    if (event) {
        if (nc_buf_append(&buf, "event: ", 7) != 0 ||
            nc_buf_append(&buf, event, strlen(event)) != 0 ||
            nc_buf_append(&buf, "\n", 1) != 0)
            goto fail;
    }

    const char *p = data;
    for (;;) {
        const char *line_end = p;
        while (*line_end && *line_end != '\r' && *line_end != '\n')
            line_end++;
        if (nc_buf_append(&buf, "data: ", 6) != 0 ||
            nc_buf_append(&buf, p, (size_t)(line_end - p)) != 0 ||
            nc_buf_append(&buf, "\n", 1) != 0)
            goto fail;
        if (*line_end == '\0') break;
        if (*line_end == '\r' && line_end[1] == '\n') line_end++;
        p = line_end + 1;
    }
    if (nc_buf_append(&buf, "\n", 1) != 0)
        goto fail;

    int rc = neverc_http_write(w, buf.data, buf.len) < 0 ? -1 : 0;
    nc_buf_free(&buf);
    return rc;

fail:
    nc_buf_free(&buf);
    return -1;
}

int neverc_http_sse_retry(neverc_http_response_writer_t *w, int ms) {
    if (!w || !w->chunked || w->chunked_ended || ms < 0) return -1;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "retry: %d\n\n", ms);
    if (n < 0 || (size_t)n >= sizeof(buf)) return -1;
    return neverc_http_write(w, buf, (size_t)n) < 0 ? -1 : 0;
}

void neverc_http_sse_end(neverc_http_response_writer_t *w) {
    if (!w) return;
    if (w->chunked && !w->chunked_ended)
        (void)neverc_http_end_chunked(w);
    w->keep_alive = 0;
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

static char *multipart_quoted_parameter(const char *begin, const char *end,
                                        const char *wanted,
                                        size_t wanted_length,
                                        int *allocation_failed) {
    const char *cursor = begin;
    while (cursor < end) {
        const char *semicolon =
            (const char *)memchr(cursor, ';', (size_t)(end - cursor));
        if (!semicolon) return NULL;
        cursor = semicolon + 1;
        while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
            cursor++;

        const char *name = cursor;
        while (cursor < end &&
               ((*cursor >= 'A' && *cursor <= 'Z') ||
                (*cursor >= 'a' && *cursor <= 'z') ||
                (*cursor >= '0' && *cursor <= '9') ||
                *cursor == '-' || *cursor == '_'))
            cursor++;
        size_t name_length = (size_t)(cursor - name);
        while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
            cursor++;
        if (cursor >= end || *cursor != '=') continue;
        cursor++;
        while (cursor < end && (*cursor == ' ' || *cursor == '\t'))
            cursor++;
        int matches = name_length == wanted_length &&
                      strncasecmp(name, wanted, wanted_length) == 0;
        if (cursor >= end) return NULL;
        if (*cursor != '"') {
            const char *value = cursor;
            while (cursor < end && *cursor != ';') cursor++;
            const char *value_end = cursor;
            while (value_end > value &&
                   (value_end[-1] == ' ' || value_end[-1] == '\t'))
                value_end--;
            if (!matches) continue;
            char *result =
                strndup_safe(value, (size_t)(value_end - value));
            if (!result) *allocation_failed = 1;
            return result;
        }
        cursor++;
        const char *value = cursor;
        while (cursor < end) {
            if (*cursor == '\\' && (size_t)(end - cursor) >= 2) {
                cursor += 2;
                continue;
            }
            if (*cursor == '"') break;
            cursor++;
        }
        if (cursor == end) return NULL;
        const char *quote = cursor;
        if (!matches) {
            cursor = quote + 1;
            continue;
        }

        size_t raw_length = (size_t)(quote - value);
        char *result = (char *)malloc(raw_length + 1);
        if (!result) *allocation_failed = 1;
        if (!result) return NULL;
        size_t written = 0;
        for (size_t i = 0; i < raw_length; i++) {
            if (value[i] == '\\' && i + 1 < raw_length) i++;
            result[written++] = value[i];
        }
        result[written] = '\0';
        return result;
    }
    return NULL;
}

static int parse_part_headers(const char *hdr, size_t hdrlen,
                              neverc_http_multipart_part_t *part) {
    const char *end = hdr + hdrlen;
    const char *p = hdr;
    while (p < end) {
        const char *line_end = NULL;
        for (const char *q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') { line_end = q; break; }
        }
        if (!line_end) break;
        if (line_end == p) break;

        size_t llen = (size_t)(line_end - p);
        if (llen > 20 && strncasecmp(p, "Content-Disposition:", 20) == 0) {
            const char *cd = p + 20;
            while (cd < line_end && *cd == ' ') cd++;
            int failed = 0;
            char *name = multipart_quoted_parameter(
                cd, line_end, "name", 4, &failed);
            if (failed) return -1;
            char *filename = multipart_quoted_parameter(
                cd, line_end, "filename", 8, &failed);
            if (failed) {
                free(name);
                return -1;
            }
            free((void *)part->name);
            free((void *)part->filename);
            part->name = name;
            part->filename = filename;
        }
        if (llen > 13 && strncasecmp(p, "Content-Type:", 13) == 0) {
            const char *ct = p + 13;
            while (ct < line_end && *ct == ' ') ct++;
            char *s = strndup_safe(ct, (size_t)(line_end - ct));
            if (!s) return -1;
            free((void *)part->content_type);
            part->content_type = s;
        }
        p = line_end + 2;
    }
    return 0;
}

static int multipart_boundary_tail(const char *after, const char *end,
                                   int *closing, const char **next) {
    size_t remaining = (size_t)(end - after);
    if (remaining >= 2 && after[0] == '-' && after[1] == '-') {
        const char *cursor = after + 2;
        *closing = 1;
        if (cursor == end) {
            *next = cursor;
            return 1;
        }
        if ((size_t)(end - cursor) >= 2 &&
            cursor[0] == '\r' && cursor[1] == '\n') {
            *next = cursor + 2;
            return 1;
        }
        return 0;
    }
    if (remaining >= 2 && after[0] == '\r' && after[1] == '\n') {
        *closing = 0;
        *next = after + 2;
        return 1;
    }
    return 0;
}

static const char *multipart_find_first_boundary(
    const char *body, const char *end, const char *delimiter,
    size_t delimiter_length, int *closing, const char **next) {
    size_t body_length = (size_t)(end - body);
    if (body_length < delimiter_length) return NULL;
    size_t last = body_length - delimiter_length;
    for (size_t offset = 0; offset <= last; offset++) {
        if (offset != 0 &&
            (offset < 2 || body[offset - 2] != '\r' ||
             body[offset - 1] != '\n'))
            continue;
        const char *candidate = body + offset;
        if (memcmp(candidate, delimiter, delimiter_length) == 0 &&
            multipart_boundary_tail(candidate + delimiter_length, end,
                                    closing, next))
            return candidate;
    }
    return NULL;
}

static const char *multipart_find_next_boundary(
    const char *begin, const char *end, const char *delimiter,
    size_t delimiter_length, int *closing, const char **next) {
    size_t available = (size_t)(end - begin);
    if (delimiter_length > SIZE_MAX - 2) return NULL;
    size_t marker_length = delimiter_length + 2;
    if (available < marker_length) return NULL;
    size_t last = available - marker_length;
    for (size_t offset = 0; offset <= last; offset++) {
        const char *candidate = begin + offset;
        if (candidate[0] == '\r' && candidate[1] == '\n' &&
            memcmp(candidate + 2, delimiter, delimiter_length) == 0 &&
            multipart_boundary_tail(candidate + marker_length, end,
                                    closing, next))
            return candidate;
    }
    return NULL;
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
    const char *pos = NULL;
    int closing = 0;
    const char *first = multipart_find_first_boundary(
        body, end, delim, dlen, &closing, &pos);
    if (!first) goto fail;
    if (closing) return mp;

    int closed = 0;
    while (pos < end && mp->nparts < MULTIPART_MAX_PARTS) {
        const char *after_boundary = NULL;
        const char *next_bound = multipart_find_next_boundary(
            pos, end, delim, dlen, &closing, &after_boundary);
        if (!next_bound) goto fail;

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
        mp->nparts++;
        if (hdr_end) {
            if (parse_part_headers(
                    part_data, (size_t)(hdr_end + 2 - part_data), p) != 0)
                goto fail;
            p->data = hdr_end + 4;
            p->data_len = part_len - (size_t)(hdr_end + 4 - part_data);
        } else if (part_len >= 2 &&
                   part_data[0] == '\r' && part_data[1] == '\n') {
            p->data = part_data + 2;
            p->data_len = part_len - 2;
        } else {
            p->data = part_data;
            p->data_len = part_len;
        }

        pos = after_boundary;
        if (closing) {
            closed = 1;
            break;
        }
    }

    if (!closed) goto fail;
    return mp;

fail:
    neverc_http_multipart_free(mp);
    return NULL;
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

static void strip_prefix_context_free(void *context) {
    strip_prefix_ctx_t *ctx = (strip_prefix_ctx_t *)context;
    if (!ctx) return;
    free(ctx->prefix);
    free(ctx);
}

static int http_path_contains_dotdot(const char *path);

static void strip_prefix_handler_fn(neverc_http_request_t *req,
                                    neverc_http_response_writer_t *w,
                                    void *context) {
    strip_prefix_ctx_t *ctx = (strip_prefix_ctx_t *)context;
    if (!req->path ||
        strncmp(req->path, ctx->prefix, ctx->prefix_len) != 0)
        goto not_found;

    const char *rest = req->path + ctx->prefix_len;
    /* "/api" must not match "/apifoo": the cut has to fall on a slash
     * or at end-of-path, unless the prefix itself ended with '/'. */
    if (rest[0] != '\0' && rest[0] != '/' &&
        (ctx->prefix_len == 0 ||
         ctx->prefix[ctx->prefix_len - 1] != '/'))
        goto not_found;

    char *owned = NULL;
    neverc_http_request_t stripped = *req;
    if (rest[0] == '\0') {
        stripped.path = "/";
    } else if (rest[0] == '/') {
        stripped.path = rest;
    } else {
        size_t rest_len = strlen(rest);
        owned = (char *)malloc(rest_len + 2);
        if (!owned) goto not_found;
        owned[0] = '/';
        memcpy(owned + 1, rest, rest_len + 1);
        stripped.path = owned;
    }
    /* After stripping, "//host" / "/\\host" is a protocol-relative URL.
     * Handlers that redirect to req->path would otherwise emit Location XSS. */
    if (http_path_contains_dotdot(stripped.path) ||
        (stripped.path[0] == '/' &&
         (stripped.path[1] == '/' || stripped.path[1] == '\\'))) {
        free(owned);
        goto not_found;
    }
    ctx->inner(&stripped, w);
    free(owned);
    return;

not_found:
    neverc_http_not_found(req, w);
}

void neverc_http_strip_prefix(neverc_http_mux_t *mux, const char *prefix,
                                const char *pattern,
                                neverc_http_handler_func_t handler) {
    if (!prefix || !handler) return;

    strip_prefix_ctx_t *ctx = (strip_prefix_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return;
    ctx->prefix = strdup(prefix);
    if (!ctx->prefix) {
        free(ctx);
        return;
    }
    ctx->prefix_len = strlen(prefix);
    ctx->inner = handler;

    int result = mux
        ? nc_http_mux_handle_owned_context(
              mux, pattern ? pattern : prefix, strip_prefix_handler_fn, ctx,
              strip_prefix_context_free)
        : nc_http_default_handle_owned_context(
              pattern ? pattern : prefix, strip_prefix_handler_fn, ctx,
              strip_prefix_context_free);
    if (result != 0) strip_prefix_context_free(ctx);
}

/* ======================================================================
 * Serve File — Content-Type sniffing, Last-Modified, Range, If-Modified-Since
 * ====================================================================== */

static int http_hex_nibble(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    return -1;
}

/* True if any path segment is ".." after one percent-decode pass.
 * Matches Go ServeFile: r.URL.Path is unescaped, then ".." elements are
 * rejected. Literal and %2e/%2E forms must not leak past StripPrefix. */
static int http_path_contains_dotdot(const char *path) {
    if (!path) return 0;
    size_t seg_len = 0;
    char seg[2];
    const unsigned char *s = (const unsigned char *)path;
    while (*s) {
        unsigned char c = *s;
        if (c == '%') {
            int high = http_hex_nibble(s[1]);
            int low = http_hex_nibble(s[2]);
            if ((high | low) >= 0) {
                c = (unsigned char)((high << 4) | low);
                s += 2;
            }
        }
        if (c == '/' || c == '\\') {
            if (seg_len == 2 && seg[0] == '.' && seg[1] == '.')
                return 1;
            seg_len = 0;
        } else {
            if (seg_len < 2) seg[seg_len] = (char)c;
            seg_len++;
        }
        s++;
    }
    return seg_len == 2 && seg[0] == '.' && seg[1] == '.';
}

static int http_parse_dec_u64(const char *start, const char *end,
                              uint64_t *value) {
    if (!start || start >= end || !value) return -1;
    uint64_t parsed = 0;
    for (const char *p = start; p < end; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < '0' || c > '9') return -1;
        uint64_t digit = (uint64_t)(c - '0');
        if (parsed > (UINT64_MAX - digit) / 10U) return -1;
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return 0;
}

/* 0 = ignore Range (send 200), 1 = satisfiable range, -1 = 416. */
static int http_parse_bytes_range(const char *header, size_t size,
                                  size_t *out_start, size_t *out_length) {
    if (!header || !out_start || !out_length) return 0;
    while (*header == ' ' || *header == '\t') header++;
    if (strncasecmp(header, "bytes=", 6) != 0) return 0;
    header += 6;
    if (strchr(header, ',')) return 0;

    size_t length = strlen(header);
    while (length > 0 &&
           (header[length - 1] == ' ' || header[length - 1] == '\t'))
        length--;
    if (length == 0) return 0;

    const char *dash = (const char *)memchr(header, '-', length);
    if (!dash) return 0;

    int has_first = dash > header;
    int has_last = (header + length) > dash + 1;
    if (!has_first && !has_last) return 0;

    uint64_t first = 0;
    uint64_t last = 0;
    if (has_first && http_parse_dec_u64(header, dash, &first) != 0)
        return 0;
    if (has_last && http_parse_dec_u64(dash + 1, header + length, &last) != 0)
        return 0;

    uint64_t start;
    uint64_t end;
    if (!has_first) {
        if (last == 0 || size == 0) return -1;
        start = last >= (uint64_t)size ? 0 : (uint64_t)size - last;
        end = (uint64_t)size - 1;
    } else {
        if (first >= (uint64_t)size) return -1;
        start = first;
        if (has_last) {
            if (last < first) return -1;
            end = last;
            if (end >= (uint64_t)size) end = (uint64_t)size - 1;
        } else {
            end = (uint64_t)size - 1;
        }
    }
    *out_start = (size_t)start;
    *out_length = (size_t)(end - start + 1);
    return 1;
}

static int http_parse_http_date(const char *value, neverc_time_t *out) {
    static const char *layouts[] = {
        "Mon, 02 Jan 2006 15:04:05 GMT",
        "Monday, 02-Jan-06 15:04:05 GMT",
        "Mon Jan _2 15:04:05 2006",
    };
    if (!value || !out) return -1;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        if (neverc_time_parse(layouts[i], value, out) == 0)
            return 0;
    }
    return -1;
}

static int http_serve_file_copy(neverc_http_response_writer_t *w, FILE *f,
                                size_t offset, size_t length) {
    if (length == 0) return 0;
    if (offset > (size_t)LONG_MAX) return -1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return -1;
    char buf[8192];
    size_t remaining = length;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
        size_t n = fread(buf, 1, chunk, f);
        if (n == 0) return -1;
        if (neverc_http_write(w, buf, n) < 0) return -1;
        remaining -= n;
        if (n < chunk) return remaining == 0 ? 0 : -1;
    }
    return 0;
}

void neverc_http_serve_file(neverc_http_response_writer_t *w,
                              neverc_http_request_t *req,
                              const char *filepath) {
    if (!w || !filepath) return;

    /* Match Go ServeFile: reject ".." in the request path even when the
     * caller-supplied filepath is already a safe absolute file. */
    if (req && http_path_contains_dotdot(req->path)) {
        neverc_http_set_status(w, 400);
        neverc_http_write_string(w, "invalid URL path\n");
        return;
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        neverc_http_set_status(w, 404);
        neverc_http_write_string(w, "404 page not found\n");
        return;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > (uint64_t)LONG_MAX ||
        (uint64_t)st.st_size > (uint64_t)SIZE_MAX) {
        neverc_http_set_status(w, 500);
        neverc_http_write_string(w, "internal error\n");
        return;
    }
    size_t fsize = (size_t)st.st_size;

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        neverc_http_set_status(w, 404);
        neverc_http_write_string(w, "404 page not found\n");
        return;
    }

    unsigned char sniff_buf[512];
    size_t sniff_size = fsize < sizeof(sniff_buf) ? fsize : sizeof(sniff_buf);
    size_t sniff_n = fread(sniff_buf, 1, sniff_size, f);
    if (ferror(f)) {
        fclose(f);
        neverc_http_set_status(w, 500);
        neverc_http_write_string(w, "internal error\n");
        return;
    }
    if (sniff_n < sniff_size) fsize = sniff_n;

    const char *ct = neverc_http_detect_content_type(sniff_buf, sniff_n);
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

    neverc_time_t modtime = neverc_time_unix((int64_t)st.st_mtime, 0);
    char *last_modified = neverc_time_format(
        modtime, "Mon, 02 Jan 2006 15:04:05 GMT");
    if (last_modified) {
        neverc_http_set_header(w, "Last-Modified", last_modified);
        free(last_modified);
    }

    int is_head = req && req->method && strcmp(req->method, "HEAD") == 0;
    int is_get = !req || !req->method || strcmp(req->method, "GET") == 0;
    if (is_head) w->head_request = 1;

    if ((is_get || is_head) && req) {
        const char *ims = neverc_http_request_header(req, "If-Modified-Since");
        neverc_time_t since;
        if (ims && http_parse_http_date(ims, &since) == 0 &&
            neverc_time_before(modtime,
                               neverc_time_add(since, NEVERC_TIME_SECOND))) {
            fclose(f);
            neverc_http_set_status(w, 304);
            return;
        }
    }

    size_t range_start = 0;
    size_t range_length = fsize;
    int ranged = 0;
    if ((is_get || is_head) && req) {
        const char *range = neverc_http_request_header(req, "Range");
        if (range) {
            int range_result = http_parse_bytes_range(
                range, fsize, &range_start, &range_length);
            if (range_result < 0) {
                char content_range[64];
                int n = snprintf(content_range, sizeof(content_range),
                                 "bytes */%zu", fsize);
                fclose(f);
                neverc_http_set_status(w, 416);
                if (n > 0 && (size_t)n < sizeof(content_range))
                    neverc_http_set_header(w, "Content-Range", content_range);
                return;
            }
            if (range_result > 0) ranged = 1;
        }
    }

    if (ranged) {
        char content_range[80];
        size_t range_end = range_start + range_length - 1;
        int n = snprintf(content_range, sizeof(content_range),
                         "bytes %zu-%zu/%zu", range_start, range_end, fsize);
        if (n > 0 && (size_t)n < sizeof(content_range))
            neverc_http_set_header(w, "Content-Range", content_range);
        neverc_http_set_status(w, 206);
    }
    (void)neverc_http_set_content_length(w, range_length);

    if (is_head) {
        fclose(f);
        return;
    }

    int copied = http_serve_file_copy(w, f, range_start, range_length);
    fclose(f);
    if (copied != 0 && !w->headers_sent) {
        (void)neverc_http_reset_response(w);
        neverc_http_set_status(w, 500);
        neverc_http_write_string(w, "internal error\n");
    }
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

static int cors_origin_in_list(const char *origins, const char *origin) {
    if (!origins || !origin || origin[0] == '\0') return 0;
    size_t origin_length = strlen(origin);
    const char *entry = origins;
    for (;;) {
        const char *separator = strchr(entry, ',');
        const char *end = separator ? separator : entry + strlen(entry);
        while (entry < end && (*entry == ' ' || *entry == '\t')) entry++;
        while (end > entry && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if ((size_t)(end - entry) == origin_length &&
            memcmp(entry, origin, origin_length) == 0)
            return 1;
        if (!separator) return 0;
        entry = separator + 1;
    }
}

void neverc_http_cors_headers(neverc_http_response_writer_t *w,
                                const neverc_http_cors_config_t *cfg,
                                const char *origin) {
    if (!w || !cfg) return;

    const char *ao = cfg->allowed_origins ? cfg->allowed_origins : "*";
    int allowed = 0;

    if (strcmp(ao, "*") == 0 && !cfg->allow_credentials) {
        neverc_http_set_header(w, "Access-Control-Allow-Origin", "*");
        allowed = 1;
    } else if (origin && (strcmp(ao, "*") == 0 ||
                          cors_origin_in_list(ao, origin))) {
        neverc_http_set_header(w, "Access-Control-Allow-Origin", origin);
        neverc_http_set_header(w, "Vary", "Origin");
        allowed = 1;
    }

    if (allowed && cfg->allow_credentials)
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
