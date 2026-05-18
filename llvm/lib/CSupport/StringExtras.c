/*===- StringExtras.c - String utilities (pure C) ----------------*- C -*-===*/
#include "include/csupport/lstring_lextras.h"
#include <ctype.h>

void csupport_to_upper(char *buf, const char *src, size_t len) {
  for (size_t i = 0; i < len; i++)
    buf[i] = (char)toupper((unsigned char)src[i]);
}

void csupport_to_lower(char *buf, const char *src, size_t len) {
  for (size_t i = 0; i < len; i++)
    buf[i] = (char)tolower((unsigned char)src[i]);
}
