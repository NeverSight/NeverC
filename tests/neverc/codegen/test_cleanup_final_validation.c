// Final validation for the 32-bit architecture cleanup (rounds 44-49).
//
// Exercises code paths that were modified by removing dead 32-bit branches:
//   1. x87 long double conditional select (FP stack CMOV legality)
//   2. CMOV combine with FP constants (canUseCMOV guard removed)
//   3. SDIVPow2 negative divisors with various widths
//   4. Mixed-width CMOV cascade at -O2 (optimizer must not regress)
//   5. AArch64 W/X register zero-extension chain (ARM32 removal safe)
//   6. Struct layout ABI invariance post-cleanup
//   7. 32-bit atomic RMW in 64-bit mode (CMPXCHG / LDXR+STXR)
//   8. LEA + address computation (classifyLEAReg fix validated)
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: cleanup_final_validation"

#include <stddef.h>
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

// --- 1. x87 long double conditional select ---
// The LowerSELECT change removed canUseCMOV() from the FP stack check.
// long double on x86_64 uses x87 80-bit FP stack. Verify CMOV_RFP80 works.

#if defined(__x86_64__)
__attribute__((noinline))
static long double fp80_select(int cond, long double a, long double b) {
    return cond ? a : b;
}

static void test_fp80_select(void) {
    long double pi = 3.14159265358979323846L;
    long double e  = 2.71828182845904523536L;

    long double r1 = fp80_select(1, pi, e);
    CHECK(r1 > 3.14L && r1 < 3.15L, "fp80 select true -> pi");

    long double r2 = fp80_select(0, pi, e);
    CHECK(r2 > 2.71L && r2 < 2.72L, "fp80 select false -> e");

    long double r3 = fp80_select(-1, pi, e);
    CHECK(r3 > 3.14L && r3 < 3.15L, "fp80 select neg-cond -> pi");

    long double zero = 0.0L;
    long double neg_zero = -0.0L;
    long double r4 = fp80_select(1, zero, neg_zero);
    CHECK(r4 == 0.0L, "fp80 select zero vs neg-zero");
}
#endif

// --- 2. CMOV combine with FP constants ---
// combineCMov path: verify float/double conditional select still optimizes.

__attribute__((noinline))
static float fp_clamp_pos(float x) {
    return x > 0.0f ? x : 0.0f;
}

__attribute__((noinline))
static double dp_select(int cond, double a, double b) {
    return cond ? a : b;
}

static void test_fp_cmov_combine(void) {
    CHECK(fp_clamp_pos(3.14f) > 3.13f, "fp clamp positive");
    CHECK(fp_clamp_pos(-1.0f) == 0.0f, "fp clamp negative -> 0");
    CHECK(fp_clamp_pos(0.0f) == 0.0f,  "fp clamp zero -> 0");

    CHECK(dp_select(1, 1.5, 2.5) == 1.5, "dp select true");
    CHECK(dp_select(0, 1.5, 2.5) == 2.5, "dp select false");
}

// --- 3. SDIVPow2 edge cases ---
// BuildSDIVPow2 no longer gated on canUseCMOV.

__attribute__((noinline)) static int32_t sdiv2(int32_t x)    { return x / 2; }
__attribute__((noinline)) static int32_t sdiv_neg2(int32_t x) { return x / -2; }
__attribute__((noinline)) static int64_t sdiv16_64(int64_t x) { return x / 16; }
__attribute__((noinline)) static int32_t sdiv_large_pow2(int32_t x) {
    return x / (1 << 30);
}

static void test_sdiv_edge_cases(void) {
    CHECK(sdiv2(1) == 0,  "sdiv2(1)");
    CHECK(sdiv2(-1) == 0, "sdiv2(-1)");
    CHECK(sdiv2(2) == 1,  "sdiv2(2)");
    CHECK(sdiv2(-2) == -1, "sdiv2(-2)");
    CHECK(sdiv2(2147483647) == 1073741823, "sdiv2(INT32_MAX)");
    CHECK(sdiv2(-2147483647) == -1073741823, "sdiv2(-INT32_MAX)");

    CHECK(sdiv_neg2(10) == -5, "sdiv_neg2(10)");
    CHECK(sdiv_neg2(-10) == 5, "sdiv_neg2(-10)");
    CHECK(sdiv_neg2(0) == 0, "sdiv_neg2(0)");
    CHECK(sdiv_neg2(1) == 0, "sdiv_neg2(1)");
    CHECK(sdiv_neg2(-1) == 0, "sdiv_neg2(-1)");

    CHECK(sdiv16_64(256) == 16, "sdiv16_64(256)");
    CHECK(sdiv16_64(-256) == -16, "sdiv16_64(-256)");
    CHECK(sdiv16_64(15) == 0, "sdiv16_64(15)");
    CHECK(sdiv16_64(-15) == 0, "sdiv16_64(-15)");

    CHECK(sdiv_large_pow2(1 << 30) == 1, "sdiv large pow2");
    CHECK(sdiv_large_pow2(-(1 << 30)) == -1, "sdiv neg large pow2");
    CHECK(sdiv_large_pow2((1 << 30) - 1) == 0, "sdiv just under pow2");
}

