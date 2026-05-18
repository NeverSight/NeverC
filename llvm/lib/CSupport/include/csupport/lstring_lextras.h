#ifndef CSUPPORT_LSTRING_LEXTRAS_H
#define CSUPPORT_LSTRING_LEXTRAS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_to_upper(char *buf, const char *src, size_t len);
void csupport_to_lower(char *buf, const char *src, size_t len);

#ifdef __cplusplus
}
#endif
#endif
