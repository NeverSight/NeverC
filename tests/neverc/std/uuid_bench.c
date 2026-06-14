/*
 * A/B benchmark + differential correctness check: uuid format / parse.
 *
 *  - old_to_string / old_parse — the previous library routines, reproduced
 *      verbatim. Formatting walked the 16 bytes with a per-byte branch deciding
 *      whether a '-' precedes this byte, then two nibble lookups and two byte
 *      stores. Parsing classified every hex digit through a three-way range
 *      branch (old_hex_val) and rejected on the first bad digit.
 *
 *  - neverc_uuid_to_string / neverc_uuid_parse (library) — the new routines:
 *      formatting writes the four dashes to their fixed slots once and emits
 *      each byte as a single 16-bit store from a 512-byte hex-pair table;
 *      parsing reads the 16 pairs at fixed offsets through a 256-entry reverse
 *      table and rejects with one (acc & 0xf0) test after the loop.
 *
 * The new code is behavior-preserving: both routines only ever return 0/-1 and
 * accept exactly the canonical "8-4-4-4-12" hex form, so the harness fuzzes a
 * large corpus (random UUIDs, case variants, and structural mutations) and
 * asserts the new (rc, bytes/string) result is identical to the old one before
 * timing.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/uuid_bench \
 *      tests/neverc/std/uuid_bench.c std/src/uuid/uuid.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/uuid.h"

/* Timing iteration count; override (e.g. -DBENCH_ITERS=50000) for fast
 * sanitizer runs where only the correctness sweeps matter. */
#ifndef BENCH_ITERS
#define BENCH_ITERS 20000000
#endif

#define NC_NOINLINE __attribute__((noinline))

/* ============================================================
 * OLD routines — verbatim reproduction of the previous library
 *
 * Marked noinline so the timing loop measures them as real cross-call library
 * functions, exactly like the new routines which live in another translation
 * unit and cannot be inlined or constant-folded here.
 * ============================================================ */
static const char old_hex[] = "0123456789abcdef";

static NC_NOINLINE void old_to_string(neverc_uuid_t u, char out[37]) {
    int p = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[p++] = '-';
        out[p++] = old_hex[u.bytes[i] >> 4];
        out[p++] = old_hex[u.bytes[i] & 0x0F];
    }
    out[p] = '\0';
}

