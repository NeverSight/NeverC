#include "neverc/std/net/url.h"
#include "neverc/std/net/netip.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

static int nc_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* Hex value per byte, -1 for non-hex. Lets the percent-decode hot path resolve
 * a '%XX' escape with two table loads instead of a branch ladder, and validate
 * both nibbles with a single sign test ((h | l) < 0 is true iff either is -1).
 * Compile-time constant, so it is immutable and shared with no init. */
static const signed char hex_val[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
     0, 1, 2, 3, 4, 5, 6, 7,  8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
};

typedef struct url_builder {
    char   *buffer;
    size_t  capacity;
    size_t  length;
    int     failed;
} url_builder_t;

static int copy_exact(char *dst, size_t cap, const char *src, size_t len) {
    if (!dst || !src || cap == 0 || len >= cap) return -1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return 0;
}

static size_t bounded_string_length(const char *s, size_t capacity) {
    size_t length = 0;
    while (length < capacity && s[length]) length++;
    return length;
}

static int fixed_string_equal(const char *field, size_t capacity,
                              const char *value) {
    size_t field_length = bounded_string_length(field, capacity);
    size_t value_length = strlen(value);
    return field_length < capacity && field_length == value_length &&
        memcmp(field, value, field_length) == 0;
}

static void builder_init(url_builder_t *builder, char *buffer,
                         size_t capacity) {
    builder->buffer = buffer;
    builder->capacity = capacity;
    builder->length = 0;
    builder->failed = capacity > 0 && !buffer;
    if (!builder->failed && capacity > 0) buffer[0] = '\0';
}

static void builder_append(url_builder_t *builder, const char *data,
                           size_t length) {
    if (builder->failed || !data || length > SIZE_MAX - builder->length) {
        builder->failed = 1;
        return;
    }

    size_t old_length = builder->length;
    if (builder->capacity > 0 && old_length < builder->capacity - 1) {
        size_t writable = builder->capacity - 1 - old_length;
        if (writable > length) writable = length;
        memcpy(builder->buffer + old_length, data, writable);
    }
    builder->length += length;
    if (builder->capacity > 0) {
        size_t terminator = builder->length < builder->capacity
            ? builder->length : builder->capacity - 1;
        builder->buffer[terminator] = '\0';
    }
}

static void builder_append_literal(url_builder_t *builder,
                                   const char *literal) {
    builder_append(builder, literal, strlen(literal));
}

static int builder_append_field(url_builder_t *builder, const char *field,
                                size_t capacity) {
    size_t length = bounded_string_length(field, capacity);
    if (length == capacity) {
        builder->failed = 1;
        return -1;
    }
    builder_append(builder, field, length);
    return builder->failed ? -1 : 0;
}

static int builder_result(const url_builder_t *builder) {
    if (builder->failed || builder->length > (size_t)INT_MAX) return -1;
    return (int)builder->length;
}

static int valid_scheme(const char *scheme, size_t length) {
    if (length == 0 ||
        !((scheme[0] >= 'A' && scheme[0] <= 'Z') ||
          (scheme[0] >= 'a' && scheme[0] <= 'z')))
        return 0;
    for (size_t i = 1; i < length; i++) {
        unsigned char c = (unsigned char)scheme[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

static int parse_port(const char *start, const char *end,
                      char *port, size_t capacity) {
    size_t length = (size_t)(end - start);
    if (length == 0 || length >= capacity) return -1;
    unsigned value = 0;
    for (const char *p = start; p < end; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < '0' || c > '9') return -1;
        value = value * 10U + (unsigned)(c - '0');
        if (value > 65535U) return -1;
    }
    return copy_exact(port, capacity, start, length);
}

static int valid_host_text(const char *host, size_t length) {
    if (length == 0) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)host[i];
        if (c <= 0x20 || c == 0x7f || c == '/' || c == '\\' ||
            c == '?' || c == '#' || c == '@' || c == '[' || c == ']')
            return 0;
    }
    return 1;
}

