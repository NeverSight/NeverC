/*===- LineIterator.c - Line iteration (pure C) -----------------*- C -*-===*/
#include "include/csupport/lline_literator.h"
#include <stddef.h>

int csupport_is_at_line_end(const char *p) {
  if (*p == '\n') return 1;
  if (*p == '\r' && *(p + 1) == '\n') return 1;
  return 0;
}

static int is_line_end(const char *p) {
  return csupport_is_at_line_end(p);
}

static int skip_line_end(const char **p) {
  if (**p == '\n') { (*p)++; return 1; }
  if (**p == '\r' && *(*p + 1) == '\n') { *p += 2; return 1; }
  return 0;
}

size_t csupport_next_line(const char **pos, const char *end,
                          char comment_marker, int skip_blanks,
                          int *line_number) {
  const char *p = *pos;
  if (skip_line_end(&p)) (*line_number)++;

  if (!skip_blanks && p < end && is_line_end(p)) {
    /* blank line */
  } else if (comment_marker == '\0') {
    while (p < end && skip_line_end(&p)) (*line_number)++;
  } else {
    for (;;) {
      if (p < end && is_line_end(p) && !skip_blanks) break;
      if (p < end && *p == comment_marker) {
        while (p < end && *p != '\0' && !is_line_end(p)) p++;
      }
      if (!skip_line_end(&p)) break;
      (*line_number)++;
    }
  }

  if (p >= end || *p == '\0') { *pos = end; return 0; }

  size_t len = 0;
  while (p + len < end && p[len] != '\0' && !is_line_end(p + len)) len++;
  *pos = p;
  return len;
}
