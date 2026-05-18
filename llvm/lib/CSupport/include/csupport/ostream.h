/*===-- csupport/ostream.h - Pure C output stream ---------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_OSTREAM_H
#define CSUPPORT_OSTREAM_H

#include "csupport/types.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CSUPPORT_OS_FILE,
  CSUPPORT_OS_BUFFER,
  CSUPPORT_OS_NULL
} csupport_os_kind_t;

typedef struct {
  csupport_os_kind_t kind;
  union {
    FILE *file;
    struct {
      char *data;
      size_t pos;
      size_t cap;
      bool owns;
    } buf;
  } u;
} csupport_ostream_t;

static inline csupport_ostream_t csupport_os_file(FILE *f) {
  csupport_ostream_t os;
  os.kind = CSUPPORT_OS_FILE;
  os.u.file = f;
  return os;
}

static inline csupport_ostream_t csupport_os_stderr(void) {
  return csupport_os_file(stderr);
}

static inline csupport_ostream_t csupport_os_stdout(void) {
  return csupport_os_file(stdout);
}

static inline csupport_ostream_t csupport_os_null(void) {
  csupport_ostream_t os;
  os.kind = CSUPPORT_OS_NULL;
  return os;
}

static inline csupport_ostream_t csupport_os_buffer(size_t initial_cap) {
  csupport_ostream_t os;
  os.kind = CSUPPORT_OS_BUFFER;
  os.u.buf.cap = initial_cap > 0 ? initial_cap : 256;
  os.u.buf.data = (char *)malloc(os.u.buf.cap);
  os.u.buf.pos = 0;
  os.u.buf.owns = true;
  return os;
}

static inline csupport_ostream_t csupport_os_fixed_buffer(char *buf,
                                                          size_t cap) {
  csupport_ostream_t os;
  os.kind = CSUPPORT_OS_BUFFER;
  os.u.buf.data = buf;
  os.u.buf.pos = 0;
  os.u.buf.cap = cap;
  os.u.buf.owns = false;
  return os;
}

static inline void csupport_os_destroy(csupport_ostream_t *os) {
  if (os->kind == CSUPPORT_OS_BUFFER && os->u.buf.owns)
    free(os->u.buf.data);
}

static inline void csupport_os_write(csupport_ostream_t *os, const char *data,
                                     size_t len) {
  if (len == 0)
    return;
  switch (os->kind) {
  case CSUPPORT_OS_FILE:
    fwrite(data, 1, len, os->u.file);
    break;
  case CSUPPORT_OS_BUFFER: {
    size_t need = os->u.buf.pos + len;
    if (need > os->u.buf.cap) {
      if (!os->u.buf.owns)
        return;
      size_t grow = os->u.buf.cap * 2;
      if (grow < need)
        grow = need;
      os->u.buf.data = (char *)realloc(os->u.buf.data, grow);
      os->u.buf.cap = grow;
    }
    memcpy(os->u.buf.data + os->u.buf.pos, data, len);
    os->u.buf.pos += len;
    break;
  }
  case CSUPPORT_OS_NULL:
    break;
  }
}

static inline void csupport_os_write_str(csupport_ostream_t *os,
                                         const char *s) {
  csupport_os_write(os, s, strlen(s));
}

static inline void csupport_os_write_char(csupport_ostream_t *os, char c) {
  csupport_os_write(os, &c, 1);
}

static inline void csupport_os_flush(csupport_ostream_t *os) {
  if (os->kind == CSUPPORT_OS_FILE)
    fflush(os->u.file);
}

static inline void csupport_os_printf(csupport_ostream_t *os, const char *fmt,
                                      ...) {
  char stack_buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
  va_end(ap);
  if (n > 0 && (size_t)n < sizeof(stack_buf)) {
    csupport_os_write(os, stack_buf, (size_t)n);
  } else if (n > 0) {
    char *heap_buf = (char *)malloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(heap_buf, (size_t)n + 1, fmt, ap);
    va_end(ap);
    csupport_os_write(os, heap_buf, (size_t)n);
    free(heap_buf);
  }
}

static inline csupport_string_ref_t
csupport_os_str(const csupport_ostream_t *os) {
  csupport_string_ref_t ref;
  if (os->kind == CSUPPORT_OS_BUFFER) {
    ref.data = os->u.buf.data;
    ref.length = os->u.buf.pos;
  } else {
    ref.data = NULL;
    ref.length = 0;
  }
  return ref;
}

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_OSTREAM_H */
