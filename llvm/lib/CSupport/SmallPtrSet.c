/*===- SmallPtrSet.c - Pointer hash set (pure C) ----------------*- C -*-===*/
#include "include/csupport/lsmall_lptr_lset.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define EMPTY_MARKER   ((const void *)(intptr_t)-1)

static void *safe_malloc_c(size_t sz) {
  void *p = malloc(sz);
  if (!p && sz != 0) { fprintf(stderr, "malloc failed\n"); abort(); }
  return p;
}

static unsigned log2_ceil_u32(unsigned val) {
  if (val <= 1) return 0;
  unsigned r = 0;
  val--;
  while (val) { val >>= 1; r++; }
  return r;
}

static unsigned ptr_hash(const void *ptr) {
  unsigned v = (unsigned)(uintptr_t)ptr;
  return (v >> 4) ^ (v >> 9);
}

static const void **find_bucket_for(const void **arr, unsigned arr_size,
                                    const void *ptr) {
  unsigned mask = arr_size - 1;
  unsigned bucket = ptr_hash(ptr) & mask;
  for (;;) {
    if (arr[bucket] == EMPTY_MARKER)
      return arr + bucket;
    if (arr[bucket] == ptr)
      return arr + bucket;
    bucket = (bucket + 1) & mask;
  }
}

void csupport_sps_shrink_and_clear(const void ***cur_array,
                                   unsigned *cur_array_size,
                                   unsigned *num_entries, unsigned size) {
  free(*cur_array);
  unsigned new_size = size > 16 ? (1u << (log2_ceil_u32(size) + 1)) : 32;
  *num_entries = 0;
  *cur_array = (const void **)safe_malloc_c(sizeof(void *) * new_size);
  *cur_array_size = new_size;
  memset(*cur_array, -1, new_size * sizeof(void *));
}

void csupport_sps_grow(const void ***cur_array, unsigned *cur_array_size,
                       unsigned old_end_offset, int was_small,
                       unsigned new_size) {
  const void **old = *cur_array;
  const void **old_end = old + old_end_offset;

  const void **new_buckets =
      (const void **)safe_malloc_c(sizeof(void *) * new_size);
  *cur_array = new_buckets;
  *cur_array_size = new_size;
  memset(new_buckets, -1, new_size * sizeof(void *));

  for (const void **bp = old; bp != old_end; ++bp) {
    const void *elt = *bp;
    if (elt != EMPTY_MARKER) {
      const void **dest = find_bucket_for(new_buckets, new_size, elt);
      *dest = elt;
    }
  }
  if (!was_small)
    free(old);
}

void csupport_sps_erase_from_bucket(const void **cur_array,
                                    unsigned cur_array_size,
                                    const void **bucket) {
  unsigned mask = cur_array_size - 1;
  unsigned i = (unsigned)(bucket - cur_array);
  unsigned j = i;
  while ((j = (j + 1) & mask), cur_array[j] != EMPTY_MARKER) {
    unsigned ideal = ptr_hash(cur_array[j]) & mask;
    if (((i - ideal) & mask) < ((j - ideal) & mask)) {
      cur_array[i] = cur_array[j];
      i = j;
    }
  }
  cur_array[i] = EMPTY_MARKER;
}
