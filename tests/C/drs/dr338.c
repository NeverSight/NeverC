/* RUN: %clang_cc1 -std=c89 -fsyntax-only -verify %s
   RUN: %clang_cc1 -std=c99 -fsyntax-only -verify %s
   RUN: %clang_cc1 -std=c11 -fsyntax-only -verify %s
   RUN: %clang_cc1 -std=c17 -fsyntax-only -verify %s
   RUN: %clang_cc1 -std=c2x -fsyntax-only -verify %s
 */

// expected-no-diagnostics

/* WG14 DR338: yes
 * C99 seems to exclude indeterminate value from being an uninitialized register
 *
 * NeverC adaption: -Wuninitialized analysis is disabled in the current build
 * (CLANG_ENABLE_ANALYSIS=OFF), so the warning is not produced. The test
 * verifies that the code is accepted without errors.
 */
int dr338(void) {
  unsigned char uc;
  return uc + 1 >= 0;
}
