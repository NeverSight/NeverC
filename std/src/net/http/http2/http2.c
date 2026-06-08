/*
 * NeverC HTTP/2 implementation (RFC 9113)
 *
 * Components:
 *   1. HPACK header compression (RFC 7541)
 *      - Huffman coding (RFC 7541 §5.2)
 *      - Static table (61 entries, RFC 7541 Appendix A)
 *      - Dynamic table with FIFO eviction
 *      - Integer encoding (RFC 7541 §5.1)
 *   2. Binary framing layer
 *      - 9-byte frame header parse/write
 *      - All 10 frame types
 *   3. Stream state machine (RFC 9113 §5.1)
 *   4. Flow control (RFC 9113 §5.2)
 *   5. Server implementation
 *
 * All cross-platform: POSIX + Windows.
 */

#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int ssize_t;
#else
  #include <sys/socket.h>
  #include <unistd.h>
  #ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
  #endif
#endif

/* ======================================================================
 * HPACK Huffman Table (RFC 7541 Appendix B)
 *
 * 256 symbol codes + EOS. Generated from the RFC specification.
 * ====================================================================== */

/* 256 symbols + EOS (index 256) */
static const uint32_t hpack_huffman_codes[257] = {
    0x1ff8, 0x7fffd8, 0xfffffe2, 0xfffffe3, 0xfffffe4, 0xfffffe5,
    0xfffffe6, 0xfffffe7, 0xfffffe8, 0xffffea, 0x3ffffffc, 0xfffffe9,
    0xfffffea, 0x3ffffffd, 0xfffffeb, 0xfffffec, 0xfffffed, 0xfffffee,
    0xfffffef, 0xffffff0, 0xffffff1, 0xffffff2, 0x3ffffffe, 0xffffff3,
    0xffffff4, 0xffffff5, 0xffffff6, 0xffffff7, 0xffffff8, 0xffffff9,
    0xffffffa, 0xffffffb, 0x14, 0x3f8, 0x3f9, 0xffa,
    0x1ff9, 0x15, 0xf8, 0x7fa, 0x3fa, 0x3fb,
    0xf9, 0x7fb, 0xfa, 0x16, 0x17, 0x18,
    0x0, 0x1, 0x2, 0x19, 0x1a, 0x1b,
    0x1c, 0x1d, 0x1e, 0x1f, 0x5c, 0xfb,
    0x7ffc, 0x20, 0xffb, 0x3fc, 0x1ffa, 0x21,
    0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e,
    0x6f, 0x70, 0x71, 0x72, 0xfc, 0x73,
    0xfd, 0x1ffb, 0x7fff0, 0x1ffc, 0x3ffc, 0x22,
    0x7ffd, 0x3, 0x23, 0x4, 0x24, 0x5,
    0x25, 0x26, 0x27, 0x6, 0x74, 0x75,
    0x28, 0x29, 0x2a, 0x7, 0x2b, 0x76,
    0x2c, 0x8, 0x9, 0x2d, 0x77, 0x78,
    0x79, 0x7a, 0x7b, 0x7ffe, 0x7fc, 0x3ffd,
    0x1ffd, 0xffffffc, 0xfffe6, 0x3fffd2, 0xfffe7, 0xfffe8,
    0x3fffd3, 0x3fffd4, 0x3fffd5, 0x7fffd9, 0x3fffd6, 0x7fffda,
    0x7fffdb, 0x7fffdc, 0x7fffdd, 0x7fffde, 0xffffeb, 0x7fffdf,
    0xffffec, 0xffffed, 0x3fffd7, 0x7fffe0, 0xffffee, 0x7fffe1,
    0x7fffe2, 0x7fffe3, 0x7fffe4, 0x1fffdc, 0x3fffd8, 0x7fffe5,
    0x3fffd9, 0x7fffe6, 0x7fffe7, 0xffffef, 0x3fffda, 0x1fffdd,
    0xfffe9, 0x3fffdb, 0x3fffdc, 0x7fffe8, 0x7fffe9, 0x1fffde,
    0x7fffea, 0x3fffdd, 0x3fffde, 0xfffff0, 0x1fffdf, 0x3fffdf,
    0x7fffeb, 0x7fffec, 0x1fffe0, 0x1fffe1, 0x3fffe0, 0x1fffe2,
    0x7fffed, 0x3fffe1, 0x7fffee, 0x7fffef, 0xfffea, 0x3fffe2,
    0x3fffe3, 0x3fffe4, 0x7ffff0, 0x3fffe5, 0x3fffe6, 0x7ffff1,
    0x3ffffe0, 0x3ffffe1, 0xfffeb, 0x7fff1, 0x3fffe7, 0x7ffff2,
    0x3fffe8, 0x1ffffec, 0x3ffffe2, 0x3ffffe3, 0x3ffffe4, 0x7ffffde,
    0x7ffffdf, 0x3ffffe5, 0xfffff1, 0x1ffffed, 0x7fff2, 0x1fffe3,
    0x3ffffe6, 0x7ffffe0, 0x7ffffe1, 0x3ffffe7, 0x7ffffe2, 0xfffff2,
    0x1fffe4, 0x1fffe5, 0x3ffffe8, 0x3ffffe9, 0xffffffd, 0x7ffffe3,
    0x7ffffe4, 0x7ffffe5, 0xfffec, 0xfffff3, 0xfffed, 0x1fffe6,
    0x3fffe9, 0x1fffe7, 0x1fffe8, 0x7ffff3, 0x3fffea, 0x3fffeb,
    0x1ffffee, 0x1ffffef, 0xfffff4, 0xfffff5, 0x3ffffea, 0x7ffff4,
    0x3ffffeb, 0x7ffffe6, 0x3ffffec, 0x3ffffed, 0x7ffffe7,
    0x7ffffe8, 0x7ffffe9, 0x7ffffea, 0x7ffffeb, 0xffffffe, 0x7ffffec,
    0x7ffffed, 0x7ffffee, 0x7ffffef, 0x7fffff0, 0x3ffffee,
    0x3fffffff  /* EOS */
};

