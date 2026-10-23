/*===---- sgxintrin.h - Native SGX supervisor intrinsics -------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if !defined __X86INTRIN_H && !defined __IMMINTRIN_H
#error "Never use <sgxintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __SGXINTRIN_H
#define __SGXINTRIN_H

#ifdef __x86_64__
#define __DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__, __target__("sgx")))

static __inline unsigned int __DEFAULT_FN_ATTRS
_encls_u32(unsigned int __leaf, __SIZE_TYPE__ __d[]) {
  unsigned long long __registers[3];
  unsigned char __flags;
  unsigned long long __result = __builtin_ia32_encls(
      __leaf, __d[0], __d[1], __d[2], __registers, &__flags);
  __d[0] = __registers[0];
  __d[1] = __registers[1];
  __d[2] = __registers[2];
  return (unsigned int)__result;
}

static __inline unsigned int __DEFAULT_FN_ATTRS
_enclv_u32(unsigned int __leaf, __SIZE_TYPE__ __d[]) {
  unsigned long long __registers[3];
  unsigned char __flags;
  unsigned long long __result = __builtin_ia32_enclv(
      __leaf, __d[0], __d[1], __d[2], __registers, &__flags);
  __d[0] = __registers[0];
  __d[1] = __registers[1];
  __d[2] = __registers[2];
  return (unsigned int)__result;
}

#undef __DEFAULT_FN_ATTRS
#endif /* __x86_64__ */

#endif /* __SGXINTRIN_H */
