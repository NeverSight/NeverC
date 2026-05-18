/*===- FoldingSet.c - Hash set for profiling (pure C) -----------*- C -*-===*/
#include "include/csupport/lfolding_lset.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static void *safe_calloc_c(size_t count, size_t size) {
  void *p = calloc(count, size);
  if (!p && count && size) { fprintf(stderr, "calloc failed\n"); abort(); }
  return p;
}

void **csupport_folding_set_allocate_buckets(unsigned num_buckets) {
  void **buckets = (void **)safe_calloc_c(num_buckets + 1, sizeof(void *));
  buckets[num_buckets] = (void *)(intptr_t)-1;
  return buckets;
}

void *csupport_folding_set_get_next_ptr(void *next_in_bucket) {
  if ((intptr_t)next_in_bucket & 1)
    return NULL;
  return next_in_bucket;
}

void **csupport_folding_set_get_bucket_ptr(void *next_in_bucket) {
  intptr_t ptr = (intptr_t)next_in_bucket;
  assert((ptr & 1) && "Not a bucket pointer");
  return (void **)(ptr & ~(intptr_t)1);
}

void **csupport_folding_set_get_bucket_for(unsigned hash, void **buckets,
                                           unsigned num_buckets) {
  unsigned bucket_num = hash & (num_buckets - 1);
  return buckets + bucket_num;
}

void csupport_folding_set_clear(void **buckets, unsigned num_buckets,
                                unsigned *num_nodes) {
  memset(buckets, 0, num_buckets * sizeof(void *));
  buckets[num_buckets] = (void *)(intptr_t)-1;
  *num_nodes = 0;
}

void csupport_folding_set_insert_node(void **bucket, void *node,
                                      void *(*get_next)(void *),
                                      void (*set_next)(void *, void *),
                                      unsigned *num_nodes) {
  void *next = *bucket;
  if (!next)
    next = (void *)((intptr_t)bucket | 1);
  set_next(node, next);
  *bucket = node;
  (*num_nodes)++;
}

int csupport_folding_set_remove_node(void *node,
                                     void *(*get_next)(void *),
                                     void (*set_next)(void *, void *),
                                     unsigned *num_nodes) {
  void *ptr = get_next(node);
  if (!ptr)
    return 0;

  (*num_nodes)--;
  set_next(node, NULL);
  void *node_next_ptr = ptr;

  for (;;) {
    void *next = csupport_folding_set_get_next_ptr(ptr);
    if (next) {
      ptr = get_next(next);
      if (ptr == node) {
        set_next(next, node_next_ptr);
        return 1;
      }
    } else {
      void **bucket = csupport_folding_set_get_bucket_ptr(ptr);
      ptr = *bucket;
      if (ptr == node) {
        *bucket = node_next_ptr;
        return 1;
      }
    }
  }
}

unsigned csupport_folding_set_id_add_string(unsigned *bits, unsigned bits_cap,
                                            unsigned bits_size,
                                            const char *str, unsigned str_len,
                                            int is_big_endian) {
  unsigned pos = bits_size;

  if (pos >= bits_cap) return pos;
  bits[pos++] = str_len;

  if (str_len == 0) return pos;

  unsigned units = str_len / 4;
  const unsigned char *s = (const unsigned char *)str;

  if (is_big_endian) {
    unsigned i;
    for (i = 0; i < units && pos < bits_cap; i++, pos++) {
      unsigned base = i * 4;
      bits[pos] = ((unsigned)s[base] << 24) |
                  ((unsigned)s[base + 1] << 16) |
                  ((unsigned)s[base + 2] << 8) |
                  (unsigned)s[base + 3];
    }
  } else {
    if (((intptr_t)str & 3) == 0) {
      const unsigned *base = (const unsigned *)str;
      unsigned i;
      for (i = 0; i < units && pos < bits_cap; i++, pos++)
        bits[pos] = base[i];
    } else {
      unsigned i;
      for (i = 0; i < units && pos < bits_cap; i++, pos++) {
        unsigned base = i * 4;
        bits[pos] = (unsigned)s[base] |
                    ((unsigned)s[base + 1] << 8) |
                    ((unsigned)s[base + 2] << 16) |
                    ((unsigned)s[base + 3] << 24);
      }
    }
  }

  unsigned remaining = str_len - units * 4;
  if (remaining > 0 && pos < bits_cap) {
    unsigned v = 0;
    switch (remaining) {
    case 3:
      v = (v << 8) | s[str_len - 3];
      __attribute__((fallthrough));
    case 2:
      v = (v << 8) | s[str_len - 2];
      __attribute__((fallthrough));
    case 1: v = (v << 8) | s[str_len - 1]; break;
    }
    bits[pos++] = v;
  }

  return pos;
}