int neverc_url_parse(neverc_url_t *u, const char *raw_url) {
    if (!u) return -1;
    memset(u, 0, sizeof(*u));
    if (!raw_url || !*raw_url) return -1;

    size_t raw_length = strlen(raw_url);
    for (size_t i = 0; i < raw_length; i++) {
        unsigned char c = (unsigned char)raw_url[i];
        if (c <= 0x20 || c == 0x7f) return -1;
    }

    const char *p = raw_url;
    const char *raw_end = raw_url + raw_length;
    const char *scheme_end = strchr(p, ':');
    const char *first_delimiter = strpbrk(p, "/?#");
    if (scheme_end && (!first_delimiter || scheme_end < first_delimiter)) {
        size_t scheme_length = (size_t)(scheme_end - p);
        if (!valid_scheme(p, scheme_length) ||
            (size_t)(raw_end - scheme_end) < 3 ||
            scheme_end[1] != '/' || scheme_end[2] != '/' ||
            copy_exact(u->scheme, sizeof(u->scheme), p,
                       scheme_length) != 0)
            return -1;
        for (size_t i = 0; u->scheme[i]; i++)
            u->scheme[i] = (char)nc_tolower((unsigned char)u->scheme[i]);
        p = scheme_end + 3;

        const char *authority_end = p + strcspn(p, "/?#");
        if (authority_end == p) return -1;
        const char *at = NULL;
        for (const char *c = p; c < authority_end; c++)
            if (*c == '@') at = c;

        if (at) {
            const char *colon = NULL;
            for (const char *c = p; c < at; c++)
                if (*c == ':') { colon = c; break; }
            if (at == p) return -1;
            if (colon) {
                if (copy_exact(u->user, sizeof(u->user), p,
                               (size_t)(colon - p)) != 0 ||
                    copy_exact(u->password, sizeof(u->password), colon + 1,
                               (size_t)(at - colon - 1)) != 0)
                    return -1;
            } else {
                if (copy_exact(u->user, sizeof(u->user), p,
                               (size_t)(at - p)) != 0)
                    return -1;
            }
            p = at + 1;
        }

        const char *host_end = authority_end;
        if (*p == '[') {
            const char *bracket = memchr(p + 1, ']',
                (size_t)(authority_end - p - 1));
            if (!bracket || bracket == p + 1)
                return -1;
            size_t hlen = (size_t)(bracket - p - 1);
            char hostbuf[256];
            neverc_netip_addr_t addr;
            /* Go net/url: only a valid IPv6 (including IPv4-mapped) literal
             * may be enclosed in brackets. IPv4 and non-IP text are errors. */
            if (hlen >= sizeof(hostbuf) ||
                copy_exact(hostbuf, sizeof(hostbuf), p + 1, hlen) != 0 ||
                neverc_netip_parse_addr(hostbuf, &addr) != 0 ||
                addr.is_v4 ||
                copy_exact(u->host, sizeof(u->host), p + 1, hlen) != 0)
                return -1;
            if (bracket + 1 < authority_end) {
                if (bracket[1] != ':' ||
                    parse_port(bracket + 2, authority_end, u->port,
                               sizeof(u->port)) != 0)
                    return -1;
            }
        } else {
            const char *colon = NULL;
            for (const char *c = p; c < host_end; c++) {
                if (*c != ':') continue;
                if (colon) return -1;
                colon = c;
            }
            const char *name_end = colon ? colon : host_end;
            if (!valid_host_text(p, (size_t)(name_end - p)) ||
                copy_exact(u->host, sizeof(u->host), p,
                           (size_t)(name_end - p)) != 0)
                return -1;
            if (colon) {
                if (parse_port(colon + 1, host_end, u->port,
                               sizeof(u->port)) != 0)
                    return -1;
            }
        }
        p = authority_end;
    }

    const char *fragment = memchr(p, '#', (size_t)(raw_end - p));
    const char *before_fragment = fragment ? fragment : raw_end;
    const char *query = memchr(p, '?', (size_t)(before_fragment - p));
    const char *path_end = query ? query : before_fragment;
    if (copy_exact(u->path, sizeof(u->path), p,
                   (size_t)(path_end - p)) != 0)
        return -1;
    if (query && copy_exact(u->raw_query, sizeof(u->raw_query), query + 1,
                            (size_t)(before_fragment - query - 1)) != 0)
        return -1;
    if (fragment && copy_exact(u->fragment, sizeof(u->fragment), fragment + 1,
                               (size_t)(raw_end - fragment - 1)) != 0)
        return -1;

    return 0;
}

