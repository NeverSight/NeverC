#include "neverc/std/net/url.h"
#include "neverc/std/net/netip.h"
#include "../idna_inc.h"
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

#define PCT_PLUS 1
#define PCT_HOST 2
#define PCT_ZONE 4
#define PCT_ALLOW_NUL 8

static int percent_decode(const char *s, char *buf, size_t cap, int flags);

/* Decode one path octet. Malformed or truncated `%XX` cannot be shown safe. */
static int path_decode_next(const char *path, size_t n, size_t i,
                            unsigned char *out, size_t *consumed) {
    if (i >= n) return 0;
    unsigned char c = (unsigned char)path[i];
    if (c != '%') {
        *out = c;
        *consumed = 1;
        return 1;
    }
    if (i + 2 >= n) return 0;
    int high = hex_val[(unsigned char)path[i + 1]];
    int low = hex_val[(unsigned char)path[i + 2]];
    if ((high | low) < 0) return 0;
    *out = (unsigned char)((high << 4) | low);
    *consumed = 3;
    return 1;
}

/* True if path is `//...` or `/\...`, including after percent-decode.
 * `/%2f/evil.com` and `/%5cevil.com` must not look like a same-origin path.
 * Encoded C0 (`/%09//evil`, `/%0d//evil`) is skipped after decode so a
 * later strip of TAB/CR/LF cannot turn a "same-origin" path into leftover
 * Host. Walks the full `n` bytes so a 1023-byte C0 prefix cannot hide a
 * later `//`, and a raw NUL cannot truncate the scan. */
static int path_n_is_protocol_relative(const char *path, size_t n) {
    if (!path || n < 1 || path[0] != '/')
        return 0;
    if (n >= 2 && (path[1] == '/' || path[1] == '\\'))
        return 1;

    size_t i = 0;
    unsigned char decoded;
    size_t consumed;
    if (!path_decode_next(path, n, i, &decoded, &consumed))
        return 1;
    if (decoded != '/')
        return 0;
    i += consumed;
    while (i < n) {
        if (!path_decode_next(path, n, i, &decoded, &consumed))
            return 1;
        i += consumed;
        if (decoded < 0x20 || decoded == 0x7f)
            continue;
        return decoded == '/' || decoded == '\\';
    }
    return 0;
}

int neverc_url_path_is_protocol_relative(const char *path) {
    if (!path) return 0;
    return path_n_is_protocol_relative(path, strlen(path));
}

int neverc_url_path_n_is_protocol_relative(const char *path, size_t n) {
    return path_n_is_protocol_relative(path, n);
}

/* RFC 3986 reg-name octets that may appear unescaped. */
static int host_ascii_ok(unsigned char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9'))
        return 1;
    switch (c) {
    case '-': case '.': case '_': case '~':
    case '!': case '$': case '&': case '\'':
    case '(': case ')': case '*': case '+':
    case ',': case ';': case '=':
        return 1;
    default:
        return 0;
    }
}

/* Go encodeHost / encodeZone percent-decode: also allow :[]<>" */
static int host_or_zone_pct_ok(unsigned char c) {
    if (host_ascii_ok(c)) return 1;
    switch (c) {
    case ':': case '[': case ']': case '<': case '>': case '"':
        return 1;
    default:
        return 0;
    }
}

/* Reject malformed %XX and encoded NUL. Userinfo is stored raw so
 * neverc_url_string can round-trip, but RFC 3986 still forbids invalid
 * percent sequences. */
static int valid_pct_encoded(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (s[i] != '%') continue;
        if (i + 2 >= len) return 0;
        int high = hex_val[(unsigned char)s[i + 1]];
        int low = hex_val[(unsigned char)s[i + 2]];
        if ((high | low) < 0) return 0;
        if (((high << 4) | low) == 0) return 0;
        i += 2;
    }
    return 1;
}

