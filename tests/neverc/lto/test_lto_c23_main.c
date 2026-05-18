// RUN: %neverc -flto -std=c23 -c %S/test_lto_c23_lib.c -o %t_lib.o
// RUN: %neverc -flto -std=c23 -c %s -o %t.o
// RUN: %neverc %t.o %t_lib.o -o %t && %t
/* LTO test with C23: main TU. Compile with -flto -std=c23, link with test_lto_c23_lib.o */
#include <stdio.h>

extern int c23_sum(int a, int b);
extern int use_nullptr_ok(void);

int main(void) {
    if (c23_sum(10, 20) != 30) return 1;
    if (use_nullptr_ok() != 0) return 2;
    printf("test_lto_c23: ALL PASSED\n");
    return 0;
}
