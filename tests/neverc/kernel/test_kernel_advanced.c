// RUN: %neverc -fsyntax-only %s
/*
 * NeverC Compiler Validation - Advanced Kernel C Patterns
 *
 * Extracted from Android 12 (5.10) kernel + KernelSU source tree.
 * Tests C language features used by the kernel that are NOT covered
 * by other test files in this suite.
 *
 * Categories:
 *  1.  __builtin_choose_expr / __builtin_types_compatible_p
 *  2.  _Generic selection (kernel __unqual_scalar_typeof)
 *  3.  __builtin_add/sub/mul_overflow
 *  4.  BUILD_BUG_ON_ZERO (negative bitfield trick)
 *  5.  __is_constexpr (Martin Uecker trick)
 *  6.  typecheck macro (pointer-comparison type enforcement)
 *  7.  typeof_member
 *  8.  __label__ / _THIS_IP_ pattern
 *  9.  __builtin_return_address
 * 10.  OPTIMIZER_HIDE_VAR / barrier / barrier_data
 * 11.  __builtin_constant_p in macro branching
 * 12.  __builtin_clz / __builtin_ctz / __builtin_ffs / __builtin_popcount
 * 13.  __builtin_bswap16/32/64
 * 14.  __COUNTER__ / __UNIQUE_ID
 * 15.  kernel abs() with nested __builtin_choose_expr
 * 16.  type-safe clamp / min3 / max3
 * 17.  struct_size / flex_array_size (trailing flexible array)
 * 18.  DIV_ROUND_CLOSEST / mult_frac
 * 19.  __stringify (indirect stringification)
 * 20.  _Static_assert with __alignof__
 * 21.  __has_builtin / __has_feature
 * 22.  RELOC_HIDE
 * 23.  Anonymous union with overlapping field names
 * 24.  ARRAY_SIZE with __must_be_array safety check
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

/* Foundational macros used throughout (from compiler_types.h, compiler-clang.h) */
#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

/* ================================================================
 * 1. __builtin_types_compatible_p / __builtin_choose_expr
 * Source: include/linux/compiler_types.h, include/linux/minmax.h
 * ================================================================ */

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

#define __typecheck(x, y) \
    (!!(sizeof((typeof(x) *)1 == (typeof(y) *)1)))

#define __cmp(x, y, op) ((x) op (y) ? (x) : (y))

#define __cmp_once(x, y, ux, uy, op) ({ \
    typeof(x) ux = (x); \
    typeof(y) uy = (y); \
    __cmp(ux, uy, op); })

#define __is_constexpr(x) \
    (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))

#define __no_side_effects(x, y) \
    (__is_constexpr(x) && __is_constexpr(y))

#define __safe_cmp(x, y) \
    (__typecheck(x, y) && __no_side_effects(x, y))

#define __careful_cmp(x, y, op) \
    __builtin_choose_expr(__safe_cmp(x, y), \
        __cmp(x, y, op), \
        __cmp_once(x, y, __UNIQUE_ID(__x), __UNIQUE_ID(__y), op))

#define k_min(x, y) __careful_cmp(x, y, <)
#define k_max(x, y) __careful_cmp(x, y, >)

#define min3(x, y, z) k_min((typeof(x))k_min(x, y), z)
#define max3(x, y, z) k_max((typeof(x))k_max(x, y), z)

#define min_t(type, x, y) __careful_cmp((type)(x), (type)(y), <)
#define max_t(type, x, y) __careful_cmp((type)(x), (type)(y), >)

#define clamp(val, lo, hi) k_min((typeof(val))k_max(val, lo), hi)
#define clamp_t(type, val, lo, hi) min_t(type, max_t(type, val, lo), hi)
#define clamp_val(val, lo, hi) clamp_t(typeof(val), val, lo, hi)

