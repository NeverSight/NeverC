/*===- StringMap.c - String hash table (pure C) -----------------*- C -*-===*/
#include "include/csupport/lstring_lmap.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void *safe_calloc_c(size_t count, size_t size) {
  void *p = calloc(count, size);
  if (!p && count && size) { fprintf(stderr, "calloc failed\n"); abort(); }
  return p;
}

static unsigned next_power_of_2_c(unsigned v) {
  v--;
  v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
  return v + 1;
}

unsigned csupport_stringmap_min_buckets(unsigned num_entries) {
  if (num_entries == 0) return 0;
  return next_power_of_2_c(num_entries * 4 / 3 + 1);
}

void **csupport_stringmap_create_table(unsigned num_buckets) {
  void **table = (void **)safe_calloc_c(
      num_buckets + 1, sizeof(void *) + sizeof(unsigned));
  table[num_buckets] = (void *)(intptr_t)2;
  return table;
}

unsigned *csupport_stringmap_hash_table(void **table, unsigned num_buckets) {
  return (unsigned *)(table + num_buckets + 1);
}

unsigned csupport_stringmap_rehash(void ***the_table, unsigned *num_buckets,
                                  unsigned *num_items, unsigned *num_tombstones,
                                  unsigned bucket_no) {
  unsigned new_size;
  if (*num_items * 4 > *num_buckets * 3)
    new_size = *num_buckets * 2;
  else if (*num_buckets - (*num_items + *num_tombstones) <= *num_buckets / 8)
    new_size = *num_buckets;
  else
    return bucket_no;

  unsigned new_bucket_no = bucket_no;
  void **new_table = csupport_stringmap_create_table(new_size);
  unsigned *new_hash = csupport_stringmap_hash_table(new_table, new_size);
  unsigned *old_hash = csupport_stringmap_hash_table(*the_table, *num_buckets);

  for (unsigned i = 0; i < *num_buckets; ++i) {
    void *bucket = (*the_table)[i];
    if (bucket && bucket != (void *)(intptr_t)-1) {
      unsigned full_hash = old_hash[i];
      unsigned nb = full_hash & (new_size - 1);
      if (new_table[nb]) {
        unsigned probe = 1;
        do { nb = (nb + probe++) & (new_size - 1); } while (new_table[nb]);
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
