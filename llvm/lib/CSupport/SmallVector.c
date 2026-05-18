/*===- SmallVector.c - Dynamic array base (pure C) --------------*- C -*-===*/
#include "include/csupport/lsmall_lvector.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void report_size_overflow_c(size_t min_size, size_t max_size) {
  fprintf(stderr,
          "SmallVector unable to grow. Requested capacity (%zu) is larger than "
          "maximum value for size type (%zu)\n",
          min_size, max_size);
  abort();
}

static void report_at_maximum_capacity_c(size_t max_size) {
  fprintf(stderr,
          "SmallVector capacity unable to grow. Already at maximum size %zu\n",
          max_size);
  abort();
}

static size_t csupport_smallvec_new_capacity_u32(size_t min_size,
                                                  size_t old_capacity) {
  const size_t max_size = UINT32_MAX;
  if (min_size > max_size)
    report_size_overflow_c(min_size, max_size);
  if (old_capacity == max_size)
    report_at_maximum_capacity_c(max_size);
  size_t new_cap = 2 * old_capacity + 1;
  if (new_cap < min_size)
    new_cap = min_size;
  if (new_cap > max_size)
    new_cap = max_size;
  return new_cap;
}

static size_t csupport_smallvec_new_capacity_u64(size_t min_size,
                                                  size_t old_capacity) {
  const size_t max_size = (size_t)UINT64_MAX;
  if (min_size > max_size)
    report_size_overflow_c(min_size, max_size);
  if (old_capacity == max_size)
    report_at_maximum_capacity_c(max_size);
  size_t new_cap = 2 * old_capacity + 1;
  if (new_cap < min_size)
    new_cap = min_size;
  if (new_cap > max_size)
    new_cap = max_size;
  return new_cap;
}

static void *safe_malloc_c(size_t sz) {
  void *p = malloc(sz);
  if (!p && sz != 0) {
    fprintf(stderr, "malloc failed\n");
    abort();
  }
  return p;
}

static void *safe_realloc_c(void *ptr, size_t sz) {
  void *p = realloc(ptr, sz);
  if (!p && sz != 0) {
    fprintf(stderr, "realloc failed\n");
    abort();
  }
  return p;
}

void *csupport_smallvec_replace_alloc(void *new_elts, size_t tsize,
                                      size_t new_capacity, size_t vsize) {
  void *replacement = safe_malloc_c(new_capacity * tsize);
  if (vsize)
    memcpy(replacement, new_elts, vsize * tsize);
  free(new_elts);
  return replacement;
}

void *csupport_smallvec_malloc_for_grow_u32(void *begin, void *first_el,
                                            size_t min_size, size_t tsize,
                                            uint32_t old_capacity,
                                            size_t *out_new_capacity) {
  size_t new_cap =
      csupport_smallvec_new_capacity_u32(min_size, (size_t)old_capacity);
  void *new_elts = safe_malloc_c(new_cap * tsize);
  if (new_elts == first_el)
    new_elts = csupport_smallvec_replace_alloc(new_elts, tsize, new_cap, 0);
  *out_new_capacity = new_cap;
  return new_elts;
}

void csupport_smallvec_grow_pod_u32(void **begin_x, uint32_t *capacity,
                                    uint32_t size_val, void *first_el,
                                    size_t min_size, size_t tsize) {
  size_t new_cap =
      csupport_smallvec_new_capacity_u32(min_size, (size_t)*capacity);
  void *new_elts;
  if (*begin_x == first_el) {
    new_elts = safe_malloc_c(new_cap * tsize);
    if (new_elts == first_el)
      new_elts = csupport_smallvec_replace_alloc(new_elts, tsize, new_cap, 0);
    memcpy(new_elts, *begin_x, (size_t)size_val * tsize);
  } else {
    new_elts = safe_realloc_c(*begin_x, new_cap * tsize);
    if (new_elts == first_el)
      new_elts = csupport_smallvec_replace_alloc(new_elts, tsize, new_cap,
                                                  (size_t)size_val);
  }
  *begin_x = new_elts;
  *capacity = (uint32_t)new_cap;
}

#if SIZE_MAX > UINT32_MAX
void *csupport_smallvec_malloc_for_grow_u64(void *begin, void *first_el,
                                            size_t min_size, size_t tsize,
                                            uint64_t old_capacity,
                                            size_t *out_new_capacity) {
  size_t new_cap =
      csupport_smallvec_new_capacity_u64(min_size, (size_t)old_capacity);
  void *new_elts = safe_malloc_c(new_cap * tsize);
  if (new_elts == first_el)
    new_elts = csupport_smallvec_replace_alloc(new_elts, tsize, new_cap, 0);
  *out_new_capacity = new_cap;
  return new_elts;
}

void csupport_smallvec_grow_pod_u64(void **begin_x, uint64_t *capacity,
                                    uint64_t size_val, void *first_el,
                                    size_t min_size, size_t tsize) {
  size_t new_cap =
      csupport_smallvec_new_capacity_u64(min_size, (size_t)*capacity);
  void *new_elts;
  if (*begin_x == first_el) {
    new_elts = safe_malloc_c(new_cap * tsize);
    if (new_elts == first_el)
      new_elts = csupport_smallvec_replace_alloc(new_elts, tsize, new_cap, 0);
    memcpy(new_elts, *begin_x, (size_t)size_val * tsize);
  } else {
    new_elts = safe_realloc_c(*begin_x, new_cap * tsize);
    if (new_elts == first_el)
      new_elts = csupport_smallvec_replace_alloc(new_elts, tsize, new_cap,
                                                  (size_t)size_val);
  }
  *begin_x = new_elts;
  *capacity = (uint64_t)new_cap;
}
#endif
