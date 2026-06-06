#include "neverc/crc64.h"
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
}

static void test_iso_known(void) {
    printf("[ISO known values]\n");
    neverc_crc64_table_t table;
    neverc_crc64_make_table(NEVERC_CRC64_ISO, table);

    check_u64("crc64_iso('')", neverc_crc64_checksum(table, (const uint8_t *)"", 0), 0);

    uint64_t c = neverc_crc64_checksum(table, (const uint8_t *)"123456789", 9);
    check_u64("crc64_iso('123456789')", c, 0xB90956C775A41001ULL);
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
}

int main(void) {
    printf("=== NeverC CRC-64 Library Tests ===\n\n");
    test_ecma_known();
    test_iso_known();
    test_incremental();
    test_different_polys();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
