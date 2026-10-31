/*===---- kvmhypercallintrin.h - Explicit KVM hypercall ABI ----------------===
 * SPDX-License-Identifier: AGPL-3.0-only
 *===----------------------------------------------------------------------===*/

#ifndef __KVMHYPERCALLINTRIN_H
#define __KVMHYPERCALLINTRIN_H

/* The environment binds the returning Linux KVM ABI: rAX is the call number
 * and result, rBX/rCX/rDX/rSI carry four arguments. Other registers, flags,
 * extended state and the compiler execution environment must be preserved.
 * Calls that modify additional state need a different interface. These
 * operations may read or write arbitrary guest memory. This is not Hyper-V's
 * ABI, and enabling the hypervisor or handling CPU faults is the caller's job.
 */
#if defined(__x86_64__) || defined(_M_X64)
#define __KVM_WORD unsigned long long
#define __KVM_VMCALL __builtin_ia32_vmcall_kvm
#define __KVM_VMMCALL __builtin_ia32_vmmcall_kvm
#else
#error "NeverC native KVM hypercall intrinsics require x86-64"
#endif

static __inline__ __KVM_WORD __attribute__((__always_inline__, __nodebug__))
__kvm_vmcall(__KVM_WORD __nr, __KVM_WORD __a, __KVM_WORD __b, __KVM_WORD __c,
             __KVM_WORD __d) {
  return __KVM_VMCALL(__nr, __a, __b, __c, __d);
}
static __inline__ __KVM_WORD __attribute__((__always_inline__, __nodebug__))
__kvm_vmmcall(__KVM_WORD __nr, __KVM_WORD __a, __KVM_WORD __b, __KVM_WORD __c,
              __KVM_WORD __d) {
  return __KVM_VMMCALL(__nr, __a, __b, __c, __d);
}

#undef __KVM_WORD
#undef __KVM_VMCALL
#undef __KVM_VMMCALL
#endif /* __KVMHYPERCALLINTRIN_H */
