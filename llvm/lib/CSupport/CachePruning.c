/*===- CachePruning.c - Cache pruning utilities (pure C) -------*- C -*-===*/
#include "include/csupport/lcache_lpruning.h"
#include <string.h>

void csupport_cache_pruning_policy_default(csupport_cache_pruning_policy_t *p) {
  p->max_size_bytes = 0;
  p->max_age_seconds = 7 * 24 * 3600;
  p->percentage = 75;
  p->prune_after = 1200;
}

int csupport_cache_pruning_parse_percentage(const char *str, size_t len,
                                            unsigned *out) {
  if (len == 0) return 0;
  unsigned val = 0;
  for (size_t i = 0; i < len; i++) {
    if (str[i] == '%') {
      if (i + 1 != len) return 0;
      break;
    }
    if (str[i] < '0' || str[i] > '9') return 0;
    val = val * 10 + (unsigned)(str[i] - '0');
    if (val > 100) return 0;
  }
  *out = val;
  return 1;
}

int64_t csupport_parse_duration_seconds(const char *str, size_t len) {
  if (len < 2) return -1;
  char unit = str[len - 1];
  uint64_t num = 0;
  for (size_t i = 0; i < len - 1; i++) {
    if (str[i] < '0' || str[i] > '9') return -1;
    num = num * 10 + (uint64_t)(str[i] - '0');
  }
  switch (unit) {
  case 's': return (int64_t)num;
  case 'm': return (int64_t)(num * 60);
  case 'h': return (int64_t)(num * 3600);
  default: return -1;
  }
}

int csupport_parse_cache_size_bytes(const char *str, size_t len,
                                     uint64_t *out) {
  if (len == 0) return 0;
  uint64_t mult = 1;
  size_t num_end = len;
  char last = str[len - 1];
  if (last == 'k' || last == 'K') { mult = 1024; num_end = len - 1; }
  else if (last == 'm' || last == 'M') { mult = 1024ULL * 1024; num_end = len - 1; }
  else if (last == 'g' || last == 'G') { mult = 1024ULL * 1024 * 1024; num_end = len - 1; }
  uint64_t val = 0;
  for (size_t i = 0; i < num_end; i++) {
    if (str[i] < '0' || str[i] > '9') return 0;
    val = val * 10 + (uint64_t)(str[i] - '0');
  }
  *out = val * mult;
  return 1;
}

int csupport_parse_cache_policy_kv(const char *key, size_t key_len,
                                    const char *val, size_t val_len,
                                    csupport_cache_pruning_policy_t *policy) {
  if (key_len == 14 && memcmp(key, "prune_interval", 14) == 0) {
    int64_t s = csupport_parse_duration_seconds(val, val_len);
    if (s < 0) return -1;
    policy->prune_after = (int)s;
    return 0;
  }
  if (key_len == 11 && memcmp(key, "prune_after", 11) == 0) {
    int64_t s = csupport_parse_duration_seconds(val, val_len);
    if (s < 0) return -1;
    policy->max_age_seconds = (unsigned)s;
    return 0;
  }
  if (key_len == 10 && memcmp(key, "cache_size", 10) == 0) {
    unsigned pct = 0;
    if (!csupport_cache_pruning_parse_percentage(val, val_len, &pct))
      return -1;
    policy->percentage = pct;
    return 0;
  }
  if (key_len == 16 && memcmp(key, "cache_size_bytes", 16) == 0) {
    uint64_t bytes = 0;
    if (!csupport_parse_cache_size_bytes(val, val_len, &bytes))
      return -1;
    policy->max_size_bytes = bytes;
    return 0;
  }
  return -2;
}
