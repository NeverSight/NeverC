// RUN: %neverc -flto -c %S/test_inline_asm_lto_helper.c -o %t_helper.o
// RUN: %neverc -flto -c %s -o %t.o
// RUN: %neverc %t.o %t_helper.o -o %t && %t
/*
 * NeverC Compiler Validation - Inline Assembly with LTO (main TU)
 *
 * Verifies that GCC-style inline assembly survives the full LTO pipeline:
 *   .c → .bc → LTO link → executable
 *
 * This TU calls functions defined in test_inline_asm_lto_helper.c
 * which implement their logic via inline assembly.
 */

#include <stdio.h>
#include <stdlib.h>

extern int asm_add(int a, int b);
extern int asm_sub(int a, int b);
extern int asm_get_constant(void);
extern int asm_negate(int x);
extern void asm_swap(int *a, int *b);

/* Local function also uses asm, to test LTO with asm in multiple TUs */
static int local_double(int x) {
    int result;
#if defined(__x86_64__)
    __asm__("addl %1, %0" : "=r"(result) : "0"(x), "r"(x) : );
#elif defined(__aarch64__)
    __asm__("add %w0, %w1, %w1" : "=r"(result) : "r"(x));
#else
    result = x + x;
#endif
    return result;
}

int main(void) {
    if (asm_add(3, 4) != 7) abort();
    if (asm_add(-5, 5) != 0) abort();
    if (asm_add(100, 200) != 300) abort();

    if (asm_sub(10, 3) != 7) abort();
    if (asm_sub(0, 5) != -5) abort();

    if (asm_get_constant() != 42) abort();

    if (asm_negate(10) != -10) abort();
    if (asm_negate(-7) != 7) abort();
    if (asm_negate(0) != 0) abort();

    int a = 11, b = 22;
    asm_swap(&a, &b);
    if (a != 22 || b != 11) abort();

    if (local_double(5) != 10) abort();
    if (local_double(0) != 0) abort();

    printf("test_inline_asm_lto: ALL PASSED\n");
    return 0;
}
