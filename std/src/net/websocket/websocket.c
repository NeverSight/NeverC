#include "neverc/std/net/websocket.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/url.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/crypto/sha1.h"
#include "neverc/std/encoding/base64.h"
#include "../_net_buffer.h"
#include "../_net_thread.h"
#include "../http/_http_internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WS_MAGIC "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_DEFAULT_HANDSHAKE_TIMEOUT_MS 30000
#define WS_DEFAULT_MAX_MESSAGE_SIZE (16u * 1024u * 1024u)
#define WS_MAX_HANDSHAKE_HEADER (64u * 1024u)
#define WS_MAX_TARGET_SIZE 2048
#define WS_KEEPALIVE_TICK_MS 25
#define WS_KEEPALIVE_WRITE_TIMEOUT_MS 1000
#define WS_MAX_DISCARD (16u * 1024u * 1024u)
#define WS_LENGTH_MSB ((uint64_t)1 << 63)

struct neverc_ws_conn {
    neverc_tcp_conn_t *tcp;
    neverc_tls_conn_t *tls;
    size_t read_limit;
    int write_timeout_ms;
    int is_client;
    volatile int failed;
    volatile int close_sent;
    volatile int close_received;
    nc_mutex_t write_lock;
    nc_mutex_t keepalive_lock;
    nc_thread_t keepalive_thread;
    int keepalive_running;
    int keepalive_stop;
    int keepalive_expired;
    int ping_interval_ms;
    int pong_timeout_ms;
    int awaiting_pong;
    uint64_t next_ping_ms;
    uint64_t pong_deadline_ms;
    uint8_t ping_token[8];
    int data_fragment_active;
    int data_message_opcode;
    size_t data_message_bytes;
    nc_buf_t text_utf8_acc;
};

typedef struct {
    char dial_addr[320];
    char host_header[320];
    char server_name[256];
    char target[WS_MAX_TARGET_SIZE];
    int secure;
} ws_url_t;

static neverc_ws_conn_t *ws_conn_new_role(neverc_tcp_conn_t *tcp,
                                           neverc_tls_conn_t *tls,
                                           int is_client,
                                           size_t read_limit);
static int write_frame_timeout(neverc_ws_conn_t *conn, int opcode,
                               const void *payload, size_t len, int fin,
                               int timeout_override_ms);
static int write_frame(neverc_ws_conn_t *conn, int opcode, int fin,
                       const void *payload, size_t len);

/* ======================================================================
 * Helpers
 * ====================================================================== */

static int strcasecmp_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = (unsigned char)a[i];
        int cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        if (ca == '\0') return 0;
    }
    return 0;
}

static void ws_set_error(const char **errp, const char *message) {
    if (errp) *errp = message;
}

static int ws_contains_ctl(const char *s) {
    if (!s) return 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7f) return 1;
    }
    return 0;
}

static int ws_is_token(const char *s) {
    if (!s || !s[0]) return 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9'))
            continue;
        switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'':
        case '*': case '+': case '-': case '.': case '^': case '_':
        case '`': case '|': case '~':
            continue;
        default:
            return 0;
        }
    }
    return 1;
}

static int ws_parse_port(const char *s, size_t len, unsigned *port) {
    if (!s || len == 0 || !port) return -1;
    unsigned value = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        unsigned digit = (unsigned)(s[i] - '0');
        if (value > (65535u - digit) / 10u) return -1;
        value = value * 10u + digit;
    }
    if (value == 0) return -1;
    *port = value;
    return 0;
}

/* Same Host byte allowlist as HTTP/1 (Go ValidHostHeader without comma). */
static int ws_host_reg_name_byte(unsigned char c) {
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

static int ws_valid_host(const char *value, size_t length) {
    if (!value || length == 0) return 0;
    if (value[0] == '[') {
        const char *close = (const char *)memchr(value, ']', length);
        if (!close || close == value + 1) return 0;
        size_t inner = (size_t)(close - value - 1);
        int has_colon = 0;
        for (size_t i = 0; i < inner; i++) {
            unsigned char c = (unsigned char)value[1 + i];
            if (c == ':') has_colon = 1;
            else if (!ws_host_reg_name_byte(c))
                return 0;
        }
        if (!has_colon &&
            !(inner > 2 && (value[1] == 'v' || value[1] == 'V')))
            return 0;
        size_t after = length - (size_t)(close - value) - 1;
        if (after == 0) return 1;
        unsigned port = 0;
        return close[1] == ':' &&
               ws_parse_port(close + 2, after - 1, &port) == 0;
    }

    const char *colon = (const char *)memchr(value, ':', length);
    size_t host_length = colon ? (size_t)(colon - value) : length;
    if (host_length == 0) return 0;
    for (size_t i = 0; i < host_length; i++) {
        if (!ws_host_reg_name_byte((unsigned char)value[i]))
            return 0;
    }
    if (!colon) return 1;
    if (memchr(colon + 1, ':', length - host_length - 1)) return 0;
    unsigned port = 0;
    return ws_parse_port(colon + 1, length - host_length - 1, &port) == 0;
}

/* Host header extras beyond net/url (comma / XSS bytes). */
static int ws_host_header_ok(const char *host) {
    if (!host || !host[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)host; *p; p++) {
        if (*p <= 0x20 || *p == 0x7f || *p == ',' || *p == '<' ||
            *p == '>' || *p == '"' || *p == '\\')
            return 0;
    }
    return 1;
}

static int ws_parse_url(const char *url, ws_url_t *parsed,
                        const char **errp) {
    neverc_url_t u;
    int n;
    unsigned port;
    int ipv6;
    size_t host_len;
    size_t path_len;
    size_t query_len;
    size_t target_len;

    if (!url || !parsed) {
        ws_set_error(errp, "invalid WebSocket URL");
        return -1;
    }
    memset(parsed, 0, sizeof(*parsed));
    if (neverc_url_parse(&u, url) != 0) {
        ws_set_error(errp, "invalid WebSocket URL");
        return -1;
    }
    if (strcmp(u.scheme, "wss") == 0)
        parsed->secure = 1;
    else if (strcmp(u.scheme, "ws") != 0) {
        ws_set_error(errp, "WebSocket URL must use ws:// or wss://");
        return -1;
    }
    if (u.user[0] || u.has_password) {
        ws_set_error(errp, "WebSocket URL userinfo is not allowed");
        return -1;
    }
    if (u.fragment[0] || strchr(url, '#')) {
        ws_set_error(errp, "WebSocket URL fragments are not allowed");
        return -1;
    }
    if (!ws_host_header_ok(u.host)) {
        ws_set_error(errp, "invalid WebSocket URL authority");
        return -1;
    }

    port = parsed->secure ? 443u : 80u;
    if (u.port[0] &&
        ws_parse_port(u.port, strlen(u.port), &port) != 0) {
        ws_set_error(errp, "invalid WebSocket URL port");
        return -1;
    }

    host_len = strlen(u.host);
    if (host_len == 0 || host_len >= sizeof(parsed->server_name)) {
        ws_set_error(errp, "invalid WebSocket server name");
        return -1;
    }
    memcpy(parsed->server_name, u.host, host_len + 1);

    ipv6 = strchr(u.host, ':') != NULL;
    if (ipv6) {
        n = u.port[0]
                ? snprintf(parsed->host_header, sizeof(parsed->host_header),
                           "[%s]:%s", u.host, u.port)
                : snprintf(parsed->host_header, sizeof(parsed->host_header),
                           "[%s]", u.host);
    } else {
        n = u.port[0]
                ? snprintf(parsed->host_header, sizeof(parsed->host_header),
                           "%s:%s", u.host, u.port)
                : snprintf(parsed->host_header, sizeof(parsed->host_header),
                           "%s", u.host);
    }
    if (n <= 0 || (size_t)n >= sizeof(parsed->host_header)) {
        ws_set_error(errp, "invalid WebSocket URL authority");
        return -1;
    }

    n = ipv6
            ? snprintf(parsed->dial_addr, sizeof(parsed->dial_addr),
                       "[%s]:%u", u.host, port)
            : snprintf(parsed->dial_addr, sizeof(parsed->dial_addr),
                       "%s:%u", u.host, port);
    if (n <= 0 || (size_t)n >= sizeof(parsed->dial_addr)) {
        ws_set_error(errp, "WebSocket dial address is too long");
        return -1;
    }

    path_len = strlen(u.path);
    query_len = u.has_query ? strlen(u.raw_query) : 0;
    if (path_len == 0) {
        parsed->target[0] = '/';
        parsed->target[1] = '\0';
        target_len = 1;
    } else {
        if (path_len >= sizeof(parsed->target)) {
            ws_set_error(errp, "WebSocket request target is too long");
            return -1;
        }
        memcpy(parsed->target, u.path, path_len + 1);
        target_len = path_len;
    }
    if (u.has_query) {
        if (target_len + 1 + query_len >= sizeof(parsed->target)) {
            ws_set_error(errp, "WebSocket request target is too long");
            return -1;
        }
        parsed->target[target_len] = '?';
        memcpy(parsed->target + target_len + 1, u.raw_query, query_len + 1);
    }
    {
        const char *target_query = strchr(parsed->target, '?');
        size_t path_length = target_query
            ? (size_t)(target_query - parsed->target)
            : strlen(parsed->target);
        if (path_length >= 2 &&
            neverc_url_path_n_is_protocol_relative(
                parsed->target, path_length)) {
            ws_set_error(errp, "invalid WebSocket request target");
            return -1;
        }
    }
    return 0;
}

static int copy_unique_header_value(const char *raw, const char *hdr_end,
                                    const char *name, char *out,
                                    size_t out_cap) {
    if (!raw || !hdr_end || !name || !out || out_cap == 0) return -1;
    size_t name_len = strlen(name);
    const char *p = raw;
    while (p + 1 < hdr_end && !(p[0] == '\r' && p[1] == '\n')) p++;
    if (p + 1 >= hdr_end) return -1;
    p += 2;

    int found = 0;
    while (p < hdr_end) {
        const char *line_end = p;
        while (line_end + 1 < hdr_end &&
               !(line_end[0] == '\r' && line_end[1] == '\n'))
            line_end++;
        if (line_end + 1 >= hdr_end) break;
        if (line_end == p) break;

        const char *colon = memchr(p, ':', (size_t)(line_end - p));
        if (colon && (size_t)(colon - p) == name_len &&
            strcasecmp_n(p, name, name_len) == 0) {
            if (found) return -1;
            const char *value = colon + 1;
            while (value < line_end && (*value == ' ' || *value == '\t'))
                value++;
            const char *value_end = line_end;
            while (value_end > value &&
                   (value_end[-1] == ' ' || value_end[-1] == '\t'))
                value_end--;
            size_t value_len = (size_t)(value_end - value);
            if (value_len >= out_cap) return -1;
            memcpy(out, value, value_len);
            out[value_len] = '\0';
            found = 1;
        }
        p = line_end + 2;
    }
    return found;
}

/* Reject obs-fold and bare LF so Content-Length / Transfer-Encoding cannot
 * hide on a continuation line or inside another header's value. */
static int ws_headers_reject_obs_fold_and_bare_lf(const char *raw,
                                                  const char *hdr_end) {
    if (!raw || !hdr_end || raw >= hdr_end) return -1;
    const char *p = raw;
    while (p < hdr_end) {
        if (*p == '\n' || *p == '\0') return -1;
        if (*p == '\r') {
            if (p + 1 >= hdr_end || p[1] != '\n') return -1;
            p += 2;
            if (p < hdr_end && (*p == ' ' || *p == '\t'))
                return -1;
            continue;
        }
        p++;
    }
    return 0;
}

static int ws_value_has_token(const char *value, const char *token) {
    if (!value || !token) return 0;
    size_t token_len = strlen(token);
    for (size_t i = 0; value[i]; ) {
        while (value[i] == ' ' || value[i] == '\t' || value[i] == ',') i++;
        size_t start = i;
        while (value[i] && value[i] != ',') i++;
        size_t end = i;
        while (end > start &&
               (value[end - 1] == ' ' || value[end - 1] == '\t'))
            end--;
        if (end - start == token_len &&
            strcasecmp_n(value + start, token, token_len) == 0)
            return 1;
        if (value[i] == ',') i++;
    }
    return 0;
}

static int tcp_write_all(neverc_tcp_conn_t *conn, const void *data, size_t len) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
        size_t remaining = len - sent;
        size_t chunk = remaining > (size_t)INT_MAX
                           ? (size_t)INT_MAX : remaining;
        int n = neverc_tcp_write(conn, p + sent, chunk);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int tcp_write_all_context(neverc_tcp_conn_t *conn,
                                 neverc_context_t *ctx,
                                 const void *data, size_t len) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
        neverc_net_result_t result =
            neverc_tcp_write_context(conn, ctx, p + sent, len - sent);
        if (result.status != NEVERC_NET_OK || result.transferred == 0)
            return -1;
        sent += result.transferred;
    }
    return 0;
}

