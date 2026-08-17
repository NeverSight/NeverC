#include "neverc/std/unicode/utf8.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_size(const char *name, size_t got, size_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %zu, expected %zu\n", name, got, expected); }
}

static void check_u32(const char *name, uint32_t got, uint32_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got U+%04X, expected U+%04X\n", name, got, expected); }
}

static void test_rune_len(void) {
    printf("[rune_len]\n");
    check_int("rune_len('A')", neverc_utf8_rune_len('A'), 1);
    check_int("rune_len(0x7F)", neverc_utf8_rune_len(0x7F), 1);
    check_int("rune_len(0x80)", neverc_utf8_rune_len(0x80), 2);
    check_int("rune_len(0x7FF)", neverc_utf8_rune_len(0x7FF), 2);
    check_int("rune_len(0x800)", neverc_utf8_rune_len(0x800), 3);
    check_int("rune_len(0xFFFF)", neverc_utf8_rune_len(0xFFFF), 3);
    check_int("rune_len(0x10000)", neverc_utf8_rune_len(0x10000), 4);
    check_int("rune_len(0x10FFFF)", neverc_utf8_rune_len(0x10FFFF), 4);
    check_int("rune_len(0x110000)", neverc_utf8_rune_len(0x110000), -1);
    check_int("rune_len(surrogate)", neverc_utf8_rune_len(0xD800), -1);
}

static void test_encode_decode_roundtrip(void) {
    printf("[encode/decode roundtrip]\n");

    uint32_t test_runes[] = {
        'A', 'z', 0x00, 0x7F,
        0x80, 0xFF, 0x7FF,
        0x800, 0x4E16, 0xFFFF,
        0x10000, 0x1F600, 0x10FFFF,
    };
    int n = sizeof(test_runes) / sizeof(test_runes[0]);

    for (int i = 0; i < n; i++) {
        uint32_t r = test_runes[i];
        uint8_t buf[4];
        int enc_len = neverc_utf8_encode_rune(buf, r);

        /* Verify encode returns expected length */
        char name[64];
        snprintf(name, sizeof(name), "encode(U+%04X) len", r);
        check_int(name, enc_len, neverc_utf8_rune_len(r));

        /* Decode back */
        uint32_t decoded; int dec_len;
        neverc_utf8_decode_rune(buf, (size_t)enc_len, &decoded, &dec_len);
        snprintf(name, sizeof(name), "decode(encode(U+%04X)) rune", r);
        check_u32(name, decoded, r);
        snprintf(name, sizeof(name), "decode(encode(U+%04X)) len", r);
        check_int(name, dec_len, enc_len);
    }
}

