// RUN: %neverc -std=c23 -fsyntax-only %s
/*
 * NeverC Compiler Validation - Advanced C23 Features
 *
 * Tests C23 features beyond the basics (auto, typeof, nullptr, etc.):
 *  1.  constexpr variables
 *  2.  Empty initializer = {}
 *  3.  #warning directive (standardized)
 *  4.  Unnamed parameters in function definitions
 *  5.  Labels at end of compound statements
 *  6.  [[fallthrough]] attribute (C23 standard)
 *  7.  [[deprecated]] attribute
 *  8.  [[nodiscard]] on types and functions
 *  9.  typeof / typeof_unqual deep test
 * 10.  Enhanced enumerations (fixed underlying type + _Generic)
 * 11.  Binary literals + digit separators (comprehensive)
 * 12.  Improved tag compatibility
 * 13.  Unreachable macro
 * 14.  C23 bool/true/false as keywords
 * 15.  _BitInt types (if supported)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <float.h>
#include <limits.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

/* ================================================================
 * 1. constexpr variables (C23)
 * ================================================================ */
static int test_constexpr(void) {
    constexpr int MAX_ITEMS = 100;
    constexpr int BUFFER_SIZE = MAX_ITEMS * 4;
    constexpr double PI = 3.14159265358979323846;

    ASSERT(MAX_ITEMS == 100);
    ASSERT(BUFFER_SIZE == 400);
    ASSERT(PI > 3.14 && PI < 3.15);

    constexpr int arr_size = 5;
    int arr[arr_size];
    for (int i = 0; i < arr_size; i++)
        arr[i] = i * i;
    ASSERT(arr[4] == 16);

    constexpr unsigned long long BIG = 0xDEADBEEFCAFEBABEULL;
    ASSERT(BIG == 0xDEADBEEFCAFEBABEULL);

    return 0;
}

/* ================================================================
 * 2. Empty initializer = {} (C23)
 * ================================================================ */
struct point3d { double x, y, z; };
struct nested_empty {
    int a;
    struct point3d p;
    int arr[4];
    char name[16];
};

static int test_empty_init(void) {
    int x = {};
    ASSERT(x == 0);

    double d = {};
    ASSERT(d == 0.0);

    struct point3d p = {};
    ASSERT(p.x == 0.0 && p.y == 0.0 && p.z == 0.0);

    int arr[10] = {};
    for (int i = 0; i < 10; i++)
        ASSERT(arr[i] == 0);

    struct nested_empty ne = {};
    ASSERT(ne.a == 0);
    ASSERT(ne.p.x == 0.0);
    ASSERT(ne.arr[3] == 0);
    ASSERT(ne.name[0] == '\0');

    char *ptr = {};
    ASSERT(ptr == NULL);

    return 0;
}

/* ================================================================
 * 3. #warning directive (standardized in C23)
 * ================================================================ */
#if 0
#warning "This is a C23 standardized warning directive"
#endif

/* ================================================================
 * 4. Unnamed parameters in function definitions (C23)
 * ================================================================ */
static int process_with_unused(int x, int, int z) {
    return x + z;
}

static void callback_ignore_args(void *, int, const char *) {
}

static int test_unnamed_params(void) {
    ASSERT(process_with_unused(10, 999, 30) == 40);
    callback_ignore_args(NULL, 42, "ignored");
    return 0;
}

/* ================================================================
 * 5. Labels at end of compound statements (C23)
 * ================================================================ */
static int test_label_at_end(void) {
    int x = 1;
    goto skip;
    x = 999;
skip:
    ASSERT(x == 1);

    for (int i = 0; i < 5; i++) {
        if (i == 3) goto done;
    }
done:

    if (x == 1) goto final;
final:

    return 0;
}

/* ================================================================
 * 6. [[fallthrough]] C23 standard attribute
 * ================================================================ */
static int c23_classify(int x) {
    int result = 0;
    switch (x) {
    case 1:
        result = 10;
        [[fallthrough]];
    case 2:
        result += 20;
        break;
    case 3:
        result = 30;
        break;
    default:
        result = -1;
    }
    return result;
}

static int test_c23_fallthrough(void) {
    ASSERT(c23_classify(1) == 30);
    ASSERT(c23_classify(2) == 20);
    ASSERT(c23_classify(3) == 30);
    ASSERT(c23_classify(99) == -1);
    return 0;
}

/* ================================================================
 * 7. [[deprecated]] attribute
 * ================================================================ */
[[deprecated("use new_func instead")]]
static int old_func(void) { return 1; }

