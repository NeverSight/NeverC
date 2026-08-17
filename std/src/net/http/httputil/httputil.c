#include "neverc/std/net/http/httputil.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/netip.h"
#include "neverc/std/net/tcp.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifndef _WIN32
#include <strings.h>
#else
static int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
#endif

static int httputil_dump_append(char **buf, size_t *length, size_t *capacity,
                                const void *data, size_t data_len) {
    if (!buf || !length || !capacity || (!data && data_len != 0) ||
        data_len == SIZE_MAX || *length > SIZE_MAX - data_len - 1U)
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
        if (!grown)
            return -1;
        *buf = grown;
        *capacity = next;
    }
    if (data_len != 0)
        memcpy(*buf + *length, data, data_len);
    *length += data_len;
    (*buf)[*length] = '\0';
    return 0;
}

static int httputil_dump_append_string(char **buf, size_t *length,
                                       size_t *capacity, const char *value) {
    return value ? httputil_dump_append(
        buf, length, capacity, value, strlen(value)) : -1;
}

static int httputil_has_crlf(const char *s) {
    if (!s) return 0;
    for (; *s; s++)
        if (*s == '\r' || *s == '\n') return 1;
    return 0;
}

/* A dump header block may contain CRLF line breaks, but a blank line would
 * terminate headers and smuggle a second request. Lone CR/LF is also rejected.
 * A final line without CRLF is accepted; the dumper appends one. */
static int httputil_header_block_valid(const char *headers, int *needs_crlf) {
    if (needs_crlf) *needs_crlf = 0;
    if (!headers || !*headers) return 1;
    size_t line = 0;
    for (const char *p = headers; *p; ) {
        if (p[0] == '\r' && p[1] == '\n') {
            if (line == 0) return 0;
            p += 2;
            line = 0;
            continue;
        }
        if (*p == '\r' || *p == '\n') return 0;
        line++;
        p++;
    }
    if (needs_crlf) *needs_crlf = line > 0;
    return 1;
}

/* RFC 9110 Host is uri-host; IPv6 literals must be bracketed. */
static int httputil_host_needs_brackets(const char *host) {
    neverc_netip_addr_t addr;
    return host && host[0] && host[0] != '[' &&
        neverc_netip_parse_addr(host, &addr) == 0 && !addr.is_v4;
}

static int httputil_headers_have_content_length(const char *headers) {
    if (!headers) return 0;
    const char *line = headers;
    while (*line) {
        const char *end = line;
        while (*end && *end != '\n') end++;
        size_t length = (size_t)(end - line);
        if (length > 0 && line[length - 1] == '\r') length--;
        if (length >= 15) {
            static const char prefix[] = "content-length:";
            int match = 1;
            for (int i = 0; i < 15; i++) {
                unsigned char c = (unsigned char)line[i];
                if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
                if (c != (unsigned char)prefix[i]) {
                    match = 0;
                    break;
                }
            }
            if (match) return 1;
        }
        if (*end == '\0') break;
        line = end + 1;
    }
    return 0;
}

/* ======================================================================
 * Reverse Proxy
 * ====================================================================== */

struct neverc_httputil_reverse_proxy {
    char target_host[256];
    char target_authority[280];
    uint16_t target_port;
    char target_path[2048];
    int target_is_ipv6;
    int use_tls;
    int set_forwarded;
    char forwarded_proto[6];
    int legacy_slot;
    size_t legacy_active;
    int legacy_released;

    neverc_httputil_rewrite_func_t rewrite_func;
    void *rewrite_data;

    neverc_httputil_error_handler_t error_handler;
    void *error_data;
};

#define PROXY_HEADER_LIMIT NEVERC_HTTPUTIL_PROXY_HEADER_LIMIT
#define PROXY_BODY_LIMIT NEVERC_HTTPUTIL_PROXY_BODY_LIMIT
#define PROXY_BACKEND_TIMEOUT_MS \
    NEVERC_HTTPUTIL_PROXY_BACKEND_TIMEOUT_MS
#define PROXY_CHUNK_LINE_LIMIT 8192U
#define PROXY_RESPONSE_MAX_FIELDS 64
#define PROXY_MAX_REQUEST_FIELDS 1024
#define PROXY_LEGACY_SLOT_COUNT 64

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} proxy_buffer_t;

typedef struct {
    neverc_tcp_conn_t *tcp;
    neverc_tls_conn_t *tls;
} proxy_transport_t;

typedef struct {
    proxy_transport_t *transport;
    neverc_context_t *context;
    unsigned char data[8192];
    size_t pos;
    size_t len;
} proxy_reader_t;

typedef struct {
    int status_code;
    int has_content_length;
    size_t content_length;
    int is_chunked;
} proxy_response_framing_t;

static _Atomic(neverc_httputil_reverse_proxy_t *)
    g_legacy_slots[PROXY_LEGACY_SLOT_COUNT];
static size_t g_legacy_next_slot;
static atomic_flag g_legacy_lock = ATOMIC_FLAG_INIT;

static void proxy_legacy_lock(void);
static void proxy_legacy_unlock(void);

static void proxy_buffer_free(proxy_buffer_t *buffer) {
    if (!buffer) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static int proxy_buffer_append(proxy_buffer_t *buffer, const void *data,
                               size_t length, size_t limit) {
    if (!buffer || (!data && length != 0) || buffer->len > limit ||
        length > limit - buffer->len ||
        buffer->len > SIZE_MAX - length - 1U)
        return -1;
    size_t required = buffer->len + length + 1U;
    if (required > buffer->cap) {
        size_t next = buffer->cap < 512U ? 512U : buffer->cap;
        while (next < required) {
            if (next > SIZE_MAX / 2U) {
                next = required;
                break;
            }
            next *= 2U;
            if (next > limit + 1U) {
                next = limit + 1U;
                break;
            }
        }
        char *grown = (char *)realloc(buffer->data, next);
        if (!grown) return -1;
        buffer->data = grown;
        buffer->cap = next;
    }
    if (length != 0)
        memcpy(buffer->data + buffer->len, data, length);
    buffer->len += length;
    buffer->data[buffer->len] = '\0';
    return 0;
}

static int proxy_buffer_append_string(proxy_buffer_t *buffer,
                                      const char *value, size_t limit) {
    return value
        ? proxy_buffer_append(buffer, value, strlen(value), limit) : -1;
}

static unsigned char proxy_ascii_lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int proxy_equal_ci_n(const char *left, size_t left_length,
                            const char *right) {
    size_t right_length = strlen(right);
    if (!left || left_length != right_length) return 0;
    for (size_t i = 0; i < left_length; i++)
        if (proxy_ascii_lower((unsigned char)left[i]) !=
            proxy_ascii_lower((unsigned char)right[i]))
            return 0;
    return 1;
}

static int proxy_equal_ci_ranges(const char *left, size_t left_length,
                                 const char *right,
                                 size_t right_length) {
    if (!left || !right || left_length != right_length) return 0;
    for (size_t i = 0; i < left_length; i++)
        if (proxy_ascii_lower((unsigned char)left[i]) !=
            proxy_ascii_lower((unsigned char)right[i]))
            return 0;
    return 1;
}

static int proxy_prefix_ci(const char *value, const char *prefix,
                           size_t prefix_length) {
    if (!value || !prefix) return 0;
    for (size_t i = 0; i < prefix_length; i++) {
        if (value[i] == '\0' ||
            proxy_ascii_lower((unsigned char)value[i]) !=
                proxy_ascii_lower((unsigned char)prefix[i]))
            return 0;
    }
    return 1;
}

static int proxy_is_token_char(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '!' || c == '#' || c == '$' ||
           c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
           c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
           c == '|' || c == '~';
}

static int proxy_valid_token(const char *value, size_t length) {
    if (!value || length == 0) return 0;
    for (size_t i = 0; i < length; i++)
        if (!proxy_is_token_char((unsigned char)value[i])) return 0;
    return 1;
}

static int proxy_valid_field_value(const char *value, size_t length) {
    if (!value && length != 0) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) return 0;
    }
    return 1;
}

static void proxy_trim_ows(const char **value, size_t *length) {
    while (*length > 0 && (**value == ' ' || **value == '\t')) {
        (*value)++;
        (*length)--;
    }
    while (*length > 0 &&
           ((*value)[*length - 1] == ' ' ||
            (*value)[*length - 1] == '\t'))
        (*length)--;
}

