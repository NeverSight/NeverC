/*===-- Valgrind.c - Valgrind communication ---------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "include/csupport/valgrind.h"
#include "llvm/Config/config.h"

#if HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>

int csupport_running_on_valgrind(void) { return RUNNING_ON_VALGRIND; }

void csupport_valgrind_discard_translations(const void *Addr, size_t Len) {
  VALGRIND_DISCARD_TRANSLATIONS(Addr, Len);
}

#else

int csupport_running_on_valgrind(void) { return 0; }

void csupport_valgrind_discard_translations(const void *Addr, size_t Len) {
  (void)Addr;
  (void)Len;
}

#endif

