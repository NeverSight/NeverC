#include "neverc/std/math/bits.h"
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

static void test_8_16_variants(void) {
    printf("[8/16 bit variants]\n");

    check_int("len8(0)", neverc_bits_len8(0), 0);
    check_int("len8(1)", neverc_bits_len8(1), 1);
    check_int("len8(128)", neverc_bits_len8(128), 8);
    check_int("len8(255)", neverc_bits_len8(255), 8);
    check_int("len16(0)", neverc_bits_len16(0), 0);
    check_int("len16(1)", neverc_bits_len16(1), 1);
    check_int("len16(0x8000)", neverc_bits_len16(0x8000), 16);

    check_int("clz8(1)", neverc_bits_leading_zeros8(1), 7);
    check_int("clz8(0)", neverc_bits_leading_zeros8(0), 8);
    check_int("clz16(1)", neverc_bits_leading_zeros16(1), 15);
    check_int("clz16(0x8000)", neverc_bits_leading_zeros16(0x8000), 0);

    check_int("ctz8(1)", neverc_bits_trailing_zeros8(1), 0);
    check_int("ctz8(0)", neverc_bits_trailing_zeros8(0), 8);
    check_int("ctz8(128)", neverc_bits_trailing_zeros8(128), 7);
    check_int("ctz16(1)", neverc_bits_trailing_zeros16(1), 0);
    check_int("ctz16(0)", neverc_bits_trailing_zeros16(0), 16);

    check_int("pop8(0)", neverc_bits_ones_count8(0), 0);
    check_int("pop8(0xFF)", neverc_bits_ones_count8(0xFF), 8);
    check_int("pop8(0x55)", neverc_bits_ones_count8(0x55), 4);
    check_int("pop16(0)", neverc_bits_ones_count16(0), 0);
    check_int("pop16(0xFFFF)", neverc_bits_ones_count16(0xFFFF), 16);

    check_int("rev8(0x01)", (int)neverc_bits_reverse8(0x01), 0x80);
    check_int("rev8(0xAA)", (int)neverc_bits_reverse8(0xAA), 0x55);
    check_int("rev16(0x0001)", (int)neverc_bits_reverse16(0x0001), (int)0x8000);

    check_int("rotl8(0x01,1)", (int)neverc_bits_rotate_left8(0x01, 1), 0x02);
    check_int("rotl8(0x80,1)", (int)neverc_bits_rotate_left8(0x80, 1), 0x01);
    check_int("rotl16(0x0001,1)", (int)neverc_bits_rotate_left16(0x0001, 1), 0x0002);
}

static void test_32_arithmetic(void) {
    printf("[32-bit arithmetic]\n");
    uint32_t sum, carry, diff, borrow, hi, lo;

    neverc_bits_add32(0xFFFFFFFF, 1, 0, &sum, &carry);
    check_int("add32 overflow sum", (int)sum, 0);
    check_int("add32 overflow carry", (int)carry, 1);

    neverc_bits_add32(100, 200, 0, &sum, &carry);
    check_int("add32 normal", (int)sum, 300);
    check_int("add32 no carry", (int)carry, 0);

    neverc_bits_sub32(100, 50, 0, &diff, &borrow);
    check_int("sub32 normal", (int)diff, 50);
    check_int("sub32 no borrow", (int)borrow, 0);

    neverc_bits_sub32(0, 1, 0, &diff, &borrow);
    check_int("sub32 underflow", (int)diff, (int)0xFFFFFFFF);
    check_int("sub32 borrow", (int)borrow, 1);

    neverc_bits_mul32(0xFFFF, 0xFFFF, &hi, &lo);
    check_int("mul32 hi", (int)hi, 0);
    check_int("mul32 lo", (int)lo, (int)((uint32_t)0xFFFF * (uint32_t)0xFFFF));

    neverc_bits_mul32(0xFFFFFFFF, 2, &hi, &lo);
    check_int("mul32 big hi", (int)hi, 1);
    check_int("mul32 big lo", (int)lo, (int)0xFFFFFFFE);
}

