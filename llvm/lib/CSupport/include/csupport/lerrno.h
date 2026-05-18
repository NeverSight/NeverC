#ifndef CSUPPORT_LERRNO_H
#define CSUPPORT_LERRNO_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Thread-safe strerror into caller-supplied buffer.
   Returns 0 on success, -1 if errnum == 0 (empty string written). */
int csupport_strerror(int errnum, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
#endif
