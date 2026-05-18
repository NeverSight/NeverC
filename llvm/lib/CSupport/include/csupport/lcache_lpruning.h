#ifndef CSUPPORT_LCACHE_LPRUNING_H
#define CSUPPORT_LCACHE_LPRUNING_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint64_t max_size_bytes;
  unsigned max_age_seconds;
  unsigned percentage;
  int prune_after;
} csupport_cache_pruning_policy_t;

void csupport_cache_pruning_policy_default(csupport_cache_pruning_policy_t *p);

int csupport_cache_pruning_parse_percentage(const char *str, size_t len,
                                            unsigned *out);

int64_t csupport_parse_duration_seconds(const char *str, size_t len);
int csupport_parse_cache_size_bytes(const char *str, size_t len, uint64_t *out);
int csupport_parse_cache_policy_kv(const char *key, size_t key_len,
                                   const char *val, size_t val_len,
                                   csupport_cache_pruning_policy_t *policy);

#ifdef __cplusplus
}
#endif
#endif
