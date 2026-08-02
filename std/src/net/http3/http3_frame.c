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
    p += consumed;

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
    if (s->max_field_section_size < UINT64_MAX) {
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
        uint64_t id, val;
        size_t consumed;

        if (neverc_quic_varint_decode(p, rem, &id, &consumed) != 0) return -1;
        p += consumed; rem -= consumed;

        if (neverc_quic_varint_decode(p, rem, &val, &consumed) != 0) return -1;
        p += consumed; rem -= consumed;

        switch (id) {
        case H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY:
            if (seen & 1U) return -1;
            seen |= 1U;
            s->qpack_max_table_capacity = val;
            break;
        case H3_SETTINGS_MAX_FIELD_SECTION_SIZE:
            if (seen & 2U) return -1;
            seen |= 2U;
            s->max_field_section_size = val;
            break;
        case H3_SETTINGS_QPACK_BLOCKED_STREAMS:
            if (seen & 4U) return -1;
            seen |= 4U;
            s->qpack_blocked_streams = val;
            break;
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
    if (pos + data_len > cap) return -1;
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
    if (pos + headers_len > cap) return -1;
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
        if (!name || !value) return -1;

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
    uint8_t max_first = (uint8_t)((1 << prefix_bits) - 1);
    *value = buf[*pos] & max_first;
    (*pos)++;

    if (*value < max_first) return 0;

    uint64_t m = 0;
    uint8_t b;
    do {
        if (*pos >= len) return -1;
        b = buf[*pos];
        (*pos)++;
        uint64_t add = (uint64_t)(b & 0x7F) << m;
        if (*value > UINT64_MAX - add) return -1;
        *value += add;
        m += 7;
        if (m > 62) return -1;
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
                                     &decoded) != 0) {
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
    memset(headers, 0, (size_t)max_headers * sizeof(*headers));
    size_t pos = 0;
    int count = 0;

    /* Skip Required Insert Count (varint prefix 8) */
    uint64_t ric;
    if (qpack_decode_integer(data, len, &pos, 8, &ric) != 0 || ric != 0)
        return -1;

    /* Skip Delta Base (varint prefix 7, with S bit) */
    if (pos >= len) return -1;
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
        } else if ((first & 0xF0) == 0x50) {
            /* Literal with name reference (static, N=0): 0101NNNN */
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
            if (!headers[count].name) goto failed;
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

    if (pos != len) goto failed;

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
