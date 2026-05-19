// C23 feature tests
// RUN: %neverc -std=c23 -c %s -o %t.o && echo "PASS: c23 compile"
// RUN: %neverc -std=c23 %s -o %t && %t && echo "PASS: c23 run"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

static void test_auto(void) {
    auto x = 42;
    auto y = 3.14;
    auto z = 'A';
    auto p = (int *)0;

    if (sizeof(x) != sizeof(int)) abort();
    if (sizeof(y) != sizeof(double)) abort();
    if (sizeof(z) != sizeof(int)) abort();
    if (sizeof(p) != sizeof(int *)) abort();

    auto arr_val = 100;
    auto *ptr = &arr_val;
    *ptr = 200;
    if (arr_val != 200) abort();
}

static void test_typeof(void) {
    int original = 42;
    typeof(original) copy = original;
    if (copy != 42) abort();

    double d = 3.14;
    typeof(d) d2 = d + 1.0;
    if (d2 < 4.13 || d2 > 4.15) abort();

    typeof_unqual(const int) mutable_val = 10;
    mutable_val = 20;
    if (mutable_val != 20) abort();
}

static void test_bool(void) {
    bool t = true;
    bool f = false;
    if (!t) abort();
    if (f) abort();
    if (sizeof(bool) != 1) abort();
}

static void test_nullptr(void) {
    int *p = nullptr;
    if (p != nullptr) abort();

    void *vp = nullptr;
    if (vp != nullptr) abort();
}

static void test_static_assert(void) {
    static_assert(sizeof(int) >= 4);
    static_assert(true);
    static_assert(1 == 1, "one equals one");
}

enum Color : int { RED = 0, GREEN = 1, BLUE = 2 };

static void test_enum_underlying(void) {
    enum Color c = GREEN;
    if (c != 1) abort();
    if (sizeof(enum Color) != sizeof(int)) abort();
}

static void test_digit_separator(void) {
    int million = 1'000'000;
    if (million != 1000000) abort();

    long long big = 0xFF'FF'FF'FFLL;
    if (big != 0xFFFFFFFFL) abort();

    int bin = 0b1010'0101;
    if (bin != 0xA5) abort();
}

[[nodiscard]] static int important_func(void) {
    return 42;
}

static void test_attributes(void) {
    int result = important_func();
    if (result != 42) abort();

    [[maybe_unused]] int unused_var = 100;
}

static void test_unreachable(void) {
    int x = 1;
    switch (x) {
    case 1: break;
    default: __builtin_unreachable();
    }
}

int main(void) {
    test_auto();
    test_typeof();
    test_bool();
    test_nullptr();
    test_static_assert();
    test_enum_underlying();
    test_digit_separator();
    test_attributes();
    test_unreachable();

    printf("test_c23: ALL PASSED\n");
    return 0;
}
