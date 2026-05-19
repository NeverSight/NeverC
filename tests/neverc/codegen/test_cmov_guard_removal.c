// Validates that removing canUseCMOV() guards from X86ISelLowering
// hasn't broken codegen. On x86_64, CMOV is always available as a
// baseline feature, so these guards were dead code.
//
// Affected code paths:
//   1. ISD::ABS lowering (was gated on canUseCMOV)
//   2. BuildSDIVPow2 (was gated on canUseCMOV for conditional adjustment)
//   3. LowerSELECT ffs-minus-1 pattern (was gated on canUseCMOV)
//   4. LowerSELECT i8 promotion to i32 (was gated on canUseCMOV)
//   5. LowerSELECT FP stack CMOV legality check
//   6. combineCMov constant-folding (was gated on canUseCMOV)
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: cmov_guard_removal"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

static volatile int sink;

// --- 1. Integer absolute value (ISD::ABS) ---

__attribute__((noinline))
static int32_t abs_i32(int32_t x) {
    return x < 0 ? -x : x;
}

__attribute__((noinline))
static int64_t abs_i64(int64_t x) {
    return x < 0 ? -x : x;
}

__attribute__((noinline))
static int16_t abs_i16(int16_t x) {
    return x < 0 ? -x : x;
}

static void test_abs(void) {
    CHECK(abs_i32(0) == 0, "abs32(0)");
    CHECK(abs_i32(42) == 42, "abs32(42)");
    CHECK(abs_i32(-42) == 42, "abs32(-42)");
    CHECK(abs_i32(-2147483647) == 2147483647, "abs32(INT32_MIN+1)");
    CHECK(abs_i32(2147483647) == 2147483647, "abs32(INT32_MAX)");

    CHECK(abs_i64(0) == 0, "abs64(0)");
    CHECK(abs_i64(1000000000000LL) == 1000000000000LL, "abs64(1T)");
    CHECK(abs_i64(-1000000000000LL) == 1000000000000LL, "abs64(-1T)");
    CHECK(abs_i64(-1) == 1, "abs64(-1)");

    CHECK(abs_i16(0) == 0, "abs16(0)");
    CHECK(abs_i16(100) == 100, "abs16(100)");
    CHECK(abs_i16(-100) == 100, "abs16(-100)");
    CHECK(abs_i16(32767) == 32767, "abs16(INT16_MAX)");
}

// --- 2. SDIV by power of 2 (BuildSDIVPow2) ---

__attribute__((noinline))
static int32_t sdiv_pow2_i32(int32_t x) {
    return x / 4;
}

__attribute__((noinline))
static int64_t sdiv_pow2_i64(int64_t x) {
    return x / 8;
}

__attribute__((noinline))
static int32_t sdiv_neg_pow2(int32_t x) {
    return x / -16;
}

static void test_sdiv_pow2(void) {
    CHECK(sdiv_pow2_i32(100) == 25, "sdiv32 100/4");
    CHECK(sdiv_pow2_i32(-100) == -25, "sdiv32 -100/4");
    CHECK(sdiv_pow2_i32(3) == 0, "sdiv32 3/4");
    CHECK(sdiv_pow2_i32(-3) == 0, "sdiv32 -3/4");
    CHECK(sdiv_pow2_i32(-4) == -1, "sdiv32 -4/4");
    CHECK(sdiv_pow2_i32(0) == 0, "sdiv32 0/4");

    CHECK(sdiv_pow2_i64(800) == 100, "sdiv64 800/8");
    CHECK(sdiv_pow2_i64(-800) == -100, "sdiv64 -800/8");
    CHECK(sdiv_pow2_i64(7) == 0, "sdiv64 7/8");
    CHECK(sdiv_pow2_i64(-7) == 0, "sdiv64 -7/8");

    CHECK(sdiv_neg_pow2(160) == -10, "sdiv32 160/-16");
    CHECK(sdiv_neg_pow2(-160) == 10, "sdiv32 -160/-16");
    CHECK(sdiv_neg_pow2(0) == 0, "sdiv32 0/-16");
}

