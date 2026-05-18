/*===-- BuryPointer.c - Intentional memory leak -----------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "include/csupport/bury_pointer.h"
#include <stddef.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define CSUPPORT_HAS_C11_ATOMICS 1
#else
#define CSUPPORT_HAS_C11_ATOMICS 0
#endif

#ifndef LLVM_ATTRIBUTE_USED
#if __has_attribute(used)
#define LLVM_ATTRIBUTE_USED __attribute__((__used__))
#else
#define LLVM_ATTRIBUTE_USED
#endif
#endif

void csupport_bury_pointer(const void *Ptr) {
  static const size_t kGraveYardMaxSize = 16;
  LLVM_ATTRIBUTE_USED static const void *GraveYard[16];

#if CSUPPORT_HAS_C11_ATOMICS
  static _Atomic(unsigned) GraveYardSize;
  unsigned Idx = atomic_fetch_add(&GraveYardSize, 1);
#else
  static unsigned GraveYardSize;
  unsigned Idx = __sync_fetch_and_add(&GraveYardSize, 1);
#endif

  if (Idx >= kGraveYardMaxSize)
    return;
  GraveYard[Idx] = Ptr;
}

