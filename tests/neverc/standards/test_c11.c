// test_c11.c - C11 feature tests
// RUN: %neverc -std=c11 -c %s -o %t.o && echo "PASS: c11 compile"
// RUN: %neverc -std=c11 %s -o %t && %t && echo "PASS: c11 run"

#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <stdnoreturn.h>
#include <stdbool.h>
#include <stdatomic.h>

noreturn void die(const char *msg) {
    fprintf(stderr, "FATAL: %s\n", msg);
    abort();
}

static void test_generic(void) {
    int i = 42;
    double d = 3.14;
    const char *ti = _Generic(i, int: "int", double: "double", default: "other");
    const char *td = _Generic(d, int: "int", double: "double", default: "other");

    if (ti[0] != 'i') die("_Generic int failed");
    if (td[0] != 'd') die("_Generic double failed");

    float f = 1.0f;
    int size = _Generic(f,
        float:  sizeof(float),
        double: sizeof(double),
        default: 0
    );
    if (size != sizeof(float)) die("_Generic float size failed");
}

static void test_static_assert(void) {
    _Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");
    _Static_assert(sizeof(char) == 1, "char must be 1 byte");
    _Static_assert(1 + 1 == 2, "math check");
}

static void test_alignas(void) {
    _Alignas(16) int aligned_var = 42;
    if ((uintptr_t)&aligned_var % 16 != 0) die("alignas(16) failed");
    if (aligned_var != 42) die("aligned_var value wrong");
}

static void test_anonymous_structs(void) {
    struct Outer {
        int tag;
        union {
            int i;
            float f;
        };
    };

    struct Outer o = {.tag = 1, .i = 99};
    if (o.tag != 1) die("anonymous union tag failed");
    if (o.i != 99) die("anonymous union value failed");
}

static void test_atomics(void) {
    _Atomic int ai = 0;
    atomic_store(&ai, 42);
    int val = atomic_load(&ai);
    if (val != 42) die("atomic load/store failed");

    int expected = 42;
    bool ok = atomic_compare_exchange_strong(&ai, &expected, 100);
    if (!ok) die("atomic CAS should succeed");
    if (atomic_load(&ai) != 100) die("atomic CAS value wrong");
}

struct CLPoint { int x, y; };

static void test_compound_literals(void) {
    int *p = (int[]){10, 20, 30};
    if (p[1] != 20) die("compound literal failed");

    struct CLPoint *sp = &(struct CLPoint){5, 10};
    if (sp->x != 5 || sp->y != 10) die("struct compound literal failed");
}

int main(void) {
    test_generic();
    test_static_assert();
    test_alignas();
    test_anonymous_structs();
    test_atomics();
    test_compound_literals();

    printf("test_c11: ALL PASSED\n");
    return 0;
}
