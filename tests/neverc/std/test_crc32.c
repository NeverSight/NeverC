#include "neverc/std/hash/crc32.h"
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

    uint8_t long_data[80];
    for (int i = 0; i < 80; i++)
        long_data[i] = (uint8_t)i;
    uint32_t full = neverc_crc32_checksum(table, long_data, 80);
    uint32_t part = neverc_crc32_update(0, table, long_data, 40);
    part = neverc_crc32_update(part, table, long_data + 40, 40);
    check_u32("slicing8 80 vs 40+40", part, full);
}

static void test_castagnoli(void) {
    printf("[castagnoli]\n");
    neverc_crc32_table_t table;
    neverc_crc32_make_table(NEVERC_CRC32_CASTAGNOLI, table);

    check_u32("castagnoli(123456789)",
              neverc_crc32_checksum(table, "123456789", 9),
              0xE3069283);
}

static void test_koopman(void) {
    printf("[koopman]\n");
    neverc_crc32_table_t table;
    neverc_crc32_make_table(NEVERC_CRC32_KOOPMAN, table);

    check_u32("koopman(123456789)",
              neverc_crc32_checksum(table, "123456789", 9),
              0x2D3DD0AE);
}

static void test_null_empty(void) {
    printf("[null_empty]\n");
    neverc_crc32_table_t table;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, table);
    check_u32("ieee(NULL,0)", neverc_crc32_ieee(NULL, 0), 0);
    check_u32("update(NULL,0)", neverc_crc32_update(0, table, NULL, 0), 0);
    check_u32("update(NULL table)", neverc_crc32_update(0xA5A5A5A5u, NULL, "x", 1),
              0xA5A5A5A5u);
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, NULL); /* must not crash */
}

/* Regression: reusing one table buffer for a different polynomial must not
 * return a stale slicing-8 result for len >= 64. */
static void test_table_buffer_reuse(void) {
    printf("[table_buffer_reuse]\n");
    uint8_t data[128];
    for (int i = 0; i < 128; i++) data[i] = (uint8_t)(i * 7 + 1);

    neverc_crc32_table_t reused;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, reused);
    (void)neverc_crc32_checksum(reused, data, sizeof data);  /* warm the cache */
    neverc_crc32_make_table(NEVERC_CRC32_CASTAGNOLI, reused); /* same buffer, new poly */
    uint32_t got = neverc_crc32_checksum(reused, data, sizeof data);

    neverc_crc32_table_t fresh;
    neverc_crc32_make_table(NEVERC_CRC32_CASTAGNOLI, fresh);
    uint32_t want = neverc_crc32_checksum(fresh, data, sizeof data);

    check_u32("reuse buffer (castagnoli after ieee)", got, want);
}

static void test_slicing8_other_polys(void) {
    printf("[slicing8 other polys]\n");
    uint8_t data[128];
    for (int i = 0; i < 128; i++) data[i] = (uint8_t)(i * 7 + 1);

    neverc_crc32_table_t cast, koop;
    neverc_crc32_make_table(NEVERC_CRC32_CASTAGNOLI, cast);
    neverc_crc32_make_table(NEVERC_CRC32_KOOPMAN, koop);

    /* 32-byte chunks stay on the byte path; 128 bytes hits slicing-8. */
    uint32_t cast_ref = 0, koop_ref = 0;
    for (size_t off = 0; off < sizeof data; off += 32) {
        cast_ref = neverc_crc32_update(cast_ref, cast, data + off, 32);
        koop_ref = neverc_crc32_update(koop_ref, koop, data + off, 32);
    }
    check_u32("castagnoli slicing8 vs chunks",
              neverc_crc32_checksum(cast, data, sizeof data), cast_ref);
    check_u32("koopman slicing8 vs chunks",
              neverc_crc32_checksum(koop, data, sizeof data), koop_ref);
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
    test_koopman();
    test_null_empty();
    test_table_buffer_reuse();
    test_slicing8_other_polys();
    test_zero_data();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0)
        puts("passed");

    return tests_failed > 0 ? 1 : 0;
}
