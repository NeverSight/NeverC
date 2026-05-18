// RUN: %neverc -std=c11 -fsyntax-only %s
/*
 * NeverC Compiler Validation - C11 Atomics, Thread-local, Alignment, Noreturn
 *
 * Tests C11 features that are commonly used in systems code:
 *  1.  _Atomic qualifier and <stdatomic.h> operations
 *  2.  _Thread_local storage class
 *  3.  _Alignas / _Alignof
 *  4.  _Noreturn function specifier
 *  5.  _Complex type
 *  6.  restrict pointer qualifier (C99)
 *  7.  VLA (Variable Length Arrays)
 *  8.  _Generic with more complex associations
 *  9.  Anonymous structs/unions (C11)
 * 10.  _Static_assert in struct/function scope
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stdnoreturn.h>
#include <complex.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

/* ================================================================
 * 1. _Atomic qualifier and atomic operations
 * ================================================================ */
static int test_atomics(void) {
    _Atomic int counter = 0;
    atomic_store(&counter, 42);
    ASSERT(atomic_load(&counter) == 42);

    int old = atomic_fetch_add(&counter, 10);
    ASSERT(old == 42);
    ASSERT(atomic_load(&counter) == 52);

    old = atomic_fetch_sub(&counter, 2);
    ASSERT(old == 52);
    ASSERT(atomic_load(&counter) == 50);

    atomic_fetch_or(&counter, 0x100);
    ASSERT(atomic_load(&counter) == (50 | 0x100));

    atomic_store(&counter, 100);
    int expected = 100;
    bool swapped = atomic_compare_exchange_strong(&counter, &expected, 200);
    ASSERT(swapped);
    ASSERT(atomic_load(&counter) == 200);

    expected = 999;
    swapped = atomic_compare_exchange_strong(&counter, &expected, 300);
    ASSERT(!swapped);
    ASSERT(expected == 200);

    _Atomic(unsigned long) big = ATOMIC_VAR_INIT(0);
    atomic_store(&big, 0xDEADBEEFUL);
    ASSERT(atomic_load(&big) == 0xDEADBEEFUL);

    atomic_flag flag = ATOMIC_FLAG_INIT;
    bool was_set = atomic_flag_test_and_set(&flag);
    ASSERT(!was_set);
    was_set = atomic_flag_test_and_set(&flag);
    ASSERT(was_set);
    atomic_flag_clear(&flag);

    return 0;
}

/* ================================================================
 * 2. _Thread_local storage class
 * ================================================================ */
static _Thread_local int tls_counter = 0;
static _Thread_local const char *tls_name = "main";

static int test_thread_local(void) {
    tls_counter = 42;
    ASSERT(tls_counter == 42);
    tls_counter++;
    ASSERT(tls_counter == 43);

    tls_name = "modified";
    ASSERT(strcmp(tls_name, "modified") == 0);
    return 0;
}

/* ================================================================
 * 3. _Alignas / _Alignof / alignas / alignof (C11)
 * ================================================================ */
struct aligned_data {
    _Alignas(16) char buf[64];
    _Alignas(8) int value;
};

static int test_alignment(void) {
    ASSERT(alignof(struct aligned_data) >= 16);
    ASSERT(_Alignof(double) >= 8);
    ASSERT(alignof(max_align_t) >= 8);

    _Alignas(64) char cache_line[64];
    ASSERT(((uintptr_t)cache_line % 64) == 0);
    cache_line[0] = 'A';
    ASSERT(cache_line[0] == 'A');

    struct aligned_data ad;
    ASSERT(((uintptr_t)ad.buf % 16) == 0);
    ad.value = 12345;
    ASSERT(ad.value == 12345);

    _Static_assert(alignof(struct aligned_data) >= 16, "alignment check");
    return 0;
}

/* ================================================================
 * 4. _Noreturn / noreturn function specifier
 * ================================================================ */
noreturn static void do_abort(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    abort();
}

static int test_noreturn(void) {
    int x = 1;
    if (x == 0) {
        do_abort("should not reach here");
    }
    return 0;
}

/* ================================================================
 * 5. _Complex type
 * ================================================================ */
static int test_complex(void) {
    double _Complex z1 = 3.0 + 4.0 * _Complex_I;
    double _Complex z2 = 1.0 - 2.0 * _Complex_I;

    double _Complex sum = z1 + z2;
    ASSERT(__real__ sum == 4.0);
    ASSERT(__imag__ sum == 2.0);

    double _Complex prod = z1 * z2;
    ASSERT(__real__ prod == 11.0);
    ASSERT(__imag__ prod == -2.0);

    float _Complex fc = 1.0f + 1.0f * _Complex_I;
    ASSERT(__real__ fc == 1.0f);
    ASSERT(__imag__ fc == 1.0f);

    return 0;
}

/* ================================================================
 * 6. restrict pointer qualifier (C99)
 * ================================================================ */
static void vec_add(int *restrict dst, const int *restrict a,
                    const int *restrict b, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = a[i] + b[i];
}

