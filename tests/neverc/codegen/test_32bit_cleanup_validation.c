// test_32bit_cleanup_validation.c
// Comprehensive validation of the 32-bit architecture cleanup round.
//
// Validates ALL affected code paths haven't broken 64-bit codegen:
//
// X86 CMOV cleanup (canUseCMOV() removal + NoCMOV predicate deletion):
//   1. ISD::ABS: i16/i32/i64 absolute value (no longer gated on canUseCMOV)
//   2. BuildSDIVPow2: power-of-2 signed division (CMOV-based select always used)
//   3. LowerSELECT: ffs-1 pattern, i8 promotion, FP conditional move
//   4. combineCMov: constant folding through CMOV nodes
//   5. CMOV_GR8 pseudo: still works (only NoCMOV GR16/GR32 removed)
//
// MC layer is64Bit() hardcoding:
//   6. 64-bit pointer/type width validation
//   7. Function pointer correctness (64-bit nlist/symtab)
//   8. Global data section alignment (64-bit segment commands)
//
// ARM32 unwind/thumb removal:
//   9. W-register (32-bit sub-register) ALU ops on AArch64/x86_64
//  10. Zero/sign extension between 32 and 64 bit
//  11. Bit manipulation (CLZ/CTZ) with 32-bit operands
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: 32bit_cleanup_validation"

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
static volatile int vol_zero = 0;
static volatile int vol_one = 1;

// ============================================================
// Section 1: CMOV-dependent ABS (was gated on canUseCMOV)
// ============================================================

__attribute__((noinline))
static int16_t abs16(int16_t x) { return x < 0 ? -x : x; }

__attribute__((noinline))
static int32_t abs32(int32_t x) { return x < 0 ? -x : x; }

__attribute__((noinline))
static int64_t abs64(int64_t x) { return x < 0 ? -x : x; }

static void test_abs(void) {
    CHECK(abs16(0) == 0, "abs16(0)");
    CHECK(abs16(100) == 100, "abs16(100)");
    CHECK(abs16(-100) == 100, "abs16(-100)");
    CHECK(abs16(-32767) == 32767, "abs16(INT16_MIN+1)");

    CHECK(abs32(0) == 0, "abs32(0)");
    CHECK(abs32(42) == 42, "abs32(42)");
    CHECK(abs32(-42) == 42, "abs32(-42)");
    CHECK(abs32(-2147483647) == 2147483647, "abs32(INT32_MIN+1)");

    CHECK(abs64(0) == 0, "abs64(0)");
    CHECK(abs64(100) == 100, "abs64(100)");
    CHECK(abs64(-100) == 100, "abs64(-100)");
    CHECK(abs64(-9223372036854775807LL) == 9223372036854775807LL,
          "abs64(INT64_MIN+1)");
}

// ============================================================
// Section 2: SDIV by power-of-2 (was gated on canUseCMOV)
// ============================================================

__attribute__((noinline))
static int32_t sdiv4_32(int32_t x) { return x / 4; }

__attribute__((noinline))
static int64_t sdiv8_64(int64_t x) { return x / 8; }

__attribute__((noinline))
static int32_t sdiv_neg16(int32_t x) { return x / -16; }

static void test_sdiv_pow2(void) {
    CHECK(sdiv4_32(100) == 25, "100/4");
    CHECK(sdiv4_32(-100) == -25, "-100/4");
    CHECK(sdiv4_32(3) == 0, "3/4 truncate");
    CHECK(sdiv4_32(-3) == 0, "-3/4 truncate toward zero");
    CHECK(sdiv4_32(0) == 0, "0/4");

    CHECK(sdiv8_64(800) == 100, "800/8");
    CHECK(sdiv8_64(-800) == -100, "-800/8");
    CHECK(sdiv8_64(7) == 0, "7/8 truncate");

    CHECK(sdiv_neg16(160) == -10, "160/-16");
    CHECK(sdiv_neg16(-160) == 10, "-160/-16");
}

// ============================================================
// Section 3: Conditional select at all widths
// ============================================================

__attribute__((noinline))
static uint8_t sel8(int c, uint8_t a, uint8_t b) { return c ? a : b; }

__attribute__((noinline))
static int16_t sel16(int c, int16_t a, int16_t b) { return c ? a : b; }

__attribute__((noinline))
static int32_t sel32(int c, int32_t a, int32_t b) { return c ? a : b; }

__attribute__((noinline))
static int64_t sel64(int c, int64_t a, int64_t b) { return c ? a : b; }