int neverc_url_string(const neverc_url_t *u, char *buf, size_t cap) {
    if (!u) return -1;
    url_builder_t builder;
    builder_init(&builder, buf, cap);
    if (u->scheme[0]) {
        builder_append_field(&builder, u->scheme, sizeof(u->scheme));
        builder_append_literal(&builder, "://");
    }
    if (u->user[0]) {
        builder_append_field(&builder, u->user, sizeof(u->user));
        if (u->password[0]) {
            builder_append_literal(&builder, ":");
            builder_append_field(&builder, u->password, sizeof(u->password));
        }
        builder_append_literal(&builder, "@");
    }
    if (u->host[0]) {
        size_t host_length = bounded_string_length(u->host, sizeof(u->host));
        if (host_length == sizeof(u->host)) builder.failed = 1;
        int is_ipv6 = !builder.failed && memchr(u->host, ':', host_length);
        if (is_ipv6) builder_append_literal(&builder, "[");
        builder_append(&builder, u->host, host_length);
        if (is_ipv6) builder_append_literal(&builder, "]");
        if (u->port[0]) {
            builder_append_literal(&builder, ":");
            builder_append_field(&builder, u->port, sizeof(u->port));
        }
    }
    builder_append_field(&builder, u->path, sizeof(u->path));
    if (u->raw_query[0]) {
        builder_append_literal(&builder, "?");
        builder_append_field(&builder, u->raw_query, sizeof(u->raw_query));
    }
    if (u->fragment[0]) {
        builder_append_literal(&builder, "#");
        builder_append_field(&builder, u->fragment, sizeof(u->fragment));
    }
    return builder_result(&builder);
}

int neverc_url_hostname(const neverc_url_t *u, char *buf, size_t cap) {
    if (!u) return -1;
    url_builder_t builder;
    builder_init(&builder, buf, cap);
    builder_append_field(&builder, u->host, sizeof(u->host));
    return builder_result(&builder);
}

int neverc_url_request_uri(const neverc_url_t *u, char *buf, size_t cap) {
    if (!u) return -1;
    url_builder_t builder;
    builder_init(&builder, buf, cap);
    if (u->path[0])
        builder_append_field(&builder, u->path, sizeof(u->path));
    else
        builder_append_literal(&builder, "/");
    if (u->raw_query[0]) {
        builder_append_literal(&builder, "?");
        builder_append_field(&builder, u->raw_query, sizeof(u->raw_query));
    }
    return builder_result(&builder);
}

int neverc_url_is_abs(const neverc_url_t *u) {
    return u && u->scheme[0] != '\0';
}

