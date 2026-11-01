; SPDX-License-Identifier: AGPL-3.0-only
declare void @llvm.x86.psmash.context(ptr,ptr,i32 immarg)
declare void @llvm.x86.pvalidate.context(ptr,ptr,i32 immarg)
declare void @llvm.x86.rmpadjust.context(ptr,ptr,i32 immarg)
declare void @llvm.x86.rmpupdate.context(ptr,ptr,i32 immarg)
declare void @llvm.x86.rmpquery.context(ptr,ptr,i32 immarg)
declare void @llvm.x86.rmpread.context(ptr,ptr,i32 immarg)
declare void @llvm.x86.rmpopt.context(ptr,ptr,i32 immarg)
declare void @llvm.x86.rmpchkd.context(ptr,ptr,i32 immarg)
declare void @llvm.x86.vmgexit.sev(i64)
define i64 @candidate(i32 %form,ptr %g,ptr %flags,i64 %seed,ptr %fp,ptr %out) {
entry:
  %p0 = getelementptr double, ptr %fp, i64 0
  %v0 = load double, ptr %p0, align 1
  %p1 = getelementptr double, ptr %fp, i64 1
  %v1 = load double, ptr %p1, align 1
  %p2 = getelementptr double, ptr %fp, i64 2
  %v2 = load double, ptr %p2, align 1
  %p3 = getelementptr double, ptr %fp, i64 3
  %v3 = load double, ptr %p3, align 1
  %p4 = getelementptr double, ptr %fp, i64 4
  %v4 = load double, ptr %p4, align 1
  %p5 = getelementptr double, ptr %fp, i64 5
  %v5 = load double, ptr %p5, align 1
  %i0 = add i64 %seed,7
  %i1 = mul i64 %seed,13
  %i2 = xor i64 %seed,18364758544493064720
  %i3 = add i64 %seed,1311768467463790320
  switch i32 %form,label %bad [
    i32 0,label %op0
    i32 1,label %op1
    i32 2,label %op2
    i32 3,label %op3
    i32 4,label %op4
    i32 5,label %op5
    i32 6,label %op6
    i32 7,label %op7
    i32 8,label %op8
    i32 9,label %op9
    i32 10,label %op10
    i32 11,label %op11
  ]
op0:
  call void @llvm.x86.psmash.context(ptr %g,ptr %flags,i32 0)
  br label %done
op1:
  call void @llvm.x86.pvalidate.context(ptr %g,ptr %flags,i32 0)
  br label %done
op2:
  call void @llvm.x86.rmpadjust.context(ptr %g,ptr %flags,i32 0)
  br label %done
op3:
  call void @llvm.x86.rmpupdate.context(ptr %g,ptr %flags,i32 0)
  br label %done
op4:
  call void @llvm.x86.rmpquery.context(ptr %g,ptr %flags,i32 0)
  br label %done
op5:
  call void @llvm.x86.rmpread.context(ptr %g,ptr %flags,i32 0)
  br label %done
op6:
  call void @llvm.x86.rmpopt.context(ptr %g,ptr %flags,i32 0)
  br label %done
op7:
  call void @llvm.x86.rmpchkd.context(ptr %g,ptr %flags,i32 0)
  br label %done
op8:
  call void @llvm.x86.pvalidate.context(ptr %g,ptr %flags,i32 1)
  br label %done
op9:
  %param9 = load i64, ptr %g, align 1
  call void @llvm.x86.vmgexit.sev(i64 %param9)
  br label %done
op10:
  %param10 = load i64, ptr %g, align 1
  call void @llvm.x86.vmgexit.sev(i64 %param10)
  br label %done
op11:
  call void @llvm.x86.rmpread.context(ptr %g,ptr %flags,i32 25856)
  br label %done
bad:
  unreachable
done:
  %f0 = fadd double %v0,%v1
  %f1 = fsub double %v2,%v3
  %f2 = fmul double %v4,%v5
  store double %f0,ptr %out,align 1
  %o1 = getelementptr double,ptr %out,i64 1
  store double %f1,ptr %o1,align 1
  %o2 = getelementptr double,ptr %out,i64 2
  store double %f2,ptr %o2,align 1
  %a0 = xor i64 %i0,%i1
  %a1 = xor i64 %a0,%i2
  %r = xor i64 %a1,%i3
  ret i64 %r
}