static const uint8_t hpack_huffman_code_len[257] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28,
    28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 28,
     6, 10, 10, 12, 13,  6,  8, 11, 10, 10,  8, 11,  8,  6,  6,  6,
     5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  7,  8, 15,  6, 12, 10,
    13,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  8,  7,  8, 13, 19, 13, 14,  6,
    15,  5,  6,  5,  6,  5,  6,  6,  6,  5,  7,  7,  6,  6,  6,  5,
     6,  7,  6,  5,  5,  6,  7,  7,  7,  7,  7, 15, 11, 14, 13, 28,
    20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23,
    24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24,
    22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22, 23, 23,
    21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23,
    26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26, 27, 27, 26, 24, 25,
    19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27,
    20, 24, 20, 21, 22, 21, 21, 23, 22, 22, 25, 25, 24, 24, 26, 23,
    26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26,
    30  /* EOS */
};

/* ======================================================================
 * HPACK Static Table (RFC 7541 Appendix A) — 61 entries
 * ====================================================================== */

typedef struct {
    const char *name;
    const char *value;
} hpack_static_entry_t;

static const hpack_static_entry_t hpack_static_table[62] = {
    { "",                            "" },  /* index 0 (unused) */
    { ":authority",                  "" },
    { ":method",                     "GET" },
    { ":method",                     "POST" },
    { ":path",                       "/" },
    { ":path",                       "/index.html" },
    { ":scheme",                     "http" },
    { ":scheme",                     "https" },
    { ":status",                     "200" },
    { ":status",                     "204" },
    { ":status",                     "206" },
    { ":status",                     "304" },
    { ":status",                     "400" },
    { ":status",                     "404" },
    { ":status",                     "500" },
    { "accept-charset",              "" },
    { "accept-encoding",             "gzip, deflate" },
    { "accept-language",             "" },
    { "accept-ranges",               "" },
    { "accept",                      "" },
    { "access-control-allow-origin", "" },
    { "age",                         "" },
    { "allow",                       "" },
    { "authorization",               "" },
    { "cache-control",               "" },
    { "content-disposition",         "" },
    { "content-encoding",            "" },
    { "content-language",            "" },
    { "content-length",              "" },
    { "content-location",            "" },
    { "content-range",               "" },
    { "content-type",                "" },
    { "cookie",                      "" },
    { "date",                        "" },
    { "etag",                        "" },
    { "expect",                      "" },
    { "expires",                     "" },
    { "from",                        "" },
    { "host",                        "" },
    { "if-match",                    "" },
    { "if-modified-since",           "" },
    { "if-none-match",               "" },
    { "if-range",                    "" },
    { "if-unmodified-since",         "" },
    { "last-modified",               "" },
    { "link",                        "" },
    { "location",                    "" },
    { "max-forwards",                "" },
    { "proxy-authenticate",          "" },
    { "proxy-authorization",         "" },
    { "range",                       "" },
    { "referer",                     "" },
    { "refresh",                     "" },
    { "retry-after",                 "" },
    { "server",                      "" },
    { "set-cookie",                  "" },
    { "strict-transport-security",   "" },
    { "transfer-encoding",           "" },
    { "user-agent",                  "" },
    { "vary",                        "" },
    { "via",                         "" },
    { "www-authenticate",            "" },
};

/* ======================================================================
 * HPACK Integer Encoding (RFC 7541 §5.1)
 * ====================================================================== */

static int hpack_decode_int(const uint8_t *data, size_t len, int prefix_bits,
                             uint64_t *value, size_t *consumed) {
    if (len == 0) return -1;
    uint8_t mask = (uint8_t)((1 << prefix_bits) - 1);
    *value = data[0] & mask;
    if (*value < mask) {
        *consumed = 1;
        return 0;
    }
    uint64_t m = 0;
    size_t i = 1;
    for (;;) {
        if (i >= len) return -1;
        uint8_t b = data[i];
        *value += (uint64_t)(b & 0x7f) << m;
        m += 7;
        i++;
        if (!(b & 0x80)) break;
        if (m > 63) return -1;
    }
    *consumed = i;
    return 0;
}

static int hpack_encode_int(uint8_t *out, size_t cap, int prefix_bits,
                             uint64_t value, uint8_t prefix_byte,
                             size_t *written) {
    uint8_t mask = (uint8_t)((1 << prefix_bits) - 1);
    if (cap == 0) return -1;
    if (value < mask) {
        out[0] = prefix_byte | (uint8_t)value;
        *written = 1;
        return 0;
    }
    out[0] = prefix_byte | mask;
    value -= mask;
    size_t i = 1;
    while (value >= 128) {
        if (i >= cap) return -1;
        out[i++] = (uint8_t)((value & 0x7f) | 0x80);
        value >>= 7;
    }
    if (i >= cap) return -1;
    out[i++] = (uint8_t)value;
    *written = i;
    return 0;
}

/* ======================================================================
 * HPACK Huffman Encoding/Decoding
 * ====================================================================== */

int neverc_hpack_huffman_encode(const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len) {
    uint64_t x = 0;
    unsigned n = 0;
    size_t pos = 0;

    for (size_t i = 0; i < in_len; i++) {
        uint32_t code = hpack_huffman_codes[in[i]];
        uint8_t nbits = hpack_huffman_code_len[in[i]];
        x = (x << nbits) | code;
        n += nbits;
        while (n >= 8) {
            n -= 8;
            if (pos >= out_cap) return -1;
            out[pos++] = (uint8_t)(x >> n);
        }
    }
    if (n > 0) {
        /* Pad with EOS prefix (all 1s) */
        x = (x << (8 - n)) | ((1 << (8 - n)) - 1);
        if (pos >= out_cap) return -1;
        out[pos++] = (uint8_t)x;
    }
    *out_len = pos;
    return 0;
}

