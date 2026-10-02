#include "neverc/std/hash/crc64.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_u64(const char *name, uint64_t got, uint64_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got 0x%016llX, expected 0x%016llX\n",
                                   name, (unsigned long long)got, (unsigned long long)expected); }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: expected true\n", name); }
}

static void test_ecma_known(void) {
    printf("[ECMA known values]\n");
    neverc_crc64_table_t table;
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, table);

    check_u64("crc64_ecma('')", neverc_crc64_checksum(table, (const uint8_t *)"", 0), 0);

    uint64_t c = neverc_crc64_checksum(table, (const uint8_t *)"123456789", 9);
    check_u64("crc64_ecma('123456789')", c, 0x995DC9BBDF1939FAULL);

    /* Go hash/crc64 golden */
    check_u64("crc64_ecma('a')",
              neverc_crc64_checksum(table, (const uint8_t *)"a", 1),
              0x330284772E652B05ULL);
    check_u64("crc64_ecma('abc')",
              neverc_crc64_checksum(table, (const uint8_t *)"abc", 3),
              0x2CD8094A1A277627ULL);
    check_u64("crc64_ecma(broadcast)",
              neverc_crc64_checksum(
                  table,
                  (const uint8_t *)"This is a test of the emergency broadcast system.",
                  49),
              0x27DB187FC15BBC72ULL);
}

static void test_iso_known(void) {
    printf("[ISO known values]\n");
    neverc_crc64_table_t table;
    neverc_crc64_make_table(NEVERC_CRC64_ISO, table);

    check_u64("crc64_iso('')", neverc_crc64_checksum(table, (const uint8_t *)"", 0), 0);

    uint64_t c = neverc_crc64_checksum(table, (const uint8_t *)"123456789", 9);
    check_u64("crc64_iso('123456789')", c, 0xB90956C775A41001ULL);

    /* Go hash/crc64 golden */
    check_u64("crc64_iso('a')",
              neverc_crc64_checksum(table, (const uint8_t *)"a", 1),
              0x3420000000000000ULL);
    check_u64("crc64_iso('abc')",
              neverc_crc64_checksum(table, (const uint8_t *)"abc", 3),
              0x3776C42000000000ULL);
    check_u64("crc64_iso(broadcast)",
              neverc_crc64_checksum(
                  table,
                  (const uint8_t *)"This is a test of the emergency broadcast system.",
                  49),
              0xE7FCF1006B503B61ULL);
}

static void test_incremental(void) {
    printf("[incremental]\n");
    neverc_crc64_table_t table;
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, table);

    const char *data = "Hello, World!";
    size_t len = strlen(data);
    uint64_t full = neverc_crc64_checksum(table, (const uint8_t *)data, len);

    for (size_t split = 0; split <= len; split++) {
        uint64_t inc = neverc_crc64_update(0, table, (const uint8_t *)data, split);
        inc = neverc_crc64_update(inc, table, (const uint8_t *)data + split, len - split);
        char buf[64];
        snprintf(buf, sizeof(buf), "inc split@%zu", split);
        check_u64(buf, inc, full);
    }
}

static void test_different_polys(void) {
    printf("[different polynomials]\n");
    neverc_crc64_table_t tab_iso, tab_ecma;
    neverc_crc64_make_table(NEVERC_CRC64_ISO, tab_iso);
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, tab_ecma);

    const char *data = "test data";
    uint64_t c_iso = neverc_crc64_checksum(tab_iso, (const uint8_t *)data, strlen(data));
    uint64_t c_ecma = neverc_crc64_checksum(tab_ecma, (const uint8_t *)data, strlen(data));

    check_true("ISO != ECMA", c_iso != c_ecma);
    check_true("ISO != 0", c_iso != 0);
    check_true("ECMA != 0", c_ecma != 0);

    check_u64("update(NULL,0)",
              neverc_crc64_update(0, tab_ecma, NULL, 0), 0);
    check_u64("update(NULL table)",
              neverc_crc64_update(0xA5A5A5A5A5A5A5A5ULL, NULL,
                                  (const uint8_t *)"x", 1),
              0xA5A5A5A5A5A5A5A5ULL);
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, NULL); /* must not crash */
}

/* Regression: reusing one table buffer for a different polynomial must not
 * return a stale slicing-8 result for len >= 64. */