static int proxy_parse_decimal(const char *value, size_t length,
                               size_t *result) {
    if (!value || !result || length == 0) return -1;
    size_t parsed = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < '0' || c > '9' ||
            parsed > (SIZE_MAX - (size_t)(c - '0')) / 10U)
            return -1;
        parsed = parsed * 10U + (size_t)(c - '0');
    }
    *result = parsed;
    return 0;
}

static int proxy_parse_port(const char *start, const char *end,
                            uint16_t *port) {
    if (!start || !end || !port || start == end) return -1;
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

static int proxy_valid_reg_name(const char *host, size_t length) {
    if (!host || length == 0) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)host[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '.' || c == '-' ||
              c == '_'))
            return 0;
    }
    return 1;
}

static int proxy_valid_ipv4_tail(const char *value, size_t length) {
    size_t offset = 0;
    int parts = 0;
    while (offset < length) {
        size_t start = offset;
        unsigned part = 0;
        while (offset < length && value[offset] >= '0' &&
               value[offset] <= '9') {
            part = part * 10U + (unsigned)(value[offset] - '0');
            if (part > 255U || offset - start >= 3U) return 0;
            offset++;
        }
        if (offset == start || ++parts > 4) return 0;
        if (offset == length) break;
        if (value[offset++] != '.' || offset == length) return 0;
    }
    return parts == 4;
}

static int proxy_valid_bracketed_ipv6(const char *host, size_t length) {
    if (!host || length < 2) return 0;
    size_t offset = 0;
    int groups = 0;
    int compressed = 0;
    if (host[0] == ':') {
        if (host[1] != ':') return 0;
        compressed = 1;
        offset = 2;
        if (offset == length) return 1;
    }

    while (offset < length) {
        size_t start = offset;
        while (offset < length && host[offset] != ':') offset++;
        size_t group_length = offset - start;
        if (group_length == 0) return 0;
        if (memchr(host + start, '.', group_length)) {
            if (offset != length ||
                !proxy_valid_ipv4_tail(host + start, group_length))
                return 0;
            groups += 2;
            break;
        }
        if (group_length > 4) return 0;
        for (size_t i = start; i < offset; i++) {
            unsigned char c = (unsigned char)host[i];
            if (!((c >= '0' && c <= '9') ||
                  (c >= 'A' && c <= 'F') ||
                  (c >= 'a' && c <= 'f')))
                return 0;
        }
        if (++groups > 8) return 0;
        if (offset == length) break;
        if (offset + 1U < length && host[offset + 1U] == ':') {
            if (compressed) return 0;
            compressed = 1;
            offset += 2U;
            if (offset == length) break;
        } else {
            offset++;
            if (offset == length) return 0;
        }
    }
    return compressed ? groups < 8 : groups == 8;
}

static int parse_target_url(const char *url,
                             char *host, size_t hostlen,
                             char *authority, size_t authoritylen,
                             uint16_t *port, char *path, size_t pathlen,
                             int *use_tls, int *is_ipv6) {
    if (!url || !host || hostlen == 0 || !authority ||
        authoritylen == 0 || !port || !path || pathlen == 0 ||
        !use_tls || !is_ipv6)
        return -1;

    *use_tls = 0;
    *is_ipv6 = 0;
    const char *p = url;

    if (proxy_prefix_ci(p, "https://", 8)) {
        *use_tls = 1;
        p += 8;
        *port = 443;
    } else if (proxy_prefix_ci(p, "http://", 7)) {
        p += 7;
        *port = 80;
    } else {
        return -1;
    }

    const char *end = p + strlen(p);
    for (const char *cursor = p; cursor < end; cursor++) {
        unsigned char c = (unsigned char)*cursor;
        if (c <= 0x20 || c == 0x7f || c == '\\' ||
            c == '#' || c == '?')
            return -1;
    }

    const char *authority_end = strchr(p, '/');
    if (!authority_end) authority_end = end;
    if (authority_end == p ||
        memchr(p, '@', (size_t)(authority_end - p)) != NULL)
        return -1;

    int explicit_port = 0;
    size_t host_length = 0;
    if (*p == '[') {
        const char *bracket = (const char *)memchr(
            p + 1, ']', (size_t)(authority_end - p - 1));
        if (!bracket) return -1;
        host_length = (size_t)(bracket - p - 1);
        if (host_length >= hostlen ||
            !proxy_valid_bracketed_ipv6(p + 1, host_length))
            return -1;
        memcpy(host, p + 1, host_length);
        host[host_length] = '\0';
        *is_ipv6 = 1;
        if (bracket + 1 < authority_end) {
            if (bracket[1] != ':' ||
                proxy_parse_port(bracket + 2, authority_end, port) != 0)
                return -1;
            explicit_port = 1;
        } else if (bracket + 1 != authority_end) {
            return -1;
        }
    } else {
        const char *colon = (const char *)memchr(
            p, ':', (size_t)(authority_end - p));
        if (colon && memchr(colon + 1, ':',
                            (size_t)(authority_end - colon - 1)))
            return -1;
        const char *host_end = colon ? colon : authority_end;
        host_length = (size_t)(host_end - p);
        if (host_length >= hostlen ||
            !proxy_valid_reg_name(p, host_length))
            return -1;
        memcpy(host, p, host_length);
        host[host_length] = '\0';
        if (colon) {
            if (proxy_parse_port(colon + 1, authority_end, port) != 0)
                return -1;
            explicit_port = 1;
        }
    }

    int authority_length;
    if (*is_ipv6) {
        authority_length = explicit_port
            ? snprintf(authority, authoritylen, "[%s]:%u", host,
                       (unsigned)*port)
            : snprintf(authority, authoritylen, "[%s]", host);
    } else {
        authority_length = explicit_port
            ? snprintf(authority, authoritylen, "%s:%u", host,
                       (unsigned)*port)
            : snprintf(authority, authoritylen, "%s", host);
    }
    if (authority_length < 0 ||
        (size_t)authority_length >= authoritylen)
        return -1;

    size_t path_length = (size_t)(end - authority_end);
    if (path_length >= pathlen) return -1;
    if (path_length > 0) {
        if (*authority_end != '/') return -1;
        memcpy(path, authority_end, path_length);
    }
    path[path_length] = '\0';
    return 0;
}

static int proxy_token_list(const char *value, const char *wanted,
                            size_t wanted_length, int *contains) {
    if (!value || !contains) return -1;
    *contains = 0;
    size_t length = strlen(value);
    size_t offset = 0;
    int count = 0;
    while (offset < length) {
        while (offset < length &&
               (value[offset] == ' ' || value[offset] == '\t'))
            offset++;
        size_t start = offset;
        while (offset < length &&
               proxy_is_token_char((unsigned char)value[offset]))
            offset++;
        size_t token_length = offset - start;
        if (token_length == 0) return -1;
        while (offset < length &&
               (value[offset] == ' ' || value[offset] == '\t'))
            offset++;
        if (wanted &&
            proxy_equal_ci_ranges(
                value + start, token_length, wanted, wanted_length))
            *contains = 1;
        count++;
        if (offset == length) break;
        if (value[offset] != ',') return -1;
        offset++;
        if (offset == length) return -1;
    }
    return count > 0 ? 0 : -1;
}

static const char *proxy_next_request_header(
    const char *cursor, const char **name, size_t *name_length,
    const char **value, size_t *value_length) {
    *name = cursor;
    *name_length = strlen(cursor);
    cursor += *name_length + 1U;
    *value = cursor;
    *value_length = strlen(cursor);
    return cursor + *value_length + 1U;
}

static int proxy_validate_request_headers(
    const neverc_http_request_t *request) {
    if (!request || request->nheaders < 0 ||
        request->nheaders > PROXY_MAX_REQUEST_FIELDS ||
        (request->nheaders > 0 && !request->raw_headers))
        return -1;
    const char *cursor = request->raw_headers;
    for (int i = 0; i < request->nheaders; i++) {
        const char *name;
        const char *value;
        size_t name_length;
        size_t value_length;
        cursor = proxy_next_request_header(
            cursor, &name, &name_length, &value, &value_length);
        if (!proxy_valid_token(name, name_length) ||
            !proxy_valid_field_value(value, value_length))
            return -1;
        if (proxy_equal_ci_n(name, name_length, "Connection")) {
            int ignored = 0;
            if (proxy_token_list(value, NULL, 0, &ignored) != 0)
                return -1;
        }
    }
    return 0;
}

