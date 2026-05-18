/*===- VersionTuple.c - Version parsing (pure C) ----------------*- C -*-===*/
#include "include/csupport/lversion_ltuple.h"
#include <stdio.h>
#include <string.h>

static int parse_uint(const char **input, size_t *remaining, unsigned *value) {
  *value = 0;
  if (*remaining == 0) return 1;
  char c = **input;
  (*input)++; (*remaining)--;
  if (c < '0' || c > '9') return 1;
  *value = (unsigned)(c - '0');
  while (*remaining > 0) {
    c = **input;
    if (c < '0' || c > '9') return 0;
    (*input)++; (*remaining)--;
    *value = *value * 10 + (unsigned)(c - '0');
  }
  return 0;
}

int csupport_version_try_parse(const char *input, size_t len,
                               unsigned *major, unsigned *minor,
                               unsigned *subminor, unsigned *build,
                               int *has_minor, int *has_subminor,
                               int *has_build) {
  *has_minor = *has_subminor = *has_build = 0;
  if (parse_uint(&input, &len, major)) return 1;
  if (len == 0) return 0;

  if (*input != '.') return 1;
  input++; len--;
  if (parse_uint(&input, &len, minor)) return 1;
  *has_minor = 1;
  if (len == 0) return 0;

  if (*input != '.') return 1;
  input++; len--;
  if (parse_uint(&input, &len, subminor)) return 1;
  *has_subminor = 1;
  if (len == 0) return 0;

  if (*input != '.') return 1;
  input++; len--;
  if (parse_uint(&input, &len, build)) return 1;
  *has_build = 1;
  return len != 0;
}

int csupport_version_format(char *buf, size_t buflen,
                            unsigned major, int has_minor, unsigned minor,
                            int has_subminor, unsigned subminor,
                            int has_build, unsigned build) {
  int n;
  if (has_build)
    n = snprintf(buf, buflen, "%u.%u.%u.%u", major, minor, subminor, build);
  else if (has_subminor)
    n = snprintf(buf, buflen, "%u.%u.%u", major, minor, subminor);
  else if (has_minor)
    n = snprintf(buf, buflen, "%u.%u", major, minor);
  else
    n = snprintf(buf, buflen, "%u", major);
  return n;
}
