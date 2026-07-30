/*===- SmallPtrSet.c - Pointer hash set (pure C) ----------------*- C -*-===*/
#include "include/csupport/lsmall_lptr_lset.h"
#include "include/csupport/allocation.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define EMPTY_MARKER   ((const void *)(intptr_t)-1)
#define TOMBSTONE_MARKER ((const void *)(intptr_t)-2)

static unsigned log2_ceil_u32(unsigned val) {
  if (val <= 1) return 0;
  unsigned r = 0;
  val--;
  while (val) { val >>= 1; r++; }
  return r;
}

static const void **find_bucket_for(const void **arr, unsigned arr_size,
                                    const void *ptr) {
  unsigned mask = arr_size - 1;
  unsigned bucket = csupport_sps_hash_pointer(ptr) & mask;
  const void **tombstone = NULL;
  for (;;) {
    if (arr[bucket] == EMPTY_MARKER)
      return tombstone ? tombstone : (arr + bucket);
    if (arr[bucket] == ptr)
      return (arr + bucket);
    if (arr[bucket] == TOMBSTONE_MARKER && !tombstone)
      tombstone = (arr + bucket);
    bucket = (bucket + 1) & mask;
  }
}

void csupport_sps_erase_from_bucket(const void **cur_array,
                                    unsigned cur_array_size,
                                    const void **bucket) {
  unsigned mask = cur_array_size - 1;
  unsigned i = (unsigned)(bucket - cur_array);
  unsigned j = i;
  while ((j = (j + 1) & mask), cur_array[j] != EMPTY_MARKER) {
    unsigned ideal = csupport_sps_hash_pointer(cur_array[j]) & mask;
    if (((i - ideal) & mask) < ((j - ideal) & mask)) {
      cur_array[i] = cur_array[j];
      i = j;
    }
  }
  cur_array[i] = EMPTY_MARKER;
}

void csupport_sps_shrink_and_clear(const void ***cur_array,
                                   unsigned *cur_array_size,
                                   unsigned *num_non_empty,
                                   unsigned *num_tombstones, unsigned size) {
  free(*cur_array);
  unsigned new_size = 32;
  if (size > 16) {
    unsigned exponent = log2_ceil_u32(size);
    if (exponent >= sizeof(unsigned) * CHAR_BIT - 1)
      csupport_allocation_failure();
    new_size = 1u << (exponent + 1);
  }
  *num_non_empty = 0;
  *num_tombstones = 0;
  *cur_array =
      (const void **)csupport_checked_malloc(new_size, sizeof(void *));
  *cur_array_size = new_size;
  memset(*cur_array, -1, new_size * sizeof(void *));
}

void csupport_sps_grow(const void ***cur_array, unsigned *cur_array_size,
                       unsigned *num_non_empty, unsigned *num_tombstones,
                       const void **small_array, unsigned new_size) {
  const void **old = *cur_array;
  const void **old_end = old + *cur_array_size;
  int was_small = (*cur_array == small_array);

  const void **new_buckets =
      (const void **)csupport_checked_malloc(new_size, sizeof(void *));
  *cur_array = new_buckets;
  *cur_array_size = new_size;
  memset(new_buckets, -1, new_size * sizeof(void *));

  for (const void **bp = old; bp != old_end; ++bp) {
    const void *elt = *bp;
    if (elt != TOMBSTONE_MARKER && elt != EMPTY_MARKER) {
      const void **dest = find_bucket_for(new_buckets, new_size, elt);
      *dest = elt;
    }
  }
  if (!was_small)
    free(old);
  *num_non_empty -= *num_tombstones;
  *num_tombstones = 0;
}

int csupport_sps_insert_big(const void ***cur_array, unsigned *cur_array_size,
                            unsigned *num_non_empty, unsigned *num_tombstones,
                            const void **small_array, const void *ptr,
                            const void ***out_bucket) {
  unsigned sz = *num_non_empty - *num_tombstones;
  if (sz >= (*cur_array_size / 4) * 3) {
    if (*cur_array_size > UINT_MAX / 2)
      csupport_allocation_failure();
    unsigned new_sz = *cur_array_size < 64 ? 128 : *cur_array_size * 2;
    csupport_sps_grow(cur_array, cur_array_size, num_non_empty, num_tombstones,
                      small_array, new_sz);
  } else if (*cur_array_size - *num_non_empty < *cur_array_size / 8) {
    csupport_sps_grow(cur_array, cur_array_size, num_non_empty, num_tombstones,
                      small_array, *cur_array_size);
  }

  const void **bucket = find_bucket_for(*cur_array, *cur_array_size, ptr);
  if (*bucket == ptr) {
    *out_bucket = bucket;
    return 0;
  }
  if (*bucket == TOMBSTONE_MARKER)
    (*num_tombstones)--;
  else
    (*num_non_empty)++;
  *bucket = ptr;
  *out_bucket = bucket;
  return 1;
}