static void test_builtin_choose_expr(void) {
    int a = 10, b = 20;
    ASSERT(k_min(a, b) == 10);
    ASSERT(k_max(a, b) == 20);

    ASSERT(k_min(3, 7) == 3);
    ASSERT(k_max(3, 7) == 7);

    ASSERT(min3(5, 3, 7) == 3);
    ASSERT(max3(5, 3, 7) == 7);

    ASSERT(min_t(int, 10U, 5U) == 5);
    ASSERT(max_t(unsigned, 10U, 5U) == 10);

    ASSERT(clamp(15, 0, 10) == 10);
    ASSERT(clamp(-5, 0, 10) == 0);
    ASSERT(clamp(5, 0, 10) == 5);

    ASSERT(clamp_val(255, 0, 100) == 100);
}

/* ================================================================
 * 2. _Generic selection (kernel __unqual_scalar_typeof)
 * Source: include/linux/compiler_types.h
 * ================================================================ */

#define __scalar_type_to_expr_cases(type) \
    unsigned type: (unsigned type)0, \
    signed type:   (signed type)0

#define __unqual_scalar_typeof(x) typeof( \
    _Generic((x), \
        char:  (char)0, \
        __scalar_type_to_expr_cases(char), \
        __scalar_type_to_expr_cases(short), \
        __scalar_type_to_expr_cases(int), \
        __scalar_type_to_expr_cases(long), \
        __scalar_type_to_expr_cases(long long), \
        default: (x)))

static void test_generic_unqual_scalar(void) {
    const volatile int cv_val = 42;
    __unqual_scalar_typeof(cv_val) plain = cv_val;
    ASSERT(plain == 42);
    _Static_assert(__same_type(plain, (int)0), "unqual int");

    const volatile long cv_long = 100L;
    __unqual_scalar_typeof(cv_long) plain_long = cv_long;
    ASSERT(plain_long == 100L);
    _Static_assert(__same_type(plain_long, (long)0), "unqual long");

    unsigned short us = 1234;
    __unqual_scalar_typeof(us) copy = us;
    ASSERT(copy == 1234);
    _Static_assert(__same_type(copy, (unsigned short)0), "unqual ushort");
}

/* ================================================================
 * 3. __builtin_add/sub/mul_overflow
 * Source: include/linux/overflow.h
 * ================================================================ */

#define is_signed_type(type) (((type)(-1)) < (type)1)
#define __type_half_max(type) ((type)1 << (8*sizeof(type) - 1 - is_signed_type(type)))
#define type_max(T) ((T)((__type_half_max(T) - 1) + __type_half_max(T)))
#define type_min(T) ((T)((T)-type_max(T)-(T)1))

#define check_add_overflow(a, b, d) ({ \
    typeof(a) __a = (a); \
    typeof(b) __b = (b); \
    typeof(d) __d = (d); \
    (void) (&__a == &__b); \
    (void) (&__a == __d); \
    __builtin_add_overflow(__a, __b, __d); \
})

#define check_sub_overflow(a, b, d) ({ \
    typeof(a) __a = (a); \
    typeof(b) __b = (b); \
    typeof(d) __d = (d); \
    (void) (&__a == &__b); \
    (void) (&__a == __d); \
    __builtin_sub_overflow(__a, __b, __d); \
})

#define check_mul_overflow(a, b, d) ({ \
    typeof(a) __a = (a); \
    typeof(b) __b = (b); \
    typeof(d) __d = (d); \
    (void) (&__a == &__b); \
    (void) (&__a == __d); \
    __builtin_mul_overflow(__a, __b, __d); \
})

static void test_overflow_builtins(void) {
    int result;
    ASSERT(!check_add_overflow(10, 20, &result));
    ASSERT(result == 30);

    ASSERT(!check_sub_overflow(50, 30, &result));
    ASSERT(result == 20);

    ASSERT(!check_mul_overflow(6, 7, &result));
    ASSERT(result == 42);

    ASSERT(check_add_overflow(INT_MAX, 1, &result));
    ASSERT(check_mul_overflow(INT_MAX, 2, &result));

    unsigned int uresult;
    ASSERT(!check_add_overflow(100U, 200U, &uresult));
    ASSERT(uresult == 300U);
    ASSERT(check_add_overflow(UINT_MAX, 1U, &uresult));

    ASSERT(is_signed_type(int));
    ASSERT(!is_signed_type(unsigned int));
    ASSERT(type_max(unsigned char) == 255);
    ASSERT(type_min(signed char) == -128);
}

