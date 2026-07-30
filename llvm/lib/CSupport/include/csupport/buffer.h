/*===-- csupport/buffer.h - Filling a caller's buffer -------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.      *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_BUFFER_H
#define CSUPPORT_BUFFER_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A caller's buffer, plus the length its output turned out to need.
 *
 * Every csupport routine that writes into a buffer the caller owns follows
 * snprintf's convention through this type: write what fits, terminate what
 * was written, and report the length the *whole* output needs.  Reporting the
 * length that fit instead is what makes truncation invisible -- a cut answer
 * is a perfectly ordinary string, indistinguishable from a complete one -- so
 * the reported length is the only place the difference can survive, and the
 * only thing a caller can act on.  llvm::fillCSupportBuffer acts on it by
 * retrying with a buffer that holds the whole answer.
 *
 * A null buffer, or a zero capacity, measures without writing.  That is what
 * lets a routine whose output has already outgrown the buffer hand the rest
 * of the work to another one and still come back with a true length. */
typedef struct {
  char *data;
  size_t cap;
  /* Counts every byte the output has, including the ones that did not fit. */
  size_t needed;
} csupport_obuf_t;

static inline csupport_obuf_t csupport_obuf(char *data, size_t cap) {
  csupport_obuf_t buf;
  buf.data = data;
  buf.cap = data ? cap : 0;
  buf.needed = 0;
  return buf;
}

/* Append one byte.  The last byte of capacity is held back for the
   terminator, so content never reaches index cap - 1. */
static inline void csupport_obuf_put(csupport_obuf_t *buf, char c) {
  if (buf->needed == SIZE_MAX)
    return;
  if (buf->cap != 0 && buf->needed < buf->cap - 1)
    buf->data[buf->needed] = c;
  buf->needed++;
}

static inline void csupport_obuf_write(csupport_obuf_t *buf, const char *str,
                                       size_t len) {
  for (size_t i = 0; i < len; i++)
    csupport_obuf_put(buf, str[i]);
}

/* The room left over, as a buffer to hand to a nested routine.  It is empty
   once the output has outgrown the caller's buffer, which is what keeps the
   nested routine counting rather than writing past the end.  Add what it
   reports back with csupport_obuf_grew. */
static inline csupport_obuf_t csupport_obuf_rest(const csupport_obuf_t *buf) {
  if (buf->cap != 0 && buf->needed < buf->cap - 1)
    return csupport_obuf(buf->data + buf->needed, buf->cap - buf->needed);
  return csupport_obuf(NULL, 0);
}

static inline void csupport_obuf_grew(csupport_obuf_t *buf, size_t needed) {
  if (needed > SIZE_MAX - buf->needed)
    buf->needed = SIZE_MAX;
  else
    buf->needed += needed;
}

/* Append a formatted run.  vsnprintf already answers in the terms this type
   carries -- it writes what fits and reports what the whole run needs -- so
   the two compose without either one having to guess. */
static inline void csupport_obuf_printf(csupport_obuf_t *buf, const char *fmt,
                                        ...) {
  csupport_obuf_t rest = csupport_obuf_rest(buf);
  va_list args;
  int needed;
  va_start(args, fmt);
  needed = vsnprintf(rest.data, rest.cap, fmt, args);
  va_end(args);
  /* A negative result is either an encoding error or an output too large for
     vsnprintf's int result.  Neither has a complete string to publish. */
  if (needed > 0)
    csupport_obuf_grew(buf, (size_t)needed);
  else if (needed < 0)
    buf->needed = SIZE_MAX;
}

/* Terminate what fits and report the length the whole output needs. */
static inline size_t csupport_obuf_finish(csupport_obuf_t *buf) {
  if (buf->cap)
    buf->data[buf->needed < buf->cap ? buf->needed : buf->cap - 1] = '\0';
  return buf->needed;
}

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_BUFFER_H */
