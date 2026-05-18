// RUN: %neverc -std=gnu11 -Wall -Wextra -Werror -c %s -o %t.gnu11.o
// RUN: test -s %t.gnu11.o
// RUN: %neverc -std=gnu17 -Wall -Werror -c %s -o %t.gnu17.o
// RUN: test -s %t.gnu17.o
// RUN: %neverc -std=gnu23 -Wall -Werror -c %s -o %t.gnu23.o
// RUN: test -s %t.gnu23.o
// RUN: %neverc -std=gnu11 -O2 -c %s -o %t.opt.o
// RUN: test -s %t.opt.o
// RUN: %neverc -std=gnu11 -O3 -c %s -o %t.opt3.o
// RUN: test -s %t.opt3.o
// RUN: %neverc -std=gnu11 -Os -c %s -o %t.opts.o
// RUN: test -s %t.opts.o
// RUN: %neverc -std=gnu11 -flto -c %s -o %t.lto.o
// RUN: test -s %t.lto.o
// RUN: %neverc -target x86_64-apple-darwin -std=gnu11 -c %s -o %t.x64.o
// RUN: test -s %t.x64.o

// ============================================================
// Comprehensive C language feature validation for NeverC
// ============================================================

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <float.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stdnoreturn.h>

// --- Basic types ---
void test_basic_types(void) {
    char c = 'A';
    signed char sc = -1;
    unsigned char uc = 255;
    short s = -32768;
    unsigned short us = 65535;
    int i = -1;
    unsigned int ui = 42u;
    long l = 100L;
    unsigned long ul = 200UL;
    long long ll = -1LL;
    unsigned long long ull = 1ULL;
    float f = 3.14f;
    double d = 2.718281828;
    long double ld = 1.0L;
    _Bool b = 1;
    bool b2 = true;
    (void)c; (void)sc; (void)uc; (void)s; (void)us;
    (void)i; (void)ui; (void)l; (void)ul;
    (void)ll; (void)ull; (void)f; (void)d; (void)ld;
    (void)b; (void)b2;
}

// --- Fixed-width integer types ---
void test_fixed_width(void) {
    int8_t v_i8 = INT8_MIN;
    int16_t v_i16 = INT16_MAX;
    int32_t v_i32 = INT32_MIN;
    int64_t v_i64 = INT64_MAX;
    uint8_t v_u8 = UINT8_MAX;
    uint16_t v_u16 = UINT16_MAX;
    uint32_t v_u32 = UINT32_MAX;
    uint64_t v_u64 = UINT64_MAX;
    intptr_t ip = 0;
    uintptr_t up = 0;
    size_t sz = sizeof(int);
    ptrdiff_t pd = 0;
    (void)v_i8; (void)v_i16; (void)v_i32; (void)v_i64;
    (void)v_u8; (void)v_u16; (void)v_u32; (void)v_u64;
    (void)ip; (void)up; (void)sz; (void)pd;
}

// --- Enum ---
enum Color { RED = 0, GREEN = 1, BLUE = 2 };
enum Flags { FLAG_A = 1 << 0, FLAG_B = 1 << 1, FLAG_C = 1 << 2 };

// --- Struct / Union / Bitfield ---
struct Point { int x, y; };
struct Nested { struct Point p; int z; };
struct Packed { char a; int b; } __attribute__((packed));
struct BitField { unsigned int a : 3; unsigned int b : 5; unsigned int c : 24; };
union Variant { int i; float f; char bytes[4]; };

void test_struct_union(void) {
    struct Point p = {10, 20};
    struct Nested n = {{1, 2}, 3};
    struct Packed pk = {'x', 42};
    struct BitField bf = {7, 31, 0};
    union Variant v;
    v.i = 0x12345678;
    (void)p; (void)n; (void)pk; (void)bf; (void)v;
}

// --- Designated initializers ---
void test_designated_init(void) {
    struct Point p = { .y = 20, .x = 10 };
    int arr[5] = { [2] = 99, [4] = 77 };
    (void)p; (void)arr;
}

// --- Compound literals (C99) ---
void test_compound_literals(void) {
    struct Point *pp = &(struct Point){100, 200};
    int *ip = (int[]){1, 2, 3, 4, 5};
    (void)pp; (void)ip;
}

// --- Flexible array member ---
struct FlexArray {
    int count;
    int data[];
};

// --- Anonymous struct/union (C11) ---
struct AnonMember {
    int tag;
    union {
        int ival;
        float fval;
    };
};
void test_anon_member(void) {
    struct AnonMember am = {.tag = 1, .ival = 42};
    (void)am;
}

// --- Function pointers and callbacks ---
typedef int (*BinOp)(int, int);
static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }
void test_funcptr(void) {
    BinOp ops[] = {add, mul};
    int r = ops[0](3, 4) + ops[1](3, 4);
    (void)r;
}

// --- Variadic functions ---
static int sum_args(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int total = 0;
    for (int i = 0; i < count; i++)
        total += va_arg(ap, int);
    va_end(ap);
    return total;
}
void test_variadic(void) {
    int s = sum_args(3, 10, 20, 30);
    (void)s;
}

// --- Static assert (C11) ---
_Static_assert(sizeof(int) >= 4, "int must be at least 32 bits");
_Static_assert(sizeof(char) == 1, "char must be 1 byte");

// --- Alignas / Alignof (C11) ---
void test_alignment(void) {
    _Alignas(16) int aligned_arr[4];
    size_t align = _Alignof(double);
    (void)aligned_arr; (void)align;
}

// --- Generic selection (C11) ---
#define type_name(x) _Generic((x), \
    int: "int", \
    float: "float", \
    double: "double", \
    default: "other")