static void test_rotate_edge_cases(void) {
    printf("[rotate edge cases — k=0 (was UB)]\n");
    check_u32("rot32(0xABCD,0)", neverc_bits_rotate_left32(0xABCD1234U, 0), 0xABCD1234U);
    check_u64("rot64(0xDEAD,0)", neverc_bits_rotate_left64(0xDEADBEEFCAFEBABEULL, 0), 0xDEADBEEFCAFEBABEULL);
    check_int("rot8(0x5A,0)", (int)neverc_bits_rotate_left8(0x5A, 0), 0x5A);
    check_int("rot16(0x1234,0)", (int)neverc_bits_rotate_left16(0x1234, 0), 0x1234);
    check_u32("rot32(0x12345678,32)", neverc_bits_rotate_left32(0x12345678U, 32), 0x12345678U);
    check_u64("rot64(x,64)", neverc_bits_rotate_left64(0x123ULL, 64), 0x123ULL);
    check_int("rot8(x,8)", (int)neverc_bits_rotate_left8(0xAB, 8), 0xAB);
    check_int("rot16(x,16)", (int)neverc_bits_rotate_left16(0xABCD, 16), (int)(uint16_t)0xABCD);

    unsigned int val = 0xDEADBEEFU;
    check_int("rotate(x,0) generic", (int)neverc_bits_rotate_left(val, 0), (int)val);
}

static void test_generic_versions(void) {
    printf("[generic uint-sized versions]\n");
    unsigned int x = 42, y = 58;

    unsigned int sum, carry;
    neverc_bits_add(x, y, 0, &sum, &carry);
    check_int("add(42,58,0).sum", (int)sum, 100);
    check_int("add(42,58,0).carry", (int)carry, 0);

    unsigned int diff, borrow;
    neverc_bits_sub(100, 42, 0, &diff, &borrow);
    check_int("sub(100,42,0).diff", (int)diff, 58);
    check_int("sub(100,42,0).borrow", (int)borrow, 0);

    unsigned int hi, lo;
    neverc_bits_mul(6, 7, &hi, &lo);
    check_int("mul(6,7).lo", (int)lo, 42);
    check_int("mul(6,7).hi", (int)hi, 0);

    unsigned int quo, rem;
    neverc_bits_div(0, 100, 7, &quo, &rem);
    check_int("div(0,100,7).quo", (int)quo, 14);
    check_int("div(0,100,7).rem", (int)rem, 2);

    check_int("rem(0,100,7)", (int)neverc_bits_rem(0, 100, 7), 2);

    unsigned int rv = neverc_bits_reverse(neverc_bits_reverse(0x12345678U));
    check_int("reverse(reverse(x))==x", (int)rv, (int)0x12345678U);

    unsigned int bsw = neverc_bits_reverse_bytes(neverc_bits_reverse_bytes(0xDEADBEEFU));
    check_int("rbytes(rbytes(x))==x", (int)bsw, (int)0xDEADBEEFU);
}

static void test_division(void) {
    printf("[division]\n");
    uint32_t q32, r32;
    uint64_t q64, r64;

    neverc_bits_div32(0, 100, 7, &q32, &r32);
    check_int("div32(100,7).q", (int)q32, 14);
    check_int("div32(100,7).r", (int)r32, 2);

    neverc_bits_div32(1, 0, 0xFFFFFFFF, &q32, &r32);
    check_int("div32(1<<32, max).q", (int)q32, 1);

    check_int("rem32(0,100,7)", (int)neverc_bits_rem32(0, 100, 7), 2);

    neverc_bits_div64(0, 100, 7, &q64, &r64);
    check_u64("div64(100,7).q", q64, 14);
    check_u64("div64(100,7).r", r64, 2);

    check_u64("rem64(0,100,7)", neverc_bits_rem64(0, 100, 7), 2);
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
    test_8_16_variants();
    test_32_arithmetic();
    test_rotate_edge_cases();
    test_generic_versions();
    test_division();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
