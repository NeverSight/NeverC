// RUN: %nevercc -S -emit-llvm -O1 -fencrypt-call-strings %s -o - | FileCheck %s

// Verify that -fencrypt-call-strings auto-encrypts string arguments in calls.

extern void consume(const char *s);
extern void consume_wide(const unsigned short *s);

// CHECK: define {{.*}}@test_auto_encrypt
// CHECK: xorstr.buf
// CHECK: xorstr.loop
// CHECK-NOT: "hello auto"
void test_auto_encrypt(void) {
  consume("hello auto");
}

// The original plaintext string should not appear in the IR.
// CHECK-NOT: @.str{{.*}} = {{.*}}"hello auto"
