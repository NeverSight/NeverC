#ifndef CSUPPORT_LFORMATTED_LSTREAM_H
#define CSUPPORT_LFORMATTED_LSTREAM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_update_column_position(unsigned *column, unsigned *line,
                                     const char *ptr, size_t size,
                                     char *partial_utf8, size_t *partial_len);

unsigned csupport_utf8_byte_length(unsigned char first_byte);

#ifdef __cplusplus
}
#endif
#endif
