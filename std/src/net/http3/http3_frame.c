/*
 * HTTP/3 Frame Codec (RFC 9114 §7)
 *
 * HTTP/3 frames are different from HTTP/2 frames. They use QUIC varint
 * encoding for type and length fields:
 *
 *   Frame {
 *     Type (varint),
 *     Length (varint),
 *     Frame Payload (..),
 *   }
 *
 * The frame layer is simpler than HTTP/2 because QUIC handles:
 *   - Multiplexing (via streams)
 *   - Flow control
 *   - Ordering (per-stream)
 */

#include "neverc/std/net/http3.h"
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/url.h"
#include <string.h>
#include <stdlib.h>

extern int neverc_quic_varint_decode(const uint8_t *buf, size_t len,
                                      uint64_t *value, size_t *consumed);
extern int neverc_quic_varint_encode(uint64_t value, uint8_t *buf, size_t cap,
                                      size_t *written);
extern size_t neverc_quic_varint_len(uint64_t value);

typedef neverc_qpack_header_t qpack_header_t;

/* Frame types */
#define H3_FRAME_DATA           0x00
#define H3_FRAME_HEADERS        0x01
#define H3_FRAME_CANCEL_PUSH    0x03
#define H3_FRAME_SETTINGS       0x04
#define H3_FRAME_PUSH_PROMISE   0x05
#define H3_FRAME_GOAWAY         0x07
#define H3_FRAME_MAX_PUSH_ID    0x0D

/* Settings IDs */
#define H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY 0x01
#define H3_SETTINGS_MAX_FIELD_SECTION_SIZE   0x06
#define H3_SETTINGS_QPACK_BLOCKED_STREAMS    0x07

/* ======================================================================
 * Frame Header Parsing
 * ====================================================================== */

#ifndef H3_FRAME_TYPES_DEFINED
#define H3_FRAME_TYPES_DEFINED
typedef struct {
    uint64_t type;
    uint64_t length;
    size_t   header_size;  /* bytes consumed by type + length encoding */
} h3_frame_header_t;

int neverc_h3_parse_frame_header(const uint8_t *buf, size_t len,
                                   h3_frame_header_t *hdr) {
    if (!buf || !hdr || len == 0) return -1;

    const uint8_t *p = buf;
    size_t rem = len;
    size_t consumed;

    if (neverc_quic_varint_decode(p, rem, &hdr->type, &consumed) != 0)
        return -1;
    p += consumed; rem -= consumed;

    if (neverc_quic_varint_decode(p, rem, &hdr->length, &consumed) != 0)
        return -1;
    p += consumed; rem -= consumed;
    if (hdr->length > rem) return -1;

    hdr->header_size = (size_t)(p - buf);
    return 0;
}

/* ======================================================================
 * SETTINGS Frame (RFC 9114 §7.2.4)
 *
 * Settings {
 *   Identifier (varint),
 *   Value (varint),
 * } *
 * ====================================================================== */

typedef struct {
    uint64_t qpack_max_table_capacity;   /* default 0 */
    uint64_t max_field_section_size;      /* default unlimited (UINT64_MAX) */
    uint64_t qpack_blocked_streams;       /* default 0 */
} h3_settings_t;
#endif /* H3_FRAME_TYPES_DEFINED */

void neverc_h3_settings_default(h3_settings_t *s) {
    if (!s) return;
    s->qpack_max_table_capacity = 0;
    s->max_field_section_size = 16 * 1024 * 1024;  /* 16MB */
    s->qpack_blocked_streams = 0;
}

int neverc_h3_settings_encode(const h3_settings_t *s,
                                uint8_t *buf, size_t cap, size_t *written) {
    if (!s || !buf || !written ||
        s->qpack_max_table_capacity > ((UINT64_C(1) << 62) - 1) ||
        s->qpack_blocked_streams > ((UINT64_C(1) << 62) - 1) ||
        (s->max_field_section_size != UINT64_MAX &&
         s->max_field_section_size > ((UINT64_C(1) << 62) - 1)))
        return -1;
    *written = 0;
    /* Encode settings payload first to get its length */
    uint8_t payload[256];
    size_t plen = 0, w;

    if (s->qpack_max_table_capacity > 0) {
        if (neverc_quic_varint_encode(H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY, payload + plen, sizeof(payload) - plen, &w) != 0) return -1; plen += w;
        if (neverc_quic_varint_encode(s->qpack_max_table_capacity, payload + plen, sizeof(payload) - plen, &w) != 0) return -1; plen += w;
    }
    if (s->max_field_section_size != UINT64_MAX) {
        if (neverc_quic_varint_encode(H3_SETTINGS_MAX_FIELD_SECTION_SIZE, payload + plen, sizeof(payload) - plen, &w) != 0) return -1; plen += w;
        if (neverc_quic_varint_encode(s->max_field_section_size, payload + plen, sizeof(payload) - plen, &w) != 0) return -1; plen += w;
    }
    if (s->qpack_blocked_streams > 0) {
        if (neverc_quic_varint_encode(H3_SETTINGS_QPACK_BLOCKED_STREAMS, payload + plen, sizeof(payload) - plen, &w) != 0) return -1; plen += w;
        if (neverc_quic_varint_encode(s->qpack_blocked_streams, payload + plen, sizeof(payload) - plen, &w) != 0) return -1; plen += w;
    }

    /* Frame header: Type + Length */
    size_t pos = 0;
    if (neverc_quic_varint_encode(H3_FRAME_SETTINGS, buf + pos, cap - pos, &w) != 0) return -1; pos += w;
    if (neverc_quic_varint_encode(plen, buf + pos, cap - pos, &w) != 0) return -1; pos += w;

    if (pos + plen > cap) return -1;
    memcpy(buf + pos, payload, plen);
    pos += plen;

    *written = pos;
    return 0;
}

static int h3_settings_id_duplicate(const uint8_t *payload, size_t prefix_len,
                                    uint64_t id) {
    const uint8_t *scan = payload;
    size_t rem = prefix_len;
    while (rem > 0) {
        uint64_t prev_id, prev_val;
        size_t used;
        if (neverc_quic_varint_decode(scan, rem, &prev_id, &used) != 0)
            return -1;
        scan += used;
        rem -= used;
        if (neverc_quic_varint_decode(scan, rem, &prev_val, &used) != 0)
            return -1;
        scan += used;
        rem -= used;
        if (prev_id == id) return 1;
    }
    return 0;
}

int neverc_h3_settings_decode(const uint8_t *payload, size_t len,
                                h3_settings_t *s) {
    if (!s || (!payload && len != 0)) return -1;
    s->qpack_max_table_capacity = 0;
    s->max_field_section_size = UINT64_MAX;
    s->qpack_blocked_streams = 0;

    const uint8_t *p = payload;
    size_t rem = len;

    unsigned seen = 0;
    while (rem > 0) {
        const uint8_t *pair = p;
        uint64_t id, val;
        size_t consumed;

        if (neverc_quic_varint_decode(p, rem, &id, &consumed) != 0) return -1;
        p += consumed; rem -= consumed;

        if (neverc_quic_varint_decode(p, rem, &val, &consumed) != 0) return -1;
        p += consumed; rem -= consumed;

        /* RFC 9114 §7.2.4 / quic-go parseSettingsFrame: every identifier,
         * including GREASE 0x1f*N+0x21, MUST occur at most once. */
        if (h3_settings_id_duplicate(payload, (size_t)(pair - payload),
                                     id) != 0)
            return -1;

        switch (id) {
        case H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY:
            if (seen & 1U) return -1;
            seen |= 1U;
            s->qpack_max_table_capacity = val;
            break;
        case H3_SETTINGS_MAX_FIELD_SECTION_SIZE:
            if (seen & 2U) return -1;
            seen |= 2U;
            /* RFC 9114 §7.2.4.1: omitted means unlimited; an explicit 0
             * is a limit of 0, not unlimited. */
            s->max_field_section_size = val;
            break;
        case H3_SETTINGS_QPACK_BLOCKED_STREAMS:
            if (seen & 4U) return -1;
            seen |= 4U;
            s->qpack_blocked_streams = val;
            break;
        case 0x00: /* RFC 9114 §11.2.1 reserved identifier */
        case 0x02: /* HTTP/2 SETTINGS_ENABLE_PUSH */
        case 0x03: /* HTTP/2 SETTINGS_MAX_CONCURRENT_STREAMS */
        case 0x04: /* HTTP/2 SETTINGS_INITIAL_WINDOW_SIZE */
        case 0x05: /* HTTP/2 SETTINGS_MAX_FRAME_SIZE */
            /* RFC 9114 §7.2.4.1 / §11.2.1 / §11.2.2: reserved identifiers
             * MUST be treated as a connection error of H3_SETTINGS_ERROR. */
            return -1;
        default:
            /* Unknown settings MUST be ignored (RFC 9114 §7.2.4) */
            break;
        }
    }

    return 0;
}

/* ======================================================================
 * DATA Frame (RFC 9114 §7.2.1)
 * ====================================================================== */

int neverc_h3_write_data_frame(uint8_t *buf, size_t cap,
                                 const uint8_t *data, size_t data_len,
                                 size_t *written) {
    if (!buf || !written || data_len > ((UINT64_C(1) << 62) - 1))
        return -1;
    *written = 0;
    size_t pos = 0, w;
    if (neverc_quic_varint_encode(H3_FRAME_DATA, buf + pos, cap - pos, &w) != 0) return -1; pos += w;
    if (neverc_quic_varint_encode(data_len, buf + pos, cap - pos, &w) != 0) return -1; pos += w;
    if (pos > cap || data_len > cap - pos) return -1;
    if (data_len) {
        if (!data) return -1;
        memcpy(buf + pos, data, data_len);
    }
    pos += data_len;
    *written = pos;
    return 0;
}

/* ======================================================================
 * HEADERS Frame (RFC 9114 §7.2.2)
 * Payload is QPACK-encoded header block.
 * ====================================================================== */

int neverc_h3_write_headers_frame(uint8_t *buf, size_t cap,
                                    const uint8_t *encoded_headers,
                                    size_t headers_len,
                                    size_t *written) {
    if (!buf || !written || (!encoded_headers && headers_len != 0) ||
        headers_len > ((UINT64_C(1) << 62) - 1))
        return -1;
    *written = 0;
    size_t pos = 0, w;
    if (neverc_quic_varint_encode(H3_FRAME_HEADERS, buf + pos, cap - pos, &w) != 0) return -1; pos += w;
    if (neverc_quic_varint_encode(headers_len, buf + pos, cap - pos, &w) != 0) return -1; pos += w;
    if (pos > cap || headers_len > cap - pos) return -1;
    if (headers_len) memcpy(buf + pos, encoded_headers, headers_len);
    pos += headers_len;
    *written = pos;
    return 0;
}

/* ======================================================================
 * GOAWAY Frame (RFC 9114 §7.2.6)
 * ====================================================================== */

