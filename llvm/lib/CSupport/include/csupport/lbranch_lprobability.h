#ifndef CSUPPORT_LBRANCH_LPROBABILITY_H
#define CSUPPORT_LBRANCH_LPROBABILITY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

uint64_t csupport_branch_prob_scale(uint64_t num, uint32_t n, uint32_t d);
uint64_t csupport_branch_prob_scale_inverse(uint64_t num, uint32_t n,
                                            uint32_t d);

/* Normalize numerator/denominator to fixed denominator D=0x80000000. */
uint32_t csupport_branch_prob_normalize(uint32_t numerator,
                                        uint32_t denominator, uint32_t D);

/* Scale denominator down to fit uint32_t. Returns scaled N. */
uint32_t csupport_branch_prob_get64(uint64_t numerator, uint64_t denominator,
                                    uint32_t D);

#ifdef __cplusplus
}
#endif
#endif