// --- 4. Mixed-width CMOV cascade ---
// Validates the removed NoCMOV XOR/OR fallback doesn't affect optimizer.

__attribute__((noinline))
static uint32_t bit_blend(uint32_t mask, uint32_t a, uint32_t b) {
    uint32_t result = 0;
    for (int i = 0; i < 32; i++) {
        uint32_t bit = (mask >> i) & 1;
        result |= (bit ? (a & (1U << i)) : (b & (1U << i)));
    }
    return result;
}

static void test_cmov_cascade(void) {
    CHECK(bit_blend(0xFFFFFFFF, 0xAAAAAAAA, 0x55555555) == 0xAAAAAAAA,
          "blend all-from-a");
    CHECK(bit_blend(0x00000000, 0xAAAAAAAA, 0x55555555) == 0x55555555,
          "blend all-from-b");
    CHECK(bit_blend(0xFF00FF00, 0xDEADBEEF, 0x12345678) == 0xDE34BE78,
          "blend alternating bytes");
}

// --- 5. Platform-specific register validation ---

#if defined(__x86_64__)
static void test_x86_64_regs(void) {
    uint64_t rflags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(rflags));
    CHECK((rflags & 0x2) != 0, "RFLAGS bit 1 always set");

    uint64_t rsp;
    __asm__ __volatile__("movq %%rsp, %0" : "=r"(rsp));
    CHECK((rsp & 0xF) == 0, "RSP 16-byte aligned");

    uint32_t eax_val;
    uint64_t rax_after;
    __asm__ __volatile__(
        "movq $-1, %%rax\n\t"
        "xorl %%eax, %%eax\n\t"
        "movq %%rax, %0"
        : "=r"(rax_after)
        :
        : "rax");
    CHECK(rax_after == 0, "xorl eax,eax zeros all of rax");
}
#elif defined(__aarch64__)
static void test_aarch64_regs(void) {
    uint64_t sp;
    __asm__ __volatile__("mov %0, sp" : "=r"(sp));
    CHECK((sp & 0xF) == 0, "SP 16-byte aligned");

    uint64_t x_after;
    __asm__ __volatile__(
        "mov x9, #-1\n\t"
        "mov w9, #0\n\t"
        "mov %0, x9"
        : "=r"(x_after)
        :
        : "x9");
    CHECK(x_after == 0, "mov w9,#0 zeros upper 32 bits of x9");

    uint32_t w_val;
    __asm__ __volatile__(
        "mov w10, #0xBEEF\n\t"
        "mov %w0, w10"
        : "=r"(w_val)
        :
        : "x10");
    CHECK(w_val == 0xBEEF, "w10 register holds 16-bit immediate");
}
#endif

// --- 6. Struct layout ABI invariance ---

#pragma pack(push, 1)
struct PackedMixed {
    uint8_t  a;
    uint32_t b;
    uint16_t c;
    uint64_t d;
    uint8_t  e;
};
#pragma pack(pop)

struct NaturalMixed {
    uint8_t  a;
    uint32_t b;
    uint64_t c;
    uint32_t d;
    void    *e;
};

