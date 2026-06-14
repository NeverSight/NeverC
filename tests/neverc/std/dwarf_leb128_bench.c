/*
 * A/B benchmark + correctness check: debug/dwarf LEB128 decoding.
 *
 *  - old_uleb128 / old_sleb128 — the previous decoders, reproduced verbatim:
 *    a single loop that accumulates 7 bits per byte and tracks a running shift,
 *    paying that bookkeeping even for the one-byte values that dominate DWARF.
 *
 *  - new_uleb128 / new_sleb128 — the new decoders: a single-byte fast path
 *    (value < 0x80) returns immediately, falling back to the loop only for the
 *    rare multi-byte values. This mirrors the change applied to the library
 *    (std/src/debug/dwarf/dwarf.c); library-level correctness is covered by
 *    tests/neverc/std/test_dwarf.c.
 *
 * Abbreviation tables and DIE attribute lists are almost entirely small ULEB
 * codes/tags/forms, so the one-byte case is the hot one. Each corpus is decoded
 * by both implementations and every value is asserted equal before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/dwarf_leb_bench \
 *      tests/neverc/std/dwarf_leb128_bench.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ============================================================
 * OLD decoders — verbatim reproduction of the previous library
 * ============================================================ */
static uint64_t old_uleb128(const uint8_t **p, const uint8_t *end) {
    uint64_t result = 0;
    int shift = 0;
    while (*p < end) {
        uint8_t b = **p; (*p)++;
        result |= ((uint64_t)(b & 0x7f)) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

static int64_t old_sleb128(const uint8_t **p, const uint8_t *end) {
    int64_t result = 0;
    int shift = 0;
    uint8_t b = 0;
    while (*p < end) {
        b = **p; (*p)++;
        result |= ((int64_t)(b & 0x7f)) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
    }
    if (shift < 64 && (b & 0x40))
        result |= -(((int64_t)1) << shift);
    return result;
}

/* ============================================================
 * NEW decoders — mirror std/src/debug/dwarf/dwarf.c
 * ============================================================ */
static uint64_t new_uleb128(const uint8_t **p, const uint8_t *end) {
    if (*p < end && **p < 0x80)
        return *(*p)++;
    uint64_t result = 0;
    int shift = 0;
    while (*p < end) {
        uint8_t b = **p; (*p)++;
        result |= ((uint64_t)(b & 0x7f)) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

static int64_t new_sleb128(const uint8_t **p, const uint8_t *end) {
    if (*p < end && **p < 0x80) {
        uint8_t b = *(*p)++;
        int64_t result = b;
        if (b & 0x40)
            result |= -(((int64_t)1) << 7);
        return result;
    }
    int64_t result = 0;
    int shift = 0;
    uint8_t b = 0;
    while (*p < end) {
        b = **p; (*p)++;
        result |= ((int64_t)(b & 0x7f)) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
    }
    if (shift < 64 && (b & 0x40))
        result |= -(((int64_t)1) << shift);
    return result;
}

/* ============================================================
 * LEB128 encoders (to build test corpora)
 * ============================================================ */
static size_t enc_uleb(uint8_t *out, uint64_t v) {
    size_t n = 0;
    do {
        uint8_t b = v & 0x7f;
        v >>= 7;
        if (v) b |= 0x80;
        out[n++] = b;
    } while (v);
    return n;
}

static size_t enc_sleb(uint8_t *out, int64_t v) {
    size_t n = 0;
    int more = 1;
    while (more) {
        uint8_t b = v & 0x7f;
        v >>= 7; /* arithmetic shift */
        if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)))
            more = 0;
        else
            b |= 0x80;
        out[n++] = b;
    }
    return n;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile uint64_t sink;

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t xrand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* Build a ULEB corpus where `pct_small` percent of values are < 128 (one byte)
 * and the rest are larger multi-byte values. Returns count of encoded values. */
static size_t build_uleb_corpus(uint8_t *buf, size_t buf_cap, size_t nvals,
                                 int pct_small, size_t *out_bytes) {
    size_t off = 0, count = 0;
    for (size_t i = 0; i < nvals && off + 10 <= buf_cap; i++) {
        uint64_t v;
        if ((int)(xrand() % 100) < pct_small) {
            v = xrand() % 128;                  /* 1 byte */
        } else {
            int extra = 1 + (int)(xrand() % 4); /* 2..5 bytes worth */
            v = xrand() & ((1ULL << (7 * extra + 6)) - 1);
            if (v < 128) v += 128;              /* force multi-byte */
        }
        off += enc_uleb(buf + off, v);
        count++;
    }
    *out_bytes = off;
    return count;
}

static size_t build_sleb_corpus(uint8_t *buf, size_t buf_cap, size_t nvals,
                                 int pct_small, size_t *out_bytes) {
    size_t off = 0, count = 0;
    for (size_t i = 0; i < nvals && off + 10 <= buf_cap; i++) {
        int64_t v;
        if ((int)(xrand() % 100) < pct_small) {
            v = (int64_t)(xrand() % 128) - 64;  /* -64..63, 1 byte */
        } else {
            int extra = 1 + (int)(xrand() % 3);
            v = (int64_t)(xrand() & ((1ULL << (7 * extra)) - 1));
            if (xrand() & 1) v = -v;
            if (v >= -64 && v < 64) v += 1000;  /* force multi-byte */
        }
        off += enc_sleb(buf + off, v);
        count++;
    }
    *out_bytes = off;
    return count;
}

static void bench_uleb(const char *label, const uint8_t *buf, size_t len, size_t nvals) {
    /* correctness: decode whole buffer both ways, compare every value */
    const uint8_t *po = buf, *pn = buf, *end = buf + len;
    size_t mism = 0;
    while (po < end && pn < end) {
        uint64_t a = old_uleb128(&po, end);
        uint64_t b = new_uleb128(&pn, end);
        if (a != b || po != pn) { mism++; break; }
    }
    if (mism || po != pn) { printf("%-22s  CORRECTNESS FAIL\n", label); return; }

    int reps = 200;
    double t_old = 1e30, t_new = 1e30;
    for (int r = 0; r < 5; r++) {
        double t0 = now_sec();
        for (int k = 0; k < reps; k++) {
            const uint8_t *p = buf; uint64_t acc = 0;
            while (p < end) acc += old_uleb128(&p, end);
            sink += acc;
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int k = 0; k < reps; k++) {
            const uint8_t *p = buf; uint64_t acc = 0;
            while (p < end) acc += new_uleb128(&p, end);
            sink += acc;
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx   (%zu vals, %zu B)\n",
           label, t_old*1000, t_new*1000, t_old/t_new, nvals, len);
}

static void bench_sleb(const char *label, const uint8_t *buf, size_t len, size_t nvals) {
    const uint8_t *po = buf, *pn = buf, *end = buf + len;
    size_t mism = 0;
    while (po < end && pn < end) {
        int64_t a = old_sleb128(&po, end);
        int64_t b = new_sleb128(&pn, end);
        if (a != b || po != pn) { mism++; break; }
    }
    if (mism || po != pn) { printf("%-22s  CORRECTNESS FAIL\n", label); return; }

    int reps = 200;
    double t_old = 1e30, t_new = 1e30;
    for (int r = 0; r < 5; r++) {
        double t0 = now_sec();
        for (int k = 0; k < reps; k++) {
            const uint8_t *p = buf; int64_t acc = 0;
            while (p < end) acc += old_sleb128(&p, end);
            sink += (uint64_t)acc;
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int k = 0; k < reps; k++) {
            const uint8_t *p = buf; int64_t acc = 0;
            while (p < end) acc += new_sleb128(&p, end);
            sink += (uint64_t)acc;
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx   (%zu vals, %zu B)\n",
           label, t_old*1000, t_new*1000, t_old/t_new, nvals, len);
}

/* Exhaustive correctness over every 1- and 2-byte encoding. */
static void correctness_exhaustive(void) {
    int ok = 1; long checked = 0;
    uint8_t b2[2];
    for (int x = 0; x < 256 && ok; x++) {
        /* 1-byte */
        b2[0] = (uint8_t)x;
        if ((x & 0x80) == 0) {
            const uint8_t *p1 = b2, *p2 = b2, *e = b2 + 1;
            if (old_uleb128(&p1, e) != new_uleb128(&p2, e) || p1 != p2) ok = 0;
            p1 = p2 = b2;
            if (old_sleb128(&p1, e) != new_sleb128(&p2, e) || p1 != p2) ok = 0;
            checked++;
        }
        /* 2-byte: first has continuation bit */
        for (int y = 0; y < 256 && ok; y++) {
            b2[0] = (uint8_t)(x | 0x80); b2[1] = (uint8_t)y;
            const uint8_t *e = b2 + 2;
            const uint8_t *p1 = b2, *p2 = b2;
            if (old_uleb128(&p1, e) != new_uleb128(&p2, e) || p1 != p2) ok = 0;
            p1 = p2 = b2;
            if (old_sleb128(&p1, e) != new_sleb128(&p2, e) || p1 != p2) ok = 0;
            checked++;
        }
    }
    printf("exhaustive 1/2-byte: %s (%ld encodings)\n", ok ? "all identical" : "FAIL", checked);
}

int main(void) {
    static uint8_t buf[1 << 20];
    size_t len, nvals;

    printf("=== debug/dwarf LEB128: single-byte fast path (new) vs loop (old) ===\n");
    printf("%-22s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    nvals = build_uleb_corpus(buf, sizeof(buf), 120000, 100, &len);
    bench_uleb("uleb all 1-byte", buf, len, nvals);

    nvals = build_uleb_corpus(buf, sizeof(buf), 120000, 90, &len);
    bench_uleb("uleb 90% 1-byte", buf, len, nvals);

    nvals = build_uleb_corpus(buf, sizeof(buf), 120000, 50, &len);
    bench_uleb("uleb 50% 1-byte", buf, len, nvals);

    nvals = build_sleb_corpus(buf, sizeof(buf), 120000, 90, &len);
    bench_sleb("sleb 90% 1-byte", buf, len, nvals);

    printf("\n");
    correctness_exhaustive();
    printf("\n=== Done ===\n");
    return 0;
}