static int ws_transport_write_all_context(neverc_tcp_conn_t *tcp,
                                          neverc_tls_conn_t *tls,
                                          neverc_context_t *ctx,
                                          const void *data, size_t len) {
    if (!tls)
        return ctx ? tcp_write_all_context(tcp, ctx, data, len)
                   : tcp_write_all(tcp, data, len);
    int written = ctx ? neverc_tls_write_context(tls, ctx, data, len)
                      : neverc_tls_write(tls, data, len);
    return written >= 0 && (size_t)written == len ? 0 : -1;
}

static int ws_transport_read_context(neverc_tcp_conn_t *tcp,
                                     neverc_tls_conn_t *tls,
                                     neverc_context_t *ctx,
                                     void *data, size_t len) {
    if (tls) return neverc_tls_read_context(tls, ctx, data, len);
    neverc_net_result_t result = neverc_tcp_read_context(
        tcp, ctx, data, len);
    return result.status == NEVERC_NET_OK ? (int)result.transferred :
           result.status == NEVERC_NET_EOF ? 0 : -1;
}

/* ======================================================================
 * Handshake
 * ====================================================================== */

static int ws_read_client_handshake(neverc_tcp_conn_t *tcp,
                                    neverc_tls_conn_t *tls,
                                    neverc_context_t *ctx,
                                    const char *expected_accept,
                                    const char *subprotocol,
                                    const char **errp) {
    char *response = (char *)malloc(WS_MAX_HANDSHAKE_HEADER + 1);
    if (!response) {
        ws_set_error(errp, "out of memory reading WebSocket handshake");
        return -1;
    }

    size_t len = 0;
    while (len < WS_MAX_HANDSHAKE_HEADER) {
        char byte;
        int count = ws_transport_read_context(tcp, tls, ctx, &byte, 1);
        if (count != 1) {
            free(response);
            ws_set_error(errp, "failed to read WebSocket handshake");
            return -1;
        }
        if (byte == '\0') {
            free(response);
            ws_set_error(errp, "invalid WebSocket handshake header");
            return -1;
        }
        response[len++] = byte;
        if (len >= 4 && memcmp(response + len - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    if (len < 4 || memcmp(response + len - 4, "\r\n\r\n", 4) != 0) {
        free(response);
        ws_set_error(errp, "WebSocket handshake header is too large");
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)response[i];
        if (c == '\r') {
            if (i + 1 >= len || response[i + 1] != '\n') {
                free(response);
                ws_set_error(errp, "invalid WebSocket handshake header");
                return -1;
            }
            i++;
        } else if (c == '\n' || (c < 0x20 && c != '\t') || c == 0x7f) {
            free(response);
            ws_set_error(errp, "invalid WebSocket handshake header");
            return -1;
        }
    }
    response[len] = '\0';

    const char *status_end = strstr(response, "\r\n");
    size_t status_len = status_end ? (size_t)(status_end - response) : 0;
    if (!status_end || status_len < 12 ||
        memcmp(response, "HTTP/1.1 ", 9) != 0 ||
        response[9] != '1' || response[10] != '0' || response[11] != '1' ||
        (status_len > 12 && response[12] != ' ')) {
        free(response);
        ws_set_error(errp, "WebSocket server rejected the upgrade");
        return -1;
    }

    const char *header_end = response + len - 4;
    const char *header_scan_end = header_end + 2;
    if (ws_headers_reject_obs_fold_and_bare_lf(response, header_scan_end) != 0) {
        free(response);
        ws_set_error(errp, "invalid WebSocket handshake header");
        return -1;
    }
    char value[256];
    if (copy_unique_header_value(response, header_scan_end, "Upgrade",
                                 value, sizeof(value)) != 1 ||
        strcasecmp(value, "websocket") != 0) {
        free(response);
        ws_set_error(errp, "invalid WebSocket Upgrade response header");
        return -1;
    }
    if (copy_unique_header_value(response, header_scan_end, "Connection",
                                 value, sizeof(value)) != 1 ||
        !ws_value_has_token(value, "upgrade")) {
        free(response);
        ws_set_error(errp, "invalid WebSocket Connection response header");
        return -1;
    }
    if (copy_unique_header_value(response, header_scan_end,
                                 "Sec-WebSocket-Accept", value,
                                 sizeof(value)) != 1 ||
        strcmp(value, expected_accept) != 0) {
        free(response);
        ws_set_error(errp, "invalid Sec-WebSocket-Accept response header");
        return -1;
    }

    int protocol_result = copy_unique_header_value(
        response, header_scan_end, "Sec-WebSocket-Protocol", value,
        sizeof(value));
    if ((subprotocol &&
         (protocol_result != 1 || strcmp(value, subprotocol) != 0)) ||
        (!subprotocol && protocol_result != 0)) {
        free(response);
        ws_set_error(errp, "invalid WebSocket subprotocol response");
        return -1;
    }
    if (copy_unique_header_value(response, header_scan_end,
                                 "Sec-WebSocket-Extensions", value,
                                 sizeof(value)) != 0) {
        free(response);
        ws_set_error(errp, "server selected an unsupported WebSocket extension");
        return -1;
    }
    {
        char clen[32];
        int ncl = copy_unique_header_value(response, header_scan_end,
                                           "Content-Length", clen,
                                           sizeof(clen));
        if (ncl < 0 || (ncl == 1 && strcmp(clen, "0") != 0)) {
            free(response);
            ws_set_error(errp, "WebSocket 101 must not have a body");
            return -1;
        }
        char te[32];
        int nte = copy_unique_header_value(response, header_scan_end,
                                           "Transfer-Encoding", te,
                                           sizeof(te));
        if (nte != 0) {
            free(response);
            ws_set_error(errp, "WebSocket 101 must not have a body");
            return -1;
        }
    }

    free(response);
    return 0;
}

neverc_ws_conn_t *neverc_ws_dial(const char *url,
                                  const neverc_ws_client_config_t *config,
                                  const char **errp) {
    ws_set_error(errp, NULL);
    ws_url_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    if (ws_parse_url(url, &parsed, errp) != 0) return NULL;

    const char *origin = config ? config->origin : NULL;
    const char *subprotocol = config ? config->subprotocol : NULL;
    if ((origin && ws_contains_ctl(origin)) ||
        (subprotocol && !ws_is_token(subprotocol)) ||
        (config &&
         (config->handshake_timeout_ms < 0 || config->read_timeout_ms < 0 ||
          config->write_timeout_ms < 0 || config->ping_interval_ms < 0 ||
          config->pong_timeout_ms < 0 ||
          ((config->ping_interval_ms == 0) !=
           (config->pong_timeout_ms == 0))))) {
        ws_set_error(errp, "invalid WebSocket client configuration");
        return NULL;
    }

    uint8_t nonce[16];
    if (neverc_crypto_rand_read(nonce, sizeof(nonce)) != 0) {
        ws_set_error(errp, "failed to generate WebSocket handshake nonce");
        return NULL;
    }
    char key[25];
    size_t key_len = neverc_base64_encode(key, nonce, sizeof(nonce));
    if (key_len != 24) {
        ws_set_error(errp, "failed to encode WebSocket handshake nonce");
        return NULL;
    }
    key[key_len] = '\0';

    char expected_accept[64];
    if (neverc_ws_compute_accept(key, expected_accept,
                                 sizeof(expected_accept)) != 0) {
        ws_set_error(errp, "failed to compute WebSocket accept value");
        return NULL;
    }

    char request[4096];
    int request_len = snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s%s%s%s%s%s"
        "\r\n",
        parsed.target, parsed.host_header, key,
        origin ? "Origin: " : "", origin ? origin : "",
        origin ? "\r\n" : "",
        subprotocol ? "Sec-WebSocket-Protocol: " : "",
        subprotocol ? subprotocol : "", subprotocol ? "\r\n" : "");
    if (request_len <= 0 || (size_t)request_len >= sizeof(request)) {
        ws_set_error(errp, "WebSocket handshake request is too large");
        return NULL;
    }

    int timeout = config && config->handshake_timeout_ms
                      ? config->handshake_timeout_ms
                      : WS_DEFAULT_HANDSHAKE_TIMEOUT_MS;
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *ctx = background
        ? neverc_context_with_timeout_handle(background, timeout, &cancel)
        : NULL;
    if (!ctx || !cancel) {
        if (ctx) neverc_context_free(ctx);
        if (cancel) neverc_context_cancel_handle_free(cancel);
        if (background) neverc_context_free(background);
        ws_set_error(errp, "failed to create WebSocket handshake context");
        return NULL;
    }

    neverc_tcp_conn_t *tcp = NULL;
    neverc_tls_conn_t *tls = NULL;
    neverc_net_result_t dial_result =
        neverc_tcp_dial_context(parsed.dial_addr, ctx, &tcp);
    if (dial_result.status != NEVERC_NET_OK || !tcp) {
        neverc_context_cancel_handle_cancel(cancel);
        neverc_context_free(ctx);
        neverc_context_cancel_handle_free(cancel);
        neverc_context_free(background);
        ws_set_error(errp, "WebSocket TCP dial failed");
        return NULL;
    }
    if (parsed.secure) {
        int64_t deadline = neverc_context_deadline(ctx);
        if (deadline > 0 &&
            (neverc_tcp_set_read_deadline(tcp, deadline) != 0 ||
             neverc_tcp_set_write_deadline(tcp, deadline) != 0)) {
            ws_set_error(errp, "failed to configure WSS deadline");
            goto dial_failed;
        }
        neverc_tls_config_t *tls_config = neverc_tls_config_new();
        if (!tls_config) {
            ws_set_error(errp, "out of memory creating WSS configuration");
            goto dial_failed;
        }
        neverc_tls_config_set_server_name(tls_config, parsed.server_name);
        const char *alpn[] = { "http/1.1" };
        neverc_tls_config_set_alpn(tls_config, alpn, 1);
        const char *tls_error = NULL;
        tls = neverc_tls_client(tcp, tls_config, &tls_error);
        neverc_tls_config_free(tls_config);
        if (!tls) {
            ws_set_error(errp, tls_error ? tls_error :
                         "WSS TLS handshake failed");
            goto dial_failed;
        }
        const char *negotiated = neverc_tls_alpn(tls);
        /* Client offered http/1.1. Missing ALPN is fail-closed: a peer
         * that selected h2 or omitted ALPN is not a confirmed HTTP/1.1
         * WebSocket transport. */
        if (!negotiated || strcmp(negotiated, "http/1.1") != 0) {
            ws_set_error(errp, "WSS server selected unsupported ALPN");
            goto dial_failed;
        }
    }
    if (ws_transport_write_all_context(tcp, tls, ctx, request,
                                       (size_t)request_len) != 0 ||
        ws_read_client_handshake(tcp, tls, ctx, expected_accept, subprotocol,
                                 errp) != 0) {
        if (errp && !*errp) *errp = "WebSocket client handshake failed";
        goto dial_failed;
    }
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_free(ctx);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(background);
    if (parsed.secure &&
        (neverc_tcp_set_read_deadline(tcp, 0) != 0 ||
         neverc_tcp_set_write_deadline(tcp, 0) != 0)) {
        if (tls) neverc_tls_close(tls);
        neverc_tcp_close(tcp);
        ws_set_error(errp, "failed to clear WSS handshake deadline");
        return NULL;
    }

    size_t read_limit = config && config->max_message_size
                            ? config->max_message_size
                            : WS_DEFAULT_MAX_MESSAGE_SIZE;
    neverc_ws_conn_t *ws = ws_conn_new_role(tcp, tls, 1, read_limit);
    if (!ws) {
        if (tls) neverc_tls_close(tls);
        neverc_tcp_close(tcp);
        ws_set_error(errp, "out of memory creating WebSocket connection");
        return NULL;
    }
    if (config &&
        ((config->read_timeout_ms > 0 &&
          neverc_ws_set_read_timeout(ws, config->read_timeout_ms) != 0) ||
         (config->write_timeout_ms > 0 &&
          neverc_ws_set_write_timeout(ws, config->write_timeout_ms) != 0) ||
         (config->ping_interval_ms > 0 &&
          neverc_ws_set_keepalive(ws, config->ping_interval_ms,
                                  config->pong_timeout_ms) != 0))) {
        neverc_ws_conn_free(ws);
        ws_set_error(errp, "invalid WebSocket runtime configuration");
        return NULL;
    }
    return ws;

dial_failed:
    if (tls) neverc_tls_close(tls);
    neverc_tcp_close(tcp);
    neverc_context_cancel_handle_cancel(cancel);
    neverc_context_free(ctx);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(background);
    return NULL;
}

int neverc_ws_compute_accept(const char *key, char *accept, size_t accept_cap) {
    if (!key || !accept || accept_cap < 29) return -1;

    char combined[128];
    int clen = snprintf(combined, sizeof(combined), "%s%s", key, WS_MAGIC);
    if (clen <= 0 || (size_t)clen >= sizeof(combined)) return -1;

    uint8_t digest[20];
    neverc_sha1_sum((const uint8_t *)combined, (size_t)clen, digest);

    size_t need = neverc_base64_encoded_len(20);
    if (need >= accept_cap) return -1;
    neverc_base64_encode(accept, digest, 20);
    accept[need] = '\0';
    return 0;
}

int neverc_ws_handshake_server(neverc_tcp_conn_t *conn, const char *raw_request,
                                size_t raw_len, size_t *consumed) {
    if (!conn || !raw_request || !consumed) return -1;
    *consumed = 0;

    const char *hdr_end = NULL;
    for (size_t i = 0; i + 3 < raw_len; i++) {
        if (raw_request[i] == '\r' && raw_request[i+1] == '\n' &&
            raw_request[i+2] == '\r' && raw_request[i+3] == '\n') {
            hdr_end = raw_request + i;
            break;
        }
    }
    if (!hdr_end) return -1;

    if (strncmp(raw_request, "GET ", 4) != 0) return -1;
    {
        const char *req_crlf = memchr(raw_request, '\r',
                                      (size_t)(hdr_end - raw_request));
        if (!req_crlf || req_crlf[1] != '\n') return -1;
        const char *target = raw_request + 4;
        if (target >= req_crlf || *target == ' ') return -1;
        const char *sp = memchr(target, ' ', (size_t)(req_crlf - target));
        if (!sp || (size_t)(req_crlf - sp) != 9 ||
            memcmp(sp, " HTTP/1.1", 9) != 0)
            return -1;
        size_t target_length = (size_t)(sp - target);
        if (target[0] != '/') return -1;
        const char *target_query = (const char *)memchr(
            target, '?', target_length);
        size_t path_length = target_query
            ? (size_t)(target_query - target) : target_length;
        if (path_length >= 2 &&
            neverc_url_path_n_is_protocol_relative(target, path_length))
            return -1;
        for (size_t i = 0; i < target_length; i++) {
            unsigned char c = (unsigned char)target[i];
            if (c <= 0x20 || c == 0x7f || c == '#' || c == '\\')
                return -1;
        }
    }

    /* hdr_end points at the first \r of the terminating \r\n\r\n, which is
     * also the last header line's trailing \r. Include its \n for parsing. */
    const char *hdr_scan_end = hdr_end + 2;
    if (ws_headers_reject_obs_fold_and_bare_lf(raw_request, hdr_scan_end) != 0)
        return -1;

    char host[256];
    char upgrade[32];
    char connection[128];
    char version[16];
    char key_buf[64];
    if (copy_unique_header_value(raw_request, hdr_scan_end, "Host",
                                 host, sizeof(host)) != 1 || !host[0] ||
        !ws_valid_host(host, strlen(host)))
        return -1;
    if (copy_unique_header_value(raw_request, hdr_scan_end, "Upgrade",
                                 upgrade, sizeof(upgrade)) != 1 ||
        strcasecmp(upgrade, "websocket") != 0)
        return -1;
    if (copy_unique_header_value(raw_request, hdr_scan_end, "Connection",
                                 connection, sizeof(connection)) != 1 ||
        !ws_value_has_token(connection, "upgrade"))
        return -1;
    if (copy_unique_header_value(raw_request, hdr_scan_end,
                                 "Sec-WebSocket-Version", version,
                                 sizeof(version)) != 1 ||
        strcmp(version, "13") != 0)
        return -1;
    if (copy_unique_header_value(raw_request, hdr_scan_end,
                                 "Sec-WebSocket-Key", key_buf,
                                 sizeof(key_buf)) != 1 ||
        strlen(key_buf) != 24)
        return -1;
    {
        /* 24 alphabet chars without '=' decode to 18 bytes. Writing into
         * 16 bytes overflowed the stack before the length check ran. */
        uint8_t decoded_key[32];
        if (neverc_base64_decode(decoded_key, key_buf, 24) != 16)
            return -1;
    }
    {
        char clen[32];
        int ncl = copy_unique_header_value(raw_request, hdr_scan_end,
                                           "Content-Length", clen,
                                           sizeof(clen));
        if (ncl < 0) return -1;
        if (ncl == 1 && strcmp(clen, "0") != 0)
            return -1;
        char te[32];
        int nte = copy_unique_header_value(raw_request, hdr_scan_end,
                                           "Transfer-Encoding", te,
                                           sizeof(te));
        if (nte != 0) return -1;
    }

    char accept[64];
    if (neverc_ws_compute_accept(key_buf, accept, sizeof(accept)) != 0)
        return -1;

    char response[512];
    int n = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);
    if (n <= 0 || (size_t)n >= sizeof(response)) return -1;

    if (tcp_write_all(conn, response, (size_t)n) != 0) return -1;

    *consumed = (size_t)(hdr_end + 4 - raw_request);
    return 0;
}