int neverc_h3_write_goaway_frame(uint8_t *buf, size_t cap,
                                   uint64_t stream_id,
                                   size_t *written) {
    if (!buf || !written || stream_id > ((UINT64_C(1) << 62) - 1))
        return -1;
    *written = 0;
    size_t pos = 0, w;
    size_t payload_len = neverc_quic_varint_len(stream_id);
    if (neverc_quic_varint_encode(H3_FRAME_GOAWAY, buf + pos, cap - pos, &w) != 0) return -1; pos += w;
    if (neverc_quic_varint_encode(payload_len, buf + pos, cap - pos, &w) != 0) return -1; pos += w;
    if (neverc_quic_varint_encode(stream_id, buf + pos, cap - pos, &w) != 0) return -1; pos += w;
    *written = pos;
    return 0;
}

static int h3_is_tchar(unsigned char character) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9'))
        return 1;
    return strchr("!#$%&'*+-.^_`|~", (int)character) != NULL;
}

/* RFC 9114 §4.3.1 / RFC 9110 §9: :method is a token, never CONNECT here. */
int neverc_h3_method_allowed(const char *method) {
    if (!method || !method[0] || strcmp(method, "CONNECT") == 0)
        return 0;
    for (const unsigned char *cursor = (const unsigned char *)method;
         *cursor; cursor++) {
        if (!h3_is_tchar(*cursor))
            return 0;
    }
    return 1;
}

/* RFC 9114 §4.3.1: :path is an absolute path, or "*" for OPTIONS.
 * Reject SP/CTL/'#' the same way HTTP/2 does so intermediaries cannot
 * desync on a space or fragment in the pseudo-header. */
int neverc_h3_request_path_allowed(const char *method, const char *path) {
    if (!method || !path || !path[0]) return 0;
    if (strcmp(path, "*") == 0)
        return strcmp(method, "OPTIONS") == 0;
    if (path[0] != '/') return 0;
    /* Same origin-form rule as HTTP/1 and HTTP/2: scheme-relative "//host"
     * and a leading backslash are open-redirect / XSS if reflected into
     * Location. RFC 9114 §4.3.1 :path is path-absolute, so "//…" is not
     * valid; '\' is not pchar. `/foo//bar` empty segments stay allowed.
     * Percent-decoded `/%2f` / `/%5c` are the same leftover. */
    {
        const char *query = strchr(path, '?');
        size_t path_len = query ? (size_t)(query - path) : strlen(path);
        if (neverc_url_path_n_is_protocol_relative(path, path_len))
            return 0;
    }
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p <= 0x20 || *p == 0x7f || *p == '#' || *p == '\\')
            return 0;
    return 1;
}

