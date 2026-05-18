// RUN: %neverc -std=c23 -Wall -Werror -c %s -o %t.c23.o
// RUN: test -s %t.c23.o
// RUN: %neverc -std=c23 -O2 -c %s -o %t.c23.opt.o
// RUN: test -s %t.c23.opt.o
// RUN: %neverc -std=c23 -flto -c %s -o %t.c23.lto.o
// RUN: test -s %t.c23.lto.o
// RUN: %neverc -target x86_64-apple-darwin -std=c23 -c %s -o %t.c23.x64.o
// RUN: test -s %t.c23.x64.o

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

static_assert(sizeof(int) >= 4, "int must be at least 32 bits");
static_assert(sizeof(char) == 1);

void test_auto_type_inference(void) {
    auto x = 42;
    auto y = 3.14;
    auto z = 'A';
    auto ptr = &x;
    (void)x; (void)y; (void)z; (void)ptr;
}

void test_typeof(void) {
    int x = 42;
    typeof(x) y = x + 1;
    typeof_unqual(const int) z = 100;
    (void)y; (void)z;
}

void test_nullptr(void) {
    nullptr_t np = nullptr;
    int *p = nullptr;
    if (p == nullptr) {
        (void)np;
    }
    bool b = (p != nullptr);
    (void)b;
}

void test_bool_keywords(void) {
    bool a = true;
    bool b = false;
    bool c = !a;
    (void)a; (void)b; (void)c;
}

void test_static_assert_no_message(void) {
    static_assert(1 + 1 == 2);
    static_assert(sizeof(void *) >= 4);
}

enum Color : int { RED, GREEN, BLUE };
enum SmallFlags : unsigned char { FLAG_A = 1, FLAG_B = 2, FLAG_C = 4 };

void test_enum_with_underlying(void) {
    enum Color c = RED;
    enum SmallFlags f = FLAG_A | FLAG_B;
    static_assert(sizeof(enum SmallFlags) == sizeof(unsigned char));
    (void)c; (void)f;
}

void test_label_at_end(void) {
    int x = 0;
    if (x) {
        goto end;
    }
    x = 1;
end:
}

[[nodiscard]] int must_use(void) { return 42; }
[[maybe_unused]] static int unused_thing = 0;
[[deprecated("use new_func")]] static int old_func(void) { return 0; }

void test_attributes(void) {
    (void)must_use();
}

void test_digit_separators(void) {
    int million = 1'000'000;
    long hex = 0xFF'FF'FF'FF;
    (void)million; (void)hex;
}

constexpr int compile_time_value = 42;
constexpr int doubled = compile_time_value * 2;

void test_constexpr(void) {
    static_assert(compile_time_value == 42);
    static_assert(doubled == 84);
    constexpr int local = 10;
    (void)local;
}

void test_unreachable(int x) {
    switch (x) {
    case 0: return;
    case 1: return;
    default: unreachable();
    }
}

int main(void) {
    test_auto_type_inference();
    test_typeof();
    test_nullptr();
    test_bool_keywords();
    test_static_assert_no_message();
    test_enum_with_underlying();
    test_label_at_end();
    test_attributes();
    test_digit_separators();
    test_constexpr();
    return 0;
}
