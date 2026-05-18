// test_lea_and_stack_cleanup.c
// Validates correctness of LEA optimizations and stack handling after
// the 32-bit cleanup. The classifyLEAReg fix (x86_64) changed how
// LEA register classes are selected when converting ADD/SHL/INC/DEC
// to LEA form. Stack alignment is always 16 bytes (32-bit 4-byte removed).
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: lea_and_stack_cleanup"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

// --- 1. LEA as ADD replacement (SHL+ADD → LEA) ---
// classifyLEAReg ensures GR64 register class for LEA base operands.

__attribute__((noinline))
static int32_t lea_add_pattern(int32_t a, int32_t b) {
    return a + b;
}

__attribute__((noinline))
static int32_t lea_shl_add(int32_t x, int32_t y) {
    return x * 4 + y;
}

__attribute__((noinline))
static int32_t lea_shl1_add(int32_t x, int32_t y) {
    return x * 2 + y;
}

__attribute__((noinline))
static int32_t lea_shl3_add(int32_t x, int32_t y) {
    return x * 8 + y;
}

static void test_lea_patterns(void) {
    CHECK(lea_add_pattern(10, 20) == 30, "LEA add 10+20");
    CHECK(lea_add_pattern(-1, 1) == 0, "LEA add -1+1");
    CHECK(lea_add_pattern(0x7FFFFFFF, 1) == (int32_t)0x80000000, "LEA add overflow");

    CHECK(lea_shl_add(10, 5) == 45, "LEA x*4+y: 10*4+5");
    CHECK(lea_shl_add(0, 100) == 100, "LEA x*4+y: 0*4+100");
    CHECK(lea_shl_add(-1, 0) == -4, "LEA x*4+y: -1*4+0");

    CHECK(lea_shl1_add(10, 5) == 25, "LEA x*2+y: 10*2+5");
    CHECK(lea_shl1_add(-10, 20) == 0, "LEA x*2+y: -10*2+20");

    CHECK(lea_shl3_add(3, 1) == 25, "LEA x*8+y: 3*8+1");
    CHECK(lea_shl3_add(0, 0) == 0, "LEA x*8+y: 0*8+0");
}

// --- 2. LEA with 64-bit addresses ---
// LEA64_32r: 64-bit addressing → 32-bit result (truncation)
// This exercises the path that had the classifyLEAReg bug.

__attribute__((noinline))
static int32_t lea64_truncate(int64_t base, int32_t offset) {
    return (int32_t)(base + offset);
}

__attribute__((noinline))
static uint32_t array_index_32(const int32_t *arr, int idx) {
    return (uint32_t)(uintptr_t)&arr[idx];
}

static void test_lea64_patterns(void) {
    CHECK(lea64_truncate(0x100000000LL, 42) == 42,
          "LEA64 trunc: high bits discarded");
    CHECK(lea64_truncate(100, -100) == 0,
          "LEA64 trunc: 100 + (-100) = 0");

    int32_t arr[4] = {10, 20, 30, 40};
    uint32_t a0 = array_index_32(arr, 0);
    uint32_t a1 = array_index_32(arr, 1);
    CHECK(a1 - a0 == 4, "array stride is 4 bytes (sizeof(int32_t))");
}

// --- 3. INC/DEC → LEA conversion ---

__attribute__((noinline))
static int32_t inc_pattern(int32_t x) {
    return x + 1;
}

__attribute__((noinline))
static int32_t dec_pattern(int32_t x) {
    return x - 1;
}

__attribute__((noinline))
static int32_t inc_then_double(int32_t x) {
    return (x + 1) * 2;
}

static void test_inc_dec(void) {
    CHECK(inc_pattern(0) == 1, "inc 0");
    CHECK(inc_pattern(-1) == 0, "inc -1");
    CHECK(inc_pattern(0x7FFFFFFF) == (int32_t)0x80000000, "inc MAX");

    CHECK(dec_pattern(1) == 0, "dec 1");
    CHECK(dec_pattern(0) == -1, "dec 0");
    CHECK(dec_pattern((int32_t)0x80000000) == 0x7FFFFFFF, "dec MIN");

    CHECK(inc_then_double(5) == 12, "inc*2: (5+1)*2=12");
    CHECK(inc_then_double(-1) == 0, "inc*2: (-1+1)*2=0");
}

