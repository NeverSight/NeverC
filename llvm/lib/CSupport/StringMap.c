/*===- StringMap.c - String hash table (pure C) -----------------*- C -*-===*/
#include "include/csupport/lstring_lmap.h"
#include "include/csupport/allocation.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned next_power_of_2_c(unsigned v) {
  unsigned power = 1;
  while (power < v) {
    if (power > UINT_MAX / 2)
      csupport_allocation_failure();
    power *= 2;
  }
  return power;
}

unsigned csupport_stringmap_min_buckets(unsigned num_entries) {
  if (num_entries == 0) return 0;
  uint64_t required = (uint64_t)num_entries * 4 / 3 + 1;
  if (required > UINT_MAX)
    csupport_allocation_failure();
  return next_power_of_2_c((unsigned)required);
}

void **csupport_stringmap_create_table(unsigned num_buckets) {
  if (num_buckets == UINT_MAX)
    csupport_allocation_failure();
  void **table = (void **)csupport_checked_calloc(
      (size_t)num_buckets + 1, sizeof(void *) + sizeof(unsigned));
  table[num_buckets] = (void *)(intptr_t)2;
  return table;
}

unsigned *csupport_stringmap_hash_table(void **table, unsigned num_buckets) {
  return (unsigned *)(table + num_buckets + 1);
}

unsigned csupport_stringmap_rehash(void ***the_table, unsigned *num_buckets,
                                  unsigned *num_items, unsigned *num_tombstones,
                                  unsigned bucket_no, void *tombstone,
                                  int force_growth) {
  unsigned new_size;
  uint64_t occupied = (uint64_t)*num_items + *num_tombstones;
  if (occupied > *num_buckets)
    csupport_allocation_failure();
  if (force_growth ||
      (uint64_t)*num_items * 4 > (uint64_t)*num_buckets * 3) {
    if (*num_buckets > UINT_MAX / 2)
      csupport_allocation_failure();
    new_size = *num_buckets * 2;
  } else if (*num_buckets - (unsigned)occupied <= *num_buckets / 8) {
    new_size = *num_buckets;
  } else {
    return bucket_no;
  }

  unsigned new_bucket_no = bucket_no;
  void **new_table = csupport_stringmap_create_table(new_size);
  unsigned *new_hash = csupport_stringmap_hash_table(new_table, new_size);
  unsigned *old_hash = csupport_stringmap_hash_table(*the_table, *num_buckets);

  for (unsigned i = 0; i < *num_buckets; ++i) {
    void *bucket = (*the_table)[i];
    if (bucket && bucket != tombstone) {
      unsigned full_hash = old_hash[i];
      unsigned nb = full_hash & (new_size - 1);
      if (new_table[nb]) {
        do {
          nb = (nb + 1) & (new_size - 1);
        } while (new_table[nb]);
      }
      new_table[nb] = bucket;
      new_hash[nb] = full_hash;
      if (i == bucket_no) new_bucket_no = nb;
    }
  }

  free(*the_table);
  *the_table = new_table;
  *num_buckets = new_size;
  *num_tombstones = 0;
  return new_bucket_no;
}
