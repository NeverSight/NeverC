#include "neverc/mime/quotedprintable.h"
#include <string.h>

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int neverc_qp_decode(const char *src, size_t src_len,
                     unsigned char *out, size_t out_cap) {
    if (!src || !out) return -1;
    size_t si = 0, di = 0;

    while (si < src_len) {
        if (src[si] == '=') {
            if (si + 2 >= src_len) return -1;
            /* Soft line break: =\r\n or =\n */
            if (src[si+1] == '\r' && si + 2 < src_len && src[si+2] == '\n') {
                si += 3; continue;
            }
            if (src[si+1] == '\n') {
                si += 2; continue;
            }
            int hi = hex_digit(src[si+1]);
            int lo = hex_digit(src[si+2]);
            if (hi < 0 || lo < 0) return -1;
            if (di >= out_cap) return -1;
            out[di++] = (unsigned char)((hi << 4) | lo);
            si += 3;
        } else {
            if (di >= out_cap) return -1;
            out[di++] = (unsigned char)src[si++];
        }
    }
    return (int)di;
}

static const char hex_chars[] = "0123456789ABCDEF";

int neverc_qp_encode(const unsigned char *src, size_t src_len,
                     char *out, size_t out_cap, int max_line) {
    if (!src || !out) return -1;
    if (max_line <= 0) max_line = 76;
    size_t di = 0, line_len = 0;

    for (size_t i = 0; i < src_len; i++) {
        unsigned char c = src[i];
        int need_encode = 0;

        if (c == '\t' || c == ' ') {
            /* Encode trailing whitespace before line end */
            if (i + 1 == src_len || src[i+1] == '\r' || src[i+1] == '\n')
                need_encode = 1;
        } else if (c == '\r' || c == '\n') {
            need_encode = 0; /* pass through line endings */
        } else if (c < 33 || c > 126 || c == '=') {
            need_encode = 1;
        }

        int char_len = need_encode ? 3 : 1;

        /* Line wrapping with soft break */
        if (c != '\r' && c != '\n' && (int)(line_len + (size_t)char_len) > max_line - 1) {
            if (di + 3 > out_cap) return -1;
            out[di++] = '='; out[di++] = '\r'; out[di++] = '\n';
            line_len = 0;
        }

        if (need_encode) {
            if (di + 3 > out_cap) return -1;
            out[di++] = '=';
            out[di++] = hex_chars[c >> 4];
            out[di++] = hex_chars[c & 0x0f];
            line_len += 3;
        } else {
            if (di + 1 > out_cap) return -1;
            out[di++] = (char)c;
            if (c == '\n') line_len = 0;
            else if (c != '\r') line_len++;
        }
    }
    return (int)di;
}

size_t neverc_qp_max_encoded_len(size_t src_len) {
    return src_len * 3 + (src_len / 25) * 3 + 16;
}
