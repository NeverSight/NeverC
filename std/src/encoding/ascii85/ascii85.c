#include "neverc/std/encoding/ascii85.h"

int neverc_ascii85_max_encoded_len(int n) {
    return (n + 3) / 4 * 5;
}

int neverc_ascii85_encode(unsigned char *dst, const unsigned char *src, size_t src_len) {
    if (src_len == 0) return 0;

    int n = 0;
    size_t off = 0;
    while (off < src_len) {
        unsigned int v = 0;
        size_t remain = src_len - off;

        switch (remain >= 4 ? 4 : remain) {
        case 4: v |= (unsigned int)src[off + 3]; /* fall through */
        case 3: v |= (unsigned int)src[off + 2] << 8;  /* fall through */
        case 2: v |= (unsigned int)src[off + 1] << 16; /* fall through */
        case 1: v |= (unsigned int)src[off + 0] << 24; break;
        }

        if (v == 0 && remain >= 4) {
            dst[n++] = 'z';
            off += 4;
            continue;
        }

        unsigned char tmp[5];
        for (int i = 4; i >= 0; i--) {
            tmp[i] = '!' + (unsigned char)(v % 85);
            v /= 85;
        }

        int m = 5;
        if (remain < 4)
            m = (int)remain + 1;

        for (int i = 0; i < m; i++)
            dst[n++] = tmp[i];

        if (remain < 4) break;
        off += 4;
    }
    return n;
}

neverc_ascii85_result_t neverc_ascii85_decode(unsigned char *dst, size_t dst_len,
                                               const unsigned char *src, size_t src_len,
                                               int flush) {
    neverc_ascii85_result_t result = {0, 0, 0};
    unsigned int v = 0;
    int nb = 0;

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
            for (int i = 0; i < nb - 1; i++) {
                dst[result.ndst] = (unsigned char)(v >> 24);
                v <<= 8;
                result.ndst++;
            }
        }
    }

    return result;
}