__attribute__((noinline))
static float sel_f32(int c, float a, float b) { return c ? a : b; }

__attribute__((noinline))
static double sel_f64(int c, double a, double b) { return c ? a : b; }

static void test_select(void) {
    CHECK(sel8(1, 0xAA, 0x55) == 0xAA, "sel8 true");
    CHECK(sel8(0, 0xAA, 0x55) == 0x55, "sel8 false");

    CHECK(sel16(1, 1000, 2000) == 1000, "sel16 true");
    CHECK(sel16(0, 1000, 2000) == 2000, "sel16 false");

    CHECK(sel32(vol_one, 0x12345678, 0) == 0x12345678, "sel32 true");
    CHECK(sel32(vol_zero, 0x12345678, 0xABCD) == 0xABCD, "sel32 false");

    CHECK(sel64(1, 0x123456789ABCDEF0LL, 0) == 0x123456789ABCDEF0LL,
          "sel64 true");
    CHECK(sel64(0, 0, -1LL) == -1LL, "sel64 false");

    CHECK(sel_f32(1, 3.14f, 2.71f) == 3.14f, "f32 select true");
    CHECK(sel_f32(0, 3.14f, 2.71f) == 2.71f, "f32 select false");

    CHECK(sel_f64(1, 1.23456789, 9.87654321) == 1.23456789, "f64 select true");
    CHECK(sel_f64(0, 1.23456789, 9.87654321) == 9.87654321, "f64 select false");
}

// ============================================================
// Section 4: FFS-1 pattern (CTTZ + CMOV)
// ============================================================

__attribute__((noinline))
static int ffs_minus1_32(uint32_t x) { return x ? __builtin_ctz(x) : -1; }

__attribute__((noinline))
static int ffs_minus1_64(uint64_t x) { return x ? __builtin_ctzll(x) : -1; }

static void test_ffs(void) {
    CHECK(ffs_minus1_32(0) == -1, "ffs32(0)");
    CHECK(ffs_minus1_32(1) == 0, "ffs32(1)");
    CHECK(ffs_minus1_32(0x80) == 7, "ffs32(0x80)");
    CHECK(ffs_minus1_32(0x80000000U) == 31, "ffs32(1<<31)");

    CHECK(ffs_minus1_64(0) == -1, "ffs64(0)");
    CHECK(ffs_minus1_64(1) == 0, "ffs64(1)");
    CHECK(ffs_minus1_64(0x100000000ULL) == 32, "ffs64(1<<32)");
}

// ============================================================
// Section 5: min/max/clamp (CMOV chains)
// ============================================================

__attribute__((noinline))
static int32_t smin(int32_t a, int32_t b) { return a < b ? a : b; }

__attribute__((noinline))
static int32_t smax(int32_t a, int32_t b) { return a > b ? a : b; }

__attribute__((noinline))
static int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

__attribute__((noinline))
static int32_t smin_zero(int32_t x) { return x < 0 ? x : 0; }

__attribute__((noinline))
static int32_t smax_zero(int32_t x) { return x > 0 ? x : 0; }

static void test_minmax_clamp(void) {
    CHECK(smin(5, 10) == 5, "smin(5,10)");
    CHECK(smin(-5, 5) == -5, "smin(-5,5)");
    CHECK(smax(5, 10) == 10, "smax(5,10)");
    CHECK(smax(-5, 5) == 5, "smax(-5,5)");

    CHECK(clamp(50, 0, 100) == 50, "clamp mid");
    CHECK(clamp(-10, 0, 100) == 0, "clamp lo");
    CHECK(clamp(200, 0, 100) == 100, "clamp hi");

    CHECK(smin_zero(10) == 0, "smin_zero(10)");
    CHECK(smin_zero(-10) == -10, "smin_zero(-10)");
    CHECK(smax_zero(10) == 10, "smax_zero(10)");
    CHECK(smax_zero(-10) == 0, "smax_zero(-10)");
}

// ============================================================
// Section 6: 64-bit type widths (MC layer is64Bit hardcoding)
// ============================================================

static void test_type_widths(void) {
    CHECK(sizeof(char) == 1, "char 1B");
    CHECK(sizeof(short) == 2, "short 2B");
    CHECK(sizeof(int) == 4, "int 4B");
    CHECK(sizeof(long long) == 8, "ll 8B");
    CHECK(sizeof(void *) == 8, "ptr 8B");
    CHECK(sizeof(size_t) == 8, "size_t 8B");
    CHECK(_Alignof(void *) == 8, "ptr align 8");
}

