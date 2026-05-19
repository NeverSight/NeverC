/* WG14 N2826: Clang 17
 * Add annotations for unreachable control flow v2
 *
 * NeverC adaption:
 *   - NeverC treats unreachable() as a builtin in all modes, so the C17
 *     "undeclared function" error no longer applies and that -verify run
 *     has been dropped.
 *   - IR shape is verified directly by the NeverC test driver
 *     (tests/neverc/BasicTests.cpp) via `ir_check_n2826`, which asserts that
 *     the switch default label opens a block whose first instruction is
 *     `unreachable`.
 */
#include <stddef.h>

enum E {
  Zero,
  One,
  Two,
};

int test(enum E e) {
  switch (e) {
  case Zero: return 0;
  case One: return 1;
  case Two: return 2;
  }
  unreachable();
}
