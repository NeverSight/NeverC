; RUN: opt -passes='xorstr-cleanup' -S < %s | FileCheck %s

; Verify that XorStrCleanupPass inserts memset before ret for xorstr allocas.

define i32 @test_cleanup() {
entry:
  %buf = alloca [16 x i8], !neverc.xorstr !0
  store i8 65, ptr %buf
  ; CHECK: call void @llvm.memset
  ; CHECK-NEXT: ret i32 0
  ret i32 0
}

!0 = !{}
