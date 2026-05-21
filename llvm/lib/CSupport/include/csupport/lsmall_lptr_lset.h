#ifndef CSUPPORT_LSMALL_LPTR_LSET_H
#define CSUPPORT_LSMALL_LPTR_LSET_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_sps_erase_from_bucket(const void **cur_array,
                                    unsigned cur_array_size,
                                    const void **bucket);

#ifdef __cplusplus
}
#endif
#endif
