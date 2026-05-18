// test_basic.c - Basic C compilation and linking test
// RUN: %neverc -c %s -o %t.o && echo "PASS: basic compile"
// RUN: %neverc %s -o %t && %t && echo "PASS: basic run"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

static int add(int a, int b) { return a + b; }

struct Point {
    int x, y;
};

union Data {
    int i;
    float f;
    char c[4];
};

enum Color { RED, GREEN, BLUE };

typedef struct Point Point;

static int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

static void test_types(void) {
    int8_t   v_i8  = -128;
    uint8_t  v_u8  = 255;
    int16_t  v_i16 = -32768;
    uint16_t v_u16 = 65535;
    int32_t  v_i32 = -2147483647 - 1;
    uint32_t v_u32 = 4294967295U;
    int64_t  v_i64 = -1;
    uint64_t v_u64 = 18446744073709551615ULL;

    (void)v_i8; (void)v_u8; (void)v_i16; (void)v_u16;
    (void)v_i32; (void)v_u32; (void)v_i64; (void)v_u64;

    size_t sz = sizeof(struct Point);
    ptrdiff_t pd = 0;
    (void)sz; (void)pd;
}

static void test_control_flow(void) {
    int sum = 0;
    for (int i = 0; i < 10; i++) sum += i;
    if (sum != 45) abort();

    int count = 0;
    while (count < 5) count++;
    if (count != 5) abort();

    int val = 3;
    switch (val) {
    case 1: abort(); break;
    case 2: abort(); break;
    case 3: break;
    default: abort(); break;
    }
}

static void test_pointers(void) {
    int arr[5] = {10, 20, 30, 40, 50};
    int *p = arr;
    if (*(p + 2) != 30) abort();
    if (p[4] != 50) abort();

    void *vp = arr;
    int *ip = (int *)vp;
    if (ip[0] != 10) abort();
}

static void test_strings(void) {
    const char *s = "hello";
    if (strlen(s) != 5) abort();
    if (strcmp(s, "hello") != 0) abort();

    char buf[32];
    snprintf(buf, sizeof(buf), "%d + %d = %d", 1, 2, 3);
    if (strcmp(buf, "1 + 2 = 3") != 0) abort();
}

static void test_structs(void) {
    Point p = {.x = 10, .y = 20};
    if (p.x != 10 || p.y != 20) abort();

    Point arr[3] = {{1, 2}, {3, 4}, {5, 6}};
    if (arr[2].y != 6) abort();

    union Data d;
    d.i = 42;
    if (d.i != 42) abort();
}

static void test_function_pointers(void) {
    int (*fp)(int, int) = add;
    if (fp(3, 7) != 10) abort();
}

int main(void) {
    test_types();
    test_control_flow();
    test_pointers();
    test_strings();
    test_structs();
    test_function_pointers();

    if (factorial(6) != 720) abort();
    if (add(100, 200) != 300) abort();

    printf("test_basic: ALL PASSED\n");
    return 0;
}