int neverc_url_values_parse(neverc_url_values_t *v, const char *query) {
    if (!v) return -1;
    v->count = 0;
    if (!query || !*query) return 0;

    const char *p = query;
    while (*p) {
        if (v->count >= 64) goto invalid;
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        const char *eq = NULL;
        for (const char *c = p; c < end; c++)
            if (*c == '=') { eq = c; break; }

        if (eq) {
            char key_buf[sizeof(v->keys[0]) * 3];
            char val_buf[sizeof(v->vals[0]) * 3];
            if (copy_exact(key_buf, sizeof(key_buf), p,
                           (size_t)(eq - p)) != 0 ||
                copy_exact(val_buf, sizeof(val_buf), eq + 1,
                           (size_t)(end - eq - 1)) != 0)
                goto invalid;
            int key_length = neverc_url_query_unescape(
                key_buf, v->keys[v->count], sizeof(v->keys[0]));
            int value_length = neverc_url_query_unescape(
                val_buf, v->vals[v->count], sizeof(v->vals[0]));
            if (key_length < 0 || value_length < 0 ||
                (size_t)key_length >= sizeof(v->keys[0]) ||
                (size_t)value_length >= sizeof(v->vals[0]))
                goto invalid;
        } else {
            char key_buf[sizeof(v->keys[0]) * 3];
            if (copy_exact(key_buf, sizeof(key_buf), p,
                           (size_t)(end - p)) != 0)
                goto invalid;
            int key_length = neverc_url_query_unescape(
                key_buf, v->keys[v->count], sizeof(v->keys[0]));
            if (key_length < 0 ||
                (size_t)key_length >= sizeof(v->keys[0]))
                goto invalid;
            v->vals[v->count][0] = '\0';
        }
        v->count++;

        if (!amp) break;
        p = amp + 1;
    }
    return 0;

invalid:
    v->count = 0;
    return -1;
}

const char *neverc_url_values_get(const neverc_url_values_t *v, const char *key) {
    if (!v || !key || v->count < 0 || v->count > 64) return NULL;
    for (int i = 0; i < v->count; i++)
        if (fixed_string_equal(v->keys[i], sizeof(v->keys[i]), key))
            return v->vals[i];
    return NULL;
}

void neverc_url_values_set(neverc_url_values_t *v, const char *key, const char *val) {
    if (!v || !key || !val || v->count < 0 || v->count > 64) return;
    size_t key_length = strlen(key);
    size_t value_length = strlen(val);
    if (key_length >= sizeof(v->keys[0]) ||
        value_length >= sizeof(v->vals[0]))
        return;
    for (int i = 0; i < v->count; i++) {
        if (fixed_string_equal(v->keys[i], sizeof(v->keys[i]), key)) {
            copy_exact(v->vals[i], sizeof(v->vals[0]), val, value_length);
            return;
        }
    }
    if (v->count < 64) {
        copy_exact(v->keys[v->count], sizeof(v->keys[0]), key, key_length);
        copy_exact(v->vals[v->count], sizeof(v->vals[0]), val,
                   value_length);
        v->count++;
    }
}

int neverc_url_values_encode(const neverc_url_values_t *v, char *buf, size_t cap) {
    if (!v || v->count < 0 || v->count > 64) return -1;
    url_builder_t builder;
    builder_init(&builder, buf, cap);
    for (int i = 0; i < v->count; i++) {
        size_t key_length = bounded_string_length(
            v->keys[i], sizeof(v->keys[i]));
        size_t value_length = bounded_string_length(
            v->vals[i], sizeof(v->vals[i]));
        if (key_length == sizeof(v->keys[i]) ||
            value_length == sizeof(v->vals[i]))
            return -1;

        char escaped_key[sizeof(v->keys[i]) * 3];
        char escaped_value[sizeof(v->vals[i]) * 3];
        int escaped_key_length = neverc_url_query_escape(
            v->keys[i], escaped_key, sizeof(escaped_key));
        int escaped_value_length = neverc_url_query_escape(
            v->vals[i], escaped_value, sizeof(escaped_value));
        if (escaped_key_length < 0 || escaped_value_length < 0)
            return -1;
        if (i > 0) builder_append_literal(&builder, "&");
        builder_append(&builder, escaped_key, (size_t)escaped_key_length);
        builder_append_literal(&builder, "=");
        builder_append(&builder, escaped_value,
                       (size_t)escaped_value_length);
    }
    return builder_result(&builder);
}

