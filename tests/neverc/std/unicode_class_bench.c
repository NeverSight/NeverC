/*
 * A/B benchmark + exhaustive correctness check: unicode classification.
 *
 *  - old_is_letter / old_is_graphic / old_is_print — verbatim reproduction of
 *      the previous library bodies (they call the unchanged is_upper/is_lower/
 *      is_digit/is_punct/is_space/is_control helpers). For ASCII they walk every
 *      Latin-1/Greek/Cyrillic/CJK range or call several sub-classifiers.
 *
 *  - neverc_unicode_is_letter/_is_graphic/_is_print (library) — the new bodies:
 *      an "r < 0x80" ASCII fast path resolves the common case in 1-2 compares.
 *
 * The fast path is behavior-preserving, so the harness checks EVERY rune in
 * 0..0x110000 (old == new) before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/unicode_class_bench \
 *      tests/neverc/std/unicode_class_bench.c std/src/unicode/unicode.c
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "neverc/std/unicode.h"

#define NC_NOINLINE __attribute__((noinline))

/* ============================================================
 * OLD classifiers — verbatim reproduction of previous bodies
 * ============================================================ */
static NC_NOINLINE int old_is_letter(uint32_t r) {
    if (neverc_unicode_is_upper(r) || neverc_unicode_is_lower(r)) return 1;
    if (r == '_') return 0;
    if (r >= 0x4E00 && r <= 0x9FFF) return 1;
    if (r >= 0x3400 && r <= 0x4DBF) return 1;
    if (r >= 0x3040 && r <= 0x309F) return 1;
    if (r >= 0x30A0 && r <= 0x30FF) return 1;
    if (r >= 0xAC00 && r <= 0xD7AF) return 1;
    if (r >= 0x1E00 && r <= 0x1EFF) return 1;
    if (r >= 0x0621 && r <= 0x064A) return 1;
    if (r >= 0x05D0 && r <= 0x05EA) return 1;
    if (r >= 0x0E01 && r <= 0x0E3A) return 1;
    if (r >= 0x0900 && r <= 0x0963) return 1;
    return 0;
}

static NC_NOINLINE int old_is_print(uint32_t r) {
    if (neverc_unicode_is_control(r)) return 0;
    if (r == 0x7F) return 0;
    if (r > NEVERC_UNICODE_MAX_RUNE) return 0;
    if (r >= 0xD800 && r <= 0xDFFF) return 0;
    if (r >= 0xFDD0 && r <= 0xFDEF) return 0;
    if ((r & 0xFFFE) == 0xFFFE) return 0;
    if (r >= 0x20) return 1;
    return 0;
}

static NC_NOINLINE int old_is_graphic(uint32_t r) {
    if (old_is_letter(r)) return 1;
    if (neverc_unicode_is_digit(r)) return 1;
    if (neverc_unicode_is_punct(r)) return 1;
    if (neverc_unicode_is_space(r) && r != ' ' && r != '\t' && r != '\n' &&
        r != '\r' && r != '\v' && r != '\f') return 0;
    if (r >= 0x24 && r <= 0x24) return 1;
    if (r == 0xA2 || r == 0xA3 || r == 0xA4 || r == 0xA5) return 1;
    if (r >= 0x2200 && r <= 0x22FF) return 1;
    return old_is_print(r) && !neverc_unicode_is_control(r);
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile uint64_t sink;
static uint32_t opaque_u32(uint32_t v) { __asm__ volatile("" : "+r"(v)); return v; }

/* ============================================================
 * Exhaustive correctness: every rune, old == new
 * ============================================================ */
static void verify_all(void) {
    long mism = 0, total = 0;
    for (uint32_t r = 0; r <= 0x110000u; r++) {
        total += 3;
        if (old_is_letter(r)  != neverc_unicode_is_letter(r))  { if (mism < 8) printf("  letter  mismatch r=U+%04X\n", r);  mism++; }
        if (old_is_print(r)   != neverc_unicode_is_print(r))   { if (mism < 8) printf("  print   mismatch r=U+%04X\n", r);  mism++; }
        if (old_is_graphic(r) != neverc_unicode_is_graphic(r)) { if (mism < 8) printf("  graphic mismatch r=U+%04X\n", r);  mism++; }
    }
    printf("exhaustive check (every rune 0..0x110000): %ld/%ld identical%s\n",
           total - mism, total, mism ? "  *** MISMATCH ***" : "");
}

/* ============================================================
 * Timing over a realistic mixed-ASCII rune stream
 * ============================================================ */
static void bench_fn(const char *label, int (*oldf)(uint32_t), int (*newf)(uint32_t),
                     const uint32_t *runes, size_t n) {
    const int iters = 60000;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        uint64_t acc = 0;
        double t0 = now_sec();
        for (int it = 0; it < iters; it++)
            for (size_t i = 0; i < n; i++) acc += (uint64_t)oldf(opaque_u32(runes[i]));
        double e = now_sec() - t0; if (e < t_old) t_old = e; sink += acc;

        acc = 0;
        t0 = now_sec();
        for (int it = 0; it < iters; it++)
            for (size_t i = 0; i < n; i++) acc += (uint64_t)newf(opaque_u32(runes[i]));
        e = now_sec() - t0; if (e < t_new) t_new = e; sink += acc;
    }
    printf("%-16s  %8.1f ms  %8.1f ms  %6.2fx   (%zu runes x %d)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, n, iters);
}

int main(void) {
    printf("=== unicode classify: ASCII fast path (new) vs full range checks (old) ===\n\n");

    verify_all();

    /* Representative text: English prose with punctuation, digits and spaces. */
    static const char *sample =
        "The quick brown fox jumps over 13 lazy dogs; pack my box with 5 dozen "
        "liquor jugs! Visit https://example.com/path?q=42 for more (really). "
        "C11, C17 & C23 -- strconv.ParseInt(\"0xFF\", 0) == 255. 100% done.\n";
    static uint32_t runes[1024];
    size_t n = 0;
    for (const char *p = sample; *p && n < 1024; p++)
        runes[n++] = (uint32_t)(unsigned char)*p;

    printf("\n%-16s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    bench_fn("is_letter",  old_is_letter,  neverc_unicode_is_letter,  runes, n);
    bench_fn("is_graphic", old_is_graphic, neverc_unicode_is_graphic, runes, n);
    bench_fn("is_print",   old_is_print,   neverc_unicode_is_print,   runes, n);

    printf("\n=== Done ===\n");
    return 0;
}
