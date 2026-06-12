#include "neverc/std/encoding/base64.h"

static const char std_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char url_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/*
 * Combined decode + validation table: valid entries are 0-63, invalid = 0x80.
 * Single lookup validates AND decodes; batch check: (a|b|c|d) & 0x80.
 */
#define B64_INV 0x80
#define X B64_INV

static const uint8_t decode_std[256] = {
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
    X, X, X, X, X, X, X, X, X, X, X,62, X,62, X,63,
   52,53,54,55,56,57,58,59,60,61, X, X, X, X, X, X,
    X, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
   15,16,17,18,19,20,21,22,23,24,25, X, X, X, X,63,
    X,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
   41,42,43,44,45,46,47,48,49,50,51, X, X, X, X, X,
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

size_t neverc_base64_encoded_len(size_t n) {
    return ((n + 2) / 3) * 4;
}

size_t neverc_base64_decoded_len(size_t n) {
    return (n / 4) * 3;
}

static size_t encode_with_table(char *dst, const uint8_t *src, size_t src_len,
                                const char *table) {
    size_t di = 0;
    size_t si = 0;

    size_t n6 = (src_len / 6) * 6;
    while (si < n6) {
        uint32_t v0 = ((uint32_t)src[si] << 16) |
                       ((uint32_t)src[si+1] << 8) |
                       (uint32_t)src[si+2];
        uint32_t v1 = ((uint32_t)src[si+3] << 16) |
                       ((uint32_t)src[si+4] << 8) |
                       (uint32_t)src[si+5];
        dst[di]   = table[(v0 >> 18) & 0x3f];
        dst[di+1] = table[(v0 >> 12) & 0x3f];
        dst[di+2] = table[(v0 >> 6)  & 0x3f];
        dst[di+3] = table[v0         & 0x3f];
        dst[di+4] = table[(v1 >> 18) & 0x3f];
        dst[di+5] = table[(v1 >> 12) & 0x3f];
        dst[di+6] = table[(v1 >> 6)  & 0x3f];
        dst[di+7] = table[v1         & 0x3f];
        si += 6;
        di += 8;
    }

    size_t n3 = (src_len / 3) * 3;
    while (si < n3) {
        uint32_t val = ((uint32_t)src[si] << 16) |
                       ((uint32_t)src[si+1] << 8) |
                       (uint32_t)src[si+2];
        dst[di]   = table[(val >> 18) & 0x3f];
        dst[di+1] = table[(val >> 12) & 0x3f];
        dst[di+2] = table[(val >> 6)  & 0x3f];
        dst[di+3] = table[val         & 0x3f];
        si += 3;
        di += 4;
    }

    size_t remain = src_len - si;
    if (remain == 1) {
        uint32_t val = (uint32_t)src[si] << 16;
        dst[di]   = table[(val >> 18) & 0x3f];
        dst[di+1] = table[(val >> 12) & 0x3f];
        dst[di+2] = '=';
        dst[di+3] = '=';
        di += 4;
    } else if (remain == 2) {
        uint32_t val = ((uint32_t)src[si] << 16) |
                       ((uint32_t)src[si+1] << 8);
        dst[di]   = table[(val >> 18) & 0x3f];
        dst[di+1] = table[(val >> 12) & 0x3f];
        dst[di+2] = table[(val >> 6)  & 0x3f];
        dst[di+3] = '=';
        di += 4;
    }

    dst[di] = '\0';
    return di;
}

static int decode_impl(uint8_t *dst, const char *src, size_t src_len) {
    while (src_len > 0 && src[src_len - 1] == '=')
        src_len--;

    size_t di = 0;
    size_t si = 0;

    size_t n = (src_len / 4) * 4;
    while (si < n) {
        uint8_t a = decode_std[(uint8_t)src[si]];
        uint8_t b = decode_std[(uint8_t)src[si+1]];
        uint8_t c = decode_std[(uint8_t)src[si+2]];
        uint8_t d = decode_std[(uint8_t)src[si+3]];
        if ((a | b | c | d) & B64_INV) return -1;
        uint32_t val = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                       ((uint32_t)c << 6) | (uint32_t)d;
        dst[di]   = (uint8_t)(val >> 16);
        dst[di+1] = (uint8_t)(val >> 8);
        dst[di+2] = (uint8_t)val;
        si += 4;
        di += 3;
    }

    size_t remain = src_len - si;
    if (remain == 2) {
        uint8_t a = decode_std[(uint8_t)src[si]];
        uint8_t b = decode_std[(uint8_t)src[si+1]];
        if ((a | b) & B64_INV) return -1;
        uint32_t val = ((uint32_t)a << 18) | ((uint32_t)b << 12);
        dst[di] = (uint8_t)(val >> 16);
        di += 1;
    } else if (remain == 3) {
        uint8_t a = decode_std[(uint8_t)src[si]];
        uint8_t b = decode_std[(uint8_t)src[si+1]];
        uint8_t c = decode_std[(uint8_t)src[si+2]];
        if ((a | b | c) & B64_INV) return -1;
        uint32_t val = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                       ((uint32_t)c << 6);
        dst[di]   = (uint8_t)(val >> 16);
        dst[di+1] = (uint8_t)(val >> 8);
        di += 2;
    } else if (remain == 1) {
        return -1;
    }

    return (int)di;
}

size_t neverc_base64_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, std_table);
}

int neverc_base64_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len);
}

size_t neverc_base64_url_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, url_table);
}

int neverc_base64_url_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len);
}
