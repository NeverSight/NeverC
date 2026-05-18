// RUN: %neverc -fsyntax-only %s
/*
 * NeverC Compiler Validation - GCC-style Inline Assembly
 *
 * Tests GCC extended asm syntax (__asm__) on both x86_64 and AArch64:
 *  1.  Basic asm (nop)
 *  2.  Output operands
 *  3.  Input + output operands
 *  4.  Volatile memory barrier
 *  5.  Named operands [name]
 *  6.  Multiple outputs
 *  7.  Register clobbers
 *  8.  asm goto (GCC extension)
 *  9.  Inline function with asm (cycle counter)
 * 10.  Memory operand constraint ("m")
 * 11.  Immediate constant constraint ("i")
 * 12.  Read-write ("+") constraint
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

/* 1. Basic asm — nop is valid on both x86_64 and AArch64 */
static void test_basic_asm(void) {
    __asm__ __volatile__("nop");
    __asm__ ("nop");
}

/* 2. Output operand — move an immediate into a register */
static void test_asm_output(void) {
    int result = 0;
#if defined(__x86_64__)
    __asm__("movl $42, %0" : "=r"(result));
#elif defined(__aarch64__)
    __asm__("mov %w0, #42" : "=r"(result));
#endif
    ASSERT(result == 42);
}

/* 3. Input + output — addition via asm */
static void test_asm_add(void) {
    int a = 10, b = 20, sum = 0;
#if defined(__x86_64__)
    __asm__("addl %2, %0" : "=r"(sum) : "0"(a), "r"(b));
#elif defined(__aarch64__)
    __asm__("add %w0, %w1, %w2" : "=r"(sum) : "r"(a), "r"(b));
#endif
    ASSERT(sum == 30);
}

/* 4. Volatile + memory clobber (compiler barrier) */
static void test_asm_barrier(void) {
    volatile int x = 100;
    __asm__ __volatile__("" ::: "memory");
    ASSERT(x == 100);
}

/* 5. Named operands */
static void test_asm_named(void) {
    int src = 15, dst = 0;
#if defined(__x86_64__)
    __asm__("movl %[input], %[output]"
            : [output] "=r"(dst)
            : [input] "r"(src));
#elif defined(__aarch64__)
    __asm__("mov %w[output], %w[input]"
            : [output] "=r"(dst)
            : [input] "r"(src));
#endif
    ASSERT(dst == 15);
}

/* 6. Multiple outputs */
static void test_asm_multi_output(void) {
    int lo = 0, hi = 0;
#if defined(__x86_64__)
    __asm__("movl $1, %0\n\t"
            "movl $2, %1"
            : "=r"(lo), "=r"(hi));
#elif defined(__aarch64__)
    __asm__("mov %w0, #1\n\t"
            "mov %w1, #2"
            : "=r"(lo), "=r"(hi));
#endif
    ASSERT(lo == 1);
    ASSERT(hi == 2);
}

/* 7. Register clobbers */
static void test_asm_clobbers(void) {
    int val = 0;
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movl $99, %%ecx\n\t"
        "movl %%ecx, %0"
        : "=r"(val)
        :
        : "ecx"
    );
#elif defined(__aarch64__)
    __asm__ __volatile__(
        "mov x9, #99\n\t"
        "mov %w0, w9"
        : "=r"(val)
        :
        : "x9"
    );
#endif
    ASSERT(val == 99);
}

/* 8. asm goto — label index counts after all outputs+inputs */
static void test_asm_goto(void) {
    int flag = 1;
    int reached = 0;
#if defined(__x86_64__)
    __asm__ goto(
        "testl %0, %0\n\t"
        "jnz %l1"
        :
        : "r"(flag)
        :
        : taken
    );
    ASSERT(0 && "should not reach here");
taken:
    reached = 1;
#elif defined(__aarch64__)
    __asm__ goto(
        "cbnz %w0, %l1"
        :
        : "r"(flag)
        :
        : taken_arm
    );
    ASSERT(0 && "should not reach here");
taken_arm:
    reached = 1;
#endif
    ASSERT(reached == 1);
}

/* 9. Inline function with asm — read a hardware counter */
static inline uint64_t read_counter(void) {
    uint64_t val;
#if defined(__x86_64__)
    unsigned lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    val = ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
#endif
    return val;
}

static void test_asm_inline_counter(void) {
    uint64_t c1 = read_counter();
    uint64_t c2 = read_counter();
    /* c2 >= c1 (counter is monotonic) */
    ASSERT(c2 >= c1);
}

/* 10. Memory operand constraint */
static void test_asm_memory_operand(void) {
    int value = 77;
    int loaded = 0;
#if defined(__x86_64__)
    __asm__("movl %1, %0" : "=r"(loaded) : "m"(value));
#elif defined(__aarch64__)
    __asm__("ldr %w0, %1" : "=r"(loaded) : "m"(value));
#endif
    ASSERT(loaded == 77);
}

/* 11. Immediate constant constraint */
static void test_asm_immediate(void) {
    int result = 0;
#if defined(__x86_64__)
    __asm__("movl %1, %0" : "=r"(result) : "i"(123));
#elif defined(__aarch64__)
    __asm__("mov %w0, %1" : "=r"(result) : "i"(123));
#endif
    ASSERT(result == 123);
}

/* 12. Read-write constraint ("+r") */
static void test_asm_readwrite(void) {
    int val = 50;
#if defined(__x86_64__)
    __asm__("addl $10, %0" : "+r"(val));
#elif defined(__aarch64__)
    __asm__("add %w0, %w0, #10" : "+r"(val));
#endif
    ASSERT(val == 60);
}

int main(void) {
    test_basic_asm();
    test_asm_output();
    test_asm_add();
    test_asm_barrier();
    test_asm_named();
    test_asm_multi_output();
    test_asm_clobbers();
    test_asm_goto();
    test_asm_inline_counter();
    test_asm_memory_operand();
    test_asm_immediate();
    test_asm_readwrite();

    printf("test_inline_asm_gcc: ALL PASSED\n");
    return 0;
}
