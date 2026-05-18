/*===- BranchProbability.c - Branch probability (pure C) --------*- C -*-===*/
#include "include/csupport/lbranch_lprobability.h"
#include <stdint.h>

static uint64_t scale_impl(uint64_t num, uint32_t n, uint32_t d) {
  if (!num || d == n) return num;
  if (!d) return UINT64_MAX;

  uint64_t prod_high = (num >> 32) * n;
  uint64_t prod_low = (num & UINT32_MAX) * n;

  uint32_t upper32 = (uint32_t)(prod_high >> 32);
  uint32_t lower32 = (uint32_t)(prod_low & UINT32_MAX);
  uint32_t mid32_partial = (uint32_t)(prod_high & UINT32_MAX);
  uint32_t mid32 = mid32_partial + (uint32_t)(prod_low >> 32);
  upper32 += (mid32 < mid32_partial);

  uint64_t rem = ((uint64_t)upper32 << 32) | mid32;
  uint64_t upper_q = rem / d;
  if (upper_q > UINT32_MAX) return UINT64_MAX;

  rem = ((rem % d) << 32) | lower32;
  uint64_t lower_q = rem / d;
  uint64_t q = (upper_q << 32) + lower_q;
  return q < lower_q ? UINT64_MAX : q;
}

uint64_t csupport_branch_prob_scale(uint64_t num, uint32_t n, uint32_t d) {
  return scale_impl(num, n, d);
}

uint64_t csupport_branch_prob_scale_inverse(uint64_t num, uint32_t n,
                                            uint32_t d) {
  return scale_impl(num, d, n);
}

uint32_t csupport_branch_prob_normalize(uint32_t numerator,
                                        uint32_t denominator, uint32_t D) {
  if (denominator == D)
    return numerator;
  uint64_t prob64 =
      (numerator * (uint64_t)(D) + denominator / 2) / denominator;
  return (uint32_t)(prob64);
}

uint32_t csupport_branch_prob_get64(uint64_t numerator,
                                     uint64_t denominator,
                                     uint32_t D) {
  int scale = 0;
  while (denominator > UINT32_MAX) {
    denominator >>= 1;
    scale++;
  }
  return csupport_branch_prob_normalize(
      (uint32_t)(numerator >> scale), (uint32_t)denominator, D);
}