static int ws_header_count(const neverc_http_request_t *req, const char *name) {
    if (!req || !req->raw_headers || !name) return 0;
    const char *p = req->raw_headers;
    int count = 0;
    for (int i = 0; i < req->nheaders; i++) {
        const char *hname = p;
        while (*p) p++;
        p++;
        while (*p) p++;
        p++;
        if (strcasecmp(hname, name) == 0) count++;
    }
    return count;
}

static int ws_validate_http_upgrade(const neverc_http_request_t *req,
                                     char *key_buf, size_t key_cap) {
    if (!req || !key_buf || key_cap < 2) return -1;
    if (!req->method || strcmp(req->method, "GET") != 0) return -1;
    if (!req->http_version || strcmp(req->http_version, "HTTP/1.1") != 0)
        return -1;
    if (req->body_len > 0) return -1;

    const char *upgrade = neverc_http_request_header(req, "Upgrade");
    if (!upgrade || strcasecmp(upgrade, "websocket") != 0) return -1;

    const char *conn_hdr = neverc_http_request_header(req, "Connection");
    if (!ws_value_has_token(conn_hdr, "upgrade")) return -1;

    const char *version = neverc_http_request_header(req, "Sec-WebSocket-Version");
    if (!version || strcmp(version, "13") != 0) return -1;

    if (ws_header_count(req, "Sec-WebSocket-Key") != 1) return -1;
    const char *ws_key = neverc_http_request_header(req, "Sec-WebSocket-Key");
    if (!ws_key || strlen(ws_key) != 24) return -1;
    uint8_t decoded_key[32];
    if (neverc_base64_decode(decoded_key, ws_key, 24) != 16) return -1;

    int ncl = ws_header_count(req, "Content-Length");
    if (ncl < 0) return -1;
    if (ncl > 1) return -1;
    if (ncl == 1) {
        const char *clen = neverc_http_request_header(req, "Content-Length");
        if (!clen || strcmp(clen, "0") != 0) return -1;
    }
    if (ws_header_count(req, "Transfer-Encoding") != 0) return -1;

    size_t ki = 0;
    while (ws_key[ki] && ki < key_cap - 1) {
        key_buf[ki] = ws_key[ki];
        ki++;
    }
    key_buf[ki] = '\0';
    return 0;
}