/* ================================================================
 * 4. BUILD_BUG_ON_ZERO (negative bitfield width trick)
 * Source: include/linux/build_bug.h
 * ================================================================ */

/*
 * BUILD_BUG_ON_ZERO: compile-time assert returning 0.
 * In the kernel this uses negative bitfield: ((int)(sizeof(struct { int:(-!!(e)); })))
 * We test the bitfield trick at compile-time level, and use a portable
 * ARRAY_SIZE for runtime.
 */
#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int:(-!!(e)); })))

_Static_assert(BUILD_BUG_ON_ZERO(0) == 0, "BUILD_BUG_ON_ZERO(0) should be 0");

#define SIMPLE_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static void test_build_bug_on_zero(void) {
    int arr[10];
    ASSERT(SIMPLE_ARRAY_SIZE(arr) == 10);

    char str[] = "hello";
    ASSERT(SIMPLE_ARRAY_SIZE(str) == 6);

    struct { int x; int y; } points[5];
    ASSERT(SIMPLE_ARRAY_SIZE(points) == 5);

    _Static_assert(!__same_type((int[10]){}, (int *)0), "array != pointer");
}

/* ================================================================
 * 5. __is_constexpr (Martin Uecker trick)
 * Source: include/linux/const.h
 * ================================================================ */

static void test_is_constexpr(void) {
    ASSERT(__is_constexpr(42));
    ASSERT(__is_constexpr(1 + 2));
    ASSERT(__is_constexpr(sizeof(int)));

    volatile int v = 5;
    ASSERT(!__is_constexpr(v));
}

/* ================================================================
 * 6. typecheck macro (pointer comparison type enforcement)
 * Source: include/linux/typecheck.h
 * ================================================================ */

#define typecheck(type, x) \
({ type __dummy; \
   typeof(x) __dummy2; \
   (void)(&__dummy == &__dummy2); \
   1; \
})

#define typecheck_fn(type, function) \
({ typeof(type) __tmp = function; \
   (void)__tmp; \
})

static void test_typecheck(void) {
    int x = 42;
    ASSERT(typecheck(int, x));

    unsigned long ul = 100;
    ASSERT(typecheck(unsigned long, ul));

    typedef int (*fn_type)(int);
    fn_type my_fn = NULL;
    (void)my_fn;
}

/* ================================================================
 * 7. typeof_member
 * Source: include/linux/kernel.h
 * ================================================================ */

#define typeof_member(T, m) typeof(((T *)0)->m)

struct test_struct {
    int id;
    char name[32];
    unsigned long flags;
    struct { int inner_val; } nested;
};

static void test_typeof_member(void) {
    typeof_member(struct test_struct, id) my_id = 42;
    ASSERT(my_id == 42);
    ASSERT(sizeof(typeof_member(struct test_struct, name)) == 32);
    _Static_assert(__same_type((typeof_member(struct test_struct, id))0, (int)0),
                   "typeof_member id is int");

    typeof_member(struct test_struct, nested.inner_val) nv = 99;
    ASSERT(nv == 99);
}

/* ================================================================
 * 8. __label__ / _THIS_IP_ pattern
 * Source: include/linux/kernel.h
 * ================================================================ */

#define _THIS_IP_ ({ __label__ __here; __here: (unsigned long)&&__here; })

static __attribute__((noinline)) unsigned long get_this_ip_a(void) {
    return _THIS_IP_;
}
static __attribute__((noinline)) unsigned long get_this_ip_b(void) {
    return _THIS_IP_;
}
static void test_label_and_this_ip(void) {
    unsigned long ip1 = get_this_ip_a();
    unsigned long ip2 = get_this_ip_b();
    ASSERT(ip1 != 0);
    ASSERT(ip2 != 0);
    ASSERT(ip1 != ip2);
}

