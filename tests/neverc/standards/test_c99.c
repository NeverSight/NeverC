// NeverC test: C99 features
// RUN: %neverc -std=c99 -Wall -Wextra -Werror -fsyntax-only %s
// RUN: %neverc -std=gnu99 -Wall -Wextra -Werror -fsyntax-only %s
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>

// --- designated initializers ---
struct Point { int x, y, z; };
static struct Point origin = {.x = 0, .y = 0, .z = 0};
static struct Point pts[] = {
    [0] = {.x = 1, .z = 3},
    [2] = {.y = 9},
};

// --- compound literals ---
static struct Point *get_tmp(void) {
    return &(struct Point){.x = 10, .y = 20, .z = 30};
}

// --- flexible array member ---
struct DynBuf {
    size_t len;
    char data[];
};

// --- inline ---
static inline int sq(int v) { return v * v; }

// --- restrict ---
void vec_add(int *restrict dst, const int *restrict a,
             const int *restrict b, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = a[i] + b[i];
}

// --- VLA ---
double vla_sum(int n, const double a[n]) {
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

// --- _Bool ---
_Bool is_pos(int x) { return x > 0; }

// --- for-loop declaration ---
int loop_sum(void) {
    int s = 0;
    for (int i = 0; i < 10; i++) s += i;
    return s;
}

// --- variadic ---
int sum_va(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

// --- fixed-width types ---
void check_stdint(void) {
    int8_t   a = -1;   (void)a;
    uint16_t b = 0xFFFF; (void)b;
    int32_t  c = INT32_MIN; (void)c;
    uint64_t d = UINT64_MAX; (void)d;
    intptr_t e = 0;    (void)e;
    size_t   f = 0;    (void)f;
    ptrdiff_t g = 0;   (void)g;
}

// --- function pointers ---
typedef int (*BinOp)(int, int);
static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }
static BinOp ops[] = {add, mul};

// --- bit-fields ---
struct Flags {
    unsigned f1 : 1;
    unsigned f2 : 1;
    unsigned val : 6;
};

int main(void) {
    (void)origin; (void)pts; (void)get_tmp;
    (void)sq(5);
    int a[] = {1,2,3}, b[] = {4,5,6}, c[3];
    vec_add(c, a, b, 3);
    double d[] = {1.0, 2.0, 3.0};
    (void)vla_sum(3, d);
    (void)is_pos(1);
    (void)loop_sum();
    (void)sum_va(3, 10, 20, 30);
    check_stdint();
    (void)ops[0](1, 2);
    struct Flags fl = {.f1 = 1, .val = 42};
    (void)fl;
    return 0;
}
