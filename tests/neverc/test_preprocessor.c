// Preprocessor feature tests
// RUN: %neverc -std=c23 -c %s -o %t.o && echo "PASS: preprocessor compile"
// RUN: %neverc -std=c23 %s -o %t && %t && echo "PASS: preprocessor run"

#include <stdio.h>
#include <stdlib.h>

#define STRINGIFY(x) #x
#define CONCAT(a, b) a##b
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static void test_macros(void) {
    const char *s = STRINGIFY(hello);
    if (s[0] != 'h') abort();

    int CONCAT(my, var) = 42;
    if (myvar != 42) abort();

    if (MAX(3, 7) != 7) abort();
    if (MAX(10, 5) != 10) abort();
}

#define VA_COUNT(...) VA_COUNT_IMPL(__VA_ARGS__, 5, 4, 3, 2, 1)
#define VA_COUNT_IMPL(_1, _2, _3, _4, _5, N, ...) N

static void test_variadic_macros(void) {
    int n1 = VA_COUNT(a);
    int n2 = VA_COUNT(a, b);
    int n3 = VA_COUNT(a, b, c);
    if (n1 != 1) abort();
    if (n2 != 2) abort();
    if (n3 != 3) abort();
}

#if defined(__STDC__)
  #define IS_STDC 1
#else
  #define IS_STDC 0
#endif

#ifdef __STDC_VERSION__
  #define HAS_VERSION 1
#else
  #define HAS_VERSION 0
#endif

static void test_predefined(void) {
    if (!IS_STDC) abort();
    if (!HAS_VERSION) abort();

    const char *file = __FILE__;
    int line = __LINE__;
    (void)file;
    if (line <= 0) abort();
}

#define SQUARE(x) ((x) * (x))

static void test_complex_macros(void) {
    int result = SQUARE(2 + 3);
    if (result != 25) abort();

    #define LIST 1, 2, 3
    int arr[] = { LIST };
    if (arr[0] != 1 || arr[1] != 2 || arr[2] != 3) abort();
    #undef LIST
}

int main(void) {
    test_macros();
    test_variadic_macros();
    test_predefined();
    test_complex_macros();

    printf("test_preprocessor: ALL PASSED\n");
    return 0;
}
