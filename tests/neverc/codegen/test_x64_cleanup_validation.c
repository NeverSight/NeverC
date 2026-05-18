// test_x64_cleanup_validation.c
// Validates the x86_64-only cleanup: canUseCMOV() guards removed,
// NoCMOV pseudo deleted, ABS/SDIV optimizations unconditional,
// MC layer hardcoded to 64-bit, ARM32/Thumb infrastructure removed.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: x64_cleanup_validation"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        abort(); \
    } \
} while (0)

// ---- CMOV on all widths (canUseCMOV guard removal) ----
// Previously gated behind Subtarget.canUseCMOV(); now unconditional.
// x86_64 always has CMOV; compiler should use hardware CMOV16/32/64.

__attribute__((noinline))
static int8_t select_i8(int cond, int8_t a, int8_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int16_t select_i16(int cond, int16_t a, int16_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int32_t select_i32(int cond, int32_t a, int32_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int64_t select_i64(int cond, int64_t a, int64_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static double select_f64(int cond, double a, double b) {
    return cond ? a : b;
}

static void test_cmov_all_widths(void) {
    CHECK(select_i8(1, 0x7F, -1) == 0x7F, "cmov i8 true");
    CHECK(select_i8(0, 0x7F, -1) == -1, "cmov i8 false");

    CHECK(select_i16(1, 0x7FFF, -1) == 0x7FFF, "cmov i16 true");
    CHECK(select_i16(0, 0x7FFF, -1) == -1, "cmov i16 false");

    CHECK(select_i32(1, 0x7FFFFFFF, -1) == 0x7FFFFFFF, "cmov i32 true");
    CHECK(select_i32(0, 0x7FFFFFFF, -1) == -1, "cmov i32 false");

    CHECK(select_i64(1, 0x7FFFFFFFFFFFFFFFLL, -1) == 0x7FFFFFFFFFFFFFFFLL,
          "cmov i64 true");
    CHECK(select_i64(0, 0x7FFFFFFFFFFFFFFFLL, -1) == -1, "cmov i64 false");

    CHECK(select_f64(1, 3.14, 2.71) == 3.14, "cmov f64 true");
    CHECK(select_f64(0, 3.14, 2.71) == 2.71, "cmov f64 false");
}

// ---- Chained CMOV (multi-way select) ----

__attribute__((noinline))
static int32_t clamp(int32_t x, int32_t lo, int32_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void test_chained_cmov(void) {
    CHECK(clamp(-100, 0, 255) == 0, "clamp below");
    CHECK(clamp(128, 0, 255) == 128, "clamp inside");
    CHECK(clamp(999, 0, 255) == 255, "clamp above");
    CHECK(clamp(0, 0, 255) == 0, "clamp at lower bound");
    CHECK(clamp(255, 0, 255) == 255, "clamp at upper bound");
}

// ---- Integer ABS (guard removed: was if(canUseCMOV())) ----

__attribute__((noinline))
static int16_t abs_i16(int16_t x) { return x < 0 ? -x : x; }

__attribute__((noinline))
static int32_t abs_i32(int32_t x) { return x < 0 ? -x : x; }

__attribute__((noinline))
static int64_t abs_i64(int64_t x) { return x < 0 ? -x : x; }

static void test_abs(void) {
    CHECK(abs_i16(0) == 0, "abs_i16(0)");
    CHECK(abs_i16(42) == 42, "abs_i16(42)");
    CHECK(abs_i16(-42) == 42, "abs_i16(-42)");
    CHECK(abs_i16(-32767) == 32767, "abs_i16(-32767)");

    CHECK(abs_i32(0) == 0, "abs_i32(0)");
    CHECK(abs_i32(12345) == 12345, "abs_i32(12345)");
    CHECK(abs_i32(-12345) == 12345, "abs_i32(-12345)");
    CHECK(abs_i32(-2147483647) == 2147483647, "abs_i32(-INT32_MAX)");

    CHECK(abs_i64(0) == 0, "abs_i64(0)");
    CHECK(abs_i64(-1) == 1, "abs_i64(-1)");
    CHECK(abs_i64(0x7FFFFFFFFFFFFFFFLL) == 0x7FFFFFFFFFFFFFFFLL,
          "abs_i64(INT64_MAX)");
    CHECK(abs_i64(-0x7FFFFFFFFFFFFFFFLL) == 0x7FFFFFFFFFFFFFFFLL,
          "abs_i64(-INT64_MAX)");
}

// ---- SDIV by power of 2 (guard removed: was if(!canUseCMOV())) ----

__attribute__((noinline))
static int32_t sdiv_pow2_4(int32_t x) { return x / 4; }

__attribute__((noinline))
static int32_t sdiv_pow2_8(int32_t x) { return x / 8; }

__attribute__((noinline))
static int64_t sdiv64_pow2_16(int64_t x) { return x / 16; }

__attribute__((noinline))
static int32_t sdiv_neg_pow2(int32_t x) { return x / -4; }

static void test_sdiv_pow2(void) {
    CHECK(sdiv_pow2_4(100) == 25, "sdiv 100/4");
    CHECK(sdiv_pow2_4(-100) == -25, "sdiv -100/4");
    CHECK(sdiv_pow2_4(3) == 0, "sdiv 3/4");
    CHECK(sdiv_pow2_4(-3) == 0, "sdiv -3/4");
    CHECK(sdiv_pow2_4(-4) == -1, "sdiv -4/4");
    CHECK(sdiv_pow2_4(0) == 0, "sdiv 0/4");

    CHECK(sdiv_pow2_8(64) == 8, "sdiv 64/8");
    CHECK(sdiv_pow2_8(-64) == -8, "sdiv -64/8");
    CHECK(sdiv_pow2_8(7) == 0, "sdiv 7/8");
    CHECK(sdiv_pow2_8(-7) == 0, "sdiv -7/8");

    CHECK(sdiv64_pow2_16(256LL) == 16, "sdiv64 256/16");
    CHECK(sdiv64_pow2_16(-256LL) == -16, "sdiv64 -256/16");
    CHECK(sdiv64_pow2_16(15LL) == 0, "sdiv64 15/16");

    CHECK(sdiv_neg_pow2(100) == -25, "sdiv 100/(-4)");
    CHECK(sdiv_neg_pow2(-100) == 25, "sdiv -100/(-4)");
}

// ---- FFS/CTZ select pattern (canUseCMOV guard removed) ----

__attribute__((noinline))
static int ffs_like(unsigned x) {
    return x == 0 ? 0 : __builtin_ctz(x) + 1;
}

static void test_ffs_select(void) {
    CHECK(ffs_like(0) == 0, "ffs(0)");
    CHECK(ffs_like(1) == 1, "ffs(1)");
    CHECK(ffs_like(0x80) == 8, "ffs(0x80)");
    CHECK(ffs_like(0x80000000U) == 32, "ffs(1<<31)");
    CHECK(ffs_like(0x100) == 9, "ffs(0x100)");
}

// ---- SMIN / SMAX (exercises CMOV codegen) ----

__attribute__((noinline))
static int32_t smin32(int32_t a, int32_t b) { return a < b ? a : b; }

__attribute__((noinline))
static int32_t smax32(int32_t a, int32_t b) { return a > b ? a : b; }

__attribute__((noinline))
static int64_t smin64(int64_t a, int64_t b) { return a < b ? a : b; }

static void test_smin_smax(void) {
    CHECK(smin32(10, 20) == 10, "smin32(10,20)");
    CHECK(smin32(-5, 5) == -5, "smin32(-5,5)");
    CHECK(smin32(INT32_MIN, 0) == INT32_MIN, "smin32(INT32_MIN,0)");

    CHECK(smax32(10, 20) == 20, "smax32(10,20)");
    CHECK(smax32(-5, 5) == 5, "smax32(-5,5)");
    CHECK(smax32(INT32_MAX, 0) == INT32_MAX, "smax32(INT32_MAX,0)");

    CHECK(smin64(-1, 1) == -1, "smin64(-1,1)");
    CHECK(smin64(INT64_MIN, 0) == INT64_MIN, "smin64(INT64_MIN,0)");
}

// ---- LEA correctness (LEA64_32r was a bug site, now fixed) ----

__attribute__((noinline))
static uint32_t lea_add(uint32_t a, uint32_t b) {
    return a + b;
}

__attribute__((noinline))
static uint32_t lea_shl_add(uint32_t a, uint32_t b) {
    return (a << 2) + b;
}

__attribute__((noinline))
static uint32_t lea_chain(uint32_t x) {
    return x * 3 + 5;
}

static void test_lea(void) {
    CHECK(lea_add(0xFFFFFFFF, 1) == 0, "lea add overflow u32");
    CHECK(lea_add(100, 200) == 300, "lea add simple");

    CHECK(lea_shl_add(10, 3) == 43, "lea shl+add");
    CHECK(lea_shl_add(0x40000000, 0) == 0, "lea shl+add overflow");

    CHECK(lea_chain(10) == 35, "lea chain 10*3+5");
    CHECK(lea_chain(0) == 5, "lea chain 0*3+5");
    CHECK(lea_chain(0xFFFFFFFF) == 2, "lea chain overflow");
}

// ---- 32-bit ops in 64-bit zero-extend correctness ----

__attribute__((noinline))
static uint64_t zext_add32(uint32_t a, uint32_t b) {
    uint32_t r = a + b;
    return r;
}

__attribute__((noinline))
static uint64_t mixed_width_chain(uint32_t x) {
    uint32_t y = x * 7;
    uint64_t z = y;
    z += 0x100000000ULL;
    return z;
}

static void test_32bit_in_64bit(void) {
    CHECK(zext_add32(0xFFFFFFFF, 1) == 0, "zext add32 overflow -> 0");
    CHECK(zext_add32(100, 200) == 300, "zext add32 simple");

    CHECK(mixed_width_chain(10) == 70ULL + 0x100000000ULL,
          "mixed width chain");
    CHECK(mixed_width_chain(0xFFFFFFFF) == 0xFFFFFFF9ULL + 0x100000000ULL,
          "mixed width chain overflow");
}

// ---- Pointer is 64-bit (verifies 64-bit target assumption) ----

static void test_pointer_size(void) {
    CHECK(sizeof(void *) == 8, "pointer is 8 bytes");
    CHECK(sizeof(size_t) == 8, "size_t is 8 bytes");
    CHECK(sizeof(ptrdiff_t) == 8, "ptrdiff_t is 8 bytes");
    CHECK(sizeof(long long) == 8, "long long is 8 bytes");
}

int main(void) {
    test_cmov_all_widths();
    test_chained_cmov();
    test_abs();
    test_sdiv_pow2();
    test_ffs_select();
    test_smin_smax();
    test_lea();
    test_32bit_in_64bit();
    test_pointer_size();

    printf("PASS: x64_cleanup_validation\n");
    return 0;
}
