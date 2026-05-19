// Validates round 3 of 64-bit-only cleanup:
//   - isX86() simplified to only match x86_64 (Triple::x86 removed)
//   - CMOV_GR16/CMOV_GR32 NoCMOV pseudos removed (x86_64 always has CMOV)
//   - canUseCMOV() deleted (always true on x86_64)
//   - MC layer is64Bit() fields removed from ELF/MachO/COFF writers
//   - ARM32 unwind emitter (~1083 lines) removed from MCWin64EH.cpp
//   - emitThumbFunc / ThumbFuncs / SF_ThumbFunc removed
//   - RelocationTypesARM enum removed from COFF.h
//   - ARMSubArch v7/v6/v5/v4t enums removed from Triple
//   - isArch32Bit() / isArch16Bit() / get32BitArchVariant() removed
//   - hasArmWideBranch() removed from TargetTransformInfo
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: 64only_round3"

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

// Prevent constant folding
static volatile int zero = 0;

// ============================================================
// 1. CMOV is always available: conditional select must work
//    for all integer widths (i8/i16/i32/i64)
// ============================================================

__attribute__((noinline))
static int8_t  sel_i8 (int c, int8_t  a, int8_t  b) { return c ? a : b; }
__attribute__((noinline))
static int16_t sel_i16(int c, int16_t a, int16_t b) { return c ? a : b; }
__attribute__((noinline))
static int32_t sel_i32(int c, int32_t a, int32_t b) { return c ? a : b; }
__attribute__((noinline))
static int64_t sel_i64(int c, int64_t a, int64_t b) { return c ? a : b; }

static void test_cmov_all_widths(void) {
    int t = 1 + zero, f = 0 + zero;

    CHECK(sel_i8(t,  0x7F, -1) == 0x7F, "cmov i8 true");
    CHECK(sel_i8(f,  0x7F, -1) == -1,   "cmov i8 false");

    CHECK(sel_i16(t, 0x7FFF, -1) == 0x7FFF, "cmov i16 true");
    CHECK(sel_i16(f, 0x7FFF, -1) == -1,     "cmov i16 false");

    CHECK(sel_i32(t, 0x7FFFFFFF, -1) == 0x7FFFFFFF, "cmov i32 true");
    CHECK(sel_i32(f, 0x7FFFFFFF, -1) == -1,         "cmov i32 false");

    CHECK(sel_i64(t, 0x7FFFFFFFFFFFFFFFLL, -1) == 0x7FFFFFFFFFFFFFFFLL,
          "cmov i64 true");
    CHECK(sel_i64(f, 0x7FFFFFFFFFFFFFFFLL, -1) == -1, "cmov i64 false");
}

// ============================================================
// 2. ABS lowering (was gated on canUseCMOV)
// ============================================================

__attribute__((noinline))
static int32_t my_abs32(int32_t x) { return x < 0 ? -x : x; }
__attribute__((noinline))
static int64_t my_abs64(int64_t x) { return x < 0 ? -x : x; }

static void test_abs(void) {
    int32_t v32 = -42 + zero;
    int64_t v64 = -1000000000000LL + zero;
    CHECK(my_abs32(v32) == 42,           "abs32");
    CHECK(my_abs64(v64) == 1000000000000LL, "abs64");
    CHECK(my_abs32(0 + zero) == 0,       "abs32(0)");
    CHECK(my_abs64(0 + zero) == 0,       "abs64(0)");
}

// ============================================================
// 3. SDIV by power-of-2 (was gated on canUseCMOV)
// ============================================================

__attribute__((noinline))
static int32_t sdiv4(int32_t x) { return x / 4; }
__attribute__((noinline))
static int64_t sdiv8(int64_t x) { return x / 8; }

static void test_sdiv_pow2(void) {
    CHECK(sdiv4(100 + zero) == 25,   "sdiv4(100)");
    CHECK(sdiv4(-7  + zero) == -1,   "sdiv4(-7)");
    CHECK(sdiv8(800 + zero) == 100,  "sdiv8(800)");
    CHECK(sdiv8(-15 + zero) == -1,   "sdiv8(-15)");
}

// ============================================================
// 4. 32-bit sub-register ops in 64-bit mode (must still work)
//    - EAX/W0 registers, i32 arithmetic, zero-extension
// ============================================================

