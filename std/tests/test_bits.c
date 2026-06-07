#include "neverc/math/bits.h"
#include <stdio.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
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

static void test_leading_zeros(void) {
    printf("[leading_zeros]\n");
    check_int("clz32(0)", neverc_bits_leading_zeros32(0), 32);
    check_int("clz32(1)", neverc_bits_leading_zeros32(1), 31);
    check_int("clz32(2)", neverc_bits_leading_zeros32(2), 30);
    check_int("clz32(0x80000000)", neverc_bits_leading_zeros32(0x80000000U), 0);
    check_int("clz32(0xFF)", neverc_bits_leading_zeros32(0xFF), 24);
    check_int("clz32(0x10000)", neverc_bits_leading_zeros32(0x10000), 15);

    check_int("clz64(0)", neverc_bits_leading_zeros64(0), 64);
    check_int("clz64(1)", neverc_bits_leading_zeros64(1), 63);
    check_int("clz64(1<<63)", neverc_bits_leading_zeros64(1ULL << 63), 0);
    check_int("clz64(0xFF00)", neverc_bits_leading_zeros64(0xFF00ULL), 48);
}

static void test_trailing_zeros(void) {
    printf("[trailing_zeros]\n");
    check_int("ctz32(0)", neverc_bits_trailing_zeros32(0), 32);
    check_int("ctz32(1)", neverc_bits_trailing_zeros32(1), 0);
    check_int("ctz32(2)", neverc_bits_trailing_zeros32(2), 1);
    check_int("ctz32(8)", neverc_bits_trailing_zeros32(8), 3);
    check_int("ctz32(0x80000000)", neverc_bits_trailing_zeros32(0x80000000U), 31);
    check_int("ctz32(0x100)", neverc_bits_trailing_zeros32(0x100), 8);

    check_int("ctz64(0)", neverc_bits_trailing_zeros64(0), 64);
    check_int("ctz64(1)", neverc_bits_trailing_zeros64(1), 0);
    check_int("ctz64(1<<63)", neverc_bits_trailing_zeros64(1ULL << 63), 63);
    check_int("ctz64(0x10000)", neverc_bits_trailing_zeros64(0x10000ULL), 16);
}

static void test_ones_count(void) {
    printf("[ones_count]\n");
    check_int("pop32(0)", neverc_bits_ones_count32(0), 0);
    check_int("pop32(1)", neverc_bits_ones_count32(1), 1);
    check_int("pop32(0xF)", neverc_bits_ones_count32(0xF), 4);
    check_int("pop32(0xFFFFFFFF)", neverc_bits_ones_count32(0xFFFFFFFFU), 32);
    check_int("pop32(0xAAAAAAAA)", neverc_bits_ones_count32(0xAAAAAAAAU), 16);
    check_int("pop32(0x55555555)", neverc_bits_ones_count32(0x55555555U), 16);

    check_int("pop64(0)", neverc_bits_ones_count64(0), 0);
    check_int("pop64(FFFFFFFFFFFFFFFF)", neverc_bits_ones_count64(0xFFFFFFFFFFFFFFFFULL), 64);
    check_int("pop64(0x123456789ABCDEF0)", neverc_bits_ones_count64(0x123456789ABCDEF0ULL), 32);
}

static void test_len(void) {
    printf("[len]\n");
    check_int("len32(0)", neverc_bits_len32(0), 0);
    check_int("len32(1)", neverc_bits_len32(1), 1);
    check_int("len32(7)", neverc_bits_len32(7), 3);
    check_int("len32(8)", neverc_bits_len32(8), 4);
    check_int("len32(0xFF)", neverc_bits_len32(0xFF), 8);
    check_int("len32(0x80000000)", neverc_bits_len32(0x80000000U), 32);

    check_int("len64(0)", neverc_bits_len64(0), 0);
    check_int("len64(1<<50)", neverc_bits_len64(1ULL << 50), 51);
    check_int("len64(1<<63)", neverc_bits_len64(1ULL << 63), 64);
}

static void test_rotate(void) {
    printf("[rotate]\n");
    check_u32("rot32(1,1)", neverc_bits_rotate_left32(1, 1), 2);
    check_u32("rot32(1,31)", neverc_bits_rotate_left32(1, 31), 0x80000000U);
    check_u32("rot32(0x80000000,1)", neverc_bits_rotate_left32(0x80000000U, 1), 1);
    check_u32("rot32(0xABCD1234,16)", neverc_bits_rotate_left32(0xABCD1234U, 16), 0x1234ABCDU);

    check_u64("rot64(1,1)", neverc_bits_rotate_left64(1, 1), 2);
    check_u64("rot64(1,63)", neverc_bits_rotate_left64(1, 63), 1ULL << 63);
    check_u64("rot64(1<<63,1)", neverc_bits_rotate_left64(1ULL << 63, 1), 1);
}

