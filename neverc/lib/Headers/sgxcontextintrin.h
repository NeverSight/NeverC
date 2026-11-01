/*===---- sgxcontextintrin.h - Terminal native SGX input state -------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*/
#ifndef __SGXCONTEXTINTRIN_H
#define __SGXCONTEXTINTRIN_H

#if !defined(__x86_64__) && !defined(_M_X64)
#error "This NeverC SGX context interface requires x86-64"
#endif

/* GPR order: AX,CX,DX,BX,SP,BP,SI,DI,R8..R15. Low EAX selects EENTER (2),
 * ERESUME (3) or EEXIT (4). Other leaves trap before XRSTOR or target stack
 * installation. The entire RAX input, including its upper half, is retained.
 * XState is a valid aligned standard XSAVE image with live XCR0=7. Context,
 * image and compiler stack must remain accessible and disjoint; no concurrent
 * mutation is permitted. RFlags must be realizable by CPL3 POPFQ, with the
 * ambient IF/IOPL and clear TF, RF, NT and VM. CET must be disabled.
 * Transport is not atomic: faults/events observe the partially installed
 * native state and are not mapped to an original instruction's PC.
 * The native SGX environment owns
 * EPC, TCS, SSA, FS/GS caches, debug state, faults, AEX and enclave protocols.
 *
 * This operation abandons the compiler frame. EENTER exposes the address
 * after the actual emitted ENCLU in RCX, not an original binary's virtual PC.
 * That address deliberately traps: the enclave protocol must transfer to its
 * explicit native continuation/AEP. This is not an ordinary returning enclave
 * call, vDSO ABI, or an original-PC-preserving source projection. AEX/ERESUME
 * restoration is performed by SGX hardware, not by the compiler.
 */
struct __sgx_transfer_context {
  unsigned long long GPR[16], RFlags, XState;
};

static __inline__ void __attribute__((__always_inline__, __nodebug__,
                                      __noreturn__, __target__("xsave,sgx")))
__sgx_transfer(const struct __sgx_transfer_context *__context) {
  __builtin_ia32_enclu_transfer_context(__context);
}

/* Execute immutable ENCLU bytes at a constant original linear address after
 * restoring the same bank. All original continuation/AEP code and mappings
 * must remain executable. No GPR is consumed by the final jump; EENTER's RCX
 * next-PC is therefore the original address plus three. Unlike __sgx_transfer,
 * this path has no compiler-created ENCLU return address. The native original
 * continuation owns execution after a transfer, including AEX/ERESUME.
 */
#define __sgx_transfer_at(__context, __site)                                   \
  __builtin_ia32_enclu_transfer_at_context((__context), (__site))

#endif /* __SGXCONTEXTINTRIN_H */