static int proxy_request_header_named_by_connection(
    const neverc_http_request_t *request,
    const char *name, size_t name_length) {
    const char *cursor = request->raw_headers;
    for (int i = 0; i < request->nheaders; i++) {
        const char *candidate;
        const char *value;
        size_t candidate_length;
        size_t value_length;
        cursor = proxy_next_request_header(
            cursor, &candidate, &candidate_length, &value, &value_length);
        (void)value_length;
        if (proxy_equal_ci_n(candidate, candidate_length, "Connection")) {
            int contains = 0;
            if (proxy_token_list(value, name, name_length, &contains) == 0 &&
                contains)
                return 1;
        }
    }
    return 0;
}

static int proxy_is_hop_header(const char *name, size_t length) {
    return proxy_equal_ci_n(name, length, "Connection") ||
           proxy_equal_ci_n(name, length, "Proxy-Connection") ||
           proxy_equal_ci_n(name, length, "Keep-Alive") ||
           proxy_equal_ci_n(name, length, "Proxy-Authenticate") ||
           proxy_equal_ci_n(name, length, "Proxy-Authorization") ||
           proxy_equal_ci_n(name, length, "TE") ||
           proxy_equal_ci_n(name, length, "Trailer") ||
           proxy_equal_ci_n(name, length, "Transfer-Encoding") ||
           proxy_equal_ci_n(name, length, "Upgrade");
}

static int proxy_is_forwarded_header(const char *name, size_t length) {
    static const char prefix[] = "X-Forwarded-";
    if (proxy_equal_ci_n(name, length, "Forwarded")) return 1;
    if (length < sizeof(prefix) - 1U) return 0;
    for (size_t i = 0; i < sizeof(prefix) - 1U; i++)
        if (proxy_ascii_lower((unsigned char)name[i]) !=
            proxy_ascii_lower((unsigned char)prefix[i]))
            return 0;
    return 1;
}

static int proxy_hex_nibble(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    return -1;
}

/* Reject "." / ".." (including %2e) and encoded slashes so a target prefix
 * such as /api cannot be escaped as /api/../admin. "//" is scheme-relative. */
static int proxy_request_segment_unsafe(const char *seg, size_t len) {
    char decoded[4];
    size_t decoded_length = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)seg[i];
        if (c == '%') {
            if (i + 2 >= len) return 1;
            int high = proxy_hex_nibble((unsigned char)seg[i + 1]);
            int low = proxy_hex_nibble((unsigned char)seg[i + 2]);
            if ((high | low) < 0) return 1;
            c = (unsigned char)((high << 4) | low);
            i += 2;
        }
        if (c <= 0x20 || c == 0x7f || c == '/' || c == '\\') return 1;
        if (decoded_length < sizeof(decoded))
            decoded[decoded_length] = (char)c;
        decoded_length++;
    }
    return (decoded_length == 1 && decoded[0] == '.') ||
           (decoded_length == 2 && decoded[0] == '.' && decoded[1] == '.');
}

static int proxy_valid_request_target(const char *value) {
    if (!value || value[0] != '/' || value[1] == '/') return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p <= 0x20 || *p == 0x7f || *p == '#' || *p == '?' ||
            *p == '\\')
            return 0;
    const char *seg = value + 1;
    while (*seg) {
        const char *slash = strchr(seg, '/');
        size_t seg_len = slash ? (size_t)(slash - seg) : strlen(seg);
        if (proxy_request_segment_unsafe(seg, seg_len)) return 0;
        if (!slash) break;
        seg = slash + 1;
    }
    return 1;
}

static int proxy_valid_query(const char *value) {
    if (!value) return 1;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p <= 0x20 || *p == 0x7f || *p == '#') return 0;
    return 1;
}

static int proxy_valid_forwarded_host(const char *value) {
    if (!value || value[0] == '\0') return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p <= 0x20 || *p == 0x7f || *p == ',') return 0;
    return 1;
}

static int proxy_build_request(
    const neverc_httputil_reverse_proxy_t *proxy,
    const neverc_http_request_t *original,
    const neverc_http_request_t *outbound,
    proxy_buffer_t *headers) {
    if (!proxy || !original || !outbound || !headers ||
        !outbound->method ||
        !proxy_valid_token(outbound->method, strlen(outbound->method)) ||
        (outbound->body_len > 0 && !outbound->body) ||
        outbound->body_len > PROXY_BODY_LIMIT ||
        proxy_validate_request_headers(outbound) != 0)
        return -1;

    const char *path = outbound->path ? outbound->path : "/";
    int asterisk_target = strcmp(path, "*") == 0;
    if (asterisk_target) {
        if (strcmp(outbound->method, "OPTIONS") != 0 ||
            (outbound->query && outbound->query[0] != '\0'))
            return -1;
    } else if (!proxy_valid_request_target(path) ||
               !proxy_valid_query(outbound->query)) {
        return -1;
    }
    if (proxy->set_forwarded && original->host &&
        !proxy_valid_forwarded_host(original->host))
        return -1;

    if (proxy_buffer_append_string(
            headers, outbound->method, PROXY_HEADER_LIMIT) != 0 ||
        proxy_buffer_append_string(headers, " ", PROXY_HEADER_LIMIT) != 0)
        return -1;

    if (!asterisk_target && proxy->target_path[0]) {
        size_t base_length = strlen(proxy->target_path);
        int base_slash = proxy->target_path[base_length - 1] == '/';
        int path_slash = path[0] == '/';
        if (proxy_buffer_append(
                headers, proxy->target_path, base_length,
                PROXY_HEADER_LIMIT) != 0)
            return -1;
        if (base_slash && path_slash) path++;
        else if (!base_slash && !path_slash &&
                 proxy_buffer_append_string(
                     headers, "/", PROXY_HEADER_LIMIT) != 0)
            return -1;
    }
    if (proxy_buffer_append_string(headers, path, PROXY_HEADER_LIMIT) != 0)
        return -1;
    if (outbound->query && outbound->query[0] &&
        (proxy_buffer_append_string(
             headers, "?", PROXY_HEADER_LIMIT) != 0 ||
         proxy_buffer_append_string(
             headers, outbound->query, PROXY_HEADER_LIMIT) != 0))
        return -1;
    if (proxy_buffer_append_string(
            headers, " HTTP/1.1\r\nHost: ", PROXY_HEADER_LIMIT) != 0 ||
        proxy_buffer_append_string(
            headers, proxy->target_authority, PROXY_HEADER_LIMIT) != 0 ||
        proxy_buffer_append_string(
            headers, "\r\n", PROXY_HEADER_LIMIT) != 0)
        return -1;

    const char *cursor = outbound->raw_headers;
    for (int i = 0; i < outbound->nheaders; i++) {
        const char *name;
        const char *value;
        size_t name_length;
        size_t value_length;
        cursor = proxy_next_request_header(
            cursor, &name, &name_length, &value, &value_length);
        if (proxy_equal_ci_n(name, name_length, "Host") ||
            proxy_equal_ci_n(name, name_length, "Content-Length") ||
            proxy_is_hop_header(name, name_length) ||
            proxy_request_header_named_by_connection(
                outbound, name, name_length) ||
            (proxy->set_forwarded &&
             proxy_is_forwarded_header(name, name_length)))
            continue;
        if (proxy_buffer_append(
                headers, name, name_length, PROXY_HEADER_LIMIT) != 0 ||
            proxy_buffer_append_string(
                headers, ": ", PROXY_HEADER_LIMIT) != 0 ||
            proxy_buffer_append(
                headers, value, value_length, PROXY_HEADER_LIMIT) != 0 ||
            proxy_buffer_append_string(
                headers, "\r\n", PROXY_HEADER_LIMIT) != 0)
            return -1;
    }

    if (proxy->set_forwarded) {
        if (original->host &&
            (proxy_buffer_append_string(
                 headers, "X-Forwarded-Host: ", PROXY_HEADER_LIMIT) != 0 ||
             proxy_buffer_append_string(
                 headers, original->host, PROXY_HEADER_LIMIT) != 0 ||
             proxy_buffer_append_string(
                 headers, "\r\n", PROXY_HEADER_LIMIT) != 0))
            return -1;
        if (proxy_buffer_append_string(
                headers, "X-Forwarded-Proto: ", PROXY_HEADER_LIMIT) != 0 ||
            proxy_buffer_append_string(
                headers, proxy->forwarded_proto,
                PROXY_HEADER_LIMIT) != 0 ||
            proxy_buffer_append_string(
                headers, "\r\n", PROXY_HEADER_LIMIT) != 0)
            return -1;
    }

    char content_length[80];
    int content_length_size = snprintf(
        content_length, sizeof(content_length),
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        outbound->body_len);
    if (content_length_size < 0 ||
        (size_t)content_length_size >= sizeof(content_length) ||
        proxy_buffer_append(
            headers, content_length, (size_t)content_length_size,
            PROXY_HEADER_LIMIT) != 0)
        return -1;
    return 0;
}

