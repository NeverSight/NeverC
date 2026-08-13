#include "neverc/std/encoding/base64.h"

#include <limits.h>

static const char std_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char url_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t neverc_base64_encoded_len(size_t n) {
    size_t groups = n / 3 + (n % 3 != 0);
    if (groups > SIZE_MAX / 4)
        return SIZE_MAX;
    return groups * 4;
}

size_t neverc_base64_decoded_len(size_t n) {
    /* Upper bound on bytes the decoder may write for n input chars. The decoder
     * accepts unpadded input (RawStd/RawURL, as in JWTs), where a trailing group
     * of 2 or 3 chars yields 1 or 2 bytes beyond (n/4)*3. Reporting only the
     * padded (n/4)*3 made callers that size a buffer by this overflow the heap on
     * unpadded input; include the remainder so the bound is exact. */
    size_t full = (n / 4) * 3;
    size_t rem = n % 4;
    if (rem == 2) full += 1;
    else if (rem == 3) full += 2;
    return full;
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

    return di;
}

static int decode_value(unsigned char c, int url_safe) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (url_safe)
        return c == '-' ? 62 : c == '_' ? 63 : -1;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}

static int decode_impl(uint8_t *dst, const char *src, size_t src_len,
                       int url_safe) {
    const uintmax_t max_encoded_for_int =
        ((uintmax_t)INT_MAX / 3 + ((uintmax_t)INT_MAX % 3 != 0)) * 4;
    if ((uintmax_t)src_len > max_encoded_for_int ||
        ((!dst || !src) && src_len != 0))
        return -1;
    if (src_len == 0)
        return 0;

    size_t padding = 0;
    while (padding < src_len && src[src_len - 1 - padding] == '=')
        ++padding;
    if (padding > 2 || (padding != 0 && src_len % 4 != 0))
        return -1;

    size_t encoded_len = src_len - padding;
    size_t remain = encoded_len % 4;
    if (remain == 1 ||
        (padding == 1 && remain != 3) ||
        (padding == 2 && remain != 2))
        return -1;
    for (size_t i = 0; i < encoded_len; ++i) {
        if (src[i] == '=')
            return -1;
    }

    size_t decoded_len = (encoded_len / 4) * 3 +
                         (remain == 2 ? 1 : remain == 3 ? 2 : 0);
    if ((uintmax_t)decoded_len > (uintmax_t)INT_MAX)
        return -1;

    size_t di = 0;
    size_t si = 0;

    size_t n = (encoded_len / 4) * 4;
    while (si < n) {
        int a = decode_value((unsigned char)src[si], url_safe);
        int b = decode_value((unsigned char)src[si + 1], url_safe);
        int c = decode_value((unsigned char)src[si + 2], url_safe);
        int d = decode_value((unsigned char)src[si + 3], url_safe);
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return -1;
        uint32_t val = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                       ((uint32_t)c << 6) | (uint32_t)d;
        dst[di]   = (uint8_t)(val >> 16);
        dst[di+1] = (uint8_t)(val >> 8);
        dst[di+2] = (uint8_t)val;
        si += 4;
        di += 3;
    }

    if (remain == 2) {
        int a = decode_value((unsigned char)src[si], url_safe);
        int b = decode_value((unsigned char)src[si + 1], url_safe);
        if (a < 0 || b < 0 || (b & 0x0f) != 0)
            return -1;
        uint32_t val = ((uint32_t)a << 18) | ((uint32_t)b << 12);
        dst[di] = (uint8_t)(val >> 16);
        di += 1;
    } else if (remain == 3) {
        int a = decode_value((unsigned char)src[si], url_safe);
        int b = decode_value((unsigned char)src[si + 1], url_safe);
        int c = decode_value((unsigned char)src[si + 2], url_safe);
        if (a < 0 || b < 0 || c < 0 || (c & 0x03) != 0)
            return -1;
        uint32_t val = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                       ((uint32_t)c << 6);
        dst[di]   = (uint8_t)(val >> 16);
        dst[di+1] = (uint8_t)(val >> 8);
        di += 2;
    }

    return (int)di;
}

size_t neverc_base64_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, std_table);
}

int neverc_base64_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len, 0);
}

size_t neverc_base64_url_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, url_table);
}

int neverc_base64_url_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len, 1);
}
