#include "neverc/std/encoding/binary.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_u16(const char *name, uint16_t got, uint16_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got 0x%04X, expected 0x%04X\n", name, got, expected); }
}

static void check_u32(const char *name, uint32_t got, uint32_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got 0x%08X, expected 0x%08X\n", name, got, expected); }
}

static void check_u64(const char *name, uint64_t got, uint64_t expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got 0x%016llX, expected 0x%016llX\n",
                                   name, (unsigned long long)got, (unsigned long long)expected); }
}

static void check_bytes(const char *name, const uint8_t *got, const uint8_t *expected, int n) {
    tests_run++;
    if (memcmp(got, expected, (size_t)n) == 0) { tests_passed++; }
    else {
        tests_failed++;
        printf("  FAIL: %s: bytes differ\n", name);
    }
}

static void test_big_endian_read(void) {
    printf("[big endian read]\n");
    /* 0x0102 */
    uint8_t b2[] = { 0x01, 0x02 };
    check_u16("be_u16", neverc_binary_big_endian_uint16(b2), 0x0102);

    /* 0x01020304 */
    uint8_t b4[] = { 0x01, 0x02, 0x03, 0x04 };
    check_u32("be_u32", neverc_binary_big_endian_uint32(b4), 0x01020304U);

    /* 0x0102030405060708 */
    uint8_t b8[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    check_u64("be_u64", neverc_binary_big_endian_uint64(b8), 0x0102030405060708ULL);
}

static void test_little_endian_read(void) {
    printf("[little endian read]\n");
    uint8_t b2[] = { 0x01, 0x02 };
    check_u16("le_u16", neverc_binary_little_endian_uint16(b2), 0x0201);

    uint8_t b4[] = { 0x01, 0x02, 0x03, 0x04 };
    check_u32("le_u32", neverc_binary_little_endian_uint32(b4), 0x04030201U);

    uint8_t b8[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    check_u64("le_u64", neverc_binary_little_endian_uint64(b8), 0x0807060504030201ULL);
}

static void test_big_endian_write(void) {
    printf("[big endian write]\n");
    uint8_t buf[8];

    neverc_binary_big_endian_put_uint16(buf, 0xABCD);
    uint8_t exp2[] = { 0xAB, 0xCD };
    check_bytes("be_put_u16", buf, exp2, 2);

    neverc_binary_big_endian_put_uint32(buf, 0x12345678U);
    uint8_t exp4[] = { 0x12, 0x34, 0x56, 0x78 };
    check_bytes("be_put_u32", buf, exp4, 4);

    neverc_binary_big_endian_put_uint64(buf, 0xDEADBEEFCAFEBABEULL);
    uint8_t exp8[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };
    check_bytes("be_put_u64", buf, exp8, 8);
}

static void test_little_endian_write(void) {
    printf("[little endian write]\n");
    uint8_t buf[8];

    neverc_binary_little_endian_put_uint16(buf, 0xABCD);
    uint8_t exp2[] = { 0xCD, 0xAB };
    check_bytes("le_put_u16", buf, exp2, 2);

    neverc_binary_little_endian_put_uint32(buf, 0x12345678U);
    uint8_t exp4[] = { 0x78, 0x56, 0x34, 0x12 };
    check_bytes("le_put_u32", buf, exp4, 4);

    neverc_binary_little_endian_put_uint64(buf, 0xDEADBEEFCAFEBABEULL);
    uint8_t exp8[] = { 0xBE, 0xBA, 0xFE, 0xCA, 0xEF, 0xBE, 0xAD, 0xDE };
    check_bytes("le_put_u64", buf, exp8, 8);
}

static void test_roundtrip(void) {
    printf("[roundtrip]\n");
    uint8_t buf[8];

    /* Write then read should produce original value */
    neverc_binary_big_endian_put_uint16(buf, 0xBEEF);
    check_u16("be rt u16", neverc_binary_big_endian_uint16(buf), 0xBEEF);

    neverc_binary_big_endian_put_uint32(buf, 0xDEADBEEFU);
    check_u32("be rt u32", neverc_binary_big_endian_uint32(buf), 0xDEADBEEFU);

    neverc_binary_big_endian_put_uint64(buf, 0x123456789ABCDEF0ULL);
    check_u64("be rt u64", neverc_binary_big_endian_uint64(buf), 0x123456789ABCDEF0ULL);

    neverc_binary_little_endian_put_uint16(buf, 0xBEEF);
    check_u16("le rt u16", neverc_binary_little_endian_uint16(buf), 0xBEEF);

    neverc_binary_little_endian_put_uint32(buf, 0xDEADBEEFU);
    check_u32("le rt u32", neverc_binary_little_endian_uint32(buf), 0xDEADBEEFU);

    neverc_binary_little_endian_put_uint64(buf, 0x123456789ABCDEF0ULL);
    check_u64("le rt u64", neverc_binary_little_endian_uint64(buf), 0x123456789ABCDEF0ULL);
}

static void test_zero_max(void) {
    printf("[zero/max]\n");
    uint8_t buf[8];

    neverc_binary_big_endian_put_uint16(buf, 0);
    check_u16("be u16 zero", neverc_binary_big_endian_uint16(buf), 0);
    neverc_binary_big_endian_put_uint16(buf, 0xFFFF);
    check_u16("be u16 max", neverc_binary_big_endian_uint16(buf), 0xFFFF);

    neverc_binary_big_endian_put_uint32(buf, 0);
    check_u32("be u32 zero", neverc_binary_big_endian_uint32(buf), 0);
    neverc_binary_big_endian_put_uint32(buf, 0xFFFFFFFFU);
    check_u32("be u32 max", neverc_binary_big_endian_uint32(buf), 0xFFFFFFFFU);

    neverc_binary_big_endian_put_uint64(buf, 0);
    check_u64("be u64 zero", neverc_binary_big_endian_uint64(buf), 0);
    neverc_binary_big_endian_put_uint64(buf, 0xFFFFFFFFFFFFFFFFULL);
    check_u64("be u64 max", neverc_binary_big_endian_uint64(buf), 0xFFFFFFFFFFFFFFFFULL);
}

int main(void) {
    printf("=== NeverC Binary Library Tests ===\n\n");
    test_big_endian_read();
    test_little_endian_read();
    test_big_endian_write();
    test_little_endian_write();
    test_roundtrip();
    test_zero_max();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
