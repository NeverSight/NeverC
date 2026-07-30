/*===-- csupport/allocation.h - Checked C allocation ------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.      *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_ALLOCATION_H
#define CSUPPORT_ALLOCATION_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * LLVM's value operations are total apart from process-wide allocation
 * failure: their public APIs have no recoverable OOM result.  CSupport code
 * implementing those operations must therefore fail deterministically rather
 * than dereference null or return a plausible but incorrect value.
 */
static inline void csupport_allocation_failure(void) {
  fputs("CSupport allocation failed\n", stderr);
  abort();
}

static inline size_t csupport_checked_allocation_size(size_t count,
                                                      size_t element_size) {
  if (element_size != 0 && count > SIZE_MAX / element_size)
    csupport_allocation_failure();
  return count * element_size;
}

static inline void *csupport_checked_malloc(size_t count,
                                            size_t element_size) {
  size_t size = csupport_checked_allocation_size(count, element_size);
  void *allocation = malloc(size);
  if (!allocation && size != 0)
    csupport_allocation_failure();
  return allocation;
}

static inline void *csupport_checked_calloc(size_t count,
                                            size_t element_size) {
  (void)csupport_checked_allocation_size(count, element_size);
  void *allocation = calloc(count, element_size);
  if (!allocation && count != 0 && element_size != 0)
    csupport_allocation_failure();
  return allocation;
}

static inline void *csupport_checked_realloc(void *allocation, size_t count,
                                             size_t element_size) {
  size_t size = csupport_checked_allocation_size(count, element_size);
  void *resized = realloc(allocation, size);
  if (!resized && size != 0)
    csupport_allocation_failure();
  return resized;
}

#endif /* CSUPPORT_ALLOCATION_H */