/*
 * Per-mode escape tables (1 = byte must be percent-encoded). Compile-time
 * constants, so the hot loop's per-byte test is a single table load and the
 * encoders are reentrant with no lazy-init data race (a lazily built table can
 * be observed half-initialized by another thread on weakly-ordered targets such
 * as arm64).
 *
 * path:  left as-is only for ALPHA / DIGIT and "-_.~/:@"  (0); else escape (1).
 * query: left as-is only for ALPHA / DIGIT and "-_.~"     (0); else escape (1).
 */
static const unsigned char esc_table_path[256] = {
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,1,1,1,1,1,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,1,1,1,1,0,
    1,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,1,1,1,0,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
};
static const unsigned char esc_table_query[256] = {
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,0,0,1,
    0,0,0,0,0,0,0,0, 0,0,1,1,1,1,1,1,
    1,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,1,1,1,1,0,
    1,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,1,1,1,0,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
};

static int percent_encode(const char *s, char *buf, size_t cap,
                           const unsigned char *esc) {
    static const char hexU[] = "0123456789ABCDEF";
    if (!s || (cap > 0 && !buf)) return -1;
    if (cap > 0) buf[0] = '\0';

    size_t output_length = 0;
    for (size_t input_offset = 0; s[input_offset]; input_offset++) {
        unsigned char c = (unsigned char)s[input_offset];
        size_t encoded_length = esc[c] ? 3U : 1U;
        if (encoded_length > SIZE_MAX - output_length ||
            output_length + encoded_length > (size_t)INT_MAX)
            return -1;
        if (esc[c]) {
            if (cap > 0 && output_length < cap - 1)
                buf[output_length] = '%';
            if (cap > 0 && output_length + 1 < cap - 1)
                buf[output_length + 1] = hexU[c >> 4];
            if (cap > 0 && output_length + 2 < cap - 1)
                buf[output_length + 2] = hexU[c & 0x0F];
        } else {
            if (cap > 0 && output_length < cap - 1)
                buf[output_length] = (char)c;
        }
        output_length += encoded_length;
    }

    if (cap > 0) {
        size_t terminator = output_length < cap
            ? output_length : cap - 1;
        buf[terminator] = '\0';
    }
    return (int)output_length;
}

static int percent_decode(const char *s, char *buf, size_t cap,
                          int plus_as_space) {
    if (!s || (cap > 0 && !buf)) return -1;
    if (cap > 0) buf[0] = '\0';

    size_t input_offset = 0;
    size_t output_length = 0;
    while (s[input_offset]) {
        unsigned char decoded = (unsigned char)s[input_offset];
        if (decoded == '%') {
            if (!s[input_offset + 1] || !s[input_offset + 2]) goto malformed;
            int high = hex_val[(unsigned char)s[input_offset + 1]];
            int low = hex_val[(unsigned char)s[input_offset + 2]];
            if ((high | low) < 0) goto malformed;
            decoded = (unsigned char)((high << 4) | low);
            if (decoded == 0) goto malformed;
            input_offset += 3;
        } else {
            if (plus_as_space && decoded == '+') decoded = ' ';
            input_offset++;
        }
        if (output_length == (size_t)INT_MAX) goto malformed;
        if (cap > 0 && output_length < cap - 1)
            buf[output_length] = (char)decoded;
        output_length++;
    }

    if (cap > 0) {
        size_t terminator = output_length < cap
            ? output_length : cap - 1;
        buf[terminator] = '\0';
    }
    return (int)output_length;

malformed:
    if (cap > 0) {
        size_t terminator = output_length < cap
            ? output_length : cap - 1;
        buf[terminator] = '\0';
    }
    return -1;
}

int neverc_url_path_escape(const char *s, char *buf, size_t cap) {
    return percent_encode(s, buf, cap, esc_table_path);
}

int neverc_url_path_unescape(const char *s, char *buf, size_t cap) {
    return percent_decode(s, buf, cap, 0);
}

int neverc_url_query_escape(const char *s, char *buf, size_t cap) {
    return percent_encode(s, buf, cap, esc_table_query);
}

int neverc_url_query_unescape(const char *s, char *buf, size_t cap) {
    return percent_decode(s, buf, cap, 1);
}