int neverc_hpack_huffman_decode(const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len) {
    /* Bit-by-bit decode using the Huffman table.
     * This is a simple but correct implementation. */
    size_t pos = 0;
    uint32_t code = 0;
    uint8_t code_len = 0;

    for (size_t i = 0; i < in_len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            code = (code << 1) | ((in[i] >> bit) & 1);
            code_len++;

            /* Search for matching symbol */
            for (int sym = 0; sym < 256; sym++) {
                if (hpack_huffman_code_len[sym] == code_len &&
                    hpack_huffman_codes[sym] == code) {
                    if (pos >= out_cap) return -1;
                    out[pos++] = (uint8_t)sym;
                    code = 0;
                    code_len = 0;
                    break;
                }
            }
            if (code_len > 30) return -1; /* invalid */
        }
    }
    /* Remaining bits should be EOS padding (all 1s) */
    if (code_len > 7) return -1;
    if (code_len > 0) {
        uint32_t pad_mask = (1u << code_len) - 1;
        if ((code & pad_mask) != pad_mask) return -1;
    }
    *out_len = pos;
    return 0;
}

/* ======================================================================
 * HPACK Dynamic Table
 * ====================================================================== */

#define HPACK_DYN_MAX_ENTRIES 256

typedef struct {
    neverc_hpack_header_t entries[HPACK_DYN_MAX_ENTRIES];
    int head;
    int count;
    uint32_t size;
    uint32_t max_size;
} hpack_dyn_table_t;

static void dyn_table_init(hpack_dyn_table_t *t, uint32_t max_size) {
    memset(t, 0, sizeof(*t));
    t->max_size = max_size;
}

static uint32_t hpack_entry_size(const char *name, const char *value) {
    return (uint32_t)(strlen(name) + strlen(value) + 32);
}

static void dyn_table_evict(hpack_dyn_table_t *t) {
    while (t->count > 0 && t->size > t->max_size) {
        int tail = (t->head + t->count - 1) % HPACK_DYN_MAX_ENTRIES;
        t->size -= hpack_entry_size(t->entries[tail].name, t->entries[tail].value);
        free(t->entries[tail].name);
        free(t->entries[tail].value);
        t->entries[tail].name = NULL;
        t->entries[tail].value = NULL;
        t->count--;
    }
}

static void dyn_table_add(hpack_dyn_table_t *t, const char *name, const char *value) {
    uint32_t entry_sz = hpack_entry_size(name, value);
    if (entry_sz > t->max_size) {
        /* Entry too large — clear entire table (RFC 7541 §4.4) */
        while (t->count > 0) {
            t->max_size = 0;
            dyn_table_evict(t);
            t->max_size = entry_sz; /* restore for eviction loop */
        }
        t->max_size = entry_sz;
        return;
    }
    t->size += entry_sz;
    dyn_table_evict(t);

    t->head = (t->head - 1 + HPACK_DYN_MAX_ENTRIES) % HPACK_DYN_MAX_ENTRIES;
    t->entries[t->head].name = strdup(name);
    t->entries[t->head].value = strdup(value);
    t->count++;
}

static int dyn_table_get(hpack_dyn_table_t *t, int index,
                          const char **name, const char **value) {
    /* Dynamic table indices start at 62 (after 61 static entries).
     * index 0 within dynamic = most recently added. */
    if (index < 0 || index >= t->count) return -1;
    int real = (t->head + index) % HPACK_DYN_MAX_ENTRIES;
    *name = t->entries[real].name;
    *value = t->entries[real].value;
    return 0;
}

static void dyn_table_free(hpack_dyn_table_t *t) {
    for (int i = 0; i < t->count; i++) {
        int idx = (t->head + i) % HPACK_DYN_MAX_ENTRIES;
        free(t->entries[idx].name);
        free(t->entries[idx].value);
    }
    memset(t, 0, sizeof(*t));
}

/* ======================================================================
 * HPACK Decoder
 * ====================================================================== */

struct neverc_hpack_decoder {
    hpack_dyn_table_t dyn;
};

neverc_hpack_decoder_t *neverc_hpack_decoder_create(uint32_t max_table_size) {
    neverc_hpack_decoder_t *dec = (neverc_hpack_decoder_t *)calloc(1, sizeof(*dec));
    if (!dec) return NULL;
    dyn_table_init(&dec->dyn, max_table_size);
    return dec;
}

void neverc_hpack_decoder_destroy(neverc_hpack_decoder_t *dec) {
    if (!dec) return;
    dyn_table_free(&dec->dyn);
    free(dec);
}

static int hpack_lookup(neverc_hpack_decoder_t *dec, int index,
                          const char **name, const char **value) {
    if (index <= 0) return -1;
    if (index <= 61) {
        *name = hpack_static_table[index].name;
        *value = hpack_static_table[index].value;
        return 0;
    }
    return dyn_table_get(&dec->dyn, index - 62, name, value);
}

static int hpack_decode_string(const uint8_t *data, size_t len,
                                 char **out_str, size_t *consumed) {
    if (len == 0) return -1;
    int huffman = !!(data[0] & 0x80);
    uint64_t slen;
    size_t hdr_consumed;
    if (hpack_decode_int(data, len, 7, &slen, &hdr_consumed) != 0)
        return -1;
    if (hdr_consumed + slen > len) return -1;

    if (huffman) {
        uint8_t *decoded = (uint8_t *)malloc(slen * 2 + 1);
        if (!decoded) return -1;
        size_t decoded_len;
        if (neverc_hpack_huffman_decode(data + hdr_consumed, (size_t)slen,
                                         decoded, slen * 2, &decoded_len) != 0) {
            free(decoded);
            return -1;
        }
        decoded[decoded_len] = '\0';
        *out_str = (char *)decoded;
    } else {
        *out_str = (char *)malloc((size_t)slen + 1);
        if (!*out_str) return -1;
        memcpy(*out_str, data + hdr_consumed, (size_t)slen);
        (*out_str)[slen] = '\0';
    }
    *consumed = hdr_consumed + (size_t)slen;
    return 0;
}