static void test_reverse(void) {
    printf("[reverse]\n");
    check_u32("rev32(0)", neverc_bits_reverse32(0), 0);
    check_u32("rev32(1)", neverc_bits_reverse32(1), 0x80000000U);
    check_u32("rev32(0x80000000)", neverc_bits_reverse32(0x80000000U), 1);
    /* 0x12345678 reversed: every bit flipped in order */
    check_u32("rev32(rev32(x))==x", neverc_bits_reverse32(neverc_bits_reverse32(0x12345678U)), 0x12345678U);

    check_u64("rev64(1)", neverc_bits_reverse64(1), 1ULL << 63);
    check_u64("rev64(rev64(x))==x",
        neverc_bits_reverse64(neverc_bits_reverse64(0x123456789ABCDEF0ULL)), 0x123456789ABCDEF0ULL);
}

static void test_reverse_bytes(void) {
    printf("[reverse_bytes]\n");
    check_u32("bswap16(0x1234)", (uint32_t)neverc_bits_reverse_bytes16(0x1234), 0x3412);

    check_u32("bswap32(0x12345678)", neverc_bits_reverse_bytes32(0x12345678U), 0x78563412U);
    check_u32("bswap32(bswap32(x))==x",
        neverc_bits_reverse_bytes32(neverc_bits_reverse_bytes32(0xDEADBEEFU)), 0xDEADBEEFU);

    check_u64("bswap64(0x0102030405060708)",
        neverc_bits_reverse_bytes64(0x0102030405060708ULL), 0x0807060504030201ULL);
}

static void test_add_sub(void) {
    printf("[add64/sub64]\n");
    uint64_t sum, carry;

    neverc_bits_add64(1, 2, 0, &sum, &carry);
    check_u64("add64(1,2,0).sum", sum, 3);
    check_u64("add64(1,2,0).carry", carry, 0);

    neverc_bits_add64(0xFFFFFFFFFFFFFFFFULL, 1, 0, &sum, &carry);
    check_u64("add64(MAX,1,0).sum", sum, 0);
    check_u64("add64(MAX,1,0).carry", carry, 1);

    neverc_bits_add64(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 1, &sum, &carry);
    check_u64("add64(MAX,MAX,1).sum", sum, 0xFFFFFFFFFFFFFFFFULL);
    check_u64("add64(MAX,MAX,1).carry", carry, 1);

    uint64_t diff, borrow;
    neverc_bits_sub64(5, 3, 0, &diff, &borrow);
    check_u64("sub64(5,3,0).diff", diff, 2);
    check_u64("sub64(5,3,0).borrow", borrow, 0);

    neverc_bits_sub64(0, 1, 0, &diff, &borrow);
    check_u64("sub64(0,1,0).diff", diff, 0xFFFFFFFFFFFFFFFFULL);
    check_u64("sub64(0,1,0).borrow", borrow, 1);
}

static void test_mul64(void) {
    printf("[mul64]\n");
    uint64_t hi, lo;

    neverc_bits_mul64(3, 4, &hi, &lo);
    check_u64("mul64(3,4).hi", hi, 0);
    check_u64("mul64(3,4).lo", lo, 12);

    neverc_bits_mul64(0xFFFFFFFFFFFFFFFFULL, 2, &hi, &lo);
    check_u64("mul64(MAX,2).hi", hi, 1);
    check_u64("mul64(MAX,2).lo", lo, 0xFFFFFFFFFFFFFFFEULL);

    neverc_bits_mul64(0x100000000ULL, 0x100000000ULL, &hi, &lo);
    check_u64("mul64(2^32,2^32).hi", hi, 1);
    check_u64("mul64(2^32,2^32).lo", lo, 0);

    /* Identity: x*1 = x */
    neverc_bits_mul64(0xDEADBEEFCAFEBABEULL, 1, &hi, &lo);
    check_u64("mul64(x,1).hi", hi, 0);
    check_u64("mul64(x,1).lo", lo, 0xDEADBEEFCAFEBABEULL);
}

/* Exhaustive consistency: clz + ctz + popcount properties */
static void test_consistency(void) {
    printf("[consistency]\n");
    /* For powers of 2: popcount = 1, clz + ctz = 31 (32-bit) */
    for (int i = 0; i < 32; i++) {
        uint32_t x = 1U << i;
        char buf[64];
        snprintf(buf, sizeof(buf), "pop32(1<<%d)==1", i);
        check_int(buf, neverc_bits_ones_count32(x), 1);
        snprintf(buf, sizeof(buf), "clz+ctz=31 (1<<%d)", i);
        check_int(buf, neverc_bits_leading_zeros32(x) + neverc_bits_trailing_zeros32(x), 31);
    }
}

int main(void) {
    printf("=== NeverC Bits Library Tests ===\n\n");

    test_leading_zeros();
    test_trailing_zeros();
    test_ones_count();
    test_len();
    test_rotate();
    test_reverse();
    test_reverse_bytes();
    test_add_sub();
    test_mul64();
    test_consistency();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