static void proxy_transport_close(proxy_transport_t *transport) {
    if (!transport) return;
    if (transport->tls) {
        /* Abort the write side first so neverc_tls_close cannot block while
         * trying to emit close_notify to an unresponsive backend. The TLS
         * wrapper borrows transport->tcp, which is closed exactly once below. */
        (void)neverc_tls_shutdown_write(transport->tls);
        neverc_tls_close(transport->tls);
        transport->tls = NULL;
    }
    if (transport->tcp) {
        neverc_tcp_close(transport->tcp);
    }
    memset(transport, 0, sizeof(*transport));
}

static const char *proxy_net_error(
    const neverc_net_result_t *result, const char *fallback) {
    if (!result) return fallback;
    if (result->status == NEVERC_NET_TIMEOUT)
        return "backend operation timed out";
    if (result->status == NEVERC_NET_CANCELLED)
        return "backend operation canceled";
    if (result->status == NEVERC_NET_RESOLVE)
        return "backend name resolution failed";
    return fallback;
}

static int proxy_transport_dial(
    const neverc_httputil_reverse_proxy_t *proxy,
    neverc_context_t *context, proxy_transport_t *transport,
    const char **error) {
    if (error) *error = NULL;
    if (!proxy || !context || !transport) {
        if (error) *error = "invalid backend transport";
        return -1;
    }
    if (neverc_context_done(context)) {
        if (error)
            *error = neverc_context_err(context)
                ? neverc_context_err(context)
                : "backend operation canceled";
        return -1;
    }
    memset(transport, 0, sizeof(*transport));
    char address[300];
    int address_length = proxy->target_is_ipv6
        ? snprintf(address, sizeof(address), "[%s]:%u",
                   proxy->target_host, (unsigned)proxy->target_port)
        : snprintf(address, sizeof(address), "%s:%u",
                   proxy->target_host, (unsigned)proxy->target_port);
    if (address_length < 0 || (size_t)address_length >= sizeof(address)) {
        if (error) *error = "backend address is too long";
        return -1;
    }

    neverc_net_result_t dial_result = neverc_tcp_dial_context(
        address, context, &transport->tcp);
    if (dial_result.status != NEVERC_NET_OK || !transport->tcp) {
        const char *reason = proxy_net_error(
            &dial_result, "backend connection failed");
        proxy_transport_close(transport);
        if (error)
            *error = reason;
        return -1;
    }
    int64_t deadline = neverc_context_deadline(context);
    if (deadline <= 0 ||
        neverc_tcp_set_read_deadline(
            transport->tcp, deadline) != 0 ||
        neverc_tcp_set_write_deadline(
            transport->tcp, deadline) != 0) {
        proxy_transport_close(transport);
        if (error) *error = "failed to configure backend deadline";
        return -1;
    }
    if (!proxy->use_tls) return 0;

    neverc_tls_config_t *config = neverc_tls_config_new();
    if (!config) {
        proxy_transport_close(transport);
        if (error) *error = "failed to allocate TLS configuration";
        return -1;
    }
    neverc_tls_config_set_server_name(config, proxy->target_host);
    const char *protocols[] = {"http/1.1"};
    neverc_tls_config_set_alpn(config, protocols, 1);
    transport->tls = neverc_tls_client_context(
        transport->tcp, config, context, error);
    neverc_tls_config_free(config);
    if (!transport->tls) {
        proxy_transport_close(transport);
        return -1;
    }
    const char *negotiated = neverc_tls_alpn(transport->tls);
    if (negotiated && strcmp(negotiated, "http/1.1") != 0) {
        proxy_transport_close(transport);
        if (error) *error = "backend selected unsupported ALPN";
        return -1;
    }
    return 0;
}

static int proxy_transport_write_all(proxy_transport_t *transport,
                                     neverc_context_t *context,
                                     const void *data, size_t length) {
    if (!transport || !context || (!data && length != 0)) return -1;
    size_t written = 0;
    while (written < length) {
        size_t remaining = length - written;
        size_t chunk = remaining > (size_t)INT_MAX
            ? (size_t)INT_MAX : remaining;
        if (transport->tls) {
            int result = neverc_tls_write_context(
                transport->tls, context,
                (const char *)data + written, chunk);
            if (result <= 0 || (size_t)result > chunk) return -1;
            written += (size_t)result;
        } else {
            neverc_net_result_t result = neverc_tcp_write_context(
                transport->tcp, context,
                (const char *)data + written, chunk);
            if (result.status != NEVERC_NET_OK ||
                result.transferred == 0 ||
                result.transferred > chunk)
                return -1;
            written += result.transferred;
        }
    }
    return 0;
}

static int proxy_transport_read(proxy_transport_t *transport,
                                neverc_context_t *context,
                                void *data, size_t capacity) {
    if (!transport || !context || !data || capacity == 0 ||
        capacity > (size_t)INT_MAX)
        return -1;
    if (transport->tls)
        return neverc_tls_read_context(
            transport->tls, context, data, capacity);
    neverc_net_result_t result = neverc_tcp_read_context(
        transport->tcp, context, data, capacity);
    if (result.status == NEVERC_NET_EOF) return 0;
    if (result.status != NEVERC_NET_OK ||
        result.transferred > (size_t)INT_MAX)
        return -1;
    return (int)result.transferred;
}

static int proxy_reader_fill(proxy_reader_t *reader) {
    if (!reader || !reader->transport) return -1;
    if (reader->pos < reader->len)
        return (int)(reader->len - reader->pos);
    reader->pos = 0;
    reader->len = 0;
    int result = proxy_transport_read(
        reader->transport, reader->context,
        reader->data, sizeof(reader->data));
    if (result <= 0) return result;
    if ((size_t)result > sizeof(reader->data)) return -1;
    reader->len = (size_t)result;
    return result;
}

static int proxy_reader_byte(proxy_reader_t *reader,
                             unsigned char *value) {
    if (!reader || !value) return -1;
    if (reader->pos == reader->len) {
        int result = proxy_reader_fill(reader);
        if (result <= 0) return result;
    }
    *value = reader->data[reader->pos++];
    return 1;
}

static int proxy_reader_header_block(proxy_reader_t *reader,
                                     proxy_buffer_t *headers) {
    while (headers->len < PROXY_HEADER_LIMIT) {
        unsigned char byte = 0;
        if (proxy_reader_byte(reader, &byte) != 1 ||
            proxy_buffer_append(
                headers, &byte, 1, PROXY_HEADER_LIMIT) != 0)
            return -1;
        if (headers->len >= 4 &&
            memcmp(headers->data + headers->len - 4,
                   "\r\n\r\n", 4) == 0)
            return 0;
    }
    return -1;
}

static int proxy_reader_exact_append(proxy_reader_t *reader,
                                     proxy_buffer_t *output,
                                     size_t length, size_t limit) {
    size_t remaining = length;
    while (remaining > 0) {
        if (reader->pos == reader->len) {
            int result = proxy_reader_fill(reader);
            if (result <= 0) return -1;
        }
        size_t available = reader->len - reader->pos;
        size_t take = available < remaining ? available : remaining;
        if (proxy_buffer_append(
                output, reader->data + reader->pos, take, limit) != 0)
            return -1;
        reader->pos += take;
        remaining -= take;
    }
    return 0;
}

static int proxy_reader_line(proxy_reader_t *reader, char *line,
                             size_t capacity, size_t *length) {
    if (!reader || !line || capacity == 0 || !length) return -1;
    size_t used = 0;
    for (;;) {
        unsigned char byte = 0;
        if (proxy_reader_byte(reader, &byte) != 1) return -1;
        if (byte == '\n') return -1;
        if (byte == '\r') {
            unsigned char next = 0;
            if (proxy_reader_byte(reader, &next) != 1 || next != '\n')
                return -1;
            line[used] = '\0';
            *length = used;
            return 0;
        }
        if (used + 1U >= capacity) return -1;
        line[used++] = (char)byte;
    }
}