static int copy_unescaped_host(char *dst, size_t cap, const char *src,
                               size_t len) {
    if (!dst || !src || cap == 0 || len >= 1024) return -1;
    char encoded[1024];
    memcpy(encoded, src, len);
    encoded[len] = '\0';
    int n = percent_decode(encoded, dst, cap, PCT_HOST);
    if (n < 0 || (size_t)n >= cap)
        return -1;
    return 0;
}

/* RFC 6874: zone IDs are introduced by %25. Bare `%eth0` is invalid. */
static int unescape_ipv6_literal(char *dst, size_t cap, const char *src,
                                 size_t len) {
    if (!dst || !src || cap == 0 || len >= 1024) return -1;
    char encoded[1024];
    memcpy(encoded, src, len);
    encoded[len] = '\0';

    char *zone = strstr(encoded, "%25");
    if (!zone) {
        int n = percent_decode(encoded, dst, cap, PCT_HOST);
        return (n >= 0 && (size_t)n < cap) ? 0 : -1;
    }

    char hostpart[1024];
    size_t hlen = (size_t)(zone - encoded);
    if (copy_exact(hostpart, sizeof(hostpart), encoded, hlen) != 0)
        return -1;

    char hostbuf[256];
    char zonebuf[256];
    int hn = percent_decode(hostpart, hostbuf, sizeof(hostbuf), PCT_HOST);
    int zn = percent_decode(zone, zonebuf, sizeof(zonebuf), PCT_ZONE);
    if (hn < 0 || zn < 0)
        return -1;
    if ((size_t)hn + (size_t)zn >= cap)
        return -1;
    memcpy(dst, hostbuf, (size_t)hn);
    memcpy(dst + hn, zonebuf, (size_t)zn);
    dst[hn + zn] = '\0';
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

/* Go net/url shouldEscape(c, encodeHost): alnum, unreserved -_.~,
 * sub-delims, and :[]<>" stay literal. */
static int url_host_byte_should_escape(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9'))
        return 0;
    switch (c) {
    case '!': case '$': case '&': case '\'': case '(': case ')':
    case '*': case '+': case ',': case ';': case '=':
    case ':': case '[': case ']': case '<': case '>': case '"':
    case '-': case '_': case '.': case '~':
        return 0;
    default:
        return 1;
    }
}

/* Go encodeUserPassword: keep existing %XX; escape @ / ? / : so
 * `user:p@ss` String()s as `user:p%40ss` and `user:p:ss` as
 * `user:p%3Ass`. */
static int userinfo_byte_should_escape(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9'))
        return 0;
    switch (c) {
    case '-': case '_': case '.': case '~':
    case '$': case '&': case '+': case ',':
    case ';': case '=':
        return 0;
    default:
        return 1;
    }
}

static int is_hex_byte(unsigned char c) {
    return hex_val[c] >= 0;
}

static void builder_append_userinfo(url_builder_t *builder, const char *field,
                                    size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    size_t length = bounded_string_length(field, capacity);
    if (length == capacity) builder->failed = 1;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)field[i];
        if (c == '%' && i + 2 < length &&
            is_hex_byte((unsigned char)field[i + 1]) &&
            is_hex_byte((unsigned char)field[i + 2])) {
            builder_append(builder, field + i, 3);
            i += 2;
        } else if (userinfo_byte_should_escape(c)) {
            char enc[3];
            enc[0] = '%';
            enc[1] = hex[c >> 4];
            enc[2] = hex[c & 0xF];
            builder_append(builder, enc, 3);
        } else {
            builder_append(builder, field + i, 1);
        }
    }
}

