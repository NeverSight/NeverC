#include "neverc/std/mime/quotedprintable.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

/* Hex value per byte, -1 for non-hex, so the =XX hot loop avoids the branchy
 * hex_digit(). A compile-time constant table keeps the decoder reentrant with
 * no lazy-init data race (a lazily built table can be seen half-initialized by
 * another thread on weakly-ordered targets such as arm64). */
static const signed char qp_hex_val[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
     0, 1, 2, 3, 4, 5, 6, 7,  8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
};

/* RFC 2045 line ending for soft breaks / trailing WSP: LF, CRLF, or a
 * CR only at EOF. A bare CR in the middle of a line is not a break, so
 * `=\rX` is not a soft line break (Go quotedprintable: issue 13219). */
static int qp_is_line_end(const char *src, size_t src_len, size_t j) {
    if (j >= src_len) return 1;
    if (src[j] == '\n') return 1;
    if (src[j] == '\r')
        return j + 1 >= src_len || src[j + 1] == '\n';
    return 0;
}

static int qp_need(size_t di, size_t n, size_t cap) {
    return di > cap || n > cap - di;
}

int neverc_qp_decode(const char *src, size_t src_len,
                     unsigned char *out, size_t out_cap) {
    if (!src || !out) return -1;
    size_t si = 0, di = 0;

    while (si < src_len) {
        unsigned char c = (unsigned char)src[si];

        if (c == '=') {
            if (si + 2 < src_len) {
                int hi = qp_hex_val[(unsigned char)src[si + 1]];
                int lo = qp_hex_val[(unsigned char)src[si + 2]];
                if (hi >= 0 && lo >= 0) {
                    if (di >= out_cap) return -1;
                    out[di++] = (unsigned char)((hi << 4) | lo);
                    si += 3;
                    continue;
                }
            }
            /* Soft break: '=' WSP* (CRLF | LF | CR-at-EOF | EOF). Transport
             * may insert spaces between '=' and the line ending (RFC 2045 6.7). */
            size_t j = si + 1;
            while (j < src_len && (src[j] == ' ' || src[j] == '\t'))
                j++;
            if (!qp_is_line_end(src, src_len, j))
                return -1;
            if (j >= src_len) {
                si = j;
                continue;
            }
            if (src[j] == '\n') {
                si = j + 1;
                continue;
            }
            si = j + 1;
            if (si < src_len && src[si] == '\n')
                si++;
            continue;
        }

        if (c == ' ' || c == '\t') {
            size_t j = si;
            while (j < src_len && (src[j] == ' ' || src[j] == '\t'))
                j++;
            /* Trailing WSP on a line is transport padding and must be dropped. */
            if (qp_is_line_end(src, src_len, j)) {
                si = j;
                continue;
            }
            if (qp_need(di, j - si, out_cap)) return -1;
            memcpy(out + di, src + si, j - si);
            di += j - si;
            si = j;
            continue;
        }

        {
            size_t j = si + 1;
            while (j < src_len) {
                unsigned char u = (unsigned char)src[j];
                if (u == '=' || u == ' ' || u == '\t')
                    break;
                j++;
            }
            if (qp_need(di, j - si, out_cap)) return -1;
            memcpy(out + di, src + si, j - si);
            di += j - si;
            si = j;
        }
    }
    if (di > (size_t)INT_MAX) return -1;
    return (int)di;
}

static const char hex_chars[] = "0123456789ABCDEF";

/* Bytes that QP copies verbatim as one output char in the common case:
 * printable ASCII except '=', plus space and tab. (Space/tab still need
 * =XX-encoding when they are the last byte before a line break or EOF; that
 * single position is excluded from a bulk run below.) A 256-entry table keeps
 * the per-byte test in the scan to a single load, so escape-dense input — where
 * most bytes are not literal — pays almost nothing for the fast-path probe.
 * Compile-time constant: immutable and shared, so the encoder is reentrant with
 * no lazy-init data race on weakly-ordered targets (e.g. arm64). Generated as
 * ((c>=33 && c<=126 && c!='=') || c==' ' || c=='\t'). */
static const unsigned char qp_literal_tab[256] = {
    0,0,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,0,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
};

/* Per-chunk threshold: copy with memcpy once a chunk reaches this length, and
 * with a tiny inline loop below it, so a few-byte literal run never pays the
 * call overhead of memcpy. */
#define QP_BULK_MIN 8