static int test_restrict(void) {
    int a[] = {1, 2, 3, 4};
    int b[] = {10, 20, 30, 40};
    int c[4];

    vec_add(c, a, b, 4);
    ASSERT(c[0] == 11 && c[1] == 22 && c[2] == 33 && c[3] == 44);
    return 0;
}

/* ================================================================
 * 7. VLA (Variable Length Arrays)
 * ================================================================ */
static int sum_vla(int n, int arr[n]) {
    int total = 0;
    for (int i = 0; i < n; i++)
        total += arr[i];
    return total;
}

static void fill_2d(int rows, int cols, int mat[rows][cols]) {
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            mat[r][c] = r * cols + c;
}

static int test_vla(void) {
    int n = 5;
    int arr[n];
    for (int i = 0; i < n; i++)
        arr[i] = i + 1;
    ASSERT(sum_vla(n, arr) == 15);

    int rows = 3, cols = 4;
    int mat[rows][cols];
    fill_2d(rows, cols, mat);
    ASSERT(mat[0][0] == 0);
    ASSERT(mat[1][2] == 6);
    ASSERT(mat[2][3] == 11);

    int dynamic_size = 10;
    char buf[dynamic_size];
    memset(buf, 'x', (size_t)dynamic_size);
    ASSERT(buf[0] == 'x' && buf[9] == 'x');

    return 0;
}

/* ================================================================
 * 8. _Generic with complex associations
 * ================================================================ */
#define type_name(x) _Generic((x),              \
    int:              "int",                     \
    unsigned int:     "unsigned int",            \
    long:             "long",                    \
    float:            "float",                   \
    double:           "double",                  \
    char *:           "char *",                  \
    const char *:     "const char *",            \
    int *:            "int *",                   \
    void *:           "void *",                  \
    default:          "other")

#define safe_abs(x) _Generic((x),               \
    int:    abs,                                 \
    long:   labs,                                \
    default: abs)(x)

static int test_generic_advanced(void) {
    int i = 42;
    double d = 3.14;
    char *s = "hello";
    int *p = &i;

    ASSERT(strcmp(type_name(i), "int") == 0);
    ASSERT(strcmp(type_name(d), "double") == 0);
    ASSERT(strcmp(type_name(s), "char *") == 0);
    ASSERT(strcmp(type_name(p), "int *") == 0);

    ASSERT(safe_abs(-42) == 42);
    ASSERT(safe_abs(-100L) == 100L);
    return 0;
}

/* ================================================================
 * 9. Anonymous structs/unions (C11)
 * ================================================================ */
struct tagged_value {
    enum { TV_INT, TV_FLOAT, TV_STR } tag;
    union {
        int i;
        float f;
        const char *s;
    };
};

struct register_file {
    union {
        uint64_t regs[4];
        struct {
            uint64_t r0, r1, r2, r3;
        };
    };
    struct {
        unsigned cf : 1;
        unsigned zf : 1;
        unsigned sf : 1;
        unsigned of : 1;
    };
};

static int test_anonymous_struct_union(void) {
    struct tagged_value v;
    v.tag = TV_INT;
    v.i = 42;
    ASSERT(v.tag == TV_INT && v.i == 42);

    v.tag = TV_STR;
    v.s = "hello";
    ASSERT(v.tag == TV_STR && strcmp(v.s, "hello") == 0);

    struct register_file rf = {0};
    rf.r0 = 0xDEAD;
    rf.r1 = 0xBEEF;
    rf.cf = 1;
    rf.zf = 0;
    ASSERT(rf.regs[0] == 0xDEAD);
    ASSERT(rf.regs[1] == 0xBEEF);
    ASSERT(rf.cf == 1);
    ASSERT(rf.zf == 0);

    return 0;
}

/* ================================================================
 * 10. _Static_assert in various scopes
 * ================================================================ */
_Static_assert(sizeof(int) >= 4, "int must be at least 32 bits");

struct checked_layout {
    int x;
    _Static_assert(sizeof(int) == 4, "layout check");
    double y;
};

static int test_static_assert_scopes(void) {
    _Static_assert(sizeof(struct checked_layout) >= 12, "struct size");
    _Static_assert(1 + 1 == 2, "math works");
    _Static_assert(sizeof(void *) == 8 || sizeof(void *) == 4, "pointer size");
    return 0;
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    int failures = 0;
#define RUN(fn) do { \
    if (fn() != 0) { fprintf(stderr, "FAIL: " #fn "\n"); failures++; } \
} while (0)

    RUN(test_atomics);
    RUN(test_thread_local);
    RUN(test_alignment);
    RUN(test_noreturn);
    RUN(test_complex);
    RUN(test_restrict);
    RUN(test_vla);
    RUN(test_generic_advanced);
    RUN(test_anonymous_struct_union);
    RUN(test_static_assert_scopes);

#undef RUN
    if (failures == 0)
        printf("test_c11_atomics: ALL PASSED\n");
    else
        printf("test_c11_atomics: %d FAILED\n", failures);
    return failures;
}