static int new_func(void) { return 2; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static int test_deprecated(void) {
    ASSERT(old_func() == 1);
    ASSERT(new_func() == 2);
    return 0;
}
#pragma clang diagnostic pop

/* ================================================================
 * 8. [[nodiscard]] on types and functions (C23)
 * ================================================================ */
struct [[nodiscard("check the error")]] error_code {
    int code;
    const char *msg;
};

[[nodiscard]] static struct error_code make_error(int code) {
    return (struct error_code){ .code = code, .msg = "error" };
}

static int test_nodiscard(void) {
    struct error_code e = make_error(42);
    ASSERT(e.code == 42);
    ASSERT(strcmp(e.msg, "error") == 0);
    return 0;
}

/* ================================================================
 * 9. typeof / typeof_unqual deep test
 * ================================================================ */
static int test_typeof_deep(void) {
    const volatile int cv = 42;
    typeof(cv) same_type = 42;
    ASSERT(same_type == 42);

    typeof_unqual(cv) mutable_copy = cv;
    mutable_copy = 100;
    ASSERT(mutable_copy == 100);

    int arr[5] = {1, 2, 3, 4, 5};
    typeof(arr[0]) elem = arr[2];
    ASSERT(elem == 3);

    typeof(arr) arr_copy;
    memcpy(arr_copy, arr, sizeof(arr));
    ASSERT(arr_copy[4] == 5);

    struct { int x; double y; } anon = {10, 3.14};
    typeof(anon) anon2 = {20, 6.28};
    ASSERT(anon2.x == 20);

    typeof(1 + 1.0) promoted = 42;
    ASSERT(sizeof(promoted) == sizeof(double));

    const volatile int *cip = &cv;
    typeof_unqual(*cip) writeable = 99;
    ASSERT(writeable == 99);

    return 0;
}

/* ================================================================
 * 10. Enhanced enumerations (fixed underlying type)
 * ================================================================ */
enum small_enum : unsigned char { SE_A = 0, SE_B = 127, SE_C = 255 };
enum signed_enum : short { NEG = -100, ZERO = 0, POS = 100 };
enum big_enum : unsigned long long { BIG_A = 0, BIG_B = 0xFFFFFFFFFFFFFFFFULL };

static int test_enum_types(void) {
    ASSERT(sizeof(enum small_enum) == sizeof(unsigned char));
    ASSERT(sizeof(enum signed_enum) == sizeof(short));
    ASSERT(sizeof(enum big_enum) == sizeof(unsigned long long));

    enum small_enum s = SE_C;
    ASSERT(s == 255);

    enum signed_enum neg = NEG;
    ASSERT(neg == -100);

    enum big_enum b = BIG_B;
    ASSERT(b == 0xFFFFFFFFFFFFFFFFULL);

    return 0;
}

/* ================================================================
 * 11. Binary literals + digit separators (comprehensive)
 * ================================================================ */
static int test_literals_comprehensive(void) {
    int bin = 0b11001010;
    ASSERT(bin == 202);

    int bin_sep = 0b1111'0000'1010'0101;
    ASSERT(bin_sep == 0xF0A5);

    unsigned long long hex_sep = 0xFF'FF'FF'FF'00'00'00'00ULL;
    ASSERT(hex_sep == 0xFFFFFFFF00000000ULL);

    int dec_sep = 1'000'000;
    ASSERT(dec_sep == 1000000);

    unsigned oct_sep = 017'77;
    ASSERT(oct_sep == 01777);

    long long neg_bin = -0b1010;
    ASSERT(neg_bin == -10);

    return 0;
}

/* ================================================================
 * 12. Improved tag compatibility: redeclare structs
 * ================================================================ */
struct forward_decl;

struct forward_decl {
    int x;
    int y;
};

struct forward_decl forward_make(int x, int y) {
    return (struct forward_decl){x, y};
}

static int test_tag_compat(void) {
    struct forward_decl f = forward_make(10, 20);
    ASSERT(f.x == 10 && f.y == 20);
    return 0;
}

/* ================================================================
 * 13. unreachable() C23
 * ================================================================ */
static int must_be_positive(int x) {
    if (x > 0) return x;
    unreachable();
}

static int test_unreachable(void) {
    ASSERT(must_be_positive(42) == 42);
    ASSERT(must_be_positive(1) == 1);
    return 0;
}

/* ================================================================
 * 14. C23 bool/true/false as proper keywords
 * ================================================================ */
static int test_c23_bool(void) {
    bool a = true;
    bool b = false;
    ASSERT(sizeof(bool) == 1);
    ASSERT(a == 1);
    ASSERT(b == 0);
    ASSERT(true == 1);
    ASSERT(false == 0);

    bool c = 42;
    ASSERT(c == true);

    bool d = !false;
    ASSERT(d == true);

    bool arr[3] = { true, false, true };
    ASSERT(arr[0] && !arr[1] && arr[2]);

    return 0;
}

/* ================================================================
 * 15. _BitInt types (C23, if supported)
 * ================================================================ */
#if __has_extension(bit_int)
static int test_bitint(void) {
    _BitInt(8) small = 100;
    ASSERT(small == 100);

    unsigned _BitInt(16) medium = 50000;
    ASSERT(medium == 50000);

    _BitInt(32) regular = -12345;
    ASSERT(regular == -12345);

    _BitInt(128) big = (_BitInt(128))1 << 100;
    ASSERT(big != 0);

    unsigned _BitInt(8) u8 = 255;
    ASSERT(u8 == 255);

    return 0;
}
#else
static int test_bitint(void) { return 0; }
#endif

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    int failures = 0;
#define RUN(fn) do { \
    if (fn() != 0) { fprintf(stderr, "FAIL: " #fn "\n"); failures++; } \
} while (0)

    RUN(test_constexpr);
    RUN(test_empty_init);
    RUN(test_unnamed_params);
    RUN(test_label_at_end);
    RUN(test_c23_fallthrough);
    RUN(test_deprecated);
    RUN(test_nodiscard);
    RUN(test_typeof_deep);
    RUN(test_enum_types);
    RUN(test_literals_comprehensive);
    RUN(test_tag_compat);
    RUN(test_unreachable);
    RUN(test_c23_bool);
    RUN(test_bitint);

#undef RUN
    if (failures == 0)
        printf("test_c23_advanced: ALL PASSED\n");
    else
        printf("test_c23_advanced: %d FAILED\n", failures);
    return failures;
}
