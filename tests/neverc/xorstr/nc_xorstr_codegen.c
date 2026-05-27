// RUN: %nevercc -S -emit-llvm -O1 %s -include neverc/xorstr.h -o - | FileCheck %s

// Verify that NC_XORSTR generates a call to __neverc_xorstr_decrypt
// and that the encrypted string literal is different from the original.

// CHECK: @{{.*}} = private {{.*}}constant
// CHECK: define {{.*}}@test_xorstr
// CHECK: call {{.*}}@__neverc_xorstr_decrypt
// CHECK-NOT: GetProcAddress
const char *test_xorstr(void) {
  return NC_XORSTR("GetProcAddress");
}