static void builder_append_host(url_builder_t *builder, const char *host,
                                size_t host_length) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < host_length; i++) {
        unsigned char c = (unsigned char)host[i];
        if (c == '%') {
            builder_append_literal(builder, "%25");
        } else if (url_host_byte_should_escape(c)) {
            char enc[3];
            enc[0] = '%';
            enc[1] = hex[c >> 4];
            enc[2] = hex[c & 0xF];
            builder_append(builder, enc, 3);
        } else {
            builder_append(builder, host + i, 1);
        }
    }
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
    /* RFC 3986 / Go validOptionalPort: empty port after ':' is allowed. */
    if (length >= capacity) return -1;
    if (length == 0) {
        port[0] = '\0';
        return 0;
    }
    unsigned value = 0;
    for (const char *p = start; p < end; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < '0' || c > '9') return -1;
        value = value * 10U + (unsigned)(c - '0');
        if (value > 65535U) return -1;
    }
    return copy_exact(port, capacity, start, length);
}

/* RFC 3986 userinfo = *( unreserved / pct-encoded / sub-delims / ":" ).
 * Go also permits '@' inside userinfo (last '@' still splits host). */
static int valid_userinfo(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9'))
            continue;
        switch (c) {
        case '-': case '.': case '_': case ':': case '~':
        case '!': case '$': case '&': case '\'':
        case '(': case ')': case '*': case '+': case ',':
        case ';': case '=': case '%': case '@':
            continue;
        default:
            return 0;
        }
    }
    return 1;
}

static int valid_host_text(const char *host, size_t length) {
    if (length == 0) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)host[i];
        if (c <= 0x20 || c == 0x7f || c == '/' || c == '\\' ||
            c == '?' || c == '#' || c == '@' || c == '[' || c == ']' ||
            c == ':')
            return 0;
    }
    return 1;
}

