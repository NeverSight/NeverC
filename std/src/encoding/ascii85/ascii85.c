#include "neverc/std/encoding/ascii85.h"
#include <limits.h>
#include <stdint.h>

int neverc_ascii85_max_encoded_len(int n) {
    if (n < 0)
        return -1;
    int groups = n / 4 + (n % 4 != 0);
    if (groups > INT_MAX / 5)
        return -1;
    return groups * 5;
}

/*
 * Powers of 85. The five base-85 digits of a 32-bit group are extracted with
 * independent divisions by these constants (each lowered to a multiply-high by
 * the compiler), instead of the previous five serial `v /= 85` steps whose
 * data dependency serialized the whole group. The top digit needs no `% 85`
 * because v/85^4 < 2^32/85^4 < 85.
 */
#define A85_P4 52200625u  /* 85^4 */
#define A85_P3   614125u  /* 85^3 */
#define A85_P2     7225u  /* 85^2 */
#define A85_P1       85u  /* 85^1 */

int neverc_ascii85_encode(unsigned char *dst, const unsigned char *src, size_t src_len) {
    if (src_len == 0) return 0;
    size_t groups = src_len / 4 + (src_len % 4 != 0);
    if (!dst || !src || groups > (size_t)INT_MAX / 5)
        return -1;

    int n = 0;
    size_t off = 0;

    /* Hot path: full 4-byte groups. */
    while (src_len - off >= 4) {
        unsigned int v = ((unsigned int)src[off + 0] << 24) |
                         ((unsigned int)src[off + 1] << 16) |
                         ((unsigned int)src[off + 2] << 8)  |
                          (unsigned int)src[off + 3];
        if (v == 0) {
            dst[n++] = 'z';
            off += 4;
            continue;
        }
        dst[n + 0] = (unsigned char)('!' + v / A85_P4);
        dst[n + 1] = (unsigned char)('!' + v / A85_P3 % 85u);
        dst[n + 2] = (unsigned char)('!' + v / A85_P2 % 85u);
        dst[n + 3] = (unsigned char)('!' + v / A85_P1 % 85u);
        dst[n + 4] = (unsigned char)('!' + v % 85u);
        n += 5;
        off += 4;
    }

    /* Tail: 1..3 bytes, high-aligned; the 'z' shorthand never applies here. */
    size_t remain = src_len - off;
    if (remain > 0) {
        unsigned int v = 0;
        switch (remain) {
        case 3: v |= (unsigned int)src[off + 2] << 8;  /* fall through */
        case 2: v |= (unsigned int)src[off + 1] << 16; /* fall through */
        case 1: v |= (unsigned int)src[off + 0] << 24; break;
        }
        unsigned char tmp[5];
        tmp[0] = (unsigned char)('!' + v / A85_P4);
        tmp[1] = (unsigned char)('!' + v / A85_P3 % 85u);
        tmp[2] = (unsigned char)('!' + v / A85_P2 % 85u);
        tmp[3] = (unsigned char)('!' + v / A85_P1 % 85u);
        tmp[4] = (unsigned char)('!' + v % 85u);
        int m = (int)remain + 1;
        for (int i = 0; i < m; i++)
            dst[n++] = tmp[i];
    }
    return n;
}

neverc_ascii85_result_t neverc_ascii85_decode(unsigned char *dst, size_t dst_len,
                                               const unsigned char *src, size_t src_len,
                                               int flush) {
    neverc_ascii85_result_t result = {0, 0, 0};
    uint64_t v = 0;
    int nb = 0;

    if ((!src && src_len != 0) || (!dst && dst_len != 0)) {
        result.error = 1;
        return result;
    }

    for (size_t i = 0; i < src_len; i++) {
        unsigned char b = src[i];

        if (dst_len - result.ndst < 4)
            return result;

        if (b <= ' ')
            continue;

        if (b == 'z' && nb == 0) {
            nb = 5;
            v = 0;
        } else if (b >= '!' && b <= 'u') {
            v = v * 85 + (unsigned int)(b - '!');
            nb++;
        } else {
            result.error = 1;
            return result;
        }

        if (nb == 5) {
            result.nsrc = i + 1;
            if (v > UINT32_MAX) {
                result.error = 1;
                return result;
            }
            dst[result.ndst]     = (unsigned char)(v >> 24);
            dst[result.ndst + 1] = (unsigned char)(v >> 16);
            dst[result.ndst + 2] = (unsigned char)(v >> 8);
            dst[result.ndst + 3] = (unsigned char)(v);
            result.ndst += 4;
            nb = 0;
            v = 0;
        }
    }

    if (flush) {
        result.nsrc = src_len;
        if (nb > 0) {
            if (nb == 1) {
                result.error = 1;
                return result;
            }
            for (int i = nb; i < 5; i++)
                v = v * 85 + 84;
            if (v > UINT32_MAX) {
                result.error = 1;
                return result;
            }
            for (int i = 0; i < nb - 1; i++) {
                dst[result.ndst] = (unsigned char)(v >> 24);
                v <<= 8;
                result.ndst++;
            }
        }
    }

    return result;
}
