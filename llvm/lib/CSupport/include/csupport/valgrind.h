/*===-- csupport/valgrind.h - Valgrind communication ------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_VALGRIND_H
#define CSUPPORT_VALGRIND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int csupport_running_on_valgrind(void);
void csupport_valgrind_discard_translations(const void *Addr, size_t Len);

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_VALGRIND_H */