static int parse_url(neverc_url_t *u, const char *raw_url, int via_request) {
    if (!u) return -1;
    memset(u, 0, sizeof(*u));
    if (!raw_url || !*raw_url) return -1;

    size_t raw_length = strlen(raw_url);
    for (size_t i = 0; i < raw_length; i++) {
        unsigned char c = (unsigned char)raw_url[i];
        if (c <= 0x20 || c == 0x7f) return -1;
    }

    if (via_request && raw_length == 1 && raw_url[0] == '*') {
        u->path[0] = '*';
        u->path[1] = '\0';
        return 0;
    }
    if (via_request && memchr(raw_url, '#', raw_length))
        return -1;

    const char *p = raw_url;
    const char *raw_end = raw_url + raw_length;
    const char *scheme_end = strchr(p, ':');
    const char *first_delimiter = strpbrk(p, "/?#");
    int has_authority = 0;
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
        has_authority = 1;
    } else if (p[0] == '/' && p[1] == '/') {
        /* Go net/url: a /// prefix is a path-absolute relative-ref
         * (and origin-form), not a network-path. `GET //evil.com` is
         * still not a valid request-target. */
        if (p[2] == '/') {
            has_authority = 0;
        } else if (via_request) {
            return -1;
        } else {
            p += 2;
            has_authority = 1;
        }
    }

    if (has_authority) {
        const char *authority_end = p + strcspn(p, "/?#");
        if (authority_end == p) {
            /* Hierarchical empty host: file:///tmp/foo. Reject it for
             * special URLs so http:///evil is not an empty-host parse. */
            if (!u->scheme[0] ||
                (*p != '/' && *p != '?' && *p != '#') ||
                strcmp(u->scheme, "http") == 0 ||
                strcmp(u->scheme, "https") == 0 ||
                strcmp(u->scheme, "ws") == 0 ||
                strcmp(u->scheme, "wss") == 0)
                return -1;
        } else {
        /* WHATWG special-URLs treat '\' as '/'. Leaving it in userinfo lets
         * http://evil.com\@good.com/ parse as host good.com. */
        for (const char *c = p; c < authority_end; c++)
            if (*c == '\\') return -1;
        const char *at = NULL;
        for (const char *c = p; c < authority_end; c++)
            if (*c == '@') at = c;

        if (at) {
            const char *colon = NULL;
            for (const char *c = p; c < at; c++)
                if (*c == ':') { colon = c; break; }
            if (at == p) return -1;
            if (!valid_userinfo(p, (size_t)(at - p)))
                return -1;
            if (colon) {
                size_t user_len = (size_t)(colon - p);
                size_t pass_len = (size_t)(at - colon - 1);
                if (!valid_pct_encoded(p, user_len) ||
                    !valid_pct_encoded(colon + 1, pass_len) ||
                    copy_exact(u->user, sizeof(u->user), p, user_len) != 0 ||
                    copy_exact(u->password, sizeof(u->password), colon + 1,
                               pass_len) != 0)
                    return -1;
                u->has_password = 1;
            } else {
                size_t user_len = (size_t)(at - p);
                if (!valid_pct_encoded(p, user_len) ||
                    copy_exact(u->user, sizeof(u->user), p, user_len) != 0)
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
             * may be enclosed in brackets. IPv4 and non-IP text are errors.
             * RFC 6874 `%25` in a zone ID unescapes to `%`; bare `%eth0`
             * is an invalid host escape. */
            if (unescape_ipv6_literal(hostbuf, sizeof(hostbuf), p + 1, hlen) != 0 ||
                neverc_netip_parse_addr(hostbuf, &addr) != 0 ||
                addr.is_v4 ||
                copy_exact(u->host, sizeof(u->host), hostbuf, strlen(hostbuf)) != 0)
                return -1;
            if (bracket + 1 < authority_end) {
                if (bracket[1] != ':' ||
                    parse_port(bracket + 2, authority_end, u->port,
                               sizeof(u->port)) != 0)
                    return -1;
                u->has_port = 1;
            }
        } else {
            const char *colon = NULL;
            for (const char *c = p; c < host_end; c++) {
                if (*c != ':') continue;
                if (colon) return -1;
                colon = c;
            }
            const char *name_end = colon ? colon : host_end;
            char hostbuf[256];
            size_t raw_len = (size_t)(name_end - p);
            if (copy_unescaped_host(hostbuf, sizeof(hostbuf), p, raw_len) != 0)
                return -1;
            {
                char idna[256];
                if (neverc_idna_to_ascii(hostbuf, idna, sizeof(idna)) != 0)
                    return -1;
                if (!valid_host_text(idna, strlen(idna)) ||
                    copy_exact(u->host, sizeof(u->host), idna, strlen(idna)) != 0)
                    return -1;
            }
            if (colon) {
                if (parse_port(colon + 1, host_end, u->port,
                               sizeof(u->port)) != 0)
                    return -1;
                u->has_port = 1;
            }
        }
        p = authority_end;
        }
    }

    const char *fragment = memchr(p, '#', (size_t)(raw_end - p));
    const char *before_fragment = fragment ? fragment : raw_end;
    const char *query = memchr(p, '?', (size_t)(before_fragment - p));
    const char *path_end = query ? query : before_fragment;
    size_t path_len = (size_t)(path_end - p);
    if (copy_exact(u->path, sizeof(u->path), p, path_len) != 0)
        return -1;
    /* WHATWG special-URLs treat '\' as '/'. `/\evil.com` as a Location
     * becomes protocol-relative `//evil.com`. */
    if (memchr(u->path, '\\', path_len))
        return -1;
    if (!valid_pct_encoded(u->path, path_len))
        return -1;
    if (query) {
        /* Go ForceQuery: a '?' is kept even when RawQuery is empty so
         * `http://host/path?` does not round-trip as `http://host/path`. */
        u->has_query = 1;
        if (copy_exact(u->raw_query, sizeof(u->raw_query), query + 1,
                       (size_t)(before_fragment - query - 1)) != 0)
            return -1;
    }
    if (fragment) {
        size_t frag_len = (size_t)(raw_end - fragment - 1);
        if (copy_exact(u->fragment, sizeof(u->fragment), fragment + 1,
                       frag_len) != 0)
            return -1;
        if (!valid_pct_encoded(u->fragment, frag_len))
            return -1;
    }

    if (via_request && !u->scheme[0] && !u->host[0] &&
        u->path[0] != '/' &&
        !(u->path[0] == '*' && u->path[1] == '\0' && !u->has_query &&
          raw_length == 1 && raw_url[0] == '*'))
        return -1;

    return 0;
}