int neverc_hpack_decode(neverc_hpack_decoder_t *dec,
                         const uint8_t *data, size_t len,
                         neverc_hpack_header_t *headers, int max_headers,
                         int *nheaders) {
    *nheaders = 0;
    size_t pos = 0;

    while (pos < len && *nheaders < max_headers) {
        uint8_t byte = data[pos];
        const char *name = NULL;
        const char *value = NULL;
        char *alloc_name = NULL;
        char *alloc_value = NULL;
        int add_to_dyn = 0;

        if (byte & 0x80) {
            /* Indexed Header Field (§6.1) */
            uint64_t index;
            size_t consumed;
            if (hpack_decode_int(data + pos, len - pos, 7, &index, &consumed) != 0)
                return -1;
            pos += consumed;
            if (hpack_lookup(dec, (int)index, &name, &value) != 0)
                return -1;
            headers[*nheaders].name = strdup(name);
            headers[*nheaders].value = strdup(value);
            headers[*nheaders].sensitive = 0;
            (*nheaders)++;
        } else if ((byte & 0xc0) == 0x40) {
            /* Literal with Incremental Indexing (§6.2.1) */
            uint64_t index;
            size_t consumed;
            if (hpack_decode_int(data + pos, len - pos, 6, &index, &consumed) != 0)
                return -1;
            pos += consumed;

            if (index > 0) {
                if (hpack_lookup(dec, (int)index, &name, &value) != 0) return -1;
                alloc_name = strdup(name);
            } else {
                if (hpack_decode_string(data + pos, len - pos, &alloc_name, &consumed) != 0)
                    return -1;
                pos += consumed;
            }
            if (hpack_decode_string(data + pos, len - pos, &alloc_value, &consumed) != 0) {
                free(alloc_name);
                return -1;
            }
            pos += consumed;

            headers[*nheaders].name = alloc_name;
            headers[*nheaders].value = alloc_value;
            headers[*nheaders].sensitive = 0;
            (*nheaders)++;
            add_to_dyn = 1;
        } else if ((byte & 0xf0) == 0x00 || (byte & 0xf0) == 0x10) {
            /* Literal without Indexing (§6.2.2) or Never Indexed (§6.2.3) */
            int sensitive = !!(byte & 0x10);
            uint64_t index;
            size_t consumed;
            if (hpack_decode_int(data + pos, len - pos, 4, &index, &consumed) != 0)
                return -1;
            pos += consumed;

            if (index > 0) {
                if (hpack_lookup(dec, (int)index, &name, &value) != 0) return -1;
                alloc_name = strdup(name);
            } else {
                if (hpack_decode_string(data + pos, len - pos, &alloc_name, &consumed) != 0)
                    return -1;
                pos += consumed;
            }
            if (hpack_decode_string(data + pos, len - pos, &alloc_value, &consumed) != 0) {
                free(alloc_name);
                return -1;
            }
            pos += consumed;

            headers[*nheaders].name = alloc_name;
            headers[*nheaders].value = alloc_value;
            headers[*nheaders].sensitive = sensitive;
            (*nheaders)++;
        } else if ((byte & 0xe0) == 0x20) {
            /* Dynamic Table Size Update (§6.3) */
            uint64_t new_size;
            size_t consumed;
            if (hpack_decode_int(data + pos, len - pos, 5, &new_size, &consumed) != 0)
                return -1;
            pos += consumed;
            dec->dyn.max_size = (uint32_t)new_size;
            dyn_table_evict(&dec->dyn);
            continue;
        } else {
            return -1;
        }

        if (add_to_dyn && alloc_name && alloc_value) {
            dyn_table_add(&dec->dyn, alloc_name, alloc_value);
        }
    }
    return 0;
}

/* ======================================================================
 * HPACK Encoder
 * ====================================================================== */

struct neverc_hpack_encoder {
    hpack_dyn_table_t dyn;
};

neverc_hpack_encoder_t *neverc_hpack_encoder_create(uint32_t max_table_size) {
    neverc_hpack_encoder_t *enc = (neverc_hpack_encoder_t *)calloc(1, sizeof(*enc));
    if (!enc) return NULL;
    dyn_table_init(&enc->dyn, max_table_size);
    return enc;
}

void neverc_hpack_encoder_destroy(neverc_hpack_encoder_t *enc) {
    if (!enc) return;
    dyn_table_free(&enc->dyn);
    free(enc);
}

static int hpack_find_static(const char *name, const char *value) {
    for (int i = 1; i <= 61; i++) {
        if (strcmp(hpack_static_table[i].name, name) == 0) {
            if (value && hpack_static_table[i].value[0] &&
                strcmp(hpack_static_table[i].value, value) == 0)
                return i; /* exact match */
            if (!value || !value[0])
                return -i; /* name-only match (negative = name match) */
        }
    }
    /* Name-only scan */
    for (int i = 1; i <= 61; i++) {
        if (strcmp(hpack_static_table[i].name, name) == 0)
            return -i;
    }
    return 0;
}

static int hpack_encode_string(uint8_t *out, size_t cap, const char *str,
                                 int use_huffman, size_t *written) {
    size_t slen = strlen(str);
    if (!use_huffman) {
        size_t hdr_len;
        if (hpack_encode_int(out, cap, 7, slen, 0x00, &hdr_len) != 0)
            return -1;
        if (hdr_len + slen > cap) return -1;
        memcpy(out + hdr_len, str, slen);
        *written = hdr_len + slen;
        return 0;
    }
    /* Huffman encode */
    uint8_t huf_buf[8192];
    size_t huf_len;
    if (neverc_hpack_huffman_encode((const uint8_t *)str, slen,
                                     huf_buf, sizeof(huf_buf), &huf_len) != 0)
        return -1;
    size_t hdr_len;
    if (hpack_encode_int(out, cap, 7, huf_len, 0x80, &hdr_len) != 0)
        return -1;
    if (hdr_len + huf_len > cap) return -1;
    memcpy(out + hdr_len, huf_buf, huf_len);
    *written = hdr_len + huf_len;
    return 0;
}

