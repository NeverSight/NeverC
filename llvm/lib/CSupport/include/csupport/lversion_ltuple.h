#ifndef CSUPPORT_LVERSION_LTUPLE_H
#define CSUPPORT_LVERSION_LTUPLE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_version_try_parse(const char *input, size_t len, unsigned *major,
                               unsigned *minor, unsigned *subminor,
                               unsigned *build, int *has_minor,
                               int *has_subminor, int *has_build);

int csupport_version_format(char *buf, size_t buflen, unsigned major,
                            int has_minor, unsigned minor, int has_subminor,
                            unsigned subminor, int has_build, unsigned build);

#ifdef __cplusplus
}
#endif
#endif