int neverc_url_parse(neverc_url_t *u, const char *raw_url) {
    return parse_url(u, raw_url, 0);
}

int neverc_url_parse_request_uri(neverc_url_t *u, const char *raw_url) {
    return parse_url(u, raw_url, 1);
}

int neverc_url_string(const neverc_url_t *u, char *buf, size_t cap) {
    if (!u) return -1;
    url_builder_t builder;
    builder_init(&builder, buf, cap);
    if (u->scheme[0]) {
        builder_append_field(&builder, u->scheme, sizeof(u->scheme));
        builder_append_literal(&builder, "://");
    } else if (u->host[0] || u->user[0] || u->has_password ||
               u->password[0]) {
        builder_append_literal(&builder, "//");
    }
    if (u->user[0] || u->has_password || u->password[0]) {
        builder_append_userinfo(&builder, u->user, sizeof(u->user));
        if (u->has_password || u->password[0]) {
            builder_append_literal(&builder, ":");
            builder_append_userinfo(&builder, u->password, sizeof(u->password));
        }
        builder_append_literal(&builder, "@");
    }
    if (u->host[0]) {
        size_t host_length = bounded_string_length(u->host, sizeof(u->host));
        if (host_length == sizeof(u->host)) builder.failed = 1;
        int is_ipv6 = !builder.failed && memchr(u->host, ':', host_length);
        if (is_ipv6) builder_append_literal(&builder, "[");
        builder_append_host(&builder, u->host, host_length);
        if (is_ipv6) builder_append_literal(&builder, "]");
        if (u->has_port || u->port[0]) {
            builder_append_literal(&builder, ":");
            builder_append_field(&builder, u->port, sizeof(u->port));
        }
    }
    builder_append_field(&builder, u->path, sizeof(u->path));
    if (u->has_query || u->raw_query[0]) {
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
    if (u->path[0]) {
        size_t path_length = bounded_string_length(
            u->path, sizeof(u->path));
        if (path_length == sizeof(u->path)) {
            builder.failed = 1;
            return builder_result(&builder);
        }
        /* origin-form `//host` is protocol-relative. Prefix so a
         * Request-URI cannot retarget a different authority. Encoded
         * `/%2f...` / `/%5c...` decode to the same form. */
        if (neverc_url_path_n_is_protocol_relative(u->path, path_length))
            builder_append_literal(&builder, "/.");
        builder_append(&builder, u->path, path_length);
    } else
        builder_append_literal(&builder, "/");
    if (u->has_query || u->raw_query[0]) {
        builder_append_literal(&builder, "?");
        builder_append_field(&builder, u->raw_query, sizeof(u->raw_query));
    }
    return builder_result(&builder);
}

int neverc_url_is_abs(const neverc_url_t *u) {
    return u && u->scheme[0] != '\0';
}

static int ascii_host_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

int neverc_url_is_safe_redirect(const char *raw_url, const char *allowed_host) {
    neverc_url_t u;
    if (neverc_url_parse(&u, raw_url) != 0)
        return 0;
    if (u.user[0] || u.has_password || u.password[0])
        return 0;
    if (strchr(raw_url, '\\'))
        return 0;

    if (u.scheme[0]) {
        if (strcmp(u.scheme, "http") != 0 && strcmp(u.scheme, "https") != 0)
            return 0;
        if (!u.host[0] || !allowed_host || !allowed_host[0])
            return 0;
        return ascii_host_equal(u.host, allowed_host);
    }
    /* Protocol-relative `//evil.com` has a host and no scheme.
     * Encoded `/%2f/evil.com` / `/%5cevil.com` decode to the same. */
    if (u.host[0])
        return 0;
    if (neverc_url_path_is_protocol_relative(u.path))
        return 0;
    if (u.path[0] && u.path[0] != '/')
        return 0;
    return 1;
}

int neverc_url_values_parse(neverc_url_values_t *v, const char *query) {
    if (!v) return -1;
    v->count = 0;
    if (!query || !*query) return 0;
    /* Go 1.17+ ParseQuery: a raw semicolon is an invalid separator, not
     * part of a value. Encoded %3B remains allowed. */
    if (strchr(query, ';'))
        return -1;

    const char *p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        /* Go ParseQuery skips empty "&" segments (`&&`, leading '&').
         * `=value` is kept as an empty key. */
        if (end == p) {
            if (!amp) break;
            p = amp + 1;
            continue;
        }
        if (v->count >= 64) goto invalid;
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
    int first = -1;
    int write = 0;
    for (int i = 0; i < v->count; i++) {
        if (fixed_string_equal(v->keys[i], sizeof(v->keys[i]), key)) {
            if (first < 0) {
                first = write;
                copy_exact(v->vals[i], sizeof(v->vals[0]), val, value_length);
            } else {
                continue;
            }
        }
        if (write != i) {
            memcpy(v->keys[write], v->keys[i], sizeof(v->keys[0]));
            memcpy(v->vals[write], v->vals[i], sizeof(v->vals[0]));
        }
        write++;
    }
    if (first >= 0) {
        v->count = write;
        return;
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
 * path:  Go url.PathEscape (encodePathSegment). Unescaped: ALPHA / DIGIT
 *        and "-_.~$&+:@=". Escapes "/" ";" "," "?" and every other byte.
 * query: Go url.QueryEscape (encodeQueryComponent). Unescaped: ALPHA /
 *        DIGIT and "-_.~". Space becomes '+'; every other byte is %XX.
 */
static const unsigned char esc_table_path[256] = {
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,0,1,0,1, 1,1,1,0,1,0,0,1,
    0,0,0,0,0,0,0,0, 0,0,0,1,1,0,1,1,
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
        int query_plus = (esc == esc_table_query && c == ' ');
        size_t encoded_length = (esc[c] && !query_plus) ? 3U : 1U;
        if (encoded_length > SIZE_MAX - output_length ||
            output_length + encoded_length > (size_t)INT_MAX)
            return -1;
        if (query_plus) {
            if (cap > 0 && output_length < cap - 1)
                buf[output_length] = '+';
        } else if (esc[c]) {
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

static int percent_decode(const char *s, char *buf, size_t cap, int flags) {
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
            int is_pct25 = (s[input_offset + 1] == '2' &&
                            s[input_offset + 2] == '5');
            /* RFC 3986: host %-encoding is only for non-ASCII, except
             * RFC 6874 `%25` in IPv6 zone IDs. */
            if ((flags & PCT_HOST) && decoded < 0x80 && !is_pct25)
                goto malformed;
            if ((flags & PCT_ZONE) && !is_pct25 && decoded != ' ' &&
                (decoded < 0x80 && !host_or_zone_pct_ok(decoded)))
                goto malformed;
            if (decoded == 0 && !(flags & PCT_ALLOW_NUL))
                goto malformed;
            input_offset += 3;
        } else {
            if ((flags & PCT_PLUS) && decoded == '+') decoded = ' ';
            /* Go encodeHost: raw ASCII that shouldEscape keeps, including
             * '"<>', is accepted; encoded %3C / %22 stay rejected. */
            if ((flags & (PCT_HOST | PCT_ZONE)) && decoded < 0x80 &&
                decoded != '%' && !host_or_zone_pct_ok(decoded) &&
                !((flags & PCT_ZONE) && decoded == ' '))
                goto malformed;
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
    if (cap > 0 && buf)
        buf[0] = '\0';
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
    return percent_decode(s, buf, cap, PCT_PLUS);
}
