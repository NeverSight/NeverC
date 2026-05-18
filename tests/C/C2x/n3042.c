// RUN: %clang_cc1 -verify -ffreestanding -Wno-unused -std=c2x %s

/* WG14 N3042: full
 * Introduce the nullptr constant
 *
 * NeverC adaption: static_assert failure message simplified
 * (no "due to requirement 'xxx'" suffix).
 */

#include <stddef.h>

#ifndef __STDC_VERSION_STDDEF_H__
#error "no version macro for stddef.h"
#endif
// expected-error@-2 {{"no version macro for stddef.h"}}

void questionable_behaviors() {
  nullptr_t val;

  (void)(1 ? val : 0);     // expected-error {{non-pointer operand type 'int' incompatible with nullptr}}
  (void)(1 ? nullptr : 0); // expected-error {{non-pointer operand type 'int' incompatible with nullptr}}

  _Bool another = val;    // expected-warning {{implicit conversion of nullptr constant to 'bool'}}
  another = val;          // expected-warning {{implicit conversion of nullptr constant to 'bool'}}
  _Bool again = nullptr;  // expected-warning {{implicit conversion of nullptr constant to 'bool'}}
  again = nullptr;        // expected-warning {{implicit conversion of nullptr constant to 'bool'}}
}

void test() {
  nullptr_t null_val;

  int *typed_ptr = nullptr;
  typed_ptr = nullptr;

  null_val = nullptr;
  nullptr_t ignore = nullptr;

  null_val = null_val;

  typed_ptr = null_val;

  &null_val;

  &nullptr; // expected-error {{cannot take the address of an rvalue of type 'nullptr_t'}}

  null_val = 0;
  null_val = (void *)0;

  typed_ptr = null_val;
  void *other_ptr = null_val;

  if (null_val) {}
  if (!null_val) {}
  for (;null_val;) {}
  while (nullptr) {}
  null_val && nullptr;
  nullptr || null_val;
  null_val ? 0 : 1;
  sizeof(null_val);
  alignas(nullptr_t) int aligned;

  (nullptr_t)12;        // expected-error {{cannot cast an object of type 'int' to 'nullptr_t'}}
  (float)null_val;      // expected-error {{cannot cast an object of type 'nullptr_t' to 'float'}}
  (float)nullptr;       // expected-error {{cannot cast an object of type 'nullptr_t' to 'float'}}
  (nullptr_t)0;         // expected-error {{cannot cast an object of type 'int' to 'nullptr_t'}}
  (nullptr_t)(void *)0; // expected-error {{cannot cast an object of type 'void *' to 'nullptr_t'}}
  (nullptr_t)(int *)12; // expected-error {{cannot cast an object of type 'int *' to 'nullptr_t'}}

  (void)null_val;
  (void)nullptr;
  (bool)null_val;
  (bool)nullptr;
  (int *)null_val;
  (int *)nullptr;
  (nullptr_t)nullptr;

  static_assert(!nullptr);
  static_assert(!null_val);
  static_assert(nullptr);  // expected-error {{static assertion failed}} \
                              expected-warning {{implicit conversion of nullptr constant to 'bool'}}
  static_assert(null_val); // expected-error {{static assertion failed}} \
                              expected-warning {{implicit conversion of nullptr constant to 'bool'}}

  static_assert(nullptr == nullptr);
  static_assert(null_val == null_val);
  static_assert(nullptr != (int*)1);
  static_assert(null_val != (int*)1);
  static_assert(nullptr == null_val);
  static_assert(nullptr == 0);
  static_assert(null_val == (void *)0);

  (void)(null_val <= 0);            // expected-error {{invalid operands to binary expression ('nullptr_t' and 'int')}}
  (void)(null_val >= (void *)0);    // expected-error {{invalid operands to binary expression ('nullptr_t' and 'void *')}}
  (void)(!(null_val < (void *)0));  // expected-error {{invalid operands to binary expression ('nullptr_t' and 'void *')}}
  (void)(!(null_val > 0));          // expected-error {{invalid operands to binary expression ('nullptr_t' and 'int')}}
  (void)(nullptr <= 0);             // expected-error {{invalid operands to binary expression ('nullptr_t' and 'int')}}
  (void)(nullptr >= (void *)0);     // expected-error {{invalid operands to binary expression ('nullptr_t' and 'void *')}}
  (void)(!(nullptr < (void *)0));   // expected-error {{invalid operands to binary expression ('nullptr_t' and 'void *')}}
  (void)(!(nullptr > 0));           // expected-error {{invalid operands to binary expression ('nullptr_t' and 'int')}}
  (void)(null_val <= null_val);     // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(null_val >= null_val);     // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(!(null_val < null_val));   // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(!(null_val > null_val));   // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(null_val <= nullptr);      // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(null_val >= nullptr);      // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(!(null_val < nullptr));    // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(!(null_val > nullptr));    // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(nullptr <= nullptr);       // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(nullptr >= nullptr);       // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(!(nullptr < nullptr));     // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}
  (void)(!(nullptr > nullptr));     // expected-error {{invalid operands to binary expression ('nullptr_t' and 'nullptr_t')}}

  _Generic(1 ? nullptr : nullptr, nullptr_t : 0);
  _Generic(1 ? null_val : null_val, nullptr_t : 0);
  _Generic(1 ? typed_ptr : null_val, typeof(typed_ptr) : 0);
  _Generic(1 ? null_val : typed_ptr, typeof(typed_ptr) : 0);
  _Generic(1 ? nullptr : typed_ptr, typeof(typed_ptr) : 0);
  _Generic(1 ? typed_ptr : nullptr, typeof(typed_ptr) : 0);

  _Generic(nullptr ?: nullptr, nullptr_t : 0);
  _Generic(null_val ?: null_val, nullptr_t : 0);
  _Generic(typed_ptr ?: null_val, typeof(typed_ptr) : 0);
  _Generic(null_val ?: typed_ptr, typeof(typed_ptr) : 0);
  _Generic(nullptr ?: typed_ptr, typeof(typed_ptr) : 0);
  _Generic(typed_ptr ?: nullptr, typeof(typed_ptr) : 0);

  int i = nullptr;   // expected-error {{initializing 'int' with an expression of incompatible type 'nullptr_t'}}
  float f = nullptr; // expected-error {{initializing 'float' with an expression of incompatible type 'nullptr_t'}}
  i = null_val;      // expected-error {{assigning to 'int' from incompatible type 'nullptr_t'}}
  f = null_val;      // expected-error {{assigning to 'float' from incompatible type 'nullptr_t'}}
  null_val = i;      // expected-error {{assigning to 'nullptr_t' from incompatible type 'int'}}
  null_val = f;      // expected-error {{assigning to 'nullptr_t' from incompatible type 'float'}}
}

void null_param(nullptr_t);

void other_test() {
  null_param(nullptr);
  null_param((void *)0);
  null_param(0);
}


void printf(const char*, ...) __attribute__((format(printf, 1, 2)));
void format_specifiers() {
  printf("%p", nullptr);
}

static_assert((nullptr_t){} == 0);