static const char *proxy_find_crlf(const char *start, const char *end) {
    for (const char *p = start; p + 1 < end; p++)
        if (p[0] == '\r' && p[1] == '\n') return p;
    return NULL;
}

static int proxy_parse_content_length_value(
    const char *value, size_t length, proxy_response_framing_t *framing) {
    proxy_trim_ows(&value, &length);
    size_t parsed = 0;
    if (!value || length == 0 ||
        proxy_parse_decimal(value, length, &parsed) != 0)
        return -1;
    framing->has_content_length = 1;
    framing->content_length = parsed;
    return 0;
}

static int proxy_parse_response_headers(
    const proxy_buffer_t *headers, proxy_response_framing_t *framing) {
    if (!headers || !headers->data || headers->len < 4 || !framing ||
        memcmp(headers->data + headers->len - 4, "\r\n\r\n", 4) != 0)
        return -1;
    memset(framing, 0, sizeof(*framing));
    const char *start = headers->data;
    const char *end = headers->data + headers->len;
    const char *line_end = proxy_find_crlf(start, end);
    size_t line_length = line_end ? (size_t)(line_end - start) : 0;
    if (!line_end || line_length < 13 ||
        line_length > PROXY_CHUNK_LINE_LIMIT ||
        (memcmp(start, "HTTP/1.1 ", 9) != 0 &&
         memcmp(start, "HTTP/1.0 ", 9) != 0) ||
        start[9] < '0' || start[9] > '9' ||
        start[10] < '0' || start[10] > '9' ||
        start[11] < '0' || start[11] > '9' ||
        start[12] != ' ')
        return -1;
    for (size_t i = 13; i < line_length; i++) {
        unsigned char c = (unsigned char)start[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) return -1;
    }
    framing->status_code = (start[9] - '0') * 100 +
                           (start[10] - '0') * 10 +
                           (start[11] - '0');
    if (framing->status_code < 100) return -1;
    int is_http_10 = memcmp(start, "HTTP/1.0 ", 9) == 0;

    int field_count = 0;
    const char *cursor = line_end + 2;
    while (cursor < end) {
        if (end - cursor >= 2 && cursor[0] == '\r' &&
            cursor[1] == '\n')
            return cursor + 2 == end ? 0 : -1;
        line_end = proxy_find_crlf(cursor, end);
        if (!line_end || line_end == cursor ||
            *cursor == ' ' || *cursor == '\t')
            return -1;
        line_length = (size_t)(line_end - cursor);
        if (line_length > PROXY_CHUNK_LINE_LIMIT ||
            ++field_count > PROXY_RESPONSE_MAX_FIELDS)
            return -1;
        const char *colon = (const char *)memchr(
            cursor, ':', line_length);
        if (!colon) return -1;
        size_t name_length = (size_t)(colon - cursor);
        if (name_length > 255U ||
            !proxy_valid_token(cursor, name_length))
            return -1;
        const char *value = colon + 1;
        size_t value_length = (size_t)(line_end - value);
        proxy_trim_ows(&value, &value_length);
        if (!proxy_valid_field_value(value, value_length)) return -1;

        if (proxy_equal_ci_n(cursor, name_length, "Content-Length")) {
            if (framing->has_content_length ||
                proxy_parse_content_length_value(
                    value, value_length, framing) != 0)
                return -1;
        } else if (proxy_equal_ci_n(
                       cursor, name_length, "Transfer-Encoding")) {
            /* RFC 9112: Transfer-Encoding is not defined for HTTP/1.0.
             * Decoding a 1.0 chunked body desynchronizes hops that read
             * identity until connection close. */
            if (is_http_10 || framing->is_chunked || value_length != 7 ||
                !proxy_equal_ci_n(value, value_length, "chunked"))
                return -1;
            framing->is_chunked = 1;
        } else if (proxy_equal_ci_n(
                       cursor, name_length, "Connection")) {
            char connection_value[PROXY_CHUNK_LINE_LIMIT + 1U];
            if (value_length > PROXY_CHUNK_LINE_LIMIT) return -1;
            memcpy(connection_value, value, value_length);
            connection_value[value_length] = '\0';
            int ignored = 0;
            if (proxy_token_list(
                    connection_value, NULL, 0, &ignored) != 0)
                return -1;
        }
        cursor = line_end + 2;
    }
    return -1;
}

static int proxy_response_header_named_by_connection(
    const proxy_buffer_t *headers, const char *name, size_t name_length) {
    const char *end = headers->data + headers->len;
    const char *cursor = proxy_find_crlf(headers->data, end);
    if (!cursor) return 0;
    cursor += 2;
    while (cursor < end && !(cursor[0] == '\r' && cursor[1] == '\n')) {
        const char *line_end = proxy_find_crlf(cursor, end);
        if (!line_end) return 0;
        const char *colon = (const char *)memchr(
            cursor, ':', (size_t)(line_end - cursor));
        if (colon && proxy_equal_ci_n(
                cursor, (size_t)(colon - cursor), "Connection")) {
            const char *value = colon + 1;
            size_t value_length = (size_t)(line_end - value);
            proxy_trim_ows(&value, &value_length);
            char copy[PROXY_CHUNK_LINE_LIMIT + 1U];
            if (value_length <= PROXY_CHUNK_LINE_LIMIT) {
                memcpy(copy, value, value_length);
                copy[value_length] = '\0';
                int contains = 0;
                if (proxy_token_list(
                        copy, name, name_length, &contains) == 0 &&
                    contains)
                    return 1;
            }
        }
        cursor = line_end + 2;
    }
    return 0;
}

static int proxy_parse_chunk_size(const char *line, size_t length,
                                  size_t *chunk_size) {
    if (!line || !chunk_size || length == 0) return -1;
    size_t value = 0;
    size_t digits = 0;
    while (digits < length) {
        unsigned char c = (unsigned char)line[digits];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else break;
        if (value > (SIZE_MAX - digit) / 16U) return -1;
        value = value * 16U + digit;
        digits++;
    }
    if (digits == 0) return -1;
    if (digits < length) {
        if (line[digits] != ';' || digits + 1U == length ||
            !proxy_valid_field_value(
                line + digits, length - digits))
            return -1;
    }
    *chunk_size = value;
    return 0;
}

static int proxy_valid_trailer_line(const char *line, size_t length) {
    if (!line || length == 0 || length > PROXY_CHUNK_LINE_LIMIT ||
        line[0] == ' ' || line[0] == '\t')
        return -1;
    const char *colon = (const char *)memchr(line, ':', length);
    if (!colon) return -1;
    size_t name_length = (size_t)(colon - line);
    const char *value = colon + 1;
    size_t value_length = length - (size_t)(value - line);
    proxy_trim_ows(&value, &value_length);
    if (!proxy_valid_token(line, name_length) ||
        !proxy_valid_field_value(value, value_length) ||
        proxy_equal_ci_n(line, name_length, "Content-Length") ||
        proxy_equal_ci_n(line, name_length, "Transfer-Encoding") ||
        proxy_equal_ci_n(line, name_length, "Host"))
        return -1;
    return 0;
}

static int proxy_read_chunked_body(proxy_reader_t *reader,
                                   proxy_buffer_t *body) {
    char line[PROXY_CHUNK_LINE_LIMIT + 1U];
    size_t metadata = 0;
    for (;;) {
        size_t line_length = 0;
        if (proxy_reader_line(
                reader, line, sizeof(line), &line_length) != 0 ||
            line_length > PROXY_HEADER_LIMIT - metadata ||
            2U > PROXY_HEADER_LIMIT - metadata - line_length)
            return -1;
        metadata += line_length + 2U;
        size_t chunk_size = 0;
        if (proxy_parse_chunk_size(
                line, line_length, &chunk_size) != 0)
            return -1;
        if (chunk_size == 0) {
            for (;;) {
                if (proxy_reader_line(
                        reader, line, sizeof(line), &line_length) != 0 ||
                    line_length > PROXY_HEADER_LIMIT - metadata ||
                    2U > PROXY_HEADER_LIMIT - metadata - line_length)
                    return -1;
                metadata += line_length + 2U;
                if (line_length == 0)
                    return reader->pos == reader->len ? 0 : -1;
                if (proxy_valid_trailer_line(
                        line, line_length) != 0)
                    return -1;
            }
        }
        if (chunk_size > PROXY_BODY_LIMIT - body->len ||
            proxy_reader_exact_append(
                reader, body, chunk_size, PROXY_BODY_LIMIT) != 0)
            return -1;
        unsigned char cr = 0;
        unsigned char lf = 0;
        if (proxy_reader_byte(reader, &cr) != 1 ||
            proxy_reader_byte(reader, &lf) != 1 ||
            cr != '\r' || lf != '\n')
            return -1;
    }
}

