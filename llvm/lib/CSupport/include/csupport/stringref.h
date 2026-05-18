/*===-- csupport/stringref.h - Pure C StringRef operations --------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_STRINGREF_H
#define CSUPPORT_STRINGREF_H

#include "csupport/types.h"
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CSUPPORT_STR_NPOS ((size_t)-1)

static inline csupport_string_ref_t csupport_str_from_cstr(const char *s) {
  csupport_string_ref_t ref;
  ref.data = s;
  ref.length = s ? strlen(s) : 0;
  return ref;
}

static inline const char *csupport_str_data(csupport_string_ref_t s) {
  return s.data;
}

static inline size_t csupport_str_size(csupport_string_ref_t s) {
  return s.length;
}

static inline bool csupport_str_empty(csupport_string_ref_t s) {
  return s.length == 0;
}

static inline const char *csupport_str_begin(csupport_string_ref_t s) {
  return s.data;
}

static inline const char *csupport_str_end(csupport_string_ref_t s) {
  return s.data + s.length;
}

static inline char csupport_str_at(csupport_string_ref_t s, size_t idx) {
  return s.data[idx];
}

static inline char csupport_str_front(csupport_string_ref_t s) {
  return s.data[0];
}

static inline char csupport_str_back(csupport_string_ref_t s) {
  return s.data[s.length - 1];
}

static inline csupport_string_ref_t
csupport_str_substr(csupport_string_ref_t s, size_t start, size_t count) {
  if (start >= s.length)
    return csupport_string_ref(NULL, 0);
  if (count == CSUPPORT_STR_NPOS || start + count > s.length)
    count = s.length - start;
  return csupport_string_ref(s.data + start, count);
}

static inline csupport_string_ref_t
csupport_str_slice(csupport_string_ref_t s, size_t start, size_t end) {
  if (start >= s.length || start >= end)
    return csupport_string_ref(NULL, 0);
  if (end > s.length)
    end = s.length;
  return csupport_string_ref(s.data + start, end - start);
}

static inline csupport_string_ref_t
csupport_str_drop_front(csupport_string_ref_t s, size_t n) {
  if (n >= s.length)
    return csupport_string_ref(NULL, 0);
  return csupport_string_ref(s.data + n, s.length - n);
}

static inline csupport_string_ref_t
csupport_str_drop_back(csupport_string_ref_t s, size_t n) {
  if (n >= s.length)
    return csupport_string_ref(NULL, 0);
  return csupport_string_ref(s.data, s.length - n);
}

static inline csupport_string_ref_t
csupport_str_take_front(csupport_string_ref_t s, size_t n) {
  if (n >= s.length)
    return s;
  return csupport_string_ref(s.data, n);
}

static inline csupport_string_ref_t
csupport_str_take_back(csupport_string_ref_t s, size_t n) {
  if (n >= s.length)
    return s;
  return csupport_string_ref(s.data + s.length - n, n);
}

static inline size_t csupport_str_find_char(csupport_string_ref_t s, char c,
                                            size_t from) {
  for (size_t i = from; i < s.length; i++) {
    if (s.data[i] == c)
      return i;
  }
  return CSUPPORT_STR_NPOS;
}

static inline size_t csupport_str_find(csupport_string_ref_t s,
                                       csupport_string_ref_t needle,
                                       size_t from) {
  if (needle.length == 0)
    return from <= s.length ? from : CSUPPORT_STR_NPOS;
  if (needle.length > s.length)
    return CSUPPORT_STR_NPOS;
  for (size_t i = from; i + needle.length <= s.length; i++) {
    if (memcmp(s.data + i, needle.data, needle.length) == 0)
      return i;
  }
  return CSUPPORT_STR_NPOS;
}

static inline size_t csupport_str_rfind_char(csupport_string_ref_t s, char c,
                                             size_t from) {
  if (s.length == 0)
    return CSUPPORT_STR_NPOS;
  size_t start =
      (from == CSUPPORT_STR_NPOS || from >= s.length) ? s.length : from;
  for (size_t i = start; i > 0; i--) {
    if (s.data[i - 1] == c)
      return i - 1;
  }
  return CSUPPORT_STR_NPOS;
}

static inline bool csupport_str_contains_char(csupport_string_ref_t s, char c) {
  return csupport_str_find_char(s, c, 0) != CSUPPORT_STR_NPOS;
}

static inline bool csupport_str_contains(csupport_string_ref_t s,
                                         csupport_string_ref_t needle) {
  return csupport_str_find(s, needle, 0) != CSUPPORT_STR_NPOS;
}

static inline bool csupport_str_starts_with(csupport_string_ref_t s,
                                            csupport_string_ref_t prefix) {
  if (prefix.length > s.length)
    return false;
  return memcmp(s.data, prefix.data, prefix.length) == 0;
}

static inline bool csupport_str_ends_with(csupport_string_ref_t s,
                                          csupport_string_ref_t suffix) {
  if (suffix.length > s.length)
    return false;
  return memcmp(s.data + s.length - suffix.length, suffix.data,
                suffix.length) == 0;
}

static inline int csupport_str_compare(csupport_string_ref_t a,
                                       csupport_string_ref_t b) {
  size_t min_len = a.length < b.length ? a.length : b.length;
  int cmp = min_len > 0 ? memcmp(a.data, b.data, min_len) : 0;
  if (cmp != 0)
    return cmp;
  if (a.length < b.length)
    return -1;
  if (a.length > b.length)
    return 1;
  return 0;
}

static inline bool csupport_str_equals(csupport_string_ref_t a,
                                       csupport_string_ref_t b) {
  if (a.length != b.length)
    return false;
  if (a.length == 0)
    return true;
  return memcmp(a.data, b.data, a.length) == 0;
}

static inline csupport_string_ref_t
csupport_str_ltrim(csupport_string_ref_t s) {
  size_t i = 0;
  while (i < s.length && isspace((unsigned char)s.data[i]))
    i++;
  return csupport_string_ref(s.data + i, s.length - i);
}

static inline csupport_string_ref_t
csupport_str_rtrim(csupport_string_ref_t s) {
  size_t len = s.length;
  while (len > 0 && isspace((unsigned char)s.data[len - 1]))
    len--;
  return csupport_string_ref(s.data, len);
}

static inline csupport_string_ref_t csupport_str_trim(csupport_string_ref_t s) {
  return csupport_str_rtrim(csupport_str_ltrim(s));
}

static inline bool csupport_str_consume_front(csupport_string_ref_t *s,
                                              csupport_string_ref_t prefix) {
  if (!csupport_str_starts_with(*s, prefix))
    return false;
  s->data += prefix.length;
  s->length -= prefix.length;
  return true;
}

static inline bool csupport_str_consume_back(csupport_string_ref_t *s,
                                             csupport_string_ref_t suffix) {
  if (!csupport_str_ends_with(*s, suffix))
    return false;
  s->length -= suffix.length;
  return true;
}

/* Split s on the first occurrence of separator.
   Returns the part before the separator.
   *rest is set to the part after the separator, or empty if not found. */
static inline csupport_string_ref_t
csupport_str_split(csupport_string_ref_t s, char sep,
                   csupport_string_ref_t *rest) {
  size_t pos = csupport_str_find_char(s, sep, 0);
  if (pos == CSUPPORT_STR_NPOS) {
    *rest = csupport_string_ref(s.data + s.length, 0);
    return s;
  }
  *rest = csupport_string_ref(s.data + pos + 1, s.length - pos - 1);
  return csupport_string_ref(s.data, pos);
}

/* Parse a decimal integer from s. Returns true on success.
   On success, *out_val is set to the parsed value. */
static inline bool csupport_str_to_int(csupport_string_ref_t s,
                                       long long *out_val) {
  if (s.length == 0)
    return false;
  char buf[32];
  size_t len = s.length < sizeof(buf) - 1 ? s.length : sizeof(buf) - 1;
  memcpy(buf, s.data, len);
  buf[len] = '\0';
  char *endptr;
  long long val = strtoll(buf, &endptr, 10);
  if (endptr != buf + len)
    return false;
  *out_val = val;
  return true;
}

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_STRINGREF_H */
