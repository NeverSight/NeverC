// Test that -fstrhash-algo selects different algorithms correctly.
//
// RUN: %nevercc -fsyntax-only -fstrhash-algo=fnv32a %s -include neverc/strhash/strhash.h 2>&1 | FileCheck %s --check-prefix=CHECK-OK
// RUN: %nevercc -fsyntax-only -fstrhash-algo=fnv64a %s -include neverc/strhash/strhash.h 2>&1 | FileCheck %s --check-prefix=CHECK-OK
// RUN: %nevercc -fsyntax-only -fstrhash-algo=xxhash64 %s -include neverc/strhash/strhash.h 2>&1 | FileCheck %s --check-prefix=CHECK-OK
// RUN: not %nevercc -fsyntax-only -fstrhash-algo=invalid %s -include neverc/strhash/strhash.h 2>&1 | FileCheck %s --check-prefix=CHECK-BAD-ALGO

// CHECK-OK-NOT: error
// CHECK-BAD-ALGO: error: invalid value 'invalid' in '-fstrhash-algo=invalid'

#include <stdint.h>

uint64_t test(void) {
  return NC_STRHASH("test");
}
