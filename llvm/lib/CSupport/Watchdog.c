/*===- Watchdog.c - Watchdog timer (pure C) ---------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
#include "include/csupport/lwatchdog.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Config/config.h"

#ifdef LLVM_ON_UNIX
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

void csupport_watchdog_start(unsigned int seconds) {
#ifdef HAVE_UNISTD_H
  alarm(seconds);
#else
  (void)seconds;
#endif
}

void csupport_watchdog_stop(void) {
#ifdef HAVE_UNISTD_H
  alarm(0);
#endif
}

#elif defined(_WIN32)

void csupport_watchdog_start(unsigned int seconds) { (void)seconds; }
void csupport_watchdog_stop(void) {}

#else

void csupport_watchdog_start(unsigned int seconds) { (void)seconds; }
void csupport_watchdog_stop(void) {}

#endif
