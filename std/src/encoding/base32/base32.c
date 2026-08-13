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

    size_t clean_len = 0;
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] != '\r' && src[i] != '\n')
            clean_len++;
    }
    if (clean_len == 0)
        return 0;

    size_t padding = 0;
    for (size_t i = src_len; i > 0; ) {
        unsigned char c = (unsigned char)src[--i];
        if (c == '\r' || c == '\n')
            continue;
        if (c != '=')
            break;
        padding++;
    }

    size_t encoded_len = clean_len - padding;
    size_t remain = encoded_len % 8;
    size_t tail_bytes;
    size_t expected_padding;
    uint8_t unused_mask;
    switch (remain) {
    case 0:
        tail_bytes = 0;
        expected_padding = 0;
        unused_mask = 0;
        break;
    case 2:
        tail_bytes = 1;
        expected_padding = 6;
        unused_mask = 0x03;
        break;
    case 4:
        tail_bytes = 2;
        expected_padding = 4;
        unused_mask = 0x0f;
        break;
    case 5:
        tail_bytes = 3;
        expected_padding = 3;
        unused_mask = 0x01;
        break;
    case 7:
        tail_bytes = 4;
        expected_padding = 1;
        unused_mask = 0x07;
        break;
    default: return -1;
    }
    if (padding != 0 &&
        (clean_len % 8 != 0 || padding != expected_padding))
        return -1;

    size_t logical = 0;
    uint8_t last_value = 0;
    for (size_t i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\r' || c == '\n')
            continue;
        if (logical < encoded_len) {
            uint8_t value = dec_table[c];
            if (value & B32_INV)
                return -1;
            if (logical + 1 == encoded_len)
                last_value = value;
        } else if (c != '=') {
            return -1;
        }
        logical++;
    }
    if (logical != clean_len || (last_value & unused_mask) != 0)
        return -1;

    size_t decoded_len = (encoded_len / 8) * 5 + tail_bytes;
    if (decoded_len > INT_MAX)
        return -1;

    size_t di = 0;
    uint8_t values[8];
    size_t value_count = 0;
    size_t data_seen = 0;
    for (size_t i = 0; i < src_len && data_seen < encoded_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\r' || c == '\n')
            continue;
        values[value_count++] = dec_table[c];
        data_seen++;
        if (value_count == 8) {
            uint64_t val = ((uint64_t)values[0] << 35) |
                           ((uint64_t)values[1] << 30) |
                           ((uint64_t)values[2] << 25) |
                           ((uint64_t)values[3] << 20) |
                           ((uint64_t)values[4] << 15) |
                           ((uint64_t)values[5] << 10) |
                           ((uint64_t)values[6] << 5) |
                           (uint64_t)values[7];
            dst[di] = (uint8_t)(val >> 32);
            dst[di + 1] = (uint8_t)(val >> 24);
            dst[di + 2] = (uint8_t)(val >> 16);
            dst[di + 3] = (uint8_t)(val >> 8);
            dst[di + 4] = (uint8_t)val;
            di += 5;
            value_count = 0;
        }
    }

    if (value_count > 0) {
        uint64_t val = 0;
        for (size_t k = 0; k < value_count; k++)
            val |= (uint64_t)values[k] << (35 - k * 5);
        if (tail_bytes > 0) dst[di++] = (uint8_t)(val >> 32);
        if (tail_bytes > 1) dst[di++] = (uint8_t)(val >> 24);
        if (tail_bytes > 2) dst[di++] = (uint8_t)(val >> 16);
        if (tail_bytes > 3) dst[di++] = (uint8_t)(val >> 8);
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
