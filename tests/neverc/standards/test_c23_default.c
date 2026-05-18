// RUN: %neverc -fsyntax-only %s
/* Test C23 features work WITHOUT -std=c23 (default gnu23) */
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* 1. static_assert without message (C23) */
static_assert(sizeof(int) >= 4);
static_assert(sizeof(char) == 1);
static_assert(sizeof(long long) >= 8);
static_assert(1 + 1 == 2);

/* 2. constexpr (C23) */
constexpr int BUFFER_SIZE = 256;
constexpr int TABLE_SIZE = 16;
constexpr double PI = 3.14159265358979;

/* 3. typeof / typeof_unqual (C23) */
static void test_typeof(void) {
    int x = 42;
    typeof(x) y = x * 2;
    if (y != 84) { fprintf(stderr, "FAIL: typeof\n"); __builtin_abort(); }

    const int cx = 10;
    typeof_unqual(cx) mutable_copy = cx;
    mutable_copy += 5;
    if (mutable_copy != 15) { fprintf(stderr, "FAIL: typeof_unqual\n"); __builtin_abort(); }

    typeof(1 + 2.0) d = 3.14;
    if (d < 3.0) { fprintf(stderr, "FAIL: typeof expr\n"); __builtin_abort(); }
}

/* 4. nullptr (C23) */
static void test_nullptr(void) {
    int *p = nullptr;
    if (p != nullptr) { fprintf(stderr, "FAIL: nullptr init\n"); __builtin_abort(); }

    char *s = nullptr;
    if (s != nullptr) { fprintf(stderr, "FAIL: nullptr char*\n"); __builtin_abort(); }

    void (*fp)(void) = nullptr;
    if (fp != nullptr) { fprintf(stderr, "FAIL: nullptr funcptr\n"); __builtin_abort(); }

    /* nullptr is its own type nullptr_t */
    nullptr_t n = nullptr;
    (void)n;
}

/* 5. auto type inference (C23) */
static void test_auto(void) {
    auto x = 42;
    static_assert(sizeof(x) == sizeof(int));

    auto d = 3.14;
    if (d < 3.0 || d > 4.0) { fprintf(stderr, "FAIL: auto double\n"); __builtin_abort(); }

    auto c = 'A';
    if (c != 'A') { fprintf(stderr, "FAIL: auto char\n"); __builtin_abort(); }

    auto ul = 100UL;
    static_assert(sizeof(ul) == sizeof(unsigned long));
}

/* 6. constexpr used at runtime */
static void test_constexpr(void) {
    char buf[BUFFER_SIZE];
    memset(buf, 0, BUFFER_SIZE);
    buf[0] = 'H';
    if (buf[0] != 'H') { fprintf(stderr, "FAIL: constexpr array\n"); __builtin_abort(); }

    int table[TABLE_SIZE];
    if (sizeof(table) != TABLE_SIZE * sizeof(int)) { fprintf(stderr, "FAIL: table size\n"); __builtin_abort(); }
    for (int i = 0; i < TABLE_SIZE; i++) table[i] = i * i;
    if (table[4] != 16) { fprintf(stderr, "FAIL: constexpr table\n"); __builtin_abort(); }
}

/* 7. bool/true/false as keywords (C23) */
static void test_bool_keywords(void) {
    bool a = true;
    bool b = false;
    if (!a) { fprintf(stderr, "FAIL: true\n"); __builtin_abort(); }
    if (b) { fprintf(stderr, "FAIL: false\n"); __builtin_abort(); }
    static_assert(sizeof(bool) == 1);
    static_assert(true == 1);
    static_assert(false == 0);
}

int main(void) {
    test_typeof();
    test_nullptr();
    test_auto();
    test_constexpr();
    test_bool_keywords();

    printf("test_c23_default: ALL PASSED\n");
    return 0;
}