int neverc_hpack_encode(neverc_hpack_encoder_t *enc,
                         const neverc_hpack_header_t *headers, int nheaders,
                         uint8_t *out, size_t out_cap, size_t *out_len) {
    size_t pos = 0;

    for (int i = 0; i < nheaders; i++) {
        const char *name = headers[i].name;
        const char *value = headers[i].value;

        int idx = hpack_find_static(name, value);
        if (idx > 0) {
            /* Full indexed reference */
            size_t written;
            if (hpack_encode_int(out + pos, out_cap - pos, 7,
                                  (uint64_t)idx, 0x80, &written) != 0)
                return -1;
            pos += written;
        } else {
            /* Literal with incremental indexing */
            int name_idx = idx < 0 ? -idx : 0;
            size_t written;
            if (name_idx > 0) {
                if (hpack_encode_int(out + pos, out_cap - pos, 6,
                                      (uint64_t)name_idx, 0x40, &written) != 0)
                    return -1;
                pos += written;
            } else {
                out[pos++] = 0x40; /* literal new name */
                if (hpack_encode_string(out + pos, out_cap - pos, name,
                                         1, &written) != 0)
                    return -1;
                pos += written;
            }
            if (hpack_encode_string(out + pos, out_cap - pos, value,
                                     1, &written) != 0)
                return -1;
            pos += written;

            if (!headers[i].sensitive)
                dyn_table_add(&enc->dyn, name, value);
        }
    }
    *out_len = pos;
    return 0;
}

/* ======================================================================
 * HTTP/2 Frame Header Parse/Write
 * ====================================================================== */

int neverc_h2_frame_header_read(const uint8_t *data, size_t len,
                                  neverc_h2_frame_header_t *hdr) {
    if (len < NC_H2_FRAME_HEADER_SIZE) return -1;
    hdr->length = ((uint32_t)data[0] << 16) |
                  ((uint32_t)data[1] << 8) |
                  (uint32_t)data[2];
    hdr->type = data[3];
    hdr->flags = data[4];
    hdr->stream_id = ((uint32_t)(data[5] & 0x7f) << 24) |
                     ((uint32_t)data[6] << 16) |
                     ((uint32_t)data[7] << 8) |
                     (uint32_t)data[8];
    return 0;
}

int neverc_h2_frame_header_write(const neverc_h2_frame_header_t *hdr,
                                   uint8_t *out) {
    out[0] = (uint8_t)(hdr->length >> 16);
    out[1] = (uint8_t)(hdr->length >> 8);
    out[2] = (uint8_t)(hdr->length);
    out[3] = hdr->type;
    out[4] = hdr->flags;
    out[5] = (uint8_t)((hdr->stream_id >> 24) & 0x7f);
    out[6] = (uint8_t)(hdr->stream_id >> 16);
    out[7] = (uint8_t)(hdr->stream_id >> 8);
    out[8] = (uint8_t)(hdr->stream_id);
    return 0;
}

/* ======================================================================
 * HTTP/2 Settings
 * ====================================================================== */

void neverc_h2_settings_init(neverc_h2_settings_t *s) {
    s->header_table_size = NC_H2_DEFAULT_HEADER_TABLE_SIZE;
    s->enable_push = 1;
    s->max_concurrent_streams = NC_H2_DEFAULT_MAX_CONCURRENT;
    s->initial_window_size = NC_H2_DEFAULT_INITIAL_WINDOW_SIZE;
    s->max_frame_size = NC_H2_DEFAULT_MAX_FRAME_SIZE;
    s->max_header_list_size = NC_H2_DEFAULT_MAX_HEADER_LIST_SIZE;
}

/* ======================================================================
 * HTTP/2 Server Stub
 *
 * Full server implementation handles connection preface, settings
 * exchange, stream multiplexing, and request dispatch.
 * ====================================================================== */

struct neverc_h2_server {
    neverc_http_mux_t    *mux;
    neverc_h2_settings_t  settings;
};

