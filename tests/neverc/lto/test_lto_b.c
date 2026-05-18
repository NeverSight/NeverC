// NeverC test: LTO - translation unit B
// RUN: %neverc -std=c11 -flto -O2 -c %s -o %t_b.o
#include <stdio.h>

int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
const char *get_greeting(void) { return "Hello from LTO!"; }
