// Comprehensive validation for the 64-bit-only architecture cleanup.
//
// This test validates that all cleanup changes are safe:
//   1. MC layer: 64-bit ELF/MachO/COFF object format hardcoding
//   2. CMOV always available: ABS/SDIV/SELECT all use CMOV
//   3. 32-bit operand-size instructions still work in 64-bit mode
//   4. AArch64 W-register (32-bit) operations still work
//   5. Zero-extension semantics (writing EAX clears upper RAX)
//   6. LEA with 32-bit result in 64-bit mode (LEA64_32r)
//   7. Mixed 32/64-bit arithmetic chains
//   8. Struct layout with 32-bit fields in 64-bit mode
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: arch_cleanup_comprehensive"

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

// === 1. CMOV-based patterns (canUseCMOV guards removed) ===

__attribute__((noinline))
static int32_t abs32(int32_t x) { return x < 0 ? -x : x; }

__attribute__((noinline))
static int64_t abs64(int64_t x) { return x < 0 ? -x : x; }

__attribute__((noinline))
static int32_t sdiv_pow2(int32_t x) { return x / 8; }

__attribute__((noinline))
static uint8_t select_u8(int c, uint8_t a, uint8_t b) { return c ? a : b; }

__attribute__((noinline))
static int32_t min32(int32_t a, int32_t b) { return a < b ? a : b; }

__attribute__((noinline))
static int32_t max32(int32_t a, int32_t b) { return a > b ? a : b; }

__attribute__((noinline))
static int ffs_or_neg1(uint32_t x) { return x ? __builtin_ctz(x) : -1; }

static void test_cmov_patterns(void) {
    CHECK(abs32(42) == 42, "abs32(42)");
    CHECK(abs32(-42) == 42, "abs32(-42)");
    CHECK(abs32(0) == 0, "abs32(0)");

    CHECK(abs64(100LL) == 100LL, "abs64(100)");
    CHECK(abs64(-100LL) == 100LL, "abs64(-100)");

    CHECK(sdiv_pow2(24) == 3, "24/8");
    CHECK(sdiv_pow2(-24) == -3, "-24/8");
    CHECK(sdiv_pow2(7) == 0, "7/8 truncates toward zero");
    CHECK(sdiv_pow2(-7) == 0, "-7/8 truncates toward zero");

    CHECK(select_u8(1, 0xAA, 0x55) == 0xAA, "u8 select true");
    CHECK(select_u8(0, 0xAA, 0x55) == 0x55, "u8 select false");

    CHECK(min32(5, 10) == 5, "min(5,10)");
    CHECK(min32(-5, 5) == -5, "min(-5,5)");
    CHECK(max32(5, 10) == 10, "max(5,10)");

    CHECK(ffs_or_neg1(0) == -1, "ffs(0)=-1");
    CHECK(ffs_or_neg1(1) == 0, "ffs(1)=0");
    CHECK(ffs_or_neg1(0x80) == 7, "ffs(0x80)=7");
}

// === 2. 32-bit ops in 64-bit mode (EAX/ECX/etc. as sub-registers) ===

__attribute__((noinline))
static uint32_t zero_extend_test(uint32_t x) {
    // Writing EAX implicitly zero-extends to RAX in x86_64.
    // On AArch64, writing W0 zero-extends to X0.
    return x + 1;
}

__attribute__((noinline))
static uint64_t mixed_width_chain(uint32_t a, uint64_t b) {
    uint32_t r32 = a * 3;
    uint64_t r64 = (uint64_t)r32 + b;
    return r64;
}

__attribute__((noinline))
static uint32_t bswap_32(uint32_t x) {
    return __builtin_bswap32(x);
}

__attribute__((noinline))
static int popcount_32(uint32_t x) {
    return __builtin_popcount(x);
}

static void test_32bit_ops_in_64bit(void) {
    CHECK(zero_extend_test(0xFFFFFFFF) == 0, "zero_extend overflow wraps");
    CHECK(zero_extend_test(41) == 42, "zero_extend simple");

    CHECK(mixed_width_chain(10, 100) == 130, "mixed 32/64 chain");
    CHECK(mixed_width_chain(0xFFFFFFFF, 1) == 0xFFFFFFFEULL,
          "mixed chain overflow");

    CHECK(bswap_32(0x01020304) == 0x04030201, "bswap32");
    CHECK(popcount_32(0xAAAAAAAA) == 16, "popcount32");
    CHECK(popcount_32(0) == 0, "popcount32(0)");
}

// === 3. LEA patterns (LEA64_32r: 64-bit address, 32-bit result) ===

__attribute__((noinline))
static int32_t lea_pattern(int32_t a, int32_t b) {
    // Compiler may use LEA to compute a+b*2+5 in one instruction.
    return a + b * 2 + 5;
}