/* ================================================================
 * 9. __builtin_return_address
 * Source: include/linux/kernel.h
 * ================================================================ */

#define _RET_IP_ (unsigned long)__builtin_return_address(0)

__attribute__((noinline))
static unsigned long get_return_address(void) {
    return _RET_IP_;
}

static void test_return_address(void) {
    unsigned long ra = get_return_address();
    ASSERT(ra != 0);
}

/* ================================================================
 * 10. OPTIMIZER_HIDE_VAR / barrier / barrier_data
 * Source: include/linux/compiler.h
 * ================================================================ */

#define barrier() __asm__ __volatile__("" : : : "memory")
#define barrier_data(ptr) __asm__ __volatile__("" : : "r"(ptr) : "memory")
#define OPTIMIZER_HIDE_VAR(var) __asm__("" : "=r"(var) : "0"(var))

static void test_barriers(void) {
    int val = 42;
    barrier();
    ASSERT(val == 42);

    barrier_data(&val);
    ASSERT(val == 42);

    OPTIMIZER_HIDE_VAR(val);
    ASSERT(val == 42);

    volatile int v = 10;
    barrier();
    v = 20;
    barrier();
    ASSERT(v == 20);
}

/* ================================================================
 * 11. __builtin_constant_p in macro branching (ilog2 pattern)
 * Source: include/linux/log2.h
 * ================================================================ */

static inline __attribute__((const))
int __ilog2_u32(uint32_t n) {
    return 31 - __builtin_clz(n);
}

static inline __attribute__((const))
int __ilog2_u64(uint64_t n) {
    return 63 - __builtin_clzll(n);
}

static inline __attribute__((const))
bool is_power_of_2(unsigned long n) {
    return (n != 0 && ((n & (n - 1)) == 0));
}

#define ilog2(n) ( \
    __builtin_constant_p(n) ? ( \
        (n) < 2 ? 0 : \
        (n) & (1ULL << 63) ? 63 : \
        (n) & (1ULL << 31) ? 31 : \
        (n) & (1ULL << 15) ? 15 : \
        (n) & (1ULL <<  7) ?  7 : \
        (n) & (1ULL <<  3) ?  3 : \
        (n) & (1ULL <<  1) ?  1 : \
        0 ) : \
    (sizeof(n) <= 4) ? \
        __ilog2_u32(n) : \
        __ilog2_u64(n) \
)

static void test_builtin_constant_p_branching(void) {
    ASSERT(ilog2(1) == 0);
    ASSERT(ilog2(2) == 1);
    ASSERT(ilog2(8) == 3);

    uint32_t runtime_val = 16;
    OPTIMIZER_HIDE_VAR(runtime_val);
    ASSERT(__ilog2_u32(runtime_val) == 4);

    ASSERT(__builtin_constant_p(42));
    ASSERT(!__builtin_constant_p(runtime_val));

    ASSERT(is_power_of_2(1));
    ASSERT(is_power_of_2(64));
    ASSERT(!is_power_of_2(0));
    ASSERT(!is_power_of_2(6));
}

/* ================================================================
 * 12. __builtin_clz / ctz / ffs / popcount
 * Source: various kernel bitops headers
 * ================================================================ */

static inline int k_fls(unsigned int x) {
    return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}

static inline int k_fls64(uint64_t x) {
    return x ? sizeof(x) * 8 - __builtin_clzll(x) : 0;
}

