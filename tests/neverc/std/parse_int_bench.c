/*
 * A/B benchmark + differential correctness check: strconv ParseUint/ParseInt.
 *
 *  - old_parse_uint / old_parse_int — the previous library parser, reproduced
 *      verbatim: every byte runs a three-way range branch to classify the
 *      digit, a separate "digit >= base" test, and a per-digit overflow check.
 *
 *  - neverc_strconv_parse_uint / _parse_int (library) — the new parser: a
 *      256-entry table classifies each byte with one lookup + one compare, and
 *      base 10 (Atoi and the common case) uses a dedicated loop whose
 *      cutoff/remainder are compile-time constants.
 *
 * The new code is behavior-preserving, so the harness fuzzes a large corpus of
 * inputs across many bases and asserts the new (rc, value) pair is identical to
 * the old one before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/parse_int_bench \
 *      tests/neverc/std/parse_int_bench.c std/src/strconv/parse_int.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "neverc/std/strconv.h"

#define NC_ULLONG_MAX  18446744073709551615ULL
#define NC_LLONG_MAX   9223372036854775807LL
#define NC_LLONG_MIN   (-9223372036854775807LL - 1)

/* ============================================================
 * OLD parser — verbatim reproduction of the previous library
 *
 * Marked noinline so the timing loop measures it as a real (cross-call)
 * library function, exactly like the new parser which lives in another
 * translation unit and cannot be inlined or constant-folded here.
 * ============================================================ */
#define NC_NOINLINE __attribute__((noinline))