static int h3_valid_port(const char *s, size_t length) {
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

/* Same Host byte allowlist as HTTP/1 and HTTP/2 (Go ValidHostHeader
 * without comma). '<' '>' '"' used to XSS dumps and reflected Host. */
static int h3_host_reg_name_byte(unsigned char c) {
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

/* Same Host/:authority rules as HTTP/1 and HTTP/2: reject userinfo, paths,
 * commas, bad ports, HTML-special bytes, and unbracketed / unclosed IPv6 so
 * intermediaries cannot treat :authority as a Host list or override origin. */
int neverc_h3_authority_allowed(const char *value) {
    if (!value || !value[0]) return 0;
    size_t length = strlen(value);
    if (value[0] == '[') {
        const char *close = (const char *)memchr(value, ']', length);
        if (!close || close == value + 1) return 0;
        size_t inner = (size_t)(close - value - 1);
        int has_colon = 0;
        for (size_t i = 0; i < inner; i++) {
            unsigned char c = (unsigned char)value[1 + i];
            if (c == ':') has_colon = 1;
            else if (!h3_host_reg_name_byte(c))
                return 0;
        }
        if (!has_colon &&
            !(inner > 2 && (value[1] == 'v' || value[1] == 'V')))
            return 0;
        size_t after = length - (size_t)(close - value) - 1;
        if (after == 0) return 1;
        return close[1] == ':' && h3_valid_port(close + 2, after - 1);
    }

    const char *colon = (const char *)memchr(value, ':', length);
    size_t host_length = colon ? (size_t)(colon - value) : length;
    if (host_length == 0) return 0;
    for (size_t i = 0; i < host_length; i++) {
        if (!h3_host_reg_name_byte((unsigned char)value[i]))
            return 0;
    }
    if (!colon) return 1;
    if (memchr(colon + 1, ':', length - host_length - 1)) return 0;
    return h3_valid_port(colon + 1, length - host_length - 1);
}

int neverc_h3_trailer_name_allowed(const char *name) {
    if (!name || !name[0] || name[0] == ':') return 0;
    return strcmp(name, "connection") != 0 &&
           strcmp(name, "keep-alive") != 0 &&
           strcmp(name, "proxy-connection") != 0 &&
           strcmp(name, "transfer-encoding") != 0 &&
           strcmp(name, "upgrade") != 0 &&
           strcmp(name, "content-length") != 0 &&
           strcmp(name, "host") != 0 &&
           strcmp(name, "te") != 0;
}

int neverc_h3_response_body_allowed(int status) {
    return status >= 200 && status != 204 && status != 304;
}

/* RFC 9110 §8.6: Content-Length on 304 is representation metadata, not the
 * (empty) message-body length. 204 must not carry Content-Length at all.
 * Returns 0 and, when the status forbids a body, clears *present so a later
 * body-length check does not treat a valid 304 as incomplete. */
int neverc_h3_apply_response_content_length(int status, int *present) {
    if (!present) return -1;
    if (status == 204 && *present) return -1;
    if (!neverc_h3_response_body_allowed(status))
        *present = 0;
    return 0;
}

/* Single varint that must consume the entire payload (GOAWAY, CANCEL_PUSH,
 * MAX_PUSH_ID). */
int neverc_h3_parse_varint_payload(const uint8_t *payload, size_t length,
                                   uint64_t *value) {
    if (!payload || !value || length == 0 || length > 8) return -1;
    size_t consumed = 0;
    if (neverc_quic_varint_decode(payload, length, value, &consumed) != 0 ||
        consumed != length)
        return -1;
    return 0;
}

/* RFC 9114 §7.2.7: MAX_PUSH_ID MUST NOT decrease. */
int neverc_h3_max_push_id_accept(int have_previous, uint64_t previous,
                                 uint64_t next) {
    return have_previous && next < previous ? -1 : 0;
}

/* RFC 9114 §7.2.6: a later GOAWAY identifier MUST NOT increase. */
int neverc_h3_goaway_id_accept(int have_previous, uint64_t previous,
                               uint64_t next) {
    return have_previous && next > previous ? -1 : 0;
}

/* Largest client-initiated bidirectional QUIC stream ID (type bits 00). */
uint64_t neverc_h3_graceful_goaway_id(void) {
    return (UINT64_C(1) << 62) - 4U;
}

int neverc_h3_server_goaway_id_valid(uint64_t stream_id) {
    return (stream_id & 3U) == 0 &&
           stream_id <= neverc_h3_graceful_goaway_id();
}

/* RFC 9114 §6.1: HTTP/3 does not use server-initiated bidirectional
 * streams. A client MUST treat one as H3_STREAM_CREATION_ERROR.
 * Type bits 0b01 (id & 3 == 1): server-initiated, bidirectional. */
int neverc_h3_is_server_initiated_bidi(uint64_t stream_id) {
    return (stream_id & 3U) == 1U;
}

/* RFC 9114 §5.2: the indicated identifier or greater is rejected.
 * GOAWAY(0) therefore refuses the first client bidi stream. */
int neverc_h3_request_stream_after_goaway(uint64_t goaway_id,
                                          uint64_t stream_id) {
    return stream_id >= goaway_id;
}

/* First rejected request-stream ID after a graceful drain: 0 if none
 * were processed, otherwise last_processed + 4 (next client bidi). */
uint64_t neverc_h3_processed_goaway_id(int have_request, uint64_t last) {
    uint64_t max;
    if (!have_request)
        return 0;
    max = neverc_h3_graceful_goaway_id();
    if (last > max - 4U)
        return max;
    return last + 4U;
}

/* RFC 9114 §6.2: 0 = control/encoder/decoder, 1 = ignore unknown/GREASE,
 * -1 = client-initiated push (H3_STREAM_CREATION_ERROR). */
int neverc_h3_uni_stream_type_class(uint64_t type) {
    if (type == 0x00 || type == 0x02 || type == 0x03)
        return 0;
    if (type == 0x01)
        return -1;
    return 1;
}

/* RFC 9114 §7.2.4.1: omitted / UINT64_MAX means unlimited; 0 is 0. */
int neverc_h3_field_section_over_limit(uint64_t size, uint64_t limit) {
    return size > limit;
}

/* ======================================================================
 * QPACK Static Table (RFC 9204 Appendix A)
 * First 99 entries — used for encoding/decoding without dynamic table.
 * ====================================================================== */

typedef struct {
    const char *name;
    const char *value;
} qpack_static_entry_t;

static const qpack_static_entry_t QPACK_STATIC_TABLE[] = {
    /* 0 */  { ":authority", "" },
    /* 1 */  { ":path", "/" },
    /* 2 */  { "age", "0" },
    /* 3 */  { "content-disposition", "" },
    /* 4 */  { "content-length", "0" },
    /* 5 */  { "cookie", "" },
    /* 6 */  { "date", "" },
    /* 7 */  { "etag", "" },
    /* 8 */  { "if-modified-since", "" },
    /* 9 */  { "if-none-match", "" },
    /* 10 */ { "last-modified", "" },
    /* 11 */ { "link", "" },
    /* 12 */ { "location", "" },
    /* 13 */ { "referer", "" },
    /* 14 */ { "set-cookie", "" },
    /* 15 */ { ":method", "CONNECT" },
    /* 16 */ { ":method", "DELETE" },
    /* 17 */ { ":method", "GET" },
    /* 18 */ { ":method", "HEAD" },
    /* 19 */ { ":method", "OPTIONS" },
    /* 20 */ { ":method", "POST" },
    /* 21 */ { ":method", "PUT" },
    /* 22 */ { ":scheme", "http" },
    /* 23 */ { ":scheme", "https" },
    /* 24 */ { ":status", "103" },
    /* 25 */ { ":status", "200" },
    /* 26 */ { ":status", "304" },
    /* 27 */ { ":status", "404" },
    /* 28 */ { ":status", "503" },
    /* 29 */ { "accept", "*/*" },
    /* 30 */ { "accept", "application/dns-message" },
    /* 31 */ { "accept-encoding", "gzip, deflate, br" },
    /* 32 */ { "accept-ranges", "bytes" },
    /* 33 */ { "access-control-allow-headers", "cache-control" },
    /* 34 */ { "access-control-allow-headers", "content-type" },
    /* 35 */ { "access-control-allow-origin", "*" },
    /* 36 */ { "cache-control", "max-age=0" },
    /* 37 */ { "cache-control", "max-age=2592000" },
    /* 38 */ { "cache-control", "max-age=604800" },
    /* 39 */ { "cache-control", "no-cache" },
    /* 40 */ { "cache-control", "no-store" },
    /* 41 */ { "cache-control", "public, max-age=31536000" },
    /* 42 */ { "content-encoding", "br" },
    /* 43 */ { "content-encoding", "gzip" },
    /* 44 */ { "content-type", "application/dns-message" },
    /* 45 */ { "content-type", "application/javascript" },
    /* 46 */ { "content-type", "application/json" },
    /* 47 */ { "content-type", "application/x-www-form-urlencoded" },
    /* 48 */ { "content-type", "image/gif" },
    /* 49 */ { "content-type", "image/jpeg" },
    /* 50 */ { "content-type", "image/png" },
    /* 51 */ { "content-type", "text/css" },
    /* 52 */ { "content-type", "text/html; charset=utf-8" },
    /* 53 */ { "content-type", "text/plain" },
    /* 54 */ { "content-type", "text/plain;charset=utf-8" },
    /* 55 */ { "range", "bytes=0-" },
    /* 56 */ { "strict-transport-security", "max-age=31536000" },
    /* 57 */ { "strict-transport-security", "max-age=31536000; includesubdomains" },
    /* 58 */ { "strict-transport-security", "max-age=31536000; includesubdomains; preload" },
    /* 59 */ { "vary", "accept-encoding" },
    /* 60 */ { "vary", "origin" },
    /* 61 */ { "x-content-type-options", "nosniff" },
    /* 62 */ { "x-xss-protection", "1; mode=block" },
    /* 63 */ { ":status", "100" },
    /* 64 */ { ":status", "204" },
    /* 65 */ { ":status", "206" },
    /* 66 */ { ":status", "302" },
    /* 67 */ { ":status", "400" },
    /* 68 */ { ":status", "403" },
    /* 69 */ { ":status", "421" },
    /* 70 */ { ":status", "425" },
    /* 71 */ { ":status", "500" },
    /* 72 */ { "accept-language", "" },
    /* 73 */ { "access-control-allow-credentials", "FALSE" },
    /* 74 */ { "access-control-allow-credentials", "TRUE" },
    /* 75 */ { "access-control-allow-headers", "*" },
    /* 76 */ { "access-control-allow-methods", "get" },
    /* 77 */ { "access-control-allow-methods", "get, post, options" },
    /* 78 */ { "access-control-allow-methods", "options" },
    /* 79 */ { "access-control-expose-headers", "content-length" },
    /* 80 */ { "access-control-request-headers", "content-type" },
    /* 81 */ { "access-control-request-method", "get" },
    /* 82 */ { "access-control-request-method", "post" },
    /* 83 */ { "alt-svc", "clear" },
    /* 84 */ { "authorization", "" },
    /* 85 */ { "content-security-policy", "script-src 'none'; object-src 'none'; base-uri 'none'" },
    /* 86 */ { "early-data", "1" },
    /* 87 */ { "expect-ct", "" },
    /* 88 */ { "forwarded", "" },
    /* 89 */ { "if-range", "" },
    /* 90 */ { "origin", "" },
    /* 91 */ { "purpose", "prefetch" },
    /* 92 */ { "server", "" },
    /* 93 */ { "timing-allow-origin", "*" },
    /* 94 */ { "upgrade-insecure-requests", "1" },
    /* 95 */ { "user-agent", "" },
    /* 96 */ { "x-forwarded-for", "" },
    /* 97 */ { "x-frame-options", "deny" },
    /* 98 */ { "x-frame-options", "sameorigin" },
};

#define QPACK_STATIC_TABLE_SIZE 99
_Static_assert(sizeof(QPACK_STATIC_TABLE) / sizeof(QPACK_STATIC_TABLE[0]) ==
                   QPACK_STATIC_TABLE_SIZE,
               "QPACK static table must contain exactly 99 entries");

/* ======================================================================
 * QPACK Encoder — Static-only mode (no dynamic table for now)
 *
 * Uses static table references when possible, otherwise literal encoding.
 * This is sufficient for a functional HTTP/3 implementation; dynamic table
 * support can be added later for better compression ratio.
 * ====================================================================== */

struct neverc_qpack_encoder {
    uint32_t max_table_capacity;
};

struct neverc_qpack_decoder {
    uint32_t max_table_capacity;
};

neverc_qpack_encoder_t *neverc_qpack_encoder_create(uint32_t max_cap) {
    neverc_qpack_encoder_t *enc = (neverc_qpack_encoder_t *)calloc(1, sizeof(*enc));
    if (enc) enc->max_table_capacity = max_cap;
    return enc;
}

void neverc_qpack_encoder_destroy(neverc_qpack_encoder_t *enc) {
    free(enc);
}

neverc_qpack_decoder_t *neverc_qpack_decoder_create(uint32_t max_cap) {
    neverc_qpack_decoder_t *dec = (neverc_qpack_decoder_t *)calloc(1, sizeof(*dec));
    if (dec) dec->max_table_capacity = max_cap;
    return dec;
}

void neverc_qpack_decoder_destroy(neverc_qpack_decoder_t *dec) {
    free(dec);
}

static int qpack_find_static(const char *name, const char *value) {
    for (int i = 0; i < QPACK_STATIC_TABLE_SIZE; i++) {
        if (strcmp(QPACK_STATIC_TABLE[i].name, name) == 0 &&
            strcmp(QPACK_STATIC_TABLE[i].value, value) == 0)
            return i;
    }
    return -1;
}

static int qpack_find_static_name(const char *name) {
    for (int i = 0; i < QPACK_STATIC_TABLE_SIZE; i++) {
        if (strcmp(QPACK_STATIC_TABLE[i].name, name) == 0)
            return i;
    }
    return -1;
}

/*
 * QPACK field line representations (RFC 9204 §4.5):
 *   - Indexed: 1TNNNNNN (T=0 static, index)
 *   - Literal with name reference: 01NTHHHHH + value
 *   - Literal: 001NHHHHH + name + value
 *
 * For static-only mode:
 *   - Indexed static:           1 1 Index(6+)
 *   - Literal with static ref:  0 1 0 0 Index(4+) + ValueLen(7+) + Value
 *   - Literal:                   0 0 1 0 NameLen(3+) + Name + ValueLen(7+) + Value
 */

/* RFC 9110 §5.5: CR, LF, NUL, and other C0 controls except HTAB are invalid. */
static int qpack_bytes_forbidden(const uint8_t *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = s[i];
        if (c == '\t') continue;
        if (c < 0x20 || c == 0x7f) return 1;
    }
    return 0;
}

/* HTTP/3 field names are lowercase tokens, with an optional ':' for
 * pseudo-headers (RFC 9114 §4.3 / RFC 9110 §5.1). */
static int qpack_valid_field_name(const char *name) {
    if (!name || !name[0]) return 0;
    const unsigned char *cursor = (const unsigned char *)name;
    if (*cursor == ':') cursor++;
    if (!*cursor) return 0;
    for (; *cursor; cursor++) {
        unsigned char c = *cursor;
        if (c >= 'A' && c <= 'Z') return 0;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
        if (strchr("!#$%&'*+-.^_`|~", (int)c) != NULL) continue;
        return 0;
    }
    return 1;
}

static int qpack_valid_field_value(const char *value) {
    if (!value) return 0;
    return !qpack_bytes_forbidden((const uint8_t *)value, strlen(value));
}

/* RFC 9114 §4.2.2 / RFC 9113 §6.5.2: SETTINGS_MAX_FIELD_SECTION_SIZE is
 * the uncompressed list size — name + value + 32 octets per field. */
int neverc_qpack_field_section_size(const neverc_qpack_header_t *headers,
                                    int nheaders, uint64_t *size) {
    if (!size || nheaders < 0 || (nheaders > 0 && !headers))
        return -1;
    uint64_t total = 0;
    for (int i = 0; i < nheaders; i++) {
        if (!headers[i].name || !headers[i].value) return -1;
        uint64_t nlen = (uint64_t)strlen(headers[i].name);
        uint64_t vlen = (uint64_t)strlen(headers[i].value);
        if (nlen > UINT64_MAX - vlen ||
            nlen + vlen > UINT64_MAX - 32U ||
            total > UINT64_MAX - (nlen + vlen + 32U))
            return -1;
        total += nlen + vlen + 32U;
    }
    *size = total;
    return 0;
}

static int qpack_encode_integer(uint8_t *buf, size_t cap, size_t *pos,
                                  uint64_t value, uint8_t prefix_bits) {
    if (!buf || !pos || prefix_bits == 0 || prefix_bits >= 8 || *pos >= cap)
        return -1;
    uint8_t max_first = (uint8_t)((1 << prefix_bits) - 1);
    if (value < max_first) {
        buf[*pos] |= (uint8_t)value;
        (*pos)++;
        return 0;
    }
    buf[*pos] |= max_first;
    (*pos)++;
    value -= max_first;
    while (value >= 128) {
        if (*pos >= cap) return -1;
        buf[*pos] = (uint8_t)((value & 0x7F) | 0x80);
        (*pos)++;
        value >>= 7;
    }
    if (*pos >= cap) return -1;
    buf[*pos] = (uint8_t)value;
    (*pos)++;
    return 0;
}

int neverc_qpack_encode(neverc_qpack_encoder_t *enc,
                          const neverc_qpack_header_t *headers, int nheaders,
                          uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!enc || nheaders < 0 || (nheaders > 0 && !headers) ||
        !out || !out_len)
        return -1;
    *out_len = 0;
    size_t pos = 0;

    /* Required Insert Count = 0, S=0, Delta Base = 0 (static-only) */
    if (pos + 2 > out_cap) return -1;
    out[pos++] = 0x00;  /* Required Insert Count = 0 */
    out[pos++] = 0x00;  /* Delta Base = 0 */

    for (int i = 0; i < nheaders; i++) {
        const char *name = headers[i].name;
        const char *value = headers[i].value;
        if (!qpack_valid_field_name(name) || !qpack_valid_field_value(value))
            return -1;

        int idx = qpack_find_static(name, value);
        if (idx >= 0) {
            if (pos >= out_cap) return -1;
            out[pos] = 0xC0;
            if (qpack_encode_integer(out, out_cap, &pos, (uint64_t)idx, 6) != 0) return -1;
        } else {
            int name_idx = qpack_find_static_name(name);
            if (name_idx >= 0) {
                if (pos >= out_cap) return -1;
                out[pos] = 0x50;
                if (qpack_encode_integer(out, out_cap, &pos, (uint64_t)name_idx, 4) != 0) return -1;
                size_t vlen = strlen(value);
                if (pos >= out_cap) return -1;
                out[pos] = 0x00;
                if (qpack_encode_integer(out, out_cap, &pos, vlen, 7) != 0) return -1;
                if (vlen > out_cap - pos) return -1;
                memcpy(out + pos, value, vlen);
                pos += vlen;
            } else {
                if (pos >= out_cap) return -1;
                out[pos] = 0x20;
                size_t nlen = strlen(name);
                if (qpack_encode_integer(out, out_cap, &pos, nlen, 3) != 0) return -1;
                if (nlen > out_cap - pos) return -1;
                memcpy(out + pos, name, nlen);
                pos += nlen;
                size_t vlen = strlen(value);
                if (pos >= out_cap) return -1;
                out[pos] = 0x00;
                if (qpack_encode_integer(out, out_cap, &pos, vlen, 7) != 0) return -1;
                if (vlen > out_cap - pos) return -1;
                memcpy(out + pos, value, vlen);
                pos += vlen;
            }
        }
    }

    *out_len = pos;
    return 0;
}

