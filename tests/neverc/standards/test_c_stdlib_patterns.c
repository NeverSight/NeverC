// RUN: %neverc -fsyntax-only %s
/*
 * NeverC Compiler Validation - C Standard Library & Language Edge Cases
 *
 * Tests standard library interaction patterns + language edge cases
 * that real-world C code (kernel, libc, embedded) relies on:
 *
 *  1.  setjmp / longjmp (non-local jumps)
 *  2.  Variadic functions: va_list, va_start, va_arg, va_copy, va_end
 *  3.  qsort / bsearch (stdlib callbacks)
 *  4.  atexit / on_exit
 *  5.  alloca / __builtin_alloca (stack allocation)
 *  6.  Wide/multi-byte strings: wchar_t, L"", char16_t, char32_t
 *  7.  _Pragma operator
 *  8.  Function attributes: always_inline, noinline, cold, hot, malloc, pure, const
 *  9.  Volatile + signal patterns
 * 10.  errno patterns
 * 11.  Compound literal as function argument (complex)
 * 12.  Recursive struct (tree, linked list)
 * 13.  Opaque pointer pattern (forward decl + incomplete type)
 * 14.  __attribute__((may_alias)) type punning
 * 15.  Struct padding / sizeof edge cases
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <errno.h>
#include <wchar.h>
#include <limits.h>
#include <float.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

/* ================================================================
 * 1. setjmp / longjmp (non-local jumps)
 * Used in: kernel panic recovery, error handling frameworks
 * ================================================================ */
static jmp_buf error_jmp;

static int risky_operation(int x) {
    if (x < 0)
        longjmp(error_jmp, -1);
    if (x == 0)
        longjmp(error_jmp, -2);
    return x * 2;
}

static int test_setjmp_longjmp(void) {
    volatile int result = 0;
    int code = setjmp(error_jmp);

    if (code == 0) {
        result = risky_operation(21);
        ASSERT(result == 42);

        result = risky_operation(-1);
        ASSERT(0 && "should not reach");
    } else if (code == -1) {
        ASSERT(result == 42);
    } else if (code == -2) {
        ASSERT(0 && "wrong code");
    }
    return 0;
}

/* ================================================================
 * 2. Variadic functions: va_list, va_copy (deep test)
 * ================================================================ */
static int my_sprintf(char *buf, size_t sz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, sz, fmt, ap);
    va_end(ap);
    return r;
}

static int sum_ints(int count, ...) {
    va_list ap, ap_copy;
    va_start(ap, count);
    va_copy(ap_copy, ap);

    int sum1 = 0;
    for (int i = 0; i < count; i++)
        sum1 += va_arg(ap, int);
    va_end(ap);

    int sum2 = 0;
    for (int i = 0; i < count; i++)
        sum2 += va_arg(ap_copy, int);
    va_end(ap_copy);

    ASSERT(sum1 == sum2);
    return sum1;
}

static int mixed_variadic(const char *types, ...) {
    va_list ap;
    va_start(ap, types);
    int sum = 0;
    for (const char *t = types; *t; t++) {
        switch (*t) {
        case 'i': sum += va_arg(ap, int); break;
        case 'd': sum += (int)va_arg(ap, double); break;
        case 'l': sum += (int)va_arg(ap, long); break;
        default: break;
        }
    }
    va_end(ap);
    return sum;
}

static int test_variadic(void) {
    char buf[64];
    my_sprintf(buf, sizeof(buf), "%d + %d = %d", 1, 2, 3);
    ASSERT(strcmp(buf, "1 + 2 = 3") == 0);

    ASSERT(sum_ints(4, 10, 20, 30, 40) == 100);
    ASSERT(sum_ints(0) == 0);
    ASSERT(mixed_variadic("iid", 10, 20, 3.7) == 33);

    return 0;
}

/* ================================================================
 * 3. qsort / bsearch (stdlib callbacks)
 * ================================================================ */
static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

struct record {
    int key;
    const char *value;
};

static int cmp_record(const void *a, const void *b) {
    return ((const struct record *)a)->key - ((const struct record *)b)->key;
}

