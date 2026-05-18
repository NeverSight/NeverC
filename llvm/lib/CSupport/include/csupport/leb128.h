/*===-- csupport/leb128.h - LEB128 encoding utilities -----------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_LEB128_H
#define CSUPPORT_LEB128_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned csupport_getULEB128Size(uint64_t Value);
unsigned csupport_getSLEB128Size(int64_t Value);

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_LEB128_H */
