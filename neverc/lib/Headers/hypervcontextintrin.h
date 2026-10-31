/*===---- hypervcontextintrin.h - Explicit Hyper-V page ABI ---------------===
 * SPDX-License-Identifier: AGPL-3.0-only
 *===----------------------------------------------------------------------===*/

#ifndef __HYPERVCONTEXTINTRIN_H
#define __HYPERVCONTEXTINTRIN_H

#if !defined(__x86_64__) && !defined(_M_X64)
#error "Hyper-V context requires x86-64"
#endif

/* The caller supplies a registered, executable TLFS hypercall page and the
 * required CPL0/feature environment. RCX/RDX/R8 and XMM0..5 are inputs.
 * RAX/RCX/RDX/R8 and XMM0..5 are published on return, including rep and fast
 * outputs. All other cells remain unchanged. The page must preserve every
 * other register and the compiler execution environment; services replacing
 * caller state, including SetVpRegisters on self, require a different API.
 * The buffers must remain accessible and disjoint from service writes and
 * from each other. Calls may read/write arbitrary guest memory. No faults
 * are caught. This invokes the real page; it does not copy its opcode.
 */
struct __hyperv_context {
  unsigned long long RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI;
  unsigned long long R8, R9, R10, R11, R12, R13, R14, R15;
  unsigned char XMM[6][16];
};

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__hyperv_call(void *__entry, struct __hyperv_context *__context) {
  __builtin_ia32_hyperv_call(__entry, &__context->RAX, __context->XMM, 16);
}

#endif /* __HYPERVCONTEXTINTRIN_H */