static int test_qsort_bsearch(void) {
    int arr[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    qsort(arr, 10, sizeof(int), cmp_int);
    for (int i = 0; i < 10; i++)
        ASSERT(arr[i] == i);

    int key = 7;
    int *found = bsearch(&key, arr, 10, sizeof(int), cmp_int);
    ASSERT(found && *found == 7);

    key = 99;
    found = bsearch(&key, arr, 10, sizeof(int), cmp_int);
    ASSERT(found == NULL);

    struct record records[] = {
        {30, "thirty"}, {10, "ten"}, {20, "twenty"}, {40, "forty"},
    };
    qsort(records, 4, sizeof(struct record), cmp_record);
    ASSERT(records[0].key == 10);
    ASSERT(strcmp(records[0].value, "ten") == 0);
    ASSERT(records[3].key == 40);

    return 0;
}

/* ================================================================
 * 4. atexit
 * ================================================================ */
static int atexit_called = 0;

static void atexit_handler(void) {
    atexit_called = 1;
}

static int test_atexit(void) {
    ASSERT(atexit(atexit_handler) == 0);
    return 0;
}

/* ================================================================
 * 5. alloca / __builtin_alloca (stack allocation)
 * ================================================================ */
static int test_alloca(void) {
    int n = 16;
    int *buf = __builtin_alloca(n * sizeof(int));
    ASSERT(buf != NULL);
    for (int i = 0; i < n; i++)
        buf[i] = i * i;
    ASSERT(buf[4] == 16);
    ASSERT(buf[10] == 100);

    char *str = __builtin_alloca(64);
    snprintf(str, 64, "hello from alloca: %d", 42);
    ASSERT(strstr(str, "42") != NULL);

    return 0;
}

/* ================================================================
 * 6. Wide/multi-byte strings
 * ================================================================ */
static int test_wide_strings(void) {
    wchar_t wc = L'A';
    ASSERT(wc == L'A');

    const wchar_t *ws = L"Hello Wide";
    ASSERT(wcslen(ws) == 10);
    ASSERT(ws[0] == L'H');
    ASSERT(ws[5] == L' ');

    wchar_t buf[32];
    wcscpy(buf, L"test");
    ASSERT(wcscmp(buf, L"test") == 0);

    wchar_t cat[64] = L"foo";
    wcscat(cat, L"bar");
    ASSERT(wcscmp(cat, L"foobar") == 0);

    ASSERT(sizeof(wchar_t) == 4 || sizeof(wchar_t) == 2);

    return 0;
}

/* ================================================================
 * 7. _Pragma operator
 * ================================================================ */
#define SUPPRESS_WARNING(w) _Pragma(#w)

static int test_pragma(void) {
    _Pragma("clang diagnostic push")
    _Pragma("clang diagnostic ignored \"-Wunused-variable\"")
    int unused_here = 42;
    _Pragma("clang diagnostic pop")

    SUPPRESS_WARNING(clang diagnostic push)
    SUPPRESS_WARNING(clang diagnostic ignored "-Wunused-value")
    42;
    SUPPRESS_WARNING(clang diagnostic pop)

    return 0;
}

/* ================================================================
 * 8. Function attributes: always_inline, noinline, cold, hot, etc.
 * ================================================================ */
__attribute__((always_inline))
static inline int fast_add(int a, int b) { return a + b; }

__attribute__((noinline))
static int slow_multiply(int a, int b) { return a * b; }

__attribute__((cold))
static void handle_rare_error(int code) { (void)code; }

__attribute__((hot))
static int hot_path(int x) { return x + 1; }

__attribute__((pure))
static int pure_square(int x) { return x * x; }

__attribute__((const))
static int const_double(int x) { return x * 2; }

__attribute__((malloc))
static void *my_alloc(size_t sz) { return malloc(sz); }

__attribute__((warn_unused_result))
static int must_check(int x) { return x > 0 ? 0 : -1; }

static int test_function_attrs(void) {
    ASSERT(fast_add(3, 4) == 7);
    ASSERT(slow_multiply(6, 7) == 42);
    handle_rare_error(0);
    ASSERT(hot_path(41) == 42);
    ASSERT(pure_square(7) == 49);
    ASSERT(const_double(21) == 42);

    void *p = my_alloc(64);
    ASSERT(p != NULL);
    free(p);

    int rc = must_check(1);
    ASSERT(rc == 0);

    return 0;
}

/* ================================================================
 * 9. Volatile + signal-safe patterns
 * ================================================================ */
static volatile sig_atomic_t signal_received = 0;

static void signal_handler_sim(int sig) {
    (void)sig;
    signal_received = 1;
}

static int test_volatile_signal(void) {
    signal_received = 0;
    signal_handler_sim(2);
    ASSERT(signal_received == 1);

    volatile int v = 0;
    v = 42;
    ASSERT(v == 42);
    v = v + 1;
    ASSERT(v == 43);

    volatile unsigned char mmio = 0xFF;
    unsigned char val = mmio;
    ASSERT(val == 0xFF);
    mmio = 0x00;
    ASSERT(mmio == 0x00);

    return 0;
}

/* ================================================================
 * 10. errno patterns
 * ================================================================ */
static int test_errno(void) {
    errno = 0;
    long val = strtol("99999999999999999999", NULL, 10);
    ASSERT(errno == ERANGE);
    (void)val;

    errno = 0;
    val = strtol("42", NULL, 10);
    ASSERT(errno == 0);
    ASSERT(val == 42);

    errno = 0;
    char *end;
    val = strtol("0xDEAD", &end, 0);
    ASSERT(val == 0xDEAD);
    ASSERT(*end == '\0');

    return 0;
}

/* ================================================================
 * 11. Compound literal as function argument (complex)
 * ================================================================ */
struct vec2 { float x, y; };
struct config { int flags; const char *name; int values[4]; };

static float vec2_len_sq(const struct vec2 *v) {
    return v->x * v->x + v->y * v->y;
}

static int config_sum(const struct config *c) {
    int s = 0;
    for (int i = 0; i < 4; i++) s += c->values[i];
    return s;
}

static int test_compound_literal_args(void) {
    float len = vec2_len_sq(&(struct vec2){3.0f, 4.0f});
    ASSERT(len == 25.0f);

    int s = config_sum(&(struct config){
        .flags = 0x1,
        .name = "test",
        .values = {10, 20, 30, 40},
    });
    ASSERT(s == 100);

    int *arr = (int[]){5, 10, 15, 20};
    ASSERT(arr[0] == 5 && arr[3] == 20);

    const char *msgs[] = {
        (char[]){"hello"},
        (char[]){"world"},
    };
    ASSERT(strcmp(msgs[0], "hello") == 0);

    return 0;
}

/* ================================================================
 * 12. Recursive struct (tree + linked list)
 * ================================================================ */
struct tree_node {
    int value;
    struct tree_node *left, *right;
};

static int tree_sum(const struct tree_node *n) {
    if (!n) return 0;
    return n->value + tree_sum(n->left) + tree_sum(n->right);
}

static int tree_depth(const struct tree_node *n) {
    if (!n) return 0;
    int l = tree_depth(n->left);
    int r = tree_depth(n->right);
    return 1 + (l > r ? l : r);
}

static int test_recursive_struct(void) {
    struct tree_node n5 = {5, NULL, NULL};
    struct tree_node n3 = {3, NULL, NULL};
    struct tree_node n7 = {7, &n5, NULL};
    struct tree_node root = {10, &n3, &n7};

    ASSERT(tree_sum(&root) == 25);
    ASSERT(tree_depth(&root) == 3);
    ASSERT(tree_depth(NULL) == 0);

    return 0;
}

/* ================================================================
 * 13. Opaque pointer pattern (incomplete type)
 * ================================================================ */
struct opaque_ctx;

struct opaque_ctx *opaque_create(int val);
int opaque_get(const struct opaque_ctx *ctx);
void opaque_destroy(struct opaque_ctx *ctx);

struct opaque_ctx { int internal_value; };

struct opaque_ctx *opaque_create(int val) {
    struct opaque_ctx *c = malloc(sizeof(*c));
    c->internal_value = val;
    return c;
}

int opaque_get(const struct opaque_ctx *ctx) {
    return ctx->internal_value;
}

void opaque_destroy(struct opaque_ctx *ctx) {
    free(ctx);
}

static int test_opaque_pointer(void) {
    struct opaque_ctx *ctx = opaque_create(42);
    ASSERT(opaque_get(ctx) == 42);
    opaque_destroy(ctx);
    return 0;
}

/* ================================================================
 * 14. __attribute__((may_alias)) type punning
 * ================================================================ */
typedef uint32_t __attribute__((may_alias)) aliased_u32;

static int test_may_alias(void) {
    float f = 3.14f;
    aliased_u32 *p = (aliased_u32 *)&f;
    uint32_t bits = *p;
    ASSERT(bits != 0);

    float *fp = (float *)p;
    ASSERT(*fp == 3.14f);

    union { float f; uint32_t u; } pun;
    pun.f = -1.0f;
    ASSERT(pun.u == 0xBF800000U);

    return 0;
}

/* ================================================================
 * 15. Struct padding / sizeof edge cases
 * ================================================================ */
struct padded { char a; int b; char c; };
struct __attribute__((packed)) dense { char a; int b; char c; };

struct bitfield_edge {
    unsigned a : 1;
    unsigned b : 31;
    unsigned c : 16;
    unsigned d : 16;
};

struct empty_ish { char x[0]; };

struct tail_padded {
    int x;
    char y;
};

static int test_struct_sizeof(void) {
    ASSERT(sizeof(struct padded) >= 9);
    ASSERT(sizeof(struct padded) == 12);
    ASSERT(sizeof(struct dense) == 6);

    ASSERT(sizeof(struct bitfield_edge) == 8);
    ASSERT(sizeof(struct tail_padded) >= 5);

    _Static_assert(sizeof(struct padded) > sizeof(struct dense),
                   "packed should be smaller");
    _Static_assert(sizeof(char) == 1, "char is 1 byte");
    _Static_assert(CHAR_BIT == 8, "8-bit bytes");

    struct bitfield_edge bf = { .a = 1, .b = 0x7FFFFFFF, .c = 0xFFFF, .d = 0 };
    ASSERT(bf.a == 1);
    ASSERT(bf.b == 0x7FFFFFFF);
    ASSERT(bf.c == 0xFFFF);

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

    RUN(test_setjmp_longjmp);
    RUN(test_variadic);
    RUN(test_qsort_bsearch);
    RUN(test_atexit);
    RUN(test_alloca);
    RUN(test_wide_strings);
    RUN(test_pragma);
    RUN(test_function_attrs);
    RUN(test_volatile_signal);
    RUN(test_errno);
    RUN(test_compound_literal_args);
    RUN(test_recursive_struct);
    RUN(test_opaque_pointer);
    RUN(test_may_alias);
    RUN(test_struct_sizeof);

#undef RUN
    if (failures == 0)
        printf("test_c_stdlib_patterns: ALL PASSED\n");
    else
        printf("test_c_stdlib_patterns: %d FAILED\n", failures);
    return failures;
}
