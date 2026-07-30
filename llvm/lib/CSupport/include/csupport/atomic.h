/*===-- csupport/atomic.h - Legacy atomic operations ------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_ATOMIC_H
#define CSUPPORT_ATOMIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
typedef long csupport_cas_flag;
#else
typedef uint32_t csupport_cas_flag;
#endif

void csupport_memory_fence(void);
csupport_cas_flag csupport_compare_and_swap(volatile csupport_cas_flag *ptr,
                                            csupport_cas_flag new_value,
                                            csupport_cas_flag old_value);

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_ATOMIC_H */