/* ======================================================================
 * QPACK Decoder — Static-only mode
 *
 * Decodes header blocks produced by the encoder above.
 * Supports: indexed static, literal with name ref, literal.
 * ====================================================================== */

static int qpack_decode_integer(const uint8_t *buf, size_t len, size_t *pos,
                                  uint8_t prefix_bits, uint64_t *value) {
    if (!buf || !pos || !value || prefix_bits == 0 || prefix_bits > 8 ||
        *pos >= len)
        return -1;
    uint8_t max_first = (uint8_t)((1U << prefix_bits) - 1U);
    *value = buf[*pos] & max_first;
    (*pos)++;

    if (*value < max_first) return 0;

    uint64_t m = 0;
    uint8_t b;
    do {
        if (*pos >= len) return -1;
        b = buf[*pos];
        (*pos)++;
        if (m > 62) return -1;
        uint64_t add = (uint64_t)(b & 0x7F) << m;
        if (*value > UINT64_MAX - add) return -1;
        *value += add;
        m += 7;
    } while (b & 0x80);

    return 0;
}

static char *qpack_decode_string(const uint8_t *buf, size_t len, size_t *pos,
                                   uint8_t prefix_bits) {
    if (!buf || !pos || *pos >= len || prefix_bits == 0 ||
        prefix_bits >= 8)
        return NULL;
    int huffman = (buf[*pos] & (1U << prefix_bits)) != 0;
    uint64_t slen;
    if (qpack_decode_integer(buf, len, pos, prefix_bits, &slen) != 0)
        return NULL;
    if (slen >= SIZE_MAX || (size_t)slen > len - *pos) return NULL;
    if (!huffman) {
        char *s = (char *)malloc((size_t)slen + 1U);
        if (!s) return NULL;
        memcpy(s, buf + *pos, (size_t)slen);
        if (qpack_bytes_forbidden((const uint8_t *)s, (size_t)slen)) {
            free(s);
            return NULL;
        }
        s[slen] = '\0';
        *pos += (size_t)slen;
        return s;
    }
    if (slen > (SIZE_MAX - 1U) / 2U) return NULL;
    size_t capacity = (size_t)slen * 2U + 1U;
    char *s = (char *)malloc(capacity);
    if (!s) return NULL;
    size_t decoded = 0;
    if (neverc_hpack_huffman_decode(buf + *pos, (size_t)slen,
                                     (uint8_t *)s, capacity - 1U,
                                     &decoded) != 0 ||
        qpack_bytes_forbidden((const uint8_t *)s, decoded)) {
        free(s);
        return NULL;
    }
    s[decoded] = '\0';
    *pos += (size_t)slen;
    return s;
}