static int old_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static NC_NOINLINE int old_parse(const char *s, neverc_uuid_t *out) {
    if (strlen(s) != 36) return -1;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
        return -1;

    int bi = 0;
    for (int i = 0; i < 36; i++) {
        if (s[i] == '-') continue;
        int hi = old_hex_val(s[i]);
        int lo = old_hex_val(s[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out->bytes[bi++] = (uint8_t)((hi << 4) | lo);
        i++;
    }
    return 0;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile unsigned long long sink;

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* ============================================================
 * Differential fuzz: new must equal old on every input
 * ============================================================ */
static int diff_format(neverc_uuid_t u) {
    char o[37], n[37];
    memset(o, 0xAA, sizeof o);
    memset(n, 0x55, sizeof n);
    old_to_string(u, o);
    neverc_uuid_to_string(u, n);
    return memcmp(o, n, 37) == 0;
}

static int diff_parse(const char *s) {
    neverc_uuid_t ou, nu;
    memset(&ou, 0xAA, sizeof ou);
    memset(&nu, 0x55, sizeof nu);
    int orc = old_parse(s, &ou);
    int nrc = neverc_uuid_parse(s, &nu);
    if (orc != nrc) return 0;
    if (orc == 0) return memcmp(&ou, &nu, sizeof ou) == 0;
    return 1; /* both rejected; output is undefined on error */
}

static void mutate_and_check(const char *base, int *total, int *ok) {
    /* the unmodified canonical string */
    *total += 1; *ok += diff_parse(base);

    char buf[64];
    size_t len = strlen(base);

    /* single-character substitutions across the whole string */
    static const char repl[] = "0fF-gG: xX\t\0z9A";
    for (size_t i = 0; i < len; i++) {
        for (size_t r = 0; r < sizeof(repl); r++) {
            memcpy(buf, base, len + 1);
            buf[i] = repl[r];
            *total += 1; *ok += diff_parse(buf);
        }
    }

    /* truncations and one-char extensions exercise the length guard */
    for (size_t i = 0; i <= len; i++) {
        memcpy(buf, base, len + 1);
        buf[i] = '\0';
        *total += 1; *ok += diff_parse(buf);
    }
    memcpy(buf, base, len + 1);
    buf[len] = 'a'; buf[len + 1] = '\0';
    *total += 1; *ok += diff_parse(buf);
    buf[len + 1] = '-'; buf[len + 2] = '\0';
    *total += 1; *ok += diff_parse(buf);
}

static void to_upper_str(char *s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'f') *s = (char)(*s - 'a' + 'A');
}

static void fuzz(void) {
    int total = 0, ok = 0;

    /* random UUIDs: format roundtrip + parse of lower/upper/mixed forms */
    for (int it = 0; it < 2000000; it++) {
        neverc_uuid_t u;
        for (int k = 0; k < 16; k++)
            u.bytes[k] = (uint8_t)(rng() >> ((k & 7) * 8));

        total += 1; ok += diff_format(u);

        char s[37];
        neverc_uuid_to_string(u, s);
        total += 1; ok += diff_parse(s);

        char up[37];
        memcpy(up, s, 37); to_upper_str(up);
        total += 1; ok += diff_parse(up);

        /* mixed case: upper-case every other hex digit */
        char mix[37];
        memcpy(mix, s, 37);
        for (int k = 0; k < 36; k += 2)
            if (mix[k] >= 'a' && mix[k] <= 'f') mix[k] = (char)(mix[k] - 32);
        total += 1; ok += diff_parse(mix);
    }

    /* structural mutations around representative fixed strings */
    mutate_and_check("550e8400-e29b-41d4-a716-446655440000", &total, &ok);
    mutate_and_check("00000000-0000-0000-0000-000000000000", &total, &ok);
    mutate_and_check("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF", &total, &ok);
    mutate_and_check("ffffffff-ffff-ffff-ffff-ffffffffffff", &total, &ok);

    /* curated odd-shaped inputs */
    static const char *edge[] = {
        "", "-", "x", "invalid-uuid",
        "550e8400e29b41d4a716446655440000",
        "550e8400-e29b-41d4-a716-44665544000",
        "550e8400-e29b-41d4-a716-4466554400000",
        "550e8400-e29b-41d4-a716_446655440000",
        "-50e8400-e29b-41d4-a716-44665544000-",
        "550e8400-e29b-41d4-a716-44665544zzzz",
        "GGGGGGGG-GGGG-GGGG-GGGG-GGGGGGGGGGGG",
    };
    for (size_t i = 0; i < sizeof(edge)/sizeof(edge[0]); i++) {
        total += 1; ok += diff_parse(edge[i]);
    }

    printf("differential fuzz: %d/%d identical (old vs new)\n", ok, total);
    if (ok != total)
        printf("  *** MISMATCH DETECTED ***\n");
}

/* ============================================================
 * Timing
 * ============================================================ */
static const char *opaque_ptr(const char *p) { __asm__ volatile("" : "+r"(p)); return p; }

static neverc_uuid_t g_uuid;
static void touch(void *p) { __asm__ volatile("" : : "r"(p) : "memory"); }

static void bench_format(void) {
    char buf[37];
    const int iters = BENCH_ITERS;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            touch(&g_uuid);
            old_to_string(g_uuid, buf);
            sink += (unsigned char)buf[i & 31];
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            touch(&g_uuid);
            neverc_uuid_to_string(g_uuid, buf);
            sink += (unsigned char)buf[i & 31];
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-16s  %8.1f ms  %8.1f ms  %6.2fx\n",
           "format", t_old * 1000, t_new * 1000, t_old / t_new);
}

static void bench_parse(const char *label, const char *s) {
    char buf[64];
    size_t n = strlen(s);
    memcpy(buf, s, n + 1);

    neverc_uuid_t ou, nu;
    int orc = old_parse(opaque_ptr(buf), &ou);
    int nrc = neverc_uuid_parse(opaque_ptr(buf), &nu);
    if (orc != nrc || (orc == 0 && memcmp(&ou, &nu, sizeof ou) != 0)) {
        printf("%-16s  CORRECTNESS FAIL (old rc=%d / new rc=%d)\n", label, orc, nrc);
        return;
    }

    const int iters = BENCH_ITERS;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        neverc_uuid_t v;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            old_parse(opaque_ptr(buf), &v);
            sink += v.bytes[0];
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            neverc_uuid_parse(opaque_ptr(buf), &v);
            sink += v.bytes[0];
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-16s  %8.1f ms  %8.1f ms  %6.2fx   (rc=%d)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, nrc);
}

int main(void) {
    printf("=== uuid: hex-pair table format + table-decode parse (new) vs old ===\n\n");

    fuzz();

    /* seed the formatting input with a non-trivial value */
    (void)neverc_uuid_parse("550e8400-e29b-41d4-a716-446655440000", &g_uuid);

    printf("\n%-16s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    bench_format();
    bench_parse("parse_lower",   "550e8400-e29b-41d4-a716-446655440000");
    bench_parse("parse_upper",   "550E8400-E29B-41D4-A716-446655440000");
    bench_parse("parse_invalid", "550e8400-e29b-41d4-a716-44665544zzzz");

    printf("\n=== Done ===\n");
    return 0;
}