// --- 3. FFS-based conditional (LowerSELECT ffs-minus-1) ---

__attribute__((noinline))
static int ffs_or_neg1(uint32_t x) {
    return x ? __builtin_ctz(x) : -1;
}

__attribute__((noinline))
static int ffs_or_neg1_64(uint64_t x) {
    return x ? __builtin_ctzll(x) : -1;
}

static void test_ffs_select(void) {
    CHECK(ffs_or_neg1(0) == -1, "ffs(0) == -1");
    CHECK(ffs_or_neg1(1) == 0, "ffs(1) == 0");
    CHECK(ffs_or_neg1(0x80) == 7, "ffs(0x80) == 7");
    CHECK(ffs_or_neg1(0x80000000U) == 31, "ffs(1<<31) == 31");

    CHECK(ffs_or_neg1_64(0) == -1, "ffs64(0) == -1");
    CHECK(ffs_or_neg1_64(1) == 0, "ffs64(1) == 0");
    CHECK(ffs_or_neg1_64(0x100000000ULL) == 32, "ffs64(1<<32) == 32");
}

// --- 4. i8 conditional select (uses CMOV_GR8 pseudo) ---

__attribute__((noinline))
static uint8_t select_u8(int cond, uint8_t a, uint8_t b) {
    return cond ? a : b;
}

static void test_i8_select(void) {
    CHECK(select_u8(1, 0xAA, 0x55) == 0xAA, "select8 true");
    CHECK(select_u8(0, 0xAA, 0x55) == 0x55, "select8 false");
    CHECK(select_u8(42, 0xFF, 0x00) == 0xFF, "select8 nonzero");
    CHECK(select_u8(-1, 0x01, 0xFE) == 0x01, "select8 neg cond");
}

// --- 5. Mixed-width conditional chains ---

__attribute__((noinline))
static uint64_t chain_select(int c1, int c2, int c3,
                              uint64_t a, uint64_t b) {
    uint32_t lo = c1 ? (uint32_t)a : (uint32_t)b;
    uint32_t hi = c2 ? (uint32_t)(a >> 32) : (uint32_t)(b >> 32);
    uint64_t combined = ((uint64_t)hi << 32) | lo;
    return c3 ? combined : ~combined;
}

static void test_chain_select(void) {
    uint64_t a = 0xDEADBEEFCAFEBABEULL;
    uint64_t b = 0x1234567890ABCDEFULL;

    CHECK(chain_select(1, 1, 1, a, b) == a, "chain all-true");
    CHECK(chain_select(0, 0, 1, a, b) == b, "chain lo+hi false");
    CHECK(chain_select(1, 0, 1, a, b) == 0x12345678CAFEBABEULL,
          "chain mixed hi=b,lo=a");
    uint64_t r = chain_select(1, 1, 0, a, b);
    CHECK(r == ~a, "chain invert");
}

// --- 6. smin/smax-like patterns (from the LowerSELECT changes) ---

__attribute__((noinline))
static int32_t smin_zero(int32_t x) {
    return x < 0 ? x : 0;
}

__attribute__((noinline))
static int32_t smax_zero(int32_t x) {
    return x > 0 ? x : 0;
}

static void test_smin_smax_zero(void) {
    CHECK(smin_zero(10) == 0, "smin(10,0)");
    CHECK(smin_zero(-10) == -10, "smin(-10,0)");
    CHECK(smin_zero(0) == 0, "smin(0,0)");

    CHECK(smax_zero(10) == 10, "smax(10,0)");
    CHECK(smax_zero(-10) == 0, "smax(-10,0)");
    CHECK(smax_zero(0) == 0, "smax(0,0)");
}

int main(void) {
    test_abs();
    test_sdiv_pow2();
    test_ffs_select();
    test_i8_select();
    test_chain_select();
    test_smin_smax_zero();
    printf("PASS: cmov_guard_removal - all tests passed\n");
    return 0;
}