int neverc_qp_encode(const unsigned char *src, size_t src_len,
                     char *out, size_t out_cap, int max_line) {
    if (!src || !out) return -1;
    /* max_line < 0 → RFC default 76; 0 → no wrapping (documented). */
    int wrap = 1;
    if (max_line < 0) max_line = 76;
    if (max_line == 0) wrap = 0;
    size_t di = 0, line_len = 0;
    const size_t line_cap = wrap ? (size_t)(max_line - 1) : SIZE_MAX;

    size_t i = 0;
    while (i < src_len) {
        unsigned char c = src[i];

        /* Classify exactly as the original routine, and emit the non-literal
         * cases here. Doing this first means escape-dense input — mostly bytes
         * that need =XX-encoding — keeps the original per-byte cost: the
         * literal-run fast path is reached only after a byte is confirmed to be
         * an ordinary one-char literal. */
        int need_encode = 0;
        if (c == '\t' || c == ' ') {
            /* Trailing whitespace before a line end must be encoded.
             * Also encode WSP that would sit in the last column of a
             * wrapped line: the next byte's soft break would otherwise
             * leave ` \r\n`, which RFC 2045 decoders may strip. */
            if (i + 1 == src_len || src[i+1] == '\r' || src[i+1] == '\n')
                need_encode = 1;
            else if (wrap && line_len + 1 >= line_cap)
                need_encode = 1;
        } else if (c == '\r' || c == '\n') {
            need_encode = 0; /* pass through line endings */
        } else if (c < 33 || c > 126 || c == '=') {
            need_encode = 1;
        }

        if (need_encode) {                 /* c is never '\r'/'\n' here */
            if (wrap && (int)(line_len + 3) > max_line - 1) {
                if (qp_need(di, 3, out_cap)) return -1;
                out[di++] = '='; out[di++] = '\r'; out[di++] = '\n';
                line_len = 0;
            }
            if (qp_need(di, 3, out_cap)) return -1;
            out[di++] = '=';
            out[di++] = hex_chars[c >> 4];
            out[di++] = hex_chars[c & 0x0f];
            line_len += 3;
            i++;
            continue;
        }

        if (c == '\r' || c == '\n') {      /* passed through, no wrapping */
            if (qp_need(di, 1, out_cap)) return -1;
            out[di++] = (char)c;
            if (c == '\n') line_len = 0;
            i++;
            continue;
        }

        /* c is an ordinary literal (one output char, increments line_len). If
         * the next byte is literal too, bulk-copy the whole run, splitting it at
         * the wrap column with soft breaks; otherwise emit the single byte. The
         * lookahead keeps isolated literals (escape-dense data) on the cheap
         * single-byte path. */
        if (line_cap >= 1 && i + 1 < src_len && qp_literal_tab[src[i+1]]) {
            size_t j = i + 2;
            while (j < src_len && qp_literal_tab[src[j]]) j++;
            /* Trailing whitespace right before a line break / EOF must be
             * =XX-encoded, so exclude that one byte from the run. */
            size_t end = j;
            if ((src[end-1] == ' ' || src[end-1] == '\t') &&
                (end == src_len || src[end] == '\r' || src[end] == '\n'))
                end--;
            size_t run = end - i;                 /* >= 1: src[i] is literal */
            while (run > 0) {
                if (wrap && line_len >= line_cap) { /* full line -> soft break */
                    if (qp_need(di, 3, out_cap)) return -1;
                    out[di++] = '='; out[di++] = '\r'; out[di++] = '\n';
                    line_len = 0;
                }
                if (wrap && line_len >= line_cap)
                    return -1;
                size_t budget = wrap ? (line_cap - line_len) : run;
                size_t chunk = run < budget ? run : budget;
                if (chunk == 0 || i + chunk > src_len)
                    break;
                /* A WSP in the last column would become ` \r\n` after the
                 * next soft break. Leave that byte for the per-byte path. */
                if (wrap && chunk == budget &&
                    (src[i + chunk - 1] == ' ' || src[i + chunk - 1] == '\t')) {
                    if (chunk == 1)
                        break;
                    chunk--;
                }
                if (qp_need(di, chunk, out_cap)) return -1;
                if (chunk >= QP_BULK_MIN) {
                    memcpy(out + di, src + i, chunk);
                } else {
                    for (size_t k = 0; k < chunk; k++) out[di+k] = (char)src[i+k];
                }
                di += chunk; i += chunk;
                line_len += chunk; run -= chunk;
            }
            continue;
        }

        /* Single literal byte. */
        if (wrap && (int)(line_len + 1) > max_line - 1) {
            if (qp_need(di, 3, out_cap)) return -1;
            out[di++] = '='; out[di++] = '\r'; out[di++] = '\n';
            line_len = 0;
        }
        if (qp_need(di, 1, out_cap)) return -1;
        out[di++] = (char)c;
        line_len++;
        i++;
    }
    if (di > (size_t)INT_MAX) return -1;
    return (int)di;
}

size_t neverc_qp_max_encoded_len(size_t src_len) {
    if (src_len > (SIZE_MAX - 16U) / 4U) return SIZE_MAX;
    return src_len * 3U + (src_len / 25U) * 3U + 16U;
}
