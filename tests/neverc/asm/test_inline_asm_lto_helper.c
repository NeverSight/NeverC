// RUN: %neverc -flto -c %s -o %t.o
/*
 * NeverC Compiler Validation - Inline Assembly with LTO (helper TU)
 *
 * Provides functions implemented via GCC-style inline asm,
 * linked with test_inline_asm_lto_main.c under -flto.
 */

int asm_add(int a, int b) {
    int result;
#if defined(__x86_64__)
    __asm__("addl %2, %0" : "=r"(result) : "0"(a), "r"(b));
#elif defined(__aarch64__)
    __asm__("add %w0, %w1, %w2" : "=r"(result) : "r"(a), "r"(b));
#else
    result = a + b;
#endif
    return result;
}

int asm_sub(int a, int b) {
    int result;
#if defined(__x86_64__)
    __asm__("subl %2, %0" : "=r"(result) : "0"(a), "r"(b));
#elif defined(__aarch64__)
    __asm__("sub %w0, %w1, %w2" : "=r"(result) : "r"(a), "r"(b));
#else
    result = a - b;
#endif
    return result;
}

int asm_get_constant(void) {
    int val;
#if defined(__x86_64__)
    __asm__("movl $42, %0" : "=r"(val));
#elif defined(__aarch64__)
    __asm__("mov %w0, #42" : "=r"(val));
#else
    val = 42;
#endif
    return val;
}

int asm_negate(int x) {
    int result;
#if defined(__x86_64__)
    __asm__("negl %0" : "=r"(result) : "0"(x));
#elif defined(__aarch64__)
    __asm__("neg %w0, %w1" : "=r"(result) : "r"(x));
#else
    result = -x;
#endif
    return result;
}

void asm_swap(int *a, int *b) {
    int tmp_a, tmp_b;
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movl (%[pa]), %[ta]\n\t"
        "movl (%[pb]), %[tb]\n\t"
        "movl %[tb], (%[pa])\n\t"
        "movl %[ta], (%[pb])"
        : [ta] "=&r"(tmp_a), [tb] "=&r"(tmp_b)
        : [pa] "r"(a), [pb] "r"(b)
        : "memory"
    );
#elif defined(__aarch64__)
    __asm__ __volatile__(
        "ldr %w[ta], [%[pa]]\n\t"
        "ldr %w[tb], [%[pb]]\n\t"
        "str %w[tb], [%[pa]]\n\t"
        "str %w[ta], [%[pb]]"
        : [ta] "=&r"(tmp_a), [tb] "=&r"(tmp_b)
        : [pa] "r"(a), [pb] "r"(b)
        : "memory"
    );
#else
    tmp_a = *a;
    *a = *b;
    *b = tmp_a;
#endif
}
