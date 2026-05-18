#ifndef CSUPPORT_LD_LJ_LB_H
#define CSUPPORT_LD_LJ_LB_H
#include "csupport/types.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t csupport_djb_hash(const char *data, size_t len,
                                         uint32_t h) {
  for (size_t i = 0; i < len; i++)
    h = (h << 5) + h + (unsigned char)data[i];
  return h;
}

uint32_t csupport_case_folding_djb_hash(csupport_string_ref_t buffer,
                                        uint32_t h);

#ifdef __cplusplus
}
#endif
#endif
