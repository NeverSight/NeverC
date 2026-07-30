/*===- FormattedStream.c - Column-tracking stream (pure C) ------*- C -*-===*/
#include "include/csupport/lformatted_lstream.h"
#include <limits.h>
#include <string.h>

unsigned csupport_utf8_byte_length(unsigned char first_byte) {
  if (first_byte < 0x80) return 1;
  if ((first_byte & 0xE0) == 0xC0) return 2;
  if ((first_byte & 0xF0) == 0xE0) return 3;
  if ((first_byte & 0xF8) == 0xF0) return 4;
  return 1;
}

static int is_printable_ascii(unsigned char c) {
  return c >= 0x20 && c < 0x7F;
}

static void process_codepoint(unsigned *column, unsigned *line,
                              const char *cp, unsigned cp_len) {
  if (cp_len == 1) {
    unsigned char c = (unsigned char)cp[0];
    switch (c) {
    case '\n':
      if (*line != UINT_MAX)
        (*line)++;
      *column = 0;
      return;
    case '\r':
      *column = 0;
      return;
    case '\t':
      if (*column > UINT_MAX - (8 - (*column & 0x7)))
        *column = UINT_MAX;
      else
        *column += 8 - (*column & 0x7);
      return;
    default:
      if (is_printable_ascii(c))
        if (*column != UINT_MAX)
          (*column)++;
      return;
    }
  }
  if (*column != UINT_MAX)
    (*column)++;
}

void csupport_update_column_position(unsigned *column, unsigned *line,
                                     const char *ptr, size_t size,
                                     char *partial_utf8, size_t *partial_len) {
  if (!column || !line || !partial_utf8 || !partial_len || (!ptr && size != 0))
    return;
  if (*partial_len > 4) {
    *partial_len = 0;
    return;
  }
  if (*partial_len > 0) {
    unsigned needed = csupport_utf8_byte_length((unsigned char)partial_utf8[0]);
    if (*partial_len >= needed) {
      process_codepoint(column, line, partial_utf8, needed);
      *partial_len = 0;
    } else {
      unsigned remaining = needed - (unsigned)*partial_len;
      if (size < remaining) {
        if (size != 0)
          memcpy(partial_utf8 + *partial_len, ptr, size);
        *partial_len += size;
        return;
      }
      memcpy(partial_utf8 + *partial_len, ptr, remaining);
      process_codepoint(column, line, partial_utf8, needed);
      *partial_len = 0;
      ptr += remaining;
      size -= remaining;
    }
  }

  if (size == 0)
    return;
  const char *end = ptr + size;
  while (ptr < end) {
    unsigned num_bytes = csupport_utf8_byte_length((unsigned char)*ptr);
    if ((unsigned)(end - ptr) < num_bytes) {
      *partial_len = (size_t)(end - ptr);
      memcpy(partial_utf8, ptr, *partial_len);
      return;
    }
    process_codepoint(column, line, ptr, num_bytes);
    ptr += num_bytes;
  }
}
