#ifndef CSUPPORT_LSMALL_LPTR_LSET_H
#define CSUPPORT_LSMALL_LPTR_LSET_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

static inline unsigned csupport_sps_hash_pointer(const void *ptr) {
  uint64_t x = (uint64_t)(uintptr_t)ptr;
  x *= 0xbf58476d1ce4e5b9ull;
  x ^= x >> 31;
  return (unsigned)x;
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
