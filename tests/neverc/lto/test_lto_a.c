// NeverC test: LTO - translation unit A
// RUN: %neverc -std=c11 -flto -O2 -c %s -o %t_a.o
// Then link with test_lto_b.c
#include <stdio.h>

int add(int a, int b);
int mul(int a, int b);
extern const char *get_greeting(void);

int main(void) {
    printf("%s\n", get_greeting());
    printf("add(3,4)=%d mul(3,4)=%d\n", add(3, 4), mul(3, 4));
    return 0;
}