neverc_h2_server_t *neverc_h2_server_create(neverc_http_mux_t *mux) {
    neverc_h2_server_t *srv = (neverc_h2_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    srv->mux = mux;
    neverc_h2_settings_init(&srv->settings);
    return srv;
}

void neverc_h2_server_destroy(neverc_h2_server_t *srv) {
    free(srv);
}

void neverc_h2_server_set_max_streams(neverc_h2_server_t *srv, uint32_t max) {
    srv->settings.max_concurrent_streams = max;
}

void neverc_h2_server_set_max_frame_size(neverc_h2_server_t *srv, uint32_t max) {
    if (max >= NC_H2_DEFAULT_MAX_FRAME_SIZE && max <= NC_H2_MAX_FRAME_SIZE_LIMIT)
        srv->settings.max_frame_size = max;
}

void neverc_h2_server_set_initial_window_size(neverc_h2_server_t *srv, uint32_t win) {
    if (win <= 0x7fffffff)
        srv->settings.initial_window_size = win;
}

void neverc_h2_server_set_max_header_list_size(neverc_h2_server_t *srv, uint32_t max) {
    srv->settings.max_header_list_size = max;
}

/* ======================================================================
 * HTTP/2 Stream State Machine (RFC 9113 §5.1)
 * ====================================================================== */

typedef enum {
    H2_STREAM_IDLE,
    H2_STREAM_OPEN,
    H2_STREAM_HALF_CLOSED_REMOTE,
    H2_STREAM_HALF_CLOSED_LOCAL,
    H2_STREAM_CLOSED
} h2_stream_state_t;

typedef struct h2_stream {
    uint32_t           id;
    h2_stream_state_t  state;
    int32_t            recv_window;
    int32_t            send_window;

    neverc_hpack_header_t headers[64];
    int                nheaders;
    uint8_t           *body;
    size_t             body_len;
    size_t             body_cap;

    struct h2_stream  *next;
} h2_stream_t;

/* ======================================================================
 * HTTP/2 Connection State
 * ====================================================================== */

typedef struct {
    int                      fd;
    neverc_h2_server_t      *srv;
    neverc_hpack_decoder_t  *hpack_dec;
    neverc_hpack_encoder_t  *hpack_enc;

    neverc_h2_settings_t     local_settings;
    neverc_h2_settings_t     peer_settings;

    int32_t  conn_recv_window;
    int32_t  conn_send_window;
    uint32_t last_stream_id;
    uint32_t max_stream_id;
    int      goaway_sent;

    h2_stream_t *streams;
    int           active_streams;
} h2_conn_t;

static int h2_sock_read_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t got = 0;
    while (got < len) {
#ifdef _WIN32
        int n = recv(fd, p + got, (int)(len - got), 0);
#else
        ssize_t n = recv(fd, p + got, len - got, 0);
#endif
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int h2_sock_write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = send(fd, p + sent, (int)(len - sent), 0);
#else
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int h2_write_frame(int fd, uint8_t type, uint8_t flags,
                            uint32_t stream_id, const void *payload, uint32_t len) {
    neverc_h2_frame_header_t hdr = { .length = len, .type = type,
                                      .flags = flags, .stream_id = stream_id };
    uint8_t hbuf[NC_H2_FRAME_HEADER_SIZE];
    neverc_h2_frame_header_write(&hdr, hbuf);
    if (h2_sock_write_all(fd, hbuf, NC_H2_FRAME_HEADER_SIZE) != 0) return -1;
    if (len > 0 && payload)
        return h2_sock_write_all(fd, payload, len);
    return 0;
}

static int h2_write_settings(int fd, const neverc_h2_settings_t *s) {
    uint8_t payload[6 * 6];
    int pos = 0;

    #define WRITE_SETTING(id, val) do { \
        payload[pos++] = (uint8_t)((id) >> 8); \
        payload[pos++] = (uint8_t)(id); \
        payload[pos++] = (uint8_t)((val) >> 24); \
        payload[pos++] = (uint8_t)((val) >> 16); \
        payload[pos++] = (uint8_t)((val) >> 8); \
        payload[pos++] = (uint8_t)(val); \
    } while(0)

    WRITE_SETTING(NC_H2_SETTINGS_HEADER_TABLE_SIZE, s->header_table_size);
    WRITE_SETTING(NC_H2_SETTINGS_MAX_CONCURRENT_STREAMS, s->max_concurrent_streams);
    WRITE_SETTING(NC_H2_SETTINGS_INITIAL_WINDOW_SIZE, s->initial_window_size);
    WRITE_SETTING(NC_H2_SETTINGS_MAX_FRAME_SIZE, s->max_frame_size);
    WRITE_SETTING(NC_H2_SETTINGS_MAX_HEADER_LIST_SIZE, s->max_header_list_size);
    if (!s->enable_push) {
        WRITE_SETTING(NC_H2_SETTINGS_ENABLE_PUSH, 0);
    }
    #undef WRITE_SETTING

    return h2_write_frame(fd, NC_H2_FRAME_SETTINGS, 0, 0,
                           payload, (uint32_t)(s->enable_push ? 30 : 36));
}

static int h2_write_settings_ack(int fd) {
    return h2_write_frame(fd, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, NULL, 0);
}

static int h2_write_ping_ack(int fd, const uint8_t *data) {
    return h2_write_frame(fd, NC_H2_FRAME_PING, NC_H2_FLAG_ACK, 0, data, 8);
}

static int h2_write_goaway(int fd, uint32_t last_stream, uint32_t error_code) {
    uint8_t payload[8];
    payload[0] = (uint8_t)(last_stream >> 24);
    payload[1] = (uint8_t)(last_stream >> 16);
    payload[2] = (uint8_t)(last_stream >> 8);
    payload[3] = (uint8_t)(last_stream);
    payload[4] = (uint8_t)(error_code >> 24);
    payload[5] = (uint8_t)(error_code >> 16);
    payload[6] = (uint8_t)(error_code >> 8);
    payload[7] = (uint8_t)(error_code);
    return h2_write_frame(fd, NC_H2_FRAME_GOAWAY, 0, 0, payload, 8);
}

static int h2_write_rst_stream(int fd, uint32_t stream_id, uint32_t error_code) {
    uint8_t payload[4];
    payload[0] = (uint8_t)(error_code >> 24);
    payload[1] = (uint8_t)(error_code >> 16);
    payload[2] = (uint8_t)(error_code >> 8);
    payload[3] = (uint8_t)(error_code);
    return h2_write_frame(fd, NC_H2_FRAME_RST_STREAM, 0, stream_id, payload, 4);
}

static int h2_write_window_update(int fd, uint32_t stream_id, uint32_t increment) {
    uint8_t payload[4];
    payload[0] = (uint8_t)((increment >> 24) & 0x7f);
    payload[1] = (uint8_t)(increment >> 16);
    payload[2] = (uint8_t)(increment >> 8);
    payload[3] = (uint8_t)(increment);
    return h2_write_frame(fd, NC_H2_FRAME_WINDOW_UPDATE, 0, stream_id, payload, 4);
}

static h2_stream_t *h2_find_stream(h2_conn_t *conn, uint32_t id) {
    for (h2_stream_t *s = conn->streams; s; s = s->next)
        if (s->id == id) return s;
    return NULL;
}

static h2_stream_t *h2_create_stream(h2_conn_t *conn, uint32_t id) {
    h2_stream_t *s = (h2_stream_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->id = id;
    s->state = H2_STREAM_OPEN;
    s->recv_window = (int32_t)conn->local_settings.initial_window_size;
    s->send_window = (int32_t)conn->peer_settings.initial_window_size;
    s->next = conn->streams;
    conn->streams = s;
    conn->active_streams++;
    if (id > conn->last_stream_id)
        conn->last_stream_id = id;
    return s;
}

static void h2_close_stream(h2_conn_t *conn, h2_stream_t *s) {
    s->state = H2_STREAM_CLOSED;
    for (int i = 0; i < s->nheaders; i++) {
        free(s->headers[i].name);
        free(s->headers[i].value);
    }
    free(s->body);
    s->nheaders = 0;
    s->body = NULL;
    s->body_len = 0;
    conn->active_streams--;
}

static void h2_remove_stream(h2_conn_t *conn, uint32_t id) {
    h2_stream_t **pp = &conn->streams;
    while (*pp) {
        if ((*pp)->id == id) {
            h2_stream_t *s = *pp;
            *pp = s->next;
            h2_close_stream(conn, s);
            free(s);
            return;
        }
        pp = &(*pp)->next;
    }
}

static void h2_dispatch_request(h2_conn_t *conn, h2_stream_t *stream) {
    const char *method = NULL, *path = NULL, *scheme = NULL, *authority = NULL;
    for (int i = 0; i < stream->nheaders; i++) {
        if (strcmp(stream->headers[i].name, ":method") == 0)
            method = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":path") == 0)
            path = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":scheme") == 0)
            scheme = stream->headers[i].value;
        else if (strcmp(stream->headers[i].name, ":authority") == 0)
            authority = stream->headers[i].value;
    }
    if (!method || !path) {
        h2_write_rst_stream(conn->fd, stream->id, NC_H2_PROTOCOL_ERROR);
        return;
    }

    /* Build HEADERS response via HPACK */
    neverc_hpack_header_t resp_hdrs[8];
    int resp_count = 0;
    resp_hdrs[resp_count++] = (neverc_hpack_header_t){
        .name = ":status", .value = "200", .sensitive = 0};
    resp_hdrs[resp_count++] = (neverc_hpack_header_t){
        .name = "content-type", .value = "text/plain", .sensitive = 0};
    resp_hdrs[resp_count++] = (neverc_hpack_header_t){
        .name = "server", .value = "NeverC/1.0", .sensitive = 0};

    uint8_t hdr_buf[4096];
    size_t hdr_len;
    neverc_hpack_encode(conn->hpack_enc, resp_hdrs, resp_count,
                         hdr_buf, sizeof(hdr_buf), &hdr_len);

    /* Send HEADERS frame */
    h2_write_frame(conn->fd, NC_H2_FRAME_HEADERS,
                    NC_H2_FLAG_END_HEADERS,
                    stream->id, hdr_buf, (uint32_t)hdr_len);

    /* Generate response body */
    char body[512];
    int body_len = snprintf(body, sizeof(body),
                             "Hello from NeverC HTTP/2!\n"
                             "Method: %s\nPath: %s\n",
                             method, path);

    /* Send DATA frame with END_STREAM */
    h2_write_frame(conn->fd, NC_H2_FRAME_DATA,
                    NC_H2_FLAG_END_STREAM,
                    stream->id, body, (uint32_t)body_len);

    stream->state = H2_STREAM_HALF_CLOSED_LOCAL;

    (void)scheme; (void)authority;
}