// ============================================================
// Section 7: 32-bit sub-register ops in 64-bit mode
// ============================================================

__attribute__((noinline))
static uint32_t add32(uint32_t a, uint32_t b) { return a + b; }

__attribute__((noinline))
static uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

__attribute__((noinline))
static uint64_t zext32(uint32_t x) { return x; }

__attribute__((noinline))
static int64_t sext32(int32_t x) { return x; }

__attribute__((noinline))
static uint32_t trunc64(uint64_t x) { return (uint32_t)x; }

static void test_subreg_ops(void) {
    CHECK(add32(0xFFFFFFFF, 1) == 0, "w add overflow");
    CHECK(rotl32(0x80000001, 1) == 3, "rotl32");
    CHECK(zext32(0xDEADBEEF) == 0xDEADBEEFULL, "zext");
    CHECK(sext32(-1) == -1LL, "sext -1");
    CHECK(sext32(-2147483648) == -2147483648LL, "sext INT32_MIN");
    CHECK(trunc64(0xDEADBEEFCAFEBABEULL) == 0xCAFEBABEU, "trunc64");
}

// ============================================================
// Section 8: Bit manipulation with 32-bit operands
// ============================================================

__attribute__((noinline))
static int clz32(uint32_t x) { return __builtin_clz(x); }

__attribute__((noinline))
static int ctz32(uint32_t x) { return __builtin_ctz(x); }

__attribute__((noinline))
static int popcount32(uint32_t x) { return __builtin_popcount(x); }

__attribute__((noinline))
static int clz64(uint64_t x) { return __builtin_clzll(x); }

static void test_bit_manip(void) {
    CHECK(clz32(1) == 31, "clz32(1)");
    CHECK(clz32(0x80000000U) == 0, "clz32(MSB)");
    CHECK(ctz32(0x80000000U) == 31, "ctz32(MSB)");
    CHECK(ctz32(4) == 2, "ctz32(4)");
    CHECK(popcount32(0xFF00FF00) == 16, "popcount32");
    CHECK(clz64(1) == 63, "clz64(1)");
}

// ============================================================
// Section 9: Struct layout and alignment (64-bit ABI)
// ============================================================

struct packed_test {
    uint8_t  a;
    uint32_t b;
    uint64_t c;
    uint16_t d;
} __attribute__((packed));

struct aligned_test {
    uint8_t  a;
    uint32_t b;
    uint64_t c;
    uint16_t d;
};

__attribute__((noinline))
static uint64_t hash_packed(const struct packed_test *p) {
    return (uint64_t)p->a ^ (uint64_t)p->b ^ p->c ^ (uint64_t)p->d;
}

static void test_struct_layout(void) {
    CHECK(sizeof(struct packed_test) == 15, "packed size 15");
    CHECK(sizeof(struct aligned_test) == 24, "aligned size 24");

    struct packed_test p = { 0x11, 0xDEADBEEF, 0xCAFEBABE12345678ULL, 0xABCD };
    uint64_t h = hash_packed(&p);
    uint64_t expected = 0x11ULL ^ 0xDEADBEEFULL ^ 0xCAFEBABE12345678ULL ^ 0xABCDULL;
    CHECK(h == expected, "packed struct hash");
}

// ============================================================
// Section 10: Function pointers (validates 64-bit symtab/nlist)
// ============================================================

__attribute__((noinline))
static int fn_add(int a, int b) { return a + b; }

__attribute__((noinline))
static int fn_sub(int a, int b) { return a - b; }

typedef int (*binop_fn)(int, int);

__attribute__((noinline))
static int apply(binop_fn f, int a, int b) { return f(a, b); }

static void test_func_ptrs(void) {
    CHECK(sizeof(binop_fn) == 8, "func ptr 8B");
    CHECK(apply(fn_add, 10, 20) == 30, "indirect add");
    CHECK(apply(fn_sub, 20, 10) == 10, "indirect sub");

    volatile binop_fn fns[] = { fn_add, fn_sub };
    CHECK(fns[vol_zero](3, 4) == 7, "array[0] add");
    CHECK(fns[vol_one](10, 3) == 7, "array[1] sub");
}

int main(void) {
    test_abs();
    test_sdiv_pow2();
    test_select();
    test_ffs();
    test_minmax_clamp();
    test_type_widths();
    test_subreg_ops();
    test_bit_manip();
    test_struct_layout();
    test_func_ptrs();

    printf("PASS: 32bit_cleanup_validation - all %d sections passed\n", 10);
    return 0;
}
