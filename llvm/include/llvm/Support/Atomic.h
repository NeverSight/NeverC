//===- llvm/Support/Atomic.h - Atomic Operations -----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys atomic operations.
//
// DO NOT USE IN NEW CODE!
//
// New code should always rely on the std::atomic facilities in C++11.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ATOMIC_H
#define LLVM_SUPPORT_ATOMIC_H

#include "llvm/Support/DataTypes.h"

// Windows will at times define MemoryFence.
#ifdef MemoryFence
#undef MemoryFence
#endif

extern "C" {
void csupport_memory_fence(void);
#ifdef _MSC_VER
typedef long csupport_cas_flag;
#else
typedef uint32_t csupport_cas_flag;
#endif
csupport_cas_flag csupport_compare_and_swap(volatile csupport_cas_flag *ptr,
                                            csupport_cas_flag new_value,
                                            csupport_cas_flag old_value);
}

namespace llvm {
namespace sys {
inline void MemoryFence() { csupport_memory_fence(); }

#ifdef _MSC_VER
typedef long cas_flag;
#else
typedef uint32_t cas_flag;
#endif
inline cas_flag CompareAndSwap(volatile cas_flag *ptr, cas_flag new_value,
                               cas_flag old_value) {
  return csupport_compare_and_swap(ptr, new_value, old_value);
}
} // namespace sys
} // namespace llvm

#endif