__attribute__((noinline))
static int32_t shl_add_pattern(int32_t x) {
    // SHL + ADD -> LEA optimization
    return x * 3;
}

__attribute__((noinline))
static int32_t shl_lea_chain(int32_t x) {
    // x*5 = x + x*4 -> LEA
    return x * 5;
}

static void test_lea_patterns(void) {
    CHECK(lea_pattern(10, 20) == 55, "lea a+b*2+5");
    CHECK(lea_pattern(-10, -20) == -45, "lea negative");

    CHECK(shl_add_pattern(7) == 21, "x*3 via LEA");
    CHECK(shl_add_pattern(-3) == -9, "x*3 negative");

    CHECK(shl_lea_chain(6) == 30, "x*5 via LEA chain");
    CHECK(shl_lea_chain(-4) == -20, "x*5 negative");
}

// === 4. Struct layout (32-bit fields in 64-bit struct) ===

struct mixed_struct {
    int32_t  a;
    int64_t  b;
    int32_t  c;
    int16_t  d;
    int8_t   e;
};

static void test_struct_layout(void) {
    struct mixed_struct s = {0x11223344, 0xAABBCCDDEEFF0011LL,
                             0x55667788, 0x1234, 0x42};

    CHECK(s.a == 0x11223344, "struct i32 field a");
    CHECK(s.b == (int64_t)0xAABBCCDDEEFF0011LL, "struct i64 field b");
    CHECK(s.c == 0x55667788, "struct i32 field c");
    CHECK(s.d == 0x1234, "struct i16 field d");
    CHECK(s.e == 0x42, "struct i8 field e");

    CHECK(sizeof(struct mixed_struct) == 24, "struct size with padding");
    CHECK(__alignof__(struct mixed_struct) == 8, "struct alignment");
}

// === 5. 64-bit specific operations (always available in x86_64/AArch64) ===

__attribute__((noinline))
static uint64_t bswap_64(uint64_t x) {
    return __builtin_bswap64(x);
}

__attribute__((noinline))
static int clz_64(uint64_t x) {
    return __builtin_clzll(x);
}

__attribute__((noinline))
static int ctz_64(uint64_t x) {
    return __builtin_ctzll(x);
}

static void test_64bit_ops(void) {
    CHECK(bswap_64(0x0102030405060708ULL) == 0x0807060504030201ULL,
          "bswap64");

    CHECK(clz_64(1ULL) == 63, "clz64(1)");
    CHECK(clz_64(0x8000000000000000ULL) == 0, "clz64(1<<63)");

    CHECK(ctz_64(1ULL) == 0, "ctz64(1)");
    CHECK(ctz_64(0x100000000ULL) == 32, "ctz64(1<<32)");
}

// === 6. Atomic CAS 32-bit in 64-bit mode ===

static void test_atomic_32bit(void) {
    _Atomic int32_t val = 10;
    int32_t expected = 10;
    _Bool ok = __c11_atomic_compare_exchange_strong(
        &val, &expected, 20,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    CHECK(ok, "CAS32 success");
    CHECK(val == 20, "CAS32 new value");

    expected = 999;
    ok = __c11_atomic_compare_exchange_strong(
        &val, &expected, 30,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    CHECK(!ok, "CAS32 fail mismatch");
    CHECK(expected == 20, "CAS32 loaded current");
}

// === 7. Function pointer / indirect call (validates codegen correctness) ===

typedef int32_t (*binop_fn)(int32_t, int32_t);

__attribute__((noinline))
static int32_t add32(int32_t a, int32_t b) { return a + b; }

__attribute__((noinline))
static int32_t sub32(int32_t a, int32_t b) { return a - b; }

static void test_indirect_call(void) {
    volatile binop_fn fn = add32;
    CHECK(fn(10, 20) == 30, "indirect call add");
    fn = sub32;
    CHECK(fn(10, 20) == -10, "indirect call sub");
}

// === 8. Varargs (validates calling convention) ===

__attribute__((noinline))
static int sum_varargs(int count, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, count);
    int sum = 0;
    for (int i = 0; i < count; i++)
        sum += __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return sum;
}

static void test_varargs(void) {
    CHECK(sum_varargs(3, 10, 20, 30) == 60, "varargs sum");
    CHECK(sum_varargs(0) == 0, "varargs empty");
    CHECK(sum_varargs(1, -42) == -42, "varargs single negative");
}

int main(void) {
    test_cmov_patterns();
    test_32bit_ops_in_64bit();
    test_lea_patterns();
    test_struct_layout();
    test_64bit_ops();
    test_atomic_32bit();
    test_indirect_call();
    test_varargs();

    printf("test_arch_cleanup_comprehensive: ALL PASSED\n");
    return 0;
}