static int proxy_read_to_eof(proxy_reader_t *reader,
                             proxy_buffer_t *body) {
    for (;;) {
        if (reader->pos < reader->len) {
            size_t available = reader->len - reader->pos;
            if (proxy_buffer_append(
                    body, reader->data + reader->pos, available,
                    PROXY_BODY_LIMIT) != 0)
                return -1;
            reader->pos = reader->len;
        }
        int result = proxy_reader_fill(reader);
        if (result == 0) return 0;
        if (result < 0) return -1;
    }
}

static int proxy_read_response(
    proxy_transport_t *transport, neverc_context_t *context,
    const char *method,
    proxy_buffer_t *headers, proxy_buffer_t *body,
    proxy_response_framing_t *framing) {
    proxy_reader_t reader;
    memset(&reader, 0, sizeof(reader));
    reader.transport = transport;
    reader.context = context;

    for (int interim = 0; interim < 17; interim++) {
        proxy_buffer_free(headers);
        if (proxy_reader_header_block(&reader, headers) != 0 ||
            proxy_parse_response_headers(headers, framing) != 0)
            return -1;
        if (framing->status_code >= 100 &&
            framing->status_code < 200 &&
            framing->status_code != 101) {
            if (framing->has_content_length || framing->is_chunked ||
                interim == 16)
                return -1;
            continue;
        }
        break;
    }

    if (framing->status_code == 101 ||
        (framing->has_content_length && framing->is_chunked) ||
        ((framing->status_code < 200 || framing->status_code == 204) &&
         (framing->has_content_length || framing->is_chunked)))
        return -1;

    int body_forbidden =
        (method && strcmp(method, "HEAD") == 0) ||
        framing->status_code < 200 ||
        framing->status_code == 204 ||
        framing->status_code == 304;
    if (body_forbidden)
        return 0;

    if (framing->is_chunked)
        return proxy_read_chunked_body(&reader, body);
    if (framing->has_content_length) {
        if (framing->content_length > PROXY_BODY_LIMIT ||
            proxy_reader_exact_append(
                &reader, body, framing->content_length,
                PROXY_BODY_LIMIT) != 0)
            return -1;
        return reader.pos == reader.len ? 0 : -1;
    }
    return proxy_read_to_eof(&reader, body);
}

static int proxy_forward_response_headers(
    neverc_http_response_writer_t *writer,
    const proxy_buffer_t *headers) {
    const char *end = headers->data + headers->len;
    const char *cursor = proxy_find_crlf(headers->data, end);
    if (!cursor) return -1;
    cursor += 2;
    while (cursor < end && !(cursor[0] == '\r' && cursor[1] == '\n')) {
        const char *line_end = proxy_find_crlf(cursor, end);
        if (!line_end) return -1;
        const char *colon = (const char *)memchr(
            cursor, ':', (size_t)(line_end - cursor));
        size_t name_length = (size_t)(colon - cursor);
        const char *value = colon + 1;
        size_t value_length = (size_t)(line_end - value);
        proxy_trim_ows(&value, &value_length);
        if (!proxy_equal_ci_n(cursor, name_length, "Content-Length") &&
            !proxy_is_hop_header(cursor, name_length) &&
            !proxy_response_header_named_by_connection(
                headers, cursor, name_length)) {
            char name_copy[256];
            char value_copy[PROXY_CHUNK_LINE_LIMIT + 1U];
            memcpy(name_copy, cursor, name_length);
            name_copy[name_length] = '\0';
            memcpy(value_copy, value, value_length);
            value_copy[value_length] = '\0';
            if (neverc_http_add_header(
                    writer, name_copy, value_copy) != 0)
                return -1;
        }
        cursor = line_end + 2;
    }
    return 0;
}

static void proxy_pin(neverc_httputil_reverse_proxy_t *proxy) {
    if (!proxy) return;
    proxy_legacy_lock();
    proxy->legacy_active++;
    proxy_legacy_unlock();
}

static int proxy_unpin(neverc_httputil_reverse_proxy_t *proxy) {
    if (!proxy) return 0;
    int release = 0;
    proxy_legacy_lock();
    proxy->legacy_active--;
    release = proxy->legacy_released && proxy->legacy_active == 0;
    proxy_legacy_unlock();
    return release;
}

static void proxy_fail(neverc_httputil_reverse_proxy_t *proxy,
                       neverc_http_response_writer_t *writer,
                       const neverc_http_request_t *request,
                       const char *message) {
    const char *error = message ? message : "reverse proxy failure";
    int reset_ok = !writer || neverc_http_reset_response(writer) == 0;
    if (proxy && proxy->error_handler) {
        proxy->error_handler(
            writer, request, error, proxy->error_data);
    } else if (writer && reset_ok) {
        neverc_http_error(writer, "502 Bad Gateway", 502);
    }
}

static void reverse_proxy_serve(
    neverc_httputil_reverse_proxy_t *proxy,
    neverc_http_request_t *request,
    neverc_http_response_writer_t *writer) {
    proxy_buffer_t request_headers = {0};
    proxy_buffer_t response_headers = {0};
    proxy_buffer_t response_body = {0};
    proxy_transport_t transport;
    memset(&transport, 0, sizeof(transport));
    neverc_context_t *backend_context = NULL;
    neverc_context_cancel_handle_t *backend_cancel = NULL;
    const char *failure = NULL;

    if (!proxy) {
        proxy_fail(NULL, writer, request, "proxy was released");
        return;
    }
    if (!request || !writer) {
        proxy_fail(proxy, writer, request, "invalid proxy invocation");
        return;
    }

    neverc_http_request_t outbound = *request;
    if (proxy->rewrite_func &&
        proxy->rewrite_func(
            request, &outbound, proxy->rewrite_data) != 0) {
        failure = "request rewrite failed";
        goto fail;
    }
    if (proxy_build_request(
            proxy, request, &outbound, &request_headers) != 0) {
        failure = "invalid or oversized proxy request";
        goto fail;
    }

    backend_context = neverc_context_with_timeout_handle(
        request->context, PROXY_BACKEND_TIMEOUT_MS, &backend_cancel);
    if (!backend_context || !backend_cancel) {
        failure = "failed to create backend timeout context";
        goto fail;
    }

    const char *dial_error = NULL;
    if (proxy_transport_dial(
            proxy, backend_context, &transport, &dial_error) != 0) {
        failure = dial_error ? dial_error : "backend connection failed";
        goto fail;
    }
    if (proxy_transport_write_all(
            &transport, backend_context,
            request_headers.data, request_headers.len) != 0 ||
        (outbound.body_len > 0 &&
         proxy_transport_write_all(
             &transport, backend_context,
             outbound.body, outbound.body_len) != 0)) {
        failure = "backend request write failed";
        goto fail;
    }

    proxy_response_framing_t framing;
    if (proxy_read_response(
            &transport, backend_context,
            outbound.method, &response_headers,
            &response_body, &framing) != 0) {
        failure = "invalid or incomplete backend response";
        goto fail;
    }
    proxy_transport_close(&transport);
    neverc_context_cancel_handle_cancel(backend_cancel);
    neverc_context_free(backend_context);
    neverc_context_cancel_handle_free(backend_cancel);
    backend_context = NULL;
    backend_cancel = NULL;

    neverc_http_set_status(writer, framing.status_code);
    if (proxy_forward_response_headers(
            writer, &response_headers) != 0) {
        failure = "client response header write failed";
        goto fail;
    }
    int preserve_content_length =
        framing.has_content_length &&
        ((request->method && strcmp(request->method, "HEAD") == 0) ||
         (outbound.method && strcmp(outbound.method, "HEAD") == 0) ||
         framing.status_code == 304);
    if (preserve_content_length &&
        neverc_http_set_content_length(
            writer, framing.content_length) != 0) {
        failure = "client content length metadata write failed";
        goto fail;
    }
    if (response_body.len > 0) {
        int written = neverc_http_write(
            writer, response_body.data, response_body.len);
        if (written < 0 || (size_t)written != response_body.len) {
            failure = "client response write failed";
            goto fail;
        }
    }

    proxy_buffer_free(&request_headers);
    proxy_buffer_free(&response_headers);
    proxy_buffer_free(&response_body);
    return;

fail:
    proxy_transport_close(&transport);
    if (backend_cancel)
        neverc_context_cancel_handle_cancel(backend_cancel);
    neverc_context_free(backend_context);
    neverc_context_cancel_handle_free(backend_cancel);
    proxy_buffer_free(&request_headers);
    proxy_buffer_free(&response_headers);
    proxy_buffer_free(&response_body);
    proxy_fail(proxy, writer, request, failure);
}

