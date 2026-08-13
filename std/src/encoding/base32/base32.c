#include "neverc/std/encoding/base32.h"
#include <limits.h>

/*
 * Base32 encoding/decoding per RFC 4648.
 *
 * Standard alphabet: A-Z 2-7 (values 0-31)
 * Hex alphabet:      0-9 A-V (values 0-31)
 *
 * Encoding: every 5 input bytes → 8 output characters
 * Padding with '=' to make output length a multiple of 8.
 */

static const char std_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static const char hex_table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

/*
 * Combined decode+validate tables: valid entries are 0-31, invalid = 0x80.
 * Single lookup decodes AND validates; batch check: OR 8 values and test
 * any high bits → (a|b|c|d|e|f|g|h) & 0x80.
 * Eliminates the separate validation tables and loops.
 */
#define B32_INV 0x80
#define X B32_INV

static const uint8_t decode_std[256] = {
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X,26,27,28,29,30,31, X, X, X, X, X, X, X, X,
    X, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
   15,16,17,18,19,20,21,22,23,24,25, X, X, X, X, X,
    X, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
   15,16,17,18,19,20,21,22,23,24,25, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
};

static const uint8_t decode_hex[256] = {
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, X, X, X, X, X, X,
    X,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
   25,26,27,28,29,30,31, X, X, X, X, X, X, X, X, X,
    X,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
   25,26,27,28,29,30,31, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
};

#undef X

size_t neverc_base32_encoded_len(size_t n) {
    size_t groups = n / 5 + (n % 5 != 0);
    if (groups > SIZE_MAX / 8)
        return SIZE_MAX;
    return groups * 8;
}

size_t neverc_base32_decoded_len(size_t n) {
    /* Upper bound on bytes the decoder may write for n input chars. The decoder
     * strips trailing '=' then a leftover group of 2/4/5/7 chars yields 1/2/3/4
     * bytes. Because stripping can turn n%8 into a *smaller* remainder with a
     * *larger* yield (e.g. n%8==3 -> after one '=' m%8==2 writes 1 byte), the
     * bound must be the prefix-maximum of that yield over all m<=n, not the
     * yield at n%8 itself. The old (n/8)*5 (and a naive per-remainder bump)
     * under-reported this, overflowing a caller's buffer on unpadded input. */
    static const unsigned char extra[8] = {0, 0, 1, 1, 2, 3, 3, 4};
    return (n / 8) * 5 + extra[n % 8];
}

static size_t encode_with_table(char *dst, const uint8_t *src, size_t src_len,
                                const char *table) {
    if (src_len == 0)
        return 0;
    if (!dst || !src || neverc_base32_encoded_len(src_len) == SIZE_MAX)
        return SIZE_MAX;

    size_t di = 0;
    size_t si = 0;

    size_t n = (src_len / 5) * 5;
    while (si < n) {
        uint64_t val = ((uint64_t)src[si] << 32) |
                       ((uint64_t)src[si+1] << 24) |
                       ((uint64_t)src[si+2] << 16) |
                       ((uint64_t)src[si+3] << 8) |
                       (uint64_t)src[si+4];
        dst[di]   = table[(val >> 35) & 0x1f];
        dst[di+1] = table[(val >> 30) & 0x1f];
        dst[di+2] = table[(val >> 25) & 0x1f];
        dst[di+3] = table[(val >> 20) & 0x1f];
        dst[di+4] = table[(val >> 15) & 0x1f];
        dst[di+5] = table[(val >> 10) & 0x1f];
        dst[di+6] = table[(val >> 5)  & 0x1f];
        dst[di+7] = table[val         & 0x1f];
        si += 5;
        di += 8;
    }

    size_t remain = src_len - si;
    if (remain > 0) {
        uint64_t val = 0;
        for (size_t k = 0; k < remain; k++)
            val |= (uint64_t)src[si + k] << (32 - k * 8);

        dst[di]   = table[(val >> 35) & 0x1f];
        dst[di+1] = table[(val >> 30) & 0x1f];
        int out_chars;
        switch (remain) {
        case 1: out_chars = 2; break;
        case 2: out_chars = 4; break;
        case 3: out_chars = 5; break;
        case 4: out_chars = 7; break;
        default: out_chars = 0; break;
        }
        if (out_chars > 2) dst[di+2] = table[(val >> 25) & 0x1f];
        if (out_chars > 3) dst[di+3] = table[(val >> 20) & 0x1f];
        if (out_chars > 4) dst[di+4] = table[(val >> 15) & 0x1f];
        if (out_chars > 5) dst[di+5] = table[(val >> 10) & 0x1f];
        if (out_chars > 6) dst[di+6] = table[(val >> 5)  & 0x1f];
        for (int k = out_chars; k < 8; k++)
            dst[di + k] = '=';
        di += 8;
    }

    return di;
}