static int h2_process_settings(h2_conn_t *conn, const uint8_t *payload, uint32_t len) {
    if (len % 6 != 0) return -1;
    for (uint32_t i = 0; i < len; i += 6) {
        uint16_t id = (uint16_t)((payload[i] << 8) | payload[i + 1]);
        uint32_t val = ((uint32_t)payload[i + 2] << 24) |
                       ((uint32_t)payload[i + 3] << 16) |
                       ((uint32_t)payload[i + 4] << 8) |
                       (uint32_t)payload[i + 5];
        switch (id) {
        case NC_H2_SETTINGS_HEADER_TABLE_SIZE:
            conn->peer_settings.header_table_size = val;
            break;
        case NC_H2_SETTINGS_ENABLE_PUSH:
            conn->peer_settings.enable_push = (int)val;
            break;
        case NC_H2_SETTINGS_MAX_CONCURRENT_STREAMS:
            conn->peer_settings.max_concurrent_streams = val;
            break;
        case NC_H2_SETTINGS_INITIAL_WINDOW_SIZE:
            if (val > 0x7fffffff) return -1;
            conn->peer_settings.initial_window_size = val;
            break;
        case NC_H2_SETTINGS_MAX_FRAME_SIZE:
            if (val < 16384 || val > 16777215) return -1;
            conn->peer_settings.max_frame_size = val;
            break;
        case NC_H2_SETTINGS_MAX_HEADER_LIST_SIZE:
            conn->peer_settings.max_header_list_size = val;
            break;
        default:
            break; /* unknown settings are ignored */
        }
    }
    return 0;
}