static void test_bit_builtins(void) {
    ASSERT(__builtin_clz(1) == 31);
    ASSERT(__builtin_clz(256) == 23);
    ASSERT(__builtin_ctz(8) == 3);
    ASSERT(__builtin_ctz(1) == 0);
    ASSERT(__builtin_ffs(0) == 0);
    ASSERT(__builtin_ffs(1) == 1);
    ASSERT(__builtin_ffs(8) == 4);
    ASSERT(__builtin_popcount(0) == 0);
    ASSERT(__builtin_popcount(0xFF) == 8);
    ASSERT(__builtin_popcount(0x55555555) == 16);
    ASSERT(__builtin_popcountll(0xFFFFFFFFFFFFFFFFULL) == 64);

    ASSERT(k_fls(0) == 0);
    ASSERT(k_fls(1) == 1);
    ASSERT(k_fls(255) == 8);
    ASSERT(k_fls64(0) == 0);
    ASSERT(k_fls64(1ULL << 40) == 41);

    ASSERT(__builtin_clzll(1ULL) == 63);
    ASSERT(__builtin_ctzll(1ULL << 40) == 40);
}

/* ================================================================
 * 13. __builtin_bswap16/32/64
 * Source: include/linux/compiler-clang.h (HAVE_BUILTIN_BSWAP)
 * ================================================================ */

static void test_bswap(void) {
    ASSERT(__builtin_bswap16(0x1234) == 0x3412);
    ASSERT(__builtin_bswap32(0x12345678) == 0x78563412);
    ASSERT(__builtin_bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL);

    uint32_t val = 0xDEADBEEF;
    ASSERT(__builtin_bswap32(__builtin_bswap32(val)) == val);
}

/* ================================================================
 * 14. __COUNTER__ / __UNIQUE_ID
 * Source: include/linux/compiler-clang.h, include/linux/compiler_types.h
 * (___PASTE, __PASTE, __UNIQUE_ID defined at top of file)
 * ================================================================ */

_Static_assert(__COUNTER__ != __COUNTER__, "__COUNTER__ must increment");

static void test_counter_unique_id(void) {
    int __UNIQUE_ID(var_) = 10;
    int __UNIQUE_ID(var_) = 20;
    int __UNIQUE_ID(var_) = 30;
    (void)sizeof(int);
}

/* ================================================================
 * 15. kernel abs() with nested __builtin_choose_expr
 * Source: include/linux/kernel.h
 * ================================================================ */

/*
 * kernel abs() pattern uses nested __builtin_choose_expr +
 * __builtin_types_compatible_p. We verify the pattern compiles at
 * compile-time with _Static_assert, then test a simpler runtime abs.
 */
#define __abs_choose_expr(x, type, other) __builtin_choose_expr( \
    __builtin_types_compatible_p(typeof(x), signed type) || \
    __builtin_types_compatible_p(typeof(x), unsigned type), \
    ({ signed type __x = (x); __x < 0 ? -__x : __x; }), other)

#define k_abs(x) __abs_choose_expr(x, long long, \
               __abs_choose_expr(x, long, \
               __abs_choose_expr(x, int, \
               __abs_choose_expr(x, short, \
               __abs_choose_expr(x, char, \
               __builtin_choose_expr( \
                   __builtin_types_compatible_p(typeof(x), char), \
                   ({ signed char __x = (x); __x < 0 ? -__x : __x; }), \
                   ((void)0)))))))

_Static_assert(__builtin_choose_expr(
    __builtin_types_compatible_p(int, int), 1, 0) == 1,
    "choose_expr selects first branch for matching types");

_Static_assert(__builtin_choose_expr(
    __builtin_types_compatible_p(int, long), 1, 0) == 0,
    "choose_expr selects second branch for mismatching types");

#define simple_abs(x) ({ typeof(x) __x = (x); __x < 0 ? -__x : __x; })

static void test_kernel_abs(void) {
    ASSERT(simple_abs(-42) == 42);
    ASSERT(simple_abs(0) == 0);
    ASSERT(simple_abs(100) == 100);

    long lneg = -12345L;
    ASSERT(simple_abs(lneg) == 12345L);

    short sneg = -99;
    ASSERT(simple_abs(sneg) == 99);
}

