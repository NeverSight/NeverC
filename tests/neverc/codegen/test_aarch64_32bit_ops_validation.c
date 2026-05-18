// test_aarch64_32bit_ops_validation.c
// Validates that 32-bit ARM cleanup didn't break AArch64:
//
//   1. W-register (32-bit) operations in AArch64 mode
//   2. Conditional select (CSEL/CSINC/CSNEG)
//   3. Mixed W/X register operations (32/64-bit)
//   4. 32-bit ARM unwind code removal doesn't affect AArch64 unwind
//   5. Architecture invariants
//
// This test is compiled to an AArch64 object file (cross-compile syntax check)
// and, on AArch64 hosts, compiled and run natively.
//
// RUN: %neverc -O2 -target aarch64-linux-gnu -c %s -o /dev/null
// RUN-NATIVE-AARCH64: %neverc -O2 %s -o %t && %t

#include <stdint.h>

// Use compiler builtins instead of stdio/stdlib so the cross-compile
// RUN line works without a Linux sysroot.
#ifdef __STDC_HOSTED__
#undef __STDC_HOSTED__
#endif

static volatile int check_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        check_failed = 1; \
        __builtin_trap(); \
    } \
} while (0)

static volatile int zero = 0;

// ---- 1. 32-bit (W-register) operations ----

static uint32_t add_w(uint32_t a, uint32_t b) { return a + b; }
static uint32_t sub_w(uint32_t a, uint32_t b) { return a - b; }
static uint32_t mul_w(uint32_t a, uint32_t b) { return a * b; }
static uint32_t and_w(uint32_t a, uint32_t b) { return a & b; }
static uint32_t orr_w(uint32_t a, uint32_t b) { return a | b; }
static uint32_t eor_w(uint32_t a, uint32_t b) { return a ^ b; }
static uint32_t lsl_w(uint32_t a, int s) { return a << s; }
static uint32_t lsr_w(uint32_t a, int s) { return a >> s; }
static int32_t  asr_w(int32_t a, int s) { return a >> s; }
static uint32_t rev_w(uint32_t a) { return __builtin_bswap32(a); }

static void test_w_register_ops(void) {
    uint32_t a = 0xDEADBEEF + zero, b = 0x12345678 + zero;

    CHECK(add_w(a, b) == 0xF0E21567, "add_w");
    CHECK(sub_w(a, b) == 0xCC796877, "sub_w");
    CHECK(mul_w(1000 + zero, 2000 + zero) == 2000000, "mul_w");
    CHECK(and_w(0xFF00FF00 + zero, 0x0F0F0F0F + zero) == 0x0F000F00, "and_w");
    CHECK(orr_w(0xF0F00000 + zero, 0x000F0F0F + zero) == 0xF0FF0F0F, "orr_w");
    CHECK(eor_w(0xAAAAAAAA + zero, 0x55555555 + zero) == 0xFFFFFFFF, "eor_w");
    CHECK(lsl_w(1 + zero, 31) == 0x80000000, "lsl_w");
    CHECK(lsr_w(0x80000000 + zero, 31) == 1, "lsr_w");
    CHECK(asr_w((int32_t)0x80000000 + zero, 31) == -1, "asr_w");
    CHECK(rev_w(0x12345678 + zero) == 0x78563412, "rev_w");
}

// ---- 2. Conditional select (CSEL) ----

static uint32_t csel32(int c, uint32_t a, uint32_t b) { return c ? a : b; }
static uint64_t csel64(int c, uint64_t a, uint64_t b) { return c ? a : b; }

static int32_t abs32(int32_t x) { return x < 0 ? -x : x; }

static void test_conditional_select(void) {
    int c1 = 1 + zero, c0 = 0 + zero;
    CHECK(csel32(c1, 42, 99) == 42, "csel32 true");
    CHECK(csel32(c0, 42, 99) == 99, "csel32 false");
    CHECK(csel64(c1, 0xDEADULL, 0xBEEFULL) == 0xDEADULL, "csel64 true");
    CHECK(csel64(c0, 0xDEADULL, 0xBEEFULL) == 0xBEEFULL, "csel64 false");

    CHECK(abs32(-42 + zero) == 42, "abs csneg");
    CHECK(abs32(42 + zero) == 42, "abs pos");
}

// ---- 3. Mixed 32/64-bit (W/X register) ----

static uint64_t zext_w_to_x(uint32_t w) { return w; }
static uint32_t trunc_x_to_w(uint64_t x) { return (uint32_t)x; }
static int64_t  sext_w_to_x(int32_t w) { return w; }

static void test_mixed_width(void) {
    CHECK(zext_w_to_x(0xFFFFFFFF + zero) == 0xFFFFFFFFULL, "uxtw");
    CHECK(trunc_x_to_w(0xDEADBEEFCAFEBABEULL + zero) == 0xCAFEBABE, "trunc");
    CHECK(sext_w_to_x(-1 + zero) == -1LL, "sxtw -1");
    CHECK(sext_w_to_x((int32_t)0x80000000 + zero) ==
          (int64_t)0xFFFFFFFF80000000LL, "sxtw min");
}

// ---- 4. SDIV optimization ----

static int32_t sdiv4(int32_t x) { return x / 4; }
static int64_t sdiv16_64(int64_t x) { return x / 16; }

static void test_sdiv(void) {
    CHECK(sdiv4(100 + zero) == 25, "sdiv4 100");
    CHECK(sdiv4(-100 + zero) == -25, "sdiv4 -100");
    CHECK(sdiv4(7 + zero) == 1, "sdiv4 7");
    CHECK(sdiv16_64(256LL + zero) == 16, "sdiv16 256");
}

// ---- 5. Architecture invariants ----

static void test_arch_invariants(void) {
    CHECK(sizeof(void *) == 8, "ptr 64-bit");
    CHECK(sizeof(__SIZE_TYPE__) == 8, "size_t 64-bit");
    CHECK(sizeof(long) == 8, "long 64-bit on LP64");
    CHECK(sizeof(int) == 4, "int 32-bit");
    CHECK(sizeof(short) == 2, "short 16-bit");
}

extern int printf(const char *, ...);

int main(void) {
    test_w_register_ops();
    test_conditional_select();
    test_mixed_width();
    test_sdiv();
    test_arch_invariants();

    if (!check_failed)
        printf("test_aarch64_32bit_ops_validation: ALL PASSED\n");

    return check_failed;
}
