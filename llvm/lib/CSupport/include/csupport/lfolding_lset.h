#ifndef CSUPPORT_LFOLDING_LSET_H
#define CSUPPORT_LFOLDING_LSET_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void **csupport_folding_set_allocate_buckets(unsigned num_buckets);
void *csupport_folding_set_get_next_ptr(void *next_in_bucket);
void **csupport_folding_set_get_bucket_ptr(void *next_in_bucket);
void **csupport_folding_set_get_bucket_for(unsigned hash, void **buckets,
                                           unsigned num_buckets);

void csupport_folding_set_clear(void **buckets, unsigned num_buckets,
                                unsigned *num_nodes);
void csupport_folding_set_insert_node(void **bucket, void *node,
                                      void *(*get_next)(void *),
                                      void (*set_next)(void *, void *),
                                      unsigned *num_nodes);
int csupport_folding_set_remove_node(void *node, void *(*get_next)(void *),
                                     void (*set_next)(void *, void *),
                                     unsigned *num_nodes);

unsigned csupport_folding_set_id_add_string(unsigned *bits, unsigned bits_cap,
                                            unsigned bits_size, const char *str,
                                            unsigned str_len,
                                            int is_big_endian);

#ifdef __cplusplus
}
#endif
#endif
