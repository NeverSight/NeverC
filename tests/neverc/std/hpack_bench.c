/*
 * hpack_bench.c — A/B benchmark for HPACK Huffman decode (RFC 7541).
 *
 * Old decode scanned all 256 symbols for every input *bit*:
 * O(in_len * 8 * 256). New decode builds canonical per-length tables once
 * (O(256)) then resolves each bit with an O(1) range test. Both decoders and
 * the RFC tables are reproduced here so the measurement is self-contained.
 *
 * Build (from repo root):
 *   build-neverc/bin/neverc -Istd/include -O2 -fno-builtin-std \
 *     -o /tmp/hpack_bench tests/neverc/std/hpack_bench.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static const uint32_t H[257] = {
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
    0x3fffffff
};
static const uint8_t L[257] = {
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
    30
};

static int hpack_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *ol) {
    uint64_t x = 0; unsigned nb = 0; size_t pos = 0;
    for (size_t i = 0; i < n; i++) {
        x = (x << L[in[i]]) | H[in[i]]; nb += L[in[i]];
        while (nb >= 8) { nb -= 8; if (pos >= cap) return -1; out[pos++] = (uint8_t)(x >> nb); }
    }
    if (nb > 0) { x = (x << (8 - nb)) | ((1u << (8 - nb)) - 1); if (pos >= cap) return -1; out[pos++] = (uint8_t)x; }
    *ol = pos; return 0;
}

__attribute__((noinline))
static int dec_old(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *ol) {
    size_t pos = 0; uint32_t code = 0; uint8_t cl = 0;
    for (size_t i = 0; i < n; i++) for (int b = 7; b >= 0; b--) {
        code = (code << 1) | ((in[i] >> b) & 1); cl++;
        for (int s = 0; s < 256; s++) if (L[s] == cl && H[s] == code) {
            if (pos >= cap) return -1; out[pos++] = (uint8_t)s; code = 0; cl = 0; break;
        }
        if (cl > 30) return -1;
    }
    *ol = pos; return 0;
}

__attribute__((noinline))
static int dec_new(const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *ol) {
    int cnt[31]; uint32_t fc[31]; int bi[31]; uint8_t ss[256];
    for (int l = 0; l <= 30; l++) { cnt[l] = 0; fc[l] = 0xFFFFFFFFu; }
    for (int s = 0; s < 256; s++) { cnt[L[s]]++; if (H[s] < fc[L[s]]) fc[L[s]] = H[s]; }
    int idx = 0; for (int l = 1; l <= 30; l++) { bi[l] = idx; idx += cnt[l]; }
    for (int s = 0; s < 256; s++) ss[bi[L[s]] + (int)(H[s] - fc[L[s]])] = (uint8_t)s;

    size_t pos = 0; uint32_t code = 0; uint8_t cl = 0;
    for (size_t i = 0; i < n; i++) for (int b = 7; b >= 0; b--) {
        code = (code << 1) | ((in[i] >> b) & 1); cl++;
        if (cnt[cl] > 0) { uint32_t off = code - fc[cl];
            if (code >= fc[cl] && off < (uint32_t)cnt[cl]) {
                if (pos >= cap) return -1; out[pos++] = ss[bi[cl] + (int)off]; code = 0; cl = 0; continue;
            }
        }
        if (cl > 30) return -1;
    }
    *ol = pos; return 0;
}

static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }

int main(void) {
    /* realistic header-ish corpus */
    const char *txt =
        "GET /api/v2/users/12345/timeline?include=posts,media&limit=50 HTTP/2 "
        "host: www.example.com user-agent: Mozilla/5.0 (X11; Linux x86_64) "
        "accept: text/html,application/json accept-encoding: gzip, br "
        "cookie: session=abcdef0123456789; theme=dark; lang=en-US ";
    size_t tn = strlen(txt);
    /* repeat to build a larger buffer */
    size_t reps_data = 40;
    uint8_t *raw = malloc(tn * reps_data);
    for (size_t r = 0; r < reps_data; r++) memcpy(raw + r*tn, txt, tn);
    size_t rawlen = tn * reps_data;

    uint8_t *enc = malloc(rawlen * 2); size_t enclen;
    hpack_encode(raw, rawlen, enc, rawlen * 2, &enclen);

    uint8_t *o1 = malloc(rawlen + 16), *o2 = malloc(rawlen + 16);
    size_t n1, n2;

    printf("=== HPACK Huffman decode: old O(n*8*256) vs new (canonical, O(1)/bit) ===\n");
    printf("encoded %zu bytes -> %zu bytes\n", rawlen, enclen);

    int reps_old = 200, reps_new = 200000;
    double t0 = now_ms(); for (int r = 0; r < reps_old; r++) dec_old(enc, enclen, o1, rawlen+16, &n1);
    double t1 = now_ms(); for (int r = 0; r < reps_new; r++) dec_new(enc, enclen, o2, rawlen+16, &n2);
    double t2 = now_ms();
    /* normalise to per-call ms for a fair speedup ratio */
    double old_ms = (t1 - t0) / reps_old, new_ms = (t2 - t1) / reps_new;

    int match = (n1 == n2) && (n1 == rawlen) && memcmp(o1, o2, n1) == 0 && memcmp(o1, raw, n1) == 0;
    printf("%-22s  %12.5f ms/call\n", "old (linear)", old_ms);
    printf("%-22s  %12.5f ms/call\n", "new (canonical)", new_ms);
    printf("%-22s  %11.1fx   %s\n", "speedup", old_ms / new_ms, match ? "OK (roundtrip exact)" : "MISMATCH");

    free(raw); free(enc); free(o1); free(o2);
    return match ? 0 : 1;
}