neverc_ws_conn_t *neverc_ws_upgrade_http(neverc_http_request_t *req,
                                          neverc_http_response_writer_t *w) {
    if (!req || !w) return NULL;

    char key_buf[64];
    if (ws_validate_http_upgrade(req, key_buf, sizeof(key_buf)) != 0)
        return NULL;

    char accept[64];
    if (neverc_ws_compute_accept(key_buf, accept, sizeof(accept)) != 0)
        return NULL;

    char response[512];
    int n = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);
    if (n <= 0 || (size_t)n >= sizeof(response)) return NULL;

    neverc_tcp_conn_t *tcp = neverc_http_hijack(w);
    neverc_tls_conn_t *tls = NULL;
    if (!tcp && nc_http_hijack_tls(w, &tls, &tcp) != 0) return NULL;
    neverc_ws_conn_t *websocket = ws_conn_new_role(
        tcp, tls, 0, WS_DEFAULT_MAX_MESSAGE_SIZE);
    if (!websocket || ws_transport_write_all_context(
            tcp, tls, NULL, response, (size_t)n) != 0) {
        if (websocket) neverc_ws_conn_free(websocket);
        else {
            if (tls) neverc_tls_close(tls);
            neverc_tcp_close(tcp);
        }
        return NULL;
    }
    return websocket;
}

/* ======================================================================
 * Connection
 * ====================================================================== */

static neverc_ws_conn_t *ws_conn_new_role(neverc_tcp_conn_t *tcp,
                                           neverc_tls_conn_t *tls,
                                           int is_client,
                                           size_t read_limit) {
    if (!tcp) return NULL;
    neverc_ws_conn_t *ws = (neverc_ws_conn_t *)calloc(1, sizeof(*ws));
    if (!ws) return NULL;
    ws->tcp = tcp;
    ws->tls = tls;
    ws->is_client = is_client != 0;
    ws->read_limit = read_limit;
    nc_buf_init(&ws->text_utf8_acc);
    nc_mutex_init(&ws->write_lock);
    nc_mutex_init(&ws->keepalive_lock);
    return ws;
}

neverc_ws_conn_t *neverc_ws_conn_new(neverc_tcp_conn_t *conn) {
    return ws_conn_new_role(conn, NULL, 0, WS_DEFAULT_MAX_MESSAGE_SIZE);
}

void neverc_ws_conn_free(neverc_ws_conn_t *conn) {
    if (!conn) return;
    nc_mutex_lock(&conn->keepalive_lock);
    int join_keepalive = conn->keepalive_running;
    conn->keepalive_stop = 1;
    nc_mutex_unlock(&conn->keepalive_lock);
    if (conn->tls) {
        (void)neverc_tls_shutdown_read(conn->tls);
        (void)neverc_tls_shutdown_write(conn->tls);
    } else if (conn->tcp) {
        neverc_tcp_shutdown_read(conn->tcp);
        neverc_tcp_shutdown_write(conn->tcp);
    }
    if (join_keepalive) nc_thread_join(conn->keepalive_thread);
    if (conn->tls) neverc_tls_close(conn->tls);
    if (conn->tcp) neverc_tcp_close(conn->tcp);
    nc_buf_free(&conn->text_utf8_acc);
    nc_mutex_destroy(&conn->keepalive_lock);
    nc_mutex_destroy(&conn->write_lock);
    free(conn);
}

int neverc_ws_set_timeout(neverc_ws_conn_t *conn, int ms) {
    if (!conn || !conn->tcp || ms < 0) return -1;
    int read_rc = neverc_ws_set_read_timeout(conn, ms);
    int write_rc = neverc_ws_set_write_timeout(conn, ms);
    return read_rc == 0 && write_rc == 0 ? 0 : -1;
}

int neverc_ws_set_read_timeout(neverc_ws_conn_t *conn, int ms) {
    if (!conn || !conn->tcp || ms < 0) return -1;
    return neverc_tcp_set_read_timeout(conn->tcp, ms);
}

int neverc_ws_set_write_timeout(neverc_ws_conn_t *conn, int ms) {
    if (!conn || !conn->tcp || ms < 0) return -1;
    nc_mutex_lock(&conn->write_lock);
    conn->write_timeout_ms = ms;
    nc_mutex_unlock(&conn->write_lock);
    return 0;
}

