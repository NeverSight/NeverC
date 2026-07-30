/*===-- csupport/number_parse.h - Length-delimited C numbers -----*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.      *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_NUMBER_PARSE_H
#define CSUPPORT_NUMBER_PARSE_H

#include "csupport/allocation.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * strto* consumes a terminated C string while LLVM passes numbers around as
 * length-delimited StringRefs.  This adapter preserves the complete input:
 * short values use caller-provided storage and long values move to the heap.
 * Silently shortening the text is never valid because a shortened prefix may
 * itself be a different, valid number.
 */
static inline char *csupport_copy_number_text(const char *text, size_t length,
                                              char *local,
                                              size_t local_capacity) {
  if (!text || !local || local_capacity == 0 || length == SIZE_MAX)
    return NULL;
  char *copy = length < local_capacity
                   ? local
                   : (char *)csupport_checked_malloc(length + 1, sizeof(char));
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

static inline void csupport_free_number_text(char *copy, char *local) {
  if (copy != local)
    free(copy);
}

#endif /* CSUPPORT_NUMBER_PARSE_H */
