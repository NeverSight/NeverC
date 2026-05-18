// test_advanced_c.c - Advanced C features (VLA, designated init, compound literals, etc.)
// RUN: %neverc -std=c11 -c %s -o %t.o && echo "PASS: advanced_c compile"
// RUN: %neverc -std=c11 %s -o %t && %t && echo "PASS: advanced_c run"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_designated_init(void) {
    struct Config {
        int width;
        int height;
        const char *name;
        int flags[4];
    };

    struct Config c = {
        .name = "test",
        .height = 480,
        .width = 640,
        .flags = {[0] = 1, [3] = 1},
    };

    if (c.width != 640) abort();
    if (c.height != 480) abort();
    if (strcmp(c.name, "test") != 0) abort();
    if (c.flags[0] != 1 || c.flags[1] != 0 || c.flags[3] != 1) abort();
}

static void test_flexible_array(void) {
    struct Buffer {
        int length;
        char data[];
    };

    struct Buffer *buf = malloc(sizeof(struct Buffer) + 16);
    buf->length = 16;
    memset(buf->data, 'A', 16);
    if (buf->data[0] != 'A') abort();
    if (buf->data[15] != 'A') abort();
    free(buf);
}

static void test_vla(void) {
    int n = 10;
    int arr[n];
    for (int i = 0; i < n; i++) arr[i] = i * i;
    if (arr[3] != 9) abort();
    if (arr[9] != 81) abort();
}

static void test_compound_literals_advanced(void) {
    struct Point { int x, y; };

    struct Point *points = (struct Point[]){
        {1, 2}, {3, 4}, {5, 6}
    };
    if (points[1].x != 3) abort();
    if (points[2].y != 6) abort();

    int sum = 0;
    for (const int *p = (const int[]){10, 20, 30, 40}; p < (const int[]){10, 20, 30, 40} + 4; p++)
        sum += *p;
}

typedef int (*BinOp)(int, int);
static int add_fn(int a, int b) { return a + b; }
static int mul_fn(int a, int b) { return a * b; }

static void test_complex_types(void) {
    BinOp ops[] = {add_fn, mul_fn};
    if (ops[0](3, 4) != 7) abort();
    if (ops[1](3, 4) != 12) abort();
}

static void test_bitfields(void) {
    struct Packed {
        unsigned int a : 3;
        unsigned int b : 5;
        unsigned int c : 8;
    };

    struct Packed p = {.a = 7, .b = 31, .c = 255};
    if (p.a != 7) abort();
    if (p.b != 31) abort();
    if (p.c != 255) abort();
}

static int apply(int (*fn)(int), int val) {
    return fn(val);
}

static int double_it(int x) { return x * 2; }
static int negate_it(int x) { return -x; }

static void test_higher_order(void) {
    if (apply(double_it, 5) != 10) abort();
    if (apply(negate_it, 5) != -5) abort();
}

int main(void) {
    test_designated_init();
    test_flexible_array();
    test_vla();
    test_compound_literals_advanced();
    test_complex_types();
    test_bitfields();
    test_higher_order();

    printf("test_advanced_c: ALL PASSED\n");
    return 0;
}
