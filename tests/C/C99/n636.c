// RUN: %clang_cc1 -verify %s
// RUN: %clang_cc1 -verify=c2x -std=c2x %s

/* WG14 N636: yes
 * remove implicit function declaration
 *
 * NeverC adaption: default standard is C23, so undeclared function calls
 * produce "use of undeclared identifier" instead of the C99-specific message.
 */

void test(void) {
  frobble(); // expected-error {{use of undeclared identifier 'frobble'}} \
                c2x-error {{use of undeclared identifier 'frobble'}}
}