static void qpack_free_decoded(neverc_qpack_header_t *headers, int count) {
    for (int i = 0; i < count; i++) {
        free(headers[i].name);
        free(headers[i].value);
        headers[i].name = NULL;
        headers[i].value = NULL;
    }
}

int neverc_qpack_decode(neverc_qpack_decoder_t *dec,
                          const uint8_t *data, size_t len,
                          neverc_qpack_header_t *headers, int max_headers,
                          int *nheaders) {
    (void)dec;
    if (!dec || !data || len < 2 || !headers || !nheaders ||
        max_headers <= 0)
        return -1;
    *nheaders = 0;
    memset(headers, 0, (size_t)max_headers * sizeof(*headers));
    size_t pos = 0;
    int count = 0;

    /* Skip Required Insert Count (varint prefix 8) */
    uint64_t ric;
    if (qpack_decode_integer(data, len, &pos, 8, &ric) != 0 || ric != 0)
        return -1;

    /* Skip Delta Base (varint prefix 7, with S bit). RFC 9204 §4.5.1.2:
     * S=1 with RIC=0 yields Base = -1 and MUST fail. */
    if (pos >= len) return -1;
    if ((data[pos] & 0x80U) != 0)
        return -1;
    uint64_t delta_base;
    if (qpack_decode_integer(data, len, &pos, 7, &delta_base) != 0 ||
        delta_base != 0)
        return -1;

    while (pos < len && count < max_headers) {
        uint8_t first = data[pos];

        if ((first & 0xC0) == 0xC0) {
            /* Indexed field line (static): 11NNNNNN */
            uint64_t idx;
            if (qpack_decode_integer(data, len, &pos, 6, &idx) != 0 ||
                idx >= QPACK_STATIC_TABLE_SIZE)
                goto failed;
            headers[count].name = strdup(QPACK_STATIC_TABLE[idx].name);
            headers[count].value = strdup(QPACK_STATIC_TABLE[idx].value);
            if (!headers[count].name || !headers[count].value) goto failed;
            count++;
        } else if ((first & 0xD0) == 0x50) {
            /* Literal with name reference (static): 01N1NNNN.
             * N=1 ("never index") is legal and must still decode. */
            uint64_t name_idx;
            if (qpack_decode_integer(data, len, &pos, 4, &name_idx) != 0 ||
                name_idx >= QPACK_STATIC_TABLE_SIZE)
                goto failed;
            headers[count].name = strdup(QPACK_STATIC_TABLE[name_idx].name);
            /* Value: H(1) + length(7) + bytes */
            if (!headers[count].name || pos >= len) goto failed;
            headers[count].value = qpack_decode_string(data, len, &pos, 7);
            if (!headers[count].value) goto failed;
            count++;
        } else if ((first & 0xE0) == 0x20) {
            /* Literal (both name and value): 001NNNNN */
            /* Name: H(1) + length(3) + bytes (actually 3-bit prefix for length) */
            if (pos >= len) goto failed;
            headers[count].name = qpack_decode_string(data, len, &pos, 3);
            if (!headers[count].name ||
                !qpack_valid_field_name(headers[count].name))
                goto failed;
            /* Value: H(1) + length(7) + bytes */
            if (pos >= len) goto failed;
            headers[count].value = qpack_decode_string(data, len, &pos, 7);
            if (!headers[count].value) goto failed;
            count++;
        } else {
            /* Unknown representation — skip or error */
            goto failed;
        }
    }

    if (pos != len) {
        /* Remaining bytes after filling max_headers is an implementation
         * limit, not a damaged field section (RFC 9204 decompression). */
        if (count == max_headers) {
            qpack_free_decoded(headers, count);
            *nheaders = 0;
            return -2;
        }
        goto failed;
    }

    *nheaders = count;
    return 0;

failed:
    if (count < max_headers) {
        free(headers[count].name);
        free(headers[count].value);
        headers[count].name = NULL;
        headers[count].value = NULL;
    }
    qpack_free_decoded(headers, count);
    *nheaders = 0;
    return -1;
}

