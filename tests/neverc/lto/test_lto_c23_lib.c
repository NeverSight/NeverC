// RUN: %neverc -flto -std=c23 -c %s -o %t.o
/* LTO test with C23: library TU. Compile with -flto -std=c23. */
#include <stddef.h>

int c23_sum(int a, int b) {
    return a + b;
}

int use_nullptr_ok(void) {
    int *p = nullptr;
    return (p == NULL) ? 0 : 1;
}
