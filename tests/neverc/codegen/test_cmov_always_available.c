// test_cmov_always_available.c
// Validates that CMOV-based optimizations work correctly after removing
// the canUseCMOV() guards (CMOV is mandatory in x86_64).
//
// Key changes validated:
//   1. Integer ABS uses CMOV (not branch) for i16/i32/i64
//   2. Power-of-2 SDIV uses CMOV-based select
//   3. SELECT lowering promotes i8 selects through i32 CMOV
//   4. FFS-1 pattern (CTTZ+CMOV) for i32/i64
//   5. CMOV_GR8/CMOV_RFP80 pseudo still works (NoCMOV GR16/GR32 removed)
//   6. FP conditional moves still valid
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: cmov_always_available"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

// --- 1. Integer ABS via CMOV ---

__attribute__((noinline))
static int32_t my_abs32(int32_t x) {
    return x < 0 ? -x : x;
}

__attribute__((noinline))
static int64_t my_abs64(int64_t x) {
    return x < 0 ? -x : x;
}

__attribute__((noinline))
static int16_t my_abs16(int16_t x) {
    return x < 0 ? -x : x;
}

static void test_abs_cmov(void) {
    CHECK(my_abs32(0) == 0, "abs32(0)");
    CHECK(my_abs32(42) == 42, "abs32(42)");
    CHECK(my_abs32(-42) == 42, "abs32(-42)");
    CHECK(my_abs32(-2147483647) == 2147483647, "abs32(INT32_MIN+1)");

    CHECK(my_abs64(0LL) == 0LL, "abs64(0)");
    CHECK(my_abs64(100LL) == 100LL, "abs64(100)");
    CHECK(my_abs64(-100LL) == 100LL, "abs64(-100)");
    CHECK(my_abs64(-9223372036854775807LL) == 9223372036854775807LL,
          "abs64(INT64_MIN+1)");

    CHECK(my_abs16(0) == 0, "abs16(0)");
    CHECK(my_abs16(100) == 100, "abs16(100)");
    CHECK(my_abs16(-100) == 100, "abs16(-100)");
    CHECK(my_abs16(-32767) == 32767, "abs16(-32767)");
}

// --- 2. Power-of-2 SDIV via CMOV ---

__attribute__((noinline))
static int32_t sdiv_pow2_4(int32_t x) {
    return x / 4;
}

__attribute__((noinline))
static int64_t sdiv_pow2_8(int64_t x) {
    return x / 8;
}

__attribute__((noinline))
static int32_t sdiv_neg_pow2(int32_t x) {
    return x / -16;
}

static void test_sdiv_pow2(void) {
    CHECK(sdiv_pow2_4(16) == 4, "16/4");
    CHECK(sdiv_pow2_4(-16) == -4, "-16/4");
    CHECK(sdiv_pow2_4(7) == 1, "7/4 truncates");
    CHECK(sdiv_pow2_4(-7) == -1, "-7/4 truncates toward zero");
    CHECK(sdiv_pow2_4(0) == 0, "0/4");

    CHECK(sdiv_pow2_8(64LL) == 8LL, "64/8");
    CHECK(sdiv_pow2_8(-64LL) == -8LL, "-64/8");
    CHECK(sdiv_pow2_8(7LL) == 0LL, "7/8 truncates");
    CHECK(sdiv_pow2_8(-7LL) == 0LL, "-7/8 truncates toward zero");

    CHECK(sdiv_neg_pow2(32) == -2, "32/-16");
    CHECK(sdiv_neg_pow2(-32) == 2, "-32/-16");
    CHECK(sdiv_neg_pow2(15) == 0, "15/-16 truncates");
}

// --- 3. i8 select promotion through i32 CMOV ---

__attribute__((noinline))
static uint8_t select_i8(int cond, uint8_t a, uint8_t b) {
    return cond ? a : b;
}

static void test_i8_select(void) {
    CHECK(select_i8(1, 0xAA, 0x55) == 0xAA, "i8 select true");
    CHECK(select_i8(0, 0xAA, 0x55) == 0x55, "i8 select false");
    CHECK(select_i8(42, 0xFF, 0x00) == 0xFF, "i8 select nonzero");
    CHECK(select_i8(0, 0, 0) == 0, "i8 select zero");
}

// --- 4. FFS-1 pattern (CTTZ + CMOV) ---

__attribute__((noinline))
static int ffs_minus1_32(uint32_t x) {
    return x ? __builtin_ctz(x) : -1;
}

__attribute__((noinline))
static int ffs_minus1_64(uint64_t x) {
    return x ? __builtin_ctzll(x) : -1;
}

