#ifndef CSUPPORT_LSMALL_LVECTOR_H
#define CSUPPORT_LSMALL_LVECTOR_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void *csupport_smallvec_replace_alloc(void *new_elts, size_t tsize,
                                      size_t new_capacity, size_t vsize);
void *csupport_smallvec_malloc_for_grow_u32(void *begin, void *first_el,
                                            size_t min_size, size_t tsize,
                                            uint32_t old_capacity,
                                            size_t *out_new_capacity);
void csupport_smallvec_grow_pod_u32(void **begin_x, uint32_t *capacity,
                                    uint32_t size_val, void *first_el,
                                    size_t min_size, size_t tsize);
#if SIZE_MAX > UINT32_MAX
void *csupport_smallvec_malloc_for_grow_u64(void *begin, void *first_el,
                                            size_t min_size, size_t tsize,
                                            uint64_t old_capacity,
                                            size_t *out_new_capacity);
void csupport_smallvec_grow_pod_u64(void **begin_x, uint64_t *capacity,
                                    uint64_t size_val, void *first_el,
                                    size_t min_size, size_t tsize);
#endif

#ifdef __cplusplus
}
#endif
#endif
