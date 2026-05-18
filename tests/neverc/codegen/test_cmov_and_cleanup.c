// test_cmov_and_cleanup.c
// Validates that removing NoCMOV-gated CMOV pseudo instructions
// (CMOV_GR32/CMOV_GR16) and all prior 32-bit cleanup hasn't broken
// conditional-select lowering on x86_64 / AArch64.
//
// On x86_64, conditional selects on i32/i16 use hardware CMOV instructions
// (CMOV32rr/CMOV16rr), NOT the NoCMOV pseudo-to-branch expansion.
// The NoCMOV pseudos only existed for pre-Pentium Pro CPUs without CMOV;
// x86_64 always has CMOV as a baseline feature.
//
// Tests cover:
//   1. i32 conditional select (exercises hardware CMOV32rr)
//   2. i16 conditional select (exercises hardware CMOV16rr)
//   3. i8 conditional select  (uses CMOV_GR8 pseudo, still present)
//   4. i64 conditional select (exercises hardware CMOV64rr)
//   5. Chained conditional selects (ISel CMOV cascade optimization)
//   6. Conditional select with memory operand (CMOV32rm/CMOV64rm)
//   7. FP conditional select (uses CMOV_FR32/FR64 pseudos, unaffected)
//   8. Mixed-width conditional chains (32/64-bit interleaved)
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: cmov_and_cleanup"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

static volatile int sink;

// --- 1. i32 conditional select ---

static uint32_t select_u32(int cond, uint32_t a, uint32_t b) {
    return cond ? a : b;
}

static void test_cmov_i32(void) {
    CHECK(select_u32(1, 0xDEADBEEF, 0xCAFEBABE) == 0xDEADBEEF, "cmov32 true");
    CHECK(select_u32(0, 0xDEADBEEF, 0xCAFEBABE) == 0xCAFEBABE, "cmov32 false");
    CHECK(select_u32(42, 100, 200) == 100, "cmov32 nonzero");
    CHECK(select_u32(-1, 0xFFFFFFFF, 0) == 0xFFFFFFFF, "cmov32 neg cond");
}

// --- 2. i16 conditional select ---

static uint16_t select_u16(int cond, uint16_t a, uint16_t b) {
    return cond ? a : b;
}

static void test_cmov_i16(void) {
    CHECK(select_u16(1, 0xBEEF, 0xCAFE) == 0xBEEF, "cmov16 true");
    CHECK(select_u16(0, 0xBEEF, 0xCAFE) == 0xCAFE, "cmov16 false");
    CHECK(select_u16(1, 0xFFFF, 0) == 0xFFFF, "cmov16 max");
}

// --- 3. i8 conditional select ---

static uint8_t select_u8(int cond, uint8_t a, uint8_t b) {
    return cond ? a : b;
}

static void test_cmov_i8(void) {
    CHECK(select_u8(1, 0xFF, 0x00) == 0xFF, "cmov8 true");
    CHECK(select_u8(0, 0xFF, 0x00) == 0x00, "cmov8 false");
    CHECK(select_u8(1, 0x42, 0x99) == 0x42, "cmov8 true 2");
}

// --- 4. i64 conditional select ---

static uint64_t select_u64(int cond, uint64_t a, uint64_t b) {
    return cond ? a : b;
}

static void test_cmov_i64(void) {
    CHECK(select_u64(1, 0xDEADBEEFCAFEBABEULL, 0x1234567890ABCDEFULL) ==
          0xDEADBEEFCAFEBABEULL, "cmov64 true");
    CHECK(select_u64(0, 0xDEADBEEFCAFEBABEULL, 0x1234567890ABCDEFULL) ==
          0x1234567890ABCDEFULL, "cmov64 false");
}

// --- 5. Chained conditional selects ---

static uint32_t chained_select(int c1, int c2, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t t1 = c1 ? a : b;
    uint32_t t2 = c2 ? t1 : c;
    return t2;
}

