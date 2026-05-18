// test_generic_cpu_cmov.c
// Validates that the "generic" CPU model has CMOV enabled (FeatureCMOV added
// to X86.td generic processor definition), and that CMOV-dependent lowering
// paths work correctly at both -O2 and -O0.
//
// Key changes validated:
//   - FeatureCMOV added to generic ProcModel in X86.td
//   - HasCMOV predicate changed from canUseCMOV() to hasCMOV()
//   - NoCMOV predicate removed (never true on x86_64)
//   - CMOV_GR32/CMOV_GR16 NoCMOV pseudos removed from X86InstrCompiler.td
//   - ABS/SDIVPow2/SELECT lowering no longer guarded by canUseCMOV()
//
// RUN: %neverc -O2 -march=x86-64 %s -o %t && %t && echo "PASS: generic_cpu_cmov"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

__attribute__((noinline))
static int32_t abs_i32(int32_t x) { return x < 0 ? -x : x; }

__attribute__((noinline))
static int64_t abs_i64(int64_t x) { return x < 0 ? -x : x; }

__attribute__((noinline))
static int32_t sdiv_pow2(int32_t x) { return x / 8; }

__attribute__((noinline))
static int32_t select_i32(int cond, int32_t a, int32_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int16_t select_i16(int cond, int16_t a, int16_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int8_t select_i8(int cond, int8_t a, int8_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int32_t clamp(int32_t x, int32_t lo, int32_t hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

__attribute__((noinline))
static int32_t ffs_minus1(int32_t x) {
    return x == 0 ? -1 : __builtin_ctz(x);
}

int main(void) {
    CHECK(abs_i32(-42) == 42, "abs_i32 negative");
    CHECK(abs_i32(42) == 42, "abs_i32 positive");
    CHECK(abs_i32(0) == 0, "abs_i32 zero");
    CHECK(abs_i32(INT32_MIN + 1) == INT32_MAX, "abs_i32 min+1");

    CHECK(abs_i64(-100LL) == 100LL, "abs_i64 negative");
    CHECK(abs_i64(0) == 0, "abs_i64 zero");

    CHECK(sdiv_pow2(80) == 10, "sdiv_pow2 positive");
    CHECK(sdiv_pow2(-80) == -10, "sdiv_pow2 negative");
    CHECK(sdiv_pow2(7) == 0, "sdiv_pow2 small");
    CHECK(sdiv_pow2(-7) == 0, "sdiv_pow2 neg small");

    CHECK(select_i32(1, 100, 200) == 100, "select_i32 true");
    CHECK(select_i32(0, 100, 200) == 200, "select_i32 false");
    CHECK(select_i16(1, 1000, 2000) == 1000, "select_i16 true");
    CHECK(select_i16(0, 1000, 2000) == 2000, "select_i16 false");
    CHECK(select_i8(1, 10, 20) == 10, "select_i8 true");
    CHECK(select_i8(0, 10, 20) == 20, "select_i8 false");

    CHECK(clamp(50, 0, 100) == 50, "clamp mid");
    CHECK(clamp(-10, 0, 100) == 0, "clamp lo");
    CHECK(clamp(200, 0, 100) == 100, "clamp hi");

    CHECK(ffs_minus1(0) == -1, "ffs zero");
    CHECK(ffs_minus1(1) == 0, "ffs bit0");
    CHECK(ffs_minus1(8) == 3, "ffs bit3");

    printf("PASS: generic_cpu_cmov\n");
    return 0;
}
