/*===---- hlecontextintrin.h - Native HLE operation contexts ----------------===
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *===----------------------------------------------------------------------===*/
#ifndef __HLECONTEXTINTRIN_H
#define __HLECONTEXTINTRIN_H
#if !defined(__x86_64__)
#error "HLE context requires x86-64"
#endif

struct __hle_context {
  unsigned long long RAX, RCX, RDX, RBX, RFLAGS;
};
enum {
  __HLE_ADD,
  __HLE_ADC,
  __HLE_AND,
  __HLE_BTC,
  __HLE_BTR,
  __HLE_BTS,
  __HLE_CMPXCHG,
  __HLE_CMPXCHG8B,
  __HLE_DEC,
  __HLE_INC,
  __HLE_NEG,
  __HLE_NOT,
  __HLE_OR,
  __HLE_SBB,
  __HLE_SUB,
  __HLE_XOR,
  __HLE_XADD,
  __HLE_XCHG,
  __HLE_MOV,
  __HLE_RELEASE = 0,
  __HLE_ACQUIRE = 0x100
};
/* Controls and width must be constant. Native callers own the entire HLE
 * region. Contexts, memory and compiler frame must be disjoint; long-mode
 * LAHF/SAHF and the selected underlying instruction must be available. */
#define __hle_native(memory, state, operation, bytes, hint)                    \
  __extension__({                                                              \
    struct __hle_context *__hle_state = (state);                               \
    __builtin_ia32_hle_context((memory), &__hle_state->RAX,                    \
                               &__hle_state->RFLAGS, (operation) | (hint),     \
                               (bytes));                                       \
  })
/* Enter outside RTM/external HLE. A native PAUSE aborts elision before any
 * output is published, so this standalone step returns after a real native
 * nontransactional completion. It does not fabricate transaction success. */
#define __hle_step(memory, state, operation, bytes, hint)                      \
  __hle_native((memory), (state), (operation) | 0x200, (bytes), (hint))

#endif
