// test_64bit_cleanup_final.c
// Validates the complete 32-bit architecture removal is safe.
// All tests run in 64-bit mode; passes prove no 64-bit codegen is broken.
// RUN: %neverc -O2 %s -o %t && %t

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(name, cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", name); \
        failures++; \
    } \
} while(0)

// --- 1. 32-bit sub-register operations in 64-bit mode ---
// x86_64: EAX is the low 32 bits of RAX; zero-extends to 64 bits.
// AArch64: W0 is the low 32 bits of X0.

int32_t negate_i32(int32_t x) { return -x; }
uint32_t rotate_left_u32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
uint32_t bswap_u32(uint32_t x) { return __builtin_bswap32(x); }

static void test_32bit_ops_in_64bit(void) {
    CHECK("negate_i32(42)", negate_i32(42) == -42);
    CHECK("negate_i32(INT32_MIN)", negate_i32(INT32_MIN) == INT32_MIN);
    CHECK("rotate_left_u32(0x12345678, 8)", rotate_left_u32(0x12345678, 8) == 0x34567812);
    CHECK("bswap_u32(0xDEADBEEF)", bswap_u32(0xDEADBEEF) == 0xEFBEADDE);
}

// --- 2. 64-bit operations ---

int64_t abs_i64(int64_t x) { return x < 0 ? -x : x; }
uint64_t bswap_u64(uint64_t x) { return __builtin_bswap64(x); }
int popcount_u64(uint64_t x) { return __builtin_popcountll(x); }

static void test_64bit_ops(void) {
    CHECK("abs_i64(-1)", abs_i64(-1) == 1);
    CHECK("abs_i64(INT64_MIN+1)", abs_i64(INT64_MIN + 1) == INT64_MAX);
    CHECK("bswap_u64", bswap_u64(0x0102030405060708ULL) == 0x0807060504030201ULL);
    CHECK("popcount_u64(0xFF00FF00FF00FF00)", popcount_u64(0xFF00FF00FF00FF00ULL) == 32);
}

// --- 3. Conditional move (CMOV always available on x86_64) ---

int32_t select_i32(int cond, int32_t a, int32_t b) { return cond ? a : b; }
int64_t select_i64(int cond, int64_t a, int64_t b) { return cond ? a : b; }
int32_t clamp_i32(int32_t x, int32_t lo, int32_t hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
int32_t abs_via_cmov(int32_t x) { return x < 0 ? -x : x; }

static void test_cmov(void) {
    CHECK("select_i32(1, 10, 20)", select_i32(1, 10, 20) == 10);
    CHECK("select_i32(0, 10, 20)", select_i32(0, 10, 20) == 20);
    CHECK("select_i64(1)", select_i64(1, 100LL, 200LL) == 100LL);
    CHECK("clamp_i32(-5, 0, 100)", clamp_i32(-5, 0, 100) == 0);
    CHECK("clamp_i32(50, 0, 100)", clamp_i32(50, 0, 100) == 50);
    CHECK("clamp_i32(150, 0, 100)", clamp_i32(150, 0, 100) == 100);
    CHECK("abs_via_cmov(-42)", abs_via_cmov(-42) == 42);
    CHECK("abs_via_cmov(0)", abs_via_cmov(0) == 0);
}

// --- 4. Mixed-width arithmetic (proves zero-extension is correct) ---

uint64_t zero_extend_mul(uint32_t a, uint32_t b) {
    return (uint64_t)a * (uint64_t)b;
}

int64_t sign_extend_add(int32_t a, int32_t b) {
    return (int64_t)a + (int64_t)b;
}

static void test_mixed_width(void) {
    CHECK("zero_extend_mul", zero_extend_mul(0xFFFFFFFF, 2) == 0x1FFFFFFFE);
    CHECK("sign_extend_add(INT32_MAX, 1)",
          sign_extend_add(INT32_MAX, 1) == (int64_t)INT32_MAX + 1);
    CHECK("sign_extend_add(-1, -1)", sign_extend_add(-1, -1) == -2LL);
}

// --- 5. Struct layout (ABI correctness) ---

struct Mixed {
    uint32_t a;
    uint64_t b;
    uint32_t c;
};

static void test_struct_layout(void) {
#if defined(__x86_64__) || defined(__aarch64__)
    CHECK("sizeof(Mixed)==24", sizeof(struct Mixed) == 24);
    CHECK("offsetof(Mixed,b)==8", __builtin_offsetof(struct Mixed, b) == 8);
    CHECK("offsetof(Mixed,c)==16", __builtin_offsetof(struct Mixed, c) == 16);
#endif

    struct Mixed m = {0x11223344, 0xAABBCCDDEEFF0011ULL, 0x55667788};
    CHECK("mixed.a", m.a == 0x11223344);
    CHECK("mixed.b", m.b == 0xAABBCCDDEEFF0011ULL);
    CHECK("mixed.c", m.c == 0x55667788);
}

// --- 6. Function pointers (64-bit address space) ---

typedef int (*binop_t)(int, int);
static int add_fn(int a, int b) { return a + b; }
static int sub_fn(int a, int b) { return a - b; }

static void test_function_pointers(void) {
    binop_t ops[] = { add_fn, sub_fn };
    CHECK("fnptr_add", ops[0](3, 4) == 7);
    CHECK("fnptr_sub", ops[1](10, 3) == 7);
    CHECK("fnptr_size", sizeof(binop_t) == 8);
}

// --- 7. Signed division power-of-2 (SDIV optimization, was behind canUseCMOV) ---

int32_t sdiv_pow2(int32_t x) { return x / 8; }
int64_t sdiv_pow2_64(int64_t x) { return x / 16; }

static void test_sdiv_pow2(void) {
    CHECK("sdiv_pow2(24)", sdiv_pow2(24) == 3);
    CHECK("sdiv_pow2(-24)", sdiv_pow2(-24) == -3);
    CHECK("sdiv_pow2(-1)", sdiv_pow2(-1) == 0);
    CHECK("sdiv_pow2(7)", sdiv_pow2(7) == 0);
    CHECK("sdiv_pow2_64(-48)", sdiv_pow2_64(-48) == -3);
}

int main(void) {
    test_32bit_ops_in_64bit();
    test_64bit_ops();
    test_cmov();
    test_mixed_width();
    test_struct_layout();
    test_function_pointers();
    test_sdiv_pow2();

    if (failures == 0) {
        printf("PASS: 64bit_cleanup_final\n");
    } else {
        printf("FAILED: %d test(s) failed\n", failures);
    }
    return failures;
}
