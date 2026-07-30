/*===-- Atomic.c - Legacy atomic operations ---------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "csupport/atomic.h"
#include "llvm/Config/llvm-config.h"

#if defined(_MSC_VER)
#include <intrin.h>
#include <windows.h>
#undef MemoryFence
#endif

#if defined(__GNUC__) || (defined(__IBMCPP__) && __IBMCPP__ >= 1210)
#define CSUPPORT_HAS_SYNC_BUILTINS 1
#endif

void csupport_memory_fence(void) {
#if LLVM_HAS_ATOMICS == 0
  return;
#elif defined(CSUPPORT_HAS_SYNC_BUILTINS)
  __sync_synchronize();
#elif defined(_MSC_VER)
  MemoryBarrier();
#else
#error No memory fence implementation for this platform
#endif
}

csupport_cas_flag csupport_compare_and_swap(volatile csupport_cas_flag *ptr,
                                            csupport_cas_flag new_value,
                                            csupport_cas_flag old_value) {
#if LLVM_HAS_ATOMICS == 0
  csupport_cas_flag result = *ptr;
  if (result == old_value)
    *ptr = new_value;
  return result;
#elif defined(CSUPPORT_HAS_SYNC_BUILTINS)
  return __sync_val_compare_and_swap(ptr, old_value, new_value);
#elif defined(_MSC_VER)
  return InterlockedCompareExchange(ptr, new_value, old_value);
#else
#error No compare-and-swap implementation for this platform
#endif
}