neverc_qpack_encoder_t *neverc_http3_qpack_encoder_create(
    uint32_t max_table_cap) {
    return neverc_qpack_encoder_create(max_table_cap);
}

void neverc_http3_qpack_encoder_destroy(
    neverc_qpack_encoder_t *encoder) {
    neverc_qpack_encoder_destroy(encoder);
}

int neverc_http3_qpack_encode(
    neverc_qpack_encoder_t *encoder,
    const neverc_qpack_header_t *headers, int header_count,
    uint8_t *output, size_t output_capacity, size_t *output_length) {
    return neverc_qpack_encode(encoder, headers, header_count, output,
                               output_capacity, output_length);
}

neverc_qpack_decoder_t *neverc_http3_qpack_decoder_create(
    uint32_t max_table_cap) {
    return neverc_qpack_decoder_create(max_table_cap);
}

void neverc_http3_qpack_decoder_destroy(
    neverc_qpack_decoder_t *decoder) {
    neverc_qpack_decoder_destroy(decoder);
}

int neverc_http3_qpack_decode(
    neverc_qpack_decoder_t *decoder,
    const uint8_t *data, size_t length,
    neverc_qpack_header_t *headers, int max_headers, int *header_count) {
    return neverc_qpack_decode(decoder, data, length, headers, max_headers,
                               header_count);
}