static void ws_sleep_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(ms / 1000U);
    delay.tv_nsec = (long)(ms % 1000U) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
#endif
}

static void ws_keepalive_shutdown(neverc_ws_conn_t *conn) {
    nc_atomic_store(&conn->failed, 1);
    if (conn->tls) {
        (void)neverc_tls_shutdown_read(conn->tls);
        (void)neverc_tls_shutdown_write(conn->tls);
    } else if (conn->tcp) {
        neverc_tcp_shutdown_read(conn->tcp);
        neverc_tcp_shutdown_write(conn->tcp);
    }
}

static void *ws_keepalive_main(void *arg) {
    neverc_ws_conn_t *conn = (neverc_ws_conn_t *)arg;
    for (;;) {
        uint8_t token[sizeof(conn->ping_token)];
        int send_ping = 0;
        int expired = 0;
        uint64_t now = nc_monotonic_ms();

        nc_mutex_lock(&conn->keepalive_lock);
        if (conn->keepalive_stop ||
            nc_atomic_load(&conn->failed) ||
            nc_atomic_load(&conn->close_sent) ||
            nc_atomic_load(&conn->close_received)) {
            nc_mutex_unlock(&conn->keepalive_lock);
            break;
        }
        if (conn->awaiting_pong && now >= conn->pong_deadline_ms) {
            conn->keepalive_expired = 1;
            conn->awaiting_pong = 0;
            expired = 1;
        } else if (!conn->awaiting_pong && now >= conn->next_ping_ms) {
            if (neverc_crypto_rand_read(token, sizeof(token)) != 0) {
                expired = 1;
            } else {
                memcpy(conn->ping_token, token, sizeof(token));
                conn->awaiting_pong = 1;
                conn->pong_deadline_ms = now +
                                         (uint64_t)conn->pong_timeout_ms;
                send_ping = 1;
            }
        }
        nc_mutex_unlock(&conn->keepalive_lock);

        if (expired) {
            ws_keepalive_shutdown(conn);
            break;
        }
        if (send_ping) {
            int write_budget = conn->pong_timeout_ms;
            if (write_budget > WS_KEEPALIVE_WRITE_TIMEOUT_MS)
                write_budget = WS_KEEPALIVE_WRITE_TIMEOUT_MS;
            if (write_frame_timeout(conn, NC_WS_OPCODE_PING, token,
                                    sizeof(token), 1, write_budget) != 0) {
                if (!nc_atomic_load(&conn->close_sent) &&
                    !nc_atomic_load(&conn->close_received))
                    ws_keepalive_shutdown(conn);
                break;
            }
        }
        ws_sleep_ms(WS_KEEPALIVE_TICK_MS);
    }
    return NULL;
}

static void ws_keepalive_stop(neverc_ws_conn_t *conn) {
    nc_mutex_lock(&conn->keepalive_lock);
    int running = conn->keepalive_running;
    conn->keepalive_stop = 1;
    nc_mutex_unlock(&conn->keepalive_lock);
    if (running) nc_thread_join(conn->keepalive_thread);
    nc_mutex_lock(&conn->keepalive_lock);
    conn->keepalive_running = 0;
    conn->keepalive_stop = 0;
    conn->awaiting_pong = 0;
    nc_mutex_unlock(&conn->keepalive_lock);
}

int neverc_ws_set_keepalive(neverc_ws_conn_t *conn, int ping_interval_ms,
                            int pong_timeout_ms) {
    if (!conn || !conn->tcp || ping_interval_ms < 0 || pong_timeout_ms < 0 ||
        ((ping_interval_ms == 0) != (pong_timeout_ms == 0)))
        return -1;

    ws_keepalive_stop(conn);
    if (ping_interval_ms == 0) return 0;
    if (nc_atomic_load(&conn->failed) ||
        nc_atomic_load(&conn->close_sent) ||
        nc_atomic_load(&conn->close_received))
        return -1;

    nc_mutex_lock(&conn->keepalive_lock);
    conn->keepalive_expired = 0;
    conn->ping_interval_ms = ping_interval_ms;
    conn->pong_timeout_ms = pong_timeout_ms;
    conn->next_ping_ms = nc_monotonic_ms() + (uint64_t)ping_interval_ms;
    nc_mutex_unlock(&conn->keepalive_lock);

    if (nc_thread_create(&conn->keepalive_thread, ws_keepalive_main, conn) !=
        0)
        return -1;
    nc_mutex_lock(&conn->keepalive_lock);
    conn->keepalive_running = 1;
    nc_mutex_unlock(&conn->keepalive_lock);
    return 0;
}

int neverc_ws_keepalive_expired(neverc_ws_conn_t *conn) {
    if (!conn) return 0;
    nc_mutex_lock(&conn->keepalive_lock);
    int expired = conn->keepalive_expired;
    nc_mutex_unlock(&conn->keepalive_lock);
    return expired;
}

int neverc_ws_set_read_limit(neverc_ws_conn_t *conn, size_t max_bytes) {
    if (!conn) return -1;
    conn->read_limit = max_bytes;
    return 0;
}

/* ======================================================================
 * Frame I/O (RFC 6455)
 * ====================================================================== */