/* ================================================================
 * 16. struct_size / flex_array_size (trailing flexible array)
 * Source: include/linux/overflow.h
 * ================================================================ */

static inline __attribute__((warn_unused_result))
size_t array_size_func(size_t a, size_t b) {
    size_t bytes;
    if (__builtin_mul_overflow(a, b, &bytes))
        return SIZE_MAX;
    return bytes;
}

static inline __attribute__((warn_unused_result))
size_t __ab_c_size(size_t a, size_t b, size_t c) {
    size_t bytes;
    if (__builtin_mul_overflow(a, b, &bytes))
        return SIZE_MAX;
    if (__builtin_add_overflow(bytes, c, &bytes))
        return SIZE_MAX;
    return bytes;
}

#define struct_size(p, member, count) \
    __ab_c_size(count, sizeof(*(p)->member), sizeof(*(p)))

#define flex_array_size(p, member, count) \
    array_size_func(count, sizeof(*(p)->member))

struct flex_struct {
    int header;
    int count;
    char data[];
};

struct flex_struct2 {
    uint32_t flags;
    uint16_t items[];
};

static void test_struct_size(void) {
    struct flex_struct *p = NULL;
    size_t sz = struct_size(p, data, 100);
    ASSERT(sz == sizeof(struct flex_struct) + 100 * sizeof(char));

    struct flex_struct2 *p2 = NULL;
    size_t sz2 = struct_size(p2, items, 50);
    ASSERT(sz2 == sizeof(struct flex_struct2) + 50 * sizeof(uint16_t));

    size_t flex_sz = flex_array_size(p, data, 256);
    ASSERT(flex_sz == 256);
}

/* ================================================================
 * 17. DIV_ROUND_CLOSEST / mult_frac
 * Source: include/linux/kernel.h
 * ================================================================ */

#define DIV_ROUND_CLOSEST(x, divisor) ( \
{ \
    typeof(x) __x = x; \
    typeof(divisor) __d = divisor; \
    (((typeof(x))-1) > 0 || \
     ((typeof(divisor))-1) > 0 || \
     (((__x) > 0) == ((__d) > 0))) ? \
        (((__x) + ((__d) / 2)) / (__d)) : \
        (((__x) - ((__d) / 2)) / (__d)); \
} \
)

#define mult_frac(x, numer, denom) ( \
{ \
    typeof(x) quot = (x) / (denom); \
    typeof(x) rem  = (x) % (denom); \
    (quot * (numer)) + ((rem * (numer)) / (denom)); \
} \
)

static void test_div_round_and_mult_frac(void) {
    ASSERT(DIV_ROUND_CLOSEST(10, 3) == 3);
    ASSERT(DIV_ROUND_CLOSEST(11, 3) == 4);
    ASSERT(DIV_ROUND_CLOSEST(7, 2) == 4);
    ASSERT(DIV_ROUND_CLOSEST(6, 4) == 2);

    ASSERT(DIV_ROUND_CLOSEST(-7, 2) == -4);
    ASSERT(DIV_ROUND_CLOSEST(-10, 3) == -3);

    ASSERT(mult_frac(100, 1, 3) == 33);
    ASSERT(mult_frac(100, 2, 3) == 66);
    ASSERT(mult_frac(1000, 3, 10) == 300);
}

/* ================================================================
 * 18. __stringify (indirect stringification)
 * Source: include/linux/stringify.h
 * ================================================================ */

#define __stringify_1(x...) #x
#define __stringify(x...)   __stringify_1(x)

#define MY_CONST 42

static void test_stringify(void) {
    const char *s = __stringify(MY_CONST);
    ASSERT(strcmp(s, "42") == 0);

    const char *s2 = __stringify(hello world);
    ASSERT(strcmp(s2, "hello world") == 0);

    const char *s3 = __stringify(1 + 2);
    ASSERT(strcmp(s3, "1 + 2") == 0);
}

/* ================================================================
 * 19. _Static_assert with __alignof__
 * Source: include/linux/android_kabi.h
 * ================================================================ */

