/*===- Errno.c - errno support ----------------------------------*- C -*-===*/
#include "include/csupport/lerrno.h"
#include "llvm/Config/config.h"
#include <string.h>

#if HAVE_ERRNO_H
#include <errno.h>
#endif

int csupport_strerror(int errnum, char *buf, size_t buflen) {
  if (buflen == 0)
    return -1;
  buf[0] = '\0';
  if (errnum == 0)
    return 0;

#ifdef HAVE_STRERROR_R
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
  /* glibc may not use the buffer we provide. */
  char *result = strerror_r(errnum, buf, buflen - 1);
  if (result != buf) {
    strncpy(buf, result, buflen - 1);
    buf[buflen - 1] = '\0';
  }
#else
  strerror_r(errnum, buf, buflen - 1);
  buf[buflen - 1] = '\0';
#endif
#elif HAVE_DECL_STRERROR_S
  strerror_s(buf, buflen - 1, errnum);
#else
  const char *msg = strerror(errnum);
  if (msg) {
    strncpy(buf, msg, buflen - 1);
    buf[buflen - 1] = '\0';
  }
#endif
  return 0;
}