neverc_httputil_reverse_proxy_t *neverc_httputil_new_single_host_reverse_proxy(
    const char *target_url) {
    if (!target_url) return NULL;

    neverc_httputil_reverse_proxy_t *rp =
        (neverc_httputil_reverse_proxy_t *)calloc(1, sizeof(*rp));
    if (!rp) return NULL;

    if (parse_target_url(target_url,
                          rp->target_host, sizeof(rp->target_host),
                          rp->target_authority,
                          sizeof(rp->target_authority),
                          &rp->target_port,
                          rp->target_path, sizeof(rp->target_path),
                          &rp->use_tls,
                          &rp->target_is_ipv6) != 0) {
        free(rp);
        return NULL;
    }

    rp->set_forwarded = 1;
    memcpy(rp->forwarded_proto, "http", 5);
    rp->legacy_slot = -1;

    return rp;
}

void neverc_httputil_proxy_set_rewrite(neverc_httputil_reverse_proxy_t *rp,
                                        neverc_httputil_rewrite_func_t func,
                                        void *user_data) {
    if (!rp) return;
    rp->rewrite_func = func;
    rp->rewrite_data = user_data;
}

void neverc_httputil_proxy_set_error_handler(
    neverc_httputil_reverse_proxy_t *rp,
    neverc_httputil_error_handler_t handler,
    void *user_data) {
    if (!rp) return;
    rp->error_handler = handler;
    rp->error_data = user_data;
}

void neverc_httputil_proxy_set_forwarded_headers(
    neverc_httputil_reverse_proxy_t *rp, int enable) {
    if (rp) rp->set_forwarded = enable;
}

int neverc_httputil_proxy_set_forwarded_proto(
    neverc_httputil_reverse_proxy_t *rp, const char *proto) {
    if (!rp || !proto) return -1;
    if (proxy_equal_ci_n(proto, strlen(proto), "http")) {
        memcpy(rp->forwarded_proto, "http", 5);
        return 0;
    }
    if (proxy_equal_ci_n(proto, strlen(proto), "https")) {
        memcpy(rp->forwarded_proto, "https", 6);
        return 0;
    }
    return -1;
}

static void reverse_proxy_context_handler(
    neverc_http_request_t *request,
    neverc_http_response_writer_t *writer, void *context) {
    neverc_httputil_reverse_proxy_t *proxy =
        (neverc_httputil_reverse_proxy_t *)context;
    proxy_pin(proxy);
    reverse_proxy_serve(proxy, request, writer);
    if (proxy_unpin(proxy))
        free(proxy);
}

int neverc_httputil_proxy_register(
    neverc_http_mux_t *mux, const char *pattern,
    neverc_httputil_reverse_proxy_t *rp) {
    if (!mux || !pattern || !rp) return -1;
    return neverc_http_mux_handle_context(
        mux, pattern, reverse_proxy_context_handler, rp);
}

static void proxy_legacy_dispatch(
    size_t slot, neverc_http_request_t *request,
    neverc_http_response_writer_t *writer) {
    proxy_legacy_lock();
    neverc_httputil_reverse_proxy_t *proxy = atomic_load_explicit(
        &g_legacy_slots[slot], memory_order_acquire);
    proxy_legacy_unlock();

    proxy_pin(proxy);
    reverse_proxy_serve(proxy, request, writer);
    if (proxy_unpin(proxy))
        free(proxy);
}

#define PROXY_LEGACY_WRAPPER(index)                                      \
    static void proxy_legacy_##index(                                   \
        neverc_http_request_t *request,                                 \
        neverc_http_response_writer_t *writer) {                        \
        proxy_legacy_dispatch(index, request, writer);                  \
    }

PROXY_LEGACY_WRAPPER(0)
PROXY_LEGACY_WRAPPER(1)
PROXY_LEGACY_WRAPPER(2)
PROXY_LEGACY_WRAPPER(3)
PROXY_LEGACY_WRAPPER(4)
PROXY_LEGACY_WRAPPER(5)
PROXY_LEGACY_WRAPPER(6)
PROXY_LEGACY_WRAPPER(7)
PROXY_LEGACY_WRAPPER(8)
PROXY_LEGACY_WRAPPER(9)
PROXY_LEGACY_WRAPPER(10)
PROXY_LEGACY_WRAPPER(11)
PROXY_LEGACY_WRAPPER(12)
PROXY_LEGACY_WRAPPER(13)
PROXY_LEGACY_WRAPPER(14)
PROXY_LEGACY_WRAPPER(15)
PROXY_LEGACY_WRAPPER(16)
PROXY_LEGACY_WRAPPER(17)
PROXY_LEGACY_WRAPPER(18)
PROXY_LEGACY_WRAPPER(19)
PROXY_LEGACY_WRAPPER(20)
PROXY_LEGACY_WRAPPER(21)
PROXY_LEGACY_WRAPPER(22)
PROXY_LEGACY_WRAPPER(23)
PROXY_LEGACY_WRAPPER(24)
PROXY_LEGACY_WRAPPER(25)
PROXY_LEGACY_WRAPPER(26)
PROXY_LEGACY_WRAPPER(27)
PROXY_LEGACY_WRAPPER(28)
PROXY_LEGACY_WRAPPER(29)
PROXY_LEGACY_WRAPPER(30)
PROXY_LEGACY_WRAPPER(31)
PROXY_LEGACY_WRAPPER(32)
PROXY_LEGACY_WRAPPER(33)
PROXY_LEGACY_WRAPPER(34)
PROXY_LEGACY_WRAPPER(35)
PROXY_LEGACY_WRAPPER(36)
PROXY_LEGACY_WRAPPER(37)
PROXY_LEGACY_WRAPPER(38)
PROXY_LEGACY_WRAPPER(39)
PROXY_LEGACY_WRAPPER(40)
PROXY_LEGACY_WRAPPER(41)
PROXY_LEGACY_WRAPPER(42)
PROXY_LEGACY_WRAPPER(43)
PROXY_LEGACY_WRAPPER(44)
PROXY_LEGACY_WRAPPER(45)
PROXY_LEGACY_WRAPPER(46)
PROXY_LEGACY_WRAPPER(47)
PROXY_LEGACY_WRAPPER(48)
PROXY_LEGACY_WRAPPER(49)
PROXY_LEGACY_WRAPPER(50)
PROXY_LEGACY_WRAPPER(51)
PROXY_LEGACY_WRAPPER(52)
PROXY_LEGACY_WRAPPER(53)
PROXY_LEGACY_WRAPPER(54)
PROXY_LEGACY_WRAPPER(55)
PROXY_LEGACY_WRAPPER(56)
PROXY_LEGACY_WRAPPER(57)
PROXY_LEGACY_WRAPPER(58)
PROXY_LEGACY_WRAPPER(59)
PROXY_LEGACY_WRAPPER(60)
PROXY_LEGACY_WRAPPER(61)
PROXY_LEGACY_WRAPPER(62)
PROXY_LEGACY_WRAPPER(63)

#undef PROXY_LEGACY_WRAPPER

