#ifndef CSUPPORT_LLINE_LITERATOR_H
#define CSUPPORT_LLINE_LITERATOR_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

size_t csupport_next_line(const char **pos, const char *end,
                          char comment_marker, int skip_blanks,
                          int *line_number);

int csupport_is_at_line_end(const char *p);

#ifdef __cplusplus
}
#endif
#endif