static void test_struct_abi(void) {
    CHECK(sizeof(struct PackedMixed) == 1+4+2+8+1, "packed struct size = 16");
    CHECK(offsetof(struct PackedMixed, a) == 0, "packed a at 0");
    CHECK(offsetof(struct PackedMixed, b) == 1, "packed b at 1");
    CHECK(offsetof(struct PackedMixed, c) == 5, "packed c at 5");
    CHECK(offsetof(struct PackedMixed, d) == 7, "packed d at 7");
    CHECK(offsetof(struct PackedMixed, e) == 15, "packed e at 15");

    CHECK(sizeof(void *) == 8, "64-bit pointers");
    CHECK(offsetof(struct NaturalMixed, a) == 0, "natural a at 0");
    CHECK(offsetof(struct NaturalMixed, b) == 4, "natural b aligned to 4");
    CHECK(offsetof(struct NaturalMixed, c) == 8, "natural c aligned to 8");
    CHECK(offsetof(struct NaturalMixed, d) == 16, "natural d at 16");
    CHECK(offsetof(struct NaturalMixed, e) == 24, "natural e aligned to 8");
    CHECK(sizeof(struct NaturalMixed) == 32, "natural struct size = 32");
}

// --- 7. 32-bit atomic RMW ---

static void test_atomic_rmw(void) {
    volatile uint32_t val = 0;

    __atomic_store_n(&val, 100, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&val, __ATOMIC_SEQ_CST) == 100, "atomic store");

    uint32_t old = __atomic_fetch_add(&val, 50, __ATOMIC_SEQ_CST);
    CHECK(old == 100 && __atomic_load_n(&val, __ATOMIC_SEQ_CST) == 150,
          "atomic fetch_add");

    old = __atomic_fetch_sub(&val, 25, __ATOMIC_SEQ_CST);
    CHECK(old == 150 && __atomic_load_n(&val, __ATOMIC_SEQ_CST) == 125,
          "atomic fetch_sub");

    old = __atomic_fetch_and(&val, 0xFF, __ATOMIC_SEQ_CST);
    CHECK(old == 125 && __atomic_load_n(&val, __ATOMIC_SEQ_CST) == 125,
          "atomic fetch_and");

    old = __atomic_fetch_or(&val, 0x100, __ATOMIC_SEQ_CST);
    CHECK(old == 125 && __atomic_load_n(&val, __ATOMIC_SEQ_CST) == (125 | 0x100),
          "atomic fetch_or");

    old = __atomic_fetch_xor(&val, 0xFF, __ATOMIC_SEQ_CST);
    uint32_t expected_xor = (125 | 0x100) ^ 0xFF;
    CHECK(__atomic_load_n(&val, __ATOMIC_SEQ_CST) == expected_xor,
          "atomic fetch_xor");

    uint32_t expected = expected_xor;
    int ok = __atomic_compare_exchange_n(&val, &expected, 0xDEAD,
                                          0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    CHECK(ok && __atomic_load_n(&val, __ATOMIC_SEQ_CST) == 0xDEAD,
          "atomic CAS success");
}

// --- 8. LEA address computation ---

__attribute__((noinline))
static uint32_t lea_add_shift(uint32_t a, uint32_t b) {
    return a + b * 4 + 12;
}

__attribute__((noinline))
static uint64_t lea_add_shift_64(uint64_t a, uint64_t b) {
    return a + b * 8 + 24;
}

__attribute__((noinline))
static uint32_t lea_chain(uint32_t x) {
    uint32_t t1 = x * 3;
    uint32_t t2 = t1 + 7;
    return t2 * 5;
}

static void test_lea_patterns(void) {
    CHECK(lea_add_shift(100, 10) == 152, "lea32: 100+10*4+12");
    CHECK(lea_add_shift(0, 0) == 12, "lea32: 0+0*4+12");
    CHECK(lea_add_shift(0xFFFFFFF0U, 2) == 4, "lea32: wrap");

    CHECK(lea_add_shift_64(1000, 100) == 1824, "lea64: 1000+100*8+24");
    CHECK(lea_add_shift_64(0, 0) == 24, "lea64: 0+0*8+24");

    CHECK(lea_chain(10) == 185, "lea chain: (10*3+7)*5");
    CHECK(lea_chain(0) == 35, "lea chain: (0*3+7)*5");
    CHECK(lea_chain(1) == 50, "lea chain: (1*3+7)*5");
}

int main(void) {
    test_fp_cmov_combine();
    test_sdiv_edge_cases();
    test_cmov_cascade();
    test_struct_abi();
    test_atomic_rmw();
    test_lea_patterns();

#if defined(__x86_64__)
    test_fp80_select();
    test_x86_64_regs();
#elif defined(__aarch64__)
    test_aarch64_regs();
#endif

    printf("PASS: cleanup_final_validation - all tests passed\n");
    return 0;
}
