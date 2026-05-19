// LTO compilation test
// Tests that full LTO pipeline works: .c -> .bc -> link -> executable
// RUN: %neverc -flto -c %s -o %t.o && echo "PASS: lto compile"
// RUN: %neverc -flto -c %S/test_lto_helper.c -o %t_helper.o && echo "PASS: lto helper compile"
// RUN: %neverc %t.o %t_helper.o -o %t && echo "PASS: lto link"

#include <stdio.h>
#include <stdlib.h>

extern int helper_add(int a, int b);
extern int helper_mul(int a, int b);
extern int helper_square(int x);

static inline int local_add(int a, int b) {
    return a + b;
}

int main(void) {
    if (helper_add(10, 20) != 30) abort();
    if (helper_mul(6, 7) != 42) abort();
    if (helper_square(5) != 25) abort();
    if (local_add(100, 200) != 300) abort();

    printf("test_lto: ALL PASSED\n");
    return 0;
}
