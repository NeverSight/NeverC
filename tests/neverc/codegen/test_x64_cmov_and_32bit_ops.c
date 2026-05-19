// Validates the canUseCMOV() removal and 32-bit cleanup:
//
//   1. ABS lowering: removed canUseCMOV() guard from ISD::ABS Custom lowering
//   2. SDIV pow2: removed canUseCMOV() guard from BuildSDIVPow2
//   3. SELECT patterns: removed canUseCMOV() guard from select lowering
//   4. CMOV_GR16/CMOV_GR32 pseudo removal (NoCMOV predicate deleted)
//   5. 32-bit integer ops in 64-bit mode still work (EAX/EBX are valid)
//   6. FeatureCMOV on generic CPU (bug fix: generic was missing FeatureCMOV)
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: x64_cmov_and_32bit_ops"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

// Prevent constant folding
static volatile int zero = 0;
static volatile int one = 1;

// ---- 1. ABS lowering (canUseCMOV guard removed from ISD::ABS) ----

static int32_t abs32(int32_t x) { return x < 0 ? -x : x; }
static int64_t abs64(int64_t x) { return x < 0 ? -x : x; }
static int16_t abs16(int16_t x) { return x < 0 ? -x : x; }

static void test_abs_lowering(void) {
    int32_t v32 = -42 + zero;
    CHECK(abs32(v32) == 42, "abs32(-42)");
    CHECK(abs32(0 + zero) == 0, "abs32(0)");
    CHECK(abs32(100 + zero) == 100, "abs32(100)");
    CHECK(abs32(INT32_MIN + 1 + zero) == INT32_MAX, "abs32(INT32_MIN+1)");

    int64_t v64 = -0x100000000LL + zero;
    CHECK(abs64(v64) == 0x100000000LL, "abs64 large");
    CHECK(abs64(0LL + zero) == 0, "abs64(0)");

    int16_t v16 = -1000 + zero;
    CHECK(abs16(v16) == 1000, "abs16(-1000)");
}

// ---- 2. SDIV pow2 (canUseCMOV guard removed from BuildSDIVPow2) ----

static int32_t sdiv_pow2_32(int32_t x) { return x / 4; }
static int32_t sdiv_neg_pow2_32(int32_t x) { return x / -8; }
static int64_t sdiv_pow2_64(int64_t x) { return x / 16; }

static void test_sdiv_pow2(void) {
    int32_t v = 100 + zero;
    CHECK(sdiv_pow2_32(v) == 25, "100/4");
    CHECK(sdiv_pow2_32(-100 + zero) == -25, "-100/4");
    CHECK(sdiv_pow2_32(7 + zero) == 1, "7/4 truncate");
    CHECK(sdiv_pow2_32(-7 + zero) == -1, "-7/4 truncate");
    CHECK(sdiv_pow2_32(0 + zero) == 0, "0/4");

    CHECK(sdiv_neg_pow2_32(80 + zero) == -10, "80/-8");
    CHECK(sdiv_neg_pow2_32(-80 + zero) == 10, "-80/-8");

    int64_t v64 = 256LL + zero;
    CHECK(sdiv_pow2_64(v64) == 16, "256/16 i64");
    CHECK(sdiv_pow2_64(-256LL + zero) == -16, "-256/16 i64");
}

// ---- 3. SELECT / CMOV patterns ----

static uint32_t select32(int c, uint32_t a, uint32_t b) { return c ? a : b; }
static uint64_t select64(int c, uint64_t a, uint64_t b) { return c ? a : b; }
static uint16_t select16(int c, uint16_t a, uint16_t b) { return c ? a : b; }
static uint8_t select8(int c, uint8_t a, uint8_t b) { return c ? a : b; }

static uint32_t ffs_minus1(uint32_t x) {
    return x ? __builtin_ctz(x) : (uint32_t)-1;
}

static uint32_t smin(int32_t x) { return x < 0 ? x : 0; }
static uint32_t smax(int32_t x) { return x > 0 ? x : 0; }

static void test_select_patterns(void) {
    int c1 = one, c0 = zero;

    CHECK(select32(c1, 0xDEADBEEF, 0xCAFEBABE) == 0xDEADBEEF, "sel32 true");
    CHECK(select32(c0, 0xDEADBEEF, 0xCAFEBABE) == 0xCAFEBABE, "sel32 false");

    CHECK(select64(c1, 0xDEADBEEFCAFEBABEULL, 0) == 0xDEADBEEFCAFEBABEULL, "sel64 true");
    CHECK(select64(c0, 0xDEADBEEFCAFEBABEULL, 0) == 0, "sel64 false");

    CHECK(select16(c1, 0xBEEF, 0xCAFE) == 0xBEEF, "sel16 true");
    CHECK(select16(c0, 0xBEEF, 0xCAFE) == 0xCAFE, "sel16 false");

    CHECK(select8(c1, 0xFF, 0x00) == 0xFF, "sel8 true");
    CHECK(select8(c0, 0xFF, 0x00) == 0x00, "sel8 false");

    CHECK(ffs_minus1(0x80 + zero) == 7, "ffs 0x80");
    CHECK(ffs_minus1(0 + zero) == (uint32_t)-1, "ffs 0");

    CHECK(smin(-5 + zero) == (uint32_t)-5, "smin neg");
    CHECK(smin(5 + zero) == 0, "smin pos");
    CHECK(smax(5 + zero) == 5, "smax pos");
    CHECK(smax(-5 + zero) == 0, "smax neg");
}