// --- 4. Stack alignment ---
// x86_64 and AArch64 both require 16-byte stack alignment.

__attribute__((noinline))
static int check_stack_alignment(void) {
    volatile char buf[1];
    buf[0] = 0;
    uintptr_t sp;
#if defined(__x86_64__)
    __asm__ volatile("movq %%rsp, %0" : "=r"(sp));
#elif defined(__aarch64__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#else
    sp = 0;
#endif
    return (sp % 16 == 0) ? 1 : 0;
}

static void test_stack_alignment(void) {
    int aligned = check_stack_alignment();
    CHECK(aligned, "stack is 16-byte aligned");
}

// --- 5. Mixed-width operations ---
// 32-bit ops in 64-bit mode: zero-extension, sign-extension, mixed LEA

__attribute__((noinline))
static uint64_t mixed_add_zext(uint32_t a, uint32_t b) {
    uint32_t sum = a + b;
    return sum;
}

__attribute__((noinline))
static int64_t mixed_add_sext(int32_t a, int32_t b) {
    int32_t sum = a + b;
    return sum;
}

__attribute__((noinline))
static uint64_t mixed_lea_zext(uint32_t x, uint32_t y) {
    uint32_t result = x * 4 + y;
    return result;
}

static void test_mixed_width(void) {
    CHECK(mixed_add_zext(0xFFFFFFFF, 1) == 0,
          "32-bit add wraps, then zero-extends to 64");
    CHECK(mixed_add_zext(100, 200) == 300,
          "32-bit add then zext");

    CHECK(mixed_add_sext(-10, 5) == -5LL,
          "32-bit add then sign-extend");
    CHECK(mixed_add_sext(0x7FFFFFFF, 1) == (int64_t)(int32_t)0x80000000,
          "32-bit overflow then sext");

    CHECK(mixed_lea_zext(10, 5) == 45,
          "32-bit LEA pattern then zext");
    CHECK(mixed_lea_zext(0x40000000, 0) == 0,
          "32-bit LEA overflow wraps then zext");
}

// --- 6. Pointer arithmetic (always 64-bit) ---

__attribute__((noinline))
static ptrdiff_t ptr_diff(const char *a, const char *b) {
    return a - b;
}

__attribute__((noinline))
static const char *ptr_advance(const char *p, int64_t n) {
    return p + n;
}

static void test_pointer_ops(void) {
    char buf[256];
    CHECK(ptr_diff(&buf[100], &buf[0]) == 100, "ptr_diff 100");
    CHECK(ptr_diff(&buf[0], &buf[100]) == -100, "ptr_diff -100");
    CHECK(ptr_advance(buf, 50) == &buf[50], "ptr_advance +50");
    CHECK(ptr_advance(&buf[50], -50) == buf, "ptr_advance -50");

    CHECK(sizeof(void *) == 8, "pointers are 64-bit");
    CHECK(sizeof(size_t) == 8, "size_t is 64-bit");
    CHECK(sizeof(ptrdiff_t) == 8, "ptrdiff_t is 64-bit");
}

// --- 7. SUB → LEA (sub r, imm → lea r, [r - imm]) ---

__attribute__((noinline))
static int32_t sub_const(int32_t x) {
    return x - 42;
}

__attribute__((noinline))
static int32_t sub_then_add(int32_t a, int32_t b) {
    return a - b + 100;
}

static void test_sub_patterns(void) {
    CHECK(sub_const(42) == 0, "sub_const 42-42");
    CHECK(sub_const(100) == 58, "sub_const 100-42");
    CHECK(sub_const(0) == -42, "sub_const 0-42");

    CHECK(sub_then_add(50, 30) == 120, "sub_then_add 50-30+100");
    CHECK(sub_then_add(0, 0) == 100, "sub_then_add 0-0+100");
}

int main(void) {
    test_lea_patterns();
    test_lea64_patterns();
    test_inc_dec();
    test_stack_alignment();
    test_mixed_width();
    test_pointer_ops();
    test_sub_patterns();

    printf("PASS: lea_and_stack_cleanup\n");
    return 0;
}
