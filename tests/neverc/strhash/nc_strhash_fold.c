// RUN: %nevercc -S -emit-llvm -O1 -fstrhash-fold %s -o - | FileCheck %s

// Verify that -fstrhash-fold folds runtime hash calls with constant
// arguments into compile-time constants (no call in output IR).

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern uint64_t neverc_fnv_sum64a(const void *data, size_t len);

// CHECK: define {{.*}}@test_fold_fnv64a
// CHECK-NOT: call {{.*}}neverc_fnv_sum64a
// CHECK: ret i64
uint64_t test_fold_fnv64a(void) {
  return neverc_fnv_sum64a("hello", 5);
}

// Non-constant argument should NOT be folded.
// CHECK: define {{.*}}@test_no_fold
// CHECK: call {{.*}}neverc_fnv_sum64a
uint64_t test_no_fold(const char *s, size_t len) {
  return neverc_fnv_sum64a(s, len);
}