static int decode_impl(uint8_t *dst, const char *src, size_t src_len,
                       const uint8_t *dec_table) {
    const uintmax_t max_encoded_for_int =
        ((uintmax_t)INT_MAX / 5 + ((uintmax_t)INT_MAX % 5 != 0)) * 8;
    if ((uintmax_t)src_len > max_encoded_for_int ||
        ((!dst || !src) && src_len != 0))
        return -1;

    size_t padded_len = src_len;
    size_t padding = 0;
    while (padding < src_len && src[src_len - 1 - padding] == '=')
        padding++;
    src_len -= padding;

    size_t remain = src_len % 8;
    size_t tail_bytes;
    size_t expected_padding;
    switch (remain) {
    case 0: tail_bytes = 0; expected_padding = 0; break;
    case 2: tail_bytes = 1; expected_padding = 6; break;
    case 4: tail_bytes = 2; expected_padding = 4; break;
    case 5: tail_bytes = 3; expected_padding = 3; break;
    case 7: tail_bytes = 4; expected_padding = 1; break;
    default: return -1;
    }
    if (padding != 0 &&
        (padded_len % 8 != 0 || padding != expected_padding))
        return -1;

    size_t decoded_len = (src_len / 8) * 5 + tail_bytes;
    if (decoded_len > INT_MAX)
        return -1;

    size_t di = 0;
    size_t si = 0;

    size_t n = (src_len / 8) * 8;
    while (si < n) {
        uint8_t a = dec_table[(uint8_t)src[si]];
        uint8_t b = dec_table[(uint8_t)src[si+1]];
        uint8_t c = dec_table[(uint8_t)src[si+2]];
        uint8_t d = dec_table[(uint8_t)src[si+3]];
        uint8_t e = dec_table[(uint8_t)src[si+4]];
        uint8_t f = dec_table[(uint8_t)src[si+5]];
        uint8_t g = dec_table[(uint8_t)src[si+6]];
        uint8_t h = dec_table[(uint8_t)src[si+7]];
        if ((a | b | c | d | e | f | g | h) & B32_INV) return -1;
        uint64_t val = ((uint64_t)a << 35) | ((uint64_t)b << 30) |
                       ((uint64_t)c << 25) | ((uint64_t)d << 20) |
                       ((uint64_t)e << 15) | ((uint64_t)f << 10) |
                       ((uint64_t)g << 5)  | (uint64_t)h;
        dst[di]   = (uint8_t)(val >> 32);
        dst[di+1] = (uint8_t)(val >> 24);
        dst[di+2] = (uint8_t)(val >> 16);
        dst[di+3] = (uint8_t)(val >> 8);
        dst[di+4] = (uint8_t)val;
        si += 8;
        di += 5;
    }

    if (remain > 0) {
        uint64_t val = 0;
        uint8_t check = 0;
        uint8_t last = 0;
        for (size_t k = 0; k < remain; k++) {
            uint8_t v = dec_table[(uint8_t)src[si + k]];
            check |= v;
            last = v;
            val |= (uint64_t)v << (35 - k * 5);
        }
        if (check & B32_INV) return -1;

        int out_bytes;
        uint8_t unused_mask;
        switch (remain) {
        case 2: out_bytes = 1; unused_mask = 0x03; break;
        case 4: out_bytes = 2; unused_mask = 0x0f; break;
        case 5: out_bytes = 3; unused_mask = 0x01; break;
        case 7: out_bytes = 4; unused_mask = 0x07; break;
        default: return -1;
        }
        if ((last & unused_mask) != 0) return -1;
        if (out_bytes > 0) dst[di++] = (uint8_t)(val >> 32);
        if (out_bytes > 1) dst[di++] = (uint8_t)(val >> 24);
        if (out_bytes > 2) dst[di++] = (uint8_t)(val >> 16);
        if (out_bytes > 3) dst[di++] = (uint8_t)(val >> 8);
    }

    return (int)di;
}

size_t neverc_base32_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, std_table);
}

int neverc_base32_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len, decode_std);
}

size_t neverc_base32_hex_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, hex_table);
}

int neverc_base32_hex_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len, decode_hex);
}