static void test_table_buffer_reuse(void) {
    printf("[table buffer reuse]\n");
    uint8_t data[128];
    for (int i = 0; i < 128; i++) data[i] = (uint8_t)(i * 7 + 1);

    neverc_crc64_table_t reused;
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, reused);
    (void)neverc_crc64_checksum(reused, data, sizeof data);  /* warm the cache */
    neverc_crc64_make_table(NEVERC_CRC64_ISO, reused);        /* same buffer, new poly */
    uint64_t got = neverc_crc64_checksum(reused, data, sizeof data);

    neverc_crc64_table_t fresh;
    neverc_crc64_make_table(NEVERC_CRC64_ISO, fresh);
    uint64_t want = neverc_crc64_checksum(fresh, data, sizeof data);

    check_u64("reuse buffer (iso after ecma)", got, want);
}

static void test_slicing8_chunked(void) {
    printf("[slicing8 chunked]\n");
    uint8_t data[128];
    for (int i = 0; i < 128; i++) data[i] = (uint8_t)(i * 7 + 1);

    neverc_crc64_table_t iso, ecma;
    neverc_crc64_make_table(NEVERC_CRC64_ISO, iso);
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, ecma);

    uint64_t iso_ref = 0, ecma_ref = 0;
    for (size_t off = 0; off < sizeof data; off += 32) {
        iso_ref = neverc_crc64_update(iso_ref, iso, data + off, 32);
        ecma_ref = neverc_crc64_update(ecma_ref, ecma, data + off, 32);
    }
    check_u64("iso slicing8 vs chunks",
              neverc_crc64_checksum(iso, data, sizeof data), iso_ref);
    check_u64("ecma slicing8 vs chunks",
              neverc_crc64_checksum(ecma, data, sizeof data), ecma_ref);
}

/* Reference CRC computed bit by bit from the polynomial, without tables. */
static uint64_t crc64_bitwise(uint64_t poly, uint64_t crc,
                              const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n-- > 0) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (crc >> 1) ^ poly : crc >> 1;
    }
    return ~crc;
}

/* Two tables that agree on entry 128 (the polynomial) but differ elsewhere
 * must not share a cached slicing-8 table. Both are linear, so slicing-8 and
 * the byte path must agree for each. */
static void test_same_key_different_table(void) {
    printf("[same key, different table]\n");
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)(i * 29 + 3);

    neverc_crc64_table_t base, odd;
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, base);
    for (int i = 0; i < 256; i++)
        odd[i] = base[i] ^ ((i & 1) ? 0x0123456789ABCDEFULL : 0);

    (void)neverc_crc64_checksum(base, data, sizeof data); /* cache base */
    uint64_t want = 0;
    for (size_t off = 0; off < sizeof data; off += 32)
        want = neverc_crc64_update(want, odd, data + off, 32);
    check_u64("odd table after base",
              neverc_crc64_checksum(odd, data, sizeof data), want);
}

/* More distinct polynomials than the slicing-8 table cache holds, revisited
 * round-robin, so that cached tables and tables past a full cache (byte path
 * for short inputs, private slicing-8 tables for long ones) are all checked. */
static void test_many_polys(void) {
    printf("[many polynomials]\n");
    enum { NPOLY = 20 };
    static uint8_t data[4096];
    static neverc_crc64_table_t tabs[NPOLY];
    static const size_t lens[] = {64, 100, 511, 512, 2048, 4096};
    uint64_t polys[NPOLY];
    for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 131u + 7u);
    for (int i = 0; i < NPOLY; i++) {
        polys[i] = 0x8000000000000001ULL ^
                   (0x9E3779B97F4A7C15ULL * (uint64_t)(i + 1));
        neverc_crc64_make_table(polys[i], tabs[i]);
    }
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < NPOLY; i++) {
            for (size_t l = 0; l < sizeof lens / sizeof lens[0]; l++) {
                char name[64];
                snprintf(name, sizeof name, "poly %d len %zu round %d",
                         i, lens[l], round);
                check_u64(name, neverc_crc64_checksum(tabs[i], data, lens[l]),
                          crc64_bitwise(polys[i], 0, data, lens[l]));
            }
            uint64_t chained = neverc_crc64_update(0, tabs[i], data, 1000);
            chained = neverc_crc64_update(chained, tabs[i], data + 1000, 3096);
            check_u64("chained 1000+3096", chained,
                      crc64_bitwise(polys[i], 0, data, 4096));
        }
    }
}

int main(void) {
    printf("=== NeverC CRC-64 Library Tests ===\n\n");
    test_ecma_known();
    test_iso_known();
    test_incremental();
    test_different_polys();
    test_table_buffer_reuse();
    test_slicing8_chunked();
    test_same_key_different_table();
    test_many_polys();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0)
        puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