static void test_decode_specific(void) {
    printf("[decode specific]\n");

    /* ASCII 'H' = 0x48 */
    {
        uint8_t b[] = { 0x48 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 1, &r, &sz);
        check_u32("decode 'H'", r, 'H');
        check_int("decode 'H' size", sz, 1);
    }

    /* U+00E9 (é) = C3 A9 */
    {
        uint8_t b[] = { 0xC3, 0xA9 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 2, &r, &sz);
        check_u32("decode U+00E9", r, 0x00E9);
        check_int("decode U+00E9 size", sz, 2);
    }

    /* U+4E16 (世) = E4 B8 96 */
    {
        uint8_t b[] = { 0xE4, 0xB8, 0x96 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 3, &r, &sz);
        check_u32("decode U+4E16", r, 0x4E16);
        check_int("decode U+4E16 size", sz, 3);
    }

    /* U+1F600 (😀) = F0 9F 98 80 */
    {
        uint8_t b[] = { 0xF0, 0x9F, 0x98, 0x80 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 4, &r, &sz);
        check_u32("decode U+1F600", r, 0x1F600);
        check_int("decode U+1F600 size", sz, 4);
    }

    /* Invalid: continuation byte alone */
    {
        uint8_t b[] = { 0x80 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 1, &r, &sz);
        check_u32("decode invalid 0x80", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("decode invalid 0x80 size", sz, 1);
    }

    /* Invalid: truncated 2-byte */
    {
        uint8_t b[] = { 0xC3 };
        uint32_t r; int sz;
        neverc_utf8_decode_rune(b, 1, &r, &sz);
        check_u32("decode truncated 2b", r, NEVERC_UTF8_RUNE_ERROR);
    }

    /* Empty input */
    {
        uint32_t r; int sz;
        neverc_utf8_decode_rune((const uint8_t *)"", 0, &r, &sz);
        check_u32("decode empty", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("decode empty size", sz, 0);
    }
}

static void test_rune_count(void) {
    printf("[rune_count]\n");
    check_size("count ''", neverc_utf8_rune_count((const uint8_t *)"", 0), 0);
    check_size("count 'Hello'", neverc_utf8_rune_count((const uint8_t *)"Hello", 5), 5);

    /* "世界" = E4B896 E7958C = 6 bytes, 2 runes */
    uint8_t shijie[] = { 0xE4, 0xB8, 0x96, 0xE7, 0x95, 0x8C };
    check_size("count '世界'", neverc_utf8_rune_count(shijie, 6), 2);

    /* "Hello, 世界!" = H,e,l,l,o,',',SP,世,界,! = 10 runes, 14 bytes */
    uint8_t mixed[] = { 'H','e','l','l','o',',',' ',
                        0xE4,0xB8,0x96, 0xE7,0x95,0x8C, '!' };
    check_size("count mixed", neverc_utf8_rune_count(mixed, 14), 10);

    /* Go RuneCount: invalid/short encodings are width-1 error runes. */
    uint8_t overlong[] = { 0xC0, 0x80 };
    check_size("count overlong NUL", neverc_utf8_rune_count(overlong, 2), 2);
    uint8_t trunc[] = { 0xE4, 0xB8 };
    check_size("count truncated 3-byte", neverc_utf8_rune_count(trunc, 2), 2);
    uint8_t surr[] = { 0xED, 0xA0, 0x80 };
    check_size("count UTF-8 surrogate", neverc_utf8_rune_count(surr, 3), 3);
    uint8_t ascii8_u0080[] = { 'a','a','a','a','a','a','a','a', 0xC2, 0x80 };
    check_size("count 8 ASCII + U+0080",
               neverc_utf8_rune_count(ascii8_u0080, 10), 9);
}

static void test_valid(void) {
    printf("[valid]\n");
    check_int("valid ''", neverc_utf8_valid((const uint8_t *)"", 0), 1);
    check_int("valid 'Hello'", neverc_utf8_valid((const uint8_t *)"Hello", 5), 1);

    uint8_t shijie[] = { 0xE4, 0xB8, 0x96, 0xE7, 0x95, 0x8C };
    check_int("valid '世界'", neverc_utf8_valid(shijie, 6), 1);

    uint8_t emoji[] = { 0xF0, 0x9F, 0x98, 0x80 };
    check_int("valid emoji", neverc_utf8_valid(emoji, 4), 1);

    /* Invalid: lone continuation */
    uint8_t inv1[] = { 0x80 };
    check_int("invalid continuation", neverc_utf8_valid(inv1, 1), 0);

    /* Invalid: overlong encoding (C0 80 for U+0000) */
    uint8_t inv2[] = { 0xC0, 0x80 };
    check_int("invalid overlong", neverc_utf8_valid(inv2, 2), 0);

    /* Invalid: truncated */
    uint8_t inv3[] = { 0xE4, 0xB8 };
    check_int("invalid truncated", neverc_utf8_valid(inv3, 2), 0);

    /* Invalid: surrogate half (ED A0 80 = U+D800) */
    uint8_t inv4[] = { 0xED, 0xA0, 0x80 };
    check_int("invalid surrogate", neverc_utf8_valid(inv4, 3), 0);

    /* RFC 3629 / W3C: more sequences that must not be accepted as valid. */
    uint8_t inv5[] = { 0xED, 0xBF, 0xBF }; /* U+DFFF */
    check_int("invalid low surrogate", neverc_utf8_valid(inv5, 3), 0);
    uint8_t inv6[] = { 0xF4, 0x90, 0x80, 0x80 }; /* U+110000 */
    check_int("invalid > U+10FFFF", neverc_utf8_valid(inv6, 4), 0);
    uint8_t inv7[] = { 0xC0, 0xAF }; /* overlong slash */
    check_int("invalid overlong slash", neverc_utf8_valid(inv7, 2), 0);
    uint8_t inv8[] = { 0xE0, 0x80, 0x80 };
    check_int("invalid overlong 3-byte NUL", neverc_utf8_valid(inv8, 3), 0);
    uint8_t inv9[] = { 0xF0, 0x80, 0x80, 0x80 };
    check_int("invalid overlong 4-byte NUL", neverc_utf8_valid(inv9, 4), 0);
    uint8_t inv10[] = { 0xF8, 0x80, 0x80, 0x80, 0x80 };
    check_int("invalid 5-byte lead", neverc_utf8_valid(inv10, 5), 0);
    uint8_t mix[] = { 'A', 0xED, 0xA0, 0x80, 'Z' };
    check_int("invalid surrogate mid-string", neverc_utf8_valid(mix, 5), 0);

    {
        uint32_t r; int sz;
        neverc_utf8_decode_rune(inv4, 3, &r, &sz);
        check_u32("decode surrogate is error", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("decode surrogate size 1", sz, 1);
        neverc_utf8_decode_rune(inv6, 4, &r, &sz);
        check_u32("decode >10FFFF is error", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("decode >10FFFF size 1", sz, 1);
        neverc_utf8_decode_rune(inv2, 2, &r, &sz);
        check_u32("decode overlong is error", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("decode overlong size 1", sz, 1);
    }

    /* RFC 3629 boundaries that Valid must accept (false-negative traps). */
    uint8_t min2[] = { 0xC2, 0x80 };           /* U+0080 */
    uint8_t min3[] = { 0xE0, 0xA0, 0x80 };     /* U+0800 */
    uint8_t pre_surr[] = { 0xED, 0x9F, 0xBF }; /* U+D7FF */
    uint8_t post_surr[] = { 0xEE, 0x80, 0x80 };/* U+E000 */
    uint8_t min4[] = { 0xF0, 0x90, 0x80, 0x80 }; /* U+10000 */
    uint8_t max4[] = { 0xF4, 0x8F, 0xBF, 0xBF }; /* U+10FFFF */
    check_int("valid U+0080", neverc_utf8_valid(min2, 2), 1);
    check_int("valid U+0800", neverc_utf8_valid(min3, 3), 1);
    check_int("valid U+D7FF", neverc_utf8_valid(pre_surr, 3), 1);
    check_int("valid U+E000", neverc_utf8_valid(post_surr, 3), 1);
    check_int("valid U+10000", neverc_utf8_valid(min4, 4), 1);
    check_int("valid U+10FFFF", neverc_utf8_valid(max4, 4), 1);
    uint8_t ascii8_u0080[] = { 'a','a','a','a','a','a','a','a', 0xC2, 0x80 };
    check_int("valid 8 ASCII + U+0080", neverc_utf8_valid(ascii8_u0080, 10), 1);
}

/* Independent oracle: validate rune-by-rune through the (unchanged) public
 * decoder. This is exactly the semantics neverc_utf8_valid must preserve, so the
 * DFA rewrite is pinned byte-for-byte against it. */
static int ref_valid(const uint8_t *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint32_t r; int sz;
        neverc_utf8_decode_rune(buf + i, len - i, &r, &sz);
        if (r == NEVERC_UTF8_RUNE_ERROR && sz <= 1) return 0;
        i += (size_t)sz;
    }
    return 1;
}

static uint64_t u8_rng = 0x9e3779b97f4a7c15ULL;
static uint32_t u8_rand(void) {
    u8_rng ^= u8_rng << 13; u8_rng ^= u8_rng >> 7; u8_rng ^= u8_rng << 17;
    return (uint32_t)(u8_rng >> 32);
}

static void test_valid_fuzz(void) {
    printf("[valid_fuzz]\n");
    u8_rng = 0x9e3779b97f4a7c15ULL;
    static uint8_t buf[1024];
    int mismatches = 0;

    /* (a) Fully random bytes across every alphabet density — heavy on invalid. */
    for (int it = 0; it < 60000 && mismatches == 0; it++) {
        size_t n = u8_rand() % 64;
        int mode = (int)(u8_rand() % 3);
        for (size_t i = 0; i < n; i++) {
            switch (mode) {
            case 0: buf[i] = (uint8_t)u8_rand(); break;             /* any byte */
            case 1: buf[i] = (uint8_t)(0x80 + (u8_rand() % 0x80)); break; /* high only */
            default: buf[i] = (uint8_t)(u8_rand() % 0x90); break;   /* ascii + lead-ish */
            }
        }
        if (neverc_utf8_valid(buf, n) != ref_valid(buf, n)) mismatches++;
    }

    /* (b) Structurally-valid streams (random valid runes) sometimes corrupted by
     *     a single byte flip — exercises both the accept and reject paths deeply,
     *     including the ASCII fast-path boundary between multibyte spans. */
    for (int it = 0; it < 60000 && mismatches == 0; it++) {
        size_t n = 0;
        int runes = (int)(u8_rand() % 40);
        for (int k = 0; k < runes && n + 4 < sizeof(buf); k++) {
            uint32_t r;
            switch (u8_rand() % 5) {
            case 0: r = u8_rand() % 0x80; break;                    /* ASCII */
            case 1: r = 0x80 + u8_rand() % (0x800 - 0x80); break;   /* 2-byte */
            case 2: r = 0x800 + u8_rand() % (0xF000); break;        /* 3-byte (some surrogate range) */
            default: r = 0x10000 + u8_rand() % 0x100000; break;     /* 4-byte */
            }
            if (!neverc_utf8_valid_rune(r)) r = 'a';
            n += (size_t)neverc_utf8_encode_rune(buf + n, r);
        }
        if (n && (u8_rand() & 3) == 0) buf[u8_rand() % n] ^= (uint8_t)(1u << (u8_rand() % 8));
        if (neverc_utf8_valid(buf, n) != ref_valid(buf, n)) mismatches++;
    }

    /* (c) Hand-picked RFC-3629 edge cases. */
    static const struct { const char *bytes; size_t n; } cases[] = {
        {"\xC0\x80", 2}, {"\xC1\xBF", 2},                 /* overlong 2-byte */
        {"\xE0\x80\x80", 3}, {"\xE0\x9F\xBF", 3},         /* overlong 3-byte */
        {"\xF0\x80\x80\x80", 4}, {"\xF0\x8F\xBF\xBF", 4}, /* overlong 4-byte */
        {"\xED\xA0\x80", 3}, {"\xED\xBF\xBF", 3},         /* surrogates */
        {"\xF4\x90\x80\x80", 4}, {"\xF5\x80\x80\x80", 4}, /* > U+10FFFF */
        {"\xEF\xBF\xBD", 3},                              /* valid U+FFFD */
        {"\xF0\x9F\x98\x80", 4},                          /* valid emoji */
        {"\xE4\xB8", 2}, {"\xF0\x9F\x98", 3},             /* truncated tails */
        {"\x80", 1}, {"\xBF", 1}, {"\xFE", 1}, {"\xFF", 1},
    };
    for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); i++) {
        const uint8_t *p = (const uint8_t *)cases[i].bytes;
        if (neverc_utf8_valid(p, cases[i].n) != ref_valid(p, cases[i].n)) mismatches++;
    }

    check_int("valid==oracle over fuzz+edges", mismatches, 0);
}

static void test_rune_start(void) {
    printf("[rune_start]\n");
    check_int("start 'A'", neverc_utf8_rune_start('A'), 1);
    check_int("start 0x00", neverc_utf8_rune_start(0x00), 1);
    check_int("start 0xC0", neverc_utf8_rune_start(0xC0), 1);
    check_int("start 0xE0", neverc_utf8_rune_start(0xE0), 1);
    check_int("start 0xF0", neverc_utf8_rune_start(0xF0), 1);
    check_int("start 0x80 (continuation)", neverc_utf8_rune_start(0x80), 0);
    check_int("start 0xBF (continuation)", neverc_utf8_rune_start(0xBF), 0);
}

static void test_valid_rune(void) {
    printf("[valid_rune]\n");
    check_int("valid_rune 'A'", neverc_utf8_valid_rune('A'), 1);
    check_int("valid_rune 0", neverc_utf8_valid_rune(0), 1);
    check_int("valid_rune 0x10FFFF", neverc_utf8_valid_rune(0x10FFFF), 1);
    check_int("valid_rune 0x110000", neverc_utf8_valid_rune(0x110000), 0);
    check_int("valid_rune 0xD800", neverc_utf8_valid_rune(0xD800), 0);
    check_int("valid_rune 0xDFFF", neverc_utf8_valid_rune(0xDFFF), 0);
}

static void test_encode_surrogate(void) {
    printf("[encode surrogate]\n");
    /* Encoding a surrogate should produce RuneError (U+FFFD) */
    uint8_t buf[4];
    int len = neverc_utf8_encode_rune(buf, 0xD800);
    check_int("encode surrogate len", len, 3);
    uint32_t r; int sz;
    neverc_utf8_decode_rune(buf, (size_t)len, &r, &sz);
    check_u32("encode surrogate -> RuneError", r, NEVERC_UTF8_RUNE_ERROR);
}

static void test_full_rune(void) {
    printf("[full_rune]\n");
    check_int("full empty", neverc_utf8_full_rune((const uint8_t *)"", 0), 0);
    check_int("full ASCII", neverc_utf8_full_rune((const uint8_t *)"a", 1), 1);
    uint8_t cont[] = { 0x80 };
    check_int("full invalid continuation", neverc_utf8_full_rune(cont, 1), 1);
    uint8_t bad_lead[] = { 0xC0 };
    check_int("full invalid C0 starter", neverc_utf8_full_rune(bad_lead, 1), 1);
    uint8_t trunc2[] = { 0xC2 };
    check_int("not full truncated 2-byte", neverc_utf8_full_rune(trunc2, 1), 0);
    uint8_t ok2[] = { 0xC2, 0x80 };
    check_int("full U+0080", neverc_utf8_full_rune(ok2, 2), 1);
    uint8_t trunc3[] = { 0xE0, 0xA0 };
    check_int("not full truncated 3-byte", neverc_utf8_full_rune(trunc3, 2), 0);
    uint8_t bad2[] = { 0xE0, 0x80 };
    check_int("full overlong E0 80 (already invalid)",
              neverc_utf8_full_rune(bad2, 2), 1);
    uint8_t trunc4[] = { 0xF0, 0x90, 0x80 };
    check_int("not full truncated 4-byte", neverc_utf8_full_rune(trunc4, 3), 0);
}

static void test_decode_last_rune(void) {
    printf("[decode_last_rune]\n");

    {
        uint32_t r; int sz;
        neverc_utf8_decode_last_rune((const uint8_t *)"", 0, &r, &sz);
        check_u32("last empty rune", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("last empty size", sz, 0);
    }
    {
        uint8_t b[] = { 'A', 'B' };
        uint32_t r; int sz;
        neverc_utf8_decode_last_rune(b, 2, &r, &sz);
        check_u32("last ASCII", r, 'B');
        check_int("last ASCII size", sz, 1);
    }
    {
        uint8_t b[] = { 'x', 0xE4, 0xB8, 0x96 }; /* x世 */
        uint32_t r; int sz;
        neverc_utf8_decode_last_rune(b, 4, &r, &sz);
        check_u32("last U+4E16", r, 0x4E16);
        check_int("last U+4E16 size", sz, 3);
    }
    {
        uint8_t b[] = { 0xF0, 0x9F, 0x98, 0x80 };
        uint32_t r; int sz;
        neverc_utf8_decode_last_rune(b, 4, &r, &sz);
        check_u32("last emoji", r, 0x1F600);
        check_int("last emoji size", sz, 4);
    }
    {
        uint8_t b[] = { 0xE4, 0xB8 }; /* truncated 世 */
        uint32_t r; int sz;
        neverc_utf8_decode_last_rune(b, 2, &r, &sz);
        check_u32("last truncated", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("last truncated size 1", sz, 1);
    }
    {
        uint8_t b[] = { 0xED, 0xA0, 0x80 }; /* U+D800 in UTF-8 */
        uint32_t r; int sz;
        neverc_utf8_decode_last_rune(b, 3, &r, &sz);
        check_u32("last surrogate", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("last surrogate size 1", sz, 1);
    }
    {
        uint8_t b[] = { 'A', 0x80 };
        uint32_t r; int sz;
        neverc_utf8_decode_last_rune(b, 2, &r, &sz);
        check_u32("last lone continuation", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("last lone continuation size", sz, 1);
    }
    {
        uint8_t b[] = { 0xC0, 0x80 };
        uint32_t r; int sz;
        neverc_utf8_decode_last_rune(b, 2, &r, &sz);
        check_u32("last overlong", r, NEVERC_UTF8_RUNE_ERROR);
        check_int("last overlong size 1", sz, 1);
    }
}

int main(void) {
    printf("=== NeverC UTF-8 Library Tests ===\n\n");

    test_rune_len();
    test_encode_decode_roundtrip();
    test_decode_specific();
    test_rune_count();
    test_valid();
    test_valid_fuzz();
    test_rune_start();
    test_valid_rune();
    test_encode_surrogate();
    test_full_rune();
    test_decode_last_rune();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