static int read_exact(neverc_ws_conn_t *conn, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t got = 0;
    while (got < len) {
        size_t remaining = len - got;
        size_t chunk = remaining > (size_t)INT_MAX
                           ? (size_t)INT_MAX : remaining;
        int n = conn->tls
            ? neverc_tls_read(conn->tls, p + got, chunk)
            : neverc_tcp_read(conn->tcp, p + got, chunk);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int ws_discard_payload(neverc_ws_conn_t *conn, uint64_t plen) {
    uint8_t sink[256];
    while (plen > 0) {
        size_t chunk = plen > sizeof(sink) ? sizeof(sink) : (size_t)plen;
        if (read_exact(conn, sink, chunk) != 0)
            return -1;
        plen -= chunk;
    }
    return 0;
}

static int ws_valid_opcode(int opcode) {
    return opcode == NC_WS_OPCODE_CONTINUATION ||
           opcode == NC_WS_OPCODE_TEXT || opcode == NC_WS_OPCODE_BINARY ||
           opcode == NC_WS_OPCODE_CLOSE || opcode == NC_WS_OPCODE_PING ||
           opcode == NC_WS_OPCODE_PONG;
}

static int ws_control_opcode(int opcode) {
    return (opcode & 0x08) != 0;
}

static int ws_valid_close_code(uint16_t code) {
    if (code >= 3000 && code <= 4999) return 1;
    if (code < 1000 || code > 1014) return 0;
    return code != 1004 && code != 1005 && code != 1006;
}

#define WS_CLOSE_PROTOCOL_ERROR 1002
#define WS_CLOSE_INVALID_PAYLOAD 1007
#define WS_CLOSE_MESSAGE_TOO_BIG 1009

static int ws_valid_utf8(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i++];
        if (c <= 0x7f) continue;
        if (c >= 0xc2 && c <= 0xdf) {
            if (i >= len || (data[i++] & 0xc0) != 0x80) return 0;
            continue;
        }
        if (c >= 0xe0 && c <= 0xef) {
            if (i + 1 >= len) return 0;
            uint8_t c1 = data[i++];
            uint8_t c2 = data[i++];
            if ((c1 & 0xc0) != 0x80 || (c2 & 0xc0) != 0x80 ||
                (c == 0xe0 && c1 < 0xa0) ||
                (c == 0xed && c1 >= 0xa0))
                return 0;
            continue;
        }
        if (c >= 0xf0 && c <= 0xf4) {
            if (i + 2 >= len) return 0;
            uint8_t c1 = data[i++];
            uint8_t c2 = data[i++];
            uint8_t c3 = data[i++];
            if ((c1 & 0xc0) != 0x80 || (c2 & 0xc0) != 0x80 ||
                (c3 & 0xc0) != 0x80 ||
                (c == 0xf0 && c1 < 0x90) ||
                (c == 0xf4 && c1 >= 0x90))
                return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

/* Valid complete UTF-8 or a prefix that may still complete with more bytes. */
static int ws_valid_utf8_prefix(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = data[i++];
        if (c <= 0x7f) continue;
        if (c >= 0xc2 && c <= 0xdf) {
            if (i >= len) return 1;
            if ((data[i++] & 0xc0) != 0x80) return 0;
            continue;
        }
        if (c >= 0xe0 && c <= 0xef) {
            if (i >= len) return 1;
            uint8_t c1 = data[i++];
            if ((c1 & 0xc0) != 0x80 ||
                (c == 0xe0 && c1 < 0xa0) ||
                (c == 0xed && c1 >= 0xa0))
                return 0;
            if (i >= len) return 1;
            uint8_t c2 = data[i++];
            if ((c2 & 0xc0) != 0x80)
                return 0;
            continue;
        }
        if (c >= 0xf0 && c <= 0xf4) {
            if (i >= len) return 1;
            uint8_t c1 = data[i++];
            if ((c1 & 0xc0) != 0x80 ||
                (c == 0xf0 && c1 < 0x90) ||
                (c == 0xf4 && c1 >= 0x90))
                return 0;
            if (i >= len) return 1;
            uint8_t c2 = data[i++];
            if ((c2 & 0xc0) != 0x80) return 0;
            if (i >= len) return 1;
            uint8_t c3 = data[i++];
            if ((c3 & 0xc0) != 0x80)
                return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

static int ws_fail(neverc_ws_conn_t *conn) {
    if (conn) nc_atomic_store(&conn->failed, 1);
    return -1;
}

static int ws_fail_protocol(neverc_ws_conn_t *conn) {
    if (conn && !nc_atomic_load(&conn->close_sent) &&
        !nc_atomic_load(&conn->failed)) {
        uint8_t payload[2] = {
            (uint8_t)(WS_CLOSE_PROTOCOL_ERROR >> 8),
            (uint8_t)(WS_CLOSE_PROTOCOL_ERROR & 0xFF),
        };
        (void)write_frame(conn, NC_WS_OPCODE_CLOSE, 1, payload, sizeof(payload));
    }
    return ws_fail(conn);
}

static int ws_fail_invalid_payload(neverc_ws_conn_t *conn) {
    if (conn && !nc_atomic_load(&conn->close_sent) &&
        !nc_atomic_load(&conn->failed)) {
        uint8_t payload[2] = {
            (uint8_t)(WS_CLOSE_INVALID_PAYLOAD >> 8),
            (uint8_t)(WS_CLOSE_INVALID_PAYLOAD & 0xFF),
        };
        (void)write_frame(conn, NC_WS_OPCODE_CLOSE, 1, payload, sizeof(payload));
    }
    return ws_fail(conn);
}

static int ws_fail_too_big(neverc_ws_conn_t *conn) {
    if (conn && !nc_atomic_load(&conn->close_sent) &&
        !nc_atomic_load(&conn->failed)) {
        uint8_t payload[2] = {
            (uint8_t)(WS_CLOSE_MESSAGE_TOO_BIG >> 8),
            (uint8_t)(WS_CLOSE_MESSAGE_TOO_BIG & 0xFF),
        };
        (void)write_frame(conn, NC_WS_OPCODE_CLOSE, 1, payload, sizeof(payload));
    }
    return ws_fail(conn);
}

static void ws_reset_data_fragment(neverc_ws_conn_t *conn) {
    if (!conn) return;
    conn->data_fragment_active = 0;
    conn->data_message_opcode = 0;
    conn->data_message_bytes = 0;
    nc_buf_reset(&conn->text_utf8_acc);
}

static int ws_data_frame_begin(neverc_ws_conn_t *conn, int opcode, int fin) {
    if (opcode == NC_WS_OPCODE_TEXT || opcode == NC_WS_OPCODE_BINARY) {
        if (conn->data_fragment_active)
            return ws_fail_protocol(conn);
        conn->data_message_opcode = opcode;
        conn->data_message_bytes = 0;
        if (!fin) conn->data_fragment_active = 1;
        nc_buf_reset(&conn->text_utf8_acc);
    } else if (opcode == NC_WS_OPCODE_CONTINUATION) {
        if (!conn->data_fragment_active)
            return ws_fail_protocol(conn);
    } else {
        return ws_fail_protocol(conn);
    }
    return 0;
}

static int ws_data_message_account(neverc_ws_conn_t *conn, uint64_t plen) {
    size_t add = (size_t)plen;
    if (conn->read_limit) {
        if (conn->data_message_bytes > conn->read_limit ||
            add > conn->read_limit - conn->data_message_bytes)
            return ws_fail_too_big(conn);
    } else if (add > SIZE_MAX - conn->data_message_bytes) {
        return ws_fail_too_big(conn);
    }
    conn->data_message_bytes += add;
    return 0;
}

static int ws_data_frame_chunk(neverc_ws_conn_t *conn, const void *payload,
                               size_t plen) {
    if (conn->data_message_opcode != NC_WS_OPCODE_TEXT)
        return 0;
    if (conn->read_limit &&
        (conn->text_utf8_acc.len > conn->read_limit ||
         plen > conn->read_limit - conn->text_utf8_acc.len))
        return ws_fail_too_big(conn);
    if (nc_buf_append(&conn->text_utf8_acc, payload, plen) != 0)
        return ws_fail(conn);
    if (!ws_valid_utf8_prefix((const uint8_t *)conn->text_utf8_acc.data,
                              conn->text_utf8_acc.len))
        return ws_fail_invalid_payload(conn);
    return 0;
}

static int ws_data_frame_end(neverc_ws_conn_t *conn, int fin) {
    if (fin) {
        if (conn->data_message_opcode == NC_WS_OPCODE_TEXT &&
            !ws_valid_utf8((const uint8_t *)conn->text_utf8_acc.data,
                           conn->text_utf8_acc.len))
            return ws_fail_invalid_payload(conn);
        conn->data_fragment_active = 0;
        conn->data_message_bytes = 0;
        nc_buf_reset(&conn->text_utf8_acc);
    }
    return 0;
}

static int ws_read_payload(neverc_ws_conn_t *conn, void *buf, size_t plen,
                           int masked, const uint8_t *mask_key, int track_text) {
    uint8_t *p = (uint8_t *)buf;
    size_t total = 0;
    while (total < plen) {
        size_t remaining = plen - total;
        size_t chunk = remaining > (size_t)INT_MAX
                           ? (size_t)INT_MAX : remaining;
        int n = conn->tls
            ? neverc_tls_read(conn->tls, p + total, chunk)
            : neverc_tcp_read(conn->tcp, p + total, chunk);
        if (n <= 0) return ws_fail(conn);
        if (masked) {
            size_t i;
            for (i = 0; i < (size_t)n; i++)
                p[total + i] ^= mask_key[(total + i) % 4];
        }
        if (track_text &&
            ws_data_frame_chunk(conn, p + total, (size_t)n) != 0)
            return -1;
        total += (size_t)n;
    }
    return 0;
}

static int ws_discard_payload_tracked(neverc_ws_conn_t *conn, uint64_t plen,
                                      int masked, const uint8_t *mask_key,
                                      int track_text) {
    uint8_t sink[256];
    size_t total = 0;
    while (plen > 0) {
        size_t chunk = plen > sizeof(sink) ? sizeof(sink) : (size_t)plen;
        if (read_exact(conn, sink, chunk) != 0)
            return ws_fail(conn);
        if (masked && mask_key) {
            size_t i;
            for (i = 0; i < chunk; i++)
                sink[i] ^= mask_key[(total + i) % 4];
        }
        if (track_text && ws_data_frame_chunk(conn, sink, chunk) != 0)
            return -1;
        total += chunk;
        plen -= chunk;
    }
    return 0;
}

typedef struct {
    int opcode;
    int fin;
    int masked;
    uint64_t payload_length;
    uint8_t mask_key[4];
    size_t header_length;
} ws_frame_header_t;

static int ws_parse_frame_header(const uint8_t *header, size_t length,
                                 int is_client, ws_frame_header_t *parsed) {
    if (!header || !parsed || length < 2) return -1;
    memset(parsed, 0, sizeof(*parsed));
    parsed->opcode = header[0] & 0x0f;
    parsed->fin = (header[0] & 0x80) != 0;
    parsed->masked = (header[1] & 0x80) != 0;
    uint8_t length_code = header[1] & 0x7f;
    size_t position = 2;
    uint64_t payload_length = length_code;

    if ((header[0] & 0x70) != 0 || !ws_valid_opcode(parsed->opcode) ||
        parsed->masked == is_client ||
        (ws_control_opcode(parsed->opcode) && !parsed->fin))
        return -1;
    if (length_code == 126) {
        if (length - position < 2) return -1;
        payload_length = ((uint64_t)header[position] << 8) |
                         header[position + 1];
        position += 2;
        if (payload_length < 126) return -1;
    } else if (length_code == 127) {
        if (length - position < 8 || (header[position] & 0x80) != 0)
            return -1;
        payload_length = 0;
        for (int i = 0; i < 8; i++)
            payload_length = (payload_length << 8) | header[position + i];
        position += 8;
        if (payload_length < 65536) return -1;
    }
    if ((ws_control_opcode(parsed->opcode) && payload_length > 125) ||
        payload_length > (uint64_t)SIZE_MAX)
        return -1;
    if (parsed->masked) {
        if (length - position < sizeof(parsed->mask_key)) return -1;
        memcpy(parsed->mask_key, header + position, sizeof(parsed->mask_key));
        position += sizeof(parsed->mask_key);
    }
    parsed->payload_length = payload_length;
    parsed->header_length = position;
    return 0;
}

#ifdef NEVERC_NETWORK_PROTOCOL_FUZZING
int neverc_ws_test_fuzz_frame_parser(const void *input, size_t input_length,
                                     int is_client) {
    const uint8_t *wire = (const uint8_t *)input;
    ws_frame_header_t header;
    if ((!wire && input_length != 0) ||
        ws_parse_frame_header(wire, input_length, is_client, &header) != 0)
        return -1;
    if (header.payload_length > input_length - header.header_length)
        return -1;
    if (header.opcode == NC_WS_OPCODE_CLOSE) {
        uint8_t payload[125];
        size_t payload_length = (size_t)header.payload_length;
        for (size_t i = 0; i < payload_length; i++) {
            uint8_t byte = wire[header.header_length + i];
            payload[i] = header.masked ? byte ^ header.mask_key[i % 4] : byte;
        }
        if (payload_length == 1 ||
            (payload_length >= 2 &&
             (!ws_valid_close_code((uint16_t)((payload[0] << 8) |
                                               payload[1])) ||
              !ws_valid_utf8(payload + 2, payload_length - 2))))
            return -1;
    }
    return 0;
}

int neverc_ws_test_fuzz_url_parser(const void *input, size_t input_length) {
    if ((!input && input_length != 0) || input_length > SIZE_MAX - 1U)
        return -1;
    char *url = (char *)malloc(input_length + 1U);
    if (!url) return -1;
    if (input_length) memcpy(url, input, input_length);
    url[input_length] = '\0';
    ws_url_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    int result = ws_parse_url(url, &parsed, NULL);
    free(url);
    return result;
}
#endif

static int ws_frame_write_all(neverc_ws_conn_t *conn, neverc_context_t *ctx,
                              const void *data, size_t len) {
    return ws_transport_write_all_context(
        conn->tcp, conn->tls, ctx, data, len);
}

static int ws_shutdown_write(neverc_ws_conn_t *conn) {
    if (conn->tls)
        return neverc_tls_shutdown_write(conn->tls);
    if (conn->tcp)
        return neverc_tcp_shutdown_write(conn->tcp);
    return -1;
}

static int write_frame_timeout(neverc_ws_conn_t *conn, int opcode,
                               const void *payload, size_t len, int fin,
                               int timeout_override_ms) {
    if (!conn) return -1;
    nc_mutex_lock(&conn->write_lock);
    if (!conn->tcp || nc_atomic_load(&conn->failed) ||
        nc_atomic_load(&conn->close_sent) ||
        !ws_valid_opcode(opcode) ||
        (len > 0 && !payload) ||
        (ws_control_opcode(opcode) && (!fin || len > 125)) ||
        ((uint64_t)len & WS_LENGTH_MSB) != 0) {
        nc_mutex_unlock(&conn->write_lock);
        return -1;
    }

    int timeout_ms = timeout_override_ms > 0
                         ? timeout_override_ms
                         : conn->write_timeout_ms;
    neverc_context_t *ctx = NULL;
    neverc_context_t *background = NULL;
    neverc_context_cancel_handle_t *cancel = NULL;
    if (timeout_ms > 0) {
        background = neverc_context_background();
        ctx = background
            ? neverc_context_with_timeout_handle(background, timeout_ms, &cancel)
            : NULL;
        if (!ctx || !cancel) {
            if (ctx) neverc_context_free(ctx);
            if (cancel) neverc_context_cancel_handle_free(cancel);
            if (background) neverc_context_free(background);
            nc_mutex_unlock(&conn->write_lock);
            return -1;
        }
    }

    uint8_t hdr[14];
    size_t hlen = 2;
    int mask = conn->is_client;
    uint64_t wire_len = (uint64_t)len;

    hdr[0] = (uint8_t)((fin ? 0x80 : 0x00) | (opcode & 0x0F));
    if (len < 126) {
        hdr[1] = (uint8_t)(len & 0x7F);
        if (mask) hdr[1] |= 0x80;
    } else if (len < 65536) {
        hdr[1] = 126;
        if (mask) hdr[1] |= 0x80;
        hdr[2] = (uint8_t)((len >> 8) & 0xFF);
        hdr[3] = (uint8_t)(len & 0xFF);
        hlen = 4;
    } else {
        hdr[1] = 127;
        if (mask) hdr[1] |= 0x80;
        for (int i = 0; i < 8; i++)
            hdr[2 + i] = (uint8_t)((wire_len >> (56 - i * 8)) & 0xFF);
        hlen = 10;
    }

    uint8_t mask_key[4];
    if (mask) {
        if (neverc_crypto_rand_read(mask_key, sizeof(mask_key)) != 0) {
            ws_fail(conn);
            goto done;
        }
        memcpy(hdr + hlen, mask_key, sizeof(mask_key));
        hlen += sizeof(mask_key);
    }
    if (opcode == NC_WS_OPCODE_CLOSE)
        nc_atomic_store(&conn->close_sent, 1);
    if (ws_frame_write_all(conn, ctx, hdr, hlen) != 0) {
        ws_fail(conn);
        goto done;
    }
    if (!mask) {
        if (len > 0 &&
            ws_frame_write_all(conn, ctx, payload, len) != 0) {
            ws_fail(conn);
            goto done;
        }
    } else {
        const uint8_t *input = (const uint8_t *)payload;
        uint8_t masked[4096];
        size_t offset = 0;
        while (offset < len) {
            size_t chunk = len - offset;
            if (chunk > sizeof(masked)) chunk = sizeof(masked);
            for (size_t i = 0; i < chunk; i++)
                masked[i] = input[offset + i] ^ mask_key[(offset + i) % 4];
            if (ws_frame_write_all(conn, ctx, masked, chunk) != 0) {
                ws_fail(conn);
                goto done;
            }
            offset += chunk;
        }
    }
    if (opcode == NC_WS_OPCODE_CLOSE)
        (void)ws_shutdown_write(conn);
    if (ctx) neverc_context_cancel_handle_cancel(cancel);
    if (ctx) neverc_context_free(ctx);
    if (cancel) neverc_context_cancel_handle_free(cancel);
    if (background) neverc_context_free(background);
    nc_mutex_unlock(&conn->write_lock);
    return 0;

done:
    if (ctx) neverc_context_cancel_handle_cancel(cancel);
    if (ctx) neverc_context_free(ctx);
    if (cancel) neverc_context_cancel_handle_free(cancel);
    if (background) neverc_context_free(background);
    nc_mutex_unlock(&conn->write_lock);
    return -1;
}

static int write_frame(neverc_ws_conn_t *conn, int opcode, int fin,
                       const void *payload, size_t len) {
    return write_frame_timeout(conn, opcode, payload, len, fin, 0);
}

int neverc_ws_read_frame(neverc_ws_conn_t *conn, int *opcode, int *fin,
                          void *buf, size_t buflen, size_t *out_len) {
    if (!conn || !conn->tcp || nc_atomic_load(&conn->failed) ||
        nc_atomic_load(&conn->close_received) ||
        !opcode || !buf || !out_len)
        return -1;
    *out_len = 0;

    uint8_t wire_header[14];
    if (read_exact(conn, wire_header, 2) != 0) return ws_fail(conn);
    uint8_t length_code = wire_header[1] & 0x7f;
    size_t remaining_header = length_code == 126 ? 2U :
                              length_code == 127 ? 8U : 0U;
    if ((wire_header[1] & 0x80) != 0) remaining_header += 4U;
    if (read_exact(conn, wire_header + 2, remaining_header) != 0)
        return ws_fail(conn);
    ws_frame_header_t header;
    if (ws_parse_frame_header(wire_header, 2U + remaining_header,
                              conn->is_client, &header) != 0)
        return ws_fail_protocol(conn);
    int frame_opcode = header.opcode;
    int frame_fin = header.fin;
    int masked = header.masked;
    uint64_t plen = header.payload_length;
    if ((conn->read_limit && plen > conn->read_limit) ||
        (!conn->read_limit && plen > WS_MAX_DISCARD && plen > buflen)) {
        return ws_fail_too_big(conn);
    }

    uint8_t control_storage[125];
    void *payload_buf = buf;
    if (ws_control_opcode(frame_opcode) && plen > buflen) {
        payload_buf = control_storage;
    } else if (plen > buflen) {
        if (plen > WS_MAX_DISCARD)
            return ws_fail_too_big(conn);
        int track_data = frame_opcode == NC_WS_OPCODE_TEXT ||
                         frame_opcode == NC_WS_OPCODE_BINARY ||
                         frame_opcode == NC_WS_OPCODE_CONTINUATION;
        int track_text = 0;
        /* RFC 6455 §5.4: a new TEXT/BINARY while a fragment is open, or a
         * CONTINUATION with none, is 1002 even if the payload will not fit. */
        if (track_data) {
            if (ws_data_frame_begin(conn, frame_opcode, frame_fin) != 0)
                return -1;
            if (ws_data_message_account(conn, plen) != 0)
                return -1;
            track_text = conn->data_message_opcode == NC_WS_OPCODE_TEXT;
        }
        if (ws_discard_payload_tracked(conn, plen, masked, header.mask_key,
                                       track_text) != 0)
            return -1;
        if (track_data && ws_data_frame_end(conn, frame_fin) != 0)
            return -1;
        /* A complete oversized message can be abandoned. Resetting a
         * non-final first fragment would 1002 the peer's CONTINUATION. */
        if (track_data) {
            if (!frame_fin)
                return ws_fail_protocol(conn);
            ws_reset_data_fragment(conn);
        }
        return -1;
    }

    int track_data = frame_opcode == NC_WS_OPCODE_TEXT ||
                     frame_opcode == NC_WS_OPCODE_BINARY ||
                     frame_opcode == NC_WS_OPCODE_CONTINUATION;
    int track_text = 0;
    if (track_data) {
        if (ws_data_frame_begin(conn, frame_opcode, frame_fin) != 0)
            return -1;
        if (ws_data_message_account(conn, plen) != 0)
            return -1;
        track_text = conn->data_message_opcode == NC_WS_OPCODE_TEXT;
    }

    if (plen > 0) {
        if (track_data) {
            if (ws_read_payload(conn, payload_buf, (size_t)plen, masked,
                                header.mask_key, track_text) != 0)
                return -1;
        } else {
            if (read_exact(conn, payload_buf, (size_t)plen) != 0)
                return ws_fail(conn);
            if (masked) {
                uint8_t *p = (uint8_t *)payload_buf;
                size_t n = (size_t)plen;
                for (size_t i = 0; i < n; i++)
                    p[i] ^= header.mask_key[i % 4];
            }
        }
    }

    if (track_data) {
        if (ws_data_frame_end(conn, frame_fin) != 0)
            return -1;
    }

    if (frame_opcode == NC_WS_OPCODE_CLOSE) {
        const uint8_t *close_payload = (const uint8_t *)payload_buf;
        if (plen == 1)
            return ws_fail_protocol(conn);
        if (plen >= 2) {
            if (!ws_valid_close_code((uint16_t)((close_payload[0] << 8) |
                                               close_payload[1])))
                return ws_fail_protocol(conn);
            if (!ws_valid_utf8(close_payload + 2, (size_t)plen - 2))
                return ws_fail_invalid_payload(conn);
        }
        nc_atomic_store(&conn->close_received, 1);
        if (!nc_atomic_load(&conn->close_sent) &&
            write_frame(conn, NC_WS_OPCODE_CLOSE, 1, payload_buf,
                        (size_t)plen) != 0)
            return ws_fail(conn);
    } else if (frame_opcode == NC_WS_OPCODE_PING) {
        if (!nc_atomic_load(&conn->close_sent) &&
            write_frame(conn, NC_WS_OPCODE_PONG, 1, payload_buf,
                        (size_t)plen) != 0)
            return ws_fail(conn);
    } else if (frame_opcode == NC_WS_OPCODE_PONG) {
        nc_mutex_lock(&conn->keepalive_lock);
        if (conn->awaiting_pong && plen == sizeof(conn->ping_token) &&
            memcmp(payload_buf, conn->ping_token,
                   sizeof(conn->ping_token)) == 0) {
            conn->awaiting_pong = 0;
            conn->next_ping_ms = nc_monotonic_ms() +
                                 (uint64_t)conn->ping_interval_ms;
        }
        nc_mutex_unlock(&conn->keepalive_lock);
    }

    size_t copied = (size_t)plen;
    if (payload_buf != buf) {
        copied = 0;
        if (plen > 0 && buflen > 0) {
            copied = (size_t)plen < buflen ? (size_t)plen : buflen;
            memcpy(buf, payload_buf, copied);
        }
    }
    *opcode = frame_opcode;
    if (fin) *fin = frame_fin;
    *out_len = copied;
    return 0;
}

int neverc_ws_write_text(neverc_ws_conn_t *conn, const void *data, size_t len) {
    if (data && !ws_valid_utf8((const uint8_t *)data, len)) return -1;
    return write_frame(conn, NC_WS_OPCODE_TEXT, 1, data, len);
}

int neverc_ws_write_binary(neverc_ws_conn_t *conn, const void *data, size_t len) {
    return write_frame(conn, NC_WS_OPCODE_BINARY, 1, data, len);
}

int neverc_ws_send_ping(neverc_ws_conn_t *conn, const void *data, size_t len) {
    return write_frame(conn, NC_WS_OPCODE_PING, 1, data, len);
}

int neverc_ws_send_pong(neverc_ws_conn_t *conn, const void *data, size_t len) {
    return write_frame(conn, NC_WS_OPCODE_PONG, 1, data, len);
}

int neverc_ws_send_close(neverc_ws_conn_t *conn, uint16_t code,
                          const char *reason) {
    if (!conn || nc_atomic_load(&conn->close_sent) ||
        !ws_valid_close_code(code))
        return -1;
    uint8_t payload[125];
    size_t plen = 2;
    payload[0] = (uint8_t)((code >> 8) & 0xFF);
    payload[1] = (uint8_t)(code & 0xFF);
    if (reason) {
        size_t rlen = strlen(reason);
        if (rlen + 2 > sizeof(payload) ||
            !ws_valid_utf8((const uint8_t *)reason, rlen))
            return -1;
        memcpy(payload + 2, reason, rlen);
        plen += rlen;
    }
    return write_frame(conn, NC_WS_OPCODE_CLOSE, 1, payload, plen);
}

int neverc_ws_write_frame(neverc_ws_conn_t *conn, int opcode, int fin,
                          const void *data, size_t len) {
    if (!conn || !ws_valid_opcode(opcode) || ws_control_opcode(opcode))
        return -1;
    if (data && opcode == NC_WS_OPCODE_TEXT) {
        if (fin) {
            if (!ws_valid_utf8((const uint8_t *)data, len)) return -1;
        } else if (!ws_valid_utf8_prefix((const uint8_t *)data, len)) {
            return -1;
        }
    }
    return write_frame(conn, opcode, fin ? 1 : 0, data, len);
}

static int ws_read_data_message(neverc_ws_conn_t *conn, int *output_opcode,
                                void *output, size_t output_capacity,
                                size_t *output_length, int terminate) {
    if (!conn || !output_opcode || !output || !output_length) return -1;
    *output_length = 0;

    nc_buf_t acc;
    nc_buf_init(&acc);
    size_t frame_cap = output_capacity;
    if (conn->read_limit && frame_cap > conn->read_limit)
        frame_cap = conn->read_limit;
    size_t chunk_cap = frame_cap < 125 ? 125 : frame_cap;
    char *chunk = (char *)malloc(chunk_cap);
    if (!chunk) return -1;
    int fragmented = 0;
    int message_opcode = 0;

    for (;;) {
        int opcode = 0;
        int frame_fin = 0;
        size_t chunk_len = 0;
        int rc = neverc_ws_read_frame(conn, &opcode, &frame_fin, chunk,
                                       chunk_cap, &chunk_len);
        if (rc != 0) {
            free(chunk);
            nc_buf_free(&acc);
            return -1;
        }

        if (opcode == NC_WS_OPCODE_CLOSE) {
            free(chunk);
            nc_buf_free(&acc);
            return -1;
        }
        if (opcode == NC_WS_OPCODE_PING) {
            continue;
        }
        if (opcode == NC_WS_OPCODE_PONG)
            continue;

        if ((!fragmented && opcode == NC_WS_OPCODE_CONTINUATION) ||
            (fragmented && opcode != NC_WS_OPCODE_CONTINUATION) ||
            (!fragmented && opcode != NC_WS_OPCODE_TEXT &&
             opcode != NC_WS_OPCODE_BINARY)) {
            free(chunk);
            nc_buf_free(&acc);
            return ws_fail_protocol(conn);
        }
        if (!fragmented) message_opcode = opcode;

        if (acc.len > output_capacity ||
            chunk_len > output_capacity - acc.len ||
            (conn->read_limit &&
             (acc.len > conn->read_limit ||
              chunk_len > conn->read_limit - acc.len)) ||
            nc_buf_append(&acc, chunk, chunk_len) != 0) {
            ws_reset_data_fragment(conn);
            free(chunk);
            nc_buf_free(&acc);
            return -1;
        }

        if (message_opcode == NC_WS_OPCODE_TEXT &&
            !ws_valid_utf8_prefix((const uint8_t *)acc.data, acc.len)) {
            free(chunk);
            nc_buf_free(&acc);
            return ws_fail_invalid_payload(conn);
        }

        if (frame_fin) {
            if (message_opcode == NC_WS_OPCODE_TEXT &&
                !ws_valid_utf8((const uint8_t *)acc.data, acc.len)) {
                free(chunk);
                nc_buf_free(&acc);
                return ws_fail_invalid_payload(conn);
            }
            if (acc.len) memcpy(output, acc.data, acc.len);
            if (terminate) ((char *)output)[acc.len] = '\0';
            *output_opcode = message_opcode;
            *output_length = acc.len;
            free(chunk);
            nc_buf_free(&acc);
            return 0;
        }
        fragmented = 1;
    }
}

int neverc_ws_read_data_message(neverc_ws_conn_t *conn, int *opcode,
                                void *output, size_t output_capacity,
                                size_t *output_length) {
    return ws_read_data_message(conn, opcode, output, output_capacity,
                                output_length, 0);
}

int neverc_ws_read_message(neverc_ws_conn_t *conn, char *buf, size_t buflen,
                            size_t *out_len) {
    if (!buf || buflen == 0) return -1;
    int opcode = 0;
    int rc = ws_read_data_message(conn, &opcode, buf, buflen - 1U, out_len, 1);
    if (rc != 0) return rc;
    /* Documented as a complete text message. BINARY (including non-UTF-8)
     * must not be coerced into a C string. */
    if (opcode != NC_WS_OPCODE_TEXT) {
        if (out_len) *out_len = 0;
        return ws_fail_protocol(conn);
    }
    return 0;
}

int neverc_ws_write_message(neverc_ws_conn_t *conn, const char *msg) {
    if (!msg) return -1;
    return neverc_ws_write_text(conn, msg, strlen(msg));
}

int neverc_ws_valid_utf8(const void *data, size_t len) {
    return data ? ws_valid_utf8((const uint8_t *)data, len) : (len == 0);
}

int neverc_ws_valid_utf8_prefix(const void *data, size_t len) {
    return data ? ws_valid_utf8_prefix((const uint8_t *)data, len) : (len == 0);
}
