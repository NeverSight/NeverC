/*===- StringExtras.c - String utilities (pure C) ----------------*- C -*-===*/
#include "include/csupport/lstring_lextras.h"
#include "include/csupport/buffer.h"
#include <ctype.h>

void csupport_to_upper(char *buf, const char *src, size_t len) {
  for (size_t i = 0; i < len; i++)
    buf[i] = (char)toupper((unsigned char)src[i]);
}

void csupport_to_lower(char *buf, const char *src, size_t len) {
  for (size_t i = 0; i < len; i++)
    buf[i] = (char)tolower((unsigned char)src[i]);
}

/* Identifier case is an ASCII question, not a locale one: these run over
   symbol names, where a byte above 127 belongs to whatever encoding the
   source used and is not a letter this should be folding. */
static int is_lower_ascii(char c) { return 'a' <= c && c <= 'z'; }
static int is_upper_ascii(char c) { return 'A' <= c && c <= 'Z'; }
static char to_lower_ascii(char c) {
  return is_upper_ascii(c) ? (char)(c - 'A' + 'a') : c;
}
static char to_upper_ascii(char c) {
  return is_lower_ascii(c) ? (char)(c - 'a' + 'A') : c;
}

size_t csupport_convert_to_snake_case(const char *input, size_t input_len,
                                      char *buf, size_t buflen) {
  csupport_obuf_t out = csupport_obuf(buf, buflen);
  for (size_t i = 0; i < input_len; ++i) {
    csupport_obuf_put(&out, to_lower_ascii(input[i]));

    /* A boundary is where the case changes: "runtimeAPI" breaks after the
       lower-to-upper step, and "APIRuntime" before the last capital of a run,
       which is the one that starts the next word. */
    const int lower_then_upper = is_lower_ascii(input[i]) &&
                                 i + 1 < input_len &&
                                 is_upper_ascii(input[i + 1]);
    const int end_of_capital_run =
        is_upper_ascii(input[i]) && i + 2 < input_len &&
        is_upper_ascii(input[i + 1]) && is_lower_ascii(input[i + 2]);
    if (lower_then_upper || end_of_capital_run)
      csupport_obuf_put(&out, '_');
  }
  return csupport_obuf_finish(&out);
}

size_t csupport_convert_to_camel_case(const char *input, size_t input_len,
                                      int capitalize_first, char *buf,
                                      size_t buflen) {
  csupport_obuf_t out = csupport_obuf(buf, buflen);
  if (input_len == 0)
    return csupport_obuf_finish(&out);

  csupport_obuf_put(&out, capitalize_first ? to_upper_ascii(input[0])
                                           : input[0]);

  /* A '_' followed by a lowercase letter is a word break, and only then: a
     trailing '_' and a "__" both stand for themselves. */
  for (size_t i = 1; i < input_len; ++i) {
    if (input[i] == '_' && i + 1 < input_len && is_lower_ascii(input[i + 1]))
      csupport_obuf_put(&out, to_upper_ascii(input[++i]));
    else
      csupport_obuf_put(&out, input[i]);
  }
  return csupport_obuf_finish(&out);
}
