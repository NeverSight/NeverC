// RUN: %nevercc -S -emit-llvm -O1 %s -include neverc/xorstr/xorstr.h -o - | FileCheck %s

// Intermediate IR keeps an opaque call so a later final link can rekey it.

// CHECK: @{{.*}} = private {{.*}}constant
// CHECK: define {{.*}}@test_xorstr
// CHECK: call {{.*}}@__neverc_xorstr_decrypt
// CHECK-NOT: GetProcAddress
const char *test_xorstr(void) {
  return NC_XORSTR("GetProcAddress");
}
