#ifndef CSUPPORT_LSTRING_LEXTRAS_H
#define CSUPPORT_LSTRING_LEXTRAS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_to_upper(char *buf, const char *src, size_t len);
void csupport_to_lower(char *buf, const char *src, size_t len);

/* Identifier case conversions, "runtimeAPI" <-> "runtime_api".  Snake case
   adds a separator at every case boundary, so it can be half again as long as
   what it was given, and an identifier has no length a fixed buffer could be
   sized against.
   Buffer fillers: see the contract on csupport_obuf_t in csupport/buffer.h. */
size_t csupport_convert_to_snake_case(const char *input, size_t input_len,
                                      char *buf, size_t buflen);
size_t csupport_convert_to_camel_case(const char *input, size_t input_len,
                                      int capitalize_first, char *buf,
                                      size_t buflen);

#ifdef __cplusplus
}
#endif
#endif