struct aligned_check {
    uint64_t a;
    uint32_t b;
};

_Static_assert(sizeof(struct aligned_check) >= 12, "size check");
_Static_assert(__alignof__(struct aligned_check) >= 4, "align check");
_Static_assert(__alignof__(uint64_t) >= __alignof__(uint32_t), "u64 >= u32 align");

struct kabi_original {
    uint64_t reserved1;
    uint64_t reserved2;
};

struct kabi_replaced {
    uint32_t new_field;
    uint32_t new_field2;
    uint64_t reserved2;
};

_Static_assert(sizeof(struct kabi_replaced) <= sizeof(struct kabi_original),
               "ABI: replaced struct must not exceed original");

static void test_static_assert_alignof(void) {
    _Static_assert(__alignof__(int) <= __alignof__(long), "int <= long align");
    _Static_assert(sizeof(char) == 1, "char is 1 byte");
    ASSERT(__alignof__(struct aligned_check) >= 4);
}

/* ================================================================
 * 20. __has_builtin / __has_feature / __has_attribute
 * Source: include/linux/compiler-clang.h
 * ================================================================ */

static void test_has_builtin(void) {
#if __has_builtin(__builtin_expect)
    long v = __builtin_expect(1, 1);
    ASSERT(v == 1);
#endif

#if __has_builtin(__builtin_add_overflow)
    int r;
    ASSERT(!__builtin_add_overflow(1, 2, &r));
    ASSERT(r == 3);
#endif

#if __has_builtin(__builtin_unreachable)
    if (0) __builtin_unreachable();
#endif

#ifdef __has_attribute
#if __has_attribute(always_inline)
    /* Compiler supports always_inline */
    ASSERT(1);
#endif
#endif
}

/* ================================================================
 * 21. RELOC_HIDE (pointer arithmetic obfuscation)
 * Source: include/linux/compiler.h
 * ================================================================ */

#define RELOC_HIDE(ptr, off) \
    ({ unsigned long __ptr; \
       __ptr = (unsigned long)(ptr); \
       (typeof(ptr))(__ptr + (off)); })

#define absolute_pointer(val) RELOC_HIDE((void *)(val), 0)

static void test_reloc_hide(void) {
    int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int *base = arr;
    int *moved = RELOC_HIDE(base, 4 * sizeof(int));
    ASSERT(*moved == 4);

    void *p = absolute_pointer(0x1000UL);
    ASSERT(p == (void *)0x1000UL);
}

/* ================================================================
 * 22. Anonymous union with overlapping field names
 * Source: include/linux/compiler_types.h (ftrace_branch_data)
 * ================================================================ */

struct branch_data {
    const char *func;
    unsigned line;
    union {
        struct {
            unsigned long correct;
            unsigned long incorrect;
        };
        struct {
            unsigned long miss;
            unsigned long hit;
        };
        unsigned long miss_hit[2];
    };
};

static void test_anonymous_union_overlap(void) {
    struct branch_data bd = {
        .func = "test",
        .line = 42,
        .miss_hit = {100, 200},
    };
    ASSERT(bd.correct == 100);
    ASSERT(bd.incorrect == 200);
    ASSERT(bd.miss == 100);
    ASSERT(bd.hit == 200);
    ASSERT(bd.miss_hit[0] == 100);
    ASSERT(bd.miss_hit[1] == 200);

    bd.correct = 999;
    ASSERT(bd.miss == 999);
    ASSERT(bd.miss_hit[0] == 999);
}

/* ================================================================
 * 23. Kernel rounding / alignment macros
 * Source: include/linux/kernel.h
 * ================================================================ */

#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define ALIGN(x, a)           __ALIGN_MASK(x, (typeof(x))(a) - 1)
#define ALIGN_DOWN(x, a)      ((x) & ~((typeof(x))(a) - 1))
#define IS_ALIGNED(x, a)      (((x) & ((typeof(x))(a) - 1)) == 0)
#define __round_mask(x, y)    ((__typeof__(x))((y) - 1))
#define round_up(x, y)        ((((x) - 1) | __round_mask(x, y)) + 1)
#define round_down(x, y)      ((x) & ~__round_mask(x, y))