int neverc_http3_qpack_field_section_size(
    const neverc_qpack_header_t *headers, int nheaders, uint64_t *size) {
    return neverc_qpack_field_section_size(headers, nheaders, size);
}

/* RFC 9204 §4.4 decoder-stream instructions. Returns 1 if one complete
 * valid instruction was consumed, 0 if more bytes are needed, -1 if the
 * instruction is invalid for a static-only encoder (RIC always 0). */
static int qpack_prefix_int(const uint8_t *buf, size_t len,
                            uint8_t prefix_bits, uint64_t *value,
                            size_t *consumed) {
    if (!buf || !value || !consumed || len == 0 || prefix_bits == 0 ||
        prefix_bits > 8)
        return 0;
    uint8_t max_first = (uint8_t)((1U << prefix_bits) - 1U);
    *value = (uint64_t)(buf[0] & max_first);
    size_t pos = 1;
    if (*value < max_first) {
        *consumed = 1;
        return 1;
    }
    uint64_t shift = 0;
    while (pos < len) {
        uint8_t b = buf[pos++];
        if (shift > 62) return -1;
        uint64_t add = (uint64_t)(b & 0x7F) << shift;
        if (*value > UINT64_MAX - add) return -1;
        *value += add;
        shift += 7;
        if ((b & 0x80) == 0) {
            *consumed = pos;
            return 1;
        }
    }
    return 0;
}

int neverc_qpack_decoder_stream_instruction(const uint8_t *data, size_t len,
                                            size_t *consumed) {
    if (!consumed) return -1;
    *consumed = 0;
    if (!data || len == 0) return 0;
    uint64_t value = 0;
    size_t n = 0;
    int rc;
    if (data[0] & 0x80U) {
        /* Section Acknowledgement — forbidden when RIC is always 0. */
        rc = qpack_prefix_int(data, len, 7, &value, &n);
        return rc <= 0 ? rc : -1;
    }
    if (data[0] & 0x40U) {
        /* Stream Cancellation is always legal. */
        rc = qpack_prefix_int(data, len, 6, &value, &n);
        if (rc <= 0) return rc;
        *consumed = n;
        return 1;
    }
    /* Insert Count Increment — any increment is invalid with zero inserts. */
    rc = qpack_prefix_int(data, len, 6, &value, &n);
    return rc <= 0 ? rc : -1;
}

/* RFC 9204 §4.3 encoder-stream instructions. Capacity 0 allows only
 * Set Dynamic Table Capacity(0) (typically the single byte 0x20). */
int neverc_qpack_encoder_stream_instruction(const uint8_t *data, size_t len,
                                            size_t *consumed) {
    if (!consumed) return -1;
    *consumed = 0;
    if (!data || len == 0) return 0;
    uint64_t value = 0;
    size_t n = 0;
    int rc;
    if (data[0] & 0x80U) {
        /* Insert With Name Reference. */
        rc = qpack_prefix_int(data, len, 6, &value, &n);
        return rc <= 0 ? rc : -1;
    }
    if ((data[0] & 0xC0U) == 0x40U) {
        /* Insert With Literal Name. The first byte is enough. */
        return -1;
    }
    if ((data[0] & 0xE0U) == 0x20U) {
        rc = qpack_prefix_int(data, len, 5, &value, &n);
        if (rc <= 0) return rc;
        if (value != 0) return -1;
        *consumed = n;
        return 1;
    }
    /* Duplicate. */
    rc = qpack_prefix_int(data, len, 5, &value, &n);
    return rc <= 0 ? rc : -1;
}
