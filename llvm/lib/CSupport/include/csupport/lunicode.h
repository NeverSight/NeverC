#ifndef CSUPPORT_LUNICODE_H
#define CSUPPORT_LUNICODE_H
#include "csupport/types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CSUPPORT_UNICODE_ERROR_NON_PRINTABLE (-1)
#define CSUPPORT_UNICODE_ERROR_INVALID_UTF8 (-2)

bool csupport_unicode_is_printable(int ucs);
bool csupport_unicode_is_formatting(int ucs);
int csupport_unicode_char_width(int ucs);
int csupport_unicode_column_width_utf8(csupport_string_ref_t text);
int csupport_unicode_column_width_utf8_raw(const char *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif
