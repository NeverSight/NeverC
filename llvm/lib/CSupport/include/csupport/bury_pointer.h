/*===-- csupport/bury_pointer.h - Intentional memory leak -------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_BURY_POINTER_H
#define CSUPPORT_BURY_POINTER_H

#ifdef __cplusplus
extern "C" {
#endif

void csupport_bury_pointer(const void *Ptr);

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_BURY_POINTER_H */