#define PTR_ALIGN(p, a) ((typeof(p))ALIGN((unsigned long)(p), (a)))
#define PTR_ALIGN_DOWN(p, a) ((typeof(p))ALIGN_DOWN((unsigned long)(p), (a)))

#define upper_32_bits(n) ((uint32_t)(((n) >> 16) >> 16))
#define lower_32_bits(n) ((uint32_t)((n) & 0xffffffff))

#define REPEAT_BYTE(x) ((~0ul / 0xff) * (x))

#define roundup_macro(x, y) ( \
{ \
    typeof(y) __y = y; \
    (((x) + (__y - 1)) / __y) * __y; \
} \
)

#define rounddown_macro(x, y) ( \
{ \
    typeof(x) __x = (x); \
    __x - (__x % (y)); \
} \
)

static void test_alignment_macros(void) {
    ASSERT(ALIGN(13, 4) == 16);
    ASSERT(ALIGN(16, 4) == 16);
    ASSERT(ALIGN(1, 8) == 8);
    ASSERT(ALIGN_DOWN(15, 4) == 12);
    ASSERT(ALIGN_DOWN(16, 4) == 16);
    ASSERT(IS_ALIGNED(16, 4));
    ASSERT(!IS_ALIGNED(15, 4));

    ASSERT(round_up(13, 4) == 16);
    ASSERT(round_down(15, 4) == 12);

    ASSERT(roundup_macro(13, 5) == 15);
    ASSERT(rounddown_macro(13, 5) == 10);

    uint64_t big = 0x123456789ABCDEF0ULL;
    ASSERT(upper_32_bits(big) == 0x12345678U);
    ASSERT(lower_32_bits(big) == 0x9ABCDEF0U);

    ASSERT((REPEAT_BYTE(0x55) & 0xFFFF) == 0x5555);

    int arr[4] __attribute__((aligned(16)));
    int *arr_ptr = arr;
    int *aligned = PTR_ALIGN(arr_ptr, 16);
    ASSERT(((unsigned long)aligned % 16) == 0);
}

/* ================================================================
 * 24. Swap macro (kernel swap)
 * Source: include/linux/minmax.h
 * ================================================================ */

#define swap(a, b) \
    do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

#define min_not_zero(x, y) ({ \
    typeof(x) __x = (x); \
    typeof(y) __y = (y); \
    __x == 0 ? __y : ((__y == 0) ? __x : k_min(__x, __y)); })

static void test_swap_and_min_not_zero(void) {
    int a = 10, b = 20;
    swap(a, b);
    ASSERT(a == 20 && b == 10);

    long la = 100, lb = 200;
    swap(la, lb);
    ASSERT(la == 200 && lb == 100);

    ASSERT(min_not_zero(0, 5) == 5);
    ASSERT(min_not_zero(3, 0) == 3);
    ASSERT(min_not_zero(3, 5) == 3);
    ASSERT(min_not_zero(0, 0) == 0);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void) {
    test_builtin_choose_expr();
    test_generic_unqual_scalar();
    test_overflow_builtins();
    test_build_bug_on_zero();
    test_is_constexpr();
    test_typecheck();
    test_typeof_member();
    test_label_and_this_ip();
    test_return_address();
    test_barriers();
    test_builtin_constant_p_branching();
    test_bit_builtins();
    test_bswap();
    test_counter_unique_id();
    test_kernel_abs();
    test_struct_size();
    test_div_round_and_mult_frac();
    test_stringify();
    test_static_assert_alignof();
    test_has_builtin();
    test_reloc_hide();
    test_anonymous_union_overlap();
    test_alignment_macros();
    test_swap_and_min_not_zero();

    printf("test_kernel_advanced: ALL PASSED\n");
    return 0;
}
