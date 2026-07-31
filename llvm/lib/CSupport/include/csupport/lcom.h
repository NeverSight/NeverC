/*===-- csupport/lcom.h - Host COM initialization C ABI ----------*- C -*-===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions. See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef CSUPPORT_LCOM_H
#define CSUPPORT_LCOM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum csupport_com_threading_mode {
  CSUPPORT_COM_SINGLE_THREADED = 0,
  CSUPPORT_COM_MULTI_THREADED = 1
} csupport_com_threading_mode_t;

/* Returns non-zero only when the caller must later uninitialize COM. */
int csupport_com_initialize(csupport_com_threading_mode_t threading_mode,
                            int speed_over_memory);
void csupport_com_uninitialize(void);

#ifdef __cplusplus
}
#endif

#endif