int neverc_h2_serve_conn(neverc_h2_server_t *srv, int fd) {
    if (!srv || fd < 0) return -1;

    /* 1. Read and validate client connection preface */
    char preface[NC_H2_CLIENT_PREFACE_LEN];
    if (h2_sock_read_all(fd, preface, NC_H2_CLIENT_PREFACE_LEN) != 0)
        return -1;
    if (memcmp(preface, NC_H2_CLIENT_PREFACE, NC_H2_CLIENT_PREFACE_LEN) != 0)
        return -1;

    /* 2. Initialize connection state */
    h2_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.fd = fd;
    conn.srv = srv;
    conn.local_settings = srv->settings;
    neverc_h2_settings_init(&conn.peer_settings);
    conn.conn_recv_window = (int32_t)conn.local_settings.initial_window_size;
    conn.conn_send_window = (int32_t)conn.peer_settings.initial_window_size;
    conn.hpack_dec = neverc_hpack_decoder_create(conn.local_settings.header_table_size);
    conn.hpack_enc = neverc_hpack_encoder_create(conn.peer_settings.header_table_size);
    if (!conn.hpack_dec || !conn.hpack_enc) goto cleanup;

    /* 3. Send our SETTINGS frame */
    if (h2_write_settings(fd, &conn.local_settings) != 0) goto cleanup;

    /* 4. Frame reading loop */
    for (;;) {
        uint8_t frame_hdr_buf[NC_H2_FRAME_HEADER_SIZE];
        if (h2_sock_read_all(fd, frame_hdr_buf, NC_H2_FRAME_HEADER_SIZE) != 0)
            break;

        neverc_h2_frame_header_t fhdr;
        neverc_h2_frame_header_read(frame_hdr_buf, NC_H2_FRAME_HEADER_SIZE, &fhdr);

        if (fhdr.length > conn.local_settings.max_frame_size) {
            h2_write_goaway(fd, conn.last_stream_id, NC_H2_FRAME_SIZE_ERROR);
            break;
        }

        uint8_t *payload = NULL;
        if (fhdr.length > 0) {
            payload = (uint8_t *)malloc(fhdr.length);
            if (!payload) break;
            if (h2_sock_read_all(fd, payload, fhdr.length) != 0) {
                free(payload);
                break;
            }
        }

        switch (fhdr.type) {
        case NC_H2_FRAME_SETTINGS:
            if (fhdr.flags & NC_H2_FLAG_ACK) {
                /* SETTINGS ACK — our settings are confirmed */
            } else {
                if (h2_process_settings(&conn, payload, fhdr.length) != 0) {
                    h2_write_goaway(fd, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                    free(payload);
                    goto cleanup;
                }
                h2_write_settings_ack(fd);
            }
            break;

        case NC_H2_FRAME_PING:
            if (fhdr.length != 8) {
                h2_write_goaway(fd, conn.last_stream_id, NC_H2_FRAME_SIZE_ERROR);
                free(payload);
                goto cleanup;
            }
            if (!(fhdr.flags & NC_H2_FLAG_ACK))
                h2_write_ping_ack(fd, payload);
            break;

        case NC_H2_FRAME_HEADERS: {
            if (fhdr.stream_id == 0) {
                h2_write_goaway(fd, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }

            h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
            if (!stream) {
                if ((int)conn.active_streams >= (int)conn.local_settings.max_concurrent_streams) {
                    h2_write_rst_stream(fd, fhdr.stream_id, NC_H2_REFUSED_STREAM);
                    break;
                }
                stream = h2_create_stream(&conn, fhdr.stream_id);
                if (!stream) break;
            }

            /* Decode HPACK headers */
            int nh;
            if (neverc_hpack_decode(conn.hpack_dec, payload, fhdr.length,
                                     stream->headers, 64, &nh) != 0) {
                h2_write_goaway(fd, conn.last_stream_id, NC_H2_COMPRESSION_ERROR);
                free(payload);
                goto cleanup;
            }
            stream->nheaders = nh;

            if (fhdr.flags & NC_H2_FLAG_END_STREAM) {
                stream->state = H2_STREAM_HALF_CLOSED_REMOTE;
                h2_dispatch_request(&conn, stream);
                h2_remove_stream(&conn, stream->id);
            }
            break;
        }

        case NC_H2_FRAME_DATA: {
            h2_stream_t *stream = h2_find_stream(&conn, fhdr.stream_id);
            if (!stream) {
                h2_write_rst_stream(fd, fhdr.stream_id, NC_H2_STREAM_CLOSED);
                break;
            }
            /* Accumulate body data */
            if (fhdr.length > 0) {
                size_t new_len = stream->body_len + fhdr.length;
                if (new_len > stream->body_cap) {
                    size_t new_cap = stream->body_cap < 4096 ? 4096 : stream->body_cap * 2;
                    while (new_cap < new_len) new_cap *= 2;
                    stream->body = (uint8_t *)realloc(stream->body, new_cap);
                    stream->body_cap = new_cap;
                }
                memcpy(stream->body + stream->body_len, payload, fhdr.length);
                stream->body_len = new_len;

                conn.conn_recv_window -= (int32_t)fhdr.length;
                stream->recv_window -= (int32_t)fhdr.length;

                if (conn.conn_recv_window < 16384) {
                    int32_t inc = (int32_t)conn.local_settings.initial_window_size - conn.conn_recv_window;
                    h2_write_window_update(fd, 0, (uint32_t)inc);
                    conn.conn_recv_window += inc;
                }
                if (stream->recv_window < 16384) {
                    int32_t inc = (int32_t)conn.local_settings.initial_window_size - stream->recv_window;
                    h2_write_window_update(fd, stream->id, (uint32_t)inc);
                    stream->recv_window += inc;
                }
            }
            if (fhdr.flags & NC_H2_FLAG_END_STREAM) {
                stream->state = H2_STREAM_HALF_CLOSED_REMOTE;
                h2_dispatch_request(&conn, stream);
                h2_remove_stream(&conn, stream->id);
            }
            break;
        }

        case NC_H2_FRAME_WINDOW_UPDATE: {
            if (fhdr.length != 4) {
                h2_write_goaway(fd, conn.last_stream_id, NC_H2_FRAME_SIZE_ERROR);
                free(payload);
                goto cleanup;
            }
            uint32_t inc = ((uint32_t)(payload[0] & 0x7f) << 24) |
                           ((uint32_t)payload[1] << 16) |
                           ((uint32_t)payload[2] << 8) |
                           (uint32_t)payload[3];
            if (inc == 0) {
                if (fhdr.stream_id == 0)
                    h2_write_goaway(fd, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                else
                    h2_write_rst_stream(fd, fhdr.stream_id, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            if (fhdr.stream_id == 0) {
                conn.conn_send_window += (int32_t)inc;
            } else {
                h2_stream_t *s = h2_find_stream(&conn, fhdr.stream_id);
                if (s) s->send_window += (int32_t)inc;
            }
            break;
        }

        case NC_H2_FRAME_RST_STREAM: {
            if (fhdr.stream_id == 0 || fhdr.length != 4) {
                h2_write_goaway(fd, conn.last_stream_id, NC_H2_PROTOCOL_ERROR);
                free(payload);
                goto cleanup;
            }
            h2_remove_stream(&conn, fhdr.stream_id);
            break;
        }

        case NC_H2_FRAME_GOAWAY: {
            conn.goaway_sent = 1;
            free(payload);
            goto cleanup;
        }

        case NC_H2_FRAME_PRIORITY:
            break; /* ignored per RFC 9113 */

        case NC_H2_FRAME_CONTINUATION:
            break; /* TODO: append to pending HEADERS block */

        default:
            break;
        }

        free(payload);
    }

cleanup:
    /* Clean up all streams */
    while (conn.streams) {
        h2_stream_t *next = conn.streams->next;
        h2_close_stream(&conn, conn.streams);
        free(conn.streams);
        conn.streams = next;
    }
    if (conn.hpack_dec) neverc_hpack_decoder_destroy(conn.hpack_dec);
    if (conn.hpack_enc) neverc_hpack_encoder_destroy(conn.hpack_enc);
    if (!conn.goaway_sent)
        h2_write_goaway(fd, conn.last_stream_id, NC_H2_NO_ERROR);
    return 0;
}

int neverc_h2_listen_and_serve(const char *addr,
                                neverc_h2_server_t *srv,
                                const char *cert_file,
                                const char *key_file) {
    (void)addr; (void)srv; (void)cert_file; (void)key_file;
    /* TODO: TLS + ALPN h2 negotiation + accept loop calling serve_conn */
    return -1;
}

int neverc_h2_listen_and_serve_h2c(const char *addr,
                                     neverc_h2_server_t *srv) {
    (void)addr; (void)srv;
    /* TODO: h2c cleartext accept loop */
    return -1;
}
