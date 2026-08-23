#ifndef CSUPPORT_LSMALL_LPTR_LSET_H
#define CSUPPORT_LSMALL_LPTR_LSET_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

static inline unsigned csupport_sps_hash_pointer(const void *ptr) {
  uintptr_t value = (uintptr_t)ptr;
  return (unsigned)(value >> 4) ^ (unsigned)(value >> 9);
}

void csupport_sps_erase_from_bucket(const void **cur_array,
                                    unsigned cur_array_size,
                                    const void **bucket);
void csupport_sps_shrink_and_clear(const void ***cur_array,
                                   unsigned *cur_array_size,
                                   unsigned *num_non_empty,
                                   unsigned *num_tombstones, unsigned size);
void csupport_sps_grow(const void ***cur_array, unsigned *cur_array_size,
                       unsigned *num_non_empty, unsigned *num_tombstones,
                       const void **small_array, unsigned new_size);
int csupport_sps_insert_big(const void ***cur_array, unsigned *cur_array_size,
                            unsigned *num_non_empty,
                            unsigned *num_tombstones,
                            const void **small_array, const void *ptr,
                            const void ***out_bucket);

#ifdef __cplusplus
}
#endif
#endif