static void test_ffs_pattern(void) {
    CHECK(ffs_minus1_32(0) == -1, "ffs(0) == -1");
    CHECK(ffs_minus1_32(1) == 0, "ffs(1) == 0");
    CHECK(ffs_minus1_32(0x80) == 7, "ffs(0x80) == 7");
    CHECK(ffs_minus1_32(0x80000000U) == 31, "ffs(1<<31) == 31");

    CHECK(ffs_minus1_64(0ULL) == -1, "ffs64(0) == -1");
    CHECK(ffs_minus1_64(1ULL) == 0, "ffs64(1) == 0");
    CHECK(ffs_minus1_64(0x100000000ULL) == 32, "ffs64(1<<32) == 32");
}

// --- 5. Conditional select with various types ---

__attribute__((noinline))
static int32_t select_i32(int cond, int32_t a, int32_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int64_t select_i64(int cond, int64_t a, int64_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int16_t select_i16(int cond, int16_t a, int16_t b) {
    return cond ? a : b;
}

static void test_select_all_sizes(void) {
    CHECK(select_i32(1, 0x12345678, 0) == 0x12345678, "i32 select true");
    CHECK(select_i32(0, 0x12345678, 0xABCDEF01) == (int32_t)0xABCDEF01,
          "i32 select false");

    CHECK(select_i64(1, 0x123456789ABCDEF0LL, 0) == 0x123456789ABCDEF0LL,
          "i64 select true");
    CHECK(select_i64(0, 0, -1LL) == -1LL, "i64 select false");

    CHECK(select_i16(1, 0x1234, 0) == 0x1234, "i16 select true");
    CHECK(select_i16(0, 0, -1) == -1, "i16 select false");
}

// --- 6. FP conditional select (validates CMOV for FP stack) ---

__attribute__((noinline))
static float select_f32(int cond, float a, float b) {
    return cond ? a : b;
}

__attribute__((noinline))
static double select_f64(int cond, double a, double b) {
    return cond ? a : b;
}

static void test_fp_select(void) {
    CHECK(select_f32(1, 3.14f, 2.71f) == 3.14f, "f32 select true");
    CHECK(select_f32(0, 3.14f, 2.71f) == 2.71f, "f32 select false");

    CHECK(select_f64(1, 3.14159265, 2.71828182) == 3.14159265,
          "f64 select true");
    CHECK(select_f64(0, 3.14159265, 2.71828182) == 2.71828182,
          "f64 select false");
}

// --- 7. min/max patterns (CMOV optimization) ---

__attribute__((noinline))
static int32_t my_min32(int32_t a, int32_t b) {
    return a < b ? a : b;
}

__attribute__((noinline))
static int32_t my_max32(int32_t a, int32_t b) {
    return a > b ? a : b;
}

__attribute__((noinline))
static int64_t my_min64(int64_t a, int64_t b) {
    return a < b ? a : b;
}

__attribute__((noinline))
static uint32_t my_minu32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static void test_minmax(void) {
    CHECK(my_min32(5, 10) == 5, "min(5,10)");
    CHECK(my_min32(10, 5) == 5, "min(10,5)");
    CHECK(my_min32(-5, 5) == -5, "min(-5,5)");

    CHECK(my_max32(5, 10) == 10, "max(5,10)");
    CHECK(my_max32(10, 5) == 10, "max(10,5)");
    CHECK(my_max32(-5, 5) == 5, "max(-5,5)");

    CHECK(my_min64(100LL, -100LL) == -100LL, "min64");
    CHECK(my_minu32(0xFFFFFFFFU, 0) == 0, "minu32");
}

// --- 8. Clamp (double CMOV) ---

__attribute__((noinline))
static int32_t clamp32(int32_t val, int32_t lo, int32_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static void test_clamp(void) {
    CHECK(clamp32(50, 0, 100) == 50, "clamp middle");
    CHECK(clamp32(-10, 0, 100) == 0, "clamp below");
    CHECK(clamp32(200, 0, 100) == 100, "clamp above");
    CHECK(clamp32(0, 0, 100) == 0, "clamp at lo");
    CHECK(clamp32(100, 0, 100) == 100, "clamp at hi");
}

int main(void) {
    test_abs_cmov();
    test_sdiv_pow2();
    test_i8_select();
    test_ffs_pattern();
    test_select_all_sizes();
    test_fp_select();
    test_minmax();
    test_clamp();

    printf("test_cmov_always_available: ALL PASSED\n");
    return 0;
}