void test_generic_selection(void) {
    const char *n1 = type_name(42);
    const char *n2 = type_name(3.14f);
    const char *n3 = type_name(2.718);
    (void)n1; (void)n2; (void)n3;
}

// --- Atomics (C11) ---
void test_atomics(void) {
    _Atomic int ai = 0;
    atomic_store(&ai, 42);
    int val = atomic_load(&ai);
    int old = 42;
    atomic_compare_exchange_strong(&ai, &old, 100);
    int prev = atomic_fetch_add(&ai, 1);
    (void)val; (void)prev;
}

// --- typeof / __auto_type (GNU extensions) ---
void test_typeof(void) {
    int x = 42;
    __typeof__(x) y = x + 1;
    __auto_type z = x * 2;
    (void)y; (void)z;
}

// --- Statement expressions (GNU) ---
void test_stmt_expr(void) {
    int m = ({
        int a = 10, b = 20;
        a > b ? a : b;
    });
    (void)m;
}

// --- Attribute handling ---
__attribute__((unused)) static int unused_var = 0;
__attribute__((warn_unused_result)) static int must_check(void) { return 0; }
__attribute__((const)) static int pure_func(int x) { return x * x; }
static int deprecated_func(void) __attribute__((deprecated));
static int deprecated_func(void) { return 0; }
__attribute__((always_inline)) static inline int force_inline(int a) { return a + 1; }
__attribute__((noinline)) static int no_inline(int a) { return a + 2; }

void test_attributes(void) {
    (void)must_check();
    int r1 = pure_func(5);
    int r2 = force_inline(10);
    int r3 = no_inline(20);
    (void)r1; (void)r2; (void)r3;
}

// --- Computed goto (GNU) ---
void test_computed_goto(void) {
    void *labels[] = {&&L1, &&L2, &&L3};
    int idx = 1;
    goto *labels[idx];
L1: idx = 10; goto done;
L2: idx = 20; goto done;
L3: idx = 30; goto done;
done:
    (void)idx;
}

// --- VLA (variable length arrays) ---
void test_vla(int n) {
    int vla[n];
    for (int i = 0; i < n; i++)
        vla[i] = i * i;
    (void)vla;
}

// --- Restrict qualifier ---
void test_restrict(int *restrict a, int *restrict b, int n) {
    for (int i = 0; i < n; i++)
        a[i] += b[i];
}

// --- Complex arithmetic (C99) ---
#include <complex.h>
void test_complex(void) {
    double complex z1 = 1.0 + 2.0 * I;
    double complex z2 = 3.0 - 1.0 * I;
    double complex sum = z1 + z2;
    double complex prod = z1 * z2;
    double re = creal(sum);
    double im = cimag(prod);
    (void)re; (void)im;
}

// --- Inline functions ---
static inline int inline_max(int a, int b) { return a > b ? a : b; }
void test_inline(void) {
    int m = inline_max(3, 7);
    (void)m;
}

// --- Recursive struct (linked list) ---
struct ListNode {
    int value;
    struct ListNode *next;
};

// --- Forward declarations ---
struct ForwardDecl;
struct ForwardDecl { int x; };

// --- Pointer arithmetic ---
void test_pointers(void) {
    int arr[] = {10, 20, 30, 40, 50};
    int *p = arr;
    int *end = arr + 5;
    int sum = 0;
    while (p < end)
        sum += *p++;
    ptrdiff_t diff = end - arr;
    (void)sum; (void)diff;
}

// --- String literals and escape sequences ---
void test_strings(void) {
    const char *s1 = "hello world";
    const char *s2 = "escape: \t\n\r\0\\\"";
    const char *s3 = "concat" "enation";
    const char *s4 = u8"UTF-8 string";
    (void)s1; (void)s2; (void)s3; (void)s4;
}

// --- Preprocessor features ---
#define STRINGIFY(x) #x
#define CONCAT(a, b) a##b
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void test_preprocessor(void) {
    const char *s = STRINGIFY(hello);
    int CONCAT(my, Var) = 42;
    int m = MAX(3, 7);
    (void)s; (void)myVar; (void)m;
}

// --- Bit manipulation ---
void test_bits(void) {
    unsigned x = 0xDEADBEEF;
    int pop = __builtin_popcount(x);
    int clz = __builtin_clz(x);
    int ctz = __builtin_ctz(x);
    unsigned bswap = __builtin_bswap32(x);
    (void)pop; (void)clz; (void)ctz; (void)bswap;
}

// --- Builtins ---
void test_builtins(int cond) {
    if (__builtin_expect(cond, 1)) {
        (void)0;
    }
    size_t s = __builtin_offsetof(struct Point, y);
    (void)s;
}

// --- Blocks (Apple extension) ---
#ifdef __BLOCKS__
void test_blocks(void) {
    int (^square)(int) = ^(int x) { return x * x; };
    int r = square(5);
    (void)r;
}
#endif

// --- noreturn (C11) ---
noreturn void test_noreturn(void) {
    __builtin_abort();
}

// --- Entry point ---
int main(void) {
    test_basic_types();
    test_fixed_width();
    test_struct_union();
    test_designated_init();
    test_compound_literals();
    test_anon_member();
    test_funcptr();
    test_variadic();
    test_alignment();
    test_generic_selection();
    test_atomics();
    test_typeof();
    test_stmt_expr();
    test_attributes();
    test_computed_goto();
    test_vla(10);
    test_pointers();
    test_strings();
    test_preprocessor();
    test_bits();
    test_complex();
    test_inline();
    test_preprocessor();
    return 0;
}