// ---- 4. 32-bit integer operations in 64-bit mode ----
// These use EAX/EBX/ECX etc (low 32 bits of 64-bit regs).
// Must still work after removing 32-bit ISA code.

static uint32_t add32(uint32_t a, uint32_t b) { return a + b; }
static uint32_t sub32(uint32_t a, uint32_t b) { return a - b; }
static uint32_t mul32(uint32_t a, uint32_t b) { return a * b; }
static uint32_t shl32(uint32_t a, int s) { return a << s; }
static uint32_t shr32(uint32_t a, int s) { return a >> s; }
static int32_t  sar32(int32_t a, int s)  { return a >> s; }
static uint32_t and32(uint32_t a, uint32_t b) { return a & b; }
static uint32_t or32(uint32_t a, uint32_t b) { return a | b; }
static uint32_t xor32(uint32_t a, uint32_t b) { return a ^ b; }
static uint32_t not32(uint32_t a) { return ~a; }
static uint32_t bswap32(uint32_t a) { return __builtin_bswap32(a); }
static int32_t  neg32(int32_t a) { return -a; }

static void test_32bit_ops_in_64bit_mode(void) {
    uint32_t a = 0xDEADBEEF + zero, b = 0x12345678 + zero;

    CHECK(add32(a, b) == 0xF0E21567, "add32");
    CHECK(sub32(a, b) == 0xCC796877, "sub32");
    CHECK(mul32(1000 + zero, 2000 + zero) == 2000000, "mul32");

    CHECK(shl32(1 + zero, 31) == 0x80000000, "shl32");
    CHECK(shr32(0x80000000 + zero, 31) == 1, "shr32");
    CHECK(sar32((int32_t)0x80000000 + zero, 31) == -1, "sar32");

    CHECK(and32(0xFF00FF00 + zero, 0x0F0F0F0F + zero) == 0x0F000F00, "and32");
    CHECK(or32(0xF0F0F0F0 + zero, 0x0F0F0F0F + zero) == 0xFFFFFFFF, "or32");
    CHECK(xor32(0xAAAAAAAA + zero, 0x55555555 + zero) == 0xFFFFFFFF, "xor32");
    CHECK(not32(0 + zero) == 0xFFFFFFFF, "not32");

    CHECK(bswap32(0x12345678 + zero) == 0x78563412, "bswap32");
    CHECK(neg32(42 + zero) == -42, "neg32");
}

// ---- 5. Mixed 32/64-bit operations ----

static uint64_t zext32to64(uint32_t x) { return x; }
static uint32_t trunc64to32(uint64_t x) { return (uint32_t)x; }
static int64_t  sext32to64(int32_t x) { return x; }

static void test_mixed_width(void) {
    CHECK(zext32to64(0xFFFFFFFF + zero) == 0xFFFFFFFFULL, "zext");
    CHECK(trunc64to32(0xDEADBEEFCAFEBABEULL + zero) == 0xCAFEBABE, "trunc");
    CHECK(sext32to64(-1 + zero) == -1LL, "sext -1");
    CHECK(sext32to64((int32_t)0x80000000 + zero) == (int64_t)0xFFFFFFFF80000000LL, "sext min");
}

// ---- 6. LEA (load effective address) ----
// Validates that LEA64_32r paths work correctly after classifyLEAReg fix.

static uint32_t lea_add_shift(uint32_t a, uint32_t b) {
    return a + b * 4;
}

static uint32_t lea_chain(uint32_t x) {
    return x * 3 + 7;
}

static void test_lea(void) {
    CHECK(lea_add_shift(10 + zero, 5 + zero) == 30, "lea a+b*4");
    CHECK(lea_chain(10 + zero) == 37, "lea x*3+7");
    CHECK(lea_chain(0 + zero) == 7, "lea 0*3+7");
}

// ---- 7. Pointer size verification ----

static void test_arch_invariants(void) {
    CHECK(sizeof(void *) == 8, "ptr 64-bit");
    CHECK(sizeof(size_t) == 8, "size_t 64-bit");
    CHECK(sizeof(long long) == 8, "long long 64-bit");
    CHECK(sizeof(int) == 4, "int 32-bit");
    CHECK(sizeof(short) == 2, "short 16-bit");
    CHECK(sizeof(char) == 1, "char 8-bit");
}

int main(void) {
    test_abs_lowering();
    test_sdiv_pow2();
    test_select_patterns();
    test_32bit_ops_in_64bit_mode();
    test_mixed_width();
    test_lea();
    test_arch_invariants();

    printf("test_x64_cmov_and_32bit_ops: ALL PASSED (%d tests)\n", 7);
    return 0;
}
