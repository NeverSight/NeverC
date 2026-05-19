// Validates that removing CMOV_GR16/CMOV_GR32 NoCMOV pseudos and the
// NoCMOV predicate hasn't broken conditional-select lowering at -O0.
//
// At -O0, SelectionDAG produces ISD::SELECT nodes that get lowered to CMOV
// pseudo instructions (CMOV_GR8 for i8, hardware CMOV16rr/CMOV32rr/CMOV64rr
// for i16/i32/i64). The deleted CMOV_GR16/CMOV_GR32 pseudos were only
// selected under the NoCMOV predicate (pre-P6 CPUs without CMOV), which is
// impossible on x86_64.
//
// This test uses -O0 to exercise the pseudo-to-CMOV lowering path without
// optimization passes that might bypass it (e.g., PHI lowering, tail merge).
//
// Also validates:
//   - canUseCMOV() removal: ISD::ABS lowering no longer gated
//   - NoCMOV branch fallback removal: no-CMOV XOR/OR pattern deleted
//   - ARM64 W-register conditional select (CSEL) at -O0
//
// RUN: %neverc -O0 %s -o %t && %t && echo "PASS: nocmov_pseudo_removal"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

static volatile int zero = 0;

// --- i32 conditional select at -O0 ---
__attribute__((noinline))
static int32_t select_i32(int cond, int32_t a, int32_t b) {
    return cond ? a : b;
}

// --- i16 conditional select at -O0 ---
__attribute__((noinline))
static int16_t select_i16(int cond, int16_t a, int16_t b) {
    return cond ? a : b;
}

// --- i8 conditional select at -O0 (uses CMOV_GR8 pseudo, still present) ---
__attribute__((noinline))
static int8_t select_i8(int cond, int8_t a, int8_t b) {
    return cond ? a : b;
}

// --- i64 conditional select at -O0 ---
__attribute__((noinline))
static int64_t select_i64(int cond, int64_t a, int64_t b) {
    return cond ? a : b;
}

// --- ISD::ABS lowering (was gated on canUseCMOV, now unconditional) ---
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

// --- Nested conditional (stress CMOV cascade at -O0) ---
__attribute__((noinline))
static int32_t nested_select(int c1, int c2, int32_t a, int32_t b, int32_t c) {
    int32_t t = c1 ? a : b;
    return c2 ? t : c;
}

// --- 32-bit sdiv by power-of-2 (BuildSDIVPow2 was gated on canUseCMOV) ---
__attribute__((noinline))
static int32_t sdiv4(int32_t x) {
    return x / 4;
}

static void test_selects(void) {
    CHECK(select_i32(1, 100, 200) == 100, "i32 select true");
    CHECK(select_i32(0, 100, 200) == 200, "i32 select false");
    CHECK(select_i32(-1, 0x7FFFFFFF, 0) == 0x7FFFFFFF, "i32 select neg cond");

    CHECK(select_i16(1, 1000, 2000) == 1000, "i16 select true");
    CHECK(select_i16(0, 1000, 2000) == 2000, "i16 select false");
    CHECK(select_i16(42, -32768, 32767) == -32768, "i16 select extremes");

    CHECK(select_i8(1, 0x7F, -128) == 0x7F, "i8 select true");
    CHECK(select_i8(0, 0x7F, -128) == -128, "i8 select false");

    CHECK(select_i64(1, 0x123456789ABCDEF0LL, 0) == 0x123456789ABCDEF0LL,
          "i64 select true");
    CHECK(select_i64(0, 0, -1LL) == -1LL, "i64 select false");
}

static void test_abs(void) {
    CHECK(my_abs32(42) == 42, "abs32 pos");
    CHECK(my_abs32(-42) == 42, "abs32 neg");
    CHECK(my_abs32(0) == 0, "abs32 zero");

    CHECK(my_abs64(1000000000000LL) == 1000000000000LL, "abs64 pos");
    CHECK(my_abs64(-1000000000000LL) == 1000000000000LL, "abs64 neg");

    CHECK(my_abs16(100) == 100, "abs16 pos");
    CHECK(my_abs16(-100) == 100, "abs16 neg");
}

static void test_nested(void) {
    CHECK(nested_select(1, 1, 10, 20, 30) == 10, "nested tt");
    CHECK(nested_select(0, 1, 10, 20, 30) == 20, "nested ft");
    CHECK(nested_select(1, 0, 10, 20, 30) == 30, "nested tf");
    CHECK(nested_select(0, 0, 10, 20, 30) == 30, "nested ff");
}

static void test_sdiv_pow2(void) {
    CHECK(sdiv4(100) == 25, "sdiv4(100)");
    CHECK(sdiv4(-100) == -25, "sdiv4(-100)");
    CHECK(sdiv4(3) == 0, "sdiv4(3)");
    CHECK(sdiv4(-3) == 0, "sdiv4(-3)");
    CHECK(sdiv4(0) == 0, "sdiv4(0)");
}

int main(void) {
    test_selects();
    test_abs();
    test_nested();
    test_sdiv_pow2();
    printf("PASS: nocmov_pseudo_removal - all tests passed\n");
    return 0;
}
