/*===---- smmcontextintrin.h - Explicit native RSM context -----------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*/
#ifndef __SMMCONTEXTINTRIN_H
#define __SMMCONTEXTINTRIN_H

#if !defined(__x86_64__) && !defined(_M_X64)
#error "RSM context requires x86-64"
#endif

/* Active SMM, code64, XSAVE enabled. XState is a 64-byte-aligned standard
 * XRSTOR image sized for XStateMask and the live XCR0. All selected components
 * and reserved header bytes must be valid. The five slots are ordinary linear
 * input memory, disjoint from the compiler frame, XSAVE image and SMRAM save
 * image. CR8 is in [0,15]; KernelGSBase is canonical for this CPU.
 * The compound operation writes CR2, CR8 and IA32_KERNEL_GS_BASE, then executes
 * XRSTOR64 immediately followed by RSM. No compiler instruction runs after
 * XRSTOR. The ambient hardware save image owns GPRs, PC, flags, segment caches,
 * CR0/3/4, EFER and the remaining RSM-managed banks. Other live MSRs, debug
 * registers and devices remain environment-owned. There is no C ABI return,
 * SMRAM layout emulation, catchable-fault or asynchronous unwind guarantee.
 */
struct __smm_rsm_context {
  unsigned long long XState, XStateMask, CR2, CR8, KernelGSBase;
};

static __inline__ void __attribute__((__always_inline__, __nodebug__,
                                      __noreturn__, __target__("xsave")))
__smm_rsm_context64(const struct __smm_rsm_context *__context) {
  __builtin_ia32_rsm_context(__context);
}

#endif /* __SMMCONTEXTINTRIN_H */
