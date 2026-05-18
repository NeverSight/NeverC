// RUN: %clang_cc1 -verify -std=c23 -Wgnu-folding-constant %s

/* WG14 N1330: Yes
 * Static assertions
 *
 * NeverC adaption: default is C23 so _Static_assert with no message is not
 * an extension warning. Test only the C23-standard behavior.
 */

int a;
_Static_assert(a, ""); // expected-error {{static assertion expression is not an integral constant expression}}
_Static_assert(1);

_Static_assert(1, "this works");
_Static_assert(0, "this fails"); // expected-error {{static assertion failed: this fails}}
_Static_assert(0); // expected-error {{static assertion failed}}

struct S {
  _Static_assert(1, "this works");
  union U {
    long l;
    _Static_assert(1, "this works");
  } u;
  enum E {
    _Static_assert(1, "this should not compile"); // expected-error {{expected identifier}}
    One
  } e;
};

void func(                                     // expected-note {{to match this '('}}
  _Static_assert(1, "this should not compile") // expected-error {{expected parameter declarator}} \
                                                  expected-error {{expected ')'}}
);
void func2(                                    // expected-note {{to match this '('}}
  _Static_assert(1, "this should not compile") // expected-error {{expected parameter declarator}} \
                                                  expected-error {{expected ')'}}
) {}

void test(void) {
  _Static_assert(1, "this works");
  _Static_assert(0, "this fails"); // expected-error {{static assertion failed: this fails}}

  int i = 0;
  for (_Static_assert(1, "this should not compile"); i < 10; ++i) // expected-error {{expected identifier or '('}} \
                                                                     expected-error {{expected ';' in 'for' statement specifier}}
    ;

  _Static_assert(1.0f, "this should not compile"); // expected-warning {{expression is not an integer constant expression; folding it to a constant is a GNU extension}}
}
