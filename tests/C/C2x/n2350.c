// RUN: %clang_cc1 -fsyntax-only -verify=silent %s
// RUN: %clang_cc1 -fsyntax-only -pedantic -Wno-comment -verify %s
// RUN: %clang_cc1 -fsyntax-only -pedantic -Wno-comment -std=c89 -verify %s
// RUN: %clang_cc1 -fsyntax-only -pedantic -Wno-comment -std=c99 -verify %s
// RUN: %clang_cc1 -fsyntax-only -pedantic -Wno-comment -std=c11 -verify %s
// RUN: %clang_cc1 -fsyntax-only -pedantic -Wno-comment -std=c17 -verify %s
// RUN: %clang_cc1 -fsyntax-only -pedantic -Wno-comment -std=c2x -verify %s

// silent-no-diagnostics

/* NeverC adaption: removed -x c++ RUN line (NeverC is C-only).
 * Changed "NeverC extension" to "NeverC extension".
 */

// Reject definitions in __builtin_offsetof
// https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2350.htm
int simple(void) {
  return __builtin_offsetof(struct A // expected-warning {{defining a type within '__builtin_offsetof' is a NeverC extension}}
  {
    int a;
    struct B // expected-warning {{defining a type within '__builtin_offsetof' is a NeverC extension}}
    {
      int c;
      int d;
    } x;
  }, a);
}

int anonymous_struct(void) {
  return __builtin_offsetof(struct // expected-warning {{defining a type within '__builtin_offsetof' is a NeverC extension}}
  {
    int a;
    int b;
  }, a);
}

int struct_in_second_param(void) {
  struct A {
    int a, b;
    int x[20];
  };
  return __builtin_offsetof(struct A, x[sizeof(struct B{int a;})]);
}


#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)


int macro(void) {
  return offsetof(struct A // expected-warning 2 {{defining a type within 'offsetof' is a NeverC extension}}
  {
    int a;
    struct B
    {
      int c;
      int d;
    } x;
  }, a);
}

#undef offsetof

#define offsetof(TYPE, MEMBER) (&((TYPE *)0)->MEMBER)

// no warning for traditional offsetof as a function-like macro
int * macro_func(void) {
  return offsetof(struct A
  {
    int a;
    int b;
  }, a);
}
