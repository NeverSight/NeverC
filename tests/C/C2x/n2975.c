// RUN: %clang_cc1 -verify -ffreestanding -Wpre-c2x-compat -std=c2x %s

/* WG14 N2975: partial
 * Relax requirements for va_start
 *
 * NeverC adaption: incompatible function pointer types changed from error
 * to warning.
 */

#include <stdarg.h>

#define DERP this is an error

void func(...) { // expected-warning {{'...' as the only parameter of a function is incompatible with C standards before C23}}
  va_list list;
  va_start(list);
  va_end(list);

  va_start(list, DERP);
  va_end(list);

  va_start(list, 1, 2);
  va_end(list);

  __builtin_va_start(list); // expected-error {{too few arguments to function call, expected 2, have 1}}

  _Static_assert(__builtin_types_compatible_p(__typeof__(va_start(list)), void), "");
  _Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_va_start(list, 0)), void), "");
}

typedef void (*fp)(...); // expected-warning {{'...' as the only parameter of a function is incompatible with C standards before C23}}

void diag(int a, int b, ...) {
  va_list list;
  va_start(list, a);
  __builtin_va_start(list, a); // expected-warning {{second argument to 'va_start' is not the last named parameter}}
  va_end(list);
}

void foo(int a...); // expected-error {{C requires a comma prior to the ellipsis in a variadic function type}}

void use(void) {
  func(1, '2', 3.0, "4");
  func();

  fp local = func;

  fp other_local = diag; // expected-warning {{incompatible function pointer types initializing 'fp' (aka 'void (*)(...)') with an expression of type 'void (int, int, ...)'}}
}
