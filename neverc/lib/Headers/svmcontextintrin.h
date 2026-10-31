/*===---- svmcontextintrin.h - Explicit AMD VMRUN context ------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*/

#ifndef __SVMCONTEXTINTRIN_H
#define __SVMCONTEXTINTRIN_H

#if !defined(__x86_64__) && !defined(_M_X64)
#error "VMRUN context requires x86-64"
#endif

/* Native CPL0 transition. VMCB is a physical address; the two xstate addresses
 * are ordinary linear addresses of distinct, 64-byte-aligned, CPUID-sized
 * standard XSAVE buffers. Initialize reserved XSAVE-header bytes to zero.
 * XStateMask must cover all enabled XCR0 components. Keep XCR0 unchanged while
 * the guest executes. GuestXState is input/output; HostXState is scratch.
 * The context and buffers must remain mapped, writable, and disjoint from the
 * guest's writable memory. The compiler preserves its own frame and host user
 * xstate; GPRs are explicit guest input/output, even when caller values are
 * dead. RAX, RSP, RIP and RFLAGS belong to the guest VMCB. VMRUN/VMEXIT
 * restores the host's versions. VM-exit information is read from the VMCB by
 * the caller.
 *
 * The VMM still owns EFER.SVME, VM_HSAVE_PA, GIF/intercepts, supervisor state
 * and any required VMLOAD/VMSAVE. Guest changes to FS/GS, MSRs, debug/control
 * registers or supervisor xstate must not invalidate the host's software ABI.
 * This interface does not emulate SVM, enable it, or catch processor faults.
 */
struct __svm_vmrun_context {
  unsigned long long VMCB;
  unsigned long long GuestXState;
  unsigned long long HostXState;
  unsigned long long XStateMask;
  unsigned long long RBX, RCX, RDX, RSI, RDI, RBP;
  unsigned long long R8, R9, R10, R11, R12, R13, R14, R15;
};

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__svm_vmrun_context64(struct __svm_vmrun_context *__context) {
  __builtin_ia32_vmrun_context(__context, 64);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__svm_vmrun_context32(struct __svm_vmrun_context *__context) {
  __builtin_ia32_vmrun_context(__context, 32);
}

#endif /* __SVMCONTEXTINTRIN_H */