static neverc_http_handler_func_t
    g_legacy_handlers[PROXY_LEGACY_SLOT_COUNT] = {
        proxy_legacy_0, proxy_legacy_1, proxy_legacy_2, proxy_legacy_3,
        proxy_legacy_4, proxy_legacy_5, proxy_legacy_6, proxy_legacy_7,
        proxy_legacy_8, proxy_legacy_9, proxy_legacy_10, proxy_legacy_11,
        proxy_legacy_12, proxy_legacy_13, proxy_legacy_14, proxy_legacy_15,
        proxy_legacy_16, proxy_legacy_17, proxy_legacy_18, proxy_legacy_19,
        proxy_legacy_20, proxy_legacy_21, proxy_legacy_22, proxy_legacy_23,
        proxy_legacy_24, proxy_legacy_25, proxy_legacy_26, proxy_legacy_27,
        proxy_legacy_28, proxy_legacy_29, proxy_legacy_30, proxy_legacy_31,
        proxy_legacy_32, proxy_legacy_33, proxy_legacy_34, proxy_legacy_35,
        proxy_legacy_36, proxy_legacy_37, proxy_legacy_38, proxy_legacy_39,
        proxy_legacy_40, proxy_legacy_41, proxy_legacy_42, proxy_legacy_43,
        proxy_legacy_44, proxy_legacy_45, proxy_legacy_46, proxy_legacy_47,
        proxy_legacy_48, proxy_legacy_49, proxy_legacy_50, proxy_legacy_51,
        proxy_legacy_52, proxy_legacy_53, proxy_legacy_54, proxy_legacy_55,
        proxy_legacy_56, proxy_legacy_57, proxy_legacy_58, proxy_legacy_59,
        proxy_legacy_60, proxy_legacy_61, proxy_legacy_62, proxy_legacy_63,
    };

static void proxy_legacy_lock(void) {
    while (atomic_flag_test_and_set_explicit(
        &g_legacy_lock, memory_order_acquire)) {
    }
}

static void proxy_legacy_unlock(void) {
    atomic_flag_clear_explicit(&g_legacy_lock, memory_order_release);
}

neverc_http_handler_func_t neverc_httputil_proxy_handler(
    neverc_httputil_reverse_proxy_t *rp) {
    if (!rp) return NULL;
    proxy_legacy_lock();
    if (rp->legacy_slot < 0) {
        if (g_legacy_next_slot >= PROXY_LEGACY_SLOT_COUNT) {
            proxy_legacy_unlock();
            return NULL;
        }
        rp->legacy_slot = (int)g_legacy_next_slot++;
        atomic_store_explicit(
            &g_legacy_slots[rp->legacy_slot], rp, memory_order_release);
    }
    neverc_http_handler_func_t handler =
        g_legacy_handlers[rp->legacy_slot];
    proxy_legacy_unlock();
    return handler;
}

void neverc_httputil_proxy_free(neverc_httputil_reverse_proxy_t *rp) {
    if (!rp) return;
    int release = 0;
    proxy_legacy_lock();
    if (rp->legacy_slot >= 0)
        atomic_store_explicit(
            &g_legacy_slots[rp->legacy_slot], NULL,
            memory_order_release);
    rp->legacy_released = 1;
    release = rp->legacy_active == 0;
    proxy_legacy_unlock();
    if (release)
        free(rp);
}

/* ======================================================================
 * Request Dumping
 * ====================================================================== */

char *neverc_httputil_dump_request(const neverc_http_request_t *req,
                                    int include_body) {
    if (!req || req->nheaders < 0 ||
        (req->nheaders > 0 && !req->raw_headers) ||
        (req->body_len > 0 && !req->body))
        return NULL;
    if (httputil_has_crlf(req->method) || httputil_has_crlf(req->path) ||
        httputil_has_crlf(req->query) || httputil_has_crlf(req->http_version) ||
        httputil_has_crlf(req->host))
        return NULL;

    size_t cap = 256;
    size_t n = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    if (httputil_dump_append_string(
            &buf, &n, &cap, req->method ? req->method : "GET") != 0 ||
        httputil_dump_append_string(&buf, &n, &cap, " ") != 0 ||
        httputil_dump_append_string(
            &buf, &n, &cap, req->path ? req->path : "/") != 0)
        goto fail;

    if (req->query && req->query[0] &&
        (httputil_dump_append_string(&buf, &n, &cap, "?") != 0 ||
         httputil_dump_append_string(&buf, &n, &cap, req->query) != 0))
        goto fail;

    if (httputil_dump_append_string(&buf, &n, &cap, " ") != 0 ||
        httputil_dump_append_string(
            &buf, &n, &cap,
            req->http_version ? req->http_version : "HTTP/1.1") != 0 ||
        httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0)
        goto fail;

    if (req->host) {
        int bracket_ipv6 = httputil_host_needs_brackets(req->host);
        if (httputil_dump_append_string(&buf, &n, &cap, "Host: ") != 0 ||
            (bracket_ipv6 &&
             httputil_dump_append_string(&buf, &n, &cap, "[") != 0) ||
            httputil_dump_append_string(&buf, &n, &cap, req->host) != 0 ||
            (bracket_ipv6 &&
             httputil_dump_append_string(&buf, &n, &cap, "]") != 0) ||
            httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0)
            goto fail;
    }

    int saw_content_length = 0;
    if (req->raw_headers) {
        const char *p = req->raw_headers;
        for (int i = 0; i < req->nheaders; i++) {
            const char *hname = p;
            while (*p) p++;
            p++;
            const char *hval = p;
            while (*p) p++;
            p++;

            if (httputil_has_crlf(hname) || httputil_has_crlf(hval))
                goto fail;
            if (req->host && strcasecmp(hname, "Host") == 0) continue;
            if (strcasecmp(hname, "Content-Length") == 0)
                saw_content_length = 1;
            if (httputil_dump_append_string(
                    &buf, &n, &cap, hname) != 0 ||
                httputil_dump_append_string(
                    &buf, &n, &cap, ": ") != 0 ||
                httputil_dump_append_string(
                    &buf, &n, &cap, hval) != 0 ||
                httputil_dump_append_string(
                    &buf, &n, &cap, "\r\n") != 0)
                goto fail;
        }
    }

    if (include_body && req->body_len > 0 && !saw_content_length) {
        char content_length[64];
        int content_length_size = snprintf(
            content_length, sizeof(content_length),
            "Content-Length: %zu\r\n", req->body_len);
        if (content_length_size < 0 ||
            (size_t)content_length_size >= sizeof(content_length) ||
            httputil_dump_append(
                &buf, &n, &cap, content_length,
                (size_t)content_length_size) != 0)
            goto fail;
    }

    if (httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0)
        goto fail;

    if (include_body && req->body_len > 0) {
        if (httputil_dump_append(
                &buf, &n, &cap, req->body, req->body_len) != 0)
            goto fail;
    }

    return buf;

fail:
    free(buf);
    return NULL;
}

char *neverc_httputil_dump_request_out(const char *method,
                                        const char *url,
                                        const char *headers,
                                        const char *body,
                                        size_t body_len) {
    if (!body && body_len != 0)
        return NULL;
    int needs_header_crlf = 0;
    if (httputil_has_crlf(method) || httputil_has_crlf(url) ||
        !httputil_header_block_valid(headers, &needs_header_crlf))
        return NULL;
    size_t cap = 256;
    size_t n = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    if (httputil_dump_append_string(
            &buf, &n, &cap, method ? method : "GET") != 0 ||
        httputil_dump_append_string(&buf, &n, &cap, " ") != 0 ||
        httputil_dump_append_string(
            &buf, &n, &cap, url ? url : "/") != 0 ||
        httputil_dump_append_string(
            &buf, &n, &cap, " HTTP/1.1\r\n") != 0)
        goto fail;

    if (headers &&
        httputil_dump_append_string(&buf, &n, &cap, headers) != 0)
        goto fail;
    if (needs_header_crlf &&
        httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0)
        goto fail;

    if (body_len > 0 && !httputil_headers_have_content_length(headers)) {
        char content_length[64];
        int content_length_size = snprintf(
            content_length, sizeof(content_length),
            "Content-Length: %zu\r\n", body_len);
        if (content_length_size < 0 ||
            (size_t)content_length_size >= sizeof(content_length) ||
            httputil_dump_append(
                &buf, &n, &cap, content_length,
                (size_t)content_length_size) != 0)
            goto fail;
    }

    if (httputil_dump_append_string(&buf, &n, &cap, "\r\n") != 0)
        goto fail;

    if (body_len > 0 &&
        httputil_dump_append(
            &buf, &n, &cap, body, body_len) != 0)
        goto fail;

    return buf;

fail:
    free(buf);
    return NULL;
}
