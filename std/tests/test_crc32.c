#include "neverc/crc32.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_u32(const char *name, uint32_t got, uint32_t expected) {
    tests_run++;
    if (got == expected) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got 0x%08x, expected 0x%08x\n", name, got, expected);
    }
}

static void test_ieee(void) {
    printf("[ieee]\n");

    check_u32("ieee(empty)",
              neverc_crc32_ieee("", 0),
              0x00000000);

    check_u32("ieee(123456789)",
              neverc_crc32_ieee("123456789", 9),
              0xCBF43926);

    check_u32("ieee(a)",
              neverc_crc32_ieee("a", 1),
              0xE8B7BE43);

    check_u32("ieee(abc)",
              neverc_crc32_ieee("abc", 3),
              0x352441C2);
}

static void test_make_table(void) {
    printf("[make_table]\n");
    neverc_crc32_table_t table;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, table);

    check_u32("table[0]", table[0], 0x00000000);
    check_u32("table[1]", table[1], 0x77073096);
    check_u32("table[255]", table[255], 0x2D02EF8D);
}

static void test_update(void) {
    printf("[update]\n");
    neverc_crc32_table_t table;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, table);

    uint32_t crc = neverc_crc32_update(0, table, "1234", 4);
    crc = neverc_crc32_update(crc, table, "56789", 5);
    check_u32("update(chunked)",  crc, 0xCBF43926);

    check_u32("checksum(full)",
              neverc_crc32_checksum(table, "123456789", 9),
              0xCBF43926);
}

static void test_castagnoli(void) {
    printf("[castagnoli]\n");
    neverc_crc32_table_t table;
    neverc_crc32_make_table(NEVERC_CRC32_CASTAGNOLI, table);

    check_u32("castagnoli(123456789)",
              neverc_crc32_checksum(table, "123456789", 9),
              0xE3069283);
}

static void test_zero_data(void) {
    printf("[zero_data]\n");
    uint8_t zeros[16] = {0};
    uint32_t crc = neverc_crc32_ieee(zeros, 16);
    tests_run++;
    if (crc != 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: ieee(16 zero bytes) should not be 0\n");
    }
}

int main(void) {
    printf("=== NeverC CRC32 Library Tests ===\n\n");

    test_ieee();
    test_make_table();
    test_update();
    test_castagnoli();
    test_zero_data();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
