// RUN: %nevercc -S -emit-llvm -O0 %s -include neverc/strhash/strhash.h -o - | FileCheck %s --check-prefix=CHECK-DEFAULT
// RUN: %nevercc -S -emit-llvm -O0 -fstrhash-algo=fnv32a %s -include neverc/strhash/strhash.h -o - | FileCheck %s --check-prefix=CHECK-FNV32

// Verify that NC_STRHASH is folded to a constant integer at -O0 (Sema level).
// The builtin should produce a constant — no function call in the IR.

// CHECK-DEFAULT: define {{.*}}@test_default
// CHECK-DEFAULT: ret i64
// CHECK-DEFAULT-NOT: call {{.*}}neverc_fnv

// FNV-32a returns a value that fits in 32 bits (zero-extended to 64).
// CHECK-FNV32: define {{.*}}@test_default
// CHECK-FNV32: ret i64

#include <stdint.h>

uint64_t test_default(void) {
  return NC_STRHASH("hello");
}