__attribute__((noinline))
static uint32_t add32(uint32_t a, uint32_t b) { return a + b; }
__attribute__((noinline))
static uint32_t mul32(uint32_t a, uint32_t b) { return a * b; }
__attribute__((noinline))
static uint32_t shr32(uint32_t a, int s) { return a >> s; }

static void test_32bit_in_64bit(void) {
    uint32_t a = 0xDEADBEEF + zero;
    uint32_t b = 0x11111111 + zero;

    CHECK(add32(a, b) == 0xEFBED000, "add32");
    CHECK(mul32(7 + zero, 6 + zero) == 42, "mul32");
    CHECK(shr32(0x80000000U + zero, 1) == 0x40000000U, "shr32");

    // Zero-extension: 32-bit result in 64-bit register
    uint64_t wide = add32(0xFFFFFFFF + zero, 1);
    CHECK(wide == 0, "zext 32→64 overflow");
}

// ============================================================
// 5. Pointer size is 64-bit (MC layer hardcoded to 64-bit)
// ============================================================

static void test_pointer_size(void) {
    CHECK(sizeof(void *) == 8, "ptr size == 8");
    CHECK(sizeof(size_t) == 8, "size_t == 8");
    CHECK(sizeof(intptr_t) == 8, "intptr_t == 8");

    // Function pointer should be 8 bytes
    int (*fp)(void) = (int (*)(void))(uintptr_t)(0xDEADCAFE + zero);
    CHECK(sizeof(fp) == 8, "fptr size == 8");
}

// ============================================================
// 6. Struct layout with mixed widths
// ============================================================

struct Mixed {
    uint8_t  a;
    uint32_t b;
    uint64_t c;
    uint16_t d;
};

struct __attribute__((packed)) Packed {
    uint8_t  x;
    uint64_t y;
    uint32_t z;
};

static void test_struct_layout(void) {
    CHECK(sizeof(struct Mixed) == 24, "Mixed size");
    CHECK(__builtin_offsetof(struct Mixed, a) == 0, "Mixed.a offset");
    CHECK(__builtin_offsetof(struct Mixed, b) == 4, "Mixed.b offset");
    CHECK(__builtin_offsetof(struct Mixed, c) == 8, "Mixed.c offset");
    CHECK(__builtin_offsetof(struct Mixed, d) == 16, "Mixed.d offset");

    CHECK(sizeof(struct Packed) == 13, "Packed size");
    CHECK(__builtin_offsetof(struct Packed, x) == 0, "Packed.x offset");
    CHECK(__builtin_offsetof(struct Packed, y) == 1, "Packed.y offset");
    CHECK(__builtin_offsetof(struct Packed, z) == 9, "Packed.z offset");
}

// ============================================================
// 7. Atomic operations (32-bit atomics in 64-bit mode)
// ============================================================

static void test_atomics(void) {
    volatile int32_t atom32 = 0;
    __atomic_store_n(&atom32, 42, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&atom32, __ATOMIC_SEQ_CST) == 42, "atomic32 store/load");

    int32_t old = 42;
    _Bool ok = __atomic_compare_exchange_n(&atom32, &old, 99,
                                            0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    CHECK(ok && atom32 == 99, "atomic32 CAS");

    volatile int64_t atom64 = 0;
    __atomic_store_n(&atom64, 0xDEADBEEFCAFELL, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&atom64, __ATOMIC_SEQ_CST) == 0xDEADBEEFCAFELL,
          "atomic64 store/load");
}

// ============================================================
// 8. Builtins that produce 32-bit results
// ============================================================

static void test_32bit_builtins(void) {
    uint32_t v32 = 0x00FF0000 + zero;
    CHECK(__builtin_clz(v32) == 8, "clz32");
    CHECK(__builtin_ctz(v32 | 0x10000) == 16, "ctz32");
    CHECK(__builtin_popcount(0xAAAA + zero) == 8, "popcount32");

    uint64_t v64 = 0x00FF000000000000ULL + zero;
    CHECK(__builtin_clzll(v64) == 8, "clz64");
    CHECK(__builtin_ctzll(v64) == 48, "ctz64");
    CHECK(__builtin_popcountll(0xAAAAAAAAAAAAAAAAULL + zero) == 32, "popcount64");
}

int main(void) {
    test_cmov_all_widths();
    test_abs();
    test_sdiv_pow2();
    test_32bit_in_64bit();
    test_pointer_size();
    test_struct_layout();
    test_atomics();
    test_32bit_builtins();
    printf("PASS: 64only_round3\n");
    return 0;
}
