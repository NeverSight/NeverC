#ifndef CSUPPORT_LSTRING_LMAP_H
#define CSUPPORT_LSTRING_LMAP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

unsigned csupport_stringmap_min_buckets(unsigned num_entries);
void **csupport_stringmap_create_table(unsigned num_buckets);
unsigned *csupport_stringmap_hash_table(void **table, unsigned num_buckets);
unsigned csupport_stringmap_rehash(void ***the_table, unsigned *num_buckets,
                                   unsigned *num_items,
                                   unsigned *num_tombstones,
                                   unsigned bucket_no, void *tombstone,
                                   int force_growth);

#ifdef __cplusplus
}
#endif
#endif