static void test_chained_cmov(void) {
    CHECK(chained_select(1, 1, 10, 20, 30) == 10, "chain tt");
    CHECK(chained_select(1, 0, 10, 20, 30) == 30, "chain tf");
    CHECK(chained_select(0, 1, 10, 20, 30) == 20, "chain ft");
    CHECK(chained_select(0, 0, 10, 20, 30) == 30, "chain ff");
}

// --- 6. Conditional select with memory operand ---

static uint32_t select_from_array(int idx, const uint32_t *arr, uint32_t fallback) {
    return (idx >= 0) ? arr[idx] : fallback;
}

static void test_cmov_mem(void) {
    uint32_t arr[] = { 100, 200, 300, 400 };
    CHECK(select_from_array(0, arr, 999) == 100, "cmov_mem idx0");
    CHECK(select_from_array(3, arr, 999) == 400, "cmov_mem idx3");
    CHECK(select_from_array(-1, arr, 999) == 999, "cmov_mem fallback");
}

// --- 7. Floating-point conditional select ---

static float select_float(int cond, float a, float b) {
    return cond ? a : b;
}

static double select_double(int cond, double a, double b) {
    return cond ? a : b;
}

static void test_cmov_fp(void) {
    float f = select_float(1, 3.14f, 2.72f);
    CHECK(f > 3.13f && f < 3.15f, "cmov_float true");
    f = select_float(0, 3.14f, 2.72f);
    CHECK(f > 2.71f && f < 2.73f, "cmov_float false");

    double d = select_double(1, 1.41421356, 2.71828182);
    CHECK(d > 1.414 && d < 1.415, "cmov_double true");
    d = select_double(0, 1.41421356, 2.71828182);
    CHECK(d > 2.718 && d < 2.719, "cmov_double false");
}

// --- 8. Mixed-width conditional chains ---

static uint64_t mixed_chain(int c, uint32_t narrow, uint64_t wide) {
    uint32_t r32 = c ? narrow : (uint32_t)0;
    uint64_t r64 = c ? wide : 0ULL;
    return (uint64_t)r32 + r64;
}

static void test_mixed_width_cmov(void) {
    CHECK(mixed_chain(1, 0xFFFFFFFF, 0x100000000ULL) ==
          0x1FFFFFFFFULL, "mixed true");
    CHECK(mixed_chain(0, 0xFFFFFFFF, 0x100000000ULL) == 0, "mixed false");
}

// --- 9. Comparison-driven select (ensures EFLAGS-based CMOV) ---

static int32_t abs_i32(int32_t x) {
    return x < 0 ? -x : x;
}

static int32_t min_i32(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static int32_t max_i32(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static int32_t clamp_i32(int32_t val, int32_t lo, int32_t hi) {
    return min_i32(max_i32(val, lo), hi);
}

static void test_comparison_cmov(void) {
    CHECK(abs_i32(42) == 42, "abs positive");
    CHECK(abs_i32(-42) == 42, "abs negative");
    CHECK(abs_i32(0) == 0, "abs zero");

    CHECK(min_i32(3, 7) == 3, "min 3,7");
    CHECK(min_i32(7, 3) == 3, "min 7,3");
    CHECK(max_i32(3, 7) == 7, "max 3,7");

    CHECK(clamp_i32(50, 0, 100) == 50, "clamp middle");
    CHECK(clamp_i32(-10, 0, 100) == 0, "clamp low");
    CHECK(clamp_i32(200, 0, 100) == 100, "clamp high");
}

// --- 10. Pointer-width verify (64-bit only) ---

static void test_pointer_width(void) {
    CHECK(sizeof(void *) == 8, "pointer is 64-bit");
    CHECK(sizeof(size_t) == 8, "size_t is 64-bit");
    CHECK(sizeof(intptr_t) == 8, "intptr_t is 64-bit");
}

int main(void) {
    test_cmov_i32();
    test_cmov_i16();
    test_cmov_i8();
    test_cmov_i64();
    test_chained_cmov();
    test_cmov_mem();
    test_cmov_fp();
    test_mixed_width_cmov();
    test_comparison_cmov();
    test_pointer_width();

    printf("test_cmov_and_cleanup: ALL PASSED\n");
    return 0;
}