static NC_NOINLINE int old_parse_uint(const char *s, int base, unsigned long long *result) {
    if (!s || !result || *s == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;
    if (base != 0 && (base < 2 || base > 36))
        return NEVERC_STRCONV_ERR_BASE;

    const char *p = s;

    if (base == 0) {
        if (p[0] == '0') {
            if (p[1] == 'x' || p[1] == 'X') { base = 16; p += 2; }
            else if (p[1] == 'b' || p[1] == 'B') { base = 2; p += 2; }
            else if (p[1] == 'o' || p[1] == 'O') { base = 8; p += 2; }
            else if (p[1] >= '0' && p[1] <= '7') { base = 8; p += 1; }
            else { base = 10; }
        } else {
            base = 10;
        }
    }

    if (*p == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    unsigned long long cutoff = NC_ULLONG_MAX / (unsigned long long)base;
    unsigned long long val = 0;
    int any = 0;

    for (; *p; p++) {
        if (*p == '_' && any)
            continue;

        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
        else return NEVERC_STRCONV_ERR_SYNTAX;

        if (digit >= base)
            return NEVERC_STRCONV_ERR_SYNTAX;

        if (val > cutoff ||
            (val == cutoff &&
             (unsigned long long)digit > NC_ULLONG_MAX % (unsigned long long)base)) {
            *result = NC_ULLONG_MAX;
            return NEVERC_STRCONV_ERR_RANGE;
        }
        val = val * (unsigned long long)base + (unsigned long long)digit;
        any = 1;
    }

    if (!any)
        return NEVERC_STRCONV_ERR_SYNTAX;

    *result = val;
    return NEVERC_STRCONV_OK;
}

static NC_NOINLINE int old_parse_int(const char *s, int base, long long *result) {
    if (!s || !result || *s == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    const char *p = s;
    int neg = 0;
    if (*p == '+') { p++; }
    else if (*p == '-') { neg = 1; p++; }

    unsigned long long uval;
    int rc = old_parse_uint(p, base, &uval);
    if (rc != NEVERC_STRCONV_OK) { *result = 0; return rc; }

    if (neg) {
        if (uval > (unsigned long long)NC_LLONG_MAX + 1ULL) {
            *result = NC_LLONG_MIN;
            return NEVERC_STRCONV_ERR_RANGE;
        }
        *result = (long long)(0ULL - uval);
    } else {
        if (uval > (unsigned long long)NC_LLONG_MAX) {
            *result = NC_LLONG_MAX;
            return NEVERC_STRCONV_ERR_RANGE;
        }
        *result = (long long)uval;
    }
    return NEVERC_STRCONV_OK;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile unsigned long long sink;

/* ============================================================
 * Differential fuzz: new must equal old on every input/base
 * ============================================================ */
static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static int diff_uint(const char *s, int base) {
    unsigned long long ov = 0xDEAD, nv = 0xBEEF;
    int orc = old_parse_uint(s, base, &ov);
    int nrc = neverc_strconv_parse_uint(s, base, &nv);
    if (orc != nrc) return 0;
    if (orc == NEVERC_STRCONV_OK || orc == NEVERC_STRCONV_ERR_RANGE)
        return ov == nv; /* both set result on RANGE (to MAX) and OK */
    return 1;
}

static int diff_int(const char *s, int base) {
    long long ov = 0xDEAD, nv = 0xBEEF;
    int orc = old_parse_int(s, base, &ov);
    int nrc = neverc_strconv_parse_int(s, base, &nv);
    if (orc != nrc) return 0;
    if (orc == NEVERC_STRCONV_OK || orc == NEVERC_STRCONV_ERR_RANGE)
        return ov == nv;
    return 1;
}

static void fuzz(void) {
    static const char *edge[] = {
        "", "0", "+0", "-0", "00", "007", "0x", "0X", "0b", "0o", "0xff", "0XFF",
        "0b1010", "0o777", "0777", "+", "-", "_", "1_000", "_1", "1_", "1__2",
        "123_456", "0x_1", "0x1_2", "  12", "12 ", "+-1", "ff", "FF", "zZ", "z",
        "9999999999999999999", "18446744073709551615", "18446744073709551616",
        "99999999999999999999", "9223372036854775807", "9223372036854775808",
        "-9223372036854775808", "-9223372036854775809", "4294967295",
        "-4294967296", "g", "G", ":", "/", "@", "[", "`", "{", "1.5", "0x10",
        "-0x10", "+0xFF", "deadBEEF", "11111111111111111111111111111111",
    };
    static const int bases[] = {0, 2, 8, 10, 16, 36};
    int total = 0, ok = 0;

    /* curated edge cases x bases */
    for (size_t i = 0; i < sizeof(edge)/sizeof(edge[0]); i++) {
        for (size_t b = 0; b < sizeof(bases)/sizeof(bases[0]); b++) {
            total += 2;
            ok += diff_uint(edge[i], bases[b]);
            ok += diff_int(edge[i], bases[b]);
        }
    }

    /* random strings from a mixed alphabet, all bases incl. random 2..36 */
    static const char alpha[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_+-: /";
    char buf[40];
    for (int it = 0; it < 4000000; it++) {
        int len = (int)(rng() % 24);
        for (int k = 0; k < len; k++)
            buf[k] = alpha[rng() % (sizeof(alpha) - 1)];
        buf[len] = '\0';

        int base = (int)(rng() % 39) - 1; /* -1..37, exercises invalid bases too */
        total += 2;
        ok += diff_uint(buf, base);
        ok += diff_int(buf, base);
    }

    printf("differential fuzz: %d/%d identical (old vs new)\n", ok, total);
    if (ok != total)
        printf("  *** MISMATCH DETECTED ***\n");
}

/* ============================================================
 * Timing
 *
 * Inputs are routed through compiler barriers so neither the string contents
 * nor the base are constant-folded into either parser; both are measured as
 * opaque-argument calls.
 * ============================================================ */
static const char *opaque_ptr(const char *p) { __asm__ volatile("" : "+r"(p)); return p; }
static int opaque_int(int v) { __asm__ volatile("" : "+r"(v)); return v; }

static void bench_case(const char *label, const char *s, int base) {
    char buf[64];
    size_t n = strlen(s);
    memcpy(buf, s, n + 1);

    long long ov, nv;
    int orc = old_parse_int(opaque_ptr(buf), opaque_int(base), &ov);
    int nrc = neverc_strconv_parse_int(opaque_ptr(buf), opaque_int(base), &nv);
    if (orc != nrc || (orc == NEVERC_STRCONV_OK && ov != nv)) {
        printf("%-22s  CORRECTNESS FAIL (old rc=%d val=%lld / new rc=%d val=%lld)\n",
               label, orc, ov, nrc, nv);
        return;
    }

    const int iters = 20000000;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        long long v;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            old_parse_int(opaque_ptr(buf), opaque_int(base), &v);
            sink += (unsigned long long)v;
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            neverc_strconv_parse_int(opaque_ptr(buf), opaque_int(base), &v);
            sink += (unsigned long long)v;
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx   (rc=%d val=%lld)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, nrc, nv);
}

int main(void) {
    printf("=== strconv ParseInt: table classify + base-10 fast path (new) vs old ===\n\n");

    fuzz();

    printf("\n%-22s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    bench_case("atoi_small",      "12345",                10);
    bench_case("atoi_neg",        "-2147483648",          10);
    bench_case("dec_uint64_max",  "18446744073709551615", 10);
    bench_case("dec_long",        "123456789012345",      10);
    bench_case("dec_underscored", "1_000_000_000",         0);
    bench_case("hex_base16",      "deadbeefcafe1234",     16);
    bench_case("hex_auto",        "0xDEADBEEFCAFE",        0);
    bench_case("base36",          "zyxwvut",              36);
    bench_case("oct_base8",       "1234567012345",         8);

    printf("\n=== Done ===\n");
    return 0;
}
