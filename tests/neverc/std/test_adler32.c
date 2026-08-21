#include "neverc/std/hash/adler32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_u32(const char *name, uint32_t got, uint32_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got 0x%08X, expected 0x%08X\n", name, got, expected); }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: expected true\n", name); }
}

static void test_known_values(void) {
    printf("[known values]\n");

    /* Go's adler32 test golden values */
    /* adler32("") = 1 (the initial value) */
    check_u32("adler32('')", neverc_adler32_checksum((const uint8_t *)"", 0), 1);

    /* adler32("a") = 0x00620062 */
    check_u32("adler32('a')",
        neverc_adler32_checksum((const uint8_t *)"a", 1), 0x00620062U);

    /* adler32("abc") = 0x024d0127 */
    check_u32("adler32('abc')",
        neverc_adler32_checksum((const uint8_t *)"abc", 3), 0x024d0127U);

    /* adler32("message digest") */
    check_u32("adler32('message digest')",
        neverc_adler32_checksum((const uint8_t *)"message digest", 14), 0x29750586U);

    /* adler32("abcdefghijklmnopqrstuvwxyz") */
    check_u32("adler32(a-z)",
        neverc_adler32_checksum((const uint8_t *)"abcdefghijklmnopqrstuvwxyz", 26), 0x90860B20U);

    /* Go hash/adler32 golden */
    check_u32("adler32('ab')",
        neverc_adler32_checksum((const uint8_t *)"ab", 2), 0x012600C4U);
    check_u32("adler32('Wikipedia')",
        neverc_adler32_checksum((const uint8_t *)"Wikipedia", 9), 0x11E60398U);
}

static void test_incremental(void) {
    printf("[incremental]\n");

    /* Verify: update in chunks = checksum of full data */
    const char *data = "Hello, World! This is a test of incremental Adler-32.";
    size_t len = strlen(data);
    uint32_t full = neverc_adler32_checksum((const uint8_t *)data, len);

    /* Split at various points and verify */
    for (size_t split = 0; split <= len; split++) {
        uint32_t inc = neverc_adler32_update(NEVERC_ADLER32_INIT,
            (const uint8_t *)data, split);
        inc = neverc_adler32_update(inc, (const uint8_t *)data + split, len - split);
        char buf[64];
        snprintf(buf, sizeof(buf), "inc split@%zu", split);
        check_u32(buf, inc, full);
    }
}

static void test_large_data(void) {
    printf("[large data]\n");

    /* Generate a pattern and verify it doesn't overflow or produce zero */
    uint8_t pattern[10000];
    for (int i = 0; i < 10000; i++)
        pattern[i] = (uint8_t)(i & 0xFF);

    uint32_t cksum = neverc_adler32_checksum(pattern, 10000);
    check_true("adler32(10K) != 0", cksum != 0);
    check_true("adler32(10K) != 1", cksum != 1);

    /* s1 and s2 should both be in [0, 65520] */
    uint32_t s1 = cksum & 0xFFFF;
    uint32_t s2 = cksum >> 16;
    check_true("adler32(10K) s1 < 65521", s1 < 65521);
    check_true("adler32(10K) s2 < 65521", s2 < 65521);
}

static void test_all_zeros(void) {
    printf("[all zeros]\n");

    uint8_t zeros[256];
    memset(zeros, 0, 256);

    /* adler32 of N zero bytes: s1 = 1, s2 = N */
    check_u32("adler32(1 zero)", neverc_adler32_checksum(zeros, 1),
        (1U << 16) | 1U);
    check_u32("adler32(10 zeros)", neverc_adler32_checksum(zeros, 10),
        (10U << 16) | 1U);
    check_u32("adler32(100 zeros)", neverc_adler32_checksum(zeros, 100),
        (100U << 16) | 1U);
}

static void test_nmax_go_golden(void) {
    printf("[nmax go golden]\n");
    /* Go hash/adler32: nmax=5552 is the largest n that cannot overflow
     * uint32 before the modulo. These vectors sit on either side of that
     * bound (Repeat("\xff", n) + digit). */
    static const struct {
        int n_ff;
        char last;
        uint32_t want;
    } cases[] = {
        {5548, '8', 0x211297C8U},
        {5549, '9', 0xBAA198C8U},
        {5550, '0', 0x553499BEU},
        {5551, '1', 0xF0C19ABEU},
        {5552, '2', 0x8D5C9BBEU},
        {5553, '3', 0x2AF69CBEU},
        {5554, '4', 0xC9809DBEU},
        {5555, '5', 0x69189EBEU},
    };
    uint8_t buf[5556];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int n = cases[i].n_ff;
        memset(buf, 0xff, (size_t)n);
        buf[n] = (uint8_t)cases[i].last;
        char name[64];
        snprintf(name, sizeof(name), "adler32(0xff*%d+'%c')", n, cases[i].last);
        check_u32(name, neverc_adler32_checksum(buf, (size_t)n + 1), cases[i].want);
    }

    /* Go: Checksum of 1e5 zero bytes and 1e5 'a's. */
    uint8_t *big = (uint8_t *)malloc(100000);
    if (!big) {
        tests_run++;
        tests_failed++;
        printf("  FAIL: malloc 100000\n");
        return;
    }
    memset(big, 0, 100000);
    check_u32("adler32(1e5 zeros)", neverc_adler32_checksum(big, 100000), 0x86AF0001U);
    memset(big, 'a', 100000);
    check_u32("adler32(1e5 a)", neverc_adler32_checksum(big, 100000), 0x79660B4DU);
    /* Streaming across the nmax boundary must match one-shot. */
    uint32_t inc = neverc_adler32_update(NEVERC_ADLER32_INIT, big, 5552);
    inc = neverc_adler32_update(inc, big + 5552, 100000 - 5552);
    check_u32("adler32(1e5 a) 5552+rest", inc, 0x79660B4DU);
    free(big);
}

static void test_null_empty(void) {
    printf("[null empty]\n");
    check_u32("update(NULL,0)",
              neverc_adler32_update(NEVERC_ADLER32_INIT, NULL, 0),
              NEVERC_ADLER32_INIT);
}

int main(void) {
    printf("=== NeverC Adler-32 Library Tests ===\n\n");

    test_known_values();
    test_incremental();
    test_large_data();
    test_all_zeros();
    test_nmax_go_golden();
    test_null_empty();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0)
        puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
